#include "wii_menu/layout_runtime.h"
#include "wii_menu/json.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define WM_LAYOUT_MAX_PANES 4096
#define WM_LAYOUT_MAX_MATERIALS 1024
#define WM_LAYOUT_MAX_TEXTURES 8192
#define WM_LAYOUT_MAX_FONTS 256
#define WM_LAYOUT_MAX_ANIMATIONS 2048
#define WM_LAYOUT_MAX_TRACKS 131072
#define WM_LAYOUT_MAX_KEYS 262144
#define WM_LAYOUT_JSON_LIMIT (32u * 1024u * 1024u)
#define WM_LAYOUT_MAX_TEXT_BYTES 65536u
#define WM_LAYOUT_PI 3.14159265358979323846

typedef struct LayoutSrt {
    float translate[2];
    float rotation;
    float scale[2];
} LayoutSrt;

typedef struct LayoutTextureMap {
    int texture_index;
    char texture_name[128];
    uint8_t wrap_s;
    uint8_t wrap_t;
} LayoutTextureMap;

typedef struct LayoutTexCoordGen {
    int source;
    int matrix;
} LayoutTexCoordGen;

typedef struct LayoutMaterial {
    char name[64];
    float registers[3][4];
    float konst_colors[4][4];
    float material_color[4];
    uint8_t channel_control[2];
    uint8_t blend_mode[4];
    uint8_t alpha_compare[4];
    uint8_t tev_swap_table[4];
    uint8_t tev_stages[32][16];
    bool has_material_color;
    bool has_blend_mode;
    bool has_alpha_compare;
    unsigned tev_stage_count;
    LayoutTextureMap maps[4];
    size_t map_count;
    LayoutSrt srts[16];
    size_t srt_count;
    LayoutTexCoordGen generators[4];
    size_t generator_count;
} LayoutMaterial;

typedef struct LayoutPane {
    char name[64];
    char type[5];
    unsigned flags;
    unsigned origin;
    float alpha;
    float translation[3];
    float rotation[3];
    float scale[2];
    float size[2];
    int first_child;
    int next_sibling;
    int material;
    int frame_materials[8];
    uint8_t frame_flips[8];
    size_t frame_count;
    float inflation[4];
    float vertex_colors[4][4];
    float text_colors[2][4];
    WmLayoutTextColorRange text_color_ranges[WM_LAYOUT_TEXT_COLOR_RANGES];
    size_t text_color_range_count;
    char *text;
    int font_index;
    unsigned text_position;
    float font_size[2];
    float char_space;
    float line_space;
    bool no_wrap;
    bool has_vertex_colors;
    bool has_text_colors;
    float tex_coords[4][4][2];
    size_t tex_coord_count;
} LayoutPane;

typedef struct LayoutGroup {
    char name[64];
    int *members;
    size_t member_count;
} LayoutGroup;

typedef struct LayoutKey {
    float frame;
    float value;
    float slope;
} LayoutKey;

typedef enum LayoutTrackKind {
    LAYOUT_RLPA,
    LAYOUT_RLVI,
    LAYOUT_RLVC,
    LAYOUT_RLMC,
    LAYOUT_RLTS,
    LAYOUT_RLTP,
    LAYOUT_UNKNOWN
} LayoutTrackKind;

typedef struct LayoutTrack {
    LayoutTrackKind kind;
    int target_type;
    int target_index;
    int property;
    int id;
    int curve_type;
    size_t first_key;
    size_t key_count;
} LayoutTrack;

typedef struct LayoutAnimation {
    char name[128];
    float frames;
    bool loop;
    char (*textures)[128];
    size_t texture_count;
    size_t first_track;
    size_t track_count;
} LayoutAnimation;

struct WmLayout {
    WmLayoutTexture *textures;
    size_t texture_count;
    char (*fonts)[128];
    size_t font_count;
    LayoutMaterial *base_materials;
    LayoutMaterial *materials;
    size_t material_count;
    LayoutPane *base_panes;
    LayoutPane *panes;
    char **pose_text_overrides;
    size_t *pose_text_capacities;
    size_t pane_count;
    size_t next_pane;
    LayoutGroup *groups;
    size_t group_count;
    LayoutAnimation *animations;
    size_t animation_count;
    LayoutTrack *tracks;
    size_t track_count;
    size_t track_capacity;
    LayoutKey *keys;
    size_t key_count;
    size_t key_capacity;
    bool *allowed_panes;
    bool *allowed_materials;
};

static void set_error(char *error, size_t capacity, const char *message) {
    if (error && capacity) snprintf(error, capacity, "%s", message);
}

static bool json_float(const WmJson *json, size_t token, float *value) {
    if (token >= json->count || json->tokens[token].type != WM_JSON_NUMBER) return false;
    size_t length = json->tokens[token].end - json->tokens[token].start;
    if (!length || length >= 64) return false;
    char text[64];
    memcpy(text, json->source + json->tokens[token].start, length);
    text[length] = '\0';
    char *end;
    double number = strtod(text, &end);
    if (*end || !isfinite(number) || number > 3.402823466e38 ||
        number < -3.402823466e38) return false;
    *value = (float)number;
    return true;
}

static bool json_bool(const WmJson *json, size_t token, bool *value) {
    if (token >= json->count || json->tokens[token].type != WM_JSON_BOOLEAN) return false;
    *value = json->source[json->tokens[token].start] == 't';
    return true;
}

static bool json_numbers(const WmJson *json, size_t token, float *values,
                         size_t count) {
    if (token >= json->count || json->tokens[token].type != WM_JSON_ARRAY ||
        json->tokens[token].children < count) return false;
    size_t cursor = token + 1;
    for (size_t index = 0; index < count; index++) {
        if (!json_float(json, cursor, &values[index])) return false;
        cursor = json->tokens[cursor].next;
    }
    return true;
}

static bool json_integer_member(const WmJson *json, size_t object,
                                const char *name, int *value) {
    return wm_json_integer(json, wm_json_member(json, object, name), value);
}

static bool json_array_colors(const WmJson *json, size_t array,
                              float colors[][4], size_t count) {
    if (array >= json->count || json->tokens[array].type != WM_JSON_ARRAY ||
        json->tokens[array].children < count) return false;
    size_t cursor = array + 1;
    for (size_t index = 0; index < count; index++) {
        if (!json_numbers(json, cursor, colors[index], 4)) return false;
        cursor = json->tokens[cursor].next;
    }
    return true;
}

static bool json_bytes(const WmJson *json, size_t array, uint8_t *bytes,
                       size_t count) {
    if (array >= json->count || json->tokens[array].type != WM_JSON_ARRAY ||
        json->tokens[array].children < count) return false;
    size_t cursor = array + 1;
    for (size_t index = 0; index < count; index++) {
        int value;
        if (!wm_json_integer(json, cursor, &value) || value < 0 || value > 255) return false;
        bytes[index] = (uint8_t)value;
        cursor = json->tokens[cursor].next;
    }
    return true;
}

