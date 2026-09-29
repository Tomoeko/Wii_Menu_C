#include "layout_runtime_internal.h"
#include "wii_menu/support/error.h"
#include "wii_menu/support/json.h"

#include <math.h>
#include <stdlib.h>
#include <string.h>

static bool json_float(const WmJson *json, size_t token, float *value) {
    if (token >= json->count || json->tokens[token].type != WM_JSON_NUMBER)
        return false;
    size_t length = json->tokens[token].end - json->tokens[token].start;
    if (!length || length >= 64)
        return false;
    char text[64];
    memcpy(text, json->source + json->tokens[token].start, length);
    text[length] = '\0';
    char *end;
    double number = strtod(text, &end);
    if (*end || !isfinite(number) || number > 3.402823466e38 ||
        number < -3.402823466e38)
        return false;
    *value = (float)number;
    return true;
}

static bool json_bool(const WmJson *json, size_t token, bool *value) {
    if (token >= json->count || json->tokens[token].type != WM_JSON_BOOLEAN)
        return false;
    *value = json->source[json->tokens[token].start] == 't';
    return true;
}

static bool json_numbers(const WmJson *json, size_t token, float *values,
                         size_t count) {
    if (token >= json->count || json->tokens[token].type != WM_JSON_ARRAY ||
        json->tokens[token].children < count)
        return false;
    size_t cursor = token + 1;
    for (size_t index = 0; index < count; index++) {
        if (!json_float(json, cursor, &values[index]))
            return false;
        cursor = json->tokens[cursor].next;
    }
    return true;
}

static bool json_integer_member(const WmJson *json, size_t object, const char *name,
                                int *value) {
    return wm_json_integer(json, wm_json_member(json, object, name), value);
}

static bool json_array_colors(const WmJson *json, size_t array, float colors[][4],
                              size_t count) {
    if (array >= json->count || json->tokens[array].type != WM_JSON_ARRAY ||
        json->tokens[array].children < count)
        return false;
    size_t cursor = array + 1;
    for (size_t index = 0; index < count; index++) {
        if (!json_numbers(json, cursor, colors[index], 4))
            return false;
        cursor = json->tokens[cursor].next;
    }
    return true;
}

static bool json_bytes(const WmJson *json, size_t array, uint8_t *bytes, size_t count) {
    if (array >= json->count || json->tokens[array].type != WM_JSON_ARRAY ||
        json->tokens[array].children < count)
        return false;
    size_t cursor = array + 1;
    for (size_t index = 0; index < count; index++) {
        int value;
        if (!wm_json_integer(json, cursor, &value) || value < 0 || value > 255)
            return false;
        bytes[index] = (uint8_t)value;
        cursor = json->tokens[cursor].next;
    }
    return true;
}

int wm_layout_find_pane(const WmLayout *layout, const char *name) {
    /* JS Map lookup in the source project keeps the last duplicate name. */
    for (size_t index = layout->pane_count; index > 0; index--) {
        if (strcmp(layout->base_panes[index - 1].name, name) == 0)
            return (int)index - 1;
    }
    return -1;
}

static int find_material(const WmLayout *layout, const char *name) {
    for (size_t index = layout->material_count; index > 0; index--) {
        if (strcmp(layout->base_materials[index - 1].name, name) == 0) {
            return (int)index - 1;
        }
    }
    return -1;
}

static bool parse_texture(const WmJson *json, size_t token, WmLayoutTexture *texture,
                          const char *fallback_name) {
    memset(texture, 0, sizeof(*texture));
    size_t name = wm_json_member(json, token, "name");
    if (!wm_json_copy(json, name, texture->name, sizeof(texture->name))) {
        if (!fallback_name || strlen(fallback_name) >= sizeof(texture->name))
            return false;
        strcpy(texture->name, fallback_name);
    }
    size_t url = wm_json_member(json, token, "url");
    if (url != WM_JSON_INVALID &&
        !wm_json_copy(json, url, texture->url, sizeof(texture->url)))
        return false;
    int width = 0, height = 0;
    if (json_integer_member(json, token, "width", &width) && width >= 0) {
        texture->width = width;
    }
    if (json_integer_member(json, token, "height", &height) && height >= 0) {
        texture->height = height;
    }
    bool missing = false;
    if (json_bool(json, wm_json_member(json, token, "missing"), &missing)) {
        texture->missing = missing;
    }
    return true;
}

