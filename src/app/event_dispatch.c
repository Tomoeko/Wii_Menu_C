#include "event_dispatch.h"

#include "frame_transitions.h"
#include "input_routing.h"

/* Scene ownership is checked in the same order for every pointer phase.
 * A routed event never reaches the menu hit controller. */
static bool route_scene_pointer(WmAppRuntime *app, const WmEvent *event) {
    WmMenu *menu = app->menu;
    WmAppResources *resources = app->resources;
    WmAppInputState *input = app->input;
    WmAppFlowState *flow = app->flow;

    if (menu->screen == WM_SCREEN_SD && resources->sd_scene && !menu->home_open &&
        !menu->notice[0]) {
        wm_app_sd_pointer_event(&input->scene, resources->sd_scene, event);
        return true;
    }
    if (flow->active_storage && menu->screen == WM_SCREEN_SETTINGS &&
        !menu->home_open && !menu->notice[0]) {
        wm_app_storage_pointer_event(&input->scene, flow->active_storage,
                                     resources->storage_scenes[WM_STORAGE_CHANNELS],
                                     resources->audio, event);
        return true;
    }
    if (menu->screen == WM_SCREEN_BOARD && resources->board_scene && !menu->home_open &&
        !menu->notice[0]) {
        if (event->type == WM_EVENT_POINTER_MOVE) {
            wm_app_board_pointer_move(&input->board, resources->board_scene,
                                      resources->audio, event->x, event->y);
        } else if (event->type == WM_EVENT_POINTER_DOWN) {
            wm_app_board_pointer_down(&input->board, resources->board_scene,
                                      resources->audio, event->button, event->x,
                                      event->y);
        } else if (event->type == WM_EVENT_POINTER_UP) {
            wm_app_board_pointer_up(&input->board, resources->board_scene,
                                    resources->resource_scene, resources->pointer,
                                    resources->audio, event->button, event->x, event->y,
                                    event->outside_viewport);
        } else {
            return false;
        }
        return true;
    }
    if (menu->screen == WM_SCREEN_SETTINGS && resources->options_scene &&
        !menu->home_open && !menu->notice[0]) {
        wm_app_options_pointer_event(&input->scene, resources->options_scene,
                                     resources->audio, event);
        return true;
    }
    return false;
}

static void update_transition_pointer(WmPointer *pointer, const WmEvent *event) {
    if (event->type == WM_EVENT_POINTER_MOVE)
        wm_pointer_move(pointer, (float)event->x, (float)event->y);
    if (event->type == WM_EVENT_POINTER_LEAVE)
        wm_pointer_hide(pointer);
}

static void handle_home_event(WmAppRuntime *app, const WmEvent *event) {
    WmHomeOverlay *home = app->resources->home;
    WmAppInputState *input = app->input;
    if (event->type == WM_EVENT_POINTER_MOVE || event->type == WM_EVENT_POINTER_DOWN ||
        event->type == WM_EVENT_POINTER_UP)
        wm_pointer_move(app->resources->pointer, (float)event->x, (float)event->y);
    if (event->type == WM_EVENT_POINTER_MOVE) {
        input->home_hovered = wm_home_overlay_hit(home, event->x, event->y);
        wm_home_overlay_hover(home, input->home_hovered);
    } else if (event->type == WM_EVENT_POINTER_DOWN) {
        input->home_pressed = event->button == WM_POINTER_LEFT
                                  ? wm_home_overlay_hit(home, event->x, event->y)
                                  : WM_HOME_CONTROL_NONE;
    } else if (event->type == WM_EVENT_POINTER_UP) {
        input->home_hovered = wm_home_overlay_hit(home, event->x, event->y);
        wm_home_overlay_hover(home, input->home_hovered);
        if (event->button == WM_POINTER_LEFT &&
            input->home_pressed != WM_HOME_CONTROL_NONE &&
            input->home_pressed == input->home_hovered)
            wm_home_overlay_activate(home, input->home_hovered);
        input->home_pressed = WM_HOME_CONTROL_NONE;
    } else if (event->type == WM_EVENT_POINTER_LEAVE) {
        wm_pointer_hide(app->resources->pointer);
        input->home_hovered = WM_HOME_CONTROL_NONE;
        input->home_pressed = WM_HOME_CONTROL_NONE;
        wm_home_overlay_hover(home, WM_HOME_CONTROL_NONE);
    } else if (event->type == WM_EVENT_KEY_DOWN) {
        if (event->key == WM_KEY_HOME || event->key == 'h' || event->key == 'H' ||
            event->key == WM_KEY_ESCAPE || event->key == WM_KEY_BACKSPACE) {
            wm_home_overlay_back(home);
        } else if (event->key == WM_KEY_ENTER &&
                   input->home_hovered != WM_HOME_CONTROL_NONE) {
            wm_home_overlay_activate(home, input->home_hovered);
        } else if (event->key == '1' || event->key == '2') {
            wm_home_overlay_reconnect_key(home, (char)event->key, true);
        }
    }
}

