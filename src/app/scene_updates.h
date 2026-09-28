#ifndef WM_APP_SCENE_UPDATES_H
#define WM_APP_SCENE_UPDATES_H

#include "input_routing.h"

#include "wii_menu/scenes/sd_scene.h"
#include "wii_menu/scenes/storage_scene.h"

typedef struct WmAppSettingsUpdate {
    WmMenu *menu;
    WmOptionsScene *options;
    WmStorageScene **storage_scenes;
    WmStorageScene **active_storage;
    WmAudio *audio;
    WmAppSceneFade *fade;
    WmBoardAction *board_settings_request;
    WmOptionsControl *hovered;
    bool pointer_inside;
    int pointer_x;
    int pointer_y;
} WmAppSettingsUpdate;

typedef struct WmAppSdUpdate {
    WmSdScene *scene;
    WmAudio *audio;
    WmAppSceneFade *fade;
    unsigned *page;
    bool *help_seen;
} WmAppSdUpdate;

/* The caller checks the shared pause state. Storage advances after Options
 * because opening storage from an Options action takes effect this frame. */
void wm_app_settings_advance(WmAppSettingsUpdate *update, float frames);
void wm_app_sd_advance(WmAppSdUpdate *update, float frames);

#endif
