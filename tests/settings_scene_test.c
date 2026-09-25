#include "wii_menu/settings_scene.h"
#include "wii_menu/layout_runtime.h"

#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <string.h>

/* The platform spies also check the Settings-specific wide draw projection. */
static WmQuad drawn_quads[2048];
static size_t drawn_quad_count;
static WmClipRect document_clip;
static bool document_clip_seen;
static uint32_t next_texture = 1;

void wm_platform_set_clip(WmPlatform *platform, const WmClipRect *rect) {
    (void)platform;
    if (rect) {
        document_clip = *rect;
        document_clip_seen = true;
    }
}

void wm_platform_draw_quad(WmPlatform *platform, const WmQuad *quad) {
    (void)platform;
    assert(drawn_quad_count < sizeof(drawn_quads) / sizeof(drawn_quads[0]));
    drawn_quads[drawn_quad_count++] = *quad;
}

void wm_platform_draw_vertices(WmPlatform *platform,
                               const WmDrawVertex vertices[4],
                               uint32_t texture) {
    (void)platform;
    (void)vertices;
    (void)texture;
}

uint32_t wm_platform_create_texture(WmPlatform *platform, int width,
                                    int height, const uint8_t *rgba) {
    (void)platform;
    (void)width;
    (void)height;
    (void)rgba;
    return next_texture++;
}

void wm_platform_destroy_texture(WmPlatform *platform, uint32_t texture) {
    (void)platform;
    (void)texture;
}

static void test_wide_projection(const char *assets) {
    WmSettingsScene *scene = wm_settings_scene_create(
        (WmPlatform *)1, assets, (WmTextureCache *)1,
        (WmFontCache *)1);
    assert(scene);
    WmSettingsProjection narrow = wm_settings_scene_projection(scene);
    assert(narrow.document_x == 16.0f);
    assert(narrow.document_width == 608.0f);
    assert(narrow.side_width == 0.0f);

    wm_settings_scene_set_wide(scene, true);
    WmSettingsProjection wide = wm_settings_scene_projection(scene);
    float side = 112.0f * 640.0f / 832.0f;
    assert(fabsf(wide.side_width - side) < 0.001f);
    assert(fabsf(wide.document_x - side) < 0.001f);
    assert(fabsf(wide.document_width - 608.0f * 640.0f / 832.0f)
           < 0.001f);
    assert(fabsf(wide.document_x + wide.document_width + wide.side_width -
                 640.0f) < 0.001f);

    assert(wm_settings_scene_open(scene));
    wm_settings_scene_advance(scene, 21.0f);
    assert(wm_settings_scene_hit(scene, 80, 400) ==
           WM_SETTINGS_CONTROL_NONE);
    assert(wm_settings_scene_hit(scene, 135, 400) ==
           WM_SETTINGS_CONTROL_BACK);
    assert(wm_settings_scene_hit(scene, 510, 220) ==
           WM_SETTINGS_CONTROL_NEXT);
    assert(wm_settings_scene_hit(scene, 545, 105) ==
           WM_SETTINGS_CONTROL_NONE);
    assert(wm_settings_scene_activate(scene, WM_SETTINGS_CONTROL_ITEM_3));
    assert(wm_settings_scene_hit(scene, 201, 100) ==
           WM_SETTINGS_CONTROL_ITEM_1);
    wm_settings_scene_destroy(scene);
}

static void test_wide_render(const char *assets) {
    WmTextureCache *textures = wm_texture_cache_create(
        (WmPlatform *)1, assets, 64 * 1024 * 1024);
    WmFontCache *fonts = wm_font_cache_create(
        (WmPlatform *)1, assets, 16 * 1024 * 1024);
    assert(textures && fonts);
    uint32_t side_texture = 0;
    if (!wm_texture_cache_resolve(
            textures, "textures/settings_html/side-panel.png",
            &side_texture)) {
        puts("Wii Settings side-panel render test skipped: local export absent.");
        wm_font_cache_destroy(fonts);
        wm_texture_cache_destroy(textures);
        return;
    }
    WmSettingsScene *scene = wm_settings_scene_create(
        (WmPlatform *)1, assets, textures, fonts);
    assert(scene);
    wm_settings_scene_set_wide(scene, true);
    assert(wm_settings_scene_open(scene));
    wm_settings_scene_advance(scene, 21.0f);
    drawn_quad_count = 0;
    document_clip_seen = false;
    wm_texture_cache_begin_frame(textures);
    wm_font_cache_begin_frame(fonts);
    assert(wm_settings_scene_draw(scene));
    WmSettingsProjection projected = wm_settings_scene_projection(scene);
    assert(document_clip_seen);
    assert(fabsf(document_clip.x - projected.document_x) < 0.001f);
    assert(fabsf(document_clip.width - projected.document_width) < 0.001f);
    unsigned side_count = 0;
    for (size_t index = 0; index < drawn_quad_count; index++) {
        const WmQuad *quad = &drawn_quads[index];
        if (quad->texture != side_texture) continue;
        float expected_x = side_count == 0 ? 0.0f
            : projected.document_x + projected.document_width;
        assert(side_count < 2);
        assert(fabsf(quad->x - expected_x) < 0.001f);
        assert(fabsf(quad->width - projected.side_width) < 0.001f);
        assert(quad->height == 456.0f);
        side_count++;
    }
    assert(side_count == 2);

    assert(wm_settings_scene_activate(scene, WM_SETTINGS_CONTROL_ITEM_3));
    assert(wm_settings_scene_activate(scene, WM_SETTINGS_CONTROL_ITEM_2));
    wm_settings_scene_advance(scene, 20.0f);
    uint32_t standard_texture = 0;
    uint32_t wide_texture = 0;
    assert(wm_texture_cache_resolve(
        textures, "textures/settings_html/widescreen-standard.png",
        &standard_texture));
    assert(wm_texture_cache_resolve(
        textures, "textures/settings_html/widescreen-wide.png",
        &wide_texture));
    drawn_quad_count = 0;
    wm_texture_cache_begin_frame(textures);
    wm_font_cache_begin_frame(fonts);
    assert(wm_settings_scene_draw(scene));
    unsigned choice_count = 0;
    for (size_t index = 0; index < drawn_quad_count; index++) {
        const WmQuad *quad = &drawn_quads[index];
        if (quad->texture == standard_texture) {
            assert(fabsf(quad->x - (48.0f + 112.0f) * 640.0f /
                         832.0f) < 0.001f);
            assert(quad->y == 146.0f && quad->height == 140.0f);
            choice_count++;
        } else if (quad->texture == wide_texture) {
            assert(fabsf(quad->x - (296.0f + 112.0f) * 640.0f /
                         832.0f) < 0.001f);
            assert(quad->y == 146.0f && quad->height == 140.0f);
            choice_count++;
        }
    }
    assert(choice_count == 2);
    assert(wm_settings_scene_hit(scene, 160, 200) ==
           WM_SETTINGS_CONTROL_ITEM_1);
    assert(wm_settings_scene_hit(scene, 390, 200) ==
           WM_SETTINGS_CONTROL_ITEM_2);

    assert(wm_settings_scene_back(scene));
    assert(wm_settings_scene_activate(scene, WM_SETTINGS_CONTROL_ITEM_1));
    wm_settings_scene_advance(scene, 20.0f);
    uint32_t left_arrow_texture = 0;
    uint32_t right_arrow_texture = 0;
    assert(wm_texture_cache_resolve(
        textures, "textures/settings_html/arrow-left.png",
        &left_arrow_texture));
    assert(wm_texture_cache_resolve(
        textures, "textures/settings_html/arrow-right.png",
        &right_arrow_texture));
    drawn_quad_count = 0;
    wm_texture_cache_begin_frame(textures);
    wm_font_cache_begin_frame(fonts);
    assert(wm_settings_scene_draw(scene));
    unsigned arrow_count = 0;
    for (size_t index = 0; index < drawn_quad_count; index++) {
        const WmQuad *quad = &drawn_quads[index];
        if (quad->texture == left_arrow_texture) {
            assert(fabsf(quad->x - (160.0f + 112.0f) * 640.0f /
                         832.0f) < 0.001f);
            assert(quad->y == 180.0f && quad->height == 72.0f);
            arrow_count++;
        } else if (quad->texture == right_arrow_texture) {
            assert(fabsf(quad->x - (376.0f + 112.0f) * 640.0f /
                         832.0f) < 0.001f);
            assert(quad->y == 180.0f && quad->height == 72.0f);
            arrow_count++;
        }
    }
    assert(arrow_count == 2);
    assert(wm_settings_scene_hit(scene, 210, 200) ==
           WM_SETTINGS_CONTROL_ITEM_1);
    assert(wm_settings_scene_hit(scene, 380, 200) ==
           WM_SETTINGS_CONTROL_ITEM_2);

    assert(wm_settings_scene_back(scene));
    assert(wm_settings_scene_back(scene));
    assert(wm_settings_scene_activate(scene, WM_SETTINGS_CONTROL_ITEM_2));
    assert(wm_settings_scene_activate(scene, WM_SETTINGS_CONTROL_ITEM_1));
    wm_settings_scene_advance(scene, 20.0f);
    uint32_t up_texture = 0;
    uint32_t down_texture = 0;
    assert(wm_texture_cache_resolve(
        textures, "textures/settings_html/arrow-up.png",
        &up_texture));
    assert(wm_texture_cache_resolve(
        textures, "textures/settings_html/arrow-down.png",
        &down_texture));
    drawn_quad_count = 0;
    wm_texture_cache_begin_frame(textures);
    wm_font_cache_begin_frame(fonts);
    assert(wm_settings_scene_draw(scene));
    unsigned date_arrows = 0;
    for (size_t index = 0; index < drawn_quad_count; index++) {
        const WmQuad *quad = &drawn_quads[index];
        if (quad->texture != up_texture && quad->texture != down_texture)
            continue;
        assert(fabsf(quad->width - 72.0f * 640.0f / 832.0f)
               < 0.001f);
        assert(quad->height == 72.0f);
        assert(quad->y == 108.0f || quad->y == 253.0f);
        date_arrows++;
    }
    assert(date_arrows == 6);
    assert(wm_settings_scene_hit(scene, 398, 120) ==
           WM_SETTINGS_CONTROL_ITEM_1);
    assert(wm_settings_scene_hit(scene, 160, 120) ==
           WM_SETTINGS_CONTROL_ITEM_3);
    assert(wm_settings_scene_hit(scene, 266, 120) ==
           WM_SETTINGS_CONTROL_ITEM_5);
    wm_settings_scene_destroy(scene);
    wm_font_cache_destroy(fonts);
    wm_texture_cache_destroy(textures);
}

static size_t count_drawn_texture(uint32_t texture, WmQuad *first) {
    size_t count = 0;
    for (size_t index = 0; index < drawn_quad_count; index++) {
        if (drawn_quads[index].texture != texture) continue;
        if (first && count == 0) *first = drawn_quads[index];
        count++;
    }
    return count;
}

static void test_initial_page_fades_as_one_raster(const char *assets) {
    WmTextureCache *textures = wm_texture_cache_create(
        (WmPlatform *)1, assets, 64u * 1024u * 1024u);
    WmFontCache *fonts = wm_font_cache_create(
        (WmPlatform *)1, assets, 16u * 1024u * 1024u);
    assert(textures && fonts);
    uint32_t background = 0;
    uint32_t side_panel = 0;
    uint32_t title = 0;
    assert(wm_texture_cache_resolve(
        textures, "textures/settings_html/background.png", &background));
    assert(wm_texture_cache_resolve(
        textures, "textures/settings_html/side-panel.png", &side_panel));
    assert(wm_texture_cache_resolve(
        textures, "textures/settings_html/title-tab.png", &title));
    WmSettingsScene *scene = wm_settings_scene_create(
        (WmPlatform *)1, assets, textures, fonts);
    assert(scene);
    wm_settings_scene_set_wide(scene, true);
    assert(wm_settings_scene_open(scene));

    const float frames[] = {0.0f, 11.0f, 10.0f};
    const float alpha[] = {0.0f, 127.0f / 255.0f, 1.0f};
    for (size_t step = 0; step < 3; step++) {
        wm_settings_scene_advance(scene, frames[step]);
        drawn_quad_count = 0;
        wm_texture_cache_begin_frame(textures);
        wm_font_cache_begin_frame(fonts);
        assert(wm_settings_scene_draw(scene));
        WmQuad document_quad = {0};
        WmQuad panel_quad = {0};
        WmQuad title_quad = {0};
        assert(count_drawn_texture(background, &document_quad) == 1);
        assert(count_drawn_texture(side_panel, &panel_quad) == 2);
        assert(count_drawn_texture(title, &title_quad) == 1);
        assert(fabsf(document_quad.color.a - alpha[step]) < 0.001f);
        assert(fabsf(panel_quad.color.a - alpha[step]) < 0.001f);
        assert(fabsf(title_quad.color.a - alpha[step]) < 0.001f);
    }

    wm_settings_scene_destroy(scene);
    wm_font_cache_destroy(fonts);
    wm_texture_cache_destroy(textures);
}

static void test_page_crossfade_keeps_side_panels_opaque(
    const char *assets) {
    WmTextureCache *textures = wm_texture_cache_create(
        (WmPlatform *)1, assets, 64u * 1024u * 1024u);
    WmFontCache *fonts = wm_font_cache_create(
        (WmPlatform *)1, assets, 16u * 1024u * 1024u);
    assert(textures && fonts);
    uint32_t background = 0;
    uint32_t side_panel = 0;
    assert(wm_texture_cache_resolve(
        textures, "textures/settings_html/background.png", &background));
    assert(wm_texture_cache_resolve(
        textures, "textures/settings_html/side-panel.png", &side_panel));
    WmSettingsScene *scene = wm_settings_scene_create(
        (WmPlatform *)1, assets, textures, fonts);
    assert(scene);
    wm_settings_scene_set_wide(scene, true);
    assert(wm_settings_scene_open(scene));
    wm_settings_scene_advance(scene, 21.0f);
    assert(wm_settings_scene_activate(scene, WM_SETTINGS_CONTROL_ITEM_1));
    wm_settings_scene_advance(scene, 10.0f);

    drawn_quad_count = 0;
    wm_texture_cache_begin_frame(textures);
    wm_font_cache_begin_frame(fonts);
    assert(wm_settings_scene_draw(scene));
    WmQuad first_background = {0};
    WmQuad first_panel = {0};
    assert(count_drawn_texture(background, &first_background) == 2);
    assert(count_drawn_texture(side_panel, &first_panel) == 2);
    assert(first_background.color.a == 1.0f);
    assert(first_panel.color.a == 1.0f);
    unsigned faded_backgrounds = 0;
    unsigned black_shells = 0;
    for (size_t index = 0; index < drawn_quad_count; index++) {
        const WmQuad *quad = &drawn_quads[index];
        if (quad->texture == background &&
            fabsf(quad->color.a - 127.0f / 255.0f) < 0.001f)
            faded_backgrounds++;
        if (!quad->texture && quad->x == 0.0f && quad->y == 0.0f &&
            quad->width == WM_FRAME_WIDTH &&
            quad->height == WM_FRAME_HEIGHT)
            black_shells++;
    }
    assert(faded_backgrounds == 1);
    assert(black_shells == 1);

    wm_settings_scene_destroy(scene);
    wm_font_cache_destroy(fonts);
    wm_texture_cache_destroy(textures);
}

static size_t count_row_glyphs(float top, float bottom) {
    size_t count = 0;
    for (size_t index = 0; index < drawn_quad_count; index++) {
        const WmQuad *quad = &drawn_quads[index];
        if (quad->texture && quad->y >= top && quad->y < bottom &&
            quad->width < 50.0f && quad->height <= 30.0f)
            count++;
    }
    return count;
}

static size_t count_glyphs_in_box(float left, float right,
                                  float top, float bottom) {
    size_t count = 0;
    for (size_t index = 0; index < drawn_quad_count; index++) {
        const WmQuad *quad = &drawn_quads[index];
        if (quad->texture && quad->x >= left && quad->x < right &&
            quad->y >= top && quad->y < bottom &&
            quad->width < 50.0f && quad->height <= 30.0f)
            count++;
    }
    return count;
}

static void assert_adjust_arrow_images(uint32_t normal_texture,
                                       uint32_t focus_texture,
                                       float x, float normal_alpha,
                                       float focus_alpha) {
    unsigned normal_count = 0;
    unsigned focus_count = 0;
    for (size_t index = 0; index < drawn_quad_count; index++) {
        const WmQuad *quad = &drawn_quads[index];
        if (quad->x != x || quad->y != 108.0f) continue;
        if (quad->texture == normal_texture) {
            assert(fabsf(quad->color.a - normal_alpha) < 0.001f);
            normal_count++;
        } else if (quad->texture == focus_texture) {
            assert(fabsf(quad->color.a - focus_alpha) < 0.001f);
            focus_count++;
        }
    }
    assert(normal_count == (normal_alpha > 0.0f ? 1u : 0u));
    assert(focus_count == (focus_alpha > 0.0f ? 1u : 0u));
}

static void test_calendar_arrow_rollover(const char *assets) {
    WmTextureCache *textures = wm_texture_cache_create(
        (WmPlatform *)1, assets, 64 * 1024 * 1024);
    WmFontCache *fonts = wm_font_cache_create(
        (WmPlatform *)1, assets, 16 * 1024 * 1024);
    assert(textures && fonts);
    uint32_t normal = 0;
    uint32_t focused = 0;
    assert(wm_texture_cache_resolve(
        textures, "textures/settings_html/arrow-up.png", &normal));
    assert(wm_texture_cache_resolve(
        textures, "textures/settings_html/arrow-up-focus.png",
        &focused));
    WmSettingsScene *scene = wm_settings_scene_create(
        (WmPlatform *)1, assets, textures, fonts);
    assert(scene);
    assert(wm_settings_scene_open(scene));
    wm_settings_scene_advance(scene, 21.0f);
    assert(wm_settings_scene_activate(scene, WM_SETTINGS_CONTROL_ITEM_2));
    assert(wm_settings_scene_activate(scene, WM_SETTINGS_CONTROL_ITEM_1));
    wm_settings_scene_advance(scene, 20.0f);

    assert(wm_settings_scene_hover(scene, WM_SETTINGS_CONTROL_ITEM_1));
    drawn_quad_count = 0;
    wm_texture_cache_begin_frame(textures);
    wm_font_cache_begin_frame(fonts);
    assert(wm_settings_scene_draw(scene));
    assert_adjust_arrow_images(normal, focused, 416.0f, 0.0f, 1.0f);

    wm_settings_scene_advance(scene, 10.0f);
    drawn_quad_count = 0;
    wm_texture_cache_begin_frame(textures);
    wm_font_cache_begin_frame(fonts);
    assert(wm_settings_scene_draw(scene));
    assert_adjust_arrow_images(normal, focused, 416.0f, 0.0f, 1.0f);

    assert(wm_settings_scene_back(scene));
    assert(wm_settings_scene_activate(scene, WM_SETTINGS_CONTROL_ITEM_2));
    wm_settings_scene_advance(scene, 20.0f);
    assert(wm_settings_scene_hover(scene, WM_SETTINGS_CONTROL_ITEM_1));
    wm_settings_scene_advance(scene, 20.0f);
    drawn_quad_count = 0;
    wm_texture_cache_begin_frame(textures);
    wm_font_cache_begin_frame(fonts);
    assert(wm_settings_scene_draw(scene));
    assert_adjust_arrow_images(normal, focused, 216.0f, 0.0f, 1.0f);

    wm_settings_scene_destroy(scene);
    wm_font_cache_destroy(fonts);
    wm_texture_cache_destroy(textures);
}

