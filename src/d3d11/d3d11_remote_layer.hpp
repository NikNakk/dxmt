#pragma once
#include <windows.h>
#include <cstdint>
#include "Metal.hpp"

namespace dxmt {
bool CrossProcessSwapchainEnabled();
// These properties are a versioned protocol: all readers/writers take the
// per-HWND named mutex. generation is nonzero and scoped to the source PID.
uint32_t PublishRemoteLayer(HWND hwnd, uint32_t context_id, uint32_t width, uint32_t height);
void UpdateRemoteLayerSize(HWND hwnd, uint32_t context_id, uint32_t generation, uint32_t width, uint32_t height);
void RetireRemoteLayer(HWND hwnd, uint32_t context_id, uint32_t generation, WMT::Object view);
void StartRemoteLayerHost();
// DllMain only queues this bootstrap; it never waits for AppKit or a worker.
void QueueRemoteLayerHost();
} // namespace dxmt