static void handle_key_modifiers(WmAppRuntime *app, const WmEvent *event) {
    WmMenu *menu = app->menu;
    WmBoardScene *board = app->resources->board_scene;
    WmOptionsScene *options = app->resources->options_scene;
    bool editor_keyboard = (menu->screen == WM_SCREEN_BOARD && board &&
                            wm_board_scene_compose_editor_active(board)) ||
                           (menu->screen == WM_SCREEN_SETTINGS && options &&
                            wm_options_scene_text_editing(options));
    if (menu->screen == WM_SCREEN_BOARD && board)
        wm_board_scene_keyboard_modifiers(board, event->shift_down,
                                          event->caps_lock_on);
    if (menu->screen == WM_SCREEN_SETTINGS && options)
        wm_options_scene_keyboard_modifiers(options, event->shift_down,
                                            event->caps_lock_on);
    if (editor_keyboard && (event->key == WM_KEY_CAPS_LOCK ||
                            (event->key == WM_KEY_SHIFT && event->shift_down)))
        wm_audio_play(app->resources->audio, "WIPL_SE_SK_SWITCHING_02");
}

static void handle_nickname_key(WmOptionsScene *options, WmAudio *audio, int key) {
    if (key == WM_KEY_ESCAPE || key == WM_KEY_ENTER) {
        const char *cue = wm_options_scene_keyboard_close(options, key == WM_KEY_ENTER);
        if (cue)
            wm_audio_play(audio, cue);
    } else if (key == WM_KEY_BACKSPACE) {
        wm_audio_play(audio, wm_options_scene_backspace(options)
                                 ? "WIPL_SE_CHAR_DELETE"
                                 : "WIPL_SE_CHAR_DELETE_ERROR");
    } else if (key == WM_KEY_LEFT || key == WM_KEY_RIGHT) {
        wm_options_scene_move_nickname_caret(options, key == WM_KEY_LEFT ? -1 : 1);
    } else if (key >= 32 && key <= 126) {
        wm_audio_play(audio, wm_options_scene_type_ascii(options, (char)key)
                                 ? "WIPL_SE_CHAR_INPUT"
                                 : "WIPL_SE_CHAR_DELETE_ERROR");
    }
}

