#pragma once

#include <windows.h>
#include <cstdint>

namespace dxmt {

/* DXMT_CROSS_PROCESS_SWAPCHAIN is offscreen or host: swapchains for another
 * process's window are created instead of failing. */
bool CrossProcessSwapchainEnabled();

/* In the swapchain's process: show context_id in hwnd (another process's
 * window), and later stop. */
void PublishRemoteLayer(HWND hwnd, uint32_t context_id);
void WithdrawRemoteLayer(HWND hwnd);

/* In every process, when d3d11.dll loads: with DXMT_CROSS_PROCESS_SWAPCHAIN=host,
 * start the thread that shows other processes' contexts in this process's
 * windows. */
void StartRemoteLayerHost();

} // namespace dxmt