static void test_country_source_art(const char *assets) {
    WmTextureCache *textures = wm_texture_cache_create(
        (WmPlatform *)1, assets, 64u * 1024u * 1024u);
    WmFontCache *fonts = wm_font_cache_create(
        (WmPlatform *)1, assets, 16u * 1024u * 1024u);
    assert(textures && fonts);
    uint32_t row = 0;
    uint32_t focus = 0;
    uint32_t left = 0;
    uint32_t right = 0;
    assert(wm_texture_cache_resolve(
        textures, "textures/settings_html/country-row.png", &row));
    assert(wm_texture_cache_resolve(
        textures, "textures/settings_html/country-row-focus.png", &focus));
    assert(wm_texture_cache_resolve(
        textures, "textures/settings_html/country-choice-left.png", &left));
    assert(wm_texture_cache_resolve(
        textures, "textures/settings_html/country-choice-right.png", &right));

    WmSettingsScene *scene = wm_settings_scene_create(
        (WmPlatform *)1, assets, textures, fonts);
    assert(scene);
    assert(wm_settings_scene_open(scene));
    wm_settings_scene_advance(scene, 21.0f);
    for (unsigned page = 1; page < 3; page++) {
        assert(wm_settings_scene_activate(scene, WM_SETTINGS_CONTROL_NEXT));
        wm_settings_scene_advance(scene, 40.0f);
    }
    assert(wm_settings_scene_activate(scene, WM_SETTINGS_CONTROL_ITEM_2));
    wm_settings_scene_advance(scene, 20.0f);
    assert(wm_settings_scene_snapshot(scene).country_page == 0);
    assert(wm_settings_scene_snapshot(scene).country_choice == 41);

    drawn_quad_count = 0;
    wm_texture_cache_begin_frame(textures);
    wm_font_cache_begin_frame(fonts);
    assert(wm_settings_scene_draw(scene));
    WmQuad first_row;
    WmQuad selected_left;
    WmQuad selected_right;
    assert(count_drawn_texture(row, &first_row) == 4);
    assert(count_drawn_texture(focus, NULL) == 0);
    assert(count_drawn_texture(left, NULL) == 0);
    assert(count_drawn_texture(right, NULL) == 0);
    assert(first_row.x == 104.0f && first_row.y == 132.0f);

    for (unsigned page = 0; page < 9; page++)
        assert(wm_settings_scene_activate(scene, WM_SETTINGS_CONTROL_ITEM_6));
    assert(wm_settings_scene_snapshot(scene).country_page == 9);
    assert(wm_settings_scene_activate(scene, WM_SETTINGS_CONTROL_ITEM_1));
    drawn_quad_count = 0;
    wm_texture_cache_begin_frame(textures);
    wm_font_cache_begin_frame(fonts);
    assert(wm_settings_scene_draw(scene));
    assert(count_drawn_texture(row, &first_row) == 4);
    assert(count_drawn_texture(left, &selected_left) == 1);
    assert(count_drawn_texture(right, &selected_right) == 1);
    assert(first_row.x == 104.0f && first_row.y == 76.0f);
    assert(first_row.width == 432.0f && first_row.height == 56.0f);
    assert(selected_left.x == 95.0f && selected_left.y == 72.0f);
    assert(selected_right.x == 519.0f && selected_right.y == 72.0f);
    assert(selected_left.width == 24.0f && selected_left.height == 64.0f);

    assert(wm_settings_scene_hover(scene, WM_SETTINGS_CONTROL_ITEM_2));
    wm_settings_scene_advance(scene, 10.0f);
    drawn_quad_count = 0;
    wm_texture_cache_begin_frame(textures);
    wm_font_cache_begin_frame(fonts);
    assert(wm_settings_scene_draw(scene));
    WmQuad focused_row;
    assert(count_drawn_texture(focus, &focused_row) == 1);
    assert(focused_row.x == 104.0f && focused_row.y == 132.0f);
    assert(focused_row.width == 432.0f && focused_row.height == 56.0f);
    assert(focused_row.color.a == 1.0f);

    assert(wm_settings_scene_activate(scene, WM_SETTINGS_CONTROL_ITEM_2));
    drawn_quad_count = 0;
    wm_texture_cache_begin_frame(textures);
    wm_font_cache_begin_frame(fonts);
    assert(wm_settings_scene_draw(scene));
    assert(count_drawn_texture(left, &selected_left) == 1);
    assert(selected_left.y == 128.0f);

    for (unsigned page = 9; page > 0; page--)
        assert(wm_settings_scene_activate(
            scene, WM_SETTINGS_CONTROL_PREVIOUS));
    wm_settings_scene_advance(scene, 20.0f);
    assert(wm_settings_scene_snapshot(scene).country_page == 0);
    drawn_quad_count = 0;
    wm_texture_cache_begin_frame(textures);
    wm_font_cache_begin_frame(fonts);
    assert(wm_settings_scene_draw(scene));
    assert(count_drawn_texture(row, &first_row) == 4);
    assert(first_row.x == 104.0f && first_row.y == 132.0f);
    assert(count_drawn_texture(left, NULL) == 0);
    assert(count_drawn_texture(right, NULL) == 0);

    assert(wm_settings_scene_activate(scene, WM_SETTINGS_CONTROL_NEXT));
    assert(wm_settings_scene_snapshot(scene).country_choice == 45);
    assert(wm_settings_scene_activate(scene, WM_SETTINGS_CONTROL_ITEM_2));
    assert(wm_settings_scene_snapshot(scene).country_page == 0);
    assert(wm_settings_scene_snapshot(scene).country_choice == 45);

    wm_settings_scene_destroy(scene);
    wm_font_cache_destroy(fonts);
    wm_texture_cache_destroy(textures);
}

static void test_sensitivity_source_art(const char *assets) {
    WmTextureCache *textures = wm_texture_cache_create(
        (WmPlatform *)1, assets, 64 * 1024 * 1024);
    WmFontCache *fonts = wm_font_cache_create(
        (WmPlatform *)1, assets, 16 * 1024 * 1024);
    assert(textures && fonts);
    const char *const names[] = {
        "sensitivity-minus", "sensitivity-gauge", "sensitivity-plus",
        "sensitivity-rank-3", "sensitivity-rank-4", "footer-button"
    };
    uint32_t art[sizeof(names) / sizeof(names[0])] = {0};
    for (size_t index = 0; index < sizeof(names) / sizeof(names[0]);
         index++) {
        char path[128];
        int length = snprintf(path, sizeof(path),
                              "textures/settings_html/%s.png", names[index]);
        assert(length > 0 && length < (int)sizeof(path));
        assert(wm_texture_cache_resolve(textures, path, &art[index]));
    }
    WmSettingsScene *scene = wm_settings_scene_create(
        (WmPlatform *)1, assets, textures, fonts);
    assert(scene);
    wm_settings_scene_set_wide(scene, true);
    assert(wm_settings_scene_open(scene));
    wm_settings_scene_advance(scene, 21.0f);
    assert(wm_settings_scene_activate(scene, WM_SETTINGS_CONTROL_NEXT));
    wm_settings_scene_advance(scene, 41.0f);
    assert(wm_settings_scene_activate(scene, WM_SETTINGS_CONTROL_ITEM_2));
    assert(wm_settings_scene_activate(scene, WM_SETTINGS_CONTROL_ITEM_2));
    assert(wm_settings_scene_activate(scene, WM_SETTINGS_CONTROL_NEXT));
    wm_settings_scene_advance(scene, 21.0f);
    assert(wm_settings_scene_snapshot(scene).sensitivity == 3);

    WmSettingsProjection projection = wm_settings_scene_projection(scene);
    assert(projection.side_width > 0.0f);
    drawn_quad_count = 0;
    wm_texture_cache_begin_frame(textures);
    wm_font_cache_begin_frame(fonts);
    assert(wm_settings_scene_draw(scene));
    const float wide_scale = 640.0f / 832.0f;
    const float art_x[] = {74.0f, 156.0f, 502.0f, 276.0f};
    const float art_y[] = {304.0f, 296.0f, 304.0f, 300.0f};
    const float art_width[] = {32.0f, 296.0f, 32.0f, 56.0f};
    const float art_height[] = {32.0f, 48.0f, 32.0f, 56.0f};
    for (size_t index = 0; index < 4; index++) {
        WmQuad quad = {0};
        assert(count_drawn_texture(art[index], &quad) == 1);
        assert(fabsf(quad.x - (art_x[index] + 112.0f) * wide_scale)
               < 0.001f);
        assert(quad.y == art_y[index]);
        assert(fabsf(quad.width - art_width[index] * wide_scale)
               < 0.001f);
        assert(quad.height == art_height[index]);
    }
    assert(count_drawn_texture(art[4], NULL) == 0);
    assert(count_drawn_texture(art[5], NULL) == 0);
    for (size_t index = 0; index < drawn_quad_count; index++) {
        const WmQuad *quad = &drawn_quads[index];
        assert(!(quad->texture == 0 && quad->y >= 176.0f &&
                 quad->y < 284.0f));
    }
    assert(wm_settings_scene_hit(scene, 167, 320) ==
           WM_SETTINGS_CONTROL_ITEM_1);
    assert(wm_settings_scene_hit(scene, 498, 320) ==
           WM_SETTINGS_CONTROL_ITEM_2);
    assert(wm_settings_scene_hit(scene, 167, 390) ==
           WM_SETTINGS_CONTROL_NEXT);

    wm_settings_scene_set_wide(scene, false);
    drawn_quad_count = 0;
    wm_texture_cache_begin_frame(textures);
    wm_font_cache_begin_frame(fonts);
    assert(wm_settings_scene_draw(scene));
    WmQuad narrow_rank = {0};
    assert(count_drawn_texture(art[3], &narrow_rank) == 1);
    assert(narrow_rank.x == 292.0f && narrow_rank.y == 300.0f);
    assert(narrow_rank.width == 56.0f && narrow_rank.height == 56.0f);
    assert(wm_settings_scene_hit(scene, 16 + 90, 320) ==
           WM_SETTINGS_CONTROL_ITEM_1);
    assert(wm_settings_scene_hit(scene, 16 + 518, 320) ==
           WM_SETTINGS_CONTROL_ITEM_2);

    assert(wm_settings_scene_activate(scene, WM_SETTINGS_CONTROL_ITEM_2));
    drawn_quad_count = 0;
    wm_texture_cache_begin_frame(textures);
    wm_font_cache_begin_frame(fonts);
    assert(wm_settings_scene_draw(scene));
    assert(count_drawn_texture(art[3], NULL) == 0);
    assert(count_drawn_texture(art[4], NULL) == 1);
    assert(wm_settings_scene_activate(
        scene, wm_settings_scene_hit(scene, 16 + 320, 390)));
    assert(wm_settings_scene_snapshot(scene).detail == 0);
    assert(wm_settings_scene_snapshot(scene).sensitivity == 4);
    wm_settings_scene_destroy(scene);
    wm_font_cache_destroy(fonts);
    wm_texture_cache_destroy(textures);
}

static void test_connection_settings_split_rows(const char *assets) {
    WmTextureCache *textures = wm_texture_cache_create(
        (WmPlatform *)1, assets, 64u * 1024u * 1024u);
    WmFontCache *fonts = wm_font_cache_create(
        (WmPlatform *)1, assets, 16u * 1024u * 1024u);
    assert(textures && fonts);
    uint32_t split = 0;
    uint32_t generic = 0;
    uint32_t focus = 0;
    assert(wm_texture_cache_resolve(
        textures, "textures/settings_html/connection-split-row.png",
        &split));
    assert(wm_texture_cache_resolve(
        textures, "textures/settings_html/large-row.png", &generic));
    assert(wm_texture_cache_resolve(
        textures, "textures/settings_html/large-row-focus.png",
        &focus));
    WmSettingsScene *scene = wm_settings_scene_create(
        (WmPlatform *)1, assets, textures, fonts);
    assert(scene);
    assert(wm_settings_scene_open(scene));
    wm_settings_scene_advance(scene, 21.0f);
    assert(wm_settings_scene_activate(scene, WM_SETTINGS_CONTROL_NEXT));
    wm_settings_scene_advance(scene, 41.0f);
    assert(wm_settings_scene_activate(scene, WM_SETTINGS_CONTROL_ITEM_3));
    wm_settings_scene_advance(scene, 20.0f);
    assert(wm_settings_scene_activate(scene, WM_SETTINGS_CONTROL_ITEM_1));
    wm_settings_scene_advance(scene, 20.0f);

    drawn_quad_count = 0;
    wm_texture_cache_begin_frame(textures);
    wm_font_cache_begin_frame(fonts);
    assert(wm_settings_scene_draw(scene));
    assert(count_drawn_texture(split, NULL) == 3);
    assert(count_drawn_texture(generic, NULL) == 0);
    for (unsigned item = 0; item < 3; item++) {
        float top = 72.0f + item * 96.0f;
        bool split_row = false;
        for (size_t index = 0; index < drawn_quad_count; index++) {
            const WmQuad *quad = &drawn_quads[index];
            if (quad->texture == split && quad->y == top + 10.0f) {
                assert(quad->x == 120.0f && quad->width == 400.0f);
                assert(quad->height == 76.0f);
                split_row = true;
            }
        }
        assert(split_row);
        /* Source Connect_set_top places Connection N in the left field and
         * the bridge's default None in the right field. */
        assert(count_glyphs_in_box(135.0f, 350.0f,
                                   top + 25.0f, top + 65.0f) == 11);
        assert(count_glyphs_in_box(370.0f, 505.0f,
                                   top + 25.0f, top + 65.0f) == 4);
    }
    assert(wm_settings_scene_hover(scene, WM_SETTINGS_CONTROL_ITEM_2));
    wm_settings_scene_advance(scene, 10.0f);
    drawn_quad_count = 0;
    wm_texture_cache_begin_frame(textures);
    wm_font_cache_begin_frame(fonts);
    assert(wm_settings_scene_draw(scene));
    WmQuad focused = {0};
    assert(count_drawn_texture(focus, &focused) == 1);
    assert(focused.x == 123.0f && focused.y == 181.0f);
    assert(focused.width == 394.0f && focused.height == 70.0f);
    assert(focused.color.a == 1.0f);

    wm_settings_scene_destroy(scene);
    wm_font_cache_destroy(fonts);
    wm_texture_cache_destroy(textures);
}

static void test_empty_connection_choice_page(const char *assets) {
    WmTextureCache *textures = wm_texture_cache_create(
        (WmPlatform *)1, assets, 64u * 1024u * 1024u);
    WmFontCache *fonts = wm_font_cache_create(
        (WmPlatform *)1, assets, 16u * 1024u * 1024u);
    assert(textures && fonts);
    const char *paths[5] = {
        "textures/settings_html/tab-dark-gray.png",
        "textures/settings_html/tab-middle-gray-nested.png",
        "textures/settings_html/tab-gray-nested.png",
        "textures/settings_html/title-tab.png",
        "textures/settings_html/large-row.png"
    };
    uint32_t art[5] = {0};
    for (unsigned index = 0; index < 5; index++)
        assert(wm_texture_cache_resolve(textures, paths[index], &art[index]));
    uint32_t focus = 0;
    uint32_t footer = 0;
    assert(wm_texture_cache_resolve(
        textures, "textures/settings_html/large-row-focus.png", &focus));
    assert(wm_texture_cache_resolve(
        textures, "textures/settings_html/footer-button.png", &footer));
    WmSettingsScene *scene = wm_settings_scene_create(
        (WmPlatform *)1, assets, textures, fonts);
    assert(scene);
    assert(wm_settings_scene_open(scene));
    wm_settings_scene_advance(scene, 21.0f);
    assert(wm_settings_scene_activate(scene, WM_SETTINGS_CONTROL_NEXT));
    wm_settings_scene_advance(scene, 41.0f);
    assert(wm_settings_scene_activate(scene, WM_SETTINGS_CONTROL_ITEM_3));
    wm_settings_scene_advance(scene, 20.0f);
    assert(wm_settings_scene_activate(scene, WM_SETTINGS_CONTROL_ITEM_1));
    wm_settings_scene_advance(scene, 20.0f);

    for (unsigned slot = 1; slot <= 3; slot++) {
        assert(wm_settings_scene_activate(
            scene, (WmSettingsControl)(WM_SETTINGS_CONTROL_ITEM_1 +
                                       slot - 1)));
        wm_settings_scene_advance(scene, 20.0f);
        WmSettingsSnapshot snapshot = wm_settings_scene_snapshot(scene);
        assert(snapshot.category == 7 && snapshot.detail == 4);
        assert(snapshot.connection_slot == slot);
        drawn_quad_count = 0;
        wm_texture_cache_begin_frame(textures);
        wm_font_cache_begin_frame(fonts);
        assert(wm_settings_scene_draw(scene));

        /* Connect_select.html has three nested tabs and two Btn_List rows. */
        const float header_x[4] = {40.0f, 72.0f, 104.0f, 136.0f};
        for (unsigned index = 0; index < 4; index++) {
            WmQuad quad = {0};
            assert(count_drawn_texture(art[index], &quad) == 1);
            assert(quad.x == header_x[index]);
            assert(quad.y == (index == 3 ? 27.0f : 35.0f));
        }
        assert(count_glyphs_in_box(140.0f, 330.0f,
                                   30.0f, 65.0f) == 11);
        assert(count_drawn_texture(art[4], NULL) == 2);
        for (unsigned row = 0; row < 2; row++) {
            bool found = false;
            for (size_t index = 0; index < drawn_quad_count; index++) {
                const WmQuad *quad = &drawn_quads[index];
                if (quad->texture == art[4] &&
                    quad->y == 128.0f + 96.0f * row) {
                    assert(quad->x == 120.0f && quad->width == 400.0f);
                    assert(quad->height == 80.0f);
                    found = true;
                }
            }
            assert(found);
        }
        assert(count_glyphs_in_box(170.0f, 470.0f,
                                   150.0f, 190.0f) == 18);
        assert(count_glyphs_in_box(170.0f, 470.0f,
                                   246.0f, 286.0f) == 15);
        assert(count_drawn_texture(footer, NULL) == 1);
        assert(wm_settings_scene_hit(scene, 320, 165) ==
               WM_SETTINGS_CONTROL_ITEM_1);
        assert(wm_settings_scene_hit(scene, 320, 261) ==
               WM_SETTINGS_CONTROL_ITEM_2);
        assert(wm_settings_scene_hit(scene, 180, 405) ==
               WM_SETTINGS_CONTROL_BACK);
        assert(wm_settings_scene_hit(scene, 460, 405) ==
               WM_SETTINGS_CONTROL_NONE);

        assert(wm_settings_scene_hover(scene, WM_SETTINGS_CONTROL_ITEM_1));
        wm_settings_scene_advance(scene, 10.0f);
        drawn_quad_count = 0;
        wm_texture_cache_begin_frame(textures);
        wm_font_cache_begin_frame(fonts);
        assert(wm_settings_scene_draw(scene));
        WmQuad highlighted = {0};
        assert(count_drawn_texture(focus, &highlighted) == 1);
        assert(highlighted.x == 123.0f && highlighted.y == 133.0f);
        assert(highlighted.width == 394.0f && highlighted.height == 70.0f);

        assert(wm_settings_scene_activate(scene, WM_SETTINGS_CONTROL_BACK));
        wm_settings_scene_advance(scene, 20.0f);
        assert(wm_settings_scene_snapshot(scene).detail == 1);
    }
    wm_settings_scene_destroy(scene);
    wm_font_cache_destroy(fonts);
    wm_texture_cache_destroy(textures);
}

