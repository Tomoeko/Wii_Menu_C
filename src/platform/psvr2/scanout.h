#ifndef WII_MENU_PSVR2_SCANOUT_H
#define WII_MENU_PSVR2_SCANOUT_H

#include "vr_layout.h"
#include <stdbool.h>
#include <stdint.h>

enum {
    WM_PSVR2_SCANOUT_WIDTH = 4000,
    WM_PSVR2_SCANOUT_HEIGHT = 2040,
    WM_PSVR2_SCANOUT_PITCH = 12000,
    WM_PSVR2_SCANOUT_VERTICES_PER_EYE = 6
};

typedef struct WmPsvr2ScanoutLayout {
    bool row_zero_is_gl_bottom;
    bool gpu_stores_bgr;
    bool native_expects_bgr;
    bool swap_red_blue;
} WmPsvr2ScanoutLayout;

typedef struct WmPsvr2ScanoutVertex {
    float x;
    float y;
    float u;
    float v;
} WmPsvr2ScanoutVertex;

/* Asymmetric, dim colors distinguish both row direction and R/B order. */
static const uint8_t wm_psvr2_probe_gl_bottom[3] = {64, 128, 192};
static const uint8_t wm_psvr2_probe_gl_top[3] = {192, 64, 128};

static inline bool wm_psvr2_probe_color_matches(
    const uint8_t actual[3], const uint8_t expected[3], bool swap_red_blue)
{
    for (unsigned channel = 0; channel < 3; ++channel) {
        unsigned source = swap_red_blue ? 2 - channel : channel;
        int difference = (int)actual[channel] - (int)expected[source];
        if (difference < -1 || difference > 1) return false;
    }
    return true;
}

/* The import probe establishes GPU byte storage independently of the native
 * RDMA format. A successful EGL fourcc import does not establish scanout order. */
static inline bool wm_psvr2_scanout_classify(
    const uint8_t row_zero[3], const uint8_t row_last[3],
    WmPsvr2ScanoutLayout *layout)
{
    if (!row_zero || !row_last || !layout) return false;
    for (unsigned bottom_first = 0; bottom_first < 2; ++bottom_first) {
        const uint8_t *first = bottom_first
            ? wm_psvr2_probe_gl_bottom : wm_psvr2_probe_gl_top;
        const uint8_t *last = bottom_first
            ? wm_psvr2_probe_gl_top : wm_psvr2_probe_gl_bottom;
        for (unsigned swap = 0; swap < 2; ++swap) {
            if (wm_psvr2_probe_color_matches(row_zero, first, swap != 0) &&
                wm_psvr2_probe_color_matches(row_last, last, swap != 0)) {
                *layout = (WmPsvr2ScanoutLayout){
                    .row_zero_is_gl_bottom = bottom_first != 0,
                    .gpu_stores_bgr = swap != 0
                };
                return true;
            }
        }
    }
    return false;
}

/* MediaTek MDP_RDMA fmt=1 selects DRM RGB888 with SWAP clear: the packed
 * little-endian [23:0] R:G:B value has B,G,R memory bytes. SWAP bit14 selects
 * DRM BGR888 and R,G,B memory bytes. SRC_CON2=0 is the verified 8-bit path.
 * All four strip engines must agree; no hardware registers are changed. */
static inline bool wm_psvr2_scanout_rdma_contract(
    const uint32_t source[4], const uint32_t source2[4],
    WmPsvr2ScanoutLayout *layout)
{
    if (!source || !source2 || !layout) return false;
    bool native_bgr = (source[0] & (1u << 14)) == 0;
    for (unsigned channel = 0; channel < 4; ++channel) {
        if ((source[channel] & 15u) != 1u || source2[channel] != 0 ||
            ((source[channel] & (1u << 14)) == 0) != native_bgr)
            return false;
    }
    layout->native_expects_bgr = native_bgr;
    layout->swap_red_blue = layout->gpu_stores_bgr != native_bgr;
    return true;
}

/* Eye rectangles use native memory rows with y=0 at the visible top. Scene
 * FBO textures independently have v=1 at the menu's top. Preserve both. */
static inline void wm_psvr2_scanout_eye_vertices(
    const WmVrEyeRect *eye, const WmPsvr2ScanoutLayout *layout,
    WmPsvr2ScanoutVertex vertices[WM_PSVR2_SCANOUT_VERTICES_PER_EYE])
{
    float x0 = 2.0f * eye->x / WM_PSVR2_SCANOUT_WIDTH - 1.0f;
    float x1 = 2.0f * (eye->x + eye->width) / WM_PSVR2_SCANOUT_WIDTH - 1.0f;
    float y0 = 2.0f * eye->y / WM_PSVR2_SCANOUT_HEIGHT - 1.0f;
    float y1 = 2.0f * (eye->y + eye->height) / WM_PSVR2_SCANOUT_HEIGHT - 1.0f;
    if (!layout->row_zero_is_gl_bottom) {
        y0 = -y0;
        y1 = -y1;
    }
    const WmPsvr2ScanoutVertex corners[4] = {
        {x0, y0, 0, 1}, {x1, y0, 1, 1},
        {x0, y1, 0, 0}, {x1, y1, 1, 0}
    };
    static const unsigned order[WM_PSVR2_SCANOUT_VERTICES_PER_EYE] = {
        0, 1, 3, 0, 3, 2
    };
    for (unsigned index = 0; index < WM_PSVR2_SCANOUT_VERTICES_PER_EYE; ++index)
        vertices[index] = corners[order[index]];
}

#endif
