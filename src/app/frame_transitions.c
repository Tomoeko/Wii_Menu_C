#include "frame_transitions.h"

bool wm_app_try_enter_home(WmAppRuntime *app, uint64_t now) {
    WmMenu *menu = app->menu;
    WmAppResources *resources = app->resources;
    WmAppInputState *input = app->input;
    WmAppFlowState *flow = app->flow;

    if (menu->screen == WM_SCREEN_SETTINGS && resources->options_scene &&
        wm_options_scene_snapshot(resources->options_scene).locked) {
        return false;
    }
    if (!resources->home || menu->notice[0] || menu->transition != WM_TRANSITION_NONE ||
        wm_scene_fader_active(&flow->fade.clock) ||
        wm_channel_drag_state(resources->drag).phase != WM_CHANNEL_DRAG_NONE ||
        !wm_home_overlay_open(resources->home)) {
        return false;
    }

    /* The GLES2 backend permits one capture target. The zoom capture is no
     * longer needed after the grid has settled. */
    wm_resource_scene_release_preview_capture(resources->resource_scene);
    menu->home_open = true;
    flow->home_opened_at = now;
    wm_board_scene_cancel_pointer(resources->board_scene);
    wm_audio_stop_loop(resources->audio, "WIPL_SE_BOARD_DRAG");
    wm_audio_stop_loop(resources->audio, "WIPL_SE_MESSAGE_SCROLL");
    if (input->scene.options_held_arrow) {
        wm_options_scene_pointer_up(resources->options_scene);
        input->scene.options_held_arrow = false;
    }
    flow->home_underlay_elapsed = (float)(now - flow->started) / 1000000000.0f;
    flow->home_underlay_preview_elapsed =
        (float)(now - flow->preview_started) / 1000000000.0f;
    wm_app_renderer_invalidate_home_underlay(app->renderer);
    return true;
}

static void reset_menu_scenes(WmAppRuntime *app, uint64_t now) {
    WmAppResources *resources = app->resources;
    WmAppFlowState *flow = app->flow;

    wm_menu_return_to_menu(app->menu);
    wm_resource_scene_restart(resources->resource_scene);
    wm_board_scene_reset(resources->board_scene);
    wm_options_scene_reset(resources->options_scene);
    wm_sd_scene_reset(resources->sd_scene);
    flow->started = now;
    flow->preview_started = now;
    flow->preview_running_slot = -1;
    flow->active_storage = NULL;
}