static void test_initial_connection_mode_pages(const char *assets) {
    WmTextureCache *textures = wm_texture_cache_create(
        (WmPlatform *)1, assets, 64u * 1024u * 1024u);
    WmFontCache *fonts = wm_font_cache_create(
        (WmPlatform *)1, assets, 16u * 1024u * 1024u);
    assert(textures && fonts);
    const char *paths[5] = {
        "textures/settings_html/large-row.png",
        "textures/settings_html/small-row.png",
        "textures/settings_html/small-row-focus.png",
        "textures/settings_html/aoss-icon.png",
        "textures/settings_html/footer-button.png"
    };
    uint32_t art[5] = {0};
    for (unsigned index = 0; index < 5; index++)
        assert(wm_texture_cache_resolve(textures, paths[index], &art[index]));
    WmSettingsScene *scene = wm_settings_scene_create(
        (WmPlatform *)1, assets, textures, fonts);
    assert(scene);
    assert(wm_settings_scene_open(scene));
    wm_settings_scene_advance(scene, 21.0f);
    assert(wm_settings_scene_activate(scene, WM_SETTINGS_CONTROL_NEXT));
    wm_settings_scene_advance(scene, 41.0f);
    assert(wm_settings_scene_activate(scene, WM_SETTINGS_CONTROL_ITEM_3));
    wm_settings_scene_advance(scene, 20.0f);
    assert(wm_settings_scene_activate(scene, WM_SETTINGS_CONTROL_ITEM_1));
    wm_settings_scene_advance(scene, 20.0f);
    assert(wm_settings_scene_activate(scene, WM_SETTINGS_CONTROL_ITEM_2));
    wm_settings_scene_advance(scene, 20.0f);
    assert(wm_settings_scene_snapshot(scene).connection_slot == 2);

    assert(wm_settings_scene_activate(scene, WM_SETTINGS_CONTROL_ITEM_1));
    wm_settings_scene_advance(scene, 20.0f);
    assert(wm_settings_scene_snapshot(scene).detail == 5);
    drawn_quad_count = 0;
    wm_texture_cache_begin_frame(textures);
    wm_font_cache_begin_frame(fonts);
    assert(wm_settings_scene_draw(scene));
    /* Wi_Fi_set_top.html: two full rows, two small rows and the AOSS icon. */
    assert(count_drawn_texture(art[0], NULL) == 2);
    assert(count_drawn_texture(art[1], NULL) == 2);
    for (unsigned row = 0; row < 2; row++) {
        bool full_found = false;
        bool small_found = false;
        for (size_t index = 0; index < drawn_quad_count; index++) {
            const WmQuad *quad = &drawn_quads[index];
            if (quad->texture == art[0] &&
                quad->x == 120.0f && quad->y == 80.0f + 96.0f * row &&
                quad->width == 400.0f && quad->height == 80.0f)
                full_found = true;
            if (quad->texture == art[1] &&
                quad->x == (row == 0 ? 120.0f : 355.0f) &&
                quad->y == 274.0f && quad->width == 168.0f &&
                quad->height == 76.0f)
                small_found = true;
        }
        assert(full_found && small_found);
    }
    WmQuad aoss = {0};
    assert(count_drawn_texture(art[3], &aoss) == 1);
    assert(aoss.x == 172.5f && aoss.y == 284.0f);
    assert(aoss.width == 63.0f && aoss.height == 56.0f);
    WmQuad back_footer = {0};
    assert(count_drawn_texture(art[4], &back_footer) == 1);
    assert(back_footer.x == 44.0f && back_footer.y == 371.0f);
    assert(count_glyphs_in_box(110.0f, 530.0f,
                               90.0f, 155.0f) == 22);
    assert(count_glyphs_in_box(340.0f, 530.0f,
                               285.0f, 340.0f) == 11);
    assert(wm_settings_scene_hit(scene, 320, 115) ==
           WM_SETTINGS_CONTROL_ITEM_1);
    assert(wm_settings_scene_hit(scene, 320, 210) ==
           WM_SETTINGS_CONTROL_ITEM_2);
    assert(wm_settings_scene_hit(scene, 200, 312) ==
           WM_SETTINGS_CONTROL_ITEM_3);
    assert(wm_settings_scene_hit(scene, 440, 312) ==
           WM_SETTINGS_CONTROL_ITEM_4);
    assert(wm_settings_scene_hit(scene, 180, 405) ==
           WM_SETTINGS_CONTROL_BACK);
    assert(wm_settings_scene_hover(scene, WM_SETTINGS_CONTROL_ITEM_4));
    wm_settings_scene_advance(scene, 10.0f);
    drawn_quad_count = 0;
    wm_texture_cache_begin_frame(textures);
    wm_font_cache_begin_frame(fonts);
    assert(wm_settings_scene_draw(scene));
    WmQuad focused = {0};
    assert(count_drawn_texture(art[2], &focused) == 1);
    assert(focused.x == 355.0f && focused.y == 277.0f);
    assert(focused.width == 168.0f && focused.height == 70.0f);
    assert(wm_settings_scene_activate(scene, WM_SETTINGS_CONTROL_BACK));
    wm_settings_scene_advance(scene, 20.0f);
    assert(wm_settings_scene_snapshot(scene).detail == 4);

    assert(wm_settings_scene_activate(scene, WM_SETTINGS_CONTROL_ITEM_2));
    wm_settings_scene_advance(scene, 20.0f);
    assert(wm_settings_scene_snapshot(scene).detail == 6);
    drawn_quad_count = 0;
    wm_texture_cache_begin_frame(textures);
    wm_font_cache_begin_frame(fonts);
    assert(wm_settings_scene_draw(scene));
    assert(count_drawn_texture(art[0], NULL) == 0);
    WmQuad ok_footer = {0};
    assert(count_drawn_texture(art[4], &ok_footer) == 1);
    assert(ok_footer.x == 324.0f && ok_footer.y == 371.0f);
    assert(count_glyphs_in_box(170.0f, 470.0f,
                               200.0f, 240.0f) >= 20);
    assert(wm_settings_scene_hit(scene, 180, 405) ==
           WM_SETTINGS_CONTROL_NONE);
    assert(wm_settings_scene_hit(scene, 460, 405) ==
           WM_SETTINGS_CONTROL_NEXT);
    assert(wm_settings_scene_activate(scene, WM_SETTINGS_CONTROL_NEXT));
    assert(wm_settings_scene_snapshot(scene).detail == 4);
    assert(wm_settings_scene_snapshot(scene).connection_slot == 2);

    wm_settings_scene_destroy(scene);
    wm_font_cache_destroy(fonts);
    wm_texture_cache_destroy(textures);
}

static void test_offline_access_point_search(const char *assets) {
    WmTextureCache *textures = wm_texture_cache_create(
        (WmPlatform *)1, assets, 64u * 1024u * 1024u);
    WmFontCache *fonts = wm_font_cache_create(
        (WmPlatform *)1, assets, 16u * 1024u * 1024u);
    assert(textures && fonts);
    uint32_t footer = 0;
    uint32_t title = 0;
    assert(wm_texture_cache_resolve(
        textures, "textures/settings_html/footer-button.png", &footer));
    assert(wm_texture_cache_resolve(
        textures, "textures/settings_html/title-tab.png", &title));
    WmSettingsScene *scene = wm_settings_scene_create(
        (WmPlatform *)1, assets, textures, fonts);
    assert(scene);
    assert(wm_settings_scene_open(scene));
    wm_settings_scene_advance(scene, 21.0f);
    assert(wm_settings_scene_activate(scene, WM_SETTINGS_CONTROL_NEXT));
    wm_settings_scene_advance(scene, 41.0f);
    assert(wm_settings_scene_activate(scene, WM_SETTINGS_CONTROL_ITEM_3));
    assert(wm_settings_scene_activate(scene, WM_SETTINGS_CONTROL_ITEM_1));
    assert(wm_settings_scene_activate(scene, WM_SETTINGS_CONTROL_ITEM_1));
    assert(wm_settings_scene_activate(scene, WM_SETTINGS_CONTROL_ITEM_1));
    assert(wm_settings_scene_activate(scene, WM_SETTINGS_CONTROL_ITEM_1));
    assert(wm_settings_scene_snapshot(scene).detail == 7);
    wm_settings_scene_advance(scene, 20.0f);
    drawn_quad_count = 0;
    wm_texture_cache_begin_frame(textures);
    wm_font_cache_begin_frame(fonts);
    assert(wm_settings_scene_draw(scene));
    WmQuad title_quad = {0};
    assert(count_drawn_texture(title, &title_quad) == 1);
    assert(title_quad.x == 136.0f && title_quad.y == 27.0f);
    assert(count_drawn_texture(footer, NULL) == 0);
    assert(count_glyphs_in_box(100.0f, 540.0f,
                               195.0f, 240.0f) >= 25);
    assert(wm_settings_scene_hit(scene, 180, 405) ==
           WM_SETTINGS_CONTROL_NONE);
    assert(wm_settings_scene_hit(scene, 460, 405) ==
           WM_SETTINGS_CONTROL_NONE);

    /* The maintained local bridge returns funcResult 2 on its one-second
     * poll for native AP search function 2. No host scan runs in C. */
    wm_settings_scene_advance(scene, 39.0f);
    assert(wm_settings_scene_snapshot(scene).detail == 7);
    wm_settings_scene_advance(scene, 1.0f);
    assert(wm_settings_scene_snapshot(scene).detail == 8);
    wm_settings_scene_advance(scene, 20.0f);
    drawn_quad_count = 0;
    wm_texture_cache_begin_frame(textures);
    wm_font_cache_begin_frame(fonts);
    assert(wm_settings_scene_draw(scene));
    WmQuad ok_footer = {0};
    assert(count_drawn_texture(footer, &ok_footer) == 1);
    assert(ok_footer.x == 324.0f && ok_footer.y == 371.0f);
    assert(count_glyphs_in_box(100.0f, 540.0f,
                               195.0f, 240.0f) >= 20);
    assert(wm_settings_scene_hit(scene, 180, 405) ==
           WM_SETTINGS_CONTROL_NONE);
    assert(wm_settings_scene_hit(scene, 460, 405) ==
           WM_SETTINGS_CONTROL_NEXT);
    assert(wm_settings_scene_activate(scene, WM_SETTINGS_CONTROL_NEXT));
    assert(wm_settings_scene_snapshot(scene).detail == 5);
    assert(wm_settings_scene_snapshot(scene).connection_slot == 1);
    assert(wm_settings_scene_activate(scene, WM_SETTINGS_CONTROL_ITEM_1));
    wm_settings_scene_advance(scene, 59.0f);
    assert(wm_settings_scene_snapshot(scene).detail == 7);

    wm_settings_scene_destroy(scene);
    wm_font_cache_destroy(fonts);
    wm_texture_cache_destroy(textures);
}

static void test_offline_usb_connector_pages(const char *assets) {
    WmTextureCache *textures = wm_texture_cache_create(
        (WmPlatform *)1, assets, 64u * 1024u * 1024u);
    WmFontCache *fonts = wm_font_cache_create(
        (WmPlatform *)1, assets, 16u * 1024u * 1024u);
    assert(textures && fonts);
    uint32_t footer = 0;
    uint32_t focus = 0;
    uint32_t title = 0;
    assert(wm_texture_cache_resolve(
        textures, "textures/settings_html/footer-button.png", &footer));
    assert(wm_texture_cache_resolve(
        textures, "textures/settings_html/footer-button-focus.png", &focus));
    assert(wm_texture_cache_resolve(
        textures, "textures/settings_html/title-tab.png", &title));
    WmSettingsScene *scene = wm_settings_scene_create(
        (WmPlatform *)1, assets, textures, fonts);
    assert(scene);
    assert(wm_settings_scene_open(scene));
    wm_settings_scene_advance(scene, 21.0f);
    assert(wm_settings_scene_activate(scene, WM_SETTINGS_CONTROL_NEXT));
    wm_settings_scene_advance(scene, 41.0f);
    assert(wm_settings_scene_activate(scene, WM_SETTINGS_CONTROL_ITEM_3));
    assert(wm_settings_scene_activate(scene, WM_SETTINGS_CONTROL_ITEM_1));
    assert(wm_settings_scene_activate(scene, WM_SETTINGS_CONTROL_ITEM_2));
    assert(wm_settings_scene_activate(scene, WM_SETTINGS_CONTROL_ITEM_1));
    assert(wm_settings_scene_activate(scene, WM_SETTINGS_CONTROL_ITEM_2));
    assert(wm_settings_scene_snapshot(scene).detail == 9);
    assert(wm_settings_scene_snapshot(scene).connection_slot == 2);
    wm_settings_scene_advance(scene, 20.0f);
    drawn_quad_count = 0;
    wm_texture_cache_begin_frame(textures);
    wm_font_cache_begin_frame(fonts);
    assert(wm_settings_scene_draw(scene));
    WmQuad title_quad = {0};
    assert(count_drawn_texture(title, &title_quad) == 1);
    assert(title_quad.x == 136.0f && title_quad.y == 27.0f);
    assert(count_drawn_texture(footer, NULL) == 2);
    assert(count_glyphs_in_box(100.0f, 540.0f,
                               120.0f, 305.0f) >= 100);
    assert(wm_settings_scene_hit(scene, 180, 405) ==
           WM_SETTINGS_CONTROL_BACK);
    assert(wm_settings_scene_hit(scene, 460, 405) ==
           WM_SETTINGS_CONTROL_NEXT);
    assert(wm_settings_scene_hover(scene, WM_SETTINGS_CONTROL_NEXT));
    wm_settings_scene_advance(scene, 10.0f);
    drawn_quad_count = 0;
    wm_texture_cache_begin_frame(textures);
    wm_font_cache_begin_frame(fonts);
    assert(wm_settings_scene_draw(scene));
    WmQuad next_focus = {0};
    assert(count_drawn_texture(focus, &next_focus) == 1);
    assert(next_focus.x == 324.0f && next_focus.y == 371.0f);
    assert(next_focus.color.a == 1.0f);

    assert(wm_settings_scene_activate(scene, WM_SETTINGS_CONTROL_NEXT));
    assert(wm_settings_scene_snapshot(scene).detail == 10);
    wm_settings_scene_advance(scene, 20.0f);
    drawn_quad_count = 0;
    wm_texture_cache_begin_frame(textures);
    wm_font_cache_begin_frame(fonts);
    assert(wm_settings_scene_draw(scene));
    WmQuad cancel_footer = {0};
    assert(count_drawn_texture(footer, &cancel_footer) == 1);
    assert(cancel_footer.x == 44.0f && cancel_footer.y == 371.0f);
    assert(count_glyphs_in_box(100.0f, 540.0f,
                               150.0f, 275.0f) >= 75);
    assert(wm_settings_scene_hit(scene, 180, 405) ==
           WM_SETTINGS_CONTROL_BACK);
    assert(wm_settings_scene_hit(scene, 460, 405) ==
           WM_SETTINGS_CONTROL_NONE);
    /* Common0202 polls the bridge's dummy function 30 result after 1 s. */
    wm_settings_scene_advance(scene, 39.0f);
    assert(wm_settings_scene_snapshot(scene).detail == 10);
    wm_settings_scene_advance(scene, 1.0f);
    assert(wm_settings_scene_snapshot(scene).detail == 11);
    wm_settings_scene_advance(scene, 20.0f);
    drawn_quad_count = 0;
    wm_texture_cache_begin_frame(textures);
    wm_font_cache_begin_frame(fonts);
    assert(wm_settings_scene_draw(scene));
    assert(count_drawn_texture(footer, NULL) == 2);
    assert(count_glyphs_in_box(100.0f, 540.0f,
                               120.0f, 305.0f) >= 100);
    assert(wm_settings_scene_hit(scene, 180, 405) ==
           WM_SETTINGS_CONTROL_BACK);
    assert(wm_settings_scene_hit(scene, 460, 405) ==
           WM_SETTINGS_CONTROL_NEXT);
    /* Visible left Yes retries the wait, despite the swapped source IDs. */
    assert(wm_settings_scene_activate(scene, WM_SETTINGS_CONTROL_BACK));
    assert(wm_settings_scene_snapshot(scene).detail == 10);
    wm_settings_scene_advance(scene, 59.0f);
    assert(wm_settings_scene_snapshot(scene).detail == 10);
    assert(wm_settings_scene_activate(scene, WM_SETTINGS_CONTROL_BACK));
    assert(wm_settings_scene_snapshot(scene).detail == 9);
    assert(wm_settings_scene_activate(scene, WM_SETTINGS_CONTROL_BACK));
    assert(wm_settings_scene_snapshot(scene).detail == 5);
    assert(wm_settings_scene_activate(scene, WM_SETTINGS_CONTROL_ITEM_2));
    assert(wm_settings_scene_activate(scene, WM_SETTINGS_CONTROL_NEXT));
    wm_settings_scene_advance(scene, 60.0f);
    assert(wm_settings_scene_snapshot(scene).detail == 11);
    /* The local No substitute cannot report an unobserved USB success. */
    assert(wm_settings_scene_activate(scene, WM_SETTINGS_CONTROL_NEXT));
    assert(wm_settings_scene_snapshot(scene).detail == 5);
    assert(wm_settings_scene_snapshot(scene).connection_slot == 2);

    wm_settings_scene_destroy(scene);
    wm_font_cache_destroy(fonts);
    wm_texture_cache_destroy(textures);
}

