#include "../../src/d3d11/d3d11_remote_protocol.hpp"
#include "../../src/winemetal/remote_layer_geometry.h"
#include "../../src/winemetal/winemetal_thunks.h"
#include <cassert>
#include <cmath>
#include <cstdio>
#include <utility>
#include <initializer_list>

int main() {
  using namespace dxmt;
  RemoteLayerIdentity a{10, 20, 1, 100, 200}, b{11, 20, 2, 100, 200};
  assert(a == a && !(a == b));
  auto reused_pid = a; ++reused_pid.birth_high;
  assert(!(a == reused_pid));
  auto reused_context = a; ++reused_context.generation;
  assert(!(a == reused_context));
  assert(!RemoteLayerRetirementReady(1100, 3000, 1000, false));
  assert(RemoteLayerRetirementReady(1250, 3000, 1000, false));
  assert(!RemoteLayerRetirementReady(2999, 3000, 0, false));
  assert(RemoteLayerRetirementReady(3000, 3000, 0, false));
  assert(RemoteLayerRetirementReady(1000, 3000, 0, true));
  // An acknowledgement for a second child must not retire the first child's
  // source. Each caller supplies its own acknowledgement and deadline.
  assert(!RemoteLayerRetirementReady(1250, 3000, 0, false));
  WMTRemotePlacement p{150, 100, 500, 350, 150, 100, 500, 350, 2400, 1500, 1, 1, 640, 480};
  auto geometry = WMTCalculateRemoteGeometry(&p, true, 1200, 750, false);
  assert(!geometry.hidden && geometry.x == 75 && geometry.y == 525 && geometry.width == 250);
  auto previous = geometry;
  p.width = 0; p.clip_width = 0;
  assert(WMTCalculateRemoteGeometry(&p, true, 1200, 750, false).hidden);
  p.width = 500; p.clip_width = 500;
  geometry = WMTCalculateRemoteGeometry(&p, true, 1200, 750, false);
  assert(!geometry.hidden && geometry.width == previous.width);
  p.visible = 0;
  assert(WMTCalculateRemoteGeometry(&p, true, 1200, 750, false).hidden);
  p.visible = 1;
  assert(WMTCalculateRemoteGeometry(&p, false, 1200, 750, false).hidden);
  for (auto size : {std::pair<int,int>{2401,1500}, {2400,1501}, {2400,1500}}) {
    p = {0,0,size.first,size.second,0,0,size.first,size.second,size.first,size.second,1,1,640,480};
    double cw = std::ceil(size.first / 2.0), ch = std::ceil(size.second / 2.0);
    auto g = WMTCalculateRemoteGeometry(&p, true, cw, ch, false);
    assert(std::abs(g.width-cw) < 1e-9 && std::abs(g.height-ch) < 1e-9);
    assert(std::abs(g.scale_x*640-cw) < 1e-9 && std::abs(g.scale_y*480-ch) < 1e-9);
  }
  p = {-20,30,100,100,0,30,80,100,1000,700,1,1,100,100};
  geometry = WMTCalculateRemoteGeometry(&p,true,1000,700,true);
  assert(geometry.x == 0 && geometry.host_x == -20 && geometry.width == 80);
  static_assert(sizeof(WMTRemotePlacement) == 56);
  static_assert(sizeof(unixcall_update_remote_layer_host) == 64);
  static_assert(sizeof(unixcall_create_metal_view_with_ownership) == 40);
  std::puts("remote presentation identity, retirement and geometry rules passed");
}
