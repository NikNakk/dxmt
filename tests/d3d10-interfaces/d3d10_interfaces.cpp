// D3D11 resources must refuse D3D10 interfaces unless their device was created
// through D3D10, as on Windows. SteamVR's client classifies a submitted DirectX
// texture as D3D10 when it answers ID3D10Texture2D.
#include <d3d10.h>
#include <d3d11.h>
#include <cstdio>
#include <windows.h>

typedef HRESULT(WINAPI *D3D10CoreCreateDeviceFn)(IDXGIFactory *, IDXGIAdapter *, UINT, D3D_FEATURE_LEVEL,
                                                 ID3D10Device **);

int
main()
{
	setvbuf(stdout, nullptr, _IONBF, 0);
	int failures = 0;
	ID3D11Device *dev = nullptr;
	D3D_FEATURE_LEVEL level = D3D_FEATURE_LEVEL_11_0;
	if (FAILED(D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, 0, &level, 1, D3D11_SDK_VERSION, &dev,
	                             nullptr, nullptr))) {
		printf("FAIL D3D11 device\n");
		return 1;
	}
	D3D11_TEXTURE2D_DESC td = {64, 64, 1, 1, DXGI_FORMAT_R8G8B8A8_UNORM, {1, 0}, D3D11_USAGE_DEFAULT,
	                           D3D11_BIND_SHADER_RESOURCE, 0, 0};
	ID3D11Texture2D *tex = nullptr;
	dev->CreateTexture2D(&td, nullptr, &tex);
	void *out = nullptr;
	HRESULT hr10 = tex->QueryInterface(__uuidof(ID3D10Texture2D), &out);
	HRESULT hrRes = tex->QueryInterface(__uuidof(ID3D10Resource), &out);
	HRESULT hr11 = tex->QueryInterface(__uuidof(ID3D11Texture2D), &out);
	bool ok = hr10 == E_NOINTERFACE && hrRes == E_NOINTERFACE && hr11 == S_OK;
	failures += !ok;
	printf("%s D3D11 texture: ID3D10Texture2D 0x%lx, ID3D10Resource 0x%lx, ID3D11Texture2D 0x%lx\n",
	       ok ? "ok  " : "FAIL", hr10, hrRes, hr11);

	HMODULE core = LoadLibraryA("d3d10core.dll");
	auto create = core ? (D3D10CoreCreateDeviceFn)GetProcAddress(core, "D3D10CoreCreateDevice") : nullptr;
	IDXGIFactory *factory = nullptr;
	IDXGIAdapter *adapter = nullptr;
	ID3D10Device *dev10 = nullptr;
	if (create && SUCCEEDED(CreateDXGIFactory(__uuidof(IDXGIFactory), (void **)&factory)) &&
	    SUCCEEDED(factory->EnumAdapters(0, &adapter)) &&
	    SUCCEEDED(create(factory, adapter, 0, D3D_FEATURE_LEVEL_10_0, &dev10))) {
		D3D10_TEXTURE2D_DESC t10 = {64, 64, 1, 1, DXGI_FORMAT_R8G8B8A8_UNORM, {1, 0}, D3D10_USAGE_DEFAULT,
		                            D3D10_BIND_SHADER_RESOURCE, 0, 0};
		ID3D10Texture2D *tex10 = nullptr;
		dev10->CreateTexture2D(&t10, nullptr, &tex10);
		HRESULT hr = tex10 ? tex10->QueryInterface(__uuidof(ID3D10Texture2D), &out) : E_FAIL;
		HRESULT hrr = tex10 ? tex10->QueryInterface(__uuidof(ID3D10Resource), &out) : E_FAIL;
		ok = hr == S_OK && hrr == S_OK;
		failures += !ok;
		printf("%s D3D10 texture: ID3D10Texture2D 0x%lx, ID3D10Resource 0x%lx\n", ok ? "ok  " : "FAIL", hr, hrr);
	} else {
		printf("FAIL could not create a D3D10 device through d3d10core.dll\n");
		++failures;
	}
	printf("%s D3D10 interfaces: failures=%d\n", failures ? "FAIL" : "PASS", failures);
	return failures ? 1 : 0;
}
