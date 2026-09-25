#include "wii_menu/resource_scene.h"

#include <assert.h>
#include <math.h>
#include <stdio.h>

static uint32_t next_texture = 1;
static uint32_t badge_texture;
static unsigned badge_quads;
static unsigned badge_glyphs;
static unsigned default_icon_quads;
static unsigned numbered_icon_quads;

void wm_platform_begin(WmPlatform *platform, WmColor clear_color) {
    (void)platform;
    (void)clear_color;
}

void wm_platform_end(WmPlatform *platform) {
    (void)platform;
}

void wm_platform_set_clip(WmPlatform *platform, const WmClipRect *clip) {
    (void)platform;
    (void)clip;
}

void wm_platform_draw_quad(WmPlatform *platform, const WmQuad *quad) {
    (void)platform;
    (void)quad;
}

void wm_platform_draw_vertices(WmPlatform *platform,
                               const WmDrawVertex vertices[4],
                               uint32_t texture) {
    (void)platform;
    (void)texture;
    if (vertices[0].x > 550.0f && vertices[0].x < 640.0f &&
        vertices[0].y > 370.0f &&
        vertices[0].y < 440.0f && vertices[0].color.a > 0.01f) {
        badge_glyphs++;
    }
}

void wm_platform_prepare_material(WmPlatform *platform,
                                  const WmMaterialQuad *quad) {
    (void)platform;
    (void)quad;
}

void wm_platform_draw_material_quad(WmPlatform *platform,
                                    const WmMaterialQuad *quad) {
    (void)platform;
    if (quad->texture_count == 0 || quad->textures[0] != badge_texture)
        return;
    const float x = quad->vertices[0].x;
    const float y = quad->vertices[0].y;
    if (x > 500.0f && y > 330.0f && y < 440.0f &&
        quad->vertices[0].color.a > 0.01f) {
        badge_quads++;
        /* The source WAD uses the same texture for BbsMark0 and Picture_00,
         * but their first material register colors identify the two panes. */
        float red = quad->registers[0][0];
        if (fabsf(red - 140.0f / 255.0f) < 0.02f) {
            default_icon_quads++;
        } else if (fabsf(red - 100.0f / 255.0f) < 0.02f) {
            numbered_icon_quads++;
        }
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

static void expect_badge(WmResourceScene *scene, const WmMenu *menu,
                         float seconds, WmHitType hover,
                         unsigned expected_digits) {
    badge_quads = 0;
    badge_glyphs = 0;
    default_icon_quads = 0;
    numbered_icon_quads = 0;
    const WmResourceSceneFrame frame = {
        .elapsed_seconds = seconds,
        .hover = {hover, -1}
    };
    wm_resource_scene_draw(scene, menu, &frame);
    assert(badge_quads == 1);
    assert(default_icon_quads == (expected_digits ? 0u : 1u));
    assert(numbered_icon_quads == (expected_digits ? 1u : 0u));
    assert(badge_glyphs == expected_digits);
}

int main(int argc, char **argv) {
    const char *assets = argc > 1 ? argv[1] : ".local/native-assets";
    WmPlatform *platform = (WmPlatform *)1;
    WmMenu menu;
    wm_menu_init(&menu);
    if (!wm_catalog_load(&menu, assets)) {
        puts("Footer badge comparison skipped: prepared assets unavailable.");
        return 0;
    }
    WmTextureCache *textures = wm_texture_cache_create(
        platform, assets, 128u * 1024u * 1024u);
    WmFontCache *fonts = wm_font_cache_create(
        platform, assets, 16u * 1024u * 1024u);
    assert(textures && fonts);
    assert(wm_texture_cache_resolve(textures,
        "textures/cmnBtn/my_TopBtn_a0.png", &badge_texture));
    WmResourceScene *scene = wm_resource_scene_create(
        platform, assets, &menu, textures, fonts);
    assert(scene);

    expect_badge(scene, &menu, 0.0f, WM_HIT_NONE, 0);
    expect_badge(scene, &menu, 0.1f, WM_HIT_BOARD, 0);
    expect_badge(scene, &menu, 0.2f, WM_HIT_BOARD, 0);
    expect_badge(scene, &menu, 0.4f, WM_HIT_NONE, 0);

    wm_resource_scene_set_message_badge(scene, 2, false);
    expect_badge(scene, &menu, 0.6f, WM_HIT_NONE, 1);
    expect_badge(scene, &menu, 0.7f, WM_HIT_BOARD, 1);
    expect_badge(scene, &menu, 0.8f, WM_HIT_BOARD, 1);
    expect_badge(scene, &menu, 1.0f, WM_HIT_NONE, 1);

    wm_resource_scene_set_message_badge(scene, 2, true);
    /* Startup updates the badge throughout Health and Safety before the
     * Home Menu audio begins; the first arrival cue must remain queued. */
    for (unsigned frame = 0; frame < 120; frame++)
        wm_resource_scene_set_message_badge(scene, 2, true);
    assert(wm_resource_scene_take_new_mail_sound(scene));
    assert(!wm_resource_scene_take_new_mail_sound(scene));
    expect_badge(scene, &menu, 1.2f, WM_HIT_BOARD, 1);
    expect_badge(scene, &menu, 4.2f, WM_HIT_NONE, 1);
    assert(wm_resource_scene_take_new_mail_sound(scene));
    wm_resource_scene_set_message_badge(scene, 2, false);
    expect_badge(scene, &menu, 4.4f, WM_HIT_NONE, 1);
    assert(!wm_resource_scene_take_new_mail_sound(scene));

    wm_resource_scene_set_message_badge(scene, 120, false);
    expect_badge(scene, &menu, 4.6f, WM_HIT_BOARD, 2);
    wm_resource_scene_set_message_badge(scene, 0, false);
    expect_badge(scene, &menu, 4.8f, WM_HIT_BOARD, 0);
    expect_badge(scene, &menu, 5.0f, WM_HIT_NONE, 0);

    wm_resource_scene_destroy(scene);
    wm_texture_cache_destroy(textures);
    wm_font_cache_destroy(fonts);
    puts("Footer badge remains exclusive through hover, count changes, and arrival cues.");
    return 0;
}
