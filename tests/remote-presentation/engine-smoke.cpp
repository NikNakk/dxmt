// Check the experimental engine's crypto, HTTPS and Vulkan prerequisites.
// Presentation and XR acceptance remain separate tests.
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <bcrypt.h>
#include <winhttp.h>
#include <vulkan/vulkan.h>
#include <cstdio>
#include <cstring>
#include <vector>

static bool crypto() {
  BCRYPT_ALG_HANDLE algorithm = nullptr;
  BCRYPT_KEY_HANDLE key = nullptr;
  unsigned char zeros[16] = {}, encrypted[16] = {};
  const unsigned char expected[16] = {0x66,0xe9,0x4b,0xd4,0xef,0x8a,0x2c,0x3b,0x88,0x4c,0xfa,0x59,0xca,0x34,0x2b,0x2e};
  ULONG written = 0;
  NTSTATUS status = BCryptOpenAlgorithmProvider(&algorithm, BCRYPT_AES_ALGORITHM, nullptr, 0);
  if (status >= 0) status = BCryptSetProperty(algorithm, BCRYPT_CHAINING_MODE,
      reinterpret_cast<PUCHAR>(const_cast<wchar_t *>(BCRYPT_CHAIN_MODE_ECB)), sizeof(BCRYPT_CHAIN_MODE_ECB), 0);
  if (status >= 0) status = BCryptGenerateSymmetricKey(algorithm, &key, nullptr, 0, zeros, sizeof(zeros), 0);
  if (status >= 0) status = BCryptEncrypt(key, zeros, sizeof(zeros), nullptr, nullptr, 0,
      encrypted, sizeof(encrypted), &written, 0);
  bool success = status >= 0 && written == sizeof(expected) && !std::memcmp(encrypted, expected, sizeof(expected));
  if (key) BCryptDestroyKey(key);
  if (algorithm) BCryptCloseAlgorithmProvider(algorithm, 0);
  std::printf("AES known-answer: %s (NTSTATUS=%08lx)\n", success ? "PASS" : "FAIL", static_cast<unsigned long>(status));
  return success;
}

static bool https() {
  HINTERNET session = WinHttpOpen(L"DXMT-engine-readiness/1", WINHTTP_ACCESS_TYPE_DEFAULT_PROXY,
      WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);
  if (session) WinHttpSetTimeouts(session, 10000, 10000, 10000, 10000);
  HINTERNET connection = session ? WinHttpConnect(session, L"store.steampowered.com", INTERNET_DEFAULT_HTTPS_PORT, 0) : nullptr;
  HINTERNET request = connection ? WinHttpOpenRequest(connection, L"HEAD", L"/", nullptr,
      WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES, WINHTTP_FLAG_SECURE) : nullptr;
  DWORD code = 0, size = sizeof(code);
  bool success = request && WinHttpSendRequest(request, WINHTTP_NO_ADDITIONAL_HEADERS, 0,
      WINHTTP_NO_REQUEST_DATA, 0, 0, 0) && WinHttpReceiveResponse(request, nullptr) &&
      WinHttpQueryHeaders(request, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
          WINHTTP_HEADER_NAME_BY_INDEX, &code, &size, WINHTTP_NO_HEADER_INDEX);
  DWORD error = success ? ERROR_SUCCESS : GetLastError();
  success = success && code >= 200 && code < 400;
  if (request) WinHttpCloseHandle(request);
  if (connection) WinHttpCloseHandle(connection);
  if (session) WinHttpCloseHandle(session);
  std::printf("Steam HTTPS HEAD: %s (HTTP=%lu error=%lu)\n", success ? "PASS" : "FAIL", code, error);
  return success;
}

static bool vulkan() {
  HMODULE module = LoadLibraryW(L"vulkan-1.dll");
  PFN_vkGetInstanceProcAddr get = nullptr;
  FARPROC address = module ? GetProcAddress(module, "vkGetInstanceProcAddr") : nullptr;
  static_assert(sizeof(get) == sizeof(address));
  std::memcpy(&get, &address, sizeof(get));
  if (!get) { std::printf("Vulkan: FAIL (loader error=%lu)\n", GetLastError()); if (module) FreeLibrary(module); return false; }
  auto create = reinterpret_cast<PFN_vkCreateInstance>(get(VK_NULL_HANDLE, "vkCreateInstance"));
  VkApplicationInfo app = {}; app.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO; app.apiVersion = VK_API_VERSION_1_0;
  VkInstanceCreateInfo info = {}; info.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO; info.pApplicationInfo = &app;
  VkInstance instance = VK_NULL_HANDLE;
  VkResult result = create ? create(&info, nullptr, &instance) : VK_ERROR_INITIALIZATION_FAILED;
  uint32_t count = 0;
  if (result == VK_SUCCESS) {
    auto enumerate = reinterpret_cast<PFN_vkEnumeratePhysicalDevices>(get(instance, "vkEnumeratePhysicalDevices"));
    auto properties = reinterpret_cast<PFN_vkGetPhysicalDeviceProperties>(get(instance, "vkGetPhysicalDeviceProperties"));
    auto destroy = reinterpret_cast<PFN_vkDestroyInstance>(get(instance, "vkDestroyInstance"));
    result = enumerate(instance, &count, nullptr);
    if (result == VK_SUCCESS && count) {
      std::vector<VkPhysicalDevice> devices(count);
      result = enumerate(instance, &count, devices.data());
      if (result == VK_SUCCESS) for (auto device : devices) {
        VkPhysicalDeviceProperties p = {}; properties(device, &p);
        std::printf("Vulkan device: %s\n", p.deviceName);
      }
    }
    destroy(instance, nullptr);
  }
  bool success = result == VK_SUCCESS && count;
  std::printf("Vulkan: %s (result=%d devices=%u)\n", success ? "PASS" : "FAIL", result, count);
  FreeLibrary(module);
  return success;
}

int main(int argc, char **argv) {
  bool a = crypto(), b = https(), c = vulkan();
  bool xr = true;
  if (argc > 1 && !std::strcmp(argv[1], "--openxr-loader")) {
    HMODULE module = LoadLibraryW(L"wineopenxr.dll");
    xr = module && GetProcAddress(module, "xrGetInstanceProcAddr") && GetProcAddress(module, "xrCreateInstance");
    std::printf("OpenXR bridge module: %s (error=%lu; no XR instance/session created)\n",
        xr ? "PASS" : "FAIL", xr ? 0 : GetLastError());
    if (module) FreeLibrary(module);
  }
  return a && b && c && xr ? 0 : 1;
}
