#include "layout_runtime_internal.h"

#include <math.h>
#include <string.h>

static void matrix_identity(float matrix[12]) {
    const float identity[12] = {1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0};
    memcpy(matrix, identity, sizeof(identity));
}

static void matrix_multiply(const float a[12], const float b[12], float result[12]) {
    for (int row = 0; row < 3; row++) {
        for (int column = 0; column < 4; column++) {
            result[row * 4 + column] =
                (column == 3 ? a[row * 4 + 3] : 0) + a[row * 4] * b[column] +
                a[row * 4 + 1] * b[4 + column] + a[row * 4 + 2] * b[8 + column];
        }
    }
}

static void pane_matrix(const LayoutPane *pane, float scale_x, float result[12]) {
    double x = (double)pane->rotation[0] * WM_LAYOUT_PI / 180.0;
    double y = (double)pane->rotation[1] * WM_LAYOUT_PI / 180.0;
    double z = (double)pane->rotation[2] * WM_LAYOUT_PI / 180.0;
    double cx = cos(x), sx = sin(x), cy = cos(y), sy = sin(y);
    double cz = cos(z), sz = sin(z);
    double scale_y = pane->scale[1];
    result[0] = (float)(cz * cy * scale_x);
    result[1] = (float)((cz * sy * sx - sz * cx) * scale_y);
    result[2] = (float)(cz * sy * cx + sz * sx);
    result[3] = pane->translation[0];
    result[4] = (float)(sz * cy * scale_x);
    result[5] = (float)((sz * sy * sx + cz * cx) * scale_y);
    result[6] = (float)(sz * sy * cx - cz * sx);
    result[7] = pane->translation[1];
    result[8] = (float)(-sy * scale_x);
    result[9] = (float)(cy * sx * scale_y);
    result[10] = (float)(cy * cx);
    result[11] = pane->translation[2];
}

static void pane_world_matrix(const LayoutPane *pane, int index, const float parent[12],
                              bool wide, WmLayoutMode mode, float world[12]) {
    float scale_x = pane->scale[0];
    if (wide && mode != WM_LAYOUT_LOCAL) {
        if (index == 0 && mode == WM_LAYOUT_IPL)
            scale_x *= 832.0f / 608.0f;
        if (pane->flags & LAYOUT_PANE_WIDE_COMPENSATION)
            scale_x *= 608.0f / 832.0f;
    }

    float local[12];
    pane_matrix(pane, scale_x, local);
    matrix_multiply(parent, local, world);
}

static void transform_point(const float matrix[12], float x, float y, float result[3]) {
    result[0] = matrix[0] * x + matrix[1] * y + matrix[3];
    result[1] = matrix[4] * x + matrix[5] * y + matrix[7];
    result[2] = matrix[8] * x + matrix[9] * y + matrix[11];
}

static void pane_corners(const LayoutPane *pane, const float matrix[12],
                         float corners[4][3]) {
    float width = pane->size[0], height = pane->size[1];
    float left = -(float)(pane->origin % 3) * width / 2;
    float top = (float)(pane->origin / 3) * height / 2;
    transform_point(matrix, left, top, corners[0]);
    transform_point(matrix, left + width, top, corners[1]);
    transform_point(matrix, left, top - height, corners[2]);
    transform_point(matrix, left + width, top - height, corners[3]);
}

static const WmLayoutTexture *texture_by_name(const WmLayout *layout,
                                              const char *name) {
    if (!name || !*name)
        return NULL;
    /* resourceTextures wins over the static txl1 list, like textureDescriptor. */
    for (size_t index = layout->texture_count; index > 0; index--) {
        if (strcmp(layout->textures[index - 1].name, name) == 0) {
            return &layout->textures[index - 1];
        }
    }
    return NULL;
}

static const WmLayoutTexture *texture_for_map(const WmLayout *layout,
                                              const LayoutTextureMap *map) {
    if (map->texture_name[0])
        return texture_by_name(layout, map->texture_name);
    if (map->texture_index >= 0 && (size_t)map->texture_index < layout->texture_count) {
        return &layout->textures[map->texture_index];
    }
    return NULL;
}

