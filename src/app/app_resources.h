#ifndef WM_APP_RESOURCES_H
#define WM_APP_RESOURCES_H

#include "wii_menu/audio/audio.h"
#include "wii_menu/board/board_scene.h"
#include "wii_menu/fonts/font_cache.h"
#include "wii_menu/input/channel_drag.h"
#include "wii_menu/input/pointer.h"
#include "wii_menu/layout/layout_runtime.h"
#include "wii_menu/menu/menu.h"
#include "wii_menu/menu/menu_restart.h"
#include "wii_menu/platform/platform.h"
#include "wii_menu/render/texture_cache.h"
#include "wii_menu/resources/resource_bmg.h"
#include "wii_menu/scenes/health_scene.h"
#include "wii_menu/scenes/home_overlay.h"
#include "wii_menu/scenes/options_scene.h"
#include "wii_menu/scenes/preview_scene.h"
#include "wii_menu/scenes/resource_scene.h"
#include "wii_menu/scenes/sd_scene.h"
#include "wii_menu/scenes/storage_scene.h"

/* Owns the startup resources; the renderer borrows these pointers. Release its
 * retained HOME target before destroying this bundle's platform. */
typedef struct WmAppResources {
    WmPlatform *platform;
    WmLayout *layout;
    WmTextureCache *layout_textures;
    WmFontCache *layout_fonts;
    WmTextureCache *scene_textures;
    WmFontCache *scene_fonts;
    WmResourceScene *resource_scene;
    WmPreviewScene *preview_scene;
    WmBoardScene *board_scene;
    WmMenuRestartScene *restart_scene;
    WmHealthScene *health_scene;
    WmOptionsScene *options_scene;
    WmSdScene *sd_scene;
    WmBmg *messages;
    WmStorageScene *storage_scenes[3];
    WmPointer *pointer;
    WmAudio *audio;
    WmHomeOverlay *home;
    WmChannelDrag *drag;
    char *board_state_path;
} WmAppResources;

/* Returns false only when a requested layout or its required backend assets
 * cannot be initialized. Optional menu scenes continue to load independently. */
bool wm_app_resources_create(WmAppResources *resources, WmMenu *menu,
                             const char *assets, const char *layout_path,
                             const char *raw_root);
void wm_app_resources_destroy(WmAppResources *resources);

#endif
