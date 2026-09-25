#include "wii_menu/resource_scene.h"

#include <assert.h>
#include <math.h>
#include <stdio.h>

static uint32_t next_texture = 1;
static uint32_t empty_channel_textures[4];
static uint32_t empty_effect_texture;
static WmClipRect active_clip;
static bool clipped;
static unsigned right_edge_clips;
static unsigned left_edge_clips;
static unsigned right_edge_logo_quads;
static unsigned left_edge_logo_quads;
static unsigned right_edge_effect_quads;
static unsigned left_edge_effect_quads;

static bool quad_intersects_visible_clip(const WmMaterialQuad *quad) {
    float left = quad->vertices[0].x;
    float right = left;
    float top = quad->vertices[0].y;
    float bottom = top;
    float alpha = quad->vertices[0].color.a;
    for (size_t index = 1; index < 4; index++) {
        left = fminf(left, quad->vertices[index].x);
        right = fmaxf(right, quad->vertices[index].x);
        top = fminf(top, quad->vertices[index].y);
        bottom = fmaxf(bottom, quad->vertices[index].y);
        alpha = fmaxf(alpha, quad->vertices[index].color.a);
    }
    left = fmaxf(left, fmaxf(active_clip.x, 0.0f));
    right = fminf(right, fminf(active_clip.x + active_clip.width,
                               (float)WM_FRAME_WIDTH));
    top = fmaxf(top, fmaxf(active_clip.y, 0.0f));
    bottom = fminf(bottom, fminf(active_clip.y + active_clip.height,
                                 (float)WM_FRAME_HEIGHT));
    return left < right && top < bottom && alpha > 0.01f;
}

void wm_platform_begin(WmPlatform *platform, WmColor clear_color) {
    (void)platform;
    (void)clear_color;
}

void wm_platform_end(WmPlatform *platform) {
    (void)platform;
}

void wm_platform_set_clip(WmPlatform *platform, const WmClipRect *clip) {
    (void)platform;
    clipped = clip != NULL;
    if (!clipped) return;
    active_clip = *clip;
    if (clip->x < 0.0f && clip->x + clip->width > 0.0f)
        left_edge_clips++;
    if (clip->x < WM_FRAME_WIDTH &&
        clip->x + clip->width > WM_FRAME_WIDTH)
        right_edge_clips++;
}

void wm_platform_draw_quad(WmPlatform *platform, const WmQuad *quad) {
    (void)platform;
    (void)quad;
}

void wm_platform_draw_vertices(WmPlatform *platform,
                               const WmDrawVertex vertices[4],
                               uint32_t texture) {
    (void)platform;
    (void)vertices;
    (void)texture;
}

void wm_platform_prepare_material(WmPlatform *platform,
                                  const WmMaterialQuad *quad) {
    (void)platform;
    (void)quad;
}

void wm_platform_draw_material_quad(WmPlatform *platform,
                                    const WmMaterialQuad *quad) {
    (void)platform;
    if (!clipped || !quad->texture_count ||
        !quad_intersects_visible_clip(quad)) return;
    if (quad->textures[0] == empty_effect_texture) {
        if (active_clip.x < 0.0f &&
            active_clip.x + active_clip.width > 0.0f)
            left_edge_effect_quads++;
        if (active_clip.x < WM_FRAME_WIDTH &&
            active_clip.x + active_clip.width > WM_FRAME_WIDTH)
            right_edge_effect_quads++;
    }
    bool empty_art = false;
    for (size_t index = 0; index < 4; index++) {
        if (quad->textures[0] == empty_channel_textures[index])
            empty_art = true;
    }
    if (!empty_art) return;
    if (active_clip.x < 0.0f &&
        active_clip.x + active_clip.width > 0.0f) {
        left_edge_logo_quads++;
    }
    if (active_clip.x < WM_FRAME_WIDTH &&
        active_clip.x + active_clip.width > WM_FRAME_WIDTH) {
        right_edge_logo_quads++;
    }
}

uint32_t wm_platform_create_texture(WmPlatform *platform, int width,
                                    int height, const uint8_t *rgba) {
    (void)platform;
    assert(width > 0 && height > 0 && rgba);
    return next_texture++;
}

uint32_t wm_platform_create_render_texture(WmPlatform *platform) {
    (void)platform;
    return next_texture++;
}

bool wm_platform_begin_target(WmPlatform *platform, uint32_t texture,
                              WmColor clear_color) {
    (void)platform;
    (void)texture;
    (void)clear_color;
    return true;
}

void wm_platform_destroy_texture(WmPlatform *platform, uint32_t texture) {
    (void)platform;
    (void)texture;
}

static void draw_edge(WmResourceScene *scene, const WmMenu *menu,
                      float elapsed_seconds) {
    clipped = false;
    right_edge_clips = 0;
    left_edge_clips = 0;
    right_edge_logo_quads = 0;
    left_edge_logo_quads = 0;
    right_edge_effect_quads = 0;
    left_edge_effect_quads = 0;
    const WmResourceSceneFrame frame = {
        .elapsed_seconds = elapsed_seconds,
        .hover = {WM_HIT_NONE, -1}
    };
    wm_resource_scene_draw(scene, menu, &frame);
}

int main(int argc, char **argv) {
    const char *assets = argc > 1 ? argv[1] : ".local/native-assets";
    WmPlatform *platform = (WmPlatform *)1;
    WmMenu menu;
    wm_menu_init(&menu);
    WmTextureCache *textures = wm_texture_cache_create(
        platform, assets, 128u * 1024u * 1024u);
    WmFontCache *fonts = wm_font_cache_create(
        platform, assets, 16u * 1024u * 1024u);
    assert(textures && fonts);
    for (size_t index = 0; index < 4; index++) {
        char path[80];
        int length = snprintf(path, sizeof(path),
            "textures/chanSel/my_TV_c_p%zu.png", index);
        assert(length > 0 && length < (int)sizeof(path));
        if (!wm_texture_cache_resolve(textures, path,
                                      &empty_channel_textures[index])) {
            puts("Grid edge source art test skipped: local WAD export absent.");
            wm_font_cache_destroy(fonts);
            wm_texture_cache_destroy(textures);
            return 0;
        }
    }
    assert(wm_texture_cache_resolve(textures,
        "textures/chanSel/my_TVSpe_a.png", &empty_effect_texture));
    WmResourceScene *scene = wm_resource_scene_create(
        platform, assets, &menu, textures, fonts);
    assert(scene);

    draw_edge(scene, &menu, 0.0f);
    assert(right_edge_clips == 3);
    assert(right_edge_logo_quads >= 3);
    assert(right_edge_effect_quads >= 3);
    assert(left_edge_clips == 0);

    assert(wm_menu_change_page(&menu, 1));
    wm_menu_tick(&menu, 10.0f / 60.0f);
    draw_edge(scene, &menu, 10.0f / 60.0f);
    assert(right_edge_clips == 3);
    assert(right_edge_logo_quads >= 3);
    assert(right_edge_effect_quads >= 3);

    wm_menu_tick(&menu, 10.0f / 60.0f);
    draw_edge(scene, &menu, 20.0f / 60.0f);
    assert(left_edge_clips == 3);
    assert(left_edge_logo_quads >= 3);
    assert(left_edge_effect_quads >= 3);

    wm_resource_scene_destroy(scene);
    wm_texture_cache_destroy(textures);
    wm_font_cache_destroy(fonts);
    puts("Source empty channel art is submitted through both edge clips.");
    return 0;
}
