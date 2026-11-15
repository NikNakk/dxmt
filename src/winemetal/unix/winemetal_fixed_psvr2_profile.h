/* Fixed PS VR2 profile for the private DXMT experiment.
 * SPDX-License-Identifier: LGPL-2.1-or-later
 * Rates are separable per axis, with a full-rate +/-20 degree core and
 * a smooth transition to the peripheral rate at +/-35 degrees.
 * Projection FOV matches Monado's PS VR2 driver, rather than texture centre.
 */
#ifndef WINEMETAL_FIXED_PSVR2_PROFILE_H
#define WINEMETAL_FIXED_PSVR2_PROFILE_H
#include <math.h>
#define WMT_FIXED_PSVR2_COLUMNS_PER_EYE 32
#define WMT_FIXED_PSVR2_ROWS 32
static inline float
wmt_fixed_psvr2_rate(float angle_degrees, float outer)
{
    float t = (fabsf(angle_degrees) - 20.f) / 15.f;
    t = fminf(1.f, fmaxf(0.f, t));
    t = t * t * (3.f - 2.f * t);
    return 1.f + (outer - 1.f) * t;
}
static inline float
wmt_fixed_psvr2_horizontal(unsigned column, float outer)
{
    const unsigned eye = column / WMT_FIXED_PSVR2_COLUMNS_PER_EYE;
    const float u = (column % WMT_FIXED_PSVR2_COLUMNS_PER_EYE + .5f) /
                    WMT_FIXED_PSVR2_COLUMNS_PER_EYE;
    const float radians = .017453292519943295f;
    const float left = tanf((eye == 0 ? -61.5f : -43.5f) * radians);
    const float right = tanf((eye == 0 ? 43.5f : 61.5f) * radians);
    return wmt_fixed_psvr2_rate(atanf(left + u * (right - left)) / radians, outer);
}
static inline float
wmt_fixed_psvr2_vertical(unsigned row, float outer)
{
    const float radians = .017453292519943295f;
    const float v = (row + .5f) / WMT_FIXED_PSVR2_ROWS;
    return wmt_fixed_psvr2_rate(atanf((2.f * v - 1.f) * tanf(53.f * radians)) / radians, outer);
}
#endif
