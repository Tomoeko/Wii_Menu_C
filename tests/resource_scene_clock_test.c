#include "wii_menu/resource_scene.h"
#include "wii_menu/board_scene.h"
#include "wii_menu/layout_runtime.h"

#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <string.h>
#include <time.h>

enum {
    CLOCK_IMAGE_COUNT = 5,
    MAX_TEST_TEXTURES = 4096
};

typedef struct ClockImage {
    float x;
    float y;
    float width;
    float height;
    float uv[4][2];
    float register_color[2][4];
    uint32_t texture;
    unsigned nonzero_alpha_pixels;
    unsigned texture_count;
    unsigned tev_stage_count;
    bool live;
} ClockImage;

typedef struct ClockSlideSample {
    float x;
    float y;
    float alpha;
} ClockSlideSample;

typedef struct ClockColonSample {
    float x;
    float y;
    float alpha;
} ClockColonSample;

typedef struct DateGlyphBounds {
    unsigned count;
    float left;
    float top;
    float right;
    float bottom;
} DateGlyphBounds;

static ClockImage clock_images[CLOCK_IMAGE_COUNT];
static unsigned clock_image_count;
static unsigned next_texture = 1;
static unsigned texture_alpha_pixels[MAX_TEST_TEXTURES];
static bool texture_live[MAX_TEST_TEXTURES];
static uint32_t traced_clock_texture;
static float traced_clock_y;
static bool capture_slide_clock;
static ClockSlideSample slide_clock[3];
static unsigned slide_clock_count;
static uint32_t clock_colon_texture;
static ClockColonSample clock_colons[3];
static unsigned clock_colon_count;
static bool capture_clock_colon;
static bool capture_date_glyphs;
static DateGlyphBounds date_glyphs;

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
    if (!capture_date_glyphs || vertices[0].x < 200.0f ||
        vertices[0].x > 440.0f || vertices[0].y < 380.0f ||
        vertices[0].y > 420.0f) return;
    date_glyphs.count++;
    for (size_t index = 0; index < 4; index++) {
        date_glyphs.left = fminf(date_glyphs.left, vertices[index].x);
        date_glyphs.top = fminf(date_glyphs.top, vertices[index].y);
        date_glyphs.right = fmaxf(date_glyphs.right, vertices[index].x);
        date_glyphs.bottom = fmaxf(date_glyphs.bottom, vertices[index].y);
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
    const float x = quad->vertices[0].x;
    const float y = quad->vertices[0].y;
    const float width = quad->vertices[1].x - x;
    const float height = quad->vertices[2].y - y;
    if (capture_clock_colon &&
        quad->textures[0] == clock_colon_texture) {
        assert(clock_colon_count < 3);
        clock_colons[clock_colon_count++] = (ClockColonSample){
            .x = x,
            .y = y,
            .alpha = quad->vertices[0].color.a
        };
    }
    if (traced_clock_texture &&
        quad->textures[0] == traced_clock_texture &&
        x > 250.0f && x < 320.0f) {
        traced_clock_y = y;
    }
    if (capture_slide_clock && quad->textures[0] == traced_clock_texture) {
        assert(slide_clock_count < 3);
        slide_clock[slide_clock_count++] = (ClockSlideSample){
            .x = x,
            .y = y,
            .alpha = quad->vertices[0].color.a
        };
    }
    if (x < 280.0f || x > 410.0f || y < 325.0f || y > 375.0f ||
        width < 30.0f || width > 35.0f ||
        height < 25.0f || height > 45.0f) {
        return;
    }
    assert(clock_image_count < CLOCK_IMAGE_COUNT);
    ClockImage *image = &clock_images[clock_image_count++];
    image->x = x;
    image->y = y;
    image->width = width;
    image->height = height;
    image->texture = quad->textures[0];
    image->texture_count = quad->texture_count;
    image->tev_stage_count = quad->tev_stage_count;
    if (image->texture < MAX_TEST_TEXTURES) {
        image->live = texture_live[image->texture];
        image->nonzero_alpha_pixels = texture_alpha_pixels[image->texture];
    }
    for (size_t vertex = 0; vertex < 4; vertex++) {
        image->uv[vertex][0] = quad->vertices[vertex].uv[0][0];
        image->uv[vertex][1] = quad->vertices[vertex].uv[0][1];
    }
    for (size_t color = 0; color < 2; color++) {
        memcpy(image->register_color[color], quad->registers[color],
               sizeof(image->register_color[color]));
    }
}

