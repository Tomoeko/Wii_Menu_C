#include "wii_menu/layout_present.h"

#include <math.h>
#include <stddef.h>

static float color_unit(float value) {
    return fminf(255.0f, fmaxf(0.0f, value)) / 255.0f;
}

static float clamp_unit(float value) {
    return fminf(1.0f, fmaxf(0.0f, value));
}

typedef struct PresentContext {
    WmPlatform *platform;
    WmTextureCache *textures;
    WmFontCache *fonts;
    WmCachedFont *active_font;
    const WmLayoutTextInfo *active_text;
    float active_alpha;
    float screen_scale_x;
    WmLayoutPaneCallback filter;
    void *filter_context;
    WmLayoutDrawPredicate draw_predicate;
    void *draw_predicate_context;
} PresentContext;

static bool resolve_font_sheet(void *context, size_t sheet, uint32_t *texture) {
    PresentContext *present = context;
    return wm_font_cache_sheet(present->active_font, sheet, texture);
}

static void draw_font_quad(void *context, const WmFontQuad *quad) {
    PresentContext *present = context;
    if (!quad || !quad->texture || !present->active_text) return;
    const uint8_t *range_color = NULL;
    for (size_t range = 0;
         range < present->active_text->color_range_count; range++) {
        const WmLayoutTextColorRange *entry =
            &present->active_text->color_ranges[range];
        if (quad->byte_index >= entry->first_byte &&
            quad->byte_index < entry->end_byte) {
            range_color = entry->rgba;
            break;
        }
    }
    WmDrawVertex vertices[4];
    for (size_t index = 0; index < 4; index++) {
        const WmFontVertex *source = &quad->vertices[index];
        const float *color = present->active_text->colors[index < 2 ? 0 : 1];
        /* CharWriter maps glyph coverage between material colors 0 and 1.
         * Extracted menu text uses transparent color 0 and opaque color 1;
         * the atlas stores coverage in alpha, so tint by color 1 here. */
        const float *foreground = present->active_text->material
            ? present->active_text->material->registers[1] : NULL;
        vertices[index] = (WmDrawVertex){
            .x = WM_FRAME_WIDTH * 0.5f +
                 source->position[0] * present->screen_scale_x,
            .y = WM_FRAME_HEIGHT * 0.5f - source->position[1],
            .u = source->uv[0],
            .v = source->uv[1],
            .color = {
                (range_color ? range_color[0] / 255.0f : color_unit(color[0])) *
                    (foreground ? clamp_unit(foreground[0]) : 1.0f),
                (range_color ? range_color[1] / 255.0f : color_unit(color[1])) *
                    (foreground ? clamp_unit(foreground[1]) : 1.0f),
                (range_color ? range_color[2] / 255.0f : color_unit(color[2])) *
                    (foreground ? clamp_unit(foreground[2]) : 1.0f),
                (range_color ? range_color[3] / 255.0f : color_unit(color[3])) *
                    (foreground ? clamp_unit(foreground[3]) : 1.0f) *
                    present->active_alpha
            }
        };
    }
    wm_platform_draw_vertices(present->platform, vertices, quad->texture);
}

static bool visit_pane(void *context, const WmLayoutPaneView *pane) {
    PresentContext *present = context;
    if (present->filter && !present->filter(present->filter_context, pane)) {
        return false;
    }
    if (pane->text && pane->text->value[0] && pane->alpha > 0 && present->fonts &&
        (!present->draw_predicate || present->draw_predicate(
            present->draw_predicate_context, pane->name))) {
        WmCachedFont *face = wm_font_cache_resolve(present->fonts,
                                                   pane->text->font_name);
        const WmFontTextLayout *layout = face
            ? wm_font_cache_layout(face, pane->text->value, &pane->text->pane)
            : NULL;
        if (layout) {
            present->active_font = face;
            present->active_text = pane->text;
            present->active_alpha = pane->alpha;
            wm_font_emit_pane(layout, pane->matrix, 1.0f,
                              resolve_font_sheet, draw_font_quad, present);
            present->active_font = NULL;
            present->active_text = NULL;
        }
    }
    return true;
}

static bool resolve_image(void *context, const WmLayoutTexture *resource,
                          uint32_t *handle) {
    PresentContext *present = context;
    if (!present->textures || !resource || !resource->url[0]) return false;
    return wm_texture_cache_resolve(present->textures, resource->url, handle);
}