void wm_layout_material_info_from_data(const LayoutMaterial *material,
                                       WmLayoutMaterialInfo *info) {
    memset(info, 0, sizeof(*info));
    info->name = material->name;
    for (size_t color = 0; color < 3; color++) {
        for (size_t channel = 0; channel < 4; channel++) {
            info->registers[color][channel] = material->registers[color][channel] / 255;
        }
    }
    for (size_t color = 0; color < 4; color++) {
        for (size_t channel = 0; channel < 4; channel++) {
            info->konst_colors[color][channel] =
                material->konst_colors[color][channel] / 255;
        }
        info->material_color[color] = material->material_color[color] / 255;
        info->blend_mode[color] = material->blend_mode[color];
        info->alpha_compare[color] = material->alpha_compare[color];
        info->tev_swap_table[color] = material->tev_swap_table[color];
    }
    info->channel_control[0] = material->channel_control[0];
    info->channel_control[1] = material->channel_control[1];
    info->tev_stage_count = material->tev_stage_count;
    info->tev_stages = material->tev_stages;
    info->texture_map_count = (unsigned)material->map_count;
    info->has_blend_mode = material->has_blend_mode;
    info->has_alpha_compare = material->has_alpha_compare;
}

static void quad_uv(const LayoutPane *pane, const LayoutMaterial *material, size_t unit,
                    float output[4][2]) {
    static const float defaults[4][2] = {{0, 0}, {1, 0}, {0, 1}, {1, 1}};
    const LayoutTexCoordGen *generator =
        unit < material->generator_count ? &material->generators[unit] : NULL;
    int source = generator ? generator->source : 4 + (int)unit;
    int uv_set = source - 4;
    if (uv_set < 0 || (size_t)uv_set >= pane->tex_coord_count) {
        uv_set = pane->tex_coord_count ? 0 : -1;
    }
    int matrix = generator ? generator->matrix : 30 + (int)unit * 3;
    int srt_index = matrix == 60 ? -1 : (int)floorf((float)(matrix - 30) / 3);
    const LayoutSrt *srt = srt_index >= 0 && (size_t)srt_index < material->srt_count
                               ? &material->srts[srt_index]
                               : NULL;
    double radians = srt ? (double)srt->rotation * WM_LAYOUT_PI / 180.0 : 0;
    double cosine = cos(radians), sine = sin(radians);
    for (size_t corner = 0; corner < 4; corner++) {
        float u =
            uv_set < 0 ? defaults[corner][0] : pane->tex_coords[uv_set][corner][0];
        float v =
            uv_set < 0 ? defaults[corner][1] : pane->tex_coords[uv_set][corner][1];
        if (srt) {
            float x = (u - 0.5f) * srt->scale[0];
            float y = (v - 0.5f) * srt->scale[1];
            output[corner][0] =
                (float)(cosine * x - sine * y) + 0.5f + srt->translate[0];
            output[corner][1] =
                (float)(sine * x + cosine * y) + 0.5f + srt->translate[1];
        } else {
            output[corner][0] = u;
            output[corner][1] = v;
        }
    }
}

static void emit_picture(const WmLayout *layout, const LayoutPane *pane,
                         const float corners[4][3], float alpha,
                         const WmLayoutDrawOptions *options) {
    if (!options->on_quad || pane->material < 0 ||
        (size_t)pane->material >= layout->material_count)
        return;
    const LayoutMaterial *material = &layout->materials[pane->material];
    WmLayoutMaterialInfo info;
    wm_layout_material_info_from_data(material, &info);
    WmLayoutQuad quad = {.pane_name = pane->name, .material = &info};
    for (size_t unit = 0; unit < 4; unit++) {
        float uvs[4][2];
        quad_uv(pane, material, unit, uvs);
        for (size_t corner = 0; corner < 4; corner++) {
            quad.vertices[corner].uv[unit][0] = uvs[corner][0];
            quad.vertices[corner].uv[unit][1] = uvs[corner][1];
        }
        if (unit >= material->map_count)
            continue;
        const LayoutTextureMap *map = &material->maps[unit];
        WmLayoutTextureBinding *binding = &quad.textures[unit];
        binding->resource = texture_for_map(layout, map);
        binding->wrap_s = map->wrap_s;
        binding->wrap_t = map->wrap_t;
        if (binding->resource && options->image_provider) {
            uint32_t handle = 0;
            if (options->image_provider(options->context, binding->resource, &handle)) {
                binding->handle = handle;
            }
        }
    }
    for (size_t corner = 0; corner < 4; corner++) {
        WmLayoutVertex *vertex = &quad.vertices[corner];
        memcpy(vertex->position, corners[corner], sizeof(vertex->position));
        const float *color = material->channel_control[0] == 0
                                 ? material->material_color
                                 : pane->vertex_colors[corner];
        float opacity = material->channel_control[1] == 0
                            ? material->material_color[3]
                            : pane->vertex_colors[corner][3];
        for (size_t channel = 0; channel < 3; channel++) {
            vertex->color[channel] = color[channel] / 255;
        }
        vertex->color[3] = opacity / 255 * alpha;
    }
    options->on_quad(options->context, &quad);
}