static void test_user_agreements_button_order(const char *assets) {
    WmTextureCache *textures = wm_texture_cache_create(
        (WmPlatform *)1, assets, 64u * 1024u * 1024u);
    WmFontCache *fonts = wm_font_cache_create(
        (WmPlatform *)1, assets, 16u * 1024u * 1024u);
    assert(textures && fonts);
    uint32_t footer = 0;
    assert(wm_texture_cache_resolve(
        textures, "textures/settings_html/footer-button.png", &footer));
    WmSettingsScene *scene = wm_settings_scene_create(
        (WmPlatform *)1, assets, textures, fonts);
    assert(scene);
    assert(wm_settings_scene_open(scene));
    wm_settings_scene_advance(scene, 21.0f);
    assert(wm_settings_scene_activate(scene, WM_SETTINGS_CONTROL_NEXT));
    wm_settings_scene_advance(scene, 41.0f);
    assert(wm_settings_scene_activate(scene, WM_SETTINGS_CONTROL_ITEM_3));
    wm_settings_scene_advance(scene, 20.0f);
    assert(wm_settings_scene_activate(scene, WM_SETTINGS_CONTROL_ITEM_3));
    wm_settings_scene_advance(scene, 20.0f);
    assert(wm_settings_scene_snapshot(scene).detail == 3);

    drawn_quad_count = 0;
    wm_texture_cache_begin_frame(textures);
    wm_font_cache_begin_frame(fonts);
    assert(wm_settings_scene_draw(scene));
    assert(count_drawn_texture(footer, NULL) == 2);
    /* Extracted ENG Internet/EULA_index.html: Yes is UnderL, No is UnderR. */
    assert(count_glyphs_in_box(120.0f, 230.0f, 390.0f, 425.0f) == 3);
    assert(count_glyphs_in_box(430.0f, 520.0f, 390.0f, 425.0f) == 2);
    assert(wm_settings_scene_hit(scene, 180, 405) ==
           WM_SETTINGS_CONTROL_BACK);
    assert(wm_settings_scene_hit(scene, 460, 405) ==
           WM_SETTINGS_CONTROL_NEXT);

    assert(wm_settings_scene_activate(scene, WM_SETTINGS_CONTROL_NEXT));
    assert(!wm_settings_scene_snapshot(scene).internet_agreement);
    assert(wm_settings_scene_snapshot(scene).detail == 0);
    assert(wm_settings_scene_activate(scene, WM_SETTINGS_CONTROL_ITEM_3));
    assert(wm_settings_scene_activate(scene, WM_SETTINGS_CONTROL_BACK));
    assert(wm_settings_scene_snapshot(scene).internet_agreement);
    assert(wm_settings_scene_snapshot(scene).detail == 0);

    wm_settings_scene_destroy(scene);
    wm_font_cache_destroy(fonts);
    wm_texture_cache_destroy(textures);
}

static void test_resolution_source_default(const char *assets) {
    WmTextureCache *textures = wm_texture_cache_create(
        (WmPlatform *)1, assets, 64 * 1024 * 1024);
    WmFontCache *fonts = wm_font_cache_create(
        (WmPlatform *)1, assets, 16 * 1024 * 1024);
    assert(textures && fonts);
    uint32_t left_flame = 0;
    uint32_t right_flame = 0;
    assert(wm_texture_cache_resolve(
        textures, "textures/settings_html/choice-left.png",
        &left_flame));
    assert(wm_texture_cache_resolve(
        textures, "textures/settings_html/choice-right.png",
        &right_flame));

    WmSettingsScene *scene = wm_settings_scene_create(
        (WmPlatform *)1, assets, textures, fonts);
    assert(scene);
    assert(wm_settings_scene_open(scene));
    wm_settings_scene_advance(scene, 21.0f);
    assert(wm_settings_scene_activate(scene, WM_SETTINGS_CONTROL_ITEM_3));
    assert(wm_settings_scene_activate(scene, WM_SETTINGS_CONTROL_ITEM_3));
    assert(wm_settings_scene_snapshot(scene).detail == 3);
    assert(wm_settings_scene_snapshot(scene).selection == 0);
    wm_settings_scene_advance(scene, 20.0f);

    drawn_quad_count = 0;
    wm_texture_cache_begin_frame(textures);
    wm_font_cache_begin_frame(fonts);
    assert(wm_settings_scene_draw(scene));
    WmQuad left = {0};
    WmQuad right = {0};
    assert(count_drawn_texture(left_flame, &left) == 1);
    assert(count_drawn_texture(right_flame, &right) == 1);
    assert(left.x == 110.0f && left.y == 120.0f);
    assert(right.x == 506.0f && right.y == 120.0f);
    assert(left.height == 96.0f && right.height == 96.0f);

    assert(wm_settings_scene_activate(scene, WM_SETTINGS_CONTROL_ITEM_2));
    assert(wm_settings_scene_snapshot(scene).selection == 1);
    drawn_quad_count = 0;
    wm_texture_cache_begin_frame(textures);
    wm_font_cache_begin_frame(fonts);
    assert(wm_settings_scene_draw(scene));
    assert(count_drawn_texture(left_flame, &left) == 1);
    assert(count_drawn_texture(right_flame, &right) == 1);
    assert(left.y == 216.0f && right.y == 216.0f);

    assert(wm_settings_scene_back(scene));
    assert(wm_settings_scene_activate(scene, WM_SETTINGS_CONTROL_ITEM_3));
    /* The extracted Progressive_set controller writes the selected value
     * before Back, so reopening retains Standard TV (480i). */
    assert(wm_settings_scene_snapshot(scene).selection == 1);
    wm_settings_scene_destroy(scene);
    wm_font_cache_destroy(fonts);
    wm_texture_cache_destroy(textures);
}

static void test_language_selection_page(const char *assets) {
    WmTextureCache *textures = wm_texture_cache_create(
        (WmPlatform *)1, assets, 64u * 1024u * 1024u);
    WmFontCache *fonts = wm_font_cache_create(
        (WmPlatform *)1, assets, 16u * 1024u * 1024u);
    assert(textures && fonts);
    uint32_t row = 0;
    uint32_t focus = 0;
    uint32_t left = 0;
    uint32_t right = 0;
    assert(wm_texture_cache_resolve(
        textures, "textures/settings_html/large-row.png", &row));
    assert(wm_texture_cache_resolve(
        textures, "textures/settings_html/large-row-focus.png",
        &focus));
    assert(wm_texture_cache_resolve(
        textures, "textures/settings_html/choice-left.png", &left));
    assert(wm_texture_cache_resolve(
        textures, "textures/settings_html/choice-right.png", &right));

    WmSettingsScene *scene = wm_settings_scene_create(
        (WmPlatform *)1, assets, textures, fonts);
    assert(scene);
    assert(wm_settings_scene_open(scene));
    wm_settings_scene_advance(scene, 21.0f);
    for (unsigned page = 1; page < 3; page++) {
        assert(wm_settings_scene_activate(scene, WM_SETTINGS_CONTROL_NEXT));
        wm_settings_scene_advance(scene, 40.0f);
    }
    assert(wm_settings_scene_activate(scene, WM_SETTINGS_CONTROL_ITEM_1));
    wm_settings_scene_advance(scene, 20.0f);
    assert(wm_settings_scene_snapshot(scene).category == 9);
    assert(wm_settings_scene_snapshot(scene).selection == 0);
    assert(wm_settings_scene_hit(scene, 16 + 300, 181) ==
           WM_SETTINGS_CONTROL_ITEM_2);

    drawn_quad_count = 0;
    wm_texture_cache_begin_frame(textures);
    wm_font_cache_begin_frame(fonts);
    assert(wm_settings_scene_draw(scene));
    WmQuad first_row = {0};
    WmQuad selected_left = {0};
    WmQuad selected_right = {0};
    assert(count_drawn_texture(row, &first_row) == 3);
    assert(first_row.x == 120.0f && first_row.y == 80.0f);
    assert(first_row.width == 400.0f && first_row.height == 80.0f);
    assert(count_drawn_texture(left, &selected_left) == 1);
    assert(count_drawn_texture(right, &selected_right) == 1);
    assert(selected_left.x == 110.0f && selected_left.y == 72.0f);
    assert(selected_right.x == 506.0f && selected_right.y == 72.0f);
    assert(selected_left.width == 24.0f && selected_left.height == 96.0f);

    assert(wm_settings_scene_hover(scene, WM_SETTINGS_CONTROL_ITEM_2));
    wm_settings_scene_advance(scene, 10.0f);
    drawn_quad_count = 0;
    wm_texture_cache_begin_frame(textures);
    wm_font_cache_begin_frame(fonts);
    assert(wm_settings_scene_draw(scene));
    WmQuad focused_row = {0};
    assert(count_drawn_texture(focus, &focused_row) == 1);
    assert(focused_row.x == 123.0f && focused_row.y == 181.0f);
    assert(focused_row.width == 394.0f && focused_row.height == 70.0f);
    assert(focused_row.color.a == 1.0f);

    assert(wm_settings_scene_activate(scene, WM_SETTINGS_CONTROL_ITEM_2));
    assert(wm_settings_scene_snapshot(scene).selection == 1);
    assert(wm_settings_scene_snapshot(scene).page_opacity == 0.0f);
    wm_settings_scene_advance(scene, 10.0f);
    drawn_quad_count = 0;
    wm_texture_cache_begin_frame(textures);
    wm_font_cache_begin_frame(fonts);
    assert(wm_settings_scene_draw(scene));
    assert(count_drawn_texture(left, NULL) == 2);
    bool old_marker = false;
    bool new_marker = false;
    for (size_t index = 0; index < drawn_quad_count; index++) {
        const WmQuad *quad = &drawn_quads[index];
        if (quad->texture != left) continue;
        if (quad->y == 72.0f && quad->color.a == 1.0f)
            old_marker = true;
        if (quad->y == 168.0f &&
            fabsf(quad->color.a - 127.0f / 255.0f) < 0.001f)
            new_marker = true;
    }
    assert(old_marker && new_marker);

    wm_settings_scene_advance(scene, 10.0f);
    drawn_quad_count = 0;
    wm_texture_cache_begin_frame(textures);
    wm_font_cache_begin_frame(fonts);
    assert(wm_settings_scene_draw(scene));
    assert(count_drawn_texture(left, &selected_left) == 1);
    assert(selected_left.y == 168.0f);

    /* Back returns to the saved English page. Confirm commits the staged
     * language, which is selected again when reopening the category. */
    assert(wm_settings_scene_back(scene));
    wm_settings_scene_advance(scene, 20.0f);
    assert(wm_settings_scene_activate(scene, WM_SETTINGS_CONTROL_ITEM_1));
    assert(wm_settings_scene_snapshot(scene).selection == 0);
    wm_settings_scene_advance(scene, 20.0f);
    assert(wm_settings_scene_activate(scene, WM_SETTINGS_CONTROL_ITEM_3));
    wm_settings_scene_advance(scene, 20.0f);
    assert(wm_settings_scene_snapshot(scene).selection == 2);
    assert(wm_settings_scene_activate(scene, WM_SETTINGS_CONTROL_NEXT));
    wm_settings_scene_advance(scene, 20.0f);
    assert(wm_settings_scene_snapshot(scene).category == 0);
    assert(wm_settings_scene_activate(scene, WM_SETTINGS_CONTROL_ITEM_1));
    wm_settings_scene_advance(scene, 20.0f);
    assert(wm_settings_scene_snapshot(scene).selection == 2);
    drawn_quad_count = 0;
    wm_texture_cache_begin_frame(textures);
    wm_font_cache_begin_frame(fonts);
    assert(wm_settings_scene_draw(scene));
    assert(count_drawn_texture(left, &selected_left) == 1);
    assert(selected_left.y == 264.0f);

    wm_settings_scene_destroy(scene);
    wm_font_cache_destroy(fonts);
    wm_texture_cache_destroy(textures);
}

static void test_screen_language_artwork(const char *assets) {
    WmTextureCache *textures = wm_texture_cache_create(
        (WmPlatform *)1, assets, 64u * 1024u * 1024u);
    WmFontCache *fonts = wm_font_cache_create(
        (WmPlatform *)1, assets, 16u * 1024u * 1024u);
    assert(textures && fonts);
    WmSettingsScene *scene = wm_settings_scene_create(
        (WmPlatform *)1, assets, textures, fonts);
    assert(scene);
    assert(wm_settings_scene_open(scene));
    wm_settings_scene_advance(scene, 21.0f);

    /* The source Display_index first row is Screen Position in English. */
    assert(wm_settings_scene_activate(scene, WM_SETTINGS_CONTROL_ITEM_3));
    wm_settings_scene_advance(scene, 20.0f);
    drawn_quad_count = 0;
    wm_texture_cache_begin_frame(textures);
    wm_font_cache_begin_frame(fonts);
    assert(wm_settings_scene_draw(scene));
    assert(count_row_glyphs(90.0f, 130.0f) == 14);
    assert(wm_settings_scene_back(scene));

    for (unsigned page = 1; page < 3; page++) {
        assert(wm_settings_scene_activate(scene, WM_SETTINGS_CONTROL_NEXT));
        wm_settings_scene_advance(scene, 41.0f);
    }
    assert(wm_settings_scene_activate(scene, WM_SETTINGS_CONTROL_ITEM_1));
    assert(wm_settings_scene_activate(scene, WM_SETTINGS_CONTROL_ITEM_2));
    assert(wm_settings_scene_activate(scene, WM_SETTINGS_CONTROL_NEXT));
    wm_settings_scene_advance(scene, 20.0f);
    for (unsigned page = 3; page > 1; page--) {
        assert(wm_settings_scene_activate(scene,
                                          WM_SETTINGS_CONTROL_PREVIOUS));
        wm_settings_scene_advance(scene, 41.0f);
    }
    assert(wm_settings_scene_activate(scene, WM_SETTINGS_CONTROL_ITEM_3));
    wm_settings_scene_advance(scene, 20.0f);
    drawn_quad_count = 0;
    wm_texture_cache_begin_frame(textures);
    wm_font_cache_begin_frame(fonts);
    assert(wm_settings_scene_draw(scene));
    /* FRA Display_index: Position de l'écran. */
    assert(count_row_glyphs(90.0f, 130.0f) == 17);
    assert(wm_settings_scene_activate(scene, WM_SETTINGS_CONTROL_ITEM_2));
    wm_settings_scene_advance(scene, 20.0f);
    drawn_quad_count = 0;
    wm_texture_cache_begin_frame(textures);
    wm_font_cache_begin_frame(fonts);
    assert(wm_settings_scene_draw(scene));
    /* FRA Wide_set: 4:3 and 16:9, without the English TV names. */
    assert(count_row_glyphs(200.0f, 240.0f) == 7);
    assert(wm_settings_scene_back(scene));
    assert(wm_settings_scene_back(scene));

    for (unsigned page = 1; page < 3; page++) {
        assert(wm_settings_scene_activate(scene, WM_SETTINGS_CONTROL_NEXT));
        wm_settings_scene_advance(scene, 41.0f);
    }
    assert(wm_settings_scene_activate(scene, WM_SETTINGS_CONTROL_ITEM_1));
    assert(wm_settings_scene_activate(scene, WM_SETTINGS_CONTROL_ITEM_3));
    assert(wm_settings_scene_activate(scene, WM_SETTINGS_CONTROL_NEXT));
    wm_settings_scene_advance(scene, 20.0f);
    for (unsigned page = 3; page > 1; page--) {
        assert(wm_settings_scene_activate(scene,
                                          WM_SETTINGS_CONTROL_PREVIOUS));
        wm_settings_scene_advance(scene, 41.0f);
    }
    assert(wm_settings_scene_activate(scene, WM_SETTINGS_CONTROL_ITEM_3));
    wm_settings_scene_advance(scene, 20.0f);
    drawn_quad_count = 0;
    wm_texture_cache_begin_frame(textures);
    wm_font_cache_begin_frame(fonts);
    assert(wm_settings_scene_draw(scene));
    /* SPA Display_index: Posición de la pantalla. */
    assert(count_row_glyphs(90.0f, 130.0f) == 20);

    wm_settings_scene_destroy(scene);
    wm_font_cache_destroy(fonts);
    wm_texture_cache_destroy(textures);
}

static void test_source_hit_regions(void) {
    assert(wm_settings_index_hit(1, 320, 228) ==
           WM_SETTINGS_CONTROL_ITEM_3);
    assert(wm_settings_index_hit(1, 130, 78) ==
           WM_SETTINGS_CONTROL_ITEM_1);
    assert(wm_settings_index_hit(1, 130, 138) ==
           WM_SETTINGS_CONTROL_NONE);
    assert(wm_settings_index_hit(1, 568, 220) ==
           WM_SETTINGS_CONTROL_NEXT);
    assert(wm_settings_index_hit(1, 54, 220) ==
           WM_SETTINGS_CONTROL_NONE);
    assert(wm_settings_index_hit(2, 54, 220) ==
           WM_SETTINGS_CONTROL_PREVIOUS);
    assert(wm_settings_index_hit(3, 568, 220) ==
           WM_SETTINGS_CONTROL_NONE);
    assert(wm_settings_index_hit(3, 80, 400) ==
           WM_SETTINGS_CONTROL_BACK);
    assert(wm_settings_index_hit(0, 320, 228) ==
           WM_SETTINGS_CONTROL_NONE);
    assert(wm_settings_index_hit(4, 320, 228) ==
           WM_SETTINGS_CONTROL_NONE);
    assert(strcmp(wm_settings_category_label(1),
                  "Console Nickname") == 0);
    assert(strcmp(wm_settings_category_label(12),
                  "Format Wii System Memory") == 0);
    assert(wm_settings_category_label(13) == NULL);
}