static void draw_quad(void *context, const WmLayoutQuad *quad) {
    PresentContext *present = context;
    if (!quad || !quad->material) return;
    if (present->draw_predicate && !present->draw_predicate(
        present->draw_predicate_context, quad->pane_name)) return;
    const WmLayoutMaterialInfo *material = quad->material;
    WmMaterialQuad draw = {0};
    draw.texture_count = material->texture_map_count;
    draw.tev_stage_count = material->tev_stage_count;
    if (draw.texture_count > WM_MATERIAL_TEXTURES ||
        draw.tev_stage_count > WM_MATERIAL_TEV_STAGES) return;
    for (size_t index = 0; index < WM_MATERIAL_TEXTURES; index++) {
        draw.textures[index] = quad->textures[index].handle;
        draw.wrap_s[index] = quad->textures[index].wrap_s;
        draw.wrap_t[index] = quad->textures[index].wrap_t;
        for (size_t channel = 0; channel < 4; channel++) {
            draw.konst_colors[index][channel] =
                material->konst_colors[index][channel];
            if (index < 3) {
                draw.registers[index][channel] =
                    material->registers[index][channel];
            }
        }
        draw.alpha_compare[index] = material->alpha_compare[index];
        draw.blend_mode[index] = material->blend_mode[index];
        draw.tev_swap_table[index] = material->tev_swap_table[index];
    }
    for (size_t index = 0; index < draw.tev_stage_count; index++) {
        for (size_t byte = 0; byte < 16; byte++) {
            draw.tev_stages[index][byte] = material->tev_stages[index][byte];
        }
    }
    draw.has_alpha_compare = material->has_alpha_compare;
    draw.has_blend_mode = material->has_blend_mode;
    for (size_t index = 0; index < 4; index++) {
        const WmLayoutVertex *source = &quad->vertices[index];
        WmMaterialVertex *target = &draw.vertices[index];
        target->x = WM_FRAME_WIDTH * 0.5f +
                    source->position[0] * present->screen_scale_x;
        target->y = WM_FRAME_HEIGHT * 0.5f - source->position[1];
        for (size_t unit = 0; unit < WM_MATERIAL_TEXTURES; unit++) {
            target->uv[unit][0] = source->uv[unit][0];
            target->uv[unit][1] = source->uv[unit][1];
        }
        target->color = (WmColor){
            source->color[0], source->color[1],
            source->color[2], source->color[3]
        };
    }
    wm_platform_draw_material_quad(present->platform, &draw);
}

void wm_layout_present(WmPlatform *platform, WmTextureCache *textures,
                       const WmLayout *layout, bool wide, WmLayoutMode mode,
                       const float parent_matrix[12]) {
    wm_layout_present_with_fonts(platform, textures, NULL, layout, wide, mode,
                                 parent_matrix);
}

void wm_layout_present_filtered(WmPlatform *platform, WmTextureCache *textures,
                                const WmLayout *layout, bool wide,
                                WmLayoutMode mode, const float parent_matrix[12],
                                WmLayoutPaneCallback filter, void *filter_context) {
    wm_layout_present_filtered_with_fonts(platform, textures, NULL, layout, wide,
                                           mode, parent_matrix, filter,
                                           filter_context);
}

void wm_layout_present_with_fonts(WmPlatform *platform,
                                  WmTextureCache *textures, WmFontCache *fonts,
                                  const WmLayout *layout, bool wide,
                                  WmLayoutMode mode,
                                  const float parent_matrix[12]) {
    wm_layout_present_with_fonts_opacity(platform, textures, fonts, layout,
                                          wide, mode, parent_matrix, 1.0f);
}

static void present_with_opacity(
    WmPlatform *platform, WmTextureCache *textures, WmFontCache *fonts,
    const WmLayout *layout, bool wide, WmLayoutMode mode,
    const float parent_matrix[12], WmLayoutPaneCallback filter,
    void *filter_context, float opacity,
    WmLayoutDrawPredicate draw_predicate, void *draw_predicate_context) {
    if (!platform || !layout) return;
    PresentContext context = {
        .platform = platform,
        .textures = textures,
        .fonts = fonts,
        .screen_scale_x = (float)WM_FRAME_WIDTH / (wide ? 832.0f : 608.0f),
        .filter = filter,
        .filter_context = filter_context,
        .draw_predicate = draw_predicate,
        .draw_predicate_context = draw_predicate_context
    };
    const WmLayoutDrawOptions options = {
        .wide = wide,
        .mode = mode,
        .alpha = clamp_unit(opacity),
        .parent_matrix = parent_matrix,
        .on_pane = visit_pane,
        .on_quad = draw_quad,
        .image_provider = resolve_image,
        .context = &context
    };
    wm_layout_draw(layout, &options);
}

void wm_layout_present_with_fonts_opacity(
    WmPlatform *platform, WmTextureCache *textures, WmFontCache *fonts,
    const WmLayout *layout, bool wide, WmLayoutMode mode,
    const float parent_matrix[12], float opacity) {
    present_with_opacity(platform, textures, fonts, layout, wide, mode,
                         parent_matrix, NULL, NULL, opacity, NULL, NULL);
}

void wm_layout_present_with_fonts_opacity_masked(
    WmPlatform *platform, WmTextureCache *textures, WmFontCache *fonts,
    const WmLayout *layout, bool wide, WmLayoutMode mode,
    const float parent_matrix[12], float opacity,
    WmLayoutDrawPredicate predicate, void *predicate_context) {
    present_with_opacity(platform, textures, fonts, layout, wide, mode,
                         parent_matrix, NULL, NULL, opacity, predicate,
                         predicate_context);
}

void wm_layout_present_filtered_with_fonts(
    WmPlatform *platform, WmTextureCache *textures, WmFontCache *fonts,
    const WmLayout *layout, bool wide, WmLayoutMode mode,
    const float parent_matrix[12], WmLayoutPaneCallback filter,
    void *filter_context) {
    present_with_opacity(platform, textures, fonts, layout, wide, mode,
                         parent_matrix, filter, filter_context, 1.0f,
                         NULL, NULL);
}
