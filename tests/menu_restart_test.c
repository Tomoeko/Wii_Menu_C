#include "wii_menu/menu_restart.h"
#include "wii_menu/layout_runtime.h"

#include <assert.h>
#include <math.h>
#include <stdio.h>

/* Clock tests do not create a rendering scene. These satisfy the shared
 * layout library while making accidental GPU use fail immediately. */
void wm_platform_prepare_material(WmPlatform *platform,
                                  const WmMaterialQuad *quad) {
    (void)platform;
    (void)quad;
    assert(false);
}

void wm_platform_draw_material_quad(WmPlatform *platform,
                                    const WmMaterialQuad *quad) {
    (void)platform;
    (void)quad;
    assert(false);
}

void wm_platform_draw_vertices(WmPlatform *platform,
                               const WmDrawVertex vertices[4],
                               uint32_t texture) {
    (void)platform;
    (void)vertices;
    (void)texture;
    assert(false);
}

uint32_t wm_platform_create_texture(WmPlatform *platform, int width,
                                    int height, const uint8_t *rgba) {
    (void)platform;
    (void)width;
    (void)height;
    (void)rgba;
    assert(false);
    return 0;
}

void wm_platform_destroy_texture(WmPlatform *platform, uint32_t texture) {
    (void)platform;
    (void)texture;
    assert(false);
}

static bool near(float value, float expected) {
    return fabsf(value - expected) < 0.001f;
}

static void test_restart_sequence(void) {
    WmMenuRestartClock clock = {0};
    assert(wm_menu_restart_start(&clock));
    assert(!wm_menu_restart_start(&clock));
    assert(clock.phase == WM_MENU_RESTART_LOADING);
    assert(near(clock.animation_frame, 1.0f));

    assert(!wm_menu_restart_advance(&clock, 93.5f));
    assert(clock.phase == WM_MENU_RESTART_LOADING);
    assert(near(clock.animation_frame, 94.5f));
    assert(!wm_menu_restart_advance(&clock, 0.5f));
    assert(clock.phase == WM_MENU_RESTART_OUT);
    assert(near(clock.animation_frame, 95.0f));
    assert(near(wm_menu_restart_alpha(&clock), 0.0f));

    assert(!wm_menu_restart_advance(&clock, 22.0f));
    assert(clock.phase == WM_MENU_RESTART_OUT);
    assert(wm_menu_restart_alpha(&clock) > 0.9f);
    assert(!wm_menu_restart_advance(&clock, 1.0f));
    assert(clock.phase == WM_MENU_RESTART_BLACK);
    assert(near(wm_menu_restart_alpha(&clock), 1.0f));
    assert(near(clock.animation_frame, 118.0f));

    assert(!wm_menu_restart_advance(&clock, 42.0f));
    assert(clock.phase == WM_MENU_RESTART_BLACK);
    assert(wm_menu_restart_advance(&clock, 1.0f));
    assert(clock.phase == WM_MENU_RESTART_GRID);
    assert(near(wm_menu_restart_alpha(&clock), 1.0f));
    assert(!wm_menu_restart_advance(&clock, 22.0f));
    assert(!wm_menu_restart_active(&clock));
    assert(near(wm_menu_restart_alpha(&clock), 0.0f));
}

static void test_coarse_step_preserves_handoff(void) {
    WmMenuRestartClock clock = {0};
    assert(wm_menu_restart_start(&clock));
    assert(wm_menu_restart_advance(&clock, 160.5f));
    assert(clock.phase == WM_MENU_RESTART_GRID);
    assert(near(clock.frame, 0.5f));
    assert(!wm_menu_restart_advance(&clock, 21.5f));
    assert(clock.phase == WM_MENU_RESTART_IDLE);
}

static void count_visible_quad(void *context, const WmLayoutQuad *quad) {
    size_t *count = context;
    if (quad->vertices[0].color[3] > 0.01f) (*count)++;
}

static void test_embedded_layout(void) {
    const char *path =
        ".local/native-assets/layouts/restart/my_BackToWiiMenu.json";
    FILE *file = fopen(path, "rb");
    if (!file) return;
    fclose(file);
    char error[160] = {0};
    WmLayout *layout = wm_layout_load_json(path, error, sizeof(error));
    assert(layout);
    WmLayoutAnimationInfo info;
    assert(wm_layout_animation_info(layout, "my_BackToWiiMenu", &info));
    assert(near(info.frames, 1000.0f));
    size_t count = 0;
    WmLayoutDrawOptions draw = {
        .wide = true, .mode = WM_LAYOUT_IPL, .alpha = 1.0f,
        .on_quad = count_visible_quad, .context = &count
    };
    WmLayoutClip clip = {
        .animation = "my_BackToWiiMenu", .frame = 40.0f,
        .loop_override = 1
    };
    assert(wm_layout_pose(layout, &clip, 1));
    wm_layout_draw(layout, &draw);
    assert(count > 0);
    wm_layout_destroy(layout);
}

int main(void) {
    test_restart_sequence();
    test_coarse_step_preserves_handoff();
    test_embedded_layout();
    puts("menu restart tests passed");
    return 0;
}
