#ifndef WII_MENU_LAYOUT_RUNTIME_H
#define WII_MENU_LAYOUT_RUNTIME_H

#include "wii_menu/resource_font.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* A prepared BRLYT/BRLAN JSON layout. No original image bytes are owned here. */
typedef struct WmLayout WmLayout;

typedef struct WmLayoutTexture {
    char name[128];
    char url[512];
    int width;
    int height;
    bool missing;
} WmLayoutTexture;

typedef struct WmLayoutMaterialInfo {
    const char *name;
    float registers[3][4];
    float konst_colors[4][4];
    float material_color[4];
    uint8_t channel_control[2];
    uint8_t blend_mode[4];
    uint8_t alpha_compare[4];
    uint8_t tev_swap_table[4];
    const uint8_t (*tev_stages)[16];
    unsigned tev_stage_count;
    unsigned texture_map_count;
    bool has_blend_mode;
    bool has_alpha_compare;
} WmLayoutMaterialInfo;

typedef struct WmLayoutVertex {
    float position[3];
    float color[4];
    float uv[4][2];
} WmLayoutVertex;

typedef struct WmLayoutTextureBinding {
    const WmLayoutTexture *resource;
    uint32_t handle;
    uint8_t wrap_s;
    uint8_t wrap_t;
} WmLayoutTextureBinding;

typedef struct WmLayoutQuad {
    const char *pane_name;
    const WmLayoutMaterialInfo *material;
    WmLayoutVertex vertices[4]; /* LT, RT, LB, RB; triangles 0,1,3 and 0,3,2. */
    WmLayoutTextureBinding textures[4];
} WmLayoutQuad;

enum { WM_LAYOUT_TEXT_COLOR_RANGES = 4 };

typedef struct WmLayoutTextColorRange {
    size_t first_byte;
    size_t end_byte;
    uint8_t rgba[4];
} WmLayoutTextColorRange;

typedef struct WmLayoutTextInfo {
    const char *value; /* UTF-8; empty string when the source has no text. */
    const char *font_name; /* NULL when the layout font index is unresolved. */
    int font_index;
    int material_index;
    const WmLayoutMaterialInfo *material; /* NULL when unresolved. */
    WmFontPane pane; /* Size, origin, 3 x 3 alignment, spacing, and colors. */
    unsigned horizontal_align; /* 0 left, 1 center, 2 right. */
    unsigned vertical_align; /* 0 top, 1 center, 2 bottom. */
    float colors[2][4]; /* Unrounded source colors after animation. */
    WmLayoutTextColorRange color_ranges[WM_LAYOUT_TEXT_COLOR_RANGES];
    size_t color_range_count;
} WmLayoutTextInfo;

typedef struct WmLayoutPaneView {
    const char *name;
    const char *type;
    float matrix[12]; /* Row-major 3 x 4, y up, before framebuffer projection. */
    float corners[4][3];
    float alpha;
    unsigned flags; /* Includes animated visibility; hidden panes are omitted. */
    const WmLayoutTextInfo *text; /* Non-NULL for txt1, valid during callback. */
} WmLayoutPaneView;

typedef enum WmLayoutMode {
    WM_LAYOUT_IPL,
    WM_LAYOUT_EMBEDDED,
    WM_LAYOUT_LOCAL
} WmLayoutMode;

typedef struct WmLayoutClip {
    const char *animation;
    float frame;
    const char *group; /* NULL applies to every matching target. */
    bool recursive_group;
    int loop_override; /* -1: resource bit, 0: hold endpoint, 1: repeat. */
    const char *target_name; /* NULL applies to all targets in the group. */
    /* With target_name, copy that pane's animation tracks and its material
     * tracks onto this pane and its material. Mirrors NW4R pane binding. */
    const char *rebind_name;
} WmLayoutClip;

typedef struct WmLayoutAnimationInfo {
    const char *name; /* Source spelling; owned by the layout. */
    float frames;
    bool loop;
} WmLayoutAnimationInfo;

typedef struct WmLayoutPaneState {
    const char *name;
    const char *type;
    const char *text; /* NULL outside txt1. */
    const char *font_name; /* NULL when no valid font is referenced. */
    unsigned flags;
    float translation[3];
    float scale[2];
    float size[2];
    float font_size[2];
    float char_space;
} WmLayoutPaneState;

typedef bool (*WmLayoutPaneCallback)(void *context, const WmLayoutPaneView *pane);
typedef void (*WmLayoutQuadCallback)(void *context, const WmLayoutQuad *quad);

/* Resolve a local image name/URL to a backend texture. Return false to use
 * handle zero; the caller can map PNG export names to its own image format. */
typedef bool (*WmLayoutImageProvider)(void *context,
                                      const WmLayoutTexture *resource,
                                      uint32_t *handle);

typedef struct WmLayoutDrawOptions {
    bool wide;
    WmLayoutMode mode;
    float alpha; /* Zero is literal zero; use 1 for normal drawing. */
    const float *parent_matrix; /* Optional row-major 3 x 4; NULL is identity. */
    WmLayoutPaneCallback on_pane;
    WmLayoutQuadCallback on_quad;
    WmLayoutImageProvider image_provider;
    void *context;
} WmLayoutDrawOptions;

