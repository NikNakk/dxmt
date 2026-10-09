#pragma once
#include <cstdint>

namespace dxmt {
struct RemoteLayerIdentity {
  uint32_t id, pid, generation, birth_low, birth_high;
  bool operator==(const RemoteLayerIdentity &other) const {
    return id == other.id && pid == other.pid && generation == other.generation &&
           birth_low == other.birth_low && birth_high == other.birth_high;
  }
};
inline bool RemoteLayerRetirementReady(uint64_t now, uint64_t deadline, uint64_t acknowledged, bool dead) {
  return dead || now >= deadline || (acknowledged && now - acknowledged >= 250);
}
} // namespace dxmt
