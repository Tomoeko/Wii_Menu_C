#include "wii_menu/menu_restart.h"

#include "wii_menu/layout_present.h"
#include "wii_menu/layout_runtime.h"
#include "wii_menu/material_prepare.h"
#include "wii_menu/scene_fader.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>

enum { READY_FRAMES = 94, BLACK_FRAMES = 43 };

struct WmMenuRestartScene {
    WmPlatform *platform;
    WmTextureCache *textures;
    WmFontCache *fonts;
    WmLayout *layout;
};

bool wm_menu_restart_start(WmMenuRestartClock *clock) {
    if (!clock || clock->phase != WM_MENU_RESTART_IDLE) return false;
    *clock = (WmMenuRestartClock){WM_MENU_RESTART_LOADING, 0.0f, 1.0f};
    return true;
}

bool wm_menu_restart_advance(WmMenuRestartClock *clock, float frames) {
    if (!clock || !isfinite(frames) || frames < 0.0f ||
        clock->phase == WM_MENU_RESTART_IDLE) return false;
    bool reached_grid = false;
    while (frames > 0.0f && clock->phase != WM_MENU_RESTART_IDLE) {
        float boundary = clock->phase == WM_MENU_RESTART_LOADING ? READY_FRAMES :
                         clock->phase == WM_MENU_RESTART_OUT ? 23.0f :
                         clock->phase == WM_MENU_RESTART_BLACK ? BLACK_FRAMES :
                         22.0f;
        float step = fminf(frames, boundary - clock->frame);
        clock->frame += step;
        frames -= step;
        if (clock->phase == WM_MENU_RESTART_LOADING ||
            clock->phase == WM_MENU_RESTART_OUT)
            clock->animation_frame += step;
        if (clock->frame + 0.0001f < boundary) break;
        if (clock->phase == WM_MENU_RESTART_LOADING)
            clock->phase = WM_MENU_RESTART_OUT;
        else if (clock->phase == WM_MENU_RESTART_OUT)
            clock->phase = WM_MENU_RESTART_BLACK;
        else if (clock->phase == WM_MENU_RESTART_BLACK) {
            clock->phase = WM_MENU_RESTART_GRID;
            reached_grid = true;
        } else clock->phase = WM_MENU_RESTART_IDLE;
        clock->frame = 0.0f;
    }
    return reached_grid;
}

bool wm_menu_restart_active(const WmMenuRestartClock *clock) {
    return clock && clock->phase != WM_MENU_RESTART_IDLE;
}

float wm_menu_restart_alpha(const WmMenuRestartClock *clock) {
    if (!clock || clock->phase == WM_MENU_RESTART_IDLE ||
        clock->phase == WM_MENU_RESTART_LOADING) return 0.0f;
    if (clock->phase == WM_MENU_RESTART_BLACK) return 1.0f;
    return wm_native_fade_alpha(clock->frame,
                                 clock->phase == WM_MENU_RESTART_GRID);
}

WmMenuRestartScene *wm_menu_restart_scene_create(
    WmPlatform *platform, const char *assets_directory,
    WmTextureCache *textures, WmFontCache *fonts) {
    if (!platform || !assets_directory || !textures || !fonts) return NULL;
    WmMenuRestartScene *scene = calloc(1, sizeof(*scene));
    if (!scene) return NULL;
    scene->platform = platform;
    scene->textures = textures;
    scene->fonts = fonts;
    char path[4096];
    int length = snprintf(path, sizeof(path),
        "%s/layouts/restart/my_BackToWiiMenu.json", assets_directory);
    char error[160] = {0};
    if (length > 0 && length < (int)sizeof(path))
        scene->layout = wm_layout_load_json(path, error, sizeof(error));
    if (!scene->layout) {
        wm_menu_restart_scene_destroy(scene);
        return NULL;
    }
    wm_layout_prepare_materials(platform, scene->layout);
    return scene;
}

void wm_menu_restart_scene_destroy(WmMenuRestartScene *scene) {
    if (!scene) return;
    wm_layout_destroy(scene->layout);
    free(scene);
}

void wm_menu_restart_scene_draw(WmMenuRestartScene *scene,
                                const WmMenuRestartClock *clock) {
    if (!scene || !clock ||
        (clock->phase != WM_MENU_RESTART_LOADING &&
         clock->phase != WM_MENU_RESTART_OUT)) return;
    WmLayoutClip clip = {
        .animation = "my_BackToWiiMenu",
        .frame = clock->animation_frame,
        .loop_override = 1
    };
    if (!wm_layout_pose(scene->layout, &clip, 1)) return;
    wm_layout_present_with_fonts(scene->platform, scene->textures,
        scene->fonts, scene->layout, true, WM_LAYOUT_IPL, NULL);
}
