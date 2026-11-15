// Private local diagnostic. Copyright 2026 Nick Kennedy; LGPL-2.1-or-later.
#pragma once
#include <d3d11.h>
static const GUID DXMT_IID_REPLAY_PROBE = {0x0ad9c420,0x137b,0x48ab,{0xb4,0x12,0x62,0x9c,0xe7,0x11,0x2a,0x70}};
static const GUID DXMT_IID_REPLAY_PACKET = {0x0ad9c421,0x137b,0x48ab,{0xb4,0x12,0x62,0x9c,0xe7,0x11,0x2a,0x70}};
// A pending packet has no device/resource ownership. The immediate context drops
// its reference after the next draw attempt. Caller owns the completed packet.
struct IDXMTReplayPacket : IUnknown {
  virtual HRESULT STDMETHODCALLTYPE GetResult(ID3D11CommandList **list,
      ID3D11Texture2D **colour, ID3D11Texture2D **depth, UINT64 *nominal_bytes) = 0;
};
struct IDXMTReplayProbe : IUnknown {
  // DXMT_REPLAY_CAPTURE=1 required. One non-indexed Draw only, no interception
  // of updates/passes; unsupported draw types return a failed packet. Captures
  // seed attachments and mutable inputs immediately before the ordinary draw.
  virtual HRESULT STDMETHODCALLTYPE CaptureNextDraw(UINT64 budget_bytes, IDXMTReplayPacket **packet) = 0;
};

// Separate versioned interface: old diagnostic vtables are unchanged.
static const GUID DXMT_IID_REPLAY_PASS_PROBE = {0x0ad9c422,0x137b,0x48ab,{0xb4,0x12,0x62,0x9c,0xe7,0x11,0x2a,0x70}};
struct IDXMTReplayPassProbe : IUnknown {
  // Explicit owner-thread scope, 1..16 Draw/DrawIndexed calls, same RTV/DSV
  // objects. Mutable input updates are snapshotted at each draw. Operations
  // outside the bounded contract fail the packet; ordinary rendering continues.
  virtual HRESULT STDMETHODCALLTYPE BeginPassCapture(UINT64 budget_bytes, UINT max_draws, IDXMTReplayPacket **packet) = 0;
  virtual HRESULT STDMETHODCALLTYPE EndPassCapture() = 0;
};