static void test_index_navigation(const char *assets) {
    WmSettingsScene *scene = wm_settings_scene_create(
        (WmPlatform *)1, assets, (WmTextureCache *)1,
        (WmFontCache *)1);
    assert(scene);
    assert(wm_settings_scene_open(scene));
    assert(wm_settings_scene_snapshot(scene).page == 1);
    assert(wm_settings_scene_snapshot(scene).phase == WM_SETTINGS_APPEAR);
    wm_settings_scene_advance(scene, 1.0f);
    assert(wm_settings_scene_snapshot(scene).phase_frame == 0.0f);
    assert(wm_settings_scene_hit(scene, 320, 228) ==
           WM_SETTINGS_CONTROL_NONE);
    wm_settings_scene_advance(scene, 20.0f);
    assert(wm_settings_scene_snapshot(scene).phase == WM_SETTINGS_READY);
    assert(wm_settings_scene_hover(scene, WM_SETTINGS_CONTROL_NEXT));
    assert(wm_settings_scene_activate(scene, WM_SETTINGS_CONTROL_NEXT));
    assert(wm_settings_scene_snapshot(scene).page == 2);
    assert(wm_settings_scene_snapshot(scene).phase == WM_SETTINGS_SCROLL);
    assert(!wm_settings_scene_activate(scene, WM_SETTINGS_CONTROL_ITEM_1));
    wm_settings_scene_advance(scene, 39.0f);
    assert(wm_settings_scene_snapshot(scene).phase == WM_SETTINGS_SCROLL);
    wm_settings_scene_advance(scene, 1.0f);
    assert(wm_settings_scene_snapshot(scene).phase == WM_SETTINGS_READY);
    assert(wm_settings_scene_activate(scene, WM_SETTINGS_CONTROL_ITEM_2));
    assert(wm_settings_scene_snapshot(scene).category == 6);
    assert(wm_settings_scene_take_category(scene) == 0);
    assert(wm_settings_scene_hit(scene, 140, 150) ==
           WM_SETTINGS_CONTROL_ITEM_1);
    assert(wm_settings_scene_hit(scene, 140, 245) ==
           WM_SETTINGS_CONTROL_ITEM_2);
    assert(wm_settings_scene_activate(scene, WM_SETTINGS_CONTROL_ITEM_1));
    assert(wm_settings_scene_snapshot(scene).detail == 1);
    assert(wm_settings_scene_activate(scene, WM_SETTINGS_CONTROL_ITEM_2));
    assert(wm_settings_scene_snapshot(scene).selection == 1);
    assert(wm_settings_scene_activate(scene, WM_SETTINGS_CONTROL_NEXT));
    assert(wm_settings_scene_snapshot(scene).detail == 0);
    assert(wm_settings_scene_back(scene));
    assert(wm_settings_scene_snapshot(scene).category == 0);
    assert(wm_settings_scene_activate(scene, WM_SETTINGS_CONTROL_PREVIOUS));
    wm_settings_scene_advance(scene, 40.0f);
    assert(wm_settings_scene_snapshot(scene).page == 1);
    assert(wm_settings_scene_activate(scene, WM_SETTINGS_CONTROL_ITEM_1));
    assert(wm_settings_scene_snapshot(scene).category == 1);
    assert(strcmp(wm_settings_scene_snapshot(scene).nickname, "Wii") == 0);
    assert(wm_settings_scene_take_category(scene) == 0);
    assert(wm_settings_scene_back(scene));
    assert(wm_settings_scene_back(scene));
    assert(wm_settings_scene_take_exit(scene));
    assert(!wm_settings_scene_take_exit(scene));
    wm_settings_scene_reset(scene);
    assert(wm_settings_scene_snapshot(scene).phase == WM_SETTINGS_CLOSED);
    assert(wm_settings_scene_take_category(scene) == 0);
    assert(!wm_settings_scene_take_exit(scene));
    assert(wm_settings_scene_open(scene));
    assert(wm_settings_scene_snapshot(scene).page == 1);
    wm_settings_scene_destroy(scene);
}

static void test_index_source_badges_and_scroll(const char *assets) {
    WmTextureCache *textures = wm_texture_cache_create(
        (WmPlatform *)1, assets, 64u * 1024u * 1024u);
    WmFontCache *fonts = wm_font_cache_create(
        (WmPlatform *)1, assets, 16u * 1024u * 1024u);
    assert(textures && fonts);
    uint32_t background = 0;
    uint32_t title = 0;
    uint32_t footer = 0;
    uint32_t page_on = 0;
    uint32_t page_off = 0;
    uint32_t side_panel = 0;
    assert(wm_texture_cache_resolve(
        textures, "textures/settings_html/background.png", &background));
    assert(wm_texture_cache_resolve(
        textures, "textures/settings_html/title-tab.png", &title));
    assert(wm_texture_cache_resolve(
        textures, "textures/settings_html/footer-button.png", &footer));
    assert(wm_texture_cache_resolve(
        textures, "textures/settings_html/page-on.png", &page_on));
    assert(wm_texture_cache_resolve(
        textures, "textures/settings_html/page-off.png", &page_off));
    assert(wm_texture_cache_resolve(
        textures, "textures/settings_html/side-panel.png", &side_panel));
    WmSettingsScene *scene = wm_settings_scene_create(
        (WmPlatform *)1, assets, textures, fonts);
    assert(scene);
    assert(wm_settings_scene_open(scene));
    wm_settings_scene_advance(scene, 21.0f);
    drawn_quad_count = 0;
    wm_texture_cache_begin_frame(textures);
    wm_font_cache_begin_frame(fonts);
    assert(wm_settings_scene_draw(scene));
    WmQuad selected_badge = {0};
    assert(count_drawn_texture(page_on, &selected_badge) == 1);
    assert(selected_badge.x == 456.0f && selected_badge.y == 376.0f);
    assert(count_drawn_texture(page_off, NULL) == 2);
    /* The extracted index title box begins at y=27. The local outline face
     * puts its first ink pixels just below that box, as the source does. */
    float title_ink_top = INFINITY;
    for (size_t index = 0; index < drawn_quad_count; index++) {
        const WmQuad *quad = &drawn_quads[index];
        if (!quad->texture || quad->x < 48.0f || quad->x >= 340.0f ||
            quad->y < 26.0f || quad->y >= 63.0f ||
            quad->width >= 30.0f || quad->height >= 30.0f)
            continue;
        title_ink_top = fminf(title_ink_top, quad->y);
    }
    assert(title_ink_top >= 27.0f && title_ink_top <= 34.0f);
    /* The index HTML uses List.css's 24 px bold numerals in 40×32 badges. */
    for (unsigned badge = 0; badge < 3; badge++) {
        float left = 456.0f + 48.0f * badge;
        size_t glyphs = 0;
        for (size_t index = 0; index < drawn_quad_count; index++) {
            const WmQuad *quad = &drawn_quads[index];
            if (!quad->texture || quad->x < left ||
                quad->x >= left + 40.0f || quad->y < 376.0f ||
                quad->y >= 408.0f || quad->height > 30.0f)
                continue;
            assert(quad->height >= 16.0f);
            glyphs++;
        }
        assert(glyphs == 1);
    }

    assert(wm_settings_scene_activate(scene, WM_SETTINGS_CONTROL_NEXT));
    wm_settings_scene_advance(scene, 13.0f);
    drawn_quad_count = 0;
    wm_texture_cache_begin_frame(textures);
    wm_font_cache_begin_frame(fonts);
    assert(wm_settings_scene_draw(scene));
    float backgrounds[2] = {0};
    float background_alpha[2] = {0};
    float titles[2] = {0};
    float footers[2] = {0};
    unsigned background_count = 0;
    unsigned title_count = 0;
    unsigned footer_count = 0;
    for (size_t index = 0; index < drawn_quad_count; index++) {
        const WmQuad *quad = &drawn_quads[index];
        if (quad->texture == background && background_count < 2) {
            background_alpha[background_count] = quad->color.a;
            backgrounds[background_count++] = quad->x;
        }
        if (quad->texture == title && title_count < 2)
            titles[title_count++] = quad->x;
        if (quad->texture == footer && footer_count < 2)
            footers[footer_count++] = quad->x;
    }
    assert(background_count == 2 && title_count == 2 && footer_count == 2);
    assert(background_alpha[0] == 1.0f);
    assert(background_alpha[1] > 0.0f && background_alpha[1] < 1.0f);
    assert(backgrounds[0] < 16.0f && backgrounds[1] > 16.0f);
    /* SceenChange_b_Right's frame-13 N_Tra0 key is -443.8 of 477 units. */
    float source_shift = 443.8f * 608.0f / 477.0f;
    assert(fabsf(backgrounds[0] - (16.0f - source_shift)) < 0.1f);
    assert(fabsf(backgrounds[1] - backgrounds[0] - 608.0f) < 0.01f);
    for (unsigned page = 0; page < 2; page++) {
        assert(fabsf(titles[page] - backgrounds[page] - 24.0f) < 0.01f);
        assert(fabsf(footers[page] - backgrounds[page] - 28.0f) < 0.01f);
    }
    wm_settings_scene_advance(scene, 12.0f);
    drawn_quad_count = 0;
    wm_texture_cache_begin_frame(textures);
    wm_font_cache_begin_frame(fonts);
    assert(wm_settings_scene_draw(scene));
    WmQuad first_background = {0};
    assert(count_drawn_texture(background, &first_background) == 2);
    assert(first_background.x == -592.0f);
    wm_settings_scene_advance(scene, 15.0f);
    drawn_quad_count = 0;
    wm_texture_cache_begin_frame(textures);
    wm_font_cache_begin_frame(fonts);
    assert(wm_settings_scene_draw(scene));
    assert(wm_settings_scene_snapshot(scene).phase == WM_SETTINGS_READY);
    assert(count_drawn_texture(background, &first_background) == 1);
    assert(first_background.x == 16.0f);
    assert(count_drawn_texture(page_on, &selected_badge) == 1);
    assert(selected_badge.x == 504.0f);

    wm_settings_scene_set_wide(scene, true);
    WmSettingsProjection projection = wm_settings_scene_projection(scene);
    assert(wm_settings_scene_activate(scene, WM_SETTINGS_CONTROL_PREVIOUS));
    wm_settings_scene_advance(scene, 13.0f);
    drawn_quad_count = 0;
    document_clip_seen = false;
    wm_texture_cache_begin_frame(textures);
    wm_font_cache_begin_frame(fonts);
    assert(wm_settings_scene_draw(scene));
    assert(document_clip_seen);
    assert(document_clip.x == 0.0f && document_clip.width == WM_FRAME_WIDTH);
    WmQuad panel[4] = {0};
    unsigned panel_count = 0;
    for (size_t index = 0; index < drawn_quad_count; index++) {
        if (drawn_quads[index].texture == side_panel && panel_count < 4)
            panel[panel_count++] = drawn_quads[index];
    }
    assert(panel_count == 4);
    background_count = 0;
    for (size_t index = 0; index < drawn_quad_count; index++)
        if (drawn_quads[index].texture == background && background_count < 2) {
            background_alpha[background_count] = drawn_quads[index].color.a;
            backgrounds[background_count++] = drawn_quads[index].x;
        }
    assert(background_count == 2);
    assert(background_alpha[0] == 1.0f);
    assert(background_alpha[1] > 0.0f && background_alpha[1] < 1.0f);
    assert(backgrounds[0] > projection.document_x);
    assert(backgrounds[1] < projection.document_x);
    /* The exported 41-frame clip is at 443.8/477 of its full displacement
     * on frame 13; each complete widescreen composition travels 640 pixels. */
    float wide_shift = 443.8f / 477.0f * WM_FRAME_WIDTH;
    assert(fabsf(panel[0].x - wide_shift) < 0.1f);
    assert(fabsf(panel[1].x - panel[0].x -
                 (projection.document_x + projection.document_width)) < 0.01f);
    assert(fabsf(panel[2].x - panel[0].x + WM_FRAME_WIDTH) < 0.01f);
    assert(fabsf(panel[3].x - panel[2].x -
                 (projection.document_x + projection.document_width)) < 0.01f);
    assert(fabsf(backgrounds[0] - backgrounds[1] -
                 WM_FRAME_WIDTH) < 0.01f);

    wm_settings_scene_destroy(scene);
    wm_font_cache_destroy(fonts);
    wm_texture_cache_destroy(textures);
}

static void test_immediate_index_hover_art(const char *assets) {
    WmTextureCache *textures = wm_texture_cache_create(
        (WmPlatform *)1, assets, 64u * 1024u * 1024u);
    WmFontCache *fonts = wm_font_cache_create(
        (WmPlatform *)1, assets, 16u * 1024u * 1024u);
    assert(textures && fonts);
    uint32_t row_focus = 0;
    uint32_t footer_focus = 0;
    uint32_t right_focus = 0;
    uint32_t left_focus = 0;
    assert(wm_texture_cache_resolve(
        textures, "textures/settings_html/index-row-focus.png",
        &row_focus));
    assert(wm_texture_cache_resolve(
        textures, "textures/settings_html/footer-button-focus.png",
        &footer_focus));
    assert(wm_texture_cache_resolve(
        textures, "textures/settings_html/arrow-right-focus.png",
        &right_focus));
    assert(wm_texture_cache_resolve(
        textures, "textures/settings_html/arrow-left-focus.png",
        &left_focus));
    WmSettingsScene *scene = wm_settings_scene_create(
        (WmPlatform *)1, assets, textures, fonts);
    assert(scene);
    assert(wm_settings_scene_open(scene));
    wm_settings_scene_advance(scene, 21.0f);

    WmSettingsControl controls[3] = {
        WM_SETTINGS_CONTROL_ITEM_1,
        WM_SETTINGS_CONTROL_BACK,
        WM_SETTINGS_CONTROL_NEXT
    };
    uint32_t expected[3] = {row_focus, footer_focus, right_focus};
    for (unsigned step = 0; step < 3; step++) {
        assert(wm_settings_scene_hover(scene, controls[step]));
        assert(wm_settings_scene_snapshot(scene).hover_opacity == 1.0f);
        drawn_quad_count = 0;
        wm_texture_cache_begin_frame(textures);
        wm_font_cache_begin_frame(fonts);
        assert(wm_settings_scene_draw(scene));
        WmQuad focused = {0};
        assert(count_drawn_texture(expected[step], &focused) == 1);
        assert(focused.color.a == 1.0f);
        for (unsigned other = 0; other < 3; other++)
            if (other != step)
                assert(count_drawn_texture(expected[other], NULL) == 0);
    }
    assert(wm_settings_scene_activate(scene, WM_SETTINGS_CONTROL_NEXT));
    /* The caller re-hits a retained pointer position on each update. The
     * scroller blocks input until its final frame, then the same point may
     * focus the incoming page's right arrow without a pointer move. */
    assert(wm_settings_scene_hit(scene, 566, 216) ==
           WM_SETTINGS_CONTROL_NONE);
    wm_settings_scene_advance(scene, 40.0f);
    assert(wm_settings_scene_hit(scene, 566, 216) ==
           WM_SETTINGS_CONTROL_NEXT);
    assert(wm_settings_scene_hover(scene,
        wm_settings_scene_hit(scene, 566, 216)));
    drawn_quad_count = 0;
    wm_texture_cache_begin_frame(textures);
    wm_font_cache_begin_frame(fonts);
    assert(wm_settings_scene_draw(scene));
    assert(count_drawn_texture(right_focus, NULL) == 1);
    assert(wm_settings_scene_hover(scene, WM_SETTINGS_CONTROL_PREVIOUS));
    drawn_quad_count = 0;
    wm_texture_cache_begin_frame(textures);
    wm_font_cache_begin_frame(fonts);
    assert(wm_settings_scene_draw(scene));
    WmQuad focused_left = {0};
    assert(count_drawn_texture(left_focus, &focused_left) == 1);
    assert(focused_left.color.a == 1.0f);
    assert(count_drawn_texture(right_focus, NULL) == 0);
    assert(wm_settings_scene_hover(scene, WM_SETTINGS_CONTROL_NONE));
    drawn_quad_count = 0;
    wm_texture_cache_begin_frame(textures);
    wm_font_cache_begin_frame(fonts);
    assert(wm_settings_scene_draw(scene));
    assert(count_drawn_texture(left_focus, NULL) == 0);

    wm_settings_scene_destroy(scene);
    wm_font_cache_destroy(fonts);
    wm_texture_cache_destroy(textures);
}

static void test_hover_image_swap(const char *assets) {
    WmSettingsScene *scene = wm_settings_scene_create(
        (WmPlatform *)1, assets, (WmTextureCache *)1,
        (WmFontCache *)1);
    assert(scene);
    assert(wm_settings_scene_open(scene));
    wm_settings_scene_advance(scene, 21.0f);
    assert(wm_settings_scene_hover(scene, WM_SETTINGS_CONTROL_ITEM_1));
    assert(wm_settings_scene_snapshot(scene).hover_opacity == 1.0f);
    wm_settings_scene_advance(scene, 10.0f);
    assert(wm_settings_scene_snapshot(scene).hover_opacity == 1.0f);
    assert(wm_settings_scene_hover(scene, WM_SETTINGS_CONTROL_ITEM_2));
    assert(wm_settings_scene_snapshot(scene).hover_opacity == 1.0f);
    assert(wm_settings_scene_hover(scene, WM_SETTINGS_CONTROL_ITEM_1));
    assert(wm_settings_scene_snapshot(scene).hover_opacity == 1.0f);
    assert(wm_settings_scene_hover(scene, WM_SETTINGS_CONTROL_NONE));
    assert(wm_settings_scene_snapshot(scene).hover_opacity == 0.0f);
    assert(wm_settings_scene_hover(scene, WM_SETTINGS_CONTROL_ITEM_1));
    assert(wm_settings_scene_activate(scene, WM_SETTINGS_CONTROL_ITEM_1));
    assert(wm_settings_scene_snapshot(scene).hover_opacity == 0.0f);
    assert(wm_settings_scene_snapshot(scene).page_opacity == 0.0f);
    wm_settings_scene_advance(scene, 10.0f);
    assert(fabsf(wm_settings_scene_snapshot(scene).page_opacity -
                 (127.0f / 255.0f)) < 0.001f);
    wm_settings_scene_advance(scene, 10.0f);
    assert(wm_settings_scene_snapshot(scene).page_opacity == 1.0f);
    wm_settings_scene_destroy(scene);
}

