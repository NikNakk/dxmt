/* Opt-in remote presentation. Protocol v2 properties are serialized by a
 * named per-HWND mutex; no lock is held while entering AppKit. The worker and
 * its DLL are process-lifetime resources (pinned outside the loader lock).
 * Geometry/discovery polling is independent of frame presentation.
 */
#include "d3d11_remote_layer.hpp"
#include "d3d11_remote_protocol.hpp"
#include "log/log.hpp"
#include "util_env.hpp"
#include <algorithm>
#include <cwchar>
#include <cstring>
#include <mutex>
#include <vector>

namespace dxmt {
namespace {
constexpr const wchar_t *kId = L"DXMT_REMOTE_LAYER_V2_ID";
constexpr const wchar_t *kPid = L"DXMT_REMOTE_LAYER_V2_PID";
constexpr const wchar_t *kGeneration = L"DXMT_REMOTE_LAYER_V2_GENERATION";
constexpr const wchar_t *kBirthLow = L"DXMT_REMOTE_LAYER_V2_BIRTH_LOW";
constexpr const wchar_t *kBirthHigh = L"DXMT_REMOTE_LAYER_V2_BIRTH_HIGH";
constexpr const wchar_t *kWidth = L"DXMT_REMOTE_LAYER_V2_WIDTH";
constexpr const wchar_t *kHeight = L"DXMT_REMOTE_LAYER_V2_HEIGHT";
constexpr const wchar_t *kAck = L"DXMT_REMOTE_LAYER_V2_ACK";
constexpr const wchar_t *kAckPid = L"DXMT_REMOTE_LAYER_V2_ACK_PID";
constexpr DWORD kPollMs = 100, kRetirementMs = 2000;
LONG next_generation = 0;
std::mutex retired_mutex;
struct Retired {
  HWND hwnd;
  uint32_t id, generation;
  WMT::Object view;
  ULONGLONG deadline, acknowledged = 0;
};
std::vector<Retired> retired;
bool retirement_running = false;

bool Debug() { return env::getEnvVar("DXMT_REMOTE_LAYER_DEBUG") == "1"; }
uint32_t Property(HWND hwnd, const wchar_t *name) {
  return static_cast<uint32_t>(reinterpret_cast<uintptr_t>(GetPropW(hwnd, name)));
}
bool SetProperty(HWND hwnd, const wchar_t *name, uint32_t value) {
  return SetPropW(hwnd, name, reinterpret_cast<HANDLE>(static_cast<uintptr_t>(value)));
}
struct WindowLock {
  HANDLE handle = nullptr;
  bool locked = false;
  explicit WindowLock(HWND hwnd) {
    wchar_t name[96];
    swprintf(name, 96, L"Local\\DXMT_REMOTE_LAYER_V2_WINDOW_%lx", (unsigned long)(uintptr_t)hwnd);
    handle = CreateMutexW(nullptr, FALSE, name);
    if (handle) {
      DWORD result = WaitForSingleObject(handle, 100);
      locked = result == WAIT_OBJECT_0 || result == WAIT_ABANDONED;
    }
  }
  ~WindowLock() {
    if (locked) ReleaseMutex(handle);
    if (handle) CloseHandle(handle);
  }
};
HWND Root(HWND hwnd) { return GetAncestor(hwnd, GA_ROOT); }
DWORD Owner(HWND hwnd) {
  DWORD pid = 0;
  GetWindowThreadProcessId(Root(hwnd), &pid);
  return pid;
}
void EventName(DWORD pid, wchar_t *name) {
  swprintf(name, 96, L"Local\\DXMT_REMOTE_LAYER_V2_%lu", (unsigned long)pid);
}
void SignalOwner(HWND hwnd) {
  DWORD owner = Owner(hwnd);
  if (!owner) return;
  wchar_t name[96];
  EventName(owner, name);
  HANDLE event = OpenEventW(EVENT_MODIFY_STATE, FALSE, name);
  if (event) {
    SetEvent(event);
    CloseHandle(event);
  } else if (Debug()) {
    WARN("RemoteLayer: root host pid ", owner, " not participating for child ", hwnd);
  }
}
struct Publication : RemoteLayerIdentity {
  HWND hwnd, root;
  uint32_t width, height;
  std::vector<unsigned> paint_path = {};
};
bool Same(const Publication &a, const Publication &b) {
  return a.hwnd == b.hwnd && a.root == b.root && a.id == b.id &&
         static_cast<const RemoteLayerIdentity &>(a) == static_cast<const RemoteLayerIdentity &>(b);
}
Publication Read(HWND hwnd) {
  return {{Property(hwnd, kId), Property(hwnd, kPid), Property(hwnd, kGeneration), Property(hwnd, kBirthLow), Property(hwnd, kBirthHigh)}, hwnd, Root(hwnd), Property(hwnd, kWidth), Property(hwnd, kHeight)};
}
struct Hosted {
  Publication publication;
  WMT::Object view;
  HANDLE source;
  WMTRemotePlacement placement = {};
  bool placed = false;
};
struct Scan { std::vector<Publication> wanted; bool complete = true; };
BOOL CALLBACK ScanChild(HWND hwnd, LPARAM param) {
  auto &scan = *reinterpret_cast<Scan *>(param);
  if (!GetPropW(hwnd, kGeneration) || Owner(hwnd) != GetCurrentProcessId()) return TRUE;
  WindowLock lock(hwnd);
  if (!lock.locked) { scan.complete = false; return TRUE; }
  auto p = Read(hwnd);
  if (p.id && p.pid && p.generation) scan.wanted.push_back(p);
  return TRUE;
}
BOOL CALLBACK ScanRoot(HWND hwnd, LPARAM param) {
  ScanChild(hwnd, param);
  // EnumChildWindows already visits every descendant. Never recurse here.
  EnumChildWindows(hwnd, ScanChild, param);
  return TRUE;
}
// Lexicographic ancestor/sibling order preserves whole sibling subtrees. The
// Win32 first sibling is foremost; parents paint behind their descendants.
std::vector<unsigned> PaintPath(HWND hwnd) {
  std::vector<unsigned> path;
  for (HWND root = Root(hwnd); hwnd && hwnd != root; hwnd = GetParent(hwnd)) {
    unsigned rank = 0;
    for (HWND sibling = GetWindow(hwnd, GW_HWNDPREV); sibling; sibling = GetWindow(sibling, GW_HWNDPREV)) ++rank;
    path.push_back(rank);
  }
  std::reverse(path.begin(), path.end());
  return path;
}
bool InFront(const Publication &a, const Publication &b) {
  const auto &x = a.paint_path, &y = b.paint_path;
  size_t count = std::min(x.size(), y.size());
  for (size_t i = 0; i < count; ++i)
    if (x[i] != y[i]) return x[i] < y[i];
  return x.size() > y.size();
}
bool Placement(const Publication &p, unsigned order, WMTRemotePlacement &out) {
  RECT client = {}, root = {}, clip = {};
  if (!GetClientRect(p.root, &root) || !GetClientRect(p.hwnd, &client)) return false;
  POINT points[2] = {{client.left, client.top}, {client.right, client.bottom}};
  // MapWindowPoints returning zero may mean a legitimate zero displacement.
  SetLastError(0);
  if (!MapWindowPoints(p.hwnd, p.root, points, 2) && GetLastError()) return false;
  client = {points[0].x, points[0].y, points[1].x, points[1].y};
  clip = client;
  IntersectRect(&clip, &clip, &root);
  for (HWND parent = GetParent(p.hwnd); parent && parent != p.root && p.hwnd != p.root;
       parent = GetParent(parent)) {
    RECT r = {};
    if (!GetClientRect(parent, &r)) return false;
    POINT pp[2] = {{r.left, r.top}, {r.right, r.bottom}};
    SetLastError(0);
    if (!MapWindowPoints(parent, p.root, pp, 2) && GetLastError()) return false;
    r = {pp[0].x, pp[0].y, pp[1].x, pp[1].y};
    IntersectRect(&clip, &clip, &r);
  }
  out = {client.left, client.top, client.right - client.left, client.bottom - client.top,
         clip.left, clip.top, clip.right - clip.left, clip.bottom - clip.top,
         root.right - root.left, root.bottom - root.top, order,
         static_cast<uint32_t>(IsWindowVisible(p.hwnd) && !IsIconic(p.root)), p.width, p.height};
  return true;
}
void DrainRetired() {
  std::vector<WMT::Object> release;
  {
    std::lock_guard<std::mutex> guard(retired_mutex);
    ULONGLONG now = GetTickCount64();
    for (auto it = retired.begin(); it != retired.end();) {
      bool dead = !IsWindow(it->hwnd);
      WindowLock lock(it->hwnd);
      if (lock.locked) {
        auto current = Read(it->hwnd);
        bool successor = current.id && !(current.pid == GetCurrentProcessId() &&
                         current.generation == it->generation && current.id == it->id);
        if (successor && Property(it->hwnd, kAck) == current.generation &&
            Property(it->hwnd, kAckPid) == current.pid && !it->acknowledged)
          it->acknowledged = now;
      }
      if (lock.locked && RemoteLayerRetirementReady(now, it->deadline, it->acknowledged, dead)) {
        if (Property(it->hwnd, kPid) == GetCurrentProcessId() && Property(it->hwnd, kId) == it->id &&
            Property(it->hwnd, kGeneration) == it->generation) {
          RemovePropW(it->hwnd, kGeneration); RemovePropW(it->hwnd, kId); RemovePropW(it->hwnd, kPid);
          RemovePropW(it->hwnd, kAck); RemovePropW(it->hwnd, kAckPid);
        }
        if (Debug()) Logger::info(str::format("RemoteLayer: source release child ", it->hwnd,
            " context ", it->id, " generation ", it->generation, " acknowledged ", it->acknowledged != 0));
        release.push_back(it->view);
        it = retired.erase(it);
      } else ++it;
    }
  }
  for (auto view : release) WMT::ReleaseMetalView(view);
}
DWORD WINAPI RetirementThread(LPVOID) {
  for (;;) {
    DrainRetired();
    {
      std::lock_guard<std::mutex> guard(retired_mutex);
      if (retired.empty()) { retirement_running = false; return 0; }
    }
    Sleep(kPollMs);
  }
}
DWORD WINAPI HostThread(LPVOID param) {
  HANDLE event = reinterpret_cast<HANDLE>(param);
  std::vector<Hosted> hosted;
  for (;;) {
    DWORD result = WaitForSingleObject(event, kPollMs);
    if (result == WAIT_FAILED) break;
    DrainRetired();
    Scan scan;
    if (!EnumWindows(ScanRoot, reinterpret_cast<LPARAM>(&scan))) scan.complete = false;
    for (auto &p : scan.wanted) p.paint_path = PaintPath(p.hwnd);
    std::stable_sort(scan.wanted.begin(), scan.wanted.end(), InFront);
    // Install replacements before releasing predecessors. Failed installs keep
    // the previous host until retry or the publication/window disappears.
    for (size_t i = 0; i < scan.wanted.size(); ++i) {
      auto &want = scan.wanted[i];
      auto it = std::find_if(hosted.begin(), hosted.end(), [&](auto &h) { return Same(h.publication, want); });
      if (it == hosted.end()) {
        HANDLE source = OpenProcess(SYNCHRONIZE | PROCESS_QUERY_LIMITED_INFORMATION, FALSE, want.pid);
        FILETIME birth = {}, exit = {}, kernel = {}, user = {};
        if (!source || WaitForSingleObject(source, 0) != WAIT_TIMEOUT ||
            !GetProcessTimes(source, &birth, &exit, &kernel, &user) ||
            birth.dwLowDateTime != want.birth_low || birth.dwHighDateTime != want.birth_high) {
          if (source) CloseHandle(source);
          continue;
        }
        WMTRemotePlacement placement = {};
        if (!Placement(want, scan.wanted.size() - i, placement)) { CloseHandle(source); continue; }
        WMT::Object view = WMT::HostRemoteLayer(reinterpret_cast<intptr_t>(want.root), want.id);
        if (view == nullptr) { CloseHandle(source); continue; }
        WMTUpdateRemoteLayerHost(view.handle, &placement);
        bool accepted = false;
        {
          WindowLock lock(want.hwnd);
          if (lock.locked && Same(Read(want.hwnd), want)) {
            accepted = SetProperty(want.hwnd, kAckPid, want.pid) && SetProperty(want.hwnd, kAck, want.generation);
          }
        }
        if (!accepted) { WMT::ReleaseMetalView(view); CloseHandle(source); continue; }
        for (auto old = hosted.begin(); old != hosted.end();) {
          if (old->publication.hwnd == want.hwnd) {
            WMT::ReleaseMetalView(old->view);
            CloseHandle(old->source);
            old = hosted.erase(old);
          } else ++old;
        }
        hosted.push_back({want, view, source, placement, true});
        it = hosted.end() - 1;
        if (Debug()) Logger::info(str::format("RemoteLayer: host ", GetCurrentProcessId(), " root ", want.root,
            " child ", want.hwnd, " source ", want.pid, " context ", want.id, " generation ", want.generation,
            " live hosts ", hosted.size()));
      }
      WMTRemotePlacement placement = {};
      if (Placement(want, scan.wanted.size() - i, placement) &&
          (!it->placed || memcmp(&placement, &it->placement, sizeof(placement)))) {
        WMTUpdateRemoteLayerHost(it->view.handle, &placement);
        it->placement = placement;
        it->placed = true;
      }
    }
    for (auto it = hosted.begin(); it != hosted.end();) {
      bool wanted = std::any_of(scan.wanted.begin(), scan.wanted.end(), [&](auto &p) { return p.hwnd == it->publication.hwnd; });
      if ((!wanted && scan.complete) || !IsWindow(it->publication.hwnd) ||
          Root(it->publication.hwnd) != it->publication.root || WaitForSingleObject(it->source, 0) != WAIT_TIMEOUT) {
        if (Debug()) Logger::info(str::format("RemoteLayer: remove child ", it->publication.hwnd,
            " context ", it->publication.id, " generation ", it->publication.generation));
        WMT::ReleaseMetalView(it->view);
        CloseHandle(it->source);
        it = hosted.erase(it);
      } else ++it;
    }
  }
  for (auto &h : hosted) { WMT::ReleaseMetalView(h.view); CloseHandle(h.source); }
  CloseHandle(event);
  return 0;
}
DWORD WINAPI Bootstrap(LPVOID) {
  // The module was pinned before queueing; no callback can outlive its code.
  StartRemoteLayerHost();
  return 0;
}
} // namespace

bool CrossProcessSwapchainEnabled() {
  auto mode = env::getEnvVar("DXMT_CROSS_PROCESS_SWAPCHAIN");
  return mode == "offscreen" || mode == "host";
}
uint32_t PublishRemoteLayer(HWND hwnd, uint32_t id, uint32_t width, uint32_t height) {
  WindowLock lock(hwnd);
  if (!lock.locked || !IsWindow(hwnd) || !Root(hwnd) || !id) return 0;
  uint32_t generation = static_cast<uint32_t>(InterlockedIncrement(&next_generation));
  if (!generation) generation = static_cast<uint32_t>(InterlockedIncrement(&next_generation));
  RemovePropW(hwnd, kAck);
  RemovePropW(hwnd, kAckPid);
  // Generation is the commit marker, written last. A failed publication must
  // not leave a mixture of old/new property values visible to a reader.
  FILETIME birth = {}, exit = {}, kernel = {}, user = {};
  if (!GetProcessTimes(GetCurrentProcess(), &birth, &exit, &kernel, &user)) return 0;
  RemovePropW(hwnd, kGeneration);
  if (!SetProperty(hwnd, kBirthLow, birth.dwLowDateTime) || !SetProperty(hwnd, kBirthHigh, birth.dwHighDateTime) ||
      !SetProperty(hwnd, kWidth, width) || !SetProperty(hwnd, kHeight, height) ||
      !SetProperty(hwnd, kPid, GetCurrentProcessId()) || !SetProperty(hwnd, kId, id) ||
      !SetProperty(hwnd, kGeneration, generation)) {
    RemovePropW(hwnd, kId); RemovePropW(hwnd, kPid); RemovePropW(hwnd, kGeneration);
    WARN("RemoteLayer: publication failed for child ", hwnd, ": ", GetLastError());
    return 0;
  }
  if (Debug()) Logger::info(str::format("RemoteLayer: publish child ", hwnd, " root ", Root(hwnd),
      " host ", Owner(hwnd), " source ", GetCurrentProcessId(), " context ", id, " generation ", generation));
  SignalOwner(hwnd);
  return generation;
}
void UpdateRemoteLayerSize(HWND hwnd, uint32_t id, uint32_t generation, uint32_t width, uint32_t height) {
  WindowLock lock(hwnd);
  if (lock.locked && Property(hwnd, kPid) == GetCurrentProcessId() && Property(hwnd, kId) == id &&
      Property(hwnd, kGeneration) == generation) {
    SetProperty(hwnd, kWidth, width);
    SetProperty(hwnd, kHeight, height);
    SignalOwner(hwnd);
  }
}
void RetireRemoteLayer(HWND hwnd, uint32_t id, uint32_t generation, WMT::Object view) {
  // Transfer the source reference to the worker BEFORE any detach. Each
  // child/context has its own deadline and acknowledgement, never a global slot.
  bool emergency_drain = false;
  {
    std::lock_guard<std::mutex> guard(retired_mutex);
    retired.push_back({hwnd, id, generation, view, GetTickCount64() + kRetirementMs});
    if (Debug()) Logger::info(str::format("RemoteLayer: retire child ", hwnd, " context ", id,
        " generation ", generation, " pending sources ", retired.size()));
    if (!retirement_running) {
      HANDLE thread = CreateThread(nullptr, 0, RetirementThread, nullptr, 0, nullptr);
      if (thread) { retirement_running = true; CloseHandle(thread); }
      else if (QueueUserWorkItem(RetirementThread, nullptr, WT_EXECUTEDEFAULT)) {
        retirement_running = true;
      } else {
        WARN("RemoteLayer: retirement worker unavailable, draining synchronously: ", GetLastError());
        emergency_drain = true;
      }
    }
  }
  // Keep the publication and source alive during the bounded replacement
  // window. DrainRetired conditionally withdraws only this exact generation.
  SignalOwner(hwnd);
  if (emergency_drain) {
    // Resource exhaustion must not strand a retained CAContext indefinitely.
    // This exceptional destructor path waits through the same bounded grace,
    // outside all locks. Another successful worker can take over at any time.
    for (;;) {
      DrainRetired();
      {
        std::lock_guard<std::mutex> guard(retired_mutex);
        if (retired.empty() || retirement_running) break;
      }
      Sleep(kPollMs);
    }
  }
}
void StartRemoteLayerHost() {
  if (env::getEnvVar("DXMT_CROSS_PROCESS_SWAPCHAIN") != "host") return;
  wchar_t name[96];
  EventName(GetCurrentProcessId(), name);
  HANDLE event = CreateEventW(nullptr, FALSE, TRUE, name);
  if (!event) { WARN("RemoteLayer: event creation failed: ", GetLastError()); return; }
  if (GetLastError() == ERROR_ALREADY_EXISTS) { CloseHandle(event); return; }
  HANDLE thread = CreateThread(nullptr, 0, HostThread, event, 0, nullptr);
  if (thread) CloseHandle(thread);
  else { WARN("RemoteLayer: thread creation failed: ", GetLastError()); CloseHandle(event); }
}
void QueueRemoteLayerHost() {
  // Pin before queueing so FreeLibrary cannot unmap a pending callback.
  // This explicit opt-in trades unloadability for safe process-lifetime hosting.
  if (env::getEnvVar("DXMT_CROSS_PROCESS_SWAPCHAIN") != "host") return;
  HMODULE module = nullptr;
  if (!GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_PIN,
      reinterpret_cast<LPCWSTR>(&Bootstrap), &module)) return;
  QueueUserWorkItem(Bootstrap, nullptr, WT_EXECUTEDEFAULT);
}
} // namespace dxmt