static bool parse_textures(WmLayout *layout, const WmJson *json) {
    size_t static_array = wm_json_member(json, 0, "textures");
    size_t resources = wm_json_member(json, 0, "resourceTextures");
    size_t static_count =
        static_array < json->count && json->tokens[static_array].type == WM_JSON_ARRAY
            ? json->tokens[static_array].children
            : 0;
    size_t resource_count =
        resources < json->count && json->tokens[resources].type == WM_JSON_OBJECT
            ? json->tokens[resources].children
            : 0;
    if (static_count + resource_count > WM_LAYOUT_MAX_TEXTURES)
        return false;
    layout->texture_count = static_count + resource_count;
    layout->textures = calloc(layout->texture_count ? layout->texture_count : 1,
                              sizeof(*layout->textures));
    if (!layout->textures)
        return false;
    size_t index = 0;
    if (static_count) {
        size_t cursor = static_array + 1;
        while (cursor < json->tokens[static_array].next) {
            if (!parse_texture(json, cursor, &layout->textures[index++], NULL))
                return false;
            cursor = json->tokens[cursor].next;
        }
    }
    if (resource_count) {
        size_t cursor = resources + 1;
        while (cursor < json->tokens[resources].next) {
            char name[128];
            size_t value = json->tokens[cursor].next;
            if (!wm_json_copy(json, cursor, name, sizeof(name)) ||
                !parse_texture(json, value, &layout->textures[index++], name))
                return false;
            cursor = json->tokens[value].next;
        }
    }
    return true;
}

static bool parse_fonts(WmLayout *layout, const WmJson *json) {
    size_t array = wm_json_member(json, 0, "fonts");
    if (array == WM_JSON_INVALID)
        return true;
    if (json->tokens[array].type != WM_JSON_ARRAY ||
        json->tokens[array].children > WM_LAYOUT_MAX_FONTS)
        return false;
    layout->font_count = json->tokens[array].children;
    layout->fonts =
        calloc(layout->font_count ? layout->font_count : 1, sizeof(*layout->fonts));
    if (!layout->fonts)
        return false;
    size_t cursor = array + 1;
    for (size_t index = 0; index < layout->font_count; index++) {
        if (!wm_json_copy(json, cursor, layout->fonts[index],
                          sizeof(layout->fonts[index])))
            return false;
        cursor = json->tokens[cursor].next;
    }
    return true;
}

