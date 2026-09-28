#include "scene_input.h"

#include "wii_menu/audio/hover_audio.h"

#include <stddef.h>

static const char *storage_click_cue(WmStorageControl control, bool channel_storage) {
    switch (control) {
        case WM_STORAGE_CONTROL_PREVIOUS:
        case WM_STORAGE_CONTROL_NEXT:
            return "WSD_SELECT";
        case WM_STORAGE_CONTROL_WII_TAB:
        case WM_STORAGE_CONTROL_SD_TAB:
            return "WIPL_SE_BT_PUSH";
        case WM_STORAGE_CONTROL_SLOT:
        case WM_STORAGE_CONTROL_MOVE:
        case WM_STORAGE_CONTROL_COPY:
        case WM_STORAGE_CONTROL_ERASE:
            return channel_storage ? "WIPL_SE_DECIDE" : "WIPL_SE_BT_PUSH";
        case WM_STORAGE_CONTROL_BACK:
        case WM_STORAGE_CONTROL_NO:
            return "WIPL_SE_CANCEL";
        case WM_STORAGE_CONTROL_YES:
            return "WIPL_SE_DECIDE";
        case WM_STORAGE_CONTROL_NONE:
            return NULL;
    }
    return NULL;
}

void wm_app_sd_pointer_event(WmAppSceneInput *input, WmSdScene *scene,
                             const WmEvent *event) {
    if (event->type == WM_EVENT_POINTER_MOVE) {
        wm_sd_scene_hover(scene, wm_sd_scene_hit(scene, event->x, event->y));
    } else if (event->type == WM_EVENT_POINTER_DOWN) {
        input->sd_pressed = event->button == WM_POINTER_LEFT
                                ? wm_sd_scene_hit(scene, event->x, event->y)
                                : (WmSdHit){WM_SD_CONTROL_NONE, 0};
    } else if (event->type == WM_EVENT_POINTER_UP) {
        WmSdHit next = wm_sd_scene_hit(scene, event->x, event->y);
        if (event->button == WM_POINTER_LEFT &&
            input->sd_pressed.control != WM_SD_CONTROL_NONE &&
            next.control == input->sd_pressed.control &&
            next.slot == input->sd_pressed.slot) {
            wm_sd_scene_activate(scene, next);
        }
        input->sd_pressed = (WmSdHit){WM_SD_CONTROL_NONE, 0};
    }
}

void wm_app_storage_pointer_event(WmAppSceneInput *input, WmStorageScene *scene,
                                  WmStorageScene *channel_storage, WmAudio *audio,
                                  const WmEvent *event) {
    if (event->type == WM_EVENT_POINTER_MOVE) {
        WmStorageHit next = wm_storage_scene_hit(scene, event->x, event->y);
        WmStorageSnapshot snapshot = wm_storage_scene_snapshot(scene);
        bool selected_tab = (next.control == WM_STORAGE_CONTROL_WII_TAB &&
                             snapshot.tab == WM_STORAGE_WII) ||
                            (next.control == WM_STORAGE_CONTROL_SD_TAB &&
                             snapshot.tab == WM_STORAGE_SD);
        if (wm_storage_scene_hover(scene, next) &&
            next.control != WM_STORAGE_CONTROL_NONE && !selected_tab) {
            wm_audio_play(audio, wm_hover_audio_storage_cue(next.control));
        }
    } else if (event->type == WM_EVENT_POINTER_DOWN) {
        input->storage_pressed = event->button == WM_POINTER_LEFT
                                     ? wm_storage_scene_hit(scene, event->x, event->y)
                                     : (WmStorageHit){WM_STORAGE_CONTROL_NONE, -1};
    } else if (event->type == WM_EVENT_POINTER_UP) {
        WmStorageHit next = wm_storage_scene_hit(scene, event->x, event->y);
        if (event->button == WM_POINTER_LEFT &&
            input->storage_pressed.control != WM_STORAGE_CONTROL_NONE &&
            next.control == input->storage_pressed.control &&
            next.slot == input->storage_pressed.slot &&
            wm_storage_scene_activate(scene, next)) {
            const char *cue = storage_click_cue(next.control, scene == channel_storage);
            if (cue)
                wm_audio_play(audio, cue);
        }
        input->storage_pressed = (WmStorageHit){WM_STORAGE_CONTROL_NONE, -1};
    }
}