uint32_t wm_platform_create_texture(WmPlatform *platform, int width,
                                    int height, const uint8_t *rgba) {
    (void)platform;
    assert(width > 0 && height > 0 && rgba);
    assert(next_texture < MAX_TEST_TEXTURES);
    unsigned handle = next_texture++;
    texture_live[handle] = true;
    for (size_t pixel = 0; pixel < (size_t)width * (size_t)height; pixel++) {
        if (rgba[pixel * 4 + 3]) texture_alpha_pixels[handle]++;
    }
    return handle;
}

uint32_t wm_platform_create_render_texture(WmPlatform *platform) {
    (void)platform;
    assert(next_texture < MAX_TEST_TEXTURES);
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
    assert(texture < MAX_TEST_TEXTURES);
    texture_live[texture] = false;
}

static bool odd_wall_second(void) {
    time_t now = time(NULL);
    struct tm local;
    return now != (time_t)-1 && localtime_r(&now, &local) &&
           local.tm_sec % 2 != 0;
}

static bool source_colon_pane(void *context,
                              const WmLayoutPaneView *pane) {
    if (strcmp(pane->name, "ClockTen") == 0) {
        *(float *)context = pane->alpha;
    }
    return true;
}

static float source_colon_alpha(WmLayout *clock_layout, float frame) {
    WmLayoutClip clip = {
        .animation = "my_Clock_a_Min",
        .frame = frame,
        .loop_override = 0,
        .target_name = "ClockTen"
    };
    assert(wm_layout_pose(clock_layout, &clip, 1));
    float alpha = NAN;
    wm_layout_visit_all_transforms(clock_layout, true, WM_LAYOUT_IPL,
                                   NULL, source_colon_pane, &alpha);
    assert(isfinite(alpha));
    return alpha;
}

static void wait_for_wall_phase(int second_parity) {
    struct timespec pause = {.tv_nsec = 20000000L};
    for (unsigned attempt = 0; attempt < 150; attempt++) {
        struct timespec wall_time;
        struct tm local;
        assert(clock_gettime(CLOCK_REALTIME, &wall_time) == 0);
        assert(localtime_r(&wall_time.tv_sec, &local));
        long millisecond = wall_time.tv_nsec / 1000000L;
        if (local.tm_sec % 2 == second_parity &&
            millisecond >= 300 && millisecond <= 500) return;
        nanosleep(&pause, NULL);
    }
    assert(!"clock did not reach the requested wall-second phase");
}

static float capture_colon_alpha(WmResourceScene *scene,
                                 const WmMenu *menu,
                                 float elapsed_seconds,
                                 bool board_overlay) {
    clock_image_count = 0;
    clock_colon_count = 0;
    capture_clock_colon = true;
    if (board_overlay) {
        wm_resource_scene_draw_grid_overlay(scene, menu, 70.0f,
                                             elapsed_seconds);
    } else {
        const WmResourceSceneFrame frame = {
            .elapsed_seconds = elapsed_seconds
        };
        wm_resource_scene_draw(scene, menu, &frame);
    }
    capture_clock_colon = false;
    /* Zero-alpha panes are omitted by the layout presenter. The authored
     * clock has one on-screen and two off-screen anchors when visible. */
    assert(clock_colon_count == 0 || clock_colon_count == 3);
    if (!clock_colon_count) return 0.0f;
    assert(clock_colons[1].x >= 0.0f &&
           clock_colons[1].x < WM_FRAME_WIDTH);
    for (size_t index = 0; index < 3; index++) {
        assert(isfinite(clock_colons[index].alpha));
        assert(fabsf(clock_colons[index].alpha -
                     clock_colons[1].alpha) < 0.001f);
    }
    return clock_colons[1].alpha;
}