/* Load the readable JSON produced by the source layout exporter. On failure
 * returns NULL and writes a short diagnostic when error_capacity is nonzero. */
WmLayout *wm_layout_load_json(const char *path, char *error, size_t error_capacity);
void wm_layout_destroy(WmLayout *layout);

/* Each pose starts from the imported layout, then applies clips in order.
 * Group binding is nonrecursive unless the caller explicitly requests it. */
bool wm_layout_pose(WmLayout *layout, const WmLayoutClip *clips, size_t clip_count);

/* Lookups use ASCII case folding for exported animation names. Pane and group
 * names remain case sensitive. Pointers returned in info are layout-owned. */
bool wm_layout_animation_info(const WmLayout *layout, const char *name,
                              WmLayoutAnimationInfo *info);
bool wm_layout_has_group(const WmLayout *layout, const char *name);
bool wm_layout_pane_state(const WmLayout *layout, const char *name,
                          WmLayoutPaneState *state);
/* Return the current source text geometry and font name. font_name may be
 * NULL when the layout references an unresolved font; it is layout-owned. */
bool wm_layout_pane_font(const WmLayout *layout, const char *name,
                         WmFontPane *pane, const char **font_name);

/* These changes affect the current pose. A later wm_layout_pose restores the
 * source properties; call them again after posing each frame as needed. */
bool wm_layout_set_pane_visible(WmLayout *layout, const char *name, bool visible);
/* Set a pane's current pose opacity on the source 0–255 scale. Descendant
 * alpha writes mirror controllers that fade their independently drawn custom
 * layout children; both are discarded by the next wm_layout_pose. */
bool wm_layout_set_pane_alpha(WmLayout *layout, const char *name, float alpha);
bool wm_layout_set_descendant_alpha(WmLayout *layout, const char *name,
                                     float alpha);
bool wm_layout_set_pane_size(WmLayout *layout, const char *name,
                             float width, float height);
/* Absolute source-layout coordinates, applied after the current pose. A later
 * wm_layout_pose restores the authored or animated translation. */
bool wm_layout_set_pane_translation(WmLayout *layout, const char *name,
                                     float x, float y, float z);
/* Move the branch containing a pane to the end of each sibling list. Call
 * after posing; the next pose restores the authored draw order. */
bool wm_layout_raise_pane(WmLayout *layout, const char *name);
bool wm_layout_set_text_style(WmLayout *layout, const char *name,
                              float font_width, float font_height,
                              float char_space);
/* Per-pose text replacement, discarded by the next wm_layout_pose. */
bool wm_layout_set_pose_text(WmLayout *layout, const char *pane_name,
                              const char *utf8);
/* Override a text pane's glyph colors for byte ranges in its posed UTF-8 text.
 * Called after wm_layout_pose; the next pose clears these overrides. */
bool wm_layout_set_pose_text_colors(WmLayout *layout, const char *pane_name,
    const WmLayoutTextColorRange *ranges, size_t count);
/* Hide panes listed in other language groups, preserving their authored flags
 * and giving the selected group precedence for shared members. Call per pose. */
bool wm_layout_mask_language_groups(WmLayout *layout, const char *language);

/* Traverse visible panes in source order and emit picture and window quads.
 * Callback data is valid only for the current callback. Text drawing remains
 * separate porting work; text panes still participate in traversal. */
void wm_layout_draw(const WmLayout *layout, const WmLayoutDrawOptions *options);
/* Visit every pane's posed transform, including panes hidden during drawing.
 * ChannelSelect clock anchors remain valid while their parent is invisible
 * during a page scroll. This traversal emits no graphics. */
void wm_layout_visit_all_transforms(const WmLayout *layout, bool wide,
                                    WmLayoutMode mode,
                                    const float parent_matrix[12],
                                    WmLayoutPaneCallback visitor,
                                    void *context);

size_t wm_layout_pane_count(const WmLayout *layout);
size_t wm_layout_material_count(const WmLayout *layout);
/* Enumerate authored and animation-referenced image resources so a scene can
 * upload them before its first visible animation frame. The pointer is owned
 * by the layout and remains valid until that layout is destroyed. */
size_t wm_layout_texture_count(const WmLayout *layout);
const WmLayoutTexture *wm_layout_texture_at(const WmLayout *layout, size_t index);
/* Read one material for shader preparation without requiring a visible pane.
 * The stage pointer remains owned by the layout. */
bool wm_layout_material_info(const WmLayout *layout, size_t index,
                              WmLayoutMaterialInfo *info,
                              uint8_t wraps[4][2]);
size_t wm_layout_font_count(const WmLayout *layout);
const char *wm_layout_font_name(const WmLayout *layout, size_t index);

/* Replace authored placeholder text with a scene value. The replacement is
 * retained across animation poses. */
bool wm_layout_set_text(WmLayout *layout, const char *pane_name,
                        const char *utf8);

/* Apply the source menu's SetTexture donor substitution before posing. The
 * replacement is retained when later animation poses reset materials. */
bool wm_layout_copy_texture_map(WmLayout *layout, const char *donor_pane,
                                const char *target_pane, unsigned unit);

#endif
