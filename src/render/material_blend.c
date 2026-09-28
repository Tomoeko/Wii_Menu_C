#include "material_blend.h"

enum { WM_MATERIAL_BLEND_FACTOR_COUNT = 8 };

bool wm_material_blend_resolve(const WmMaterialQuad *quad,
                               WmMaterialBlend *blend) {
    if (!quad || !blend) {
        return false;
    }

    WmMaterialBlend resolved = {
        .enabled = true,
        .source = 4,
        .destination = 5
    };
    if (quad->has_blend_mode) {
        if (quad->blend_mode[0] == 0) {
            resolved.enabled = false;
        } else {
            if (quad->blend_mode[1] >= WM_MATERIAL_BLEND_FACTOR_COUNT ||
                quad->blend_mode[2] >= WM_MATERIAL_BLEND_FACTOR_COUNT) {
                return false;
            }
            resolved.source = quad->blend_mode[1];
            resolved.destination = quad->blend_mode[2];
        }
    }

    *blend = resolved;
    return true;
}
