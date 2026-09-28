#ifndef WII_MENU_MENU_RESTART_H
#define WII_MENU_MENU_RESTART_H

#include "wii_menu/fonts/font_cache.h"
#include "wii_menu/platform/platform.h"
#include "wii_menu/render/texture_cache.h"

#include <stdbool.h>

typedef enum WmMenuRestartPhase {
    WM_MENU_RESTART_IDLE,
    WM_MENU_RESTART_LOADING,
    WM_MENU_RESTART_OUT,
    WM_MENU_RESTART_BLACK,
    WM_MENU_RESTART_GRID
} WmMenuRestartPhase;

typedef struct WmMenuRestartClock {
    WmMenuRestartPhase phase;
    float frame;
    float animation_frame;
} WmMenuRestartClock;

/* Local restart uses fixed waits in place of console service readiness. */
bool wm_menu_restart_start(WmMenuRestartClock *clock);
bool wm_menu_restart_advance(WmMenuRestartClock *clock, float frames);
bool wm_menu_restart_active(const WmMenuRestartClock *clock);
float wm_menu_restart_alpha(const WmMenuRestartClock *clock);

typedef struct WmMenuRestartScene WmMenuRestartScene;

WmMenuRestartScene *wm_menu_restart_scene_create(
    WmPlatform *platform, const char *assets_directory,
    WmTextureCache *textures, WmFontCache *fonts);
void wm_menu_restart_scene_destroy(WmMenuRestartScene *scene);
void wm_menu_restart_scene_draw(WmMenuRestartScene *scene,
                                const WmMenuRestartClock *clock);

#endif