bool wm_app_advance_before_events(WmAppRuntime *app, uint64_t frame_start,
                                  float elapsed) {
    WmMenu *menu = app->menu;
    WmAppResources *resources = app->resources;
    WmAppInputState *input = app->input;
    WmAppFlowState *flow = app->flow;
    bool restart_started_this_frame = false;
    bool health_frame = wm_health_scene_active(resources->health_scene);

    if (health_frame) {
        wm_health_scene_advance(resources->health_scene, elapsed * 60.0f);
        if (!wm_health_scene_active(resources->health_scene)) {
            flow->started = frame_start;
            flow->preview_started = frame_start;
            wm_resource_scene_restart(resources->resource_scene);
            flow->entrance_active = true;
            flow->entrance_frames = 0.0f;
        }
    }
    if (!health_frame && flow->entrance_active) {
        flow->entrance_frames += elapsed * 60.0f;
        if (wm_menu_entrance_complete(flow->entrance_frames)) {
            flow->entrance_active = false;
        }
    }
    if (!resources->layout && !health_frame &&
        !wm_home_overlay_active(resources->home) &&
        !wm_menu_restart_active(&flow->restart)) {
        wm_menu_tick(menu, elapsed);
    }
    if (!resources->layout && !health_frame &&
        !wm_home_overlay_active(resources->home) &&
        !wm_menu_restart_active(&flow->restart) &&
        wm_scene_fader_advance(&flow->fade.clock, elapsed * 60.0f)) {
        if (wm_menu_switch_screen_at_black(menu, flow->fade.destination)) {
            if (flow->fade.destination == WM_SCREEN_SETTINGS) {
                if (flow->board_settings_request ==
                    WM_BOARD_ACTION_OPEN_CONNECT24_SETTINGS) {
                    wm_options_scene_open_connect24(resources->options_scene);
                } else if (flow->board_settings_request ==
                           WM_BOARD_ACTION_OPEN_SETTINGS) {
                    wm_options_scene_open_internet(resources->options_scene);
                } else {
                    wm_options_scene_open(resources->options_scene);
                }
            } else if (flow->fade.destination == WM_SCREEN_SD) {
                wm_sd_scene_open(resources->sd_scene, flow->sd_page, flow->sd_help_seen,
                                 WM_SD_MEDIA_READY);
            } else if (flow->fade.destination == WM_SCREEN_BOARD) {
                wm_options_scene_reset(resources->options_scene);
                flow->board_entry_hover_pending = true;
            }
        }
        input->menu_pointer.hovered = (WmHit){WM_HIT_NONE, -1};
        input->menu_pointer.pressed = input->menu_pointer.hovered;
    }
    wm_platform_set_fade_alpha(resources->platform,
                               flow->entrance_active
                                   ? wm_menu_entrance_alpha(flow->entrance_frames)
                                   : wm_scene_fader_alpha(&flow->fade.clock));

    if (wm_home_overlay_active(resources->home)) {
        wm_home_overlay_advance(resources->home, elapsed * 60.0f);
        WmHomeOutcome outcome = wm_home_overlay_take_outcome(resources->home);
        if (outcome != WM_HOME_OUTCOME_NONE) {
            uint64_t paused = frame_start - flow->home_opened_at;
            flow->started += paused;
            flow->preview_started += paused;
            wm_app_renderer_release_home_underlay(app->renderer);
            input->home_hovered = WM_HOME_CONTROL_NONE;
            input->home_pressed = WM_HOME_CONTROL_NONE;
            input->menu_pointer.hovered = (WmHit){WM_HIT_NONE, -1};
            input->menu_pointer.pressed = input->menu_pointer.hovered;
            input->keyboard_focus = false;
            if (outcome == WM_HOME_OUTCOME_RETURN_MENU) {
                wm_channel_drag_cancel(resources->drag);
                input->menu_pointer.drag_button = 0;
                input->menu_pointer.drag_has_previous = false;
                if (resources->restart_scene && wm_menu_restart_start(&flow->restart)) {
                    restart_started_this_frame = true;
                    wm_audio_reset_all(resources->audio);
                    flow->active_storage = NULL;
                } else {
                    reset_menu_scenes(app, frame_start);
                }
            } else {
                menu->home_open = false;
            }
        }
    }
    if (!resources->layout && !restart_started_this_frame &&
        wm_menu_restart_active(&flow->restart) &&
        wm_menu_restart_advance(&flow->restart, elapsed * 60.0f)) {
        reset_menu_scenes(app, frame_start);
        wm_audio_sync(resources->audio, menu);
    }
    if (!resources->layout && wm_menu_restart_active(&flow->restart)) {
        wm_platform_set_fade_alpha(resources->platform,
                                   wm_menu_restart_alpha(&flow->restart));
    }
    return health_frame;
}

void wm_app_update_preview_clock(WmAppRuntime *app, uint64_t frame_start) {
    WmMenu *menu = app->menu;
    WmAppFlowState *flow = app->flow;

    if (menu->screen != WM_SCREEN_PREVIEW) {
        flow->preview_running_slot = -1;
    } else if (menu->transition == WM_TRANSITION_SELECT) {
        /* The native banner clock starts after the 28-frame grid zoom.
         * The zoom capture itself samples the banner's initial pose. */
        flow->preview_started = frame_start;
        flow->preview_running_slot = -1;
    } else if (menu->transition == WM_TRANSITION_NONE &&
               flow->preview_running_slot != menu->selected) {
        bool navigated = flow->preview_running_slot >= 0;
        flow->preview_started = frame_start;
        flow->preview_running_slot = menu->selected;
        wm_preview_scene_set_module_lead(app->resources->preview_scene,
                                         navigated ? 10.0f : 0.0f);
    }
}
