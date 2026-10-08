/*
 * Cross-process presentation (DXMT_CROSS_PROCESS_SWAPCHAIN=host).
 *
 * A swapchain for another process's window (Chromium's GPU process drawing for
 * its browser process) renders into a layer attached to a CAContext. It
 * publishes the context id as a property of the window and signals an event
 * named after the window's process. A thread in that process, started when
 * d3d11.dll loads there, then shows the context in the window through a
 * CALayerHost, and removes it when the property goes. Nothing is done per
 * frame.
 */
#include "d3d11_remote_layer.hpp"
#include "Metal.hpp"
#include "log/log.hpp"
#include "util_env.hpp"
#include <cwchar>
#include <vector>

namespace dxmt {

namespace {

constexpr const wchar_t *kProperty = L"DXMT_REMOTE_LAYER";

void
EventName(DWORD process_id, wchar_t *name, size_t size) {
  swprintf(name, size, L"Local\\DXMT_REMOTE_LAYER_%lu", (unsigned long)process_id);
}

void
SignalOwner(HWND hwnd) {
  DWORD owner = 0;
  GetWindowThreadProcessId(hwnd, &owner);
  wchar_t name[64];
  EventName(owner, name, 64);
  HANDLE event = OpenEventW(EVENT_MODIFY_STATE, FALSE, name);
  if (!event) {
    WARN("RemoteLayer: process ", owner, " is not hosting remote layers; window ", hwnd, " stays blank");
    return;
  }
  SetEvent(event);
  CloseHandle(event);
}

struct Hosted {
  HWND hwnd;
  uint32_t context_id;
  WMT::Object view;
};

struct Scan {
  DWORD process_id;
  std::vector<std::pair<HWND, uint32_t>> wanted;
};

BOOL CALLBACK
ScanWindow(HWND hwnd, LPARAM param) {
  auto *scan = reinterpret_cast<Scan *>(param);
  DWORD owner = 0;
  GetWindowThreadProcessId(hwnd, &owner);
  if (owner == scan->process_id) {
    auto id = static_cast<uint32_t>(reinterpret_cast<uintptr_t>(GetPropW(hwnd, kProperty)));
    if (id)
      scan->wanted.emplace_back(hwnd, id);
  }
  EnumChildWindows(hwnd, ScanWindow, param);
  return TRUE;
}

DWORD WINAPI
HostThread(LPVOID param) {
  HANDLE event = reinterpret_cast<HANDLE>(param);
  std::vector<Hosted> hosted;
  for (;;) {
    WaitForSingleObject(event, INFINITE);
    Scan scan{GetCurrentProcessId(), {}};
    EnumWindows(ScanWindow, reinterpret_cast<LPARAM>(&scan));
    // Withdrawn or replaced contexts.
    for (auto it = hosted.begin(); it != hosted.end();) {
      bool keep = false;
      for (auto &want : scan.wanted)
        keep |= want.first == it->hwnd && want.second == it->context_id;
      if (keep) {
        ++it;
        continue;
      }
      Logger::info(str::format("RemoteLayer: window ", it->hwnd, " no longer shows context ", it->context_id));
      WMT::ReleaseMetalView(it->view);
      it = hosted.erase(it);
    }
    // New ones.
    for (auto &want : scan.wanted) {
      bool shown = false;
      for (auto &host : hosted)
        shown |= host.hwnd == want.first;
      if (shown)
        continue;
      WMT::Object view = WMT::HostRemoteLayer(reinterpret_cast<intptr_t>(want.first), want.second);
      if (view == nullptr) {
        WARN("RemoteLayer: could not show context ", want.second, " in window ", want.first);
        continue;
      }
      Logger::info(str::format("RemoteLayer: window ", want.first, " shows context ", want.second));
      hosted.push_back({want.first, want.second, view});
    }
  }
  return 0;
}

} // namespace

bool
CrossProcessSwapchainEnabled() {
  auto mode = env::getEnvVar("DXMT_CROSS_PROCESS_SWAPCHAIN");
  return mode == "offscreen" || mode == "host";
}

void
PublishRemoteLayer(HWND hwnd, uint32_t context_id) {
  if (!SetPropW(hwnd, kProperty, reinterpret_cast<HANDLE>(static_cast<uintptr_t>(context_id)))) {
    WARN("RemoteLayer: SetProp on window ", hwnd, " failed: ", GetLastError());
    return;
  }
  Logger::info(str::format("RemoteLayer: presenting window ", hwnd, " through context ", context_id));
  SignalOwner(hwnd);
}

void
WithdrawRemoteLayer(HWND hwnd) {
  RemovePropW(hwnd, kProperty);
  SignalOwner(hwnd);
}

void
StartRemoteLayerHost() {
  if (env::getEnvVar("DXMT_CROSS_PROCESS_SWAPCHAIN") != "host")
    return;
  wchar_t name[64];
  EventName(GetCurrentProcessId(), name, 64);
  HANDLE event = CreateEventW(nullptr, FALSE, FALSE, name);
  if (!event)
    return;
  HANDLE thread = CreateThread(nullptr, 0, HostThread, event, 0, nullptr);
  if (thread)
    CloseHandle(thread);
}

} // namespace dxmt