static bool parse_material(const WmJson *json, size_t token, LayoutMaterial *material) {
    memset(material, 0, sizeof(*material));
    for (size_t index = 0; index < 4; index++)
        material->material_color[index] = 255;
    for (size_t index = 0; index < 4; index++) {
        for (size_t channel = 0; channel < 4; channel++) {
            material->konst_colors[index][channel] = 255;
        }
    }
    material->channel_control[0] = 1;
    material->channel_control[1] = 1;
    material->blend_mode[0] = 1;
    material->blend_mode[1] = 4;
    material->blend_mode[2] = 5;
    material->tev_swap_table[0] = 228;
    material->tev_swap_table[1] = 192;
    material->tev_swap_table[2] = 213;
    material->tev_swap_table[3] = 234;
    if (!wm_json_copy(json, wm_json_member(json, token, "name"), material->name,
                      sizeof(material->name)) ||
        !json_array_colors(json, wm_json_member(json, token, "colors"),
                           material->registers, 3))
        return false;
    size_t konst = wm_json_member(json, token, "konstColors");
    if (konst != WM_JSON_INVALID &&
        !json_array_colors(json, konst, material->konst_colors, 4))
        return false;
    size_t color = wm_json_member(json, token, "materialColor");
    if (color != WM_JSON_INVALID &&
        !json_numbers(json, color, material->material_color, 4))
        return false;
    material->has_material_color = color != WM_JSON_INVALID;
    size_t control = wm_json_member(json, token, "channelControl");
    if (control != WM_JSON_INVALID &&
        !json_bytes(json, control, material->channel_control, 2))
        return false;
    size_t blend = wm_json_member(json, token, "blendMode");
    if (blend != WM_JSON_INVALID) {
        if (!json_bytes(json, blend, material->blend_mode, 4))
            return false;
        material->has_blend_mode = true;
    }
    size_t compare = wm_json_member(json, token, "alphaCompare");
    if (compare != WM_JSON_INVALID) {
        if (!json_bytes(json, compare, material->alpha_compare, 4))
            return false;
        material->has_alpha_compare = true;
    }
    size_t stages = wm_json_member(json, token, "tevStages");
    if (stages < json->count && json->tokens[stages].type == WM_JSON_ARRAY) {
        material->tev_stage_count = (unsigned)json->tokens[stages].children;
        if (material->tev_stage_count > 32)
            return false;
        size_t stage = stages + 1;
        for (size_t index = 0; index < material->tev_stage_count; index++) {
            if (!json_bytes(json, stage, material->tev_stages[index], 16))
                return false;
            stage = json->tokens[stage].next;
        }
    }
    size_t swap = wm_json_member(json, token, "tevSwapTable");
    if (swap != WM_JSON_INVALID && !json_bytes(json, swap, material->tev_swap_table, 4))
        return false;
    size_t maps = wm_json_member(json, token, "textureMaps");
    if (maps < json->count && json->tokens[maps].type == WM_JSON_ARRAY) {
        size_t cursor = maps + 1;
        while (cursor < json->tokens[maps].next) {
            if (material->map_count < 4) {
                LayoutTextureMap *map = &material->maps[material->map_count++];
                int index = -1, wrap = 0;
                if (json_integer_member(json, cursor, "texture", &index)) {
                    map->texture_index = index;
                } else {
                    map->texture_index = -1;
                }
                size_t name = wm_json_member(json, cursor, "textureName");
                if (name != WM_JSON_INVALID &&
                    !wm_json_copy(json, name, map->texture_name,
                                  sizeof(map->texture_name)))
                    return false;
                if (json_integer_member(json, cursor, "wrapS", &wrap) && wrap >= 0 &&
                    wrap <= 2) {
                    map->wrap_s = (uint8_t)wrap;
                }
                if (json_integer_member(json, cursor, "wrapT", &wrap) && wrap >= 0 &&
                    wrap <= 2) {
                    map->wrap_t = (uint8_t)wrap;
                }
            }
            cursor = json->tokens[cursor].next;
        }
    }
    size_t srts = wm_json_member(json, token, "textureSRTs");
    if (srts < json->count && json->tokens[srts].type == WM_JSON_ARRAY) {
        size_t cursor = srts + 1;
        while (cursor < json->tokens[srts].next) {
            if (material->srt_count < 16) {
                LayoutSrt *srt = &material->srts[material->srt_count++];
                if (!json_numbers(json, wm_json_member(json, cursor, "translate"),
                                  srt->translate, 2) ||
                    !json_float(json, wm_json_member(json, cursor, "rotation"),
                                &srt->rotation) ||
                    !json_numbers(json, wm_json_member(json, cursor, "scale"),
                                  srt->scale, 2))
                    return false;
            }
            cursor = json->tokens[cursor].next;
        }
    }
    size_t generators = wm_json_member(json, token, "texCoordGens");
    if (generators < json->count && json->tokens[generators].type == WM_JSON_ARRAY) {
        size_t cursor = generators + 1;
        while (cursor < json->tokens[generators].next) {
            if (material->generator_count < 4) {
                LayoutTexCoordGen *generator =
                    &material->generators[material->generator_count++];
                if (!json_integer_member(json, cursor, "source", &generator->source) ||
                    !json_integer_member(json, cursor, "matrix", &generator->matrix)) {
                    return false;
                }
            }
            cursor = json->tokens[cursor].next;
        }
    }
    return true;
}

static bool parse_materials(WmLayout *layout, const WmJson *json) {
    size_t array = wm_json_member(json, 0, "materials");
    if (array >= json->count || json->tokens[array].type != WM_JSON_ARRAY ||
        json->tokens[array].children > WM_LAYOUT_MAX_MATERIALS)
        return false;
    layout->material_count = json->tokens[array].children;
    layout->base_materials = calloc(layout->material_count ? layout->material_count : 1,
                                    sizeof(*layout->base_materials));
    layout->materials = calloc(layout->material_count ? layout->material_count : 1,
                               sizeof(*layout->materials));
    if (!layout->base_materials || !layout->materials)
        return false;
    size_t cursor = array + 1;
    for (size_t index = 0; index < layout->material_count; index++) {
        if (!parse_material(json, cursor, &layout->base_materials[index]))
            return false;
        cursor = json->tokens[cursor].next;
    }
    memcpy(layout->materials, layout->base_materials,
           layout->material_count * sizeof(*layout->materials));
    return true;
}

