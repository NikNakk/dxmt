/*
 * Copyright 2026 Nick Kennedy
 *
 * This library is free software; you can redistribute it and/or modify it
 * under the terms of the GNU Lesser General Public License as published by
 * the Free Software Foundation; either version 2.1 of the License, or (at
 * your option) any later version.
 */

#pragma once

#include <guiddef.h>

#ifdef __cplusplus
#include <d3d11.h>

/* Query an ID3D11Device for this optional interface. Import synchronously
 * resolves a live producer capability using dxmt_native_capability.h. These
 * names identify revocable broker ports, unlike legacy raw Metal metadata.
 * The producer keeps its broker
 * alive until this call completes; the returned resource owns the imported
 * Metal texture independently. No CPU staging or resource allocation occurs.
 * Only DEFAULT, single-mip/sample, non-cube Texture2D/Texture2DArray resources
 * with render-target and/or shader-resource bindings are supported initially.
 * Descriptors must exactly match the native texture. */
static const GUID DXMT_IID_NATIVE_DEVICE = {
    0xe95e65bc, 0xdde3, 0x41ba,
    {0xb9, 0x5e, 0xb3, 0x0d, 0x24, 0x80, 0xf4, 0x5b}};

/* Optional in-process interface. Object values are borrowed native Objective-C
 * pointers, valid only in this Wine process. Imports retain the exact object;
 * they never reopen a sharing handle, allocate replacement storage or copy.
 * The caller owns the input until import returns. GetMetalDevice is borrowed
 * for the lifetime of the D3D device. The old interface/IID is unchanged. */
static const GUID DXMT_IID_NATIVE_DEVICE2 = {
    0x5a6d2e1b, 0xb10a, 0x4ab7,
    {0x8a, 0xde, 0xd9, 0xa6, 0x4c, 0x41, 0x72, 0x83}};
struct ID3D11Fence;
struct IDXMTNativeDevice2 : public IUnknown {
  virtual HRESULT STDMETHODCALLTYPE GetMetalDevice(UINT64 *device) = 0;
  virtual HRESULT STDMETHODCALLTYPE ImportMetalTexture(UINT64 texture,
      const D3D11_TEXTURE2D_DESC *desc, ID3D11Texture2D **out) = 0;
  virtual HRESULT STDMETHODCALLTYPE ImportMetalSharedEvent(UINT64 event, ID3D11Fence **out) = 0;
};
struct IDXMTNativeDevice : public IUnknown {
  virtual HRESULT STDMETHODCALLTYPE ImportSharedTexture(
      const char *bootstrap_name, const D3D11_TEXTURE2D_DESC *desc,
      ID3D11Texture2D **texture) = 0;
  /* The imported fence supports context Signal/Wait and GetCompletedValue.
   * CreateSharedHandle is unsupported: no Windows kernel handle is created. */
  virtual HRESULT STDMETHODCALLTYPE ImportSharedEvent(
      const char *bootstrap_name, ID3D11Fence **fence) = 0;
};
#endif

/*
 * Stable DXMT private-data keys for consumers that need to reopen D3D11
 * shared resources as their native macOS Metal objects.
 *
 * The payload for both keys is a NUL-terminated bootstrap registration name
 * stored in a fixed 54-byte buffer. Consumers must treat the name as opaque.
 *
 * These keys intentionally describe DXMT's native sharing mechanism rather
 * than any particular consumer (XR runtime, streamer, or compositor).
 */
#define DXMT_NATIVE_SHARE_NAME_SIZE 54

static const GUID DXMT_GUID_SHARED_TEXTURE_BOOTSTRAP_NAME = {
    0x6f5ee9b2, 0xe42a, 0x4f4d,
    {0x97, 0x82, 0x1c, 0x73, 0x0f, 0x54, 0x64, 0xb9}};

static const GUID DXMT_GUID_SHARED_FENCE_BOOTSTRAP_NAME = {
    0x8a1e78d5, 0x9762, 0x4f7a,
    {0xb0, 0xad, 0x1d, 0x62, 0xf6, 0xa4, 0x9d, 0x31}};