static void draw_and_capture(WmResourceScene *scene, const WmMenu *menu,
                             float elapsed_seconds,
                             ClockImage output[CLOCK_IMAGE_COUNT]) {
    memset(clock_images, 0, sizeof(clock_images));
    clock_image_count = 0;
    const WmResourceSceneFrame frame = {.elapsed_seconds = elapsed_seconds};
    wm_resource_scene_draw(scene, menu, &frame);
    assert(clock_image_count == CLOCK_IMAGE_COUNT);
    memcpy(output, clock_images, sizeof(clock_images));
}

static void draw_overlay_and_capture(WmResourceScene *scene,
                                     const WmMenu *menu,
                                     float grid_frame,
                                     float elapsed_seconds,
                                     unsigned expected_count,
                                     ClockImage output[CLOCK_IMAGE_COUNT]) {
    memset(clock_images, 0, sizeof(clock_images));
    clock_image_count = 0;
    wm_resource_scene_draw_grid_overlay(scene, menu, grid_frame,
                                         elapsed_seconds);
    assert(clock_image_count == expected_count);
    memcpy(output, clock_images, sizeof(clock_images));
}

static float trace_overlay_clock_y(WmResourceScene *scene,
                                   const WmMenu *menu,
                                   float grid_frame,
                                   uint32_t texture) {
    traced_clock_texture = texture;
    traced_clock_y = NAN;
    clock_image_count = 0;
    wm_resource_scene_draw_grid_overlay(scene, menu, grid_frame, 7.0f);
    traced_clock_texture = 0;
    assert(isfinite(traced_clock_y));
    return traced_clock_y;
}

static void begin_date_capture(void) {
    date_glyphs = (DateGlyphBounds){
        .left = INFINITY,
        .top = INFINITY,
        .right = -INFINITY,
        .bottom = -INFINITY
    };
    capture_date_glyphs = true;
}

static void assert_same_date_bounds(const DateGlyphBounds *actual,
                                    const DateGlyphBounds *expected) {
    assert(actual->count == expected->count);
    assert(fabsf(actual->left - expected->left) < 0.01f);
    assert(fabsf(actual->top - expected->top) < 0.01f);
    assert(fabsf(actual->right - expected->right) < 0.01f);
    assert(fabsf(actual->bottom - expected->bottom) < 0.01f);
}

static void assert_board_date_matches(WmBoardScene *board,
                                      const DateGlyphBounds *grid_date) {
    begin_date_capture();
    wm_board_scene_draw_body(board);
    wm_board_scene_draw_footer(board);
    capture_date_glyphs = false;
    assert_same_date_bounds(&date_glyphs, grid_date);
}