static bool count_panes(const WmJson *json, size_t token, size_t *count) {
    if (token >= json->count || json->tokens[token].type != WM_JSON_OBJECT ||
        ++*count > WM_LAYOUT_MAX_PANES)
        return false;
    size_t children = wm_json_member(json, token, "children");
    if (children == WM_JSON_INVALID)
        return true;
    if (json->tokens[children].type != WM_JSON_ARRAY)
        return false;
    size_t cursor = children + 1;
    while (cursor < json->tokens[children].next) {
        if (!count_panes(json, cursor, count))
            return false;
        cursor = json->tokens[cursor].next;
    }
    return true;
}

static bool parse_pane(WmLayout *layout, const WmJson *json, size_t token,
                       int *result) {
    if (layout->next_pane >= layout->pane_count)
        return false;
    int index = (int)layout->next_pane++;
    LayoutPane *pane = &layout->base_panes[index];
    pane->first_child = -1;
    pane->next_sibling = -1;
    pane->material = -1;
    pane->font_index = -1;
    pane->scale[0] = pane->scale[1] = 1;
    for (size_t vertex = 0; vertex < 4; vertex++) {
        for (size_t channel = 0; channel < 4; channel++) {
            pane->vertex_colors[vertex][channel] = 255;
        }
    }
    for (size_t color = 0; color < 2; color++) {
        for (size_t channel = 0; channel < 4; channel++) {
            pane->text_colors[color][channel] = 255;
        }
    }
    if (!wm_json_copy(json, wm_json_member(json, token, "name"), pane->name,
                      sizeof(pane->name)) ||
        !wm_json_copy(json, wm_json_member(json, token, "type"), pane->type,
                      sizeof(pane->type)) ||
        !json_numbers(json, wm_json_member(json, token, "translation"),
                      pane->translation, 3) ||
        !json_numbers(json, wm_json_member(json, token, "rotation"), pane->rotation,
                      3) ||
        !json_numbers(json, wm_json_member(json, token, "scale"), pane->scale, 2) ||
        !json_numbers(json, wm_json_member(json, token, "size"), pane->size, 2))
        return false;
    int number;
    if (!json_integer_member(json, token, "flags", &number) || number < 0 ||
        number > 255) {
        return false;
    }
    pane->flags = (unsigned)number;
    if (!json_integer_member(json, token, "origin", &number) || number < 0 ||
        number > 8) {
        return false;
    }
    pane->origin = (unsigned)number;
    if (!json_float(json, wm_json_member(json, token, "alpha"), &pane->alpha))
        return false;
    if (json_integer_member(json, token, "material", &number))
        pane->material = number;
    size_t inflation = wm_json_member(json, token, "inflation");
    if (inflation != WM_JSON_INVALID &&
        !json_numbers(json, inflation, pane->inflation, 4))
        return false;

    size_t colors = wm_json_member(json, token, "vertexColors");
    if (colors != WM_JSON_INVALID) {
        if (!json_array_colors(json, colors, pane->vertex_colors, 4))
            return false;
        pane->has_vertex_colors = true;
    }
    colors = wm_json_member(json, token, "textColors");
    if (colors != WM_JSON_INVALID) {
        if (!json_array_colors(json, colors, pane->text_colors, 2))
            return false;
        pane->has_text_colors = true;
    }
    if (strcmp(pane->type, "txt1") == 0) {
        if (json_integer_member(json, token, "font", &number)) {
            if (number < 0 || number > 65535)
                return false;
            pane->font_index = number;
        }
        if (json_integer_member(json, token, "textPosition", &number)) {
            if (number < 0 || number > 8)
                return false;
            pane->text_position = (unsigned)number;
        }
        size_t field = wm_json_member(json, token, "fontSize");
        if (field != WM_JSON_INVALID && !json_numbers(json, field, pane->font_size, 2))
            return false;
        field = wm_json_member(json, token, "charSpace");
        if (field != WM_JSON_INVALID && !json_float(json, field, &pane->char_space))
            return false;
        field = wm_json_member(json, token, "lineSpace");
        if (field != WM_JSON_INVALID && !json_float(json, field, &pane->line_space))
            return false;
        field = wm_json_member(json, token, "noWrap");
        if (field != WM_JSON_INVALID && !json_bool(json, field, &pane->no_wrap))
            return false;
        field = wm_json_member(json, token, "text");
        size_t raw_length = field == WM_JSON_INVALID
                                ? 0
                                : json->tokens[field].end - json->tokens[field].start;
        if (raw_length > WM_LAYOUT_MAX_TEXT_BYTES * 6)
            return false;
        pane->text = malloc(raw_length + 1);
        if (!pane->text)
            return false;
        if (field == WM_JSON_INVALID)
            pane->text[0] = '\0';
        else if (!wm_json_copy(json, field, pane->text, raw_length + 1) ||
                 strlen(pane->text) > WM_LAYOUT_MAX_TEXT_BYTES)
            return false;
    }
    size_t tex_coords = wm_json_member(json, token, "texCoords");
    if (tex_coords < json->count && json->tokens[tex_coords].type == WM_JSON_ARRAY) {
        size_t set = tex_coords + 1;
        while (set < json->tokens[tex_coords].next) {
            if (pane->tex_coord_count < 4) {
                if (json->tokens[set].type != WM_JSON_ARRAY ||
                    json->tokens[set].children < 4)
                    return false;
                size_t point = set + 1;
                for (size_t vertex = 0; vertex < 4; vertex++) {
                    if (!json_numbers(json, point,
                                      pane->tex_coords[pane->tex_coord_count][vertex],
                                      2)) {
                        return false;
                    }
                    point = json->tokens[point].next;
                }
                pane->tex_coord_count++;
            }
            set = json->tokens[set].next;
        }
    }
    size_t frames = wm_json_member(json, token, "frames");
    if (frames < json->count && json->tokens[frames].type == WM_JSON_ARRAY) {
        if (json->tokens[frames].children > 8)
            return false;
        size_t cursor = frames + 1;
        while (cursor < json->tokens[frames].next) {
            if (!json_integer_member(json, cursor, "material", &number))
                return false;
            pane->frame_materials[pane->frame_count] = number;
            if (!json_integer_member(json, cursor, "flip", &number) || number < 0 ||
                number > 5)
                return false;
            pane->frame_flips[pane->frame_count++] = (uint8_t)number;
            cursor = json->tokens[cursor].next;
        }
    }

    size_t children = wm_json_member(json, token, "children");
    int previous = -1;
    if (children < json->count && json->tokens[children].type == WM_JSON_ARRAY) {
        size_t cursor = children + 1;
        while (cursor < json->tokens[children].next) {
            int child;
            if (!parse_pane(layout, json, cursor, &child))
                return false;
            if (previous < 0)
                pane->first_child = child;
            else
                layout->base_panes[previous].next_sibling = child;
            previous = child;
            cursor = json->tokens[cursor].next;
        }
    }
    *result = index;
    return true;
}

