#include "wii_menu/scenes/resource_scene.h"
#include "wii_menu/board/board_scene.h"
#include "wii_menu/input/channel_drag.h"
#include "wii_menu/input/pointer.h"

#ifdef NDEBUG
#undef NDEBUG
#endif
#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <string.h>

typedef struct ArrowSample {
    unsigned quads;
    float left;
    float right;
    float top;
    float bottom;
} ArrowSample;

static uint32_t next_texture = 1;
static uint32_t right_arrow_texture;
static uint32_t left_arrow_texture;
static uint32_t grab_overlay_texture;
static ArrowSample drawn;
static unsigned draw_event;
static unsigned last_arrow_event;
static unsigned grab_pointer_event;
static bool capture_arrow_centers;
static float arrow_centers[2];
static unsigned arrow_center_counts[2];

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

void wm_platform_draw_vertices(WmPlatform *platform, const WmDrawVertex vertices[4],
                               uint32_t texture) {
    (void)platform;
    (void)vertices;
    (void)texture;
}

void wm_platform_prepare_material(WmPlatform *platform, const WmMaterialQuad *quad) {
    (void)platform;
    (void)quad;
}

void wm_platform_draw_material_quad(WmPlatform *platform, const WmMaterialQuad *quad) {
    (void)platform;
    draw_event++;
    if (quad->texture_count >= 2 && quad->textures[1] == grab_overlay_texture)
        grab_pointer_event = draw_event;
    if (!quad->texture_count ||
        (quad->textures[0] != right_arrow_texture &&
         (!capture_arrow_centers || quad->textures[0] != left_arrow_texture)) ||
        quad->vertices[0].color.a <= 0.01f)
        return;
    if (capture_arrow_centers) {
        float center_x = 0.0f;
        for (size_t index = 0; index < 4; index++)
            center_x += quad->vertices[index].x * 0.25f;
        size_t side = center_x < WM_FRAME_WIDTH * 0.5f ? 0 : 1;
        arrow_centers[side] += center_x;
        arrow_center_counts[side]++;
    }
    last_arrow_event = draw_event;
    drawn.quads++;
    for (size_t index = 0; index < 4; index++) {
        float x = quad->vertices[index].x;
        float y = quad->vertices[index].y;
        drawn.left = fminf(drawn.left, x);
        drawn.right = fmaxf(drawn.right, x);
        drawn.top = fminf(drawn.top, y);
        drawn.bottom = fmaxf(drawn.bottom, y);
    }
}