int main(int argc, char **argv) {
    const char *assets = argc > 1 ? argv[1] : ".local/native-assets";
    WmPlatform *platform = (WmPlatform *)1;
    WmMenu menu;
    wm_menu_init(&menu);
    if (!wm_catalog_load(&menu, assets)) {
        puts("Clock resource comparison skipped: prepared assets unavailable.");
        return 0;
    }
    char clock_path[4096];
    int path_length = snprintf(clock_path, sizeof(clock_path),
                               "%s/layouts/chanSel/my_Clock_a.json", assets);
    assert(path_length > 0 && path_length < (int)sizeof(clock_path));
    char layout_error[160];
    WmLayout *clock_layout = wm_layout_load_json(
        clock_path, layout_error, sizeof(layout_error));
    assert(clock_layout);
    assert(source_colon_alpha(clock_layout, 0.0f) > 0.99f);
    float fade_out_alpha = source_colon_alpha(clock_layout, 2.5f);
    assert(fade_out_alpha > 0.0f && fade_out_alpha < 1.0f);
    assert(source_colon_alpha(clock_layout, 5.0f) < 0.01f);
    assert(source_colon_alpha(clock_layout, 53.0f) < 0.01f);
    float fade_in_alpha = source_colon_alpha(clock_layout, 58.0f);
    assert(fade_in_alpha > 0.0f && fade_in_alpha < 1.0f);
    assert(source_colon_alpha(clock_layout, 63.0f) > 0.99f);
    wm_layout_destroy(clock_layout);
    WmTextureCache *textures = wm_texture_cache_create(
        platform, assets, 128u * 1024u * 1024u);
    WmFontCache *fonts = wm_font_cache_create(
        platform, assets, 16u * 1024u * 1024u);
    assert(textures && fonts);
    WmResourceScene *scene = wm_resource_scene_create(
        platform, assets, &menu, textures, fonts);
    assert(scene);
    assert(wm_texture_cache_resolve(textures,
        "textures/chanSel/my_Clock_ab.png", &clock_colon_texture));

    struct timespec pause = {.tv_nsec = 20000000L};
    for (unsigned attempt = 0; attempt < 100 && !odd_wall_second();
         attempt++) {
        nanosleep(&pause, NULL);
    }
    assert(odd_wall_second());
    const WmResourceSceneFrame intro = {.elapsed_seconds = 5.0f};
    wm_resource_scene_draw(scene, &menu, &intro);

    ClockImage first_page[CLOCK_IMAGE_COUNT];
    ClockImage second_page[CLOCK_IMAGE_COUNT];
    ClockImage board_enter[CLOCK_IMAGE_COUNT];
    ClockImage board_exit[CLOCK_IMAGE_COUNT];
    begin_date_capture();
    draw_and_capture(scene, &menu, 7.0f, first_page);
    capture_date_glyphs = false;
    DateGlyphBounds grid_date = date_glyphs;
    assert(grid_date.count > 0);
    draw_overlay_and_capture(scene, &menu, 70.0f, 7.0f,
                             CLOCK_IMAGE_COUNT,
                             board_enter);
    draw_overlay_and_capture(scene, &menu, 100.0f, 7.0f,
                             0, board_exit);
    /* The authored exit grid begins above the screen, then the same clock
     * returns with it by frame 120. */
    draw_overlay_and_capture(scene, &menu, 120.0f, 7.0f,
                             CLOCK_IMAGE_COUNT,
                             board_exit);
    float entry_start = trace_overlay_clock_y(
        scene, &menu, 70.0f, first_page[0].texture);
    float entry_middle = trace_overlay_clock_y(
        scene, &menu, 80.0f, first_page[0].texture);
    float entry_end = trace_overlay_clock_y(
        scene, &menu, 89.0f, first_page[0].texture);
    float exit_start = trace_overlay_clock_y(
        scene, &menu, 100.0f, first_page[0].texture);
    float exit_middle = trace_overlay_clock_y(
        scene, &menu, 110.0f, first_page[0].texture);
    float exit_end = trace_overlay_clock_y(
        scene, &menu, 120.0f, first_page[0].texture);
    assert(entry_start > entry_middle && entry_middle > entry_end);
    assert(exit_start < exit_middle && exit_middle < exit_end);
    assert(entry_end + first_page[0].height < 0.0f);
    assert(exit_start + first_page[0].height < 0.0f);
    assert(fabsf(entry_start - first_page[0].y) < 0.01f);
    assert(fabsf(exit_end - first_page[0].y) < 0.01f);
    assert(wm_menu_change_page(&menu, 1));
    /* AM/PM has its own texture. A numerical digit may appear several times
     * in one clock, so it cannot identify one instance per page anchor. */
    traced_clock_texture = first_page[4].texture;
    float previous_x[3] = {INFINITY, INFINITY, INFINITY};
    for (unsigned step = 0; step < 4; step++) {
        wm_menu_tick(&menu, 5.0f / 60.0f);
        clock_image_count = 0;
        slide_clock_count = 0;
        capture_slide_clock = true;
        begin_date_capture();
        const WmResourceSceneFrame moving = {.elapsed_seconds = 7.0f + step / 12.0f};
        wm_resource_scene_draw(scene, &menu, &moving);
        capture_date_glyphs = false;
        capture_slide_clock = false;
        assert_same_date_bounds(&date_glyphs, &grid_date);
        assert(slide_clock_count == 3);
        bool visible = false;
        for (size_t anchor = 0; anchor < slide_clock_count; anchor++) {
            const ClockSlideSample *sample = &slide_clock[anchor];
            assert(fabsf(sample->y - first_page[4].y) < 0.01f);
            assert(sample->alpha > 0.99f);
            if (step < 3) assert(sample->x < previous_x[anchor]);
            previous_x[anchor] = sample->x;
            if (sample->x + first_page[4].width > 0.0f &&
                sample->x < WM_FRAME_WIDTH) visible = true;
        }
        /* The authored page slide moves three clock instances through the
         * screen. At least one remains visible while ownership changes. */
        assert(visible);
        assert(texture_live[traced_clock_texture]);
    }
    traced_clock_texture = 0;
    wm_menu_tick(&menu, 0.5f);
    draw_and_capture(scene, &menu, 8.0f, second_page);
    /* The supplied clock layout fades ClockTen out at frames 0-5 of an
     * even second and in at frames 53-63 of an odd second. Board overlay
     * draws must preserve that sampled phase instead of restarting it. */
    wait_for_wall_phase(1);
    float visible_colon = capture_colon_alpha(scene, &menu, 9.0f, false);
    float overlay_visible_colon = capture_colon_alpha(scene, &menu,
                                                       9.0f, true);
    assert(visible_colon > 0.95f);
    assert(fabsf(overlay_visible_colon - visible_colon) < 0.02f);
    wait_for_wall_phase(0);
    float hidden_colon = capture_colon_alpha(scene, &menu, 10.0f, false);
    float overlay_hidden_colon = capture_colon_alpha(scene, &menu,
                                                      10.0f, true);
    assert(hidden_colon < 0.05f);
    assert(fabsf(overlay_hidden_colon - hidden_colon) < 0.02f);
    for (size_t index = 0; index < CLOCK_IMAGE_COUNT; index++) {
        const ClockImage *first = &first_page[index];
        const ClockImage *second = &second_page[index];
        assert(first->texture && first->live &&
               first->nonzero_alpha_pixels > 0);
        assert(second->texture == first->texture && second->live &&
               second->nonzero_alpha_pixels == first->nonzero_alpha_pixels);
        assert(first->texture_count == 1 && second->texture_count == 1);
        assert(first->tev_stage_count == 0 && second->tev_stage_count == 0);
        assert(fabsf(first->x - second->x) < 0.01f);
        assert(fabsf(first->y - second->y) < 0.01f);
        assert(fabsf(first->width - second->width) < 0.01f);
        assert(fabsf(first->height - second->height) < 0.01f);
        assert(memcmp(first->uv, second->uv, sizeof(first->uv)) == 0);
        assert(memcmp(first->register_color, second->register_color,
                      sizeof(first->register_color)) == 0);
        assert(board_enter[index].texture == first->texture);
        assert(board_exit[index].texture == first->texture);
        assert(board_enter[index].live && board_exit[index].live);
        assert(fabsf(board_enter[index].x - first->x) < 0.01f);
        assert(fabsf(board_enter[index].y - first->y) < 0.01f);
        assert(fabsf(board_exit[index].x - first->x) < 0.01f);
        assert(fabsf(board_exit[index].y - first->y) < 0.01f);
    }
    WmTextureCacheStats stats = wm_texture_cache_stats(textures);
    assert(stats.failed_sources == 0);
    assert(stats.evictions == 0);

    time_t now = time(NULL);
    struct tm local_date;
    assert(now != (time_t)-1 && localtime_r(&now, &local_date));
    WmBoardDate today = {
        .year = local_date.tm_year + 1900,
        .month = local_date.tm_mon + 1,
        .day = local_date.tm_mday
    };
    WmBoardScene *board = wm_board_scene_create(
        platform, assets, textures, fonts);
    assert(board && wm_board_scene_open(board, today));
    assert_board_date_matches(board, &grid_date);
    wm_board_scene_advance(board, 19.0f);
    assert_board_date_matches(board, &grid_date);
    wm_board_scene_advance(board, 21.0f);
    assert(wm_board_scene_phase(board) == WM_BOARD_READY);
    assert_board_date_matches(board, &grid_date);
    assert(wm_board_scene_back(board));
    assert_board_date_matches(board, &grid_date);
    wm_board_scene_advance(board, 19.0f);
    assert_board_date_matches(board, &grid_date);
    wm_board_scene_advance(board, 20.0f);
    assert_board_date_matches(board, &grid_date);
    wm_board_scene_destroy(board);

    wm_resource_scene_destroy(scene);
    wm_texture_cache_destroy(textures);
    wm_font_cache_destroy(fonts);
    puts("Clock and date presentation remains continuous through grid and Board transitions.");
    return 0;
}
