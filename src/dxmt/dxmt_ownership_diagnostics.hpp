// Copyright 2026 Nick Kennedy
// SPDX-License-Identifier: LGPL-2.1-or-later
#pragma once
#include <array>
#include <atomic>
#include <chrono>
#include <cstdint>

namespace dxmt {
// Device-wide CPU intervals while at least one keyed texture is owned.
// Concurrent waits and nested Flush/queue waits overlap; never sum categories
// to infer CPU work or GPU execution time. No worker-thread GPU waits included.
struct OwnershipWaitDiagnostics {
  enum Kind { ContextLock, Flush, ChunkCapacity, CPUFence, FrameLatency, SwapchainLatency, Present, Count };
  struct Snapshot { std::array<uint64_t, Count> ns{}, calls{}; };
  std::atomic<uint32_t> active{0};
  std::array<std::atomic<uint64_t>, Count> ns{}, calls{};
  Snapshot snapshot() const {
    Snapshot result;
    for (unsigned i = 0; i < Count; ++i) {
      result.ns[i] = ns[i].load(std::memory_order_relaxed);
      result.calls[i] = calls[i].load(std::memory_order_relaxed);
    }
    return result;
  }
  struct Scope {
    OwnershipWaitDiagnostics *diagnostics;
    Kind kind;
    std::chrono::steady_clock::time_point start;
    Scope(OwnershipWaitDiagnostics *value, Kind category) : diagnostics(value), kind(category) {
      if (diagnostics && diagnostics->active.load(std::memory_order_relaxed))
        start = std::chrono::steady_clock::now();
      else diagnostics = nullptr;
    }
    ~Scope() {
      if (!diagnostics) return;
      auto elapsed = std::chrono::steady_clock::now() - start;
      diagnostics->ns[kind].fetch_add(std::chrono::duration_cast<std::chrono::nanoseconds>(elapsed).count(),
                                     std::memory_order_relaxed);
      diagnostics->calls[kind].fetch_add(1, std::memory_order_relaxed);
    }
    Scope(const Scope &) = delete;
    Scope &operator=(const Scope &) = delete;
  };
};
} // namespace dxmt
