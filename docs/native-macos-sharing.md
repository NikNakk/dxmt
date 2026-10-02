# Native macOS sharing metadata

The `macos-xr-native-sharing` development branch exposes the native macOS
representation that DXMT already creates for shared D3D11 resources.

This is intentionally a DXMT interop facility rather than an XR-specific API.

## D3D11 private-data keys

`include/dxmt_native_interop.h` defines:

- `DXMT_GUID_SHARED_TEXTURE_BOOTSTRAP_NAME`
- `DXMT_GUID_SHARED_FENCE_BOOTSTRAP_NAME`

For a shared D3D11 texture, the first key contains the opaque bootstrap name
registered for the texture's existing Metal shared handle.

For a shared D3D11 fence, the second key contains the opaque bootstrap name
registered for the fence's existing Metal shared event.

Both payloads are fixed `DXMT_NATIVE_SHARE_NAME_SIZE` (54-byte) buffers.
Consumers should copy and transport the bytes but must not interpret the name.

## Scope

The implementation does not create an additional texture, perform an
IOSurface copy, or introduce an XR/runtime dependency. It merely publishes
metadata for the native object DXMT already uses to implement D3D11 sharing.

The intended validation is a consumer outside DXMT that creates a shared
Texture2D/Texture2DArray and a shared D3D11 fence, reads both names using
`ID3D11DeviceChild::GetPrivateData`, and reopens the corresponding Metal
objects in a native macOS process.

## Import native resources into D3D11

`ID3D11Device::QueryInterface(DXMT_IID_NATIVE_DEVICE)` returns the DXMT-owned
`IDXMTNativeDevice` extension from `dxmt_native_interop.h`:

```cpp
HRESULT ImportSharedTexture(const char *broker_name,
                            const D3D11_TEXTURE2D_DESC *descriptor,
                            ID3D11Texture2D **texture);
HRESULT ImportSharedEvent(const char *broker_name, ID3D11Fence **fence);
```

These import resources created by a native producer. Texture import wraps the
existing Metal allocation through `Texture::importNative`; it does not allocate
replacement storage, rename the texture or copy image data. Ordinary 2D and
2D arrays preserve their format, size and slice count. The descriptor must use
DEFAULT usage, one mip/sample, no CPU access or misc flags, and render-target
and/or shader-resource bind flags. Native Metal format, dimensions, type,
usage and physical GPU must match; mismatches return `E_INVALIDARG` and no
resource. Depth/other layouts are outside this initial interface's scope.

Event import exposes a native `MTLSharedEvent` as an `ID3D11Fence` usable by
`ID3D11DeviceContext4::Signal`/`Wait` and `GetCompletedValue`. Creating an NT
shared handle from that imported fence is unsupported. The native producer
retains its event, and either side can participate in its timeline.

The names identify **revocable broker endpoints**, using the native-only Mach
protocol in `dxmt_native_capability.h`. They are distinct from the existing
private-data names above, which resolve directly to Metal handles. The Unix
library performs bootstrap lookup, requests the versioned texture/event kind,
validates the reply, reopens the Metal object and releases all temporary Mach
rights. No Mach port or Objective-C pointer crosses the COM interface.

The producer retains the resource and broker until import is complete. Closing
the broker's receive endpoint prevents future imports; wrappers already
imported retain their own Metal objects. The producer must authorize callers
(e.g. kernel audit-token effective UID) and handle failed reply sends without
leaking transferred rights. Existing Wine Unix call numbers are unchanged;
the new texture/event operations append entries 146 and 147.

This API has no OpenXR/runtime dependency. The external `macos-wine-xr` proof
passed three 2D images and three array-of-two images under Wine 8.16 on an Apple
M5 (2026-10-01): D3D11 clear patterns were checked by a native GPU shader;
invalid descriptors and imports after broker closure were rejected. The same
API wrapped native Monado runtime-owned images directly and shared staging
images used for Meta's GPU-copy path. Physical headset/game validation remains
in the consumer project.

## Stock Wine 11.10 presentation

When Wine does not export the macdrv Metal-view helpers, winemetal resolves
the requested top-level HWND through Wine 11.10's `WineWindow.hwnd` selector
on the AppKit thread. It attaches a retained Metal view to that exact window,
autoresizes it with the content view and removes it on release. Mouse hit
testing passes through to Wine's content view. Missing window/class/selector
matches remain unsupported; the fallback never selects an arbitrary key or
foreground window. The existing exported-helper path remains preferred.
This uses Wine's internal Objective-C surface and must be checked when updating
Wine. Child HWND presentation is outside this fallback's scope.

On an Apple M5, the consumer's two-window desktop probe passed create, present,
resize and destruction with stock Wine 11.10 (2026-10-02). The same stack
submitted 328 `hello_xr` frames to simulated Monado with direct runtime-image
sharing and 520 to Meta with shared staging plus a native Metal blit. Native
texture/event import Unix call IDs remain unchanged.