static void test_choice_and_detail_navigation(const char *assets) {
    WmSettingsScene *scene = wm_settings_scene_create(
        (WmPlatform *)1, assets, (WmTextureCache *)1, (WmFontCache *)1);
    assert(scene);
    assert(wm_settings_scene_open(scene));
    wm_settings_scene_advance(scene, 21.0f);

    assert(wm_settings_scene_activate(scene, WM_SETTINGS_CONTROL_ITEM_4));
    assert(wm_settings_scene_snapshot(scene).category == 4);
    assert(wm_settings_scene_snapshot(scene).selection == 1);
    assert(wm_settings_scene_hit(scene, 132, 100) ==
           WM_SETTINGS_CONTROL_ITEM_1);
    assert(wm_settings_scene_hit(scene, 350, 400) ==
           WM_SETTINGS_CONTROL_NEXT);
    assert(wm_settings_scene_activate(scene, WM_SETTINGS_CONTROL_ITEM_3));
    assert(wm_settings_scene_snapshot(scene).selection == 2);
    assert(wm_settings_scene_back(scene));
    assert(wm_settings_scene_snapshot(scene).category == 0);
    assert(wm_settings_scene_activate(scene, WM_SETTINGS_CONTROL_ITEM_4));
    assert(wm_settings_scene_snapshot(scene).selection == 1);
    assert(wm_settings_scene_activate(scene, WM_SETTINGS_CONTROL_ITEM_1));
    assert(wm_settings_scene_activate(scene, WM_SETTINGS_CONTROL_NEXT));
    assert(wm_settings_scene_snapshot(scene).category == 0);
    assert(wm_settings_scene_activate(scene, WM_SETTINGS_CONTROL_ITEM_4));
    assert(wm_settings_scene_snapshot(scene).selection == 0);
    assert(wm_settings_scene_back(scene));

    assert(wm_settings_scene_activate(scene, WM_SETTINGS_CONTROL_ITEM_3));
    assert(wm_settings_scene_snapshot(scene).category == 3);
    assert(wm_settings_scene_activate(scene, WM_SETTINGS_CONTROL_ITEM_2));
    assert(wm_settings_scene_snapshot(scene).detail == 2);
    assert(wm_settings_scene_hit(scene, 16 + 300, 160) ==
           WM_SETTINGS_CONTROL_ITEM_2);
    assert(wm_settings_scene_activate(scene, WM_SETTINGS_CONTROL_ITEM_1));
    assert(wm_settings_scene_snapshot(scene).selection == 0);
    assert(wm_settings_scene_activate(scene, WM_SETTINGS_CONTROL_NEXT));
    assert(wm_settings_scene_snapshot(scene).detail == 0);
    assert(wm_settings_scene_activate(scene, WM_SETTINGS_CONTROL_ITEM_2));
    assert(wm_settings_scene_snapshot(scene).selection == 0);
    assert(wm_settings_scene_back(scene));
    assert(wm_settings_scene_snapshot(scene).detail == 0);
    assert(wm_settings_scene_activate(scene, WM_SETTINGS_CONTROL_ITEM_1));
    assert(wm_settings_scene_snapshot(scene).selection == 16);
    assert(wm_settings_scene_hit(scene, 16 + 185, 205) ==
           WM_SETTINGS_CONTROL_ITEM_1);
    assert(wm_settings_scene_activate(scene, WM_SETTINGS_CONTROL_ITEM_1));
    assert(wm_settings_scene_snapshot(scene).selection == 18);
    assert(wm_settings_scene_activate(scene, WM_SETTINGS_CONTROL_ITEM_2));
    assert(wm_settings_scene_snapshot(scene).selection == 16);
    assert(wm_settings_scene_back(scene));
    assert(wm_settings_scene_back(scene));
    assert(wm_settings_scene_snapshot(scene).category == 0);
    wm_settings_scene_destroy(scene);
}

static void test_calendar_and_sensitivity(const char *assets) {
    WmSettingsScene *scene = wm_settings_scene_create(
        (WmPlatform *)1, assets, (WmTextureCache *)1, (WmFontCache *)1);
    assert(scene);
    assert(wm_settings_scene_open(scene));
    wm_settings_scene_advance(scene, 21.0f);
    assert(wm_settings_scene_activate(scene, WM_SETTINGS_CONTROL_ITEM_2));
    assert(wm_settings_scene_snapshot(scene).category == 2);
    assert(wm_settings_scene_hit(scene, 16 + 150, 150) ==
           WM_SETTINGS_CONTROL_ITEM_1);
    assert(wm_settings_scene_activate(scene, WM_SETTINGS_CONTROL_ITEM_1));
    assert(wm_settings_scene_snapshot(scene).detail == 1);
    WmSettingsSnapshot original = wm_settings_scene_snapshot(scene);
    assert(wm_settings_scene_hit(scene, 16 + 430, 120) ==
           WM_SETTINGS_CONTROL_ITEM_1);
    assert(wm_settings_scene_hit(scene, 16 + 430, 270) ==
           WM_SETTINGS_CONTROL_ITEM_2);
    assert(wm_settings_scene_hit(scene, 16 + 110, 120) ==
           WM_SETTINGS_CONTROL_ITEM_3);
    assert(wm_settings_scene_hit(scene, 16 + 110, 270) ==
           WM_SETTINGS_CONTROL_ITEM_4);
    assert(wm_settings_scene_hit(scene, 16 + 250, 120) ==
           WM_SETTINGS_CONTROL_ITEM_5);
    assert(wm_settings_scene_hit(scene, 16 + 250, 270) ==
           WM_SETTINGS_CONTROL_ITEM_6);
    assert(wm_settings_scene_activate(scene, WM_SETTINGS_CONTROL_ITEM_1));
    assert(wm_settings_scene_snapshot(scene).year ==
           (original.year + 1) % 36);
    assert(wm_settings_scene_activate(scene, WM_SETTINGS_CONTROL_ITEM_2));
    assert(wm_settings_scene_snapshot(scene).year == original.year);
    assert(wm_settings_scene_activate(scene, WM_SETTINGS_CONTROL_ITEM_3));
    assert(wm_settings_scene_snapshot(scene).month ==
           (original.month % 12) + 1);
    assert(wm_settings_scene_activate(scene, WM_SETTINGS_CONTROL_ITEM_4));
    assert(wm_settings_scene_snapshot(scene).month == original.month);
    assert(wm_settings_scene_back(scene));
    assert(wm_settings_scene_snapshot(scene).detail == 0);
    assert(wm_settings_scene_activate(scene, WM_SETTINGS_CONTROL_ITEM_1));
    assert(wm_settings_scene_snapshot(scene).year == original.year);
    assert(wm_settings_scene_activate(scene, WM_SETTINGS_CONTROL_ITEM_5));
    assert(wm_settings_scene_activate(scene, WM_SETTINGS_CONTROL_NEXT));
    assert(wm_settings_scene_snapshot(scene).detail == 0);
    unsigned saved_day = wm_settings_scene_snapshot(scene).day;
    assert(wm_settings_scene_activate(scene, WM_SETTINGS_CONTROL_ITEM_1));
    assert(wm_settings_scene_snapshot(scene).day == saved_day);
    assert(wm_settings_scene_back(scene));

    assert(wm_settings_scene_activate(scene, WM_SETTINGS_CONTROL_ITEM_2));
    assert(wm_settings_scene_snapshot(scene).detail == 2);
    unsigned hour = wm_settings_scene_snapshot(scene).hour;
    unsigned minute = wm_settings_scene_snapshot(scene).minute;
    assert(wm_settings_scene_hit(scene, 16 + 220, 120) ==
           WM_SETTINGS_CONTROL_ITEM_1);
    assert(wm_settings_scene_activate(scene, WM_SETTINGS_CONTROL_ITEM_1));
    assert(wm_settings_scene_snapshot(scene).hour == (hour + 1) % 24);
    assert(wm_settings_scene_activate(scene, WM_SETTINGS_CONTROL_ITEM_2));
    assert(wm_settings_scene_snapshot(scene).hour == hour);
    assert(wm_settings_scene_activate(scene, WM_SETTINGS_CONTROL_ITEM_3));
    assert(wm_settings_scene_snapshot(scene).minute == (minute + 1) % 60);
    assert(wm_settings_scene_activate(scene, WM_SETTINGS_CONTROL_NEXT));
    assert(wm_settings_scene_snapshot(scene).minute == (minute + 1) % 60);
    assert(wm_settings_scene_activate(scene, WM_SETTINGS_CONTROL_ITEM_2));
    unsigned repeat_hour = wm_settings_scene_snapshot(scene).hour;
    assert(wm_settings_scene_pointer_down(scene, WM_SETTINGS_CONTROL_ITEM_1));
    assert(wm_settings_scene_snapshot(scene).hour == (repeat_hour + 1) % 24);
    wm_settings_scene_advance(scene, 23.0f);
    assert(wm_settings_scene_snapshot(scene).hour == (repeat_hour + 1) % 24);
    wm_settings_scene_advance(scene, 1.0f);
    assert(wm_settings_scene_snapshot(scene).hour == (repeat_hour + 2) % 24);
    wm_settings_scene_advance(scene, 9.0f);
    assert(wm_settings_scene_snapshot(scene).hour == (repeat_hour + 3) % 24);
    wm_settings_scene_pointer_up(scene);
    wm_settings_scene_advance(scene, 30.0f);
    assert(wm_settings_scene_snapshot(scene).hour == (repeat_hour + 3) % 24);
    assert(wm_settings_scene_back(scene));
    assert(wm_settings_scene_back(scene));

    assert(wm_settings_scene_activate(scene, WM_SETTINGS_CONTROL_NEXT));
    wm_settings_scene_advance(scene, 40.0f);
    assert(wm_settings_scene_snapshot(scene).page == 2);
    assert(wm_settings_scene_activate(scene, WM_SETTINGS_CONTROL_ITEM_2));
    assert(wm_settings_scene_snapshot(scene).category == 6);
    assert(wm_settings_scene_activate(scene, WM_SETTINGS_CONTROL_ITEM_2));
    assert(wm_settings_scene_snapshot(scene).detail == 2);
    assert(wm_settings_scene_snapshot(scene).sensitivity == 3);
    assert(wm_settings_scene_hit(scene, 16 + 465, 310) ==
           WM_SETTINGS_CONTROL_NONE);
    assert(wm_settings_scene_activate(scene, WM_SETTINGS_CONTROL_NEXT));
    assert(wm_settings_scene_hit(scene, 16 + 465, 310) ==
           WM_SETTINGS_CONTROL_ITEM_2);
    assert(wm_settings_scene_activate(scene, WM_SETTINGS_CONTROL_ITEM_2));
    assert(wm_settings_scene_activate(scene, WM_SETTINGS_CONTROL_ITEM_2));
    assert(wm_settings_scene_snapshot(scene).sensitivity == 5);
    assert(wm_settings_scene_activate(scene, WM_SETTINGS_CONTROL_ITEM_2));
    assert(wm_settings_scene_snapshot(scene).sensitivity == 5);
    assert(wm_settings_scene_back(scene));
    assert(wm_settings_scene_snapshot(scene).sensitivity == 3);
    assert(wm_settings_scene_activate(scene, WM_SETTINGS_CONTROL_ITEM_2));
    assert(wm_settings_scene_activate(scene, WM_SETTINGS_CONTROL_NEXT));
    assert(wm_settings_scene_activate(scene, WM_SETTINGS_CONTROL_ITEM_2));
    assert(wm_settings_scene_activate(scene, WM_SETTINGS_CONTROL_NEXT));
    assert(wm_settings_scene_snapshot(scene).detail == 0);
    assert(wm_settings_scene_snapshot(scene).sensitivity == 4);
    wm_settings_scene_destroy(scene);
}

static void test_extended_categories(const char *assets) {
    WmSettingsScene *scene = wm_settings_scene_create(
        (WmPlatform *)1, assets, (WmTextureCache *)1, (WmFontCache *)1);
    assert(scene);
    assert(wm_settings_scene_open(scene));
    wm_settings_scene_advance(scene, 21.0f);

    assert(wm_settings_scene_activate(scene, WM_SETTINGS_CONTROL_ITEM_1));
    assert(wm_settings_scene_snapshot(scene).category == 1);
    assert(wm_settings_scene_type_ascii(scene, 'A'));
    assert(wm_settings_scene_backspace(scene));
    assert(strcmp(wm_settings_scene_snapshot(scene).nickname, "Wii") == 0);
    assert(wm_settings_scene_type_ascii(scene, 'X'));
    assert(wm_settings_scene_back(scene));
    assert(wm_settings_scene_activate(scene, WM_SETTINGS_CONTROL_ITEM_1));
    assert(strcmp(wm_settings_scene_snapshot(scene).nickname, "Wii") == 0);
    assert(wm_settings_scene_type_ascii(scene, 'X'));
    assert(wm_settings_scene_activate(scene, WM_SETTINGS_CONTROL_NEXT));
    assert(strcmp(wm_settings_scene_snapshot(scene).nickname, "WiiX") == 0);

    assert(wm_settings_scene_activate(scene, WM_SETTINGS_CONTROL_NEXT));
    wm_settings_scene_advance(scene, 40.0f);
    assert(wm_settings_scene_snapshot(scene).page == 2);
    assert(wm_settings_scene_activate(scene, WM_SETTINGS_CONTROL_ITEM_1));
    assert(wm_settings_scene_snapshot(scene).category == 5);
    assert(wm_settings_scene_activate(scene, WM_SETTINGS_CONTROL_BACK));
    assert(wm_settings_scene_snapshot(scene).detail == 1);
    assert(wm_settings_scene_activate(scene, WM_SETTINGS_CONTROL_NEXT));
    assert(wm_settings_scene_snapshot(scene).detail == 2);
    assert(wm_settings_scene_back(scene));
    assert(wm_settings_scene_snapshot(scene).detail == 1);
    assert(wm_settings_scene_activate(scene, WM_SETTINGS_CONTROL_NEXT));
    assert(wm_settings_scene_activate(scene, WM_SETTINGS_CONTROL_NEXT));
    assert(wm_settings_scene_snapshot(scene).category == 0);
    assert(wm_settings_scene_snapshot(scene).parental_enabled);

    assert(wm_settings_scene_activate(scene, WM_SETTINGS_CONTROL_ITEM_3));
    assert(wm_settings_scene_snapshot(scene).category == 7);
    assert(wm_settings_scene_hit(scene, 16 + 120, 100) ==
           WM_SETTINGS_CONTROL_ITEM_1);
    assert(wm_settings_scene_activate(scene, WM_SETTINGS_CONTROL_ITEM_1));
    assert(wm_settings_scene_snapshot(scene).detail == 1);
    assert(wm_settings_scene_activate(scene, WM_SETTINGS_CONTROL_ITEM_2));
    assert(wm_settings_scene_snapshot(scene).detail == 4);
    assert(wm_settings_scene_back(scene));
    assert(wm_settings_scene_snapshot(scene).detail == 1);
    assert(wm_settings_scene_back(scene));
    assert(wm_settings_scene_snapshot(scene).detail == 0);
    assert(wm_settings_scene_activate(scene, WM_SETTINGS_CONTROL_ITEM_3));
    assert(wm_settings_scene_snapshot(scene).detail == 3);
    assert(wm_settings_scene_activate(scene, WM_SETTINGS_CONTROL_BACK));
    assert(wm_settings_scene_snapshot(scene).internet_agreement);
    assert(wm_settings_scene_back(scene));

    assert(wm_settings_scene_activate(scene, WM_SETTINGS_CONTROL_ITEM_4));
    assert(wm_settings_scene_snapshot(scene).category == 8);
    assert(wm_settings_scene_hit(scene, 16 + 120, 200) ==
           WM_SETTINGS_CONTROL_NONE);
    assert(wm_settings_scene_activate(scene, WM_SETTINGS_CONTROL_ITEM_1));
    assert(wm_settings_scene_snapshot(scene).detail == 1);
    assert(wm_settings_scene_activate(scene, WM_SETTINGS_CONTROL_ITEM_1));
    assert(wm_settings_scene_activate(scene, WM_SETTINGS_CONTROL_NEXT));
    assert(wm_settings_scene_snapshot(scene).connect24_enabled);
    assert(wm_settings_scene_hit(scene, 16 + 120, 200) ==
           WM_SETTINGS_CONTROL_ITEM_2);
    assert(wm_settings_scene_activate(scene, WM_SETTINGS_CONTROL_ITEM_3));
    assert(wm_settings_scene_snapshot(scene).detail == 3);
    assert(wm_settings_scene_activate(scene, WM_SETTINGS_CONTROL_ITEM_2));
    assert(wm_settings_scene_activate(scene, WM_SETTINGS_CONTROL_NEXT));
    assert(wm_settings_scene_snapshot(scene).slot_light == 1);
    assert(wm_settings_scene_back(scene));

    assert(wm_settings_scene_activate(scene, WM_SETTINGS_CONTROL_NEXT));
    wm_settings_scene_advance(scene, 40.0f);
    assert(wm_settings_scene_snapshot(scene).page == 3);
    assert(wm_settings_scene_activate(scene, WM_SETTINGS_CONTROL_ITEM_2));
    assert(wm_settings_scene_snapshot(scene).category == 10);
    assert(wm_settings_scene_snapshot(scene).country_page == 0);
    assert(wm_settings_scene_snapshot(scene).country_choice == 41);
    assert(wm_settings_scene_hit(scene, 16 + 550, 90) ==
           WM_SETTINGS_CONTROL_NONE);
    for (unsigned page = 0; page < 8; page++)
        assert(wm_settings_scene_activate(scene, WM_SETTINGS_CONTROL_ITEM_6));
    assert(wm_settings_scene_snapshot(scene).country_page == 8);
    assert(wm_settings_scene_hit(scene, 16 + 550, 300) ==
           WM_SETTINGS_CONTROL_ITEM_6);
    assert(wm_settings_scene_activate(scene, WM_SETTINGS_CONTROL_ITEM_1));
    assert(wm_settings_scene_snapshot(scene).country_choice == 39);
    assert(wm_settings_scene_back(scene));
    assert(wm_settings_scene_activate(scene, WM_SETTINGS_CONTROL_ITEM_2));
    assert(wm_settings_scene_snapshot(scene).country_page == 0);
    assert(wm_settings_scene_snapshot(scene).country_choice == 41);
    assert(wm_settings_scene_activate(scene, WM_SETTINGS_CONTROL_NEXT));

    assert(wm_settings_scene_activate(scene, WM_SETTINGS_CONTROL_ITEM_3));
    assert(wm_settings_scene_snapshot(scene).category == 11);
    assert(wm_settings_scene_activate(scene, WM_SETTINGS_CONTROL_BACK));
    assert(wm_settings_scene_snapshot(scene).detail == 1);
    assert(wm_settings_scene_activate(scene, WM_SETTINGS_CONTROL_NEXT));
    assert(wm_settings_scene_snapshot(scene).category == 0);

    assert(wm_settings_scene_activate(scene, WM_SETTINGS_CONTROL_ITEM_4));
    assert(wm_settings_scene_snapshot(scene).category == 12);
    assert(wm_settings_scene_activate(scene, WM_SETTINGS_CONTROL_NEXT));
    assert(wm_settings_scene_snapshot(scene).detail == 1);
    assert(wm_settings_scene_activate(scene, WM_SETTINGS_CONTROL_NEXT));
    assert(wm_settings_scene_snapshot(scene).detail == 2);
    assert(wm_settings_scene_activate(scene, WM_SETTINGS_CONTROL_BACK));
    assert(wm_settings_scene_snapshot(scene).detail == 3);
    assert(wm_settings_scene_snapshot(scene).local_format_complete);
    assert(strcmp(wm_settings_scene_snapshot(scene).nickname, "Wii") == 0);
    assert(!wm_settings_scene_snapshot(scene).parental_enabled);
    assert(wm_settings_scene_activate(scene, WM_SETTINGS_CONTROL_NEXT));
    assert(wm_settings_scene_snapshot(scene).category == 0);
    wm_settings_scene_destroy(scene);
}

