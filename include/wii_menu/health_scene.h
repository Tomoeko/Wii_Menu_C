#ifndef WII_MENU_HEALTH_SCENE_H
#define WII_MENU_HEALTH_SCENE_H

#include "wii_menu/font_cache.h"
#include "wii_menu/layout_runtime.h"
#include "wii_menu/platform.h"
#include "wii_menu/texture_cache.h"

#include <stdbool.h>

typedef struct WmHealthScene WmHealthScene;

typedef enum WmHealthPhase {
    WM_HEALTH_ENTER,
    WM_HEALTH_WAIT,
    WM_HEALTH_LEAVE,
    WM_HEALTH_DONE
} WmHealthPhase;

/* The local assets directory is produced from a user-supplied System Menu
 * WAD. locale selects a source Has_* / Push_* pair, defaulting to US_ENG. */
WmHealthScene *wm_health_scene_create(WmPlatform *platform,
                                        const char *assets_directory,
                                        WmTextureCache *textures,
                                        WmFontCache *fonts,
                                        bool enabled,
                                        const char *locale);
void wm_health_scene_destroy(WmHealthScene *health);
void wm_health_scene_reset(WmHealthScene *health, bool enabled);
void wm_health_scene_advance(WmHealthScene *health, float frames);
bool wm_health_scene_accept(WmHealthScene *health);
bool wm_health_scene_active(const WmHealthScene *health);
bool wm_health_scene_ready(const WmHealthScene *health);
WmHealthPhase wm_health_scene_phase(const WmHealthScene *health);
float wm_health_scene_elapsed(const WmHealthScene *health);

/* Call after clearing the frame to black. The exit terminal pose remains
 * drawable after completion for a seamless handoff to the grid entrance. */
bool wm_health_scene_pose(WmHealthScene *health);
void wm_health_scene_draw(WmHealthScene *health);
const WmLayout *wm_health_scene_layout(const WmHealthScene *health);

#endif