static void handle_key_down(WmAppRuntime *app, const WmEvent *event,
                            uint64_t frame_start) {
    WmMenu *menu = app->menu;
    WmAppResources *resources = app->resources;
    WmAppInputState *input = app->input;
    WmAppFlowState *flow = app->flow;
    WmBoardScene *board = resources->board_scene;
    WmOptionsScene *options = resources->options_scene;
    WmAudio *audio = resources->audio;

    input->keyboard_focus = true;
    if (menu->screen == WM_SCREEN_BOARD && board)
        wm_board_scene_keyboard_modifiers(board, event->shift_down,
                                          event->caps_lock_on);
    if (menu->screen == WM_SCREEN_SETTINGS && options)
        wm_options_scene_keyboard_modifiers(options, event->shift_down,
                                            event->caps_lock_on);
    bool composing = menu->screen == WM_SCREEN_BOARD && board &&
                     wm_board_scene_child(board) == WM_BOARD_CHILD_COMPOSE;
    bool nickname_keyboard = menu->screen == WM_SCREEN_SETTINGS &&
                             wm_options_scene_nickname_keyboard_visible(options);
    if (event->key == WM_KEY_HOME || (!composing && !nickname_keyboard &&
                                      (event->key == 'h' || event->key == 'H'))) {
        if (wm_app_try_enter_home(app, frame_start)) {
            input->menu_pointer.hovered = (WmHit){WM_HIT_NONE, -1};
            input->menu_pointer.pressed = input->menu_pointer.hovered;
            input->keyboard_focus = false;
        }
        return;
    }
    if (menu->screen == WM_SCREEN_SD && resources->sd_scene &&
        (event->key == WM_KEY_ESCAPE || event->key == WM_KEY_BACKSPACE)) {
        wm_sd_scene_back(resources->sd_scene);
        return;
    }
    if (flow->active_storage &&
        (event->key == WM_KEY_ESCAPE || event->key == WM_KEY_BACKSPACE)) {
        if (wm_storage_scene_back(flow->active_storage))
            wm_audio_play(audio, "WIPL_SE_CANCEL");
        return;
    }
    if (menu->screen == WM_SCREEN_BOARD && board &&
        wm_board_scene_child(board) == WM_BOARD_CHILD_COMPOSE &&
        wm_app_board_compose_key(board, audio, event->key))
        return;
    if (nickname_keyboard) {
        handle_nickname_key(options, audio, event->key);
        return;
    }
    wm_app_handle_key(menu, audio, resources->resource_scene, board, options,
                      &flow->fade, event->key, &input->focused_slot);
}