static int find_pane(const WmLayout *layout, const char *name) {
    /* JS Map lookup in the source project keeps the last duplicate name. */
    for (size_t index = layout->pane_count; index > 0; index--) {
        if (strcmp(layout->base_panes[index - 1].name, name) == 0) return (int)index - 1;
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

static bool parse_texture(const WmJson *json, size_t token,
                          WmLayoutTexture *texture, const char *fallback_name) {
    memset(texture, 0, sizeof(*texture));
    size_t name = wm_json_member(json, token, "name");
    if (!wm_json_copy(json, name, texture->name, sizeof(texture->name))) {
        if (!fallback_name || strlen(fallback_name) >= sizeof(texture->name)) return false;
        strcpy(texture->name, fallback_name);
    }
    size_t url = wm_json_member(json, token, "url");
    if (url != WM_JSON_INVALID &&
        !wm_json_copy(json, url, texture->url, sizeof(texture->url))) return false;
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
    size_t static_count = static_array < json->count &&
                          json->tokens[static_array].type == WM_JSON_ARRAY
                              ? json->tokens[static_array].children : 0;
    size_t resource_count = resources < json->count &&
                            json->tokens[resources].type == WM_JSON_OBJECT
                                ? json->tokens[resources].children : 0;
    if (static_count + resource_count > WM_LAYOUT_MAX_TEXTURES) return false;
    layout->texture_count = static_count + resource_count;
    layout->textures = calloc(layout->texture_count ? layout->texture_count : 1,
                              sizeof(*layout->textures));
    if (!layout->textures) return false;
    size_t index = 0;
    if (static_count) {
        size_t cursor = static_array + 1;
        while (cursor < json->tokens[static_array].next) {
            if (!parse_texture(json, cursor, &layout->textures[index++], NULL)) return false;
            cursor = json->tokens[cursor].next;
        }
    }
    if (resource_count) {
        size_t cursor = resources + 1;
        while (cursor < json->tokens[resources].next) {
            char name[128];
            size_t value = json->tokens[cursor].next;
            if (!wm_json_copy(json, cursor, name, sizeof(name)) ||
                !parse_texture(json, value, &layout->textures[index++], name)) return false;
            cursor = json->tokens[value].next;
        }
    }
    return true;
}

static bool parse_fonts(WmLayout *layout, const WmJson *json) {
    size_t array = wm_json_member(json, 0, "fonts");
    if (array == WM_JSON_INVALID) return true;
    if (json->tokens[array].type != WM_JSON_ARRAY ||
        json->tokens[array].children > WM_LAYOUT_MAX_FONTS) return false;
    layout->font_count = json->tokens[array].children;
    layout->fonts = calloc(layout->font_count ? layout->font_count : 1,
                           sizeof(*layout->fonts));
    if (!layout->fonts) return false;
    size_t cursor = array + 1;
    for (size_t index = 0; index < layout->font_count; index++) {
        if (!wm_json_copy(json, cursor, layout->fonts[index],
                          sizeof(layout->fonts[index]))) return false;
        cursor = json->tokens[cursor].next;
    }
    return true;
}

static bool parse_material(const WmJson *json, size_t token,
                           LayoutMaterial *material) {
    memset(material, 0, sizeof(*material));
    for (size_t index = 0; index < 4; index++) material->material_color[index] = 255;
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
    if (!wm_json_copy(json, wm_json_member(json, token, "name"),
                      material->name, sizeof(material->name)) ||
        !json_array_colors(json, wm_json_member(json, token, "colors"),
                           material->registers, 3)) return false;
    size_t konst = wm_json_member(json, token, "konstColors");
    if (konst != WM_JSON_INVALID &&
        !json_array_colors(json, konst, material->konst_colors, 4)) return false;
    size_t color = wm_json_member(json, token, "materialColor");
    if (color != WM_JSON_INVALID &&
        !json_numbers(json, color, material->material_color, 4)) return false;
    material->has_material_color = color != WM_JSON_INVALID;
    size_t control = wm_json_member(json, token, "channelControl");
    if (control != WM_JSON_INVALID &&
        !json_bytes(json, control, material->channel_control, 2)) return false;
    size_t blend = wm_json_member(json, token, "blendMode");
    if (blend != WM_JSON_INVALID) {
        if (!json_bytes(json, blend, material->blend_mode, 4)) return false;
        material->has_blend_mode = true;
    }
    size_t compare = wm_json_member(json, token, "alphaCompare");
    if (compare != WM_JSON_INVALID) {
        if (!json_bytes(json, compare, material->alpha_compare, 4)) return false;
        material->has_alpha_compare = true;
    }
    size_t stages = wm_json_member(json, token, "tevStages");
    if (stages < json->count && json->tokens[stages].type == WM_JSON_ARRAY) {
        material->tev_stage_count = (unsigned)json->tokens[stages].children;
        if (material->tev_stage_count > 32) return false;
        size_t stage = stages + 1;
        for (size_t index = 0; index < material->tev_stage_count; index++) {
            if (!json_bytes(json, stage, material->tev_stages[index], 16)) return false;
            stage = json->tokens[stage].next;
        }
    }
    size_t swap = wm_json_member(json, token, "tevSwapTable");
    if (swap != WM_JSON_INVALID &&
        !json_bytes(json, swap, material->tev_swap_table, 4)) return false;
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
                                  sizeof(map->texture_name))) return false;
                if (json_integer_member(json, cursor, "wrapS", &wrap) && wrap >= 0 && wrap <= 2) {
                    map->wrap_s = (uint8_t)wrap;
                }
                if (json_integer_member(json, cursor, "wrapT", &wrap) && wrap >= 0 && wrap <= 2) {
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
                                  srt->scale, 2)) return false;
            }
            cursor = json->tokens[cursor].next;
        }
    }
    size_t generators = wm_json_member(json, token, "texCoordGens");
    if (generators < json->count && json->tokens[generators].type == WM_JSON_ARRAY) {
        size_t cursor = generators + 1;
        while (cursor < json->tokens[generators].next) {
            if (material->generator_count < 4) {
                LayoutTexCoordGen *generator = &material->generators[material->generator_count++];
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
        json->tokens[array].children > WM_LAYOUT_MAX_MATERIALS) return false;
    layout->material_count = json->tokens[array].children;
    layout->base_materials = calloc(layout->material_count ? layout->material_count : 1,
                                    sizeof(*layout->base_materials));
    layout->materials = calloc(layout->material_count ? layout->material_count : 1,
                               sizeof(*layout->materials));
    if (!layout->base_materials || !layout->materials) return false;
    size_t cursor = array + 1;
    for (size_t index = 0; index < layout->material_count; index++) {
        if (!parse_material(json, cursor, &layout->base_materials[index])) return false;
        cursor = json->tokens[cursor].next;
    }
    memcpy(layout->materials, layout->base_materials,
           layout->material_count * sizeof(*layout->materials));
    return true;
}

static bool count_panes(const WmJson *json, size_t token, size_t *count) {
    if (token >= json->count || json->tokens[token].type != WM_JSON_OBJECT ||
        ++*count > WM_LAYOUT_MAX_PANES) return false;
    size_t children = wm_json_member(json, token, "children");
    if (children == WM_JSON_INVALID) return true;
    if (json->tokens[children].type != WM_JSON_ARRAY) return false;
    size_t cursor = children + 1;
    while (cursor < json->tokens[children].next) {
        if (!count_panes(json, cursor, count)) return false;
        cursor = json->tokens[cursor].next;
    }
    return true;
}

static bool parse_pane(WmLayout *layout, const WmJson *json, size_t token,
                       int *result) {
    if (layout->next_pane >= layout->pane_count) return false;
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
    if (!wm_json_copy(json, wm_json_member(json, token, "name"),
                      pane->name, sizeof(pane->name)) ||
        !wm_json_copy(json, wm_json_member(json, token, "type"),
                      pane->type, sizeof(pane->type)) ||
        !json_numbers(json, wm_json_member(json, token, "translation"),
                      pane->translation, 3) ||
        !json_numbers(json, wm_json_member(json, token, "rotation"),
                      pane->rotation, 3) ||
        !json_numbers(json, wm_json_member(json, token, "scale"),
                      pane->scale, 2) ||
        !json_numbers(json, wm_json_member(json, token, "size"),
                      pane->size, 2)) return false;
    int number;
    if (!json_integer_member(json, token, "flags", &number) || number < 0 || number > 255) {
        return false;
    }
    pane->flags = (unsigned)number;
    if (!json_integer_member(json, token, "origin", &number) || number < 0 || number > 8) {
        return false;
    }
    pane->origin = (unsigned)number;
    if (!json_float(json, wm_json_member(json, token, "alpha"), &pane->alpha)) return false;
    if (json_integer_member(json, token, "material", &number)) pane->material = number;
    size_t inflation = wm_json_member(json, token, "inflation");
    if (inflation != WM_JSON_INVALID &&
        !json_numbers(json, inflation, pane->inflation, 4)) return false;

    size_t colors = wm_json_member(json, token, "vertexColors");
    if (colors != WM_JSON_INVALID) {
        if (!json_array_colors(json, colors, pane->vertex_colors, 4)) return false;
        pane->has_vertex_colors = true;
    }
    colors = wm_json_member(json, token, "textColors");
    if (colors != WM_JSON_INVALID) {
        if (!json_array_colors(json, colors, pane->text_colors, 2)) return false;
        pane->has_text_colors = true;
    }
    if (strcmp(pane->type, "txt1") == 0) {
        if (json_integer_member(json, token, "font", &number)) {
            if (number < 0 || number > 65535) return false;
            pane->font_index = number;
        }
        if (json_integer_member(json, token, "textPosition", &number)) {
            if (number < 0 || number > 8) return false;
            pane->text_position = (unsigned)number;
        }
        size_t field = wm_json_member(json, token, "fontSize");
        if (field != WM_JSON_INVALID &&
            !json_numbers(json, field, pane->font_size, 2)) return false;
        field = wm_json_member(json, token, "charSpace");
        if (field != WM_JSON_INVALID &&
            !json_float(json, field, &pane->char_space)) return false;
        field = wm_json_member(json, token, "lineSpace");
        if (field != WM_JSON_INVALID &&
            !json_float(json, field, &pane->line_space)) return false;
        field = wm_json_member(json, token, "noWrap");
        if (field != WM_JSON_INVALID &&
            !json_bool(json, field, &pane->no_wrap)) return false;
        field = wm_json_member(json, token, "text");
        size_t raw_length = field == WM_JSON_INVALID ? 0
                              : json->tokens[field].end - json->tokens[field].start;
        if (raw_length > WM_LAYOUT_MAX_TEXT_BYTES * 6) return false;
        pane->text = malloc(raw_length + 1);
        if (!pane->text) return false;
        if (field == WM_JSON_INVALID) pane->text[0] = '\0';
        else if (!wm_json_copy(json, field, pane->text, raw_length + 1) ||
                 strlen(pane->text) > WM_LAYOUT_MAX_TEXT_BYTES) return false;
    }
    size_t tex_coords = wm_json_member(json, token, "texCoords");
    if (tex_coords < json->count && json->tokens[tex_coords].type == WM_JSON_ARRAY) {
        size_t set = tex_coords + 1;
        while (set < json->tokens[tex_coords].next) {
            if (pane->tex_coord_count < 4) {
                if (json->tokens[set].type != WM_JSON_ARRAY ||
                    json->tokens[set].children < 4) return false;
                size_t point = set + 1;
                for (size_t vertex = 0; vertex < 4; vertex++) {
                    if (!json_numbers(json, point,
                                      pane->tex_coords[pane->tex_coord_count][vertex], 2)) {
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
        if (json->tokens[frames].children > 8) return false;
        size_t cursor = frames + 1;
        while (cursor < json->tokens[frames].next) {
            if (!json_integer_member(json, cursor, "material", &number)) return false;
            pane->frame_materials[pane->frame_count] = number;
            if (!json_integer_member(json, cursor, "flip", &number) ||
                number < 0 || number > 5) return false;
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
            if (!parse_pane(layout, json, cursor, &child)) return false;
            if (previous < 0) pane->first_child = child;
            else layout->base_panes[previous].next_sibling = child;
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
    if (!count_panes(json, root, &count)) return false;
    layout->pane_count = count;
    layout->base_panes = calloc(count, sizeof(*layout->base_panes));
    layout->panes = calloc(count, sizeof(*layout->panes));
    layout->pose_text_overrides = calloc(count, sizeof(*layout->pose_text_overrides));
    layout->pose_text_capacities = calloc(count, sizeof(*layout->pose_text_capacities));
    layout->allowed_panes = calloc(count, sizeof(*layout->allowed_panes));
    layout->allowed_materials = calloc(layout->material_count ? layout->material_count : 1,
                                        sizeof(*layout->allowed_materials));
    if (!layout->base_panes || !layout->panes || !layout->pose_text_overrides ||
        !layout->pose_text_capacities ||
        !layout->allowed_panes ||
        !layout->allowed_materials) return false;
    int root_index;
    if (!parse_pane(layout, json, root, &root_index) || root_index != 0 ||
        layout->next_pane != count) return false;
    memcpy(layout->panes, layout->base_panes, count * sizeof(*layout->panes));
    return true;
}

static bool parse_groups(WmLayout *layout, const WmJson *json) {
    size_t object = wm_json_member(json, 0, "groups");
    if (object == WM_JSON_INVALID) return true;
    if (json->tokens[object].type != WM_JSON_OBJECT ||
        json->tokens[object].children > 1024) return false;
    layout->group_count = json->tokens[object].children;
    layout->groups = calloc(layout->group_count ? layout->group_count : 1,
                            sizeof(*layout->groups));
    if (!layout->groups) return false;
    size_t cursor = object + 1;
    for (size_t index = 0; index < layout->group_count; index++) {
        LayoutGroup *group = &layout->groups[index];
        size_t members = json->tokens[cursor].next;
        if (!wm_json_copy(json, cursor, group->name, sizeof(group->name)) ||
            json->tokens[members].type != WM_JSON_ARRAY ||
            json->tokens[members].children > layout->pane_count) return false;
        group->member_count = json->tokens[members].children;
        group->members = malloc((group->member_count ? group->member_count : 1) *
                                sizeof(*group->members));
        if (!group->members) return false;
        size_t member = members + 1;
        for (size_t place = 0; place < group->member_count; place++) {
            char name[64];
            if (!wm_json_copy(json, member, name, sizeof(name))) return false;
            group->members[place] = find_pane(layout, name);
            member = json->tokens[member].next;
        }
        cursor = json->tokens[members].next;
    }
    return true;
}

static bool append_track(WmLayout *layout, const LayoutTrack *track) {
    if (layout->track_count >= WM_LAYOUT_MAX_TRACKS) return false;
    if (layout->track_count == layout->track_capacity) {
        size_t capacity = layout->track_capacity ? layout->track_capacity * 2 : 256;
        if (capacity > WM_LAYOUT_MAX_TRACKS) capacity = WM_LAYOUT_MAX_TRACKS;
        LayoutTrack *tracks = realloc(layout->tracks, capacity * sizeof(*tracks));
        if (!tracks) return false;
        layout->tracks = tracks;
        layout->track_capacity = capacity;
    }
    layout->tracks[layout->track_count++] = *track;
    return true;
}

static bool append_key(WmLayout *layout, const LayoutKey *key) {
    if (layout->key_count >= WM_LAYOUT_MAX_KEYS) return false;
    if (layout->key_count == layout->key_capacity) {
        size_t capacity = layout->key_capacity ? layout->key_capacity * 2 : 512;
        if (capacity > WM_LAYOUT_MAX_KEYS) capacity = WM_LAYOUT_MAX_KEYS;
        LayoutKey *keys = realloc(layout->keys, capacity * sizeof(*keys));
        if (!keys) return false;
        layout->keys = keys;
        layout->key_capacity = capacity;
    }
    layout->keys[layout->key_count++] = *key;
    return true;
}

static LayoutTrackKind track_kind(const char *name) {
    if (strcmp(name, "RLPA") == 0) return LAYOUT_RLPA;
    if (strcmp(name, "RLVI") == 0) return LAYOUT_RLVI;
    if (strcmp(name, "RLVC") == 0) return LAYOUT_RLVC;
    if (strcmp(name, "RLMC") == 0) return LAYOUT_RLMC;
    if (strcmp(name, "RLTS") == 0) return LAYOUT_RLTS;
    if (strcmp(name, "RLTP") == 0) return LAYOUT_RLTP;
    return LAYOUT_UNKNOWN;
}

static bool parse_animation_track(WmLayout *layout, const WmJson *json,
                                  size_t token, int target_type, int target_index) {
    LayoutTrack track = {
        .target_type = target_type,
        .target_index = target_index,
        .first_key = layout->key_count
    };
    char kind[8];
    if (!wm_json_copy(json, wm_json_member(json, token, "kind"), kind, sizeof(kind)) ||
        !json_integer_member(json, token, "id", &track.id) ||
        !json_integer_member(json, token, "target", &track.property) ||
        !json_integer_member(json, token, "curveType", &track.curve_type)) return false;
    track.kind = track_kind(kind);
    size_t keys = wm_json_member(json, token, "keys");
    if (keys >= json->count || json->tokens[keys].type != WM_JSON_ARRAY) return false;
    size_t cursor = keys + 1;
    while (cursor < json->tokens[keys].next) {
        LayoutKey key = {0};
        if (!json_float(json, wm_json_member(json, cursor, "frame"), &key.frame) ||
            !json_float(json, wm_json_member(json, cursor, "value"), &key.value)) {
            return false;
        }
        size_t slope = wm_json_member(json, cursor, "slope");
        if (slope != WM_JSON_INVALID && !json_float(json, slope, &key.slope)) return false;
        if (!append_key(layout, &key)) return false;
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
        if (animation->texture_count > WM_LAYOUT_MAX_TEXTURES) return false;
        animation->textures = calloc(animation->texture_count ? animation->texture_count : 1,
                                      sizeof(*animation->textures));
        if (!animation->textures) return false;
        size_t cursor = textures + 1;
        for (size_t index = 0; index < animation->texture_count; index++) {
            if (!wm_json_copy(json, cursor, animation->textures[index], 128)) return false;
            cursor = json->tokens[cursor].next;
        }
    }
    size_t targets = wm_json_member(json, token, "targets");
    if (targets >= json->count || json->tokens[targets].type != WM_JSON_ARRAY) return false;
    animation->first_track = layout->track_count;
    size_t cursor = targets + 1;
    while (cursor < json->tokens[targets].next) {
        char name[128];
        int type;
        if (!wm_json_copy(json, wm_json_member(json, cursor, "name"),
                          name, sizeof(name)) ||
            !json_integer_member(json, cursor, "type", &type)) return false;
        int target_index = type == 1 ? find_material(layout, name) : find_pane(layout, name);
        size_t tracks = wm_json_member(json, cursor, "tracks");
        if (tracks >= json->count || json->tokens[tracks].type != WM_JSON_ARRAY) return false;
        size_t item = tracks + 1;
        while (item < json->tokens[tracks].next) {
            if (!parse_animation_track(layout, json, item, type, target_index)) return false;
            item = json->tokens[item].next;
        }
        cursor = json->tokens[cursor].next;
    }
    animation->track_count = layout->track_count - animation->first_track;
    return true;
}

static bool parse_animations(WmLayout *layout, const WmJson *json) {
    size_t object = wm_json_member(json, 0, "animations");
    if (object == WM_JSON_INVALID) return true;
    if (json->tokens[object].type != WM_JSON_OBJECT ||
        json->tokens[object].children > WM_LAYOUT_MAX_ANIMATIONS) return false;
    layout->animation_count = json->tokens[object].children;
    layout->animations = calloc(layout->animation_count ? layout->animation_count : 1,
                                sizeof(*layout->animations));
    if (!layout->animations) return false;
    size_t cursor = object + 1;
    for (size_t index = 0; index < layout->animation_count; index++) {
        LayoutAnimation *animation = &layout->animations[index];
        size_t value = json->tokens[cursor].next;
        if (!wm_json_copy(json, cursor, animation->name, sizeof(animation->name)) ||
            !parse_animation(layout, json, value, animation)) return false;
        cursor = json->tokens[value].next;
    }
    return true;
}

WmLayout *wm_layout_load_json(const char *path, char *error, size_t error_capacity) {
    set_error(error, error_capacity, "");
    if (!path || !*path) {
        set_error(error, error_capacity, "Missing layout path");
        return NULL;
    }
    WmJson json;
    if (!wm_json_load(&json, path, WM_LAYOUT_JSON_LIMIT)) {
        set_error(error, error_capacity, "Unable to read layout JSON");
        return NULL;
    }
    WmLayout *layout = calloc(1, sizeof(*layout));
    if (!layout) {
        wm_json_free(&json);
        set_error(error, error_capacity, "Out of memory");
        return NULL;
    }
    bool success = parse_textures(layout, &json) &&
                   parse_fonts(layout, &json) &&
                   parse_materials(layout, &json) &&
                   parse_panes(layout, &json) &&
                   parse_groups(layout, &json) &&
                   parse_animations(layout, &json);
    wm_json_free(&json);
    if (!success) {
        set_error(error, error_capacity, "Invalid or unsupported layout JSON");
        wm_layout_destroy(layout);
        return NULL;
    }
    return layout;
}

void wm_layout_destroy(WmLayout *layout) {
    if (!layout) return;
    for (size_t index = 0; index < layout->group_count; index++) {
        free(layout->groups[index].members);
    }
    for (size_t index = 0; index < layout->animation_count; index++) {
        free(layout->animations[index].textures);
    }
    free(layout->textures);
    free(layout->fonts);
    free(layout->base_materials);
    free(layout->materials);
    for (size_t index = 0; index < layout->next_pane; index++) {
        free(layout->base_panes[index].text);
    }
    if (layout->pose_text_overrides) {
        for (size_t index = 0; index < layout->pane_count; index++) {
            free(layout->pose_text_overrides[index]);
        }
    }
    free(layout->base_panes);
    free(layout->panes);
    free(layout->pose_text_overrides);
    free(layout->pose_text_capacities);
    free(layout->groups);
    free(layout->animations);
    free(layout->tracks);
    free(layout->keys);
    free(layout->allowed_panes);
    free(layout->allowed_materials);
    free(layout);
}

size_t wm_layout_pane_count(const WmLayout *layout) {
    return layout ? layout->pane_count : 0;
}

static void material_info(const LayoutMaterial *material,
                          WmLayoutMaterialInfo *info);

size_t wm_layout_material_count(const WmLayout *layout) {
    return layout ? layout->material_count : 0;
}

size_t wm_layout_texture_count(const WmLayout *layout) {
    return layout ? layout->texture_count : 0;
}

const WmLayoutTexture *wm_layout_texture_at(const WmLayout *layout, size_t index) {
    return layout && index < layout->texture_count
               ? &layout->textures[index] : NULL;
}

bool wm_layout_material_info(const WmLayout *layout, size_t index,
                              WmLayoutMaterialInfo *info,
                              uint8_t wraps[4][2]) {
    if (!layout || !info || !wraps || index >= layout->material_count) {
        return false;
    }
    const LayoutMaterial *material = &layout->materials[index];
    material_info(material, info);
    memset(wraps, 0, 4 * 2 * sizeof(wraps[0][0]));
    for (size_t unit = 0; unit < material->map_count && unit < 4; unit++) {
        wraps[unit][0] = material->maps[unit].wrap_s;
        wraps[unit][1] = material->maps[unit].wrap_t;
    }
    return true;
}

size_t wm_layout_font_count(const WmLayout *layout) {
    return layout ? layout->font_count : 0;
}

const char *wm_layout_font_name(const WmLayout *layout, size_t index) {
    return layout && index < layout->font_count ? layout->fonts[index] : NULL;
}

bool wm_layout_set_text(WmLayout *layout, const char *pane_name,
                        const char *utf8) {
    if (!layout || !pane_name || !utf8) return false;
    int index = find_pane(layout, pane_name);
    if (index < 0 || strcmp(layout->base_panes[index].type, "txt1") != 0) {
        return false;
    }
    size_t length = 0;
    while (length <= WM_LAYOUT_MAX_TEXT_BYTES && utf8[length]) length++;
    if (length > WM_LAYOUT_MAX_TEXT_BYTES) return false;
    LayoutPane *pane = &layout->base_panes[index];
    if (pane->text && strcmp(pane->text, utf8) == 0) return true;
    bool pose_override_active = layout->pose_text_overrides[index] &&
        layout->panes[index].text == layout->pose_text_overrides[index];
    char *copy = malloc(length + 1);
    if (!copy) return false;
    memcpy(copy, utf8, length + 1);
    free(pane->text);
    pane->text = copy;
    if (!pose_override_active) layout->panes[index].text = copy;
    return true;
}

bool wm_layout_set_pose_text(WmLayout *layout, const char *pane_name,
                              const char *utf8) {
    if (!layout || !pane_name || !utf8) return false;
    int index = find_pane(layout, pane_name);
    if (index < 0 || strcmp(layout->panes[index].type, "txt1") != 0) return false;
    size_t length = 0;
    while (length <= WM_LAYOUT_MAX_TEXT_BYTES && utf8[length]) length++;
    if (length > WM_LAYOUT_MAX_TEXT_BYTES) return false;
    if (utf8 == layout->pose_text_overrides[index]) {
        layout->panes[index].text = layout->pose_text_overrides[index];
        return true;
    }
    if (length + 1 > layout->pose_text_capacities[index]) {
        char *copy = realloc(layout->pose_text_overrides[index], length + 1);
        if (!copy) return false;
        layout->pose_text_overrides[index] = copy;
        layout->pose_text_capacities[index] = length + 1;
    }
    memmove(layout->pose_text_overrides[index], utf8, length + 1);
    layout->panes[index].text = layout->pose_text_overrides[index];
    return true;
}

bool wm_layout_set_pose_text_colors(WmLayout *layout, const char *pane_name,
    const WmLayoutTextColorRange *ranges, size_t count) {
    if (!layout || !pane_name || count > WM_LAYOUT_TEXT_COLOR_RANGES ||
        (count && !ranges)) return false;
    int index = find_pane(layout, pane_name);
    if (index < 0 || strcmp(layout->panes[index].type, "txt1") != 0) return false;
    LayoutPane *pane = &layout->panes[index];
    size_t length = strlen(pane->text ? pane->text : "");
    for (size_t range = 0; range < count; range++) {
        if (ranges[range].first_byte > ranges[range].end_byte ||
            ranges[range].end_byte > length) return false;
    }
    if (count) memcpy(pane->text_color_ranges, ranges,
                      count * sizeof(*ranges));
    pane->text_color_range_count = count;
    return true;
}

bool wm_layout_copy_texture_map(WmLayout *layout, const char *donor_pane,
                                const char *target_pane, unsigned unit) {
    if (!layout || !donor_pane || !target_pane || unit >= 4) return false;
    int donor_index = find_pane(layout, donor_pane);
    int target_index = find_pane(layout, target_pane);
    if (donor_index < 0 || target_index < 0) return false;
    int donor_material = layout->base_panes[donor_index].material;
    int target_material = layout->base_panes[target_index].material;
    if (donor_material < 0 || target_material < 0 ||
        (size_t)donor_material >= layout->material_count ||
        (size_t)target_material >= layout->material_count ||
        layout->base_materials[donor_material].map_count <= unit ||
        layout->base_materials[target_material].map_count <= unit) return false;
    LayoutTextureMap source = layout->base_materials[donor_material].maps[unit];
    layout->base_materials[target_material].maps[unit] = source;
    layout->materials[target_material].maps[unit] = source;
    return true;
}

static const LayoutAnimation *find_animation(const WmLayout *layout, const char *name) {
    for (size_t index = 0; index < layout->animation_count; index++) {
        if (strcmp(layout->animations[index].name, name) == 0) {
            return &layout->animations[index];
        }
    }
    return NULL;
}

static const LayoutGroup *find_group(const WmLayout *layout, const char *name) {
    for (size_t index = 0; index < layout->group_count; index++) {
        if (strcmp(layout->groups[index].name, name) == 0) return &layout->groups[index];
    }
    return NULL;
}

static unsigned ascii_lower(unsigned character) {
    return character >= 'A' && character <= 'Z' ? character + ('a' - 'A')
                                                    : character;
}

static bool ascii_equal_ignore_case(const char *left, const char *right) {
    while (*left && *right) {
        if (ascii_lower((unsigned char)*left) !=
            ascii_lower((unsigned char)*right)) return false;
        left++;
        right++;
    }
    return *left == *right;
}

bool wm_layout_animation_info(const WmLayout *layout, const char *name,
                              WmLayoutAnimationInfo *info) {
    if (!layout || !name || !info) return false;
    for (size_t index = 0; index < layout->animation_count; index++) {
        const LayoutAnimation *animation = &layout->animations[index];
        if (!ascii_equal_ignore_case(animation->name, name)) continue;
        *info = (WmLayoutAnimationInfo) {
            .name = animation->name,
            .frames = animation->frames,
            .loop = animation->loop
        };
        return true;
    }
    return false;
}

bool wm_layout_has_group(const WmLayout *layout, const char *name) {
    return layout && name && find_group(layout, name) != NULL;
}

bool wm_layout_pane_state(const WmLayout *layout, const char *name,
                          WmLayoutPaneState *state) {
    if (!layout || !name || !state) return false;
    int index = find_pane(layout, name);
    if (index < 0) return false;
    const LayoutPane *pane = &layout->panes[index];
    *state = (WmLayoutPaneState) {
        .name = pane->name,
        .type = pane->type,
        .text = pane->text,
        .font_name = pane->font_index >= 0 &&
                     (size_t)pane->font_index < layout->font_count
                         ? layout->fonts[pane->font_index] : NULL,
        .flags = pane->flags,
        .translation = {
            pane->translation[0], pane->translation[1], pane->translation[2]
        },
        .scale = {pane->scale[0], pane->scale[1]},
        .size = {pane->size[0], pane->size[1]},
        .font_size = {pane->font_size[0], pane->font_size[1]},
        .char_space = pane->char_space
    };
    return true;
}

bool wm_layout_set_pane_visible(WmLayout *layout, const char *name, bool visible) {
    if (!layout || !name) return false;
    int index = find_pane(layout, name);
    if (index < 0) return false;
    if (visible) layout->panes[index].flags |= 1u;
    else layout->panes[index].flags &= ~1u;
    return true;
}

bool wm_layout_set_pane_alpha(WmLayout *layout, const char *name, float alpha) {
    if (!layout || !name || !isfinite(alpha) ||
        alpha < 0.0f || alpha > 255.0f) return false;
    int index = find_pane(layout, name);
    if (index < 0) return false;
    layout->panes[index].alpha = alpha;
    return true;
}

static void set_descendant_alpha(WmLayout *layout, int parent, float alpha) {
    for (int child = layout->panes[parent].first_child; child >= 0;
         child = layout->panes[child].next_sibling) {
        layout->panes[child].alpha = alpha;
        set_descendant_alpha(layout, child, alpha);
    }
}

bool wm_layout_set_descendant_alpha(WmLayout *layout, const char *name,
                                     float alpha) {
    if (!layout || !name || !isfinite(alpha) ||
        alpha < 0.0f || alpha > 255.0f) return false;
    int index = find_pane(layout, name);
    if (index < 0) return false;
    set_descendant_alpha(layout, index, alpha);
    return true;
}

bool wm_layout_set_pane_size(WmLayout *layout, const char *name,
                             float width, float height) {
    if (!layout || !name || !isfinite(width) || !isfinite(height) ||
        width < 0 || height < 0) return false;
    int index = find_pane(layout, name);
    if (index < 0) return false;
    layout->panes[index].size[0] = width;
    layout->panes[index].size[1] = height;
    return true;
}

bool wm_layout_set_pane_translation(WmLayout *layout, const char *name,
                                     float x, float y, float z) {
    if (!layout || !name || !isfinite(x) || !isfinite(y) || !isfinite(z)) {
        return false;
    }
    int index = find_pane(layout, name);
    if (index < 0) return false;
    LayoutPane *pane = &layout->panes[index];
    pane->translation[0] = x;
    pane->translation[1] = y;
    pane->translation[2] = z;
    return true;
}

static bool raise_pane_branch(WmLayout *layout, int parent, int target) {
    if (parent == target) return true;
    int previous = -1;
    for (int child = layout->panes[parent].first_child; child >= 0;
         child = layout->panes[child].next_sibling) {
        if (raise_pane_branch(layout, child, target)) {
            int next = layout->panes[child].next_sibling;
            if (next >= 0) {
                if (previous < 0) layout->panes[parent].first_child = next;
                else layout->panes[previous].next_sibling = next;
                int last = next;
                while (layout->panes[last].next_sibling >= 0)
                    last = layout->panes[last].next_sibling;
                layout->panes[last].next_sibling = child;
                layout->panes[child].next_sibling = -1;
            }
            return true;
        }
        previous = child;
    }
    return false;
}

bool wm_layout_raise_pane(WmLayout *layout, const char *name) {
    if (!layout || !name || layout->pane_count == 0) return false;
    int target = find_pane(layout, name);
    return target >= 0 && raise_pane_branch(layout, 0, target);
}

bool wm_layout_set_text_style(WmLayout *layout, const char *name,
                              float font_width, float font_height,
                              float char_space) {
    if (!layout || !name || !isfinite(font_width) || !isfinite(font_height) ||
        !isfinite(char_space) || font_width < 0 || font_height < 0) return false;
    int index = find_pane(layout, name);
    if (index < 0 || strcmp(layout->panes[index].type, "txt1") != 0) return false;
    LayoutPane *pane = &layout->panes[index];
    pane->font_size[0] = font_width;
    pane->font_size[1] = font_height;
    pane->char_space = char_space;
    return true;
}

static bool is_language_group(const char *name) {
    static const char *const languages[] = {
        "JPN", "ENG", "GER", "FRA", "SPA", "ITA", "NED", "CHN", "CHT", "KOR"
    };
    for (size_t index = 0; index < sizeof(languages) / sizeof(languages[0]); index++) {
        if (strcmp(name, languages[index]) == 0) return true;
    }
    return false;
}

bool wm_layout_mask_language_groups(WmLayout *layout, const char *language) {
    if (!layout) return false;
    const char *selected = language && is_language_group(language) ? language : "ENG";
    memset(layout->allowed_panes, 0,
           layout->pane_count * sizeof(*layout->allowed_panes));
    for (size_t group_index = 0; group_index < layout->group_count; group_index++) {
        const LayoutGroup *group = &layout->groups[group_index];
        if (!is_language_group(group->name) ||
            strcmp(group->name, selected) == 0) continue;
        for (size_t member_index = 0; member_index < group->member_count; member_index++) {
            int pane = group->members[member_index];
            if (pane >= 0) layout->allowed_panes[pane] = true;
        }
    }
    const LayoutGroup *selected_group = find_group(layout, selected);
    if (selected_group) {
        for (size_t member_index = 0;
             member_index < selected_group->member_count; member_index++) {
            int pane = selected_group->members[member_index];
            if (pane >= 0) layout->allowed_panes[pane] = false;
        }
    }
    for (size_t index = 0; index < layout->pane_count; index++) {
        if (layout->allowed_panes[index]) layout->panes[index].flags &= ~1u;
    }
    return true;
}

static void allow_pane(WmLayout *layout, int index, bool recursive) {
    if (index < 0 || (size_t)index >= layout->pane_count) return;
    LayoutPane *pane = &layout->panes[index];
    layout->allowed_panes[index] = true;
    if (pane->material >= 0 && (size_t)pane->material < layout->material_count) {
        layout->allowed_materials[pane->material] = true;
    }
    for (size_t frame = 0; frame < pane->frame_count; frame++) {
        int material = pane->frame_materials[frame];
        if (material >= 0 && (size_t)material < layout->material_count) {
            layout->allowed_materials[material] = true;
        }
    }
    if (recursive) {
        for (int child = pane->first_child; child >= 0;
             child = layout->panes[child].next_sibling) {
            allow_pane(layout, child, true);
        }
    }
}

static void bind_group(WmLayout *layout, const char *name, bool recursive) {
    memset(layout->allowed_panes, 0,
           layout->pane_count * sizeof(*layout->allowed_panes));
    memset(layout->allowed_materials, 0,
           layout->material_count * sizeof(*layout->allowed_materials));
    const LayoutGroup *group = find_group(layout, name);
    if (!group) return;
    for (size_t index = 0; index < group->member_count; index++) {
        allow_pane(layout, group->members[index], recursive);
    }
}

static float sample_track(const WmLayout *layout, const LayoutTrack *track, float frame) {
    if (!track->key_count) return NAN;
    const LayoutKey *keys = layout->keys + track->first_key;
    if (frame <= keys[0].frame) return keys[0].value;
    if (frame >= keys[track->key_count - 1].frame) {
        return keys[track->key_count - 1].value;
    }
    size_t low = 1, high = track->key_count - 1;
    while (low < high) {
        size_t middle = low + (high - low) / 2;
        if (keys[middle].frame <= frame) low = middle + 1;
        else high = middle;
    }
    const LayoutKey *a = &keys[low - 1], *b = &keys[low];
    if (track->curve_type == 1) return a->value;
    float span = b->frame - a->frame;
    if (span <= 0) return b->value;
    float t = (frame - a->frame) / span;
    float t2 = t * t, t3 = t2 * t;
    return (2 * t3 - 3 * t2 + 1) * a->value +
           (t3 - 2 * t2 + t) * span * a->slope +
           (-2 * t3 + 3 * t2) * b->value +
           (t3 - t2) * span * b->slope;
}

static void apply_pane_track(LayoutPane *pane, const LayoutTrack *track, float value) {
    int property = track->property;
    switch (track->kind) {
        case LAYOUT_RLPA:
            if (property >= 0 && property < 3) pane->translation[property] = value;
            else if (property < 6 && property >= 3) pane->rotation[property - 3] = value;
            else if (property < 8 && property >= 6) pane->scale[property - 6] = value;
            else if (property < 10 && property >= 8) pane->size[property - 8] = value;
            break;
        case LAYOUT_RLVI:
            if (value != 0) pane->flags |= 1;
            else pane->flags &= ~1u;
            break;
        case LAYOUT_RLVC:
            if (property == 16) pane->alpha = value;
            else if (property >= 0 && property < 16 && pane->has_text_colors) {
                pane->text_colors[property / 8][property % 4] = value;
            } else if (property >= 0 && property < 16 && pane->has_vertex_colors) {
                pane->vertex_colors[property / 4][property % 4] = value;
            }
            break;
        default:
            break;
    }
}

static void apply_material_track(LayoutMaterial *material, const LayoutTrack *track,
                                 const LayoutAnimation *animation, float value) {
    int property = track->property;
    switch (track->kind) {
        case LAYOUT_RLMC:
            if (property >= 0 && property < 4 && material->has_material_color) {
                material->material_color[property] = value;
            }
            else if (property >= 4 && property < 16) {
                material->registers[(property - 4) / 4][property % 4] = value;
            } else if (property >= 16 && property < 32) {
                material->konst_colors[(property - 16) / 4][property % 4] = value;
            }
            break;
        case LAYOUT_RLTS:
            if (track->id < 0 || (size_t)track->id >= material->srt_count) break;
            if (property >= 0 && property < 2) {
                material->srts[track->id].translate[property] = value;
            } else if (property == 2) {
                material->srts[track->id].rotation = value;
            } else if (property >= 3 && property < 5) {
                material->srts[track->id].scale[property - 3] = value;
            }
            break;
        case LAYOUT_RLTP:
            if (property != 0 || track->id < 0 ||
                (size_t)track->id >= material->map_count || value < 0 ||
                floorf(value) != value || (size_t)value >= animation->texture_count) break;
            strcpy(material->maps[track->id].texture_name,
                   animation->textures[(size_t)value]);
            break;
        default:
            break;
    }
}

bool wm_layout_pose(WmLayout *layout, const WmLayoutClip *clips, size_t clip_count) {
    if (!layout || (clip_count && !clips)) return false;
    /* Pane pointers reset below; keep text storage for the next frame. */
    memcpy(layout->panes, layout->base_panes,
           layout->pane_count * sizeof(*layout->panes));
    memcpy(layout->materials, layout->base_materials,
           layout->material_count * sizeof(*layout->materials));
    for (size_t clip_index = 0; clip_index < clip_count; clip_index++) {
        const WmLayoutClip *clip = &clips[clip_index];
        if (!clip->animation || !isfinite(clip->frame) || clip->loop_override < -1 ||
            clip->loop_override > 1) return false;
        const LayoutAnimation *animation = find_animation(layout, clip->animation);
        if (!animation) continue;
        int source_pane = -1, destination_pane = -1;
        int source_material = -1, destination_material = -1;
        if (clip->rebind_name) {
            if (!clip->target_name) return false;
            source_pane = find_pane(layout, clip->target_name);
            destination_pane = find_pane(layout, clip->rebind_name);
            if (source_pane < 0 || destination_pane < 0) return false;
            source_material = layout->base_panes[source_pane].material;
            destination_material = layout->base_panes[destination_pane].material;
        }
        if (clip->group) bind_group(layout, clip->group, clip->recursive_group);
        bool repeating = clip->loop_override < 0 ? animation->loop : clip->loop_override != 0;
        float frame = repeating && animation->frames > 0
                          ? fmodf(clip->frame, animation->frames)
                          : fminf(clip->frame, animation->frames);
        for (size_t offset = 0; offset < animation->track_count; offset++) {
            const LayoutTrack *track = &layout->tracks[animation->first_track + offset];
            if (track->target_index < 0) continue;
            int target_index = track->target_index;
            if (clip->rebind_name) {
                if (track->target_type == 1) {
                    if (source_material < 0 || destination_material < 0 ||
                        target_index != source_material) continue;
                    target_index = destination_material;
                } else {
                    if (target_index != source_pane) continue;
                    target_index = destination_pane;
                }
            } else if (clip->target_name) {
                const char *name = track->target_type == 1
                    ? layout->materials[track->target_index].name
                    : layout->panes[track->target_index].name;
                if (strcmp(name, clip->target_name) != 0) continue;
            }
            float value = sample_track(layout, track, frame);
            if (isnan(value)) continue;
            if (track->target_type == 1) {
                if (clip->group && !layout->allowed_materials[target_index]) continue;
                apply_material_track(&layout->materials[target_index], track,
                                     animation, value);
            } else {
                if (clip->group && !layout->allowed_panes[target_index]) continue;
                apply_pane_track(&layout->panes[target_index], track, value);
            }
        }
    }
    return true;
}

static void matrix_identity(float matrix[12]) {
    const float identity[12] = {
        1, 0, 0, 0,
        0, 1, 0, 0,
        0, 0, 1, 0
    };
    memcpy(matrix, identity, sizeof(identity));
}

static void matrix_multiply(const float a[12], const float b[12], float result[12]) {
    for (int row = 0; row < 3; row++) {
        for (int column = 0; column < 4; column++) {
            result[row * 4 + column] =
                (column == 3 ? a[row * 4 + 3] : 0) +
                a[row * 4] * b[column] +
                a[row * 4 + 1] * b[4 + column] +
                a[row * 4 + 2] * b[8 + column];
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

static void transform_point(const float matrix[12], float x, float y,
                            float result[3]) {
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
    if (!name || !*name) return NULL;
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
    if (map->texture_name[0]) return texture_by_name(layout, map->texture_name);
    if (map->texture_index >= 0 && (size_t)map->texture_index < layout->texture_count) {
        return &layout->textures[map->texture_index];
    }
    return NULL;
}

static void material_info(const LayoutMaterial *material, WmLayoutMaterialInfo *info) {
    memset(info, 0, sizeof(*info));
    info->name = material->name;
    for (size_t color = 0; color < 3; color++) {
        for (size_t channel = 0; channel < 4; channel++) {
            info->registers[color][channel] = material->registers[color][channel] / 255;
        }
    }
    for (size_t color = 0; color < 4; color++) {
        for (size_t channel = 0; channel < 4; channel++) {
            info->konst_colors[color][channel] = material->konst_colors[color][channel] / 255;
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

static void quad_uv(const LayoutPane *pane, const LayoutMaterial *material,
                    size_t unit, float output[4][2]) {
    static const float defaults[4][2] = {
        {0, 0}, {1, 0}, {0, 1}, {1, 1}
    };
    const LayoutTexCoordGen *generator = unit < material->generator_count
                                              ? &material->generators[unit] : NULL;
    int source = generator ? generator->source : 4 + (int)unit;
    int uv_set = source - 4;
    if (uv_set < 0 || (size_t)uv_set >= pane->tex_coord_count) {
        uv_set = pane->tex_coord_count ? 0 : -1;
    }
    int matrix = generator ? generator->matrix : 30 + (int)unit * 3;
    int srt_index = matrix == 60 ? -1 : (int)floorf((float)(matrix - 30) / 3);
    const LayoutSrt *srt = srt_index >= 0 && (size_t)srt_index < material->srt_count
                               ? &material->srts[srt_index] : NULL;
    double radians = srt ? (double)srt->rotation * WM_LAYOUT_PI / 180.0 : 0;
    double cosine = cos(radians), sine = sin(radians);
    for (size_t corner = 0; corner < 4; corner++) {
        float u = uv_set < 0 ? defaults[corner][0] : pane->tex_coords[uv_set][corner][0];
        float v = uv_set < 0 ? defaults[corner][1] : pane->tex_coords[uv_set][corner][1];
        if (srt) {
            float x = (u - 0.5f) * srt->scale[0];
            float y = (v - 0.5f) * srt->scale[1];
            output[corner][0] = (float)(cosine * x - sine * y) + 0.5f +
                                srt->translate[0];
            output[corner][1] = (float)(sine * x + cosine * y) + 0.5f +
                                srt->translate[1];
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
        (size_t)pane->material >= layout->material_count) return;
    const LayoutMaterial *material = &layout->materials[pane->material];
    WmLayoutMaterialInfo info;
    material_info(material, &info);
    WmLayoutQuad quad = {
        .pane_name = pane->name,
        .material = &info
    };
    for (size_t unit = 0; unit < 4; unit++) {
        float uvs[4][2];
        quad_uv(pane, material, unit, uvs);
        for (size_t corner = 0; corner < 4; corner++) {
            quad.vertices[corner].uv[unit][0] = uvs[corner][0];
            quad.vertices[corner].uv[unit][1] = uvs[corner][1];
        }
        if (unit >= material->map_count) continue;
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
                                 ? material->material_color : pane->vertex_colors[corner];
        float opacity = material->channel_control[1] == 0
                            ? material->material_color[3] : pane->vertex_colors[corner][3];
        for (size_t channel = 0; channel < 3; channel++) {
            vertex->color[channel] = color[channel] / 255;
        }
        vertex->color[3] = opacity / 255 * alpha;
    }
    options->on_quad(options->context, &quad);
}

static const WmLayoutTexture *window_frame_texture(const WmLayout *layout,
                                                    const LayoutPane *pane,
                                                    size_t frame) {
    if (frame >= pane->frame_count) return NULL;
    int material_index = pane->frame_materials[frame];
    if (material_index < 0 || (size_t)material_index >= layout->material_count) return NULL;
    const LayoutMaterial *material = &layout->materials[material_index];
    return material->map_count ? texture_for_map(layout, &material->maps[0]) : NULL;
}

static void window_frame_uv(const float size[2], const WmLayoutTexture *texture,
                            unsigned flip, unsigned corner, float result[4][2]) {
    static const uint8_t coordinates[6][4][2] = {
        {{0, 0}, {1, 0}, {0, 1}, {1, 1}},
        {{1, 0}, {0, 0}, {1, 1}, {0, 1}},
        {{0, 1}, {1, 1}, {0, 0}, {1, 0}},
        {{0, 1}, {0, 0}, {1, 1}, {1, 0}},
        {{1, 1}, {0, 1}, {1, 0}, {0, 0}},
        {{1, 0}, {1, 1}, {0, 0}, {0, 1}}
    };
    static const uint8_t indices[6][2] = {
        {0, 1}, {0, 1}, {0, 1}, {1, 0}, {0, 1}, {1, 0}
    };
    const float texture_size[2] = {(float)texture->width, (float)texture->height};
    memset(result, 0, sizeof(float) * 8);
    for (unsigned axis = 0; axis < 2; axis++) {
        unsigned index = indices[flip][axis];
        unsigned bit = 1u << axis;
        float anchor = coordinates[flip][corner][index];
        float direction = (float)coordinates[flip][corner ^ bit][index] - anchor;
        float opposite = anchor + size[axis] / (direction * texture_size[index]);
        for (unsigned vertex = 0; vertex < 4; vertex++) {
            result[vertex][index] = (vertex & bit) == (corner & bit)
                                        ? anchor : opposite;
        }
    }
}

static void emit_window_piece(const WmLayout *layout, const LayoutPane *pane,
                              const float matrix[12], float alpha,
                              float x, float y, float width, float height,
                              int material, const float (*uv)[2], bool white_vertices,
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
                              const float matrix[12], float alpha,
                              unsigned frame, unsigned corner,
                              float x, float y, float width, float height,
                              int forced_flip, const WmLayoutDrawOptions *options) {
    const WmLayoutTexture *texture = window_frame_texture(layout, pane, frame);
    if (!texture || texture->width <= 0 || texture->height <= 0) return;
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
    if (!options->on_quad) return;
    size_t count = pane->frame_count;
    float width = pane->size[0], height = pane->size[1];
    float left = 0, right = 0, top = 0, bottom = 0;
    if (count == 1 || count == 4 || count == 8) {
        const WmLayoutTexture *first = window_frame_texture(layout, pane, 0);
        const WmLayoutTexture *last = window_frame_texture(layout, pane,
                                                            count == 1 ? 0 : 3);
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
    emit_window_piece(layout, pane, matrix, alpha,
                      left - inflation[0], top - inflation[2],
                      width - left - right + inflation[0] + inflation[1],
                      height - top - bottom + inflation[2] + inflation[3],
                      pane->material, NULL, false, options);
    if (count == 1 || count == 4) {
        emit_window_frame(layout, pane, matrix, alpha, 0, 0,
                          0, 0, width - right, top, count == 1 ? 0 : -1, options);
        emit_window_frame(layout, pane, matrix, alpha, count == 1 ? 0 : 1, 1,
                          width - right, 0, right, height - bottom,
                          count == 1 ? 1 : -1, options);
        emit_window_frame(layout, pane, matrix, alpha, count == 1 ? 0 : 3, 3,
                          left, height - bottom, width - left, bottom,
                          count == 1 ? 4 : -1, options);
        emit_window_frame(layout, pane, matrix, alpha, count == 1 ? 0 : 2, 2,
                          0, top, left, height - top,
                          count == 1 ? 2 : -1, options);
    } else if (count == 8) {
        emit_window_frame(layout, pane, matrix, alpha, 0, 0,
                          0, 0, left, top, -1, options);
        emit_window_frame(layout, pane, matrix, alpha, 6, 0,
                          left, 0, width - left - right, top, -1, options);
        emit_window_frame(layout, pane, matrix, alpha, 1, 1,
                          width - right, 0, right, top, -1, options);
        emit_window_frame(layout, pane, matrix, alpha, 5, 1,
                          width - right, top, right, height - top - bottom, -1, options);
        emit_window_frame(layout, pane, matrix, alpha, 3, 3,
                          width - right, height - bottom, right, bottom, -1, options);
        emit_window_frame(layout, pane, matrix, alpha, 7, 3,
                          left, height - bottom, width - left - right, bottom,
                          -1, options);
        emit_window_frame(layout, pane, matrix, alpha, 2, 2,
                          0, height - bottom, left, bottom, -1, options);
        emit_window_frame(layout, pane, matrix, alpha, 4, 2,
                          0, top, left, height - top - bottom, -1, options);
    }
}

static uint8_t text_color_byte(float value) {
    if (!isfinite(value) || value <= 0) return 0;
    if (value >= 255) return 255;
    return (uint8_t)lroundf(value);
}

static void copy_font_pane(const LayoutPane *source, WmFontPane *target) {
    *target = (WmFontPane){
        .size = {source->size[0], source->size[1]},
        .origin = source->origin,
        .text_position = source->text_position,
        .font_size = {source->font_size[0], source->font_size[1]},
        .char_space = source->char_space,
        .line_space = source->line_space,
        .no_wrap = source->no_wrap
    };
    for (size_t channel = 0; channel < 4; channel++) {
        target->top_color[channel] = text_color_byte(source->text_colors[0][channel]);
        target->bottom_color[channel] = text_color_byte(source->text_colors[1][channel]);
    }
}

bool wm_layout_pane_font(const WmLayout *layout, const char *name,
                         WmFontPane *pane, const char **font_name) {
    if (!layout || !name || !pane || !font_name) return false;
    int index = find_pane(layout, name);
    if (index < 0 || strcmp(layout->panes[index].type, "txt1") != 0) {
        return false;
    }
    const LayoutPane *source = &layout->panes[index];
    copy_font_pane(source, pane);
    *font_name = source->font_index >= 0 &&
                 (size_t)source->font_index < layout->font_count
                     ? layout->fonts[source->font_index] : NULL;
    return true;
}

static void visit_pane(const WmLayout *layout, int index,
                       const float parent_matrix[12], float ancestor_alpha,
                       const WmLayoutDrawOptions *options) {
    const LayoutPane *pane = &layout->panes[index];
    if (!(pane->flags & 1)) return;
    float scale_x = pane->scale[0];
    if (options->wide && options->mode != WM_LAYOUT_LOCAL) {
        if (index == 0 && options->mode == WM_LAYOUT_IPL) scale_x *= 832.0f / 608.0f;
        if (pane->flags & 4) scale_x *= 608.0f / 832.0f;
    }
    float local[12], world[12];
    pane_matrix(pane, scale_x, local);
    matrix_multiply(parent_matrix, local, world);
    float pane_alpha = pane->alpha / 255;
    float alpha = options->alpha * pane_alpha * ancestor_alpha;
    WmLayoutPaneView view = {
        .name = pane->name,
        .type = pane->type,
        .alpha = alpha,
        .flags = pane->flags
    };
    WmLayoutTextInfo text = {0};
    WmLayoutMaterialInfo text_material;
    if (strcmp(pane->type, "txt1") == 0) {
        text.value = pane->text ? pane->text : "";
        text.font_index = pane->font_index;
        text.font_name = pane->font_index >= 0 &&
                         (size_t)pane->font_index < layout->font_count
                             ? layout->fonts[pane->font_index] : NULL;
        text.material_index = pane->material;
        if (pane->material >= 0 && (size_t)pane->material < layout->material_count) {
            material_info(&layout->materials[pane->material], &text_material);
            text.material = &text_material;
        }
        copy_font_pane(pane, &text.pane);
        text.horizontal_align = pane->text_position % 3;
        text.vertical_align = pane->text_position / 3;
        for (size_t color = 0; color < 2; color++) {
            for (size_t channel = 0; channel < 4; channel++) {
                float value = pane->text_colors[color][channel];
                text.colors[color][channel] = value;
            }
        }
        text.color_range_count = pane->text_color_range_count;
        if (text.color_range_count) {
            memcpy(text.color_ranges, pane->text_color_ranges,
                   text.color_range_count * sizeof(text.color_ranges[0]));
        }
        view.text = &text;
    }
    memcpy(view.matrix, world, sizeof(world));
    pane_corners(pane, world, view.corners);
    if (options->on_pane && !options->on_pane(options->context, &view)) return;
    if (alpha > 0) {
        if (strcmp(pane->type, "pic1") == 0) {
            emit_picture(layout, pane, view.corners, alpha, options);
        } else if (strcmp(pane->type, "wnd1") == 0 && pane->material >= 0) {
            emit_window(layout, pane, world, alpha, options);
        }
    }
    float child_alpha = ancestor_alpha * ((pane->flags & 2) ? pane_alpha : 1);
    for (int child = pane->first_child; child >= 0;
         child = layout->panes[child].next_sibling) {
        visit_pane(layout, child, world, child_alpha, options);
    }
}

void wm_layout_draw(const WmLayout *layout, const WmLayoutDrawOptions *options) {
    if (!layout || !options || !layout->pane_count) return;
    float identity[12];
    matrix_identity(identity);
    visit_pane(layout, 0,
               options->parent_matrix ? options->parent_matrix : identity,
               1, options);
}

static void visit_all_transforms(const WmLayout *layout, int index,
                                 const float parent_matrix[12], bool wide,
                                 WmLayoutMode mode,
                                 WmLayoutPaneCallback visitor, void *context) {
    const LayoutPane *pane = &layout->panes[index];
    float scale_x = pane->scale[0];
    if (wide && mode != WM_LAYOUT_LOCAL) {
        if (index == 0 && mode == WM_LAYOUT_IPL)
            scale_x *= 832.0f / 608.0f;
        if (pane->flags & 4) scale_x *= 608.0f / 832.0f;
    }
    float local[12], world[12];
    pane_matrix(pane, scale_x, local);
    matrix_multiply(parent_matrix, local, world);
    WmLayoutPaneView view = {
        .name = pane->name, .type = pane->type,
        .alpha = pane->alpha / 255.0f, .flags = pane->flags
    };
    memcpy(view.matrix, world, sizeof(world));
    if (visitor) visitor(context, &view);
    for (int child = pane->first_child; child >= 0;
         child = layout->panes[child].next_sibling)
        visit_all_transforms(layout, child, world, wide, mode,
                             visitor, context);
}

void wm_layout_visit_all_transforms(const WmLayout *layout, bool wide,
                                    WmLayoutMode mode,
                                    const float parent_matrix[12],
                                    WmLayoutPaneCallback visitor,
                                    void *context) {
    if (!layout || !layout->pane_count || !visitor) return;
    float identity[12];
    matrix_identity(identity);
    visit_all_transforms(layout, 0,
                         parent_matrix ? parent_matrix : identity,
                         wide, mode, visitor, context);
}
