#include "wii_menu/material_prepare.h"

#include <string.h>

void wm_layout_prepare_materials(WmPlatform *platform, const WmLayout *layout) {
    if (!platform || !layout) return;
    for (size_t index = 0; index < wm_layout_material_count(layout); index++) {
        WmLayoutMaterialInfo info;
        uint8_t wraps[WM_MATERIAL_TEXTURES][2];
        if (!wm_layout_material_info(layout, index, &info, wraps) ||
            info.texture_map_count > WM_MATERIAL_TEXTURES ||
            info.tev_stage_count > WM_MATERIAL_TEV_STAGES) {
            continue;
        }
        WmMaterialQuad quad = {0};
        quad.texture_count = info.texture_map_count;
        quad.tev_stage_count = info.tev_stage_count;
        quad.has_alpha_compare = info.has_alpha_compare;
        quad.has_blend_mode = info.has_blend_mode;
        memcpy(quad.alpha_compare, info.alpha_compare,
               sizeof(quad.alpha_compare));
        memcpy(quad.blend_mode, info.blend_mode,
               sizeof(quad.blend_mode));
        memcpy(quad.tev_swap_table, info.tev_swap_table,
               sizeof(quad.tev_swap_table));
        for (size_t unit = 0; unit < info.texture_map_count; unit++) {
            quad.wrap_s[unit] = wraps[unit][0];
            quad.wrap_t[unit] = wraps[unit][1];
        }
        for (size_t stage = 0; stage < info.tev_stage_count; stage++) {
            memcpy(quad.tev_stages[stage], info.tev_stages[stage],
                   sizeof(quad.tev_stages[stage]));
        }
        wm_platform_prepare_material(platform, &quad);
    }
}