void wm_app_poll_events(WmAppRuntime *app, uint64_t frame_start, bool health_frame,
                        bool *running) {
    WmMenu *menu = app->menu;
    WmAppResources *resources = app->resources;
    WmAppInputState *input = app->input;
    WmAppFlowState *flow = app->flow;
    WmPlatform *platform = resources->platform;
    WmLayout *layout = resources->layout;
    WmResourceScene *resource_scene = resources->resource_scene;
    WmPreviewScene *preview_scene = resources->preview_scene;
    WmBoardScene *board_scene = resources->board_scene;
    WmHealthScene *health_scene = resources->health_scene;
    WmOptionsScene *options_scene = resources->options_scene;
    WmSdScene *sd_scene = resources->sd_scene;
    WmPointer *pointer = resources->pointer;
    WmAudio *audio = resources->audio;
    WmHomeOverlay *home = resources->home;
    WmChannelDrag *drag = resources->drag;

    WmEvent event;
    while (wm_platform_poll(platform, &event)) {
        bool text_input_active =
            !health_frame && !layout && !menu->home_open && !menu->notice[0] &&
            !wm_home_overlay_active(home) &&
            ((menu->screen == WM_SCREEN_BOARD && board_scene &&
              wm_board_scene_child(board_scene) == WM_BOARD_CHILD_COMPOSE) ||
             (menu->screen == WM_SCREEN_SETTINGS && options_scene &&
              wm_options_scene_nickname_keyboard_visible(options_scene)));
        if (cc_window_controls_event(&input->window_controls, platform, &event,
                                     text_input_active))
            continue;
        bool memo_release_outside =
            event.outside_viewport && event.type == WM_EVENT_POINTER_UP &&
            menu->screen == WM_SCREEN_BOARD && wm_board_scene_dragging(board_scene);
        if (event.outside_viewport && input->menu_pointer.drag_button == 0 &&
            !memo_release_outside &&
            (event.type == WM_EVENT_POINTER_DOWN ||
             event.type == WM_EVENT_POINTER_MOVE ||
             event.type == WM_EVENT_POINTER_UP)) {
            event.type = WM_EVENT_POINTER_LEAVE;
        }
        if (event.type == WM_EVENT_POINTER_LEAVE &&
            input->menu_pointer.drag_button != 0 && !event.cancel_capture) {
            continue;
        }
        if (event.type == WM_EVENT_POINTER_LEAVE) {
            input->pointer_inside = false;
        } else if (event.type == WM_EVENT_POINTER_MOVE ||
                   event.type == WM_EVENT_POINTER_DOWN ||
                   event.type == WM_EVENT_POINTER_UP) {
            input->pointer_x = event.x;
            input->pointer_y = event.y;
            input->pointer_inside = true;
        }
        if (event.type == WM_EVENT_QUIT) {
            *running = false;
        } else if (layout) {
            if (event.type == WM_EVENT_KEY_DOWN &&
                (event.key == WM_KEY_ESCAPE || event.key == 'q'))
                *running = false;
        } else if (health_frame) {
            if (wm_health_scene_ready(health_scene) &&
                ((event.type == WM_EVENT_POINTER_DOWN &&
                  event.button == WM_POINTER_LEFT) ||
                 (event.type == WM_EVENT_KEY_DOWN &&
                  (event.key == WM_KEY_ENTER || event.key == WM_KEY_ESCAPE ||
                   event.key == ' ' || event.key == 'a' || event.key == 'A')))) {
                if (wm_health_scene_accept(health_scene))
                    wm_audio_play(audio, "click");
            }
        } else if (flow->entrance_active || wm_scene_fader_active(&flow->fade.clock) ||
                   wm_menu_restart_active(&flow->restart)) {
            update_transition_pointer(pointer, &event);
        } else if (wm_home_overlay_active(home)) {
            handle_home_event(app, &event);
        } else if (event.type == WM_EVENT_POINTER_MOVE) {
            wm_pointer_move(pointer, (float)event.x, (float)event.y);
            if (menu->screen == WM_SCREEN_GRID && resource_scene)
                wm_resource_scene_pointer_moved(resource_scene);
            input->keyboard_focus = false;
            if (route_scene_pointer(app, &event))
                continue;
            wm_app_menu_pointer_move(&input->menu_pointer, menu, resource_scene,
                                     preview_scene, drag, audio, event.x, event.y);
        } else if (event.type == WM_EVENT_POINTER_DOWN) {
            wm_pointer_move(pointer, (float)event.x, (float)event.y);
            input->keyboard_focus = false;
            if (route_scene_pointer(app, &event))
                continue;
            wm_app_menu_pointer_down(&input->menu_pointer, menu, resource_scene,
                                     preview_scene, drag, audio, event.button, event.x,
                                     event.y);
        } else if (event.type == WM_EVENT_POINTER_UP) {
            wm_pointer_move(pointer, (float)event.x, (float)event.y);
            if (route_scene_pointer(app, &event))
                continue;
            WmAppMenuPointerRelease release =
                wm_app_menu_pointer_up(&input->menu_pointer, menu, resource_scene,
                                       preview_scene, drag, pointer, audio, &event);
            if (release.released_drag)
                continue;
            if (release.activated.type == WM_HIT_HOME) {
                wm_app_try_enter_home(app, frame_start);
            } else if (release.activated.type != WM_HIT_NONE) {
                wm_app_activate_hit(menu, audio, resource_scene, board_scene,
                                    options_scene, &flow->fade, release.activated);
                if (release.activated.type == WM_HIT_BOARD &&
                    menu->screen == WM_SCREEN_BOARD)
                    flow->board_entry_hover_pending = true;
            }
        } else if (event.type == WM_EVENT_POINTER_LEAVE) {
            wm_app_menu_pointer_leave(&input->menu_pointer, drag, audio);
            wm_pointer_hide(pointer);
            wm_sd_scene_hover(sd_scene, (WmSdHit){WM_SD_CONTROL_NONE, 0});
            if (flow->active_storage)
                wm_storage_scene_hover(flow->active_storage,
                                       (WmStorageHit){WM_STORAGE_CONTROL_NONE, -1});
            wm_app_board_pointer_leave(&input->board, board_scene,
                                       event.cancel_capture);
            wm_app_options_pointer_leave(&input->scene, options_scene);
            if (event.cancel_capture) {
                if (board_scene)
                    wm_board_scene_keyboard_modifiers(board_scene, false, false);
                if (options_scene)
                    wm_options_scene_keyboard_modifiers(options_scene, false, false);
            }
        } else if (event.type == WM_EVENT_KEY_MODIFIERS) {
            handle_key_modifiers(app, &event);
        } else if (event.type == WM_EVENT_KEY_DOWN) {
            handle_key_down(app, &event, frame_start);
        }
    }
}
