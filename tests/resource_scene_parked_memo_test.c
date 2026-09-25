#define _POSIX_C_SOURCE 200809L

#include "wii_menu/board_scene.h"
#include "wii_menu/resource_scene.h"

#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <string.h>

static uint32_t next_texture = 1;
static uint32_t memo_texture;
static uint32_t grid_texture;
static unsigned draw_number;
static unsigned first_memo_draw;
static unsigned first_grid_draw;
static WmMaterialVertex first_memo_vertices[4];

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
    draw_number++;
    if (!quad->texture_count) return;
    if (quad->textures[0] == memo_texture && !first_memo_draw) {
        first_memo_draw = draw_number;
        memcpy(first_memo_vertices, quad->vertices,
               sizeof(first_memo_vertices));
    }
    if (quad->textures[0] == grid_texture && !first_grid_draw)
        first_grid_draw = draw_number;
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

static void clear_draws(void) {
    draw_number = 0;
    first_memo_draw = 0;
    first_grid_draw = 0;
    memset(first_memo_vertices, 0, sizeof(first_memo_vertices));
}

static float quad_width(const WmMaterialVertex vertices[4]) {
    float minimum = vertices[0].x;
    float maximum = minimum;
    for (size_t index = 1; index < 4; index++) {
        minimum = fminf(minimum, vertices[index].x);
        maximum = fmaxf(maximum, vertices[index].x);
    }
    return maximum - minimum;
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
    if (!wm_texture_cache_resolve(textures,
                                  "textures/board/my_LetterS_a.png",
                                  &memo_texture)) {
        puts("Parked Memo render test skipped: local WAD export absent.");
        wm_font_cache_destroy(fonts);
        wm_texture_cache_destroy(textures);
        return 0;
    }
    assert(wm_texture_cache_resolve(textures,
                                    "textures/chanSel/my_TV_c_p0.png",
                                    &grid_texture));
    WmBoardScene *board = wm_board_scene_create(platform, assets,
                                                textures, fonts);
    WmResourceScene *grid = wm_resource_scene_create(platform, assets,
                                                    &menu, textures, fonts);
    assert(board && grid);
    WmBoardDate today = wm_board_scene_date(board);
    WmBoardMemo memo = {
        .id = "parked",
        .text = "Memo",
        .date = today,
        .has_position = true,
        .x = -220.0f,
        .y = -65.0f
    };
    assert(wm_board_scene_set_memos(board, &memo, 1));
    WmResourceSceneFrame frame = {
        .hover = {WM_HIT_NONE, -1},
        .board_scene = board
    };

    clear_draws();
    wm_resource_scene_draw(grid, &menu, &frame);
    assert(first_memo_draw > 0 && first_grid_draw > first_memo_draw);

    /* Use an uncut card for the scale measurement. The corner card above
     * deliberately intersects the screen clip during the grid draw. */
    memo.x = 0.0f;
    memo.y = 53.0f;
    assert(wm_board_scene_set_memos(board, &memo, 1));
    clear_draws();
    wm_board_scene_draw_parked_memos(board, today, NULL);
    assert(first_memo_draw > 0);
    WmMaterialVertex settled[4];
    memcpy(settled, first_memo_vertices, sizeof(settled));

    const float camera[12] = {
        1.25f, 0.0f, 0.0f, 32.0f,
        0.0f, 1.25f, 0.0f, -12.0f,
        0.0f, 0.0f, 1.0f, 0.0f
    };
    clear_draws();
    wm_board_scene_draw_parked_memos(board, today, camera);
    assert(first_memo_draw > 0);
    assert(fabsf(quad_width(first_memo_vertices) /
                 quad_width(settled) - 1.25f) < 0.01f);
    assert(fabsf(first_memo_vertices[0].x - settled[0].x) > 1.0f);

    assert(wm_board_scene_open(board, today));
    wm_board_scene_advance(board, 40.0f);
    wm_board_scene_hover(board,
                         (WmBoardHit){WM_BOARD_CONTROL_MEMO, 0});
    wm_board_scene_advance(board, 6.0f);
    assert(wm_board_scene_back(board));
    wm_board_scene_advance(board, 39.0f);
    clear_draws();
    wm_board_scene_draw_body(board);
    assert(first_memo_draw > 0);
    WmMaterialVertex before_handoff[4];
    memcpy(before_handoff, first_memo_vertices, sizeof(before_handoff));

    wm_board_scene_advance(board, 1.0f);
    assert(wm_board_scene_phase(board) == WM_BOARD_CLOSED);
    clear_draws();
    wm_board_scene_draw_parked_memos(board, today, NULL);
    assert(first_memo_draw > 0);
    for (size_t index = 0; index < 4; index++) {
        assert(fabsf(first_memo_vertices[index].x -
                     before_handoff[index].x) < 0.01f);
        assert(fabsf(first_memo_vertices[index].y -
                     before_handoff[index].y) < 0.01f);
    }

    /* A Memo for today must already be drawn while the Board slides home
     * from another date. It must remain settled after the handoff. */
    WmBoardDate yesterday;
    assert(wm_board_date_shift(today, -1, &yesterday));
    memo.x = -220.0f;
    memo.y = -65.0f;
    assert(wm_board_scene_set_memos(board, &memo, 1));
    assert(wm_board_scene_open(board, today));
    wm_board_scene_advance(board, 40.0f);
    assert(wm_board_scene_activate(board,
        (WmBoardHit){WM_BOARD_CONTROL_PREVIOUS, SIZE_MAX}));
    wm_board_scene_advance(board, 20.0f);
    WmBoardDate selected = wm_board_scene_date(board);
    assert(selected.year == yesterday.year &&
           selected.month == yesterday.month &&
           selected.day == yesterday.day);
    assert(wm_board_scene_back(board));
    wm_board_scene_advance(board, 10.0f);
    clear_draws();
    wm_board_scene_draw_body(board);
    assert(first_memo_draw > 0);
    assert(first_memo_vertices[0].color.a > 0.0f);
    float midway_x = first_memo_vertices[0].x;
    wm_board_scene_advance(board, 10.0f);
    clear_draws();
    wm_board_scene_draw_body(board);
    assert(first_memo_draw > 0);
    assert(first_memo_vertices[0].color.a > 0.0f);
    assert(fabsf(first_memo_vertices[0].x - midway_x) > 1.0f);
    wm_board_scene_advance(board, 19.0f);
    clear_draws();
    wm_board_scene_draw_body(board);
    assert(first_memo_draw > 0);
    assert(first_memo_vertices[0].color.a > 0.0f);
    wm_board_scene_advance(board, 1.0f);
    assert(wm_board_scene_phase(board) == WM_BOARD_CLOSED);
    clear_draws();
    wm_board_scene_draw_parked_memos(board, today, NULL);
    assert(first_memo_draw > 0);

    wm_resource_scene_destroy(grid);
    wm_board_scene_destroy(board);
    wm_font_cache_destroy(fonts);
    wm_texture_cache_destroy(textures);
    puts("Parked Memo draw order and shared camera passed.");
    return 0;
}
