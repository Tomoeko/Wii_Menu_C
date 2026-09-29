#ifndef WM_LAYOUT_RUNTIME_INTERNAL_H
#define WM_LAYOUT_RUNTIME_INTERNAL_H

#include "wii_menu/layout/layout_runtime.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

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

#define LAYOUT_PANE_VISIBLE 1u
#define LAYOUT_PANE_CHILD_ALPHA 2u
#define LAYOUT_PANE_WIDE_COMPENSATION 4u

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

/* Shared lookup for loader bindings and runtime pane operations. */
int wm_layout_find_pane(const WmLayout *layout, const char *name);
void wm_layout_material_info_from_data(const LayoutMaterial *material,
                                       WmLayoutMaterialInfo *info);

#endif
