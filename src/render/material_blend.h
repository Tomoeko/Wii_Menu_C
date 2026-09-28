#ifndef WM_RENDER_MATERIAL_BLEND_H
#define WM_RENDER_MATERIAL_BLEND_H

#include "wii_menu/platform/platform.h"

#include <stdbool.h>
#include <stdint.h>

typedef struct WmMaterialBlend {
    bool enabled;
    uint8_t source;
    uint8_t destination;
} WmMaterialBlend;

/* Resolve the GX blend factors once, before either backend changes GPU state.
 * An unsupported factor rejects the draw on both backends. */
bool wm_material_blend_resolve(const WmMaterialQuad *quad,
                               WmMaterialBlend *blend);

#endif
