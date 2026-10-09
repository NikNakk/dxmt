#ifndef DXMT_REMOTE_LAYER_GEOMETRY_H
#define DXMT_REMOTE_LAYER_GEOMETRY_H
#include "winemetal.h"

struct WMTRemoteGeometry {
  bool hidden;
  double x, y, width, height, host_x, host_y, scale_x, scale_y;
};
/* Pure placement calculation, shared by AppKit and the native regression test.
 * Unknown geometry is explicit; a supplied zero area is always hidden. */
static inline struct WMTRemoteGeometry
WMTCalculateRemoteGeometry(const struct WMTRemotePlacement *p, bool known,
                          double root_width, double root_height, bool flipped) {
  struct WMTRemoteGeometry g = {false, 0, 0, 0, 0, 0, 0, 0, 0};
  g.hidden = !known || !p->visible || p->width <= 0 || p->height <= 0 ||
             p->clip_width <= 0 || p->clip_height <= 0 || p->root_width <= 0 || p->root_height <= 0 ||
             !p->source_width || !p->source_height || root_width <= 0 || root_height <= 0;
  if (g.hidden) return g;
  double sx = root_width / p->root_width, sy = root_height / p->root_height;
  g.x = p->clip_x * sx;
  g.y = flipped ? p->clip_y * sy : root_height - (p->clip_y + p->clip_height) * sy;
  g.width = p->clip_width * sx;
  g.height = p->clip_height * sy;
  g.host_x = (p->x - p->clip_x) * sx;
  g.host_y = (p->y - p->clip_y) * sy;
  g.scale_x = p->width * sx / p->source_width;
  g.scale_y = p->height * sy / p->source_height;
  return g;
}
#endif