uint32_t wm_platform_create_texture(WmPlatform *platform, int width, int height,
                                    const uint8_t *rgba) {
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

static ArrowSample sample(WmResourceScene *scene, const WmMenu *menu, float seconds) {
    drawn = (ArrowSample){
        .left = INFINITY, .right = -INFINITY, .top = INFINITY, .bottom = -INFINITY};
    const WmResourceSceneFrame frame = {.elapsed_seconds = seconds,
                                        .hover = {WM_HIT_NONE, -1}};
    wm_resource_scene_draw(scene, menu, &frame);
    return drawn;
}

static float width(ArrowSample sample) {
    return sample.right - sample.left;
}

static float height(ArrowSample sample) {
    return sample.bottom - sample.top;
}

static void begin_arrow_center_capture(void) {
    capture_arrow_centers = true;
    memset(arrow_centers, 0, sizeof(arrow_centers));
    memset(arrow_center_counts, 0, sizeof(arrow_center_counts));
}

static void end_arrow_center_capture(float centers[2]) {
    capture_arrow_centers = false;
    for (size_t side = 0; side < 2; side++) {
        assert(arrow_center_counts[side] > 0);
        centers[side] = arrow_centers[side] / (float)arrow_center_counts[side];
    }
}

static void check_board_return_arrows(WmPlatform *platform, const char *assets,
                                      WmResourceScene *grid, WmTextureCache *textures,
                                      WmFontCache *fonts) {
    WmBoardScene *board = wm_board_scene_create(platform, assets, textures, fonts);
    assert(board);
    WmMenu menu;
    wm_menu_init(&menu);
    menu.page = 1;
    wm_resource_scene_restart(grid);
    float seconds = 3.0f;
    const WmResourceSceneFrame grid_frame = {.hover = {WM_HIT_NONE, -1}};
    for (unsigned cycle = 0; cycle < 3; cycle++) {
        WmResourceSceneFrame frame = grid_frame;
        frame.elapsed_seconds = seconds;
        begin_arrow_center_capture();
        wm_resource_scene_draw(grid, &menu, &frame);
        float before[2];
        end_arrow_center_capture(before);

        assert(wm_menu_open_screen(&menu, WM_SCREEN_BOARD));
        assert(wm_menu_switch_screen_at_black(&menu, WM_SCREEN_BOARD));
        wm_board_scene_set_grid_page(board, menu.page);
        assert(wm_board_scene_open(board, (WmBoardDate){2026, 9, 25}));
        wm_board_scene_advance(board, 40.0f + (float)(cycle * 17));
        assert(wm_board_scene_back(board));
        wm_board_scene_advance(board, 39.0f);
        seconds += (79.0f + (float)(cycle * 17)) / 60.0f;
        begin_arrow_center_capture();
        wm_board_scene_set_menu_elapsed_seconds(board, seconds);
        wm_board_scene_draw_footer(board);
        float departing[2];
        end_arrow_center_capture(departing);

        wm_board_scene_advance(board, 1.0f);
        seconds += 1.0f / 60.0f;
        assert(wm_board_scene_phase(board) == WM_BOARD_CLOSED);
        assert(wm_menu_switch_screen_at_black(&menu, WM_SCREEN_GRID));
        frame.elapsed_seconds = seconds;
        begin_arrow_center_capture();
        wm_resource_scene_draw(grid, &menu, &frame);
        float returned[2];
        end_arrow_center_capture(returned);
        for (size_t side = 0; side < 2; side++) {
            assert(isfinite(before[side]));
            /* One grid tick may advance the loop, but returning from Board
             * must not replace its current phase with a different clock. */
            assert(fabsf(departing[side] - returned[side]) < 0.6f);
        }
    }
    wm_board_scene_destroy(board);
}

int main(int argc, char **argv) {
    const char *assets = argc > 1 ? argv[1] : ".local/native-assets";
    WmPlatform *platform = (WmPlatform *)1;
    WmMenu menu;
    wm_menu_init(&menu);
    if (!wm_catalog_load(&menu, assets)) {
        puts("Footer arrow transition test skipped: prepared assets unavailable.");
        return 0;
    }
    WmTextureCache *textures =
        wm_texture_cache_create(platform, assets, 128u * 1024u * 1024u);
    WmFontCache *fonts = wm_font_cache_create(platform, assets, 16u * 1024u * 1024u);
    assert(textures && fonts);
    assert(wm_texture_cache_resolve(textures, "textures/cmnBtn/my_arw_a.png",
                                    &right_arrow_texture));
    assert(wm_texture_cache_resolve(textures, "textures/cmnBtn/my_arw_b.png",
                                    &left_arrow_texture));
    WmResourceScene *scene =
        wm_resource_scene_create(platform, assets, &menu, textures, fonts);
    assert(scene);

    ArrowSample settled = sample(scene, &menu, 0.0f);
    assert(settled.quads == 1);
    assert(wm_menu_select(&menu, 0));
    ArrowSample start = sample(scene, &menu, 0.0f);
    assert(start.quads == 1);
    wm_menu_tick(&menu, 5.0f / 60.0f);
    ArrowSample middle = sample(scene, &menu, 5.0f / 60.0f);
    assert(middle.quads == 1);
    wm_menu_tick(&menu, 5.0f / 60.0f);
    ArrowSample end = sample(scene, &menu, 10.0f / 60.0f);
    assert(end.quads == 1);

    /* G_ArwR_End travels outward under the full-screen IPL projection.
     * The channel camera must never scale the common footer arrow. */
    assert(fabsf(width(start) - width(settled)) < 0.25f);
    assert(fabsf(height(start) - height(settled)) < 0.25f);
    assert(fabsf(width(middle) - width(settled)) < 0.25f);
    assert(fabsf(height(middle) - height(settled)) < 0.25f);
    assert(fabsf(width(end) - width(settled)) < 0.25f);
    assert(fabsf(height(end) - height(settled)) < 0.25f);
    assert(middle.left > start.left);
    assert(end.left > middle.left);

    /* The channel stays beneath the footer, while its grabbed hand is the
     * final overlay even when an arrow owns the pointer area. */
    wm_menu_init(&menu);
    strcpy(menu.slots[1].id, "local-channel");
    menu.slots[1].occupied = true;
    wm_resource_scene_restart(scene);
    WmPointer *pointer = wm_pointer_create(platform, assets, textures);
    WmChannelDrag *drag =
        wm_channel_drag_create(platform, assets, textures, fonts, true);
    assert(pointer && drag);
    assert(wm_texture_cache_resolve(textures, "textures/cursor/defcursor_final64_b.png",
                                    &grab_overlay_texture));
    assert(wm_channel_drag_start(drag, 1, &menu, 605.0f, 200.0f));
    wm_pointer_move(pointer, 605.0f, 200.0f);
    wm_pointer_set_grabbed(pointer, true);
    draw_event = last_arrow_event = grab_pointer_event = 0;
    const WmResourceSceneFrame drag_frame = {.elapsed_seconds = 0.0f,
                                             .hover = {WM_HIT_PAGE_NEXT, -1},
                                             .pointer = pointer,
                                             .drag = drag};
    wm_resource_scene_draw(scene, &menu, &drag_frame);
    assert(last_arrow_event > 0);
    assert(grab_pointer_event > last_arrow_event);
    wm_channel_drag_destroy(drag);
    wm_pointer_destroy(pointer);

    check_board_return_arrows(platform, assets, scene, textures, fonts);

    wm_resource_scene_destroy(scene);
    wm_font_cache_destroy(fonts);
    wm_texture_cache_destroy(textures);
    puts("Home arrows retain source size; grab hand tops the footer.");
    return 0;
}