static void test_console_information_placeholders(const char *assets) {
    WmTextureCache *textures = wm_texture_cache_create(
        (WmPlatform *)1, assets, 64u * 1024u * 1024u);
    WmFontCache *fonts = wm_font_cache_create(
        (WmPlatform *)1, assets, 16u * 1024u * 1024u);
    assert(textures && fonts);
    uint32_t nested_tab = 0;
    uint32_t footer = 0;
    assert(wm_texture_cache_resolve(
        textures, "textures/settings_html/tab-gray-nested.png",
        &nested_tab));
    assert(wm_texture_cache_resolve(
        textures, "textures/settings_html/footer-button.png", &footer));

    WmSettingsScene *scene = wm_settings_scene_create(
        (WmPlatform *)1, assets, textures, fonts);
    assert(scene);
    assert(wm_settings_scene_open(scene));
    wm_settings_scene_advance(scene, 21.0f);
    assert(wm_settings_scene_activate(scene, WM_SETTINGS_CONTROL_NEXT));
    wm_settings_scene_advance(scene, 40.0f);
    assert(wm_settings_scene_activate(scene, WM_SETTINGS_CONTROL_ITEM_3));
    wm_settings_scene_advance(scene, 20.0f);
    assert(wm_settings_scene_activate(scene, WM_SETTINGS_CONTROL_ITEM_2));
    wm_settings_scene_advance(scene, 20.0f);
    assert(wm_settings_scene_snapshot(scene).category == 7);
    assert(wm_settings_scene_snapshot(scene).detail == 2);
    assert(wm_settings_scene_hit(scene, 16 + 164, 400) ==
           WM_SETTINGS_CONTROL_BACK);
    assert(wm_settings_scene_hit(scene, 16 + 444, 400) ==
           WM_SETTINGS_CONTROL_NONE);

    drawn_quad_count = 0;
    wm_texture_cache_begin_frame(textures);
    wm_font_cache_begin_frame(fonts);
    assert(wm_settings_scene_draw(scene));
    assert(count_drawn_texture(nested_tab, NULL) == 1);
    assert(count_drawn_texture(footer, NULL) == 1);
    unsigned wireless_glyphs = 0;
    unsigned unavailable_lan_glyphs = 0;
    for (size_t index = 0; index < drawn_quad_count; index++) {
        const WmQuad *quad = &drawn_quads[index];
        if (!quad->texture) continue;
        bool white = quad->color.r == 1.0f &&
                     quad->color.g == 1.0f &&
                     quad->color.b == 1.0f;
        bool dim = fabsf(quad->color.r - 0.2f) < 0.001f &&
                   fabsf(quad->color.g - 0.2f) < 0.001f &&
                   fabsf(quad->color.b - 0.2f) < 0.001f;
        if (white && quad->y >= 108.0f && quad->y < 182.0f)
            wireless_glyphs++;
        if (dim && quad->y >= 250.0f && quad->y < 326.0f)
            unavailable_lan_glyphs++;
    }
    /* Both source labels and both local dummy addresses must be visible.
     * The LAN label and address use the extracted page's #333 tint. */
    assert(wireless_glyphs >= 20);
    assert(unavailable_lan_glyphs >= 30);

    assert(wm_settings_scene_back(scene));
    assert(wm_settings_scene_snapshot(scene).detail == 0);
    wm_settings_scene_destroy(scene);
    wm_font_cache_destroy(fonts);
    wm_texture_cache_destroy(textures);
}

static void test_update_initial_footer(const char *assets) {
    WmTextureCache *textures = wm_texture_cache_create(
        (WmPlatform *)1, assets, 64u * 1024u * 1024u);
    WmFontCache *fonts = wm_font_cache_create(
        (WmPlatform *)1, assets, 16u * 1024u * 1024u);
    assert(textures && fonts);
    uint32_t footer = 0;
    assert(wm_texture_cache_resolve(
        textures, "textures/settings_html/footer-button.png", &footer));
    WmSettingsScene *scene = wm_settings_scene_create(
        (WmPlatform *)1, assets, textures, fonts);
    assert(scene);
    assert(wm_settings_scene_open(scene));
    wm_settings_scene_advance(scene, 21.0f);
    for (unsigned page = 1; page < 3; page++) {
        assert(wm_settings_scene_activate(scene, WM_SETTINGS_CONTROL_NEXT));
        wm_settings_scene_advance(scene, 40.0f);
    }
    assert(wm_settings_scene_activate(scene, WM_SETTINGS_CONTROL_ITEM_3));
    wm_settings_scene_advance(scene, 20.0f);
    assert(wm_settings_scene_snapshot(scene).category == 11);
    assert(wm_settings_scene_hit(scene, 16 + 164, 400) ==
           WM_SETTINGS_CONTROL_BACK);
    assert(wm_settings_scene_hit(scene, 16 + 444, 400) ==
           WM_SETTINGS_CONTROL_NEXT);

    drawn_quad_count = 0;
    wm_texture_cache_begin_frame(textures);
    wm_font_cache_begin_frame(fonts);
    assert(wm_settings_scene_draw(scene));
    assert(count_drawn_texture(footer, NULL) == 2);
    unsigned left_glyphs = 0;
    unsigned right_glyphs = 0;
    for (size_t index = 0; index < drawn_quad_count; index++) {
        const WmQuad *quad = &drawn_quads[index];
        if (!quad->texture || quad->texture == footer ||
            quad->y < 390.0f || quad->y >= 430.0f ||
            fabsf(quad->color.r - 0.2f) >= 0.001f) continue;
        if (quad->x < 304.0f) left_glyphs++;
        else right_glyphs++;
    }
    /* The extracted first update page labels its left WAD footer Yes and
     * its right WAD footer No. The outline glyph counts are 3 and 2. */
    assert(left_glyphs == 3);
    assert(right_glyphs == 2);

    /* Yes enters the local offline explanation; its Back returns here.
     * No returns to the Settings index without entering the explanation. */
    assert(wm_settings_scene_activate(scene, WM_SETTINGS_CONTROL_BACK));
    assert(wm_settings_scene_snapshot(scene).detail == 1);
    assert(wm_settings_scene_back(scene));
    assert(wm_settings_scene_snapshot(scene).detail == 0);
    assert(wm_settings_scene_activate(scene, WM_SETTINGS_CONTROL_NEXT));
    assert(wm_settings_scene_snapshot(scene).category == 0);
    wm_settings_scene_destroy(scene);
    wm_font_cache_destroy(fonts);
    wm_texture_cache_destroy(textures);
}

static void test_format_red_action_rollover(const char *assets) {
    WmTextureCache *textures = wm_texture_cache_create(
        (WmPlatform *)1, assets, 64u * 1024u * 1024u);
    WmFontCache *fonts = wm_font_cache_create(
        (WmPlatform *)1, assets, 16u * 1024u * 1024u);
    assert(textures && fonts);
    uint32_t footer = 0;
    uint32_t normal_focus = 0;
    uint32_t red_focus = 0;
    assert(wm_texture_cache_resolve(
        textures, "textures/settings_html/footer-button.png", &footer));
    assert(wm_texture_cache_resolve(
        textures, "textures/settings_html/footer-button-focus.png",
        &normal_focus));
    assert(wm_texture_cache_resolve(
        textures, "textures/settings_html/footer-button-red-focus.png",
        &red_focus));
    assert(red_focus != normal_focus);

    WmSettingsScene *scene = wm_settings_scene_create(
        (WmPlatform *)1, assets, textures, fonts);
    assert(scene);
    assert(wm_settings_scene_open(scene));
    wm_settings_scene_advance(scene, 21.0f);
    for (unsigned page = 1; page < 3; page++) {
        assert(wm_settings_scene_activate(scene, WM_SETTINGS_CONTROL_NEXT));
        wm_settings_scene_advance(scene, 40.0f);
    }
    assert(wm_settings_scene_activate(scene, WM_SETTINGS_CONTROL_ITEM_4));
    wm_settings_scene_advance(scene, 20.0f);
    assert(wm_settings_scene_snapshot(scene).category == 12);
    assert(wm_settings_scene_hover(scene, WM_SETTINGS_CONTROL_NEXT));
    wm_settings_scene_advance(scene, 10.0f);
    drawn_quad_count = 0;
    wm_texture_cache_begin_frame(textures);
    wm_font_cache_begin_frame(fonts);
    assert(wm_settings_scene_draw(scene));
    WmQuad red = {0};
    assert(count_drawn_texture(footer, NULL) == 2);
    assert(count_drawn_texture(red_focus, &red) == 1);
    assert(red.x == 324.0f && red.y == 371.0f);
    assert(red.width == 272.0f && red.height == 72.0f);
    assert(red.color.a == 1.0f);
    assert(count_drawn_texture(normal_focus, NULL) == 0);

    assert(wm_settings_scene_hover(scene, WM_SETTINGS_CONTROL_BACK));
    wm_settings_scene_advance(scene, 10.0f);
    drawn_quad_count = 0;
    wm_texture_cache_begin_frame(textures);
    wm_font_cache_begin_frame(fonts);
    assert(wm_settings_scene_draw(scene));
    WmQuad ordinary = {0};
    assert(count_drawn_texture(red_focus, NULL) == 0);
    assert(count_drawn_texture(normal_focus, &ordinary) == 1);
    assert(ordinary.x == 44.0f && ordinary.y == 371.0f);

    /* The second warning still places Format on the right. The following
     * confirmation page moves that red action to the left. */
    assert(wm_settings_scene_activate(scene, WM_SETTINGS_CONTROL_NEXT));
    wm_settings_scene_advance(scene, 20.0f);
    assert(wm_settings_scene_hover(scene, WM_SETTINGS_CONTROL_NEXT));
    wm_settings_scene_advance(scene, 20.0f);
    drawn_quad_count = 0;
    wm_texture_cache_begin_frame(textures);
    wm_font_cache_begin_frame(fonts);
    assert(wm_settings_scene_draw(scene));
    assert(count_drawn_texture(red_focus, &red) == 1);
    assert(red.x == 324.0f && red.color.a == 1.0f);

    assert(wm_settings_scene_activate(scene, WM_SETTINGS_CONTROL_NEXT));
    wm_settings_scene_advance(scene, 20.0f);
    assert(wm_settings_scene_hover(scene, WM_SETTINGS_CONTROL_BACK));
    wm_settings_scene_advance(scene, 20.0f);
    drawn_quad_count = 0;
    wm_texture_cache_begin_frame(textures);
    wm_font_cache_begin_frame(fonts);
    assert(wm_settings_scene_draw(scene));
    assert(count_drawn_texture(red_focus, &red) == 1);
    assert(red.x == 44.0f && red.color.a == 1.0f);
    assert(count_drawn_texture(normal_focus, NULL) == 0);
    wm_settings_scene_destroy(scene);
    wm_font_cache_destroy(fonts);
    wm_texture_cache_destroy(textures);
}

static void test_connect24_onoff_immediate_state(const char *assets) {
    WmTextureCache *textures = wm_texture_cache_create(
        (WmPlatform *)1, assets, 64u * 1024u * 1024u);
    WmFontCache *fonts = wm_font_cache_create(
        (WmPlatform *)1, assets, 16u * 1024u * 1024u);
    assert(textures && fonts);
    uint32_t row = 0;
    uint32_t disabled_row = 0;
    uint32_t choice_left = 0;
    assert(wm_texture_cache_resolve(
        textures, "textures/settings_html/large-row.png", &row));
    assert(wm_texture_cache_resolve(
        textures, "textures/settings_html/large-row-disabled.png",
        &disabled_row));
    assert(wm_texture_cache_resolve(
        textures, "textures/settings_html/choice-left.png",
        &choice_left));

    WmSettingsScene *scene = wm_settings_scene_create(
        (WmPlatform *)1, assets, textures, fonts);
    assert(scene);
    assert(wm_settings_scene_open(scene));
    wm_settings_scene_advance(scene, 21.0f);
    assert(wm_settings_scene_activate(scene, WM_SETTINGS_CONTROL_NEXT));
    wm_settings_scene_advance(scene, 40.0f);
    assert(wm_settings_scene_activate(scene, WM_SETTINGS_CONTROL_ITEM_4));
    wm_settings_scene_advance(scene, 20.0f);
    assert(wm_settings_scene_snapshot(scene).category == 8);
    assert(!wm_settings_scene_snapshot(scene).connect24_enabled);
    drawn_quad_count = 0;
    wm_texture_cache_begin_frame(textures);
    wm_font_cache_begin_frame(fonts);
    assert(wm_settings_scene_draw(scene));
    assert(count_drawn_texture(row, NULL) == 1);
    assert(count_drawn_texture(disabled_row, NULL) == 2);

    assert(wm_settings_scene_activate(scene, WM_SETTINGS_CONTROL_ITEM_1));
    wm_settings_scene_advance(scene, 20.0f);
    WmQuad selected = {0};
    drawn_quad_count = 0;
    wm_texture_cache_begin_frame(textures);
    wm_font_cache_begin_frame(fonts);
    assert(wm_settings_scene_draw(scene));
    assert(count_drawn_texture(choice_left, &selected) == 1);
    assert(selected.y == 216.0f); /* Off is the bridge default. */

    assert(wm_settings_scene_activate(scene, WM_SETTINGS_CONTROL_ITEM_1));
    assert(wm_settings_scene_snapshot(scene).selection == 0);
    assert(wm_settings_scene_snapshot(scene).connect24_enabled);
    assert(wm_settings_scene_snapshot(scene).page_opacity == 1.0f);
    drawn_quad_count = 0;
    wm_texture_cache_begin_frame(textures);
    wm_font_cache_begin_frame(fonts);
    assert(wm_settings_scene_draw(scene));
    assert(count_drawn_texture(choice_left, &selected) == 1);
    assert(selected.y == 120.0f);

    assert(wm_settings_scene_back(scene));
    assert(wm_settings_scene_snapshot(scene).detail == 0);
    assert(wm_settings_scene_snapshot(scene).page_opacity == 0.0f);
    wm_settings_scene_advance(scene, 20.0f);
    drawn_quad_count = 0;
    wm_texture_cache_begin_frame(textures);
    wm_font_cache_begin_frame(fonts);
    assert(wm_settings_scene_draw(scene));
    assert(count_drawn_texture(row, NULL) == 3);
    assert(count_drawn_texture(disabled_row, NULL) == 0);
    assert(wm_settings_scene_hit(scene, 16 + 150, 181) ==
           WM_SETTINGS_CONTROL_ITEM_2);

    assert(wm_settings_scene_activate(scene, WM_SETTINGS_CONTROL_ITEM_1));
    wm_settings_scene_advance(scene, 20.0f);
    assert(wm_settings_scene_activate(scene, WM_SETTINGS_CONTROL_ITEM_2));
    assert(!wm_settings_scene_snapshot(scene).connect24_enabled);
    assert(wm_settings_scene_back(scene));
    wm_settings_scene_advance(scene, 20.0f);
    drawn_quad_count = 0;
    wm_texture_cache_begin_frame(textures);
    wm_font_cache_begin_frame(fonts);
    assert(wm_settings_scene_draw(scene));
    assert(count_drawn_texture(row, NULL) == 1);
    assert(count_drawn_texture(disabled_row, NULL) == 2);
    wm_settings_scene_destroy(scene);
    wm_font_cache_destroy(fonts);
    wm_texture_cache_destroy(textures);
}