static const WmLayoutTexture *
window_frame_texture(const WmLayout *layout, const LayoutPane *pane, size_t frame) {
    if (frame >= pane->frame_count)
        return NULL;
    int material_index = pane->frame_materials[frame];
    if (material_index < 0 || (size_t)material_index >= layout->material_count)
        return NULL;
    const LayoutMaterial *material = &layout->materials[material_index];
    return material->map_count ? texture_for_map(layout, &material->maps[0]) : NULL;
}

static void window_frame_uv(const float size[2], const WmLayoutTexture *texture,
                            unsigned flip, unsigned corner, float result[4][2]) {
    static const uint8_t coordinates[6][4][2] = {
        {{0, 0}, {1, 0}, {0, 1}, {1, 1}}, {{1, 0}, {0, 0}, {1, 1}, {0, 1}},
        {{0, 1}, {1, 1}, {0, 0}, {1, 0}}, {{0, 1}, {0, 0}, {1, 1}, {1, 0}},
        {{1, 1}, {0, 1}, {1, 0}, {0, 0}}, {{1, 0}, {1, 1}, {0, 0}, {0, 1}}};
    static const uint8_t indices[6][2] = {{0, 1}, {0, 1}, {0, 1},
                                          {1, 0}, {0, 1}, {1, 0}};
    const float texture_size[2] = {(float)texture->width, (float)texture->height};
    memset(result, 0, sizeof(float) * 8);
    for (unsigned axis = 0; axis < 2; axis++) {
        unsigned index = indices[flip][axis];
        unsigned bit = 1u << axis;
        float anchor = coordinates[flip][corner][index];
        float direction = (float)coordinates[flip][corner ^ bit][index] - anchor;
        float opposite = anchor + size[axis] / (direction * texture_size[index]);
        for (unsigned vertex = 0; vertex < 4; vertex++) {
            result[vertex][index] =
                (vertex & bit) == (corner & bit) ? anchor : opposite;
        }
    }
}

static void emit_window_piece(const WmLayout *layout, const LayoutPane *pane,
                              const float matrix[12], float alpha, float x, float y,
                              float width, float height, int material,
                              const float (*uv)[2], bool white_vertices,
                              const WmLayoutDrawOptions *options) {
    LayoutPane piece = *pane;
    piece.origin = 0;
    piece.material = material;
    piece.size[0] = width;
    piece.size[1] = height;
    if (uv) {
        piece.tex_coord_count = 1;
        memcpy(piece.tex_coords[0], uv, sizeof(piece.tex_coords[0]));
    }
    if (white_vertices) {
        for (size_t vertex = 0; vertex < 4; vertex++) {
            for (size_t channel = 0; channel < 4; channel++) {
                piece.vertex_colors[vertex][channel] = 255;
            }
        }
    }
    float pane_left = -(float)(pane->origin % 3) * pane->size[0] / 2;
    float pane_top = (float)(pane->origin / 3) * pane->size[1] / 2;
    float left = pane_left + x, top = pane_top - y;
    float corners[4][3];
    transform_point(matrix, left, top, corners[0]);
    transform_point(matrix, left + width, top, corners[1]);
    transform_point(matrix, left, top - height, corners[2]);
    transform_point(matrix, left + width, top - height, corners[3]);
    emit_picture(layout, &piece, corners, alpha, options);
}

static void emit_window_frame(const WmLayout *layout, const LayoutPane *pane,
                              const float matrix[12], float alpha, unsigned frame,
                              unsigned corner, float x, float y, float width,
                              float height, int forced_flip,
                              const WmLayoutDrawOptions *options) {
    const WmLayoutTexture *texture = window_frame_texture(layout, pane, frame);
    if (!texture || texture->width <= 0 || texture->height <= 0)
        return;
    float size[2] = {width, height};
    float uv[4][2];
    unsigned flip = forced_flip >= 0 ? (unsigned)forced_flip : pane->frame_flips[frame];
    window_frame_uv(size, texture, flip, corner, uv);
    emit_window_piece(layout, pane, matrix, alpha, x, y, width, height,
                      pane->frame_materials[frame], uv, true, options);
}