static bool parse_panes(WmLayout *layout, const WmJson *json) {
    size_t root = wm_json_member(json, 0, "root");
    size_t count = 0;
    if (!count_panes(json, root, &count))
        return false;
    layout->pane_count = count;
    layout->base_panes = calloc(count, sizeof(*layout->base_panes));
    layout->panes = calloc(count, sizeof(*layout->panes));
    layout->pose_text_overrides = calloc(count, sizeof(*layout->pose_text_overrides));
    layout->pose_text_capacities = calloc(count, sizeof(*layout->pose_text_capacities));
    layout->allowed_panes = calloc(count, sizeof(*layout->allowed_panes));
    layout->allowed_materials =
        calloc(layout->material_count ? layout->material_count : 1,
               sizeof(*layout->allowed_materials));
    if (!layout->base_panes || !layout->panes || !layout->pose_text_overrides ||
        !layout->pose_text_capacities || !layout->allowed_panes ||
        !layout->allowed_materials)
        return false;
    int root_index;
    if (!parse_pane(layout, json, root, &root_index) || root_index != 0 ||
        layout->next_pane != count)
        return false;
    memcpy(layout->panes, layout->base_panes, count * sizeof(*layout->panes));
    return true;
}

static bool parse_groups(WmLayout *layout, const WmJson *json) {
    size_t object = wm_json_member(json, 0, "groups");
    if (object == WM_JSON_INVALID)
        return true;
    if (json->tokens[object].type != WM_JSON_OBJECT ||
        json->tokens[object].children > 1024)
        return false;
    layout->group_count = json->tokens[object].children;
    layout->groups =
        calloc(layout->group_count ? layout->group_count : 1, sizeof(*layout->groups));
    if (!layout->groups)
        return false;
    size_t cursor = object + 1;
    for (size_t index = 0; index < layout->group_count; index++) {
        LayoutGroup *group = &layout->groups[index];
        size_t members = json->tokens[cursor].next;
        if (!wm_json_copy(json, cursor, group->name, sizeof(group->name)) ||
            json->tokens[members].type != WM_JSON_ARRAY ||
            json->tokens[members].children > layout->pane_count)
            return false;
        group->member_count = json->tokens[members].children;
        group->members = malloc((group->member_count ? group->member_count : 1) *
                                sizeof(*group->members));
        if (!group->members)
            return false;
        size_t member = members + 1;
        for (size_t place = 0; place < group->member_count; place++) {
            char name[64];
            if (!wm_json_copy(json, member, name, sizeof(name)))
                return false;
            group->members[place] = wm_layout_find_pane(layout, name);
            member = json->tokens[member].next;
        }
        cursor = json->tokens[members].next;
    }
    return true;
}

