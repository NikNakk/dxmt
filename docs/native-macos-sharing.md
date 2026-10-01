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