static void emit_window(const WmLayout *layout, const LayoutPane *pane,
                        const float matrix[12], float alpha,
                        const WmLayoutDrawOptions *options) {
    if (!options->on_quad)
        return;
    size_t count = pane->frame_count;
    float width = pane->size[0], height = pane->size[1];
    float left = 0, right = 0, top = 0, bottom = 0;
    if (count == 1 || count == 4 || count == 8) {
        const WmLayoutTexture *first = window_frame_texture(layout, pane, 0);
        const WmLayoutTexture *last =
            window_frame_texture(layout, pane, count == 1 ? 0 : 3);
        if (first) {
            left = (float)first->width;
            top = (float)first->height;
        }
        if (last) {
            right = (float)last->width;
            bottom = (float)last->height;
        }
    }
    const float *inflation = pane->inflation;
    emit_window_piece(layout, pane, matrix, alpha, left - inflation[0],
                      top - inflation[2],
                      width - left - right + inflation[0] + inflation[1],
                      height - top - bottom + inflation[2] + inflation[3],
                      pane->material, NULL, false, options);
    if (count == 1 || count == 4) {
        emit_window_frame(layout, pane, matrix, alpha, 0, 0, 0, 0, width - right, top,
                          count == 1 ? 0 : -1, options);
        emit_window_frame(layout, pane, matrix, alpha, count == 1 ? 0 : 1, 1,
                          width - right, 0, right, height - bottom, count == 1 ? 1 : -1,
                          options);
        emit_window_frame(layout, pane, matrix, alpha, count == 1 ? 0 : 3, 3, left,
                          height - bottom, width - left, bottom, count == 1 ? 4 : -1,
                          options);
        emit_window_frame(layout, pane, matrix, alpha, count == 1 ? 0 : 2, 2, 0, top,
                          left, height - top, count == 1 ? 2 : -1, options);
    } else if (count == 8) {
        emit_window_frame(layout, pane, matrix, alpha, 0, 0, 0, 0, left, top, -1,
                          options);
        emit_window_frame(layout, pane, matrix, alpha, 6, 0, left, 0,
                          width - left - right, top, -1, options);
        emit_window_frame(layout, pane, matrix, alpha, 1, 1, width - right, 0, right,
                          top, -1, options);
        emit_window_frame(layout, pane, matrix, alpha, 5, 1, width - right, top, right,
                          height - top - bottom, -1, options);
        emit_window_frame(layout, pane, matrix, alpha, 3, 3, width - right,
                          height - bottom, right, bottom, -1, options);
        emit_window_frame(layout, pane, matrix, alpha, 7, 3, left, height - bottom,
                          width - left - right, bottom, -1, options);
        emit_window_frame(layout, pane, matrix, alpha, 2, 2, 0, height - bottom, left,
                          bottom, -1, options);
        emit_window_frame(layout, pane, matrix, alpha, 4, 2, 0, top, left,
                          height - top - bottom, -1, options);
    }
}

static uint8_t text_color_byte(float value) {
    if (!isfinite(value) || value <= 0)
        return 0;
    if (value >= 255)
        return 255;
    return (uint8_t)lroundf(value);
}

static void copy_font_pane(const LayoutPane *source, WmFontPane *target) {
    *target = (WmFontPane){.size = {source->size[0], source->size[1]},
                           .origin = source->origin,
                           .text_position = source->text_position,
                           .font_size = {source->font_size[0], source->font_size[1]},
                           .char_space = source->char_space,
                           .line_space = source->line_space,
                           .no_wrap = source->no_wrap};
    for (size_t channel = 0; channel < 4; channel++) {
        target->top_color[channel] = text_color_byte(source->text_colors[0][channel]);
        target->bottom_color[channel] =
            text_color_byte(source->text_colors[1][channel]);
    }
}

static const char *pane_font_name(const WmLayout *layout, const LayoutPane *pane) {
    if (pane->font_index < 0 || (size_t)pane->font_index >= layout->font_count)
        return NULL;
    return layout->fonts[pane->font_index];
}

