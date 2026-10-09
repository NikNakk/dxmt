/* Optional native Unix interface, independent of macdrv_functions_t.
 * SPDX-License-Identifier: MIT
 * All pointers belong to the current Unix process, never to PE callers. */
#ifndef WINE_METAL_PRESENTATION_H
#define WINE_METAL_PRESENTATION_H
#include <stdint.h>
#define WINE_METAL_PRESENTATION_VERSION 1
#define WINE_METAL_PRESENTATION_LOCAL 1u
#define WINE_METAL_PRESENTATION_UNSUPPORTED 1
#define WINE_METAL_PRESENTATION_FAILED 2
struct wine_metal_presentation_v1 {
    uint32_t size, version, capabilities, reserved;
    /* On failure no resources or window attachment remain. Layer is borrowed
     * until release. Device is supplied by the renderer, retained by layer. */
    int (*create)(uintptr_t hwnd, void *device, void **token, void **layer);
    /* Called once, after first real submission, not on DXGI_PRESENT_TEST. */
    void (*submitted)(void *token);
    /* Caller drains GPU use before release; provider owns view and surface. */
    void (*release)(void *token);
};
static inline int wine_metal_presentation_valid(const struct wine_metal_presentation_v1 *api)
{
    return api && api->size >= sizeof(*api) && api->version == WINE_METAL_PRESENTATION_VERSION &&
           (api->capabilities & WINE_METAL_PRESENTATION_LOCAL) && api->create && api->submitted && api->release;
}
typedef const struct wine_metal_presentation_v1 *(*wine_metal_presentation_query_fn)(uint32_t version);
#endif
