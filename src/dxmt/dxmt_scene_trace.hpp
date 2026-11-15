#pragma once
#include "util_env.hpp"
#include "log/log.hpp"
#include <atomic>
#include <cstdio>
#include <cstdlib>
#include <cstdarg>
namespace dxmt {
// Diagnostic only. No map selection, resource mutation, flush or GPU wait.
inline bool sceneTraceEnabled() {
  static const bool enabled = env::getEnvVar("DXMT_SCENE_TRACE") == "1";
  return enabled;
}
inline void sceneTrace(const char *format, ...) {
  if (!sceneTraceEnabled()) return;
  static const unsigned limit = [] {
    auto value = env::getEnvVar("DXMT_SCENE_TRACE_LIMIT");
    if (value.empty()) return 50000u;
    char *end = nullptr; auto n = std::strtoul(value.c_str(), &end, 10);
    return end && !*end && n > 0 && n <= 1000000 ? unsigned(n) : 50000u;
  }();
  static std::atomic<unsigned> count{0};
  unsigned sequence = count.fetch_add(1, std::memory_order_relaxed);
  if (sequence > limit) return;
  if (sequence == limit) { Logger::info("SCENE truncated limit=" + std::to_string(limit)); return; }
  char row[1536];
  va_list args; va_start(args, format); std::vsnprintf(row, sizeof(row), format, args); va_end(args);
  Logger::info(std::string("SCENE ") + row);
}
// Passive layout audit: every texture access in a window of Present frames,
// with the encoder kind, so readers outside pixel shaders can be found.
inline bool layoutAuditFrame(uint64_t frame) {
  static const bool enabled = env::getEnvVar("DXMT_LAYOUT_AUDIT") == "1";
  if (!enabled) return false;
  static const auto number = [](const char *name, unsigned long fallback) {
    auto value = env::getEnvVar(name);
    if (value.empty()) return fallback;
    char *end = nullptr; auto n = std::strtoul(value.c_str(), &end, 10);
    return end && !*end ? n : fallback;
  };
  static const unsigned long start = number("DXMT_LAYOUT_AUDIT_START_FRAME", 1200);
  static const unsigned long count = number("DXMT_LAYOUT_AUDIT_FRAME_COUNT", 2);
  return frame >= start && frame < start + count;
}
inline void layoutAudit(const char *format, ...) {
  char row[1024];
  va_list args; va_start(args, format); std::vsnprintf(row, sizeof(row), format, args); va_end(args);
  Logger::info(std::string("LAYOUT ") + row);
}
}