static void pane_text_info(const WmLayout *layout, const LayoutPane *pane,
                           WmLayoutTextInfo *text, WmLayoutMaterialInfo *material) {
    *text = (WmLayoutTextInfo){0};
    text->value = pane->text ? pane->text : "";
    text->font_index = pane->font_index;
    text->font_name = pane_font_name(layout, pane);
    text->material_index = pane->material;
    if (pane->material >= 0 && (size_t)pane->material < layout->material_count) {
        wm_layout_material_info_from_data(&layout->materials[pane->material], material);
        text->material = material;
    }

    copy_font_pane(pane, &text->pane);
    text->horizontal_align = pane->text_position % 3;
    text->vertical_align = pane->text_position / 3;
    /* Preserve the animated float colors as well as the rounded font colors. */
    memcpy(text->colors, pane->text_colors, sizeof(text->colors));
    text->color_range_count = pane->text_color_range_count;
    if (text->color_range_count) {
        memcpy(text->color_ranges, pane->text_color_ranges,
               text->color_range_count * sizeof(text->color_ranges[0]));
    }
}

bool wm_layout_pane_font(const WmLayout *layout, const char *name, WmFontPane *pane,
                         const char **font_name) {
    if (!layout || !name || !pane || !font_name)
        return false;
    int index = wm_layout_find_pane(layout, name);
    if (index < 0 || strcmp(layout->panes[index].type, "txt1") != 0) {
        return false;
    }
    const LayoutPane *source = &layout->panes[index];
    copy_font_pane(source, pane);
    *font_name = pane_font_name(layout, source);
    return true;
}

static void visit_pane(const WmLayout *layout, int index, const float parent_matrix[12],
                       float ancestor_alpha, const WmLayoutDrawOptions *options) {
    const LayoutPane *pane = &layout->panes[index];
    if (!(pane->flags & LAYOUT_PANE_VISIBLE))
        return;
    float world[12];
    pane_world_matrix(pane, index, parent_matrix, options->wide, options->mode, world);
    float pane_alpha = pane->alpha / 255;
    float alpha = options->alpha * pane_alpha * ancestor_alpha;
    WmLayoutPaneView view = {
        .name = pane->name, .type = pane->type, .alpha = alpha, .flags = pane->flags};
    WmLayoutTextInfo text;
    WmLayoutMaterialInfo text_material;
    if (strcmp(pane->type, "txt1") == 0) {
        pane_text_info(layout, pane, &text, &text_material);
        view.text = &text;
    }
    memcpy(view.matrix, world, sizeof(world));
    pane_corners(pane, world, view.corners);
    if (options->on_pane && !options->on_pane(options->context, &view))
        return;
    if (alpha > 0) {
        if (strcmp(pane->type, "pic1") == 0) {
            emit_picture(layout, pane, view.corners, alpha, options);
        } else if (strcmp(pane->type, "wnd1") == 0 && pane->material >= 0) {
            emit_window(layout, pane, world, alpha, options);
        }
    }
    float child_alpha =
        ancestor_alpha * ((pane->flags & LAYOUT_PANE_CHILD_ALPHA) ? pane_alpha : 1);
    for (int child = pane->first_child; child >= 0;
         child = layout->panes[child].next_sibling) {
        visit_pane(layout, child, world, child_alpha, options);
    }
}

void wm_layout_draw(const WmLayout *layout, const WmLayoutDrawOptions *options) {
    if (!layout || !options || !layout->pane_count)
        return;
    float identity[12];
    matrix_identity(identity);
    visit_pane(layout, 0, options->parent_matrix ? options->parent_matrix : identity, 1,
               options);
}

static void visit_all_transforms(const WmLayout *layout, int index,
                                 const float parent_matrix[12], bool wide,
                                 WmLayoutMode mode, WmLayoutPaneCallback visitor,
                                 void *context) {
    const LayoutPane *pane = &layout->panes[index];
    float world[12];
    pane_world_matrix(pane, index, parent_matrix, wide, mode, world);
    WmLayoutPaneView view = {.name = pane->name,
                             .type = pane->type,
                             .alpha = pane->alpha / 255.0f,
                             .flags = pane->flags};
    memcpy(view.matrix, world, sizeof(world));
    if (visitor)
        visitor(context, &view);
    for (int child = pane->first_child; child >= 0;
         child = layout->panes[child].next_sibling)
        visit_all_transforms(layout, child, world, wide, mode, visitor, context);
}

void wm_layout_visit_all_transforms(const WmLayout *layout, bool wide,
                                    WmLayoutMode mode, const float parent_matrix[12],
                                    WmLayoutPaneCallback visitor, void *context) {
    if (!layout || !layout->pane_count || !visitor)
        return;
    float identity[12];
    matrix_identity(identity);
    visit_all_transforms(layout, 0, parent_matrix ? parent_matrix : identity, wide,
                         mode, visitor, context);
}
