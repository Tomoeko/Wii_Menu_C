#ifndef WM_APP_MENU_POINTER_H
#define WM_APP_MENU_POINTER_H

#include "wii_menu/audio/audio.h"
#include "wii_menu/input/channel_drag.h"
#include "wii_menu/input/pointer.h"
#include "wii_menu/menu/menu.h"
#include "wii_menu/platform/platform.h"
#include "wii_menu/scenes/preview_scene.h"
#include "wii_menu/scenes/resource_scene.h"

#include <stdbool.h>

typedef struct WmAppMenuPointer {
    WmHit hovered;
    WmHit pressed;
    WmPointerButton drag_button;
    float drag_previous_x;
    float drag_previous_y;
    float drag_pitch;
    bool drag_has_previous;
} WmAppMenuPointer;

typedef struct WmAppMenuPointerRelease {
    WmHit activated;
    bool released_drag;
} WmAppMenuPointerRelease;

void wm_app_menu_pointer_move(WmAppMenuPointer *input, WmMenu *menu,
                              WmResourceScene *resource_scene,
                              WmPreviewScene *preview_scene, WmChannelDrag *drag,
                              WmAudio *audio, int x, int y);
void wm_app_menu_pointer_down(WmAppMenuPointer *input, WmMenu *menu,
                              WmResourceScene *resource_scene,
                              WmPreviewScene *preview_scene, WmChannelDrag *drag,
                              WmAudio *audio, WmPointerButton button, int x, int y);
WmAppMenuPointerRelease wm_app_menu_pointer_up(WmAppMenuPointer *input, WmMenu *menu,
                                               WmResourceScene *resource_scene,
                                               WmPreviewScene *preview_scene,
                                               WmChannelDrag *drag, WmPointer *pointer,
                                               WmAudio *audio, const WmEvent *event);
void wm_app_menu_pointer_leave(WmAppMenuPointer *input, WmChannelDrag *drag,
                               WmAudio *audio);
void wm_app_menu_drag_advance(WmAppMenuPointer *input, WmMenu *menu,
                              WmResourceScene *resource_scene,
                              WmPreviewScene *preview_scene, WmChannelDrag *drag,
                              WmAudio *audio, float frames);

#endif