static bool append_track(WmLayout *layout, const LayoutTrack *track) {
    if (layout->track_count >= WM_LAYOUT_MAX_TRACKS)
        return false;
    if (layout->track_count == layout->track_capacity) {
        size_t capacity = layout->track_capacity ? layout->track_capacity * 2 : 256;
        if (capacity > WM_LAYOUT_MAX_TRACKS)
            capacity = WM_LAYOUT_MAX_TRACKS;
        LayoutTrack *tracks = realloc(layout->tracks, capacity * sizeof(*tracks));
        if (!tracks)
            return false;
        layout->tracks = tracks;
        layout->track_capacity = capacity;
    }
    layout->tracks[layout->track_count++] = *track;
    return true;
}

static bool append_key(WmLayout *layout, const LayoutKey *key) {
    if (layout->key_count >= WM_LAYOUT_MAX_KEYS)
        return false;
    if (layout->key_count == layout->key_capacity) {
        size_t capacity = layout->key_capacity ? layout->key_capacity * 2 : 512;
        if (capacity > WM_LAYOUT_MAX_KEYS)
            capacity = WM_LAYOUT_MAX_KEYS;
        LayoutKey *keys = realloc(layout->keys, capacity * sizeof(*keys));
        if (!keys)
            return false;
        layout->keys = keys;
        layout->key_capacity = capacity;
    }
    layout->keys[layout->key_count++] = *key;
    return true;
}

static LayoutTrackKind track_kind(const char *name) {
    if (strcmp(name, "RLPA") == 0)
        return LAYOUT_RLPA;
    if (strcmp(name, "RLVI") == 0)
        return LAYOUT_RLVI;
    if (strcmp(name, "RLVC") == 0)
        return LAYOUT_RLVC;
    if (strcmp(name, "RLMC") == 0)
        return LAYOUT_RLMC;
    if (strcmp(name, "RLTS") == 0)
        return LAYOUT_RLTS;
    if (strcmp(name, "RLTP") == 0)
        return LAYOUT_RLTP;
    return LAYOUT_UNKNOWN;
}

static bool parse_animation_track(WmLayout *layout, const WmJson *json, size_t token,
                                  int target_type, int target_index) {
    LayoutTrack track = {.target_type = target_type,
                         .target_index = target_index,
                         .first_key = layout->key_count};
    char kind[8];
    if (!wm_json_copy(json, wm_json_member(json, token, "kind"), kind, sizeof(kind)) ||
        !json_integer_member(json, token, "id", &track.id) ||
        !json_integer_member(json, token, "target", &track.property) ||
        !json_integer_member(json, token, "curveType", &track.curve_type))
        return false;
    track.kind = track_kind(kind);
    size_t keys = wm_json_member(json, token, "keys");
    if (keys >= json->count || json->tokens[keys].type != WM_JSON_ARRAY)
        return false;
    size_t cursor = keys + 1;
    while (cursor < json->tokens[keys].next) {
        LayoutKey key = {0};
        if (!json_float(json, wm_json_member(json, cursor, "frame"), &key.frame) ||
            !json_float(json, wm_json_member(json, cursor, "value"), &key.value)) {
            return false;
        }
        size_t slope = wm_json_member(json, cursor, "slope");
        if (slope != WM_JSON_INVALID && !json_float(json, slope, &key.slope))
            return false;
        if (!append_key(layout, &key))
            return false;
        track.key_count++;
        cursor = json->tokens[cursor].next;
    }
    return append_track(layout, &track);
}