static void test_sensor_position_source_mapping(const char *assets) {
    WmTextureCache *textures = wm_texture_cache_create(
        (WmPlatform *)1, assets, 64u * 1024u * 1024u);
    WmFontCache *fonts = wm_font_cache_create(
        (WmPlatform *)1, assets, 16u * 1024u * 1024u);
    assert(textures && fonts);
    uint32_t row = 0;
    uint32_t choice_left = 0;
    uint32_t choice_right = 0;
    assert(wm_texture_cache_resolve(
        textures, "textures/settings_html/large-row.png", &row));
    assert(wm_texture_cache_resolve(
        textures, "textures/settings_html/choice-left.png",
        &choice_left));
    assert(wm_texture_cache_resolve(
        textures, "textures/settings_html/choice-right.png",
        &choice_right));

    WmSettingsScene *scene = wm_settings_scene_create(
        (WmPlatform *)1, assets, textures, fonts);
    assert(scene);
    assert(wm_settings_scene_open(scene));
    wm_settings_scene_advance(scene, 21.0f);
    assert(wm_settings_scene_activate(scene, WM_SETTINGS_CONTROL_NEXT));
    wm_settings_scene_advance(scene, 40.0f);
    assert(wm_settings_scene_activate(scene, WM_SETTINGS_CONTROL_ITEM_2));
    wm_settings_scene_advance(scene, 20.0f);
    assert(wm_settings_scene_activate(scene, WM_SETTINGS_CONTROL_ITEM_1));
    wm_settings_scene_advance(scene, 20.0f);
    assert(wm_settings_scene_snapshot(scene).category == 6);
    assert(wm_settings_scene_snapshot(scene).detail == 1);
    assert(wm_settings_scene_snapshot(scene).selection == 1);

    drawn_quad_count = 0;
    wm_texture_cache_begin_frame(textures);
    wm_font_cache_begin_frame(fonts);
    assert(wm_settings_scene_draw(scene));
    WmQuad marker = {0};
    assert(count_drawn_texture(row, NULL) == 2);
    assert(count_drawn_texture(choice_left, &marker) == 1);
    assert(count_drawn_texture(choice_right, NULL) == 1);
    assert(marker.y == 216.0f); /* The bridge starts at Below TV. */

    assert(wm_settings_scene_activate(scene, WM_SETTINGS_CONTROL_ITEM_1));
    assert(wm_settings_scene_snapshot(scene).selection == 0);
    assert(wm_settings_scene_snapshot(scene).page_opacity == 1.0f);
    drawn_quad_count = 0;
    wm_texture_cache_begin_frame(textures);
    wm_font_cache_begin_frame(fonts);
    assert(wm_settings_scene_draw(scene));
    assert(count_drawn_texture(choice_left, &marker) == 1);
    assert(marker.y == 120.0f);

    /* The original setter changes its local sensorBar value on row click.
     * Back leaves that value in place when this detail page is reopened. */
    assert(wm_settings_scene_back(scene));
    wm_settings_scene_advance(scene, 20.0f);
    assert(wm_settings_scene_activate(scene, WM_SETTINGS_CONTROL_ITEM_1));
    wm_settings_scene_advance(scene, 20.0f);
    assert(wm_settings_scene_snapshot(scene).selection == 0);
    assert(wm_settings_scene_activate(scene, WM_SETTINGS_CONTROL_ITEM_2));
    assert(wm_settings_scene_snapshot(scene).selection == 1);
    assert(wm_settings_scene_activate(scene, WM_SETTINGS_CONTROL_NEXT));
    wm_settings_scene_advance(scene, 20.0f);
    assert(wm_settings_scene_activate(scene, WM_SETTINGS_CONTROL_ITEM_1));
    assert(wm_settings_scene_snapshot(scene).selection == 1);
    wm_settings_scene_destroy(scene);
    wm_font_cache_destroy(fonts);
    wm_texture_cache_destroy(textures);
}

static void test_screen_position_back_retains_offset(const char *assets) {
    WmTextureCache *textures = wm_texture_cache_create(
        (WmPlatform *)1, assets, 64u * 1024u * 1024u);
    WmFontCache *fonts = wm_font_cache_create(
        (WmPlatform *)1, assets, 16u * 1024u * 1024u);
    assert(textures && fonts);
    uint32_t left_flame = 0;
    uint32_t right_flame = 0;
    uint32_t left_arrow = 0;
    uint32_t right_arrow = 0;
    assert(wm_texture_cache_resolve(
        textures, "textures/settings_html/position-flame-left.png",
        &left_flame));
    assert(wm_texture_cache_resolve(
        textures, "textures/settings_html/position-flame-right.png",
        &right_flame));
    assert(wm_texture_cache_resolve(
        textures, "textures/settings_html/arrow-left.png",
        &left_arrow));
    assert(wm_texture_cache_resolve(
        textures, "textures/settings_html/arrow-right.png",
        &right_arrow));

    WmSettingsScene *scene = wm_settings_scene_create(
        (WmPlatform *)1, assets, textures, fonts);
    assert(scene);
    assert(wm_settings_scene_open(scene));
    wm_settings_scene_advance(scene, 21.0f);
    assert(wm_settings_scene_activate(scene, WM_SETTINGS_CONTROL_ITEM_3));
    wm_settings_scene_advance(scene, 20.0f);
    assert(wm_settings_scene_activate(scene, WM_SETTINGS_CONTROL_ITEM_1));
    wm_settings_scene_advance(scene, 20.0f);
    assert(wm_settings_scene_snapshot(scene).category == 3);
    assert(wm_settings_scene_snapshot(scene).detail == 1);
    assert(wm_settings_scene_snapshot(scene).selection == 16);

    drawn_quad_count = 0;
    wm_texture_cache_begin_frame(textures);
    wm_font_cache_begin_frame(fonts);
    assert(wm_settings_scene_draw(scene));
    WmQuad flame = {0};
    WmQuad arrow = {0};
    assert(count_drawn_texture(left_flame, &flame) == 1);
    assert(flame.x == 46.0f && flame.y == 80.0f);
    assert(count_drawn_texture(right_flame, &flame) == 1);
    assert(flame.x == 566.0f && flame.y == 80.0f);
    assert(count_drawn_texture(left_arrow, &arrow) == 1);
    assert(arrow.x == 176.0f && arrow.y == 180.0f);
    assert(count_drawn_texture(right_arrow, &arrow) == 1);
    assert(arrow.x == 392.0f && arrow.y == 180.0f);

    assert(wm_settings_scene_activate(scene, WM_SETTINGS_CONTROL_ITEM_1));
    assert(wm_settings_scene_snapshot(scene).selection == 18);
    assert(wm_settings_scene_snapshot(scene).page_opacity == 1.0f);
    assert(wm_settings_scene_back(scene));
    wm_settings_scene_advance(scene, 20.0f);
    assert(wm_settings_scene_activate(scene, WM_SETTINGS_CONTROL_ITEM_1));
    assert(wm_settings_scene_snapshot(scene).selection == 18);

    assert(wm_settings_scene_activate(scene, WM_SETTINGS_CONTROL_ITEM_2));
    assert(wm_settings_scene_snapshot(scene).selection == 16);
    assert(wm_settings_scene_back(scene));
    wm_settings_scene_advance(scene, 20.0f);
    assert(wm_settings_scene_activate(scene, WM_SETTINGS_CONTROL_ITEM_1));
    assert(wm_settings_scene_snapshot(scene).selection == 16);
    wm_settings_scene_destroy(scene);
    wm_font_cache_destroy(fonts);
    wm_texture_cache_destroy(textures);
}

static void test_widescreen_back_retains_row(const char *assets) {
    WmTextureCache *textures = wm_texture_cache_create(
        (WmPlatform *)1, assets, 64u * 1024u * 1024u);
    WmFontCache *fonts = wm_font_cache_create(
        (WmPlatform *)1, assets, 16u * 1024u * 1024u);
    assert(textures && fonts);
    uint32_t standard = 0;
    uint32_t widescreen = 0;
    uint32_t marker_left = 0;
    uint32_t marker_right = 0;
    assert(wm_texture_cache_resolve(
        textures, "textures/settings_html/widescreen-standard.png",
        &standard));
    assert(wm_texture_cache_resolve(
        textures, "textures/settings_html/widescreen-wide.png",
        &widescreen));
    assert(wm_texture_cache_resolve(
        textures, "textures/settings_html/tv-choice-left.png",
        &marker_left));
    assert(wm_texture_cache_resolve(
        textures, "textures/settings_html/tv-choice-right.png",
        &marker_right));

    WmSettingsScene *scene = wm_settings_scene_create(
        (WmPlatform *)1, assets, textures, fonts);
    assert(scene);
    assert(wm_settings_scene_open(scene));
    wm_settings_scene_advance(scene, 21.0f);
    assert(wm_settings_scene_activate(scene, WM_SETTINGS_CONTROL_ITEM_3));
    wm_settings_scene_advance(scene, 20.0f);
    assert(wm_settings_scene_activate(scene, WM_SETTINGS_CONTROL_ITEM_2));
    wm_settings_scene_advance(scene, 20.0f);
    assert(wm_settings_scene_snapshot(scene).category == 3);
    assert(wm_settings_scene_snapshot(scene).detail == 2);
    assert(wm_settings_scene_snapshot(scene).selection == 1);
    assert(wm_settings_scene_hit(scene, 16 + 100, 200) ==
           WM_SETTINGS_CONTROL_ITEM_1);

    WmSettingsProjection projection = wm_settings_scene_projection(scene);
    drawn_quad_count = 0;
    wm_texture_cache_begin_frame(textures);
    wm_font_cache_begin_frame(fonts);
    assert(wm_settings_scene_draw(scene));
    WmQuad art = {0};
    WmQuad marker = {0};
    assert(count_drawn_texture(standard, &art) == 1);
    assert(art.x == 64.0f && art.width == 200.0f);
    assert(count_drawn_texture(widescreen, &art) == 1);
    assert(art.x == 312.0f && art.width == 264.0f);
    assert(count_drawn_texture(marker_left, &marker) == 1);
    assert(marker.x == 302.0f && marker.y == 136.0f);
    assert(count_drawn_texture(marker_right, &marker) == 1);
    assert(marker.x == 562.0f);

    assert(wm_settings_scene_activate(scene, WM_SETTINGS_CONTROL_ITEM_1));
    assert(wm_settings_scene_snapshot(scene).selection == 0);
    assert(wm_settings_scene_snapshot(scene).page_opacity == 1.0f);
    assert(wm_settings_scene_projection(scene).document_width ==
           projection.document_width);
    drawn_quad_count = 0;
    wm_texture_cache_begin_frame(textures);
    wm_font_cache_begin_frame(fonts);
    assert(wm_settings_scene_draw(scene));
    assert(count_drawn_texture(marker_left, &marker) == 1);
    assert(marker.x == 54.0f && marker.y == 136.0f);
    assert(count_drawn_texture(marker_right, &marker) == 1);
    assert(marker.x == 250.0f);

    assert(wm_settings_scene_back(scene));
    wm_settings_scene_advance(scene, 20.0f);
    assert(wm_settings_scene_activate(scene, WM_SETTINGS_CONTROL_ITEM_2));
    assert(wm_settings_scene_snapshot(scene).selection == 0);
    assert(wm_settings_scene_activate(scene, WM_SETTINGS_CONTROL_ITEM_2));
    assert(wm_settings_scene_snapshot(scene).selection == 1);
    assert(wm_settings_scene_activate(scene, WM_SETTINGS_CONTROL_NEXT));
    wm_settings_scene_advance(scene, 20.0f);
    assert(wm_settings_scene_activate(scene, WM_SETTINGS_CONTROL_ITEM_2));
    assert(wm_settings_scene_snapshot(scene).selection == 1);
    wm_settings_scene_destroy(scene);
    wm_font_cache_destroy(fonts);
    wm_texture_cache_destroy(textures);
}

static void test_display_two_row_back_retains_choice(
    const char *assets, unsigned detail) {
    assert(detail == 3 || detail == 4);
    WmTextureCache *textures = wm_texture_cache_create(
        (WmPlatform *)1, assets, 64u * 1024u * 1024u);
    WmFontCache *fonts = wm_font_cache_create(
        (WmPlatform *)1, assets, 16u * 1024u * 1024u);
    assert(textures && fonts);
    uint32_t row = 0;
    uint32_t marker_left = 0;
    uint32_t marker_right = 0;
    assert(wm_texture_cache_resolve(
        textures, "textures/settings_html/large-row.png", &row));
    assert(wm_texture_cache_resolve(
        textures, "textures/settings_html/choice-left.png",
        &marker_left));
    assert(wm_texture_cache_resolve(
        textures, "textures/settings_html/choice-right.png",
        &marker_right));

    WmSettingsScene *scene = wm_settings_scene_create(
        (WmPlatform *)1, assets, textures, fonts);
    assert(scene);
    assert(wm_settings_scene_open(scene));
    wm_settings_scene_advance(scene, 21.0f);
    assert(wm_settings_scene_activate(scene, WM_SETTINGS_CONTROL_ITEM_3));
    wm_settings_scene_advance(scene, 20.0f);
    WmSettingsControl detail_control = detail == 3
        ? WM_SETTINGS_CONTROL_ITEM_3 : WM_SETTINGS_CONTROL_ITEM_4;
    assert(wm_settings_scene_activate(scene, detail_control));
    wm_settings_scene_advance(scene, 20.0f);
    assert(wm_settings_scene_snapshot(scene).detail == detail);
    assert(wm_settings_scene_snapshot(scene).selection == 0);

    drawn_quad_count = 0;
    wm_texture_cache_begin_frame(textures);
    wm_font_cache_begin_frame(fonts);
    assert(wm_settings_scene_draw(scene));
    WmQuad marker = {0};
    assert(count_drawn_texture(row, NULL) == 2);
    assert(count_drawn_texture(marker_left, &marker) == 1);
    assert(marker.x == 110.0f && marker.y == 120.0f);
    assert(count_drawn_texture(marker_right, NULL) == 1);

    assert(wm_settings_scene_activate(scene, WM_SETTINGS_CONTROL_ITEM_2));
    assert(wm_settings_scene_snapshot(scene).selection == 1);
    assert(wm_settings_scene_snapshot(scene).page_opacity == 1.0f);
    drawn_quad_count = 0;
    wm_texture_cache_begin_frame(textures);
    wm_font_cache_begin_frame(fonts);
    assert(wm_settings_scene_draw(scene));
    assert(count_drawn_texture(marker_left, &marker) == 1);
    assert(marker.y == 216.0f);

    /* Both extracted controllers write the value during row selection.
     * Back cancels the page navigation without restoring that local value. */
    assert(wm_settings_scene_back(scene));
    wm_settings_scene_advance(scene, 20.0f);
    assert(wm_settings_scene_activate(scene, detail_control));
    assert(wm_settings_scene_snapshot(scene).selection == 1);
    wm_settings_scene_destroy(scene);
    wm_font_cache_destroy(fonts);
    wm_texture_cache_destroy(textures);
}

static void test_tv_resolution_back_retains_row(const char *assets) {
    test_display_two_row_back_retains_choice(assets, 3);
}

static void test_burn_in_back_retains_row(const char *assets) {
    test_display_two_row_back_retains_choice(assets, 4);
}

typedef struct SlideDistance {
    float x;
    bool found;
} SlideDistance;

static bool collect_slide(void *context, const WmLayoutPaneView *pane) {
    SlideDistance *distance = context;
    if (strcmp(pane->name, "N_Tra0") == 0) {
        distance->x = pane->matrix[3];
        distance->found = true;
    }
    return true;
}

static void test_wad_scroll_curve(const char *assets) {
    char path[4096];
    int length = snprintf(path, sizeof(path),
                          "%s/layouts/setting/SceenChange_b.json", assets);
    assert(length > 0 && length < (int)sizeof(path));
    WmLayout *layout = wm_layout_load_json(path, NULL, 0);
    assert(layout);
    for (int direction = 0; direction < 2; direction++) {
        WmLayoutClip clip = {
            .animation = direction ? "SceenChange_b_Left"
                                   : "SceenChange_b_Right",
            .frame = 25.0f,
            .loop_override = 0
        };
        assert(wm_layout_pose(layout, &clip, 1));
        SlideDistance distance = {0};
        wm_layout_visit_all_transforms(layout, false, WM_LAYOUT_LOCAL,
                                        NULL, collect_slide, &distance);
        assert(distance.found);
        assert(fabsf(fabsf(distance.x) - 477.0f) < 0.1f);
    }
    wm_layout_destroy(layout);
}

static void test_direct_internet_entry(const char *assets) {
    WmSettingsScene *scene = wm_settings_scene_create(
        (WmPlatform *)1, assets, (WmTextureCache *)1,
        (WmFontCache *)1);
    assert(scene && wm_settings_scene_open_internet(scene));
    WmSettingsSnapshot entry = wm_settings_scene_snapshot(scene);
    assert(entry.page == 2 && entry.category == 7 && entry.detail == 0);
    wm_settings_scene_advance(scene, 21.0f);
    assert(wm_settings_scene_back(scene));
    assert(wm_settings_scene_take_exit(scene));
    assert(!wm_settings_scene_take_exit(scene));
    assert(wm_settings_scene_open(scene));
    assert(wm_settings_scene_snapshot(scene).category == 0);
    wm_settings_scene_destroy(scene);
}

int main(int argc, char **argv) {
    test_source_hit_regions();
    const char *assets = argc > 1 ? argv[1] : ".local/native-assets";
    char path[4096];
    int length = snprintf(path, sizeof(path),
                          "%s/layouts/setting/SceenChange_b.json", assets);
    assert(length > 0 && length < (int)sizeof(path));
    FILE *file = fopen(path, "rb");
    if (!file) {
        puts("Wii Settings WAD curve test skipped: local export absent.");
        return 0;
    }
    fclose(file);
    test_wide_projection(assets);
    test_wide_render(assets);
    test_initial_page_fades_as_one_raster(assets);
    test_page_crossfade_keeps_side_panels_opaque(assets);
    test_calendar_arrow_rollover(assets);
    test_country_source_art(assets);
    test_sensitivity_source_art(assets);
    test_connection_settings_split_rows(assets);
    test_empty_connection_choice_page(assets);
    test_initial_connection_mode_pages(assets);
    test_offline_access_point_search(assets);
    test_offline_usb_connector_pages(assets);
    test_user_agreements_button_order(assets);
    test_resolution_source_default(assets);
    test_language_selection_page(assets);
    test_screen_language_artwork(assets);
    test_index_navigation(assets);
    test_index_source_badges_and_scroll(assets);
    test_immediate_index_hover_art(assets);
    test_hover_image_swap(assets);
    test_choice_and_detail_navigation(assets);
    test_calendar_and_sensitivity(assets);
    test_extended_categories(assets);
    test_console_information_placeholders(assets);
    test_update_initial_footer(assets);
    test_format_red_action_rollover(assets);
    test_connect24_onoff_immediate_state(assets);
    test_sensor_position_source_mapping(assets);
    test_screen_position_back_retains_offset(assets);
    test_widescreen_back_retains_row(assets);
    test_tv_resolution_back_retains_row(assets);
    test_burn_in_back_retains_row(assets);
    test_wad_scroll_curve(assets);
    test_direct_internet_entry(assets);
    puts("Wii Settings index navigation and source geometry passed.");
    return 0;
}
