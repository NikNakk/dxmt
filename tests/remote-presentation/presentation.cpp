/* Fork regression harness; dynamically loads D3D11 only in the renderer.
 * API assertions are not evidence of visible content: pair with screen capture.
 */
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <d3d11.h>
#include <dxgi.h>
#include <cstdio>
#include <cstdlib>
#include <vector>
#include <string>

static const wchar_t *generation_prop = L"DXMT_REMOTE_LAYER_V2_GENERATION";
static unsigned failures = 0;
static void check(HRESULT hr, const char *operation) {
  if (FAILED(hr)) { std::fprintf(stderr, "FAIL %s: %08lx\n", operation, (unsigned long)hr); ++failures; }
}
static void pump() {
  MSG msg;
  while (PeekMessageW(&msg, nullptr, 0, 0, PM_REMOVE)) {
    TranslateMessage(&msg); DispatchMessageW(&msg);
  }
}
static LRESULT CALLBACK procedure(HWND hwnd, UINT message, WPARAM wparam, LPARAM lparam) {
  if (message == WM_CLOSE) { DestroyWindow(hwnd); return 0; }
  return DefWindowProcW(hwnd, message, wparam, lparam);
}
struct Surface {
  IDXGISwapChain *chain = nullptr;
  ID3D11RenderTargetView *target = nullptr;
  void release() {
    if (target) target->Release();
    if (chain) chain->Release();
    target = nullptr; chain = nullptr;
  }
};
static Surface create(IDXGIFactory *factory, ID3D11Device *device, HWND hwnd) {
  DXGI_SWAP_CHAIN_DESC desc = {};
  desc.BufferDesc.Width = 640; desc.BufferDesc.Height = 480;
  desc.BufferDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
  desc.SampleDesc.Count = 1; desc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
  desc.BufferCount = 1; desc.OutputWindow = hwnd; desc.Windowed = TRUE;
  desc.SwapEffect = DXGI_SWAP_EFFECT_DISCARD;
  Surface surface;
  check(factory->CreateSwapChain(device, &desc, &surface.chain), "CreateSwapChain");
  if (!surface.chain) return surface;
  ID3D11Texture2D *buffer = nullptr;
  check(surface.chain->GetBuffer(0, __uuidof(ID3D11Texture2D), (void **)&buffer), "GetBuffer");
  if (buffer) { check(device->CreateRenderTargetView(buffer, nullptr, &surface.target), "CreateRenderTargetView"); buffer->Release(); }
  return surface;
}
static void draw(Surface &surface, ID3D11DeviceContext *context, unsigned color, unsigned frame) {
  if (!surface.target) return;
  // Never deliberately black: clear color and changing luminance prove content.
  float colors[3][4] = {{0.1f, 0.75f, 0.25f, 1}, {0.8f, 0.15f, 0.2f, 1}, {0.1f, 0.3f, 0.85f, 1}};
  colors[color % 3][(color + 1) % 3] = 0.35f + (frame % 60) / 150.0f;
  context->ClearRenderTargetView(surface.target, colors[color % 3]);
  check(surface.chain->Present(0, 0), "Present");
}
struct RenderArgs { HWND hwnd; unsigned color; unsigned duration; bool churn; HWND second = nullptr; };
static DWORD WINAPI render(void *param) {
  RenderArgs args = *static_cast<RenderArgs *>(param);
  if (args.color == 3) {
    WNDCLASSW klass = {};
    klass.lpfnWndProc = procedure; klass.hInstance = GetModuleHandleW(nullptr);
    klass.lpszClassName = L"DXMTRendererOwnedChild";
    RegisterClassW(&klass);
    args.hwnd = CreateWindowW(klass.lpszClassName, L"renderer-owned child", WS_CHILD | WS_VISIBLE,
        100, 80, 640, 480, args.hwnd, nullptr, klass.hInstance, nullptr);
    DWORD child_pid = 0, root_pid = 0;
    GetWindowThreadProcessId(args.hwnd, &child_pid);
    GetWindowThreadProcessId(GetAncestor(args.hwnd, GA_ROOT), &root_pid);
    std::printf("renderer-owned child=%p child_pid=%lu root_pid=%lu source_pid=%lu\n",
                args.hwnd, child_pid, root_pid, GetCurrentProcessId());
    if (!args.hwnd || child_pid == root_pid || child_pid != GetCurrentProcessId()) { ++failures; return 1; }
    args.color = 2;
  }
  HMODULE dll = LoadLibraryW(L"d3d11.dll");
  auto create_device = dll ? reinterpret_cast<decltype(&D3D11CreateDevice)>(GetProcAddress(dll, "D3D11CreateDevice")) : nullptr;
  if (!create_device) { ++failures; return 1; }
  ID3D11Device *device = nullptr;
  ID3D11DeviceContext *context = nullptr;
  check(create_device(nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, 0, nullptr, 0, D3D11_SDK_VERSION,
                      &device, nullptr, &context), "D3D11CreateDevice");
  if (!device) { FreeLibrary(dll); return 1; }
  IDXGIFactory *factory = nullptr;
  check(CreateDXGIFactory(__uuidof(IDXGIFactory), (void **)&factory), "CreateDXGIFactory");
  if (!factory) { context->Release(); device->Release(); FreeLibrary(dll); return 1; }
  Surface current = create(factory, device, args.hwnd);
  Surface second;
  if (args.second) second = create(factory, device, args.second);
  ULONGLONG end = GetTickCount64() + args.duration;
  unsigned frame = 0, replacements = 0;
  while (IsWindow(args.hwnd) && GetTickCount64() < end && current.target) {
    draw(current, context, args.color, frame);
    if (args.second) draw(second, context, 1, frame);
    if (args.churn && frame && frame % 30 == 0) {
      Surface next = create(factory, device, args.hwnd);
      if (!next.target) { next.release(); break; }
      draw(next, context, args.color, frame);
      HANDLE generation = GetPropW(args.hwnd, generation_prop);
      current.release(); // A dies after B has published.
      if (generation && generation != GetPropW(args.hwnd, generation_prop)) {
        std::fprintf(stderr, "FAIL replacement generation withdrawn\n"); ++failures;
      }
      current = next;
      if (args.second) {
        Surface next_second = create(factory, device, args.second);
        if (!next_second.target) { next_second.release(); break; }
        draw(next_second, context, 1, frame);
        HANDLE second_generation = GetPropW(args.second, generation_prop);
        second.release();
        if (second_generation && second_generation != GetPropW(args.second, generation_prop)) {
          std::fprintf(stderr, "FAIL second child replacement generation withdrawn\n"); ++failures;
        }
        second = next_second;
      }
      ++replacements;
    }
    ++frame; Sleep(16);
  }
  current.release();
  second.release();
  // Let bounded retirement finish before testing unload. In host mode the
  // host DLLs intentionally remain pinned; offscreen/default can unload.
  Sleep(2300);
  factory->Release(); context->Release(); device->Release(); FreeLibrary(dll);
  std::printf("renderer hwnd=%p frames=%u replacements=%u failures=%u\n", args.hwnd, frame, replacements, failures);
  return failures ? 1 : 0;
}
static HANDLE child(const RenderArgs &args) {
  wchar_t exe[MAX_PATH], line[1024];
  GetModuleFileNameW(nullptr, exe, MAX_PATH);
  swprintf(line, 1024, L"\"%ls\" --render %llu %u %u %u %llu", exe,
           (unsigned long long)(uintptr_t)args.hwnd, args.color, args.duration, args.churn, (unsigned long long)(uintptr_t)args.second);
  STARTUPINFOW startup = {};
  startup.cb = sizeof(startup);
  PROCESS_INFORMATION process = {};
  if (!CreateProcessW(nullptr, line, nullptr, nullptr, FALSE, 0, nullptr, nullptr, &startup, &process)) {
    std::fprintf(stderr, "CreateProcess failed: %lu\n", GetLastError()); ++failures; return nullptr;
  }
  CloseHandle(process.hThread);
  return process.hProcess;
}
int wmain(int argc, wchar_t **argv) {
  SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
  if (argc == 7 && !wcscmp(argv[1], L"--render")) {
    RenderArgs args = {(HWND)(uintptr_t)_wcstoui64(argv[2], nullptr, 10),
                      (unsigned)_wtoi(argv[3]), (unsigned)_wtoi(argv[4]), _wtoi(argv[5]) != 0, (HWND)(uintptr_t)_wcstoui64(argv[6],nullptr,10)};
    return render(&args);
  }
  const wchar_t *mode = argc > 1 ? argv[1] : L"siblings";
  bool same = !wcscmp(mode, L"same"), root_mode = !wcscmp(mode, L"root");
  bool terminate = !wcscmp(mode, L"terminate");
  bool owned_child = !wcscmp(mode, L"owned-child");
  bool static_control = !wcscmp(mode, L"siblings-static");
  WNDCLASSW klass = {};
  klass.lpfnWndProc = procedure; klass.hInstance = GetModuleHandleW(nullptr);
  klass.lpszClassName = L"DXMTRemoteRegression";
  klass.hbrBackground = (HBRUSH)GetStockObject(WHITE_BRUSH);
  RegisterClassW(&klass);
  HWND root = CreateWindowW(klass.lpszClassName, L"DXMT remote regression: green/red/blue content",
                            WS_OVERLAPPEDWINDOW | WS_VISIBLE, 100, 100, 1000, 700, nullptr, nullptr, klass.hInstance, nullptr);
  if (!root) return 1;
  // The host process loads DXGI only. It never imports D3D11 unless mode=same.
  IDXGIFactory *factory = nullptr;
  check(CreateDXGIFactory(__uuidof(IDXGIFactory), (void **)&factory), "host CreateDXGIFactory");
  if (factory) factory->Release();
  HWND back = root_mode || same || owned_child ? root : CreateWindowW(klass.lpszClassName, L"back", WS_CHILD | WS_VISIBLE,
                             0, 0, 900, 600, root, nullptr, klass.hInstance, nullptr);
  HWND front = (!wcscmp(mode, L"siblings") || static_control) ? CreateWindowW(klass.lpszClassName, L"front", WS_CHILD | WS_VISIBLE,
                             150, 100, 500, 350, root, nullptr, klass.hInstance, nullptr) : nullptr;
  RenderArgs args = {back, owned_child ? 3u : 0u, 12000, !static_control, front};
  std::vector<HANDLE> processes;
  if (same) processes.push_back(CreateThread(nullptr, 0, render, &args, 0, nullptr));
  else processes.push_back(child(args));
  ULONGLONG start = GetTickCount64(), last_step = 0;
  unsigned step = 0;
  while (IsWindow(root)) {
    pump();
    ULONGLONG elapsed = GetTickCount64() - start;
    if (terminate && elapsed > 3500) { std::printf("terminating host while renderer is active\n"); return 0; }
    if (!static_control && elapsed > last_step + 300) {
      last_step = elapsed;
      int widths[] = {1000, 2401, 2400, 2399}, heights[] = {700, 1500, 1501, 1499};
      RECT desired = {0, 0, widths[step % 4], heights[step % 4]};
      AdjustWindowRectExForDpi(&desired, GetWindowLongW(root, GWL_STYLE), FALSE,
                              GetWindowLongW(root, GWL_EXSTYLE), GetDpiForWindow(root));
      SetWindowPos(root, nullptr, 0, 0, desired.right - desired.left, desired.bottom - desired.top,
                   SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE);
      if (back != root) {
        RECT client; GetClientRect(root, &client);
        SetWindowPos(back, HWND_BOTTOM, 0, 0, client.right, client.bottom, SWP_NOACTIVATE);
      }
      if (front) {
        bool zero = step % 6 == 2;
        ShowWindow(front, step % 6 == 3 ? SW_HIDE : SW_SHOWNA);
        SetWindowPos(front, step % 8 < 4 ? HWND_TOP : HWND_BOTTOM, 150, 100,
                     zero ? 0 : 500, zero ? 0 : 350, SWP_NOACTIVATE);
      }
      ++step;
    }
    bool finished = true;
    for (HANDLE process : processes)
      if (process && WaitForSingleObject(process, 0) == WAIT_TIMEOUT) finished = false;
    if (finished) break;
    Sleep(10);
  }
  DestroyWindow(root);
  for (HANDLE process : processes) if (process) {
    WaitForSingleObject(process, 5000);
    DWORD exit = 1; GetExitCodeProcess(process, &exit);
    if (!same && exit) ++failures;
    CloseHandle(process);
  }
  std::printf("host mode=%ls steps=%u failures=%u (visible content must be capture-verified)\n", mode, step, failures);
  return failures ? 1 : 0;
}