static bool parse_animation(WmLayout *layout, const WmJson *json, size_t token,
                            LayoutAnimation *animation) {
    int frames;
    if (!json_integer_member(json, token, "frames", &frames) || frames < 0 ||
        !json_bool(json, wm_json_member(json, token, "loop"), &animation->loop)) {
        return false;
    }
    animation->frames = (float)frames;
    size_t textures = wm_json_member(json, token, "textures");
    if (textures < json->count && json->tokens[textures].type == WM_JSON_ARRAY) {
        animation->texture_count = json->tokens[textures].children;
        if (animation->texture_count > WM_LAYOUT_MAX_TEXTURES)
            return false;
        animation->textures =
            calloc(animation->texture_count ? animation->texture_count : 1,
                   sizeof(*animation->textures));
        if (!animation->textures)
            return false;
        size_t cursor = textures + 1;
        for (size_t index = 0; index < animation->texture_count; index++) {
            if (!wm_json_copy(json, cursor, animation->textures[index], 128))
                return false;
            cursor = json->tokens[cursor].next;
        }
    }
    size_t targets = wm_json_member(json, token, "targets");
    if (targets >= json->count || json->tokens[targets].type != WM_JSON_ARRAY)
        return false;
    animation->first_track = layout->track_count;
    size_t cursor = targets + 1;
    while (cursor < json->tokens[targets].next) {
        char name[128];
        int type;
        if (!wm_json_copy(json, wm_json_member(json, cursor, "name"), name,
                          sizeof(name)) ||
            !json_integer_member(json, cursor, "type", &type))
            return false;
        int target_index =
            type == 1 ? find_material(layout, name) : wm_layout_find_pane(layout, name);
        size_t tracks = wm_json_member(json, cursor, "tracks");
        if (tracks >= json->count || json->tokens[tracks].type != WM_JSON_ARRAY)
            return false;
        size_t item = tracks + 1;
        while (item < json->tokens[tracks].next) {
            if (!parse_animation_track(layout, json, item, type, target_index))
                return false;
            item = json->tokens[item].next;
        }
        cursor = json->tokens[cursor].next;
    }
    animation->track_count = layout->track_count - animation->first_track;
    return true;
}

static bool parse_animations(WmLayout *layout, const WmJson *json) {
    size_t object = wm_json_member(json, 0, "animations");
    if (object == WM_JSON_INVALID)
        return true;
    if (json->tokens[object].type != WM_JSON_OBJECT ||
        json->tokens[object].children > WM_LAYOUT_MAX_ANIMATIONS)
        return false;
    layout->animation_count = json->tokens[object].children;
    layout->animations = calloc(layout->animation_count ? layout->animation_count : 1,
                                sizeof(*layout->animations));
    if (!layout->animations)
        return false;
    size_t cursor = object + 1;
    for (size_t index = 0; index < layout->animation_count; index++) {
        LayoutAnimation *animation = &layout->animations[index];
        size_t value = json->tokens[cursor].next;
        if (!wm_json_copy(json, cursor, animation->name, sizeof(animation->name)) ||
            !parse_animation(layout, json, value, animation))
            return false;
        cursor = json->tokens[value].next;
    }
    return true;
}

WmLayout *wm_layout_load_json(const char *path, char *error, size_t error_capacity) {
    wm_error_set(error, error_capacity, "");
    if (!path || !*path) {
        wm_error_set(error, error_capacity, "Missing layout path");
        return NULL;
    }
    WmJson json;
    if (!wm_json_load(&json, path, WM_LAYOUT_JSON_LIMIT)) {
        wm_error_set(error, error_capacity, "Unable to read layout JSON");
        return NULL;
    }
    WmLayout *layout = calloc(1, sizeof(*layout));
    if (!layout) {
        wm_json_free(&json);
        wm_error_set(error, error_capacity, "Out of memory");
        return NULL;
    }
    bool success = parse_textures(layout, &json) && parse_fonts(layout, &json) &&
                   parse_materials(layout, &json) && parse_panes(layout, &json) &&
                   parse_groups(layout, &json) && parse_animations(layout, &json);
    wm_json_free(&json);
    if (!success) {
        wm_error_set(error, error_capacity, "Invalid or unsupported layout JSON");
        wm_layout_destroy(layout);
        return NULL;
    }
    return layout;
}
