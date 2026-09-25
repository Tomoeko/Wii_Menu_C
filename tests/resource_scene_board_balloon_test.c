#include "wii_menu/resource_scene.h"

#ifdef NDEBUG
#undef NDEBUG
#endif
#include <assert.h>
#include <math.h>
#include <stdio.h>

static uint32_t next_texture = 1;
static uint32_t balloon_texture;
static unsigned balloon_quads;
static float balloon_left;
static float balloon_right;
static float balloon_top;
static float balloon_bottom;
static float balloon_alpha;

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
    if (quad->texture_count > 0 &&
        quad->textures[0] == balloon_texture &&
        quad->vertices[0].color.a > 0.01f) {
        balloon_quads++;
        balloon_alpha = fmaxf(balloon_alpha, quad->vertices[0].color.a);
        for (size_t index = 0; index < 4; index++) {
            const WmMaterialVertex *vertex = &quad->vertices[index];
            balloon_left = fminf(balloon_left, vertex->x);
            balloon_right = fmaxf(balloon_right, vertex->x);
            balloon_top = fminf(balloon_top, vertex->y);
            balloon_bottom = fmaxf(balloon_bottom, vertex->y);
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

static void sample(WmResourceScene *grid, WmBoardScene *board,
                   WmBoardControl control, float seconds,
                   bool visible, bool cue) {
    balloon_quads = 0;
    balloon_left = INFINITY;
    balloon_right = -INFINITY;
    balloon_top = INFINITY;
    balloon_bottom = -INFINITY;
    balloon_alpha = 0.0f;
    wm_board_scene_draw_footer(board);
    wm_resource_scene_draw_board_balloons(grid, board,
        (WmBoardHit){control, 0}, seconds);
    assert((balloon_quads > 0) == visible);
    assert(wm_resource_scene_take_balloon_sound(grid) == cue);
}

static void expect_position(WmBoardScene *board, WmBoardControl control) {
    float anchor_x, anchor_y;
    assert(wm_board_scene_footer_button_anchor(
        board, control, &anchor_x, &anchor_y));
    float width = (balloon_right - balloon_left) * 832.0f / 640.0f;
    float x = fmaxf(-416.0f + 120.0f + width * 0.5f,
                    fminf(416.0f - 120.0f - width * 0.5f,
                          anchor_x));
    float expected_x = 320.0f + x * 640.0f / 608.0f;
    float expected_y = 228.0f - (anchor_y + 50.0f);
    if (fabsf((balloon_left + balloon_right) * 0.5f - expected_x) >= 2.0f ||
        fabsf((balloon_top + balloon_bottom) * 0.5f - expected_y) >= 2.0f)
        fprintf(stderr, "Board balloon %d: observed %.2f, %.2f; expected %.2f, %.2f; anchor %.2f, %.2f; window %.2f; alpha %.2f\n",
                control, (balloon_left + balloon_right) * 0.5f,
                (balloon_top + balloon_bottom) * 0.5f,
                expected_x, expected_y, anchor_x, anchor_y, width,
                balloon_alpha);
    assert(fabsf((balloon_left + balloon_right) * 0.5f - expected_x) < 2.0f);
    assert(fabsf((balloon_top + balloon_bottom) * 0.5f - expected_y) < 2.0f);
}

int main(int argc, char **argv) {
    const char *assets = argc > 1 ? argv[1] : ".local/native-assets";
    WmPlatform *platform = (WmPlatform *)1;
    WmMenu menu;
    wm_menu_init(&menu);
    if (!wm_catalog_load(&menu, assets)) {
        puts("Board balloon comparison skipped: prepared assets unavailable.");
        return 0;
    }
    WmTextureCache *textures = wm_texture_cache_create(
        platform, assets, 128u * 1024u * 1024u);
    WmFontCache *fonts = wm_font_cache_create(
        platform, assets, 16u * 1024u * 1024u);
    assert(textures && fonts);
    assert(wm_texture_cache_resolve(textures,
        "textures/balloon/my_Balloon_a.png", &balloon_texture));
    WmResourceScene *grid = wm_resource_scene_create(
        platform, assets, &menu, textures, fonts);
    WmBoardScene *board = wm_board_scene_create(
        platform, assets, textures, fonts);
    assert(grid && board);
    assert(wm_board_scene_open(board, (WmBoardDate){2024, 5, 1}));
    wm_board_scene_advance(board, 40.0f);
    assert(wm_board_scene_phase(board) == WM_BOARD_READY);

    sample(grid, board, WM_BOARD_CONTROL_NONE, 10.0f, false, false);
    sample(grid, board, WM_BOARD_CONTROL_CALENDAR, 10.2f, false, false);
    sample(grid, board, WM_BOARD_CONTROL_CALENDAR, 10.3f, true, true);
    sample(grid, board, WM_BOARD_CONTROL_CALENDAR, 10.4f, true, false);
    expect_position(board, WM_BOARD_CONTROL_CALENDAR);
    sample(grid, board, WM_BOARD_CONTROL_NONE, 10.45f, true, false);
    sample(grid, board, WM_BOARD_CONTROL_NONE, 10.55f, false, false);
    sample(grid, board, WM_BOARD_CONTROL_BACK, 10.85f, true, true);
    sample(grid, board, WM_BOARD_CONTROL_CREATE, 10.95f, false, false);
    sample(grid, board, WM_BOARD_CONTROL_CREATE, 11.25f, true, true);
    sample(grid, board, WM_BOARD_CONTROL_CREATE, 11.35f, true, false);
    expect_position(board, WM_BOARD_CONTROL_CREATE);

    /* Activating a footer control reverses its text bubble, even when the
     * pointer remains over that same button during the scene transition. */
    wm_resource_scene_dismiss_balloon(grid);
    sample(grid, board, WM_BOARD_CONTROL_CREATE, 11.4f, true, false);
    sample(grid, board, WM_BOARD_CONTROL_CREATE, 11.5f, false, false);
    sample(grid, board, WM_BOARD_CONTROL_CREATE, 11.8f, false, false);

    wm_resource_scene_restart(grid);
    sample(grid, board, WM_BOARD_CONTROL_NONE, 12.0f, false, false);
    sample(grid, board, WM_BOARD_CONTROL_BACK, 12.0f, false, false);
    sample(grid, board, WM_BOARD_CONTROL_BACK, 12.3f, true, true);
    sample(grid, board, WM_BOARD_CONTROL_BACK, 12.4f, true, false);
    expect_position(board, WM_BOARD_CONTROL_BACK);

    wm_board_scene_destroy(board);
    wm_resource_scene_destroy(grid);
    wm_texture_cache_destroy(textures);
    wm_font_cache_destroy(fonts);
    puts("Board footer balloons use shared source timing and anchors.");
    return 0;
}
