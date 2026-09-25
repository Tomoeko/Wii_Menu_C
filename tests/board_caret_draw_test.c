#include "wii_menu/board_compose.h"
#include "wii_menu/font_cache.h"
#include "wii_menu/texture_cache.h"

#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <string.h>

static unsigned next_texture = 1;
static unsigned caret_count;
static WmDrawVertex caret_vertices[4];
static unsigned composed_red_glyphs;
static unsigned completion_gray_glyphs;
static unsigned completion_green_glyphs;

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
    if (texture != 0) {
        const WmColor color = vertices[0].color;
        if (fabsf(color.r - 1.0f) < 0.01f &&
            fabsf(color.g - 50.0f / 255.0f) < 0.01f &&
            fabsf(color.b - 50.0f / 255.0f) < 0.01f)
            composed_red_glyphs++;
        if (fabsf(color.r - 192.0f / 255.0f) < 0.01f &&
            fabsf(color.g - 192.0f / 255.0f) < 0.01f &&
            fabsf(color.b - 192.0f / 255.0f) < 0.01f)
            completion_gray_glyphs++;
        if (fabsf(color.r - 50.0f / 255.0f) < 0.01f &&
            fabsf(color.g - 100.0f / 255.0f) < 0.01f &&
            fabsf(color.b - 50.0f / 255.0f) < 0.01f)
            completion_green_glyphs++;
    }
    if (texture != 0 || vertices[0].color.r != 1.0f ||
        fabsf(vertices[0].color.g - 50.0f / 255.0f) > 0.0001f ||
        fabsf(vertices[0].color.b - 50.0f / 255.0f) > 0.0001f) {
        return;
    }
    caret_count++;
    memcpy(caret_vertices, vertices, sizeof(caret_vertices));
}

void wm_platform_draw_material_quad(WmPlatform *platform,
                                    const WmMaterialQuad *quad) {
    (void)platform;
    (void)quad;
}

void wm_platform_prepare_material(WmPlatform *platform,
                                  const WmMaterialQuad *quad) {
    (void)platform;
    (void)quad;
}

uint32_t wm_platform_create_texture(WmPlatform *platform, int width,
                                    int height, const uint8_t *rgba) {
    (void)platform;
    assert(width > 0 && height > 0 && rgba);
    return next_texture++;
}

void wm_platform_destroy_texture(WmPlatform *platform, uint32_t texture) {
    (void)platform;
    (void)texture;
}

int main(int argc, char **argv) {
    const char *assets = argc > 1 ? argv[1] : ".local/native-assets";
    char path[4096];
    int length = snprintf(path, sizeof(path),
                          "%s/layouts/sofkeybd/my_Memo_a.json", assets);
    assert(length > 0 && length < (int)sizeof(path));
    FILE *check = fopen(path, "rb");
    if (!check) {
        puts("Memo caret render test skipped: local WAD export absent.");
        return 0;
    }
    fclose(check);

    WmTextureCache *textures = wm_texture_cache_create(
        (WmPlatform *)1, assets, 64u * 1024u * 1024u);
    WmFontCache *fonts = wm_font_cache_create(
        (WmPlatform *)1, assets, 8u * 1024u * 1024u);
    assert(textures && fonts);
    WmBoardCompose *compose = wm_board_compose_create(
        (WmPlatform *)1, assets, textures, fonts);
    assert(compose);
    assert(wm_board_compose_open(compose));
    wm_board_compose_advance(compose, 39.0f);
    assert(wm_board_compose_activate(compose, WM_COMPOSE_CONTROL_MEMO));
    wm_board_compose_advance(compose, 26.0f);
    assert(wm_board_compose_activate(compose, WM_COMPOSE_CONTROL_EDIT));

    caret_count = 0;
    wm_board_compose_draw(compose);
    assert(caret_count == 1);
    const float source_width = (float)(14592 / 832) / 6.0f;
    /* N_Memo's widescreen-cancel flag removes the root 832/608 stretch. */
    const float projected_width = source_width * 640.0f / 832.0f;
    assert(fabsf(caret_vertices[1].x - caret_vertices[0].x -
                 projected_width) < 0.02f);
    assert(fabsf(caret_vertices[0].color.a - 127.0f / 255.0f) < 0.01f);
    float first_x = caret_vertices[0].x;

    wm_board_compose_advance(compose, 30.0f);
    caret_count = 0;
    wm_board_compose_draw(compose);
    assert(caret_count == 1);
    assert(caret_vertices[0].color.a < 0.1f);
    assert(wm_board_compose_insert_text(compose, "Wi"));
    caret_count = 0;
    wm_board_compose_draw(compose);
    assert(caret_count == 1);
    assert(caret_vertices[0].x > first_x);
    assert(fabsf(caret_vertices[0].color.a - 127.0f / 255.0f) < 0.01f);
    float first_line_y = caret_vertices[0].y;

    assert(wm_board_compose_insert_text(compose, "\n"));
    caret_count = 0;
    wm_board_compose_draw(compose);
    assert(caret_count == 1);
    assert(caret_vertices[0].y > first_line_y);
    assert(wm_board_compose_finish_edit(compose));
    caret_count = 0;
    wm_board_compose_draw(compose);
    assert(caret_count == 0);

    wm_board_compose_destroy(compose);

    compose = wm_board_compose_create((WmPlatform *)1, assets,
                                      textures, fonts);
    assert(compose && wm_board_compose_open(compose));
    wm_board_compose_advance(compose, 39.0f);
    assert(wm_board_compose_activate(compose, WM_COMPOSE_CONTROL_MEMO));
    wm_board_compose_advance(compose, 26.0f);
    assert(wm_board_compose_activate(compose, WM_COMPOSE_CONTROL_EDIT));
    wm_board_compose_advance(compose, 30.0f);
    WmBoardComposeControl prediction = (WmBoardComposeControl)(
        WM_COMPOSE_CONTROL_KEY_FIRST + WM_KEYBOARD_PREDICTION - 1);
    assert(wm_board_compose_activate(compose, prediction));
    assert(wm_board_compose_insert_text(compose, "hel"));
    composed_red_glyphs = completion_gray_glyphs = 0;
    wm_board_compose_draw(compose);
    assert(composed_red_glyphs >= 3);
    assert(completion_gray_glyphs > 0);
    WmBoardComposeControl candidate = (WmBoardComposeControl)(
        WM_COMPOSE_CONTROL_KEY_FIRST + WM_KEYBOARD_CANDIDATE_FIRST - 1);
    wm_board_compose_hover(compose, candidate);
    completion_green_glyphs = 0;
    wm_board_compose_draw(compose);
    assert(completion_green_glyphs > 0);
    assert(wm_board_compose_activate(compose, candidate));
    composed_red_glyphs = 0;
    wm_board_compose_draw(compose);
    assert(composed_red_glyphs == 0);
    wm_board_compose_destroy(compose);

    wm_font_cache_destroy(fonts);
    wm_texture_cache_destroy(textures);
    puts("Memo caret render commands passed.");
    return 0;
}