void wm_app_options_pointer_event(WmAppSceneInput *input, WmOptionsScene *scene,
                                  WmAudio *audio, const WmEvent *event) {
    if (wm_options_scene_nickname_keyboard_visible(scene)) {
        WmBoardKeyboardControl key = wm_options_scene_keyboard_hit(
            scene, event->x, event->y);
        if (event->type == WM_EVENT_POINTER_MOVE) {
            if (key != input->nickname_key_hovered &&
                key != WM_KEYBOARD_NONE)
                wm_audio_play(audio, "WIPL_SE_BT_TARGETTING");
            input->nickname_key_hovered = key;
            wm_options_scene_keyboard_hover(scene, key);
        } else if (event->type == WM_EVENT_POINTER_DOWN) {
            if (event->button == WM_POINTER_LEFT &&
                wm_options_scene_keyboard_place_caret(
                    scene, event->x, event->y)) {
                input->nickname_key_pressed = WM_KEYBOARD_NONE;
                return;
            }
            input->nickname_key_pressed = event->button == WM_POINTER_LEFT
                ? key : WM_KEYBOARD_NONE;
        } else if (event->type == WM_EVENT_POINTER_UP) {
            if (event->button == WM_POINTER_LEFT &&
                key != WM_KEYBOARD_NONE &&
                key == input->nickname_key_pressed) {
                const char *cue = wm_options_scene_keyboard_activate(scene, key);
                if (cue) wm_audio_play(audio, cue);
            }
            input->nickname_key_pressed = WM_KEYBOARD_NONE;
        }
        return;
    }
    input->nickname_key_hovered = WM_KEYBOARD_NONE;
    input->nickname_key_pressed = WM_KEYBOARD_NONE;
    if (event->type == WM_EVENT_POINTER_MOVE) {
        WmOptionsControl next = wm_options_scene_hit(scene, event->x, event->y);
        if (next != input->options_hovered && next != WM_OPTIONS_CONTROL_NONE) {
            wm_audio_play(audio, wm_options_scene_hover_cue(scene, next));
        }
        input->options_hovered = next;
        wm_options_scene_hover(scene, next);
    } else if (event->type == WM_EVENT_POINTER_DOWN) {
        input->options_pressed = event->button == WM_POINTER_LEFT
                                     ? wm_options_scene_hit(scene, event->x, event->y)
                                     : WM_OPTIONS_CONTROL_NONE;
        if (input->options_pressed ==
            WM_OPTIONS_CONTROL_SETTINGS_NICKNAME_FIELD)
            wm_options_scene_place_nickname_caret(scene, event->x);
        const char *held_cue = wm_options_scene_click_cue(
            scene, input->options_pressed);
        input->options_held_arrow =
            event->button == WM_POINTER_LEFT &&
            wm_options_scene_pointer_down(scene, input->options_pressed);
        if (input->options_held_arrow && held_cue)
            wm_audio_play(audio, held_cue);
    } else if (event->type == WM_EVENT_POINTER_UP) {
        if (input->options_held_arrow) {
            wm_options_scene_pointer_up(scene);
            input->options_held_arrow = false;
            input->options_pressed = WM_OPTIONS_CONTROL_NONE;
            return;
        }
        WmOptionsControl next = wm_options_scene_hit(scene, event->x, event->y);
        const char *click_cue = wm_options_scene_click_cue(scene, next);
        if (event->button == WM_POINTER_LEFT &&
            input->options_pressed != WM_OPTIONS_CONTROL_NONE &&
            input->options_pressed == next && wm_options_scene_activate(scene, next)) {
            if (click_cue)
                wm_audio_play(audio, click_cue);
        }
        input->options_pressed = WM_OPTIONS_CONTROL_NONE;
    }
}

void wm_app_options_pointer_leave(WmAppSceneInput *input, WmOptionsScene *options) {
    input->nickname_key_pressed = WM_KEYBOARD_NONE;
    input->nickname_key_hovered = WM_KEYBOARD_NONE;
    wm_options_scene_keyboard_hover(options, WM_KEYBOARD_NONE);
    wm_options_scene_hover(options, WM_OPTIONS_CONTROL_NONE);
    if (input->options_held_arrow) {
        wm_options_scene_pointer_up(options);
        input->options_held_arrow = false;
        input->options_pressed = WM_OPTIONS_CONTROL_NONE;
    }
    input->options_hovered = WM_OPTIONS_CONTROL_NONE;
}
