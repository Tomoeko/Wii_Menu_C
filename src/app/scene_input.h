#ifndef WM_APP_SCENE_INPUT_H
#define WM_APP_SCENE_INPUT_H

#include "wii_menu/audio/audio.h"
#include "wii_menu/platform/platform.h"
#include "wii_menu/scenes/options_scene.h"
#include "wii_menu/scenes/sd_scene.h"
#include "wii_menu/scenes/storage_scene.h"

#include <stdbool.h>

typedef struct WmAppSceneInput {
    WmOptionsControl options_hovered;
    WmOptionsControl options_pressed;
    bool options_held_arrow;
    WmSdHit sd_pressed;
    WmStorageHit storage_pressed;
} WmAppSceneInput;

void wm_app_sd_pointer_event(WmAppSceneInput *input, WmSdScene *scene,
                             const WmEvent *event);
void wm_app_storage_pointer_event(WmAppSceneInput *input, WmStorageScene *scene,
                                  WmStorageScene *channel_storage, WmAudio *audio,
                                  const WmEvent *event);
void wm_app_options_pointer_event(WmAppSceneInput *input, WmOptionsScene *scene,
                                  WmAudio *audio, const WmEvent *event);
void wm_app_options_pointer_leave(WmAppSceneInput *input, WmOptionsScene *options);

#endif
