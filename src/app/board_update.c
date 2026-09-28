#include "board_update.h"

#include "wii_menu/persistence/board_store.h"

#include <stdint.h>
#include <stddef.h>
#include <stdio.h>

static void reconcile_hover(WmAppBoardUpdate *update) {
    WmBoardScene *board = update->board;
    WmAppBoardInput *input = update->input;

    if (*update->entry_hover_pending && wm_board_scene_phase(board) == WM_BOARD_READY) {
        WmBoardHit initial =
            update->pointer_inside
                ? wm_board_scene_hit(board, update->pointer_x, update->pointer_y)
                : (WmBoardHit){WM_BOARD_CONTROL_NONE, SIZE_MAX};
        wm_app_board_play_hover(update->audio, initial.control);
        input->hovered = initial;
        wm_board_scene_hover(board, initial);
        *update->entry_hover_pending = false;
    }

    WmBoardPhase phase = wm_board_scene_phase(board);
    /* Common arrows retain one hover owner during a turn. Re-hit their
     * settled bounds even if the physical pointer has not moved. */
    if (update->pointer_inside && !update->menu->notice[0] &&
        !wm_board_scene_dragging(board) &&
        wm_board_scene_child(board) == WM_BOARD_CHILD_NONE &&
        (phase == WM_BOARD_READY || phase == WM_BOARD_DATE_SCROLL ||
         phase == WM_BOARD_MEMO_PAGE)) {
        WmBoardHit next =
            wm_board_scene_hit(board, update->pointer_x, update->pointer_y);
        if (next.control != input->hovered.control ||
            next.memo_index != input->hovered.memo_index) {
            wm_app_board_play_hover(update->audio, next.control);
            input->hovered = next;
            wm_board_scene_hover(board, next);
        }
    }
    if (phase != WM_BOARD_READY && phase != WM_BOARD_MEMO_READ &&
        phase != WM_BOARD_DATE_SCROLL && phase != WM_BOARD_MEMO_PAGE) {
        input->hovered = (WmBoardHit){WM_BOARD_CONTROL_NONE, 0};
    }
}

static void handle_action(WmAppBoardUpdate *update) {
    float departing_grid_frame = 0.0f;
    if (!wm_board_scene_grid_overlay(update->board, &departing_grid_frame))
        *update->visited = true;

    WmBoardAction action = wm_board_scene_take_action(update->board, NULL);
    if (action == WM_BOARD_ACTION_EXITED) {
        wm_menu_switch_screen_at_black(update->menu, WM_SCREEN_GRID);
        *update->menu_hover =
            update->pointer_inside
                ? wm_resource_scene_hit(update->resource_scene, update->menu,
                                        update->pointer_x, update->pointer_y)
                : (WmHit){WM_HIT_NONE, -1};
        wm_audio_stop_loop(update->audio, "WIPL_SE_BOARD_DRAG");
        wm_audio_stop_loop(update->audio, "WIPL_SE_MESSAGE_SCROLL");
    } else if ((action == WM_BOARD_ACTION_OPEN_SETTINGS ||
                action == WM_BOARD_ACTION_OPEN_CONNECT24_SETTINGS) &&
               wm_scene_fader_start(&update->fade->clock)) {
        *update->settings_request = action;
        update->fade->destination = WM_SCREEN_SETTINGS;
    } else if ((action == WM_BOARD_ACTION_MEMO_POSTED ||
                action == WM_BOARD_ACTION_ERASE_MEMO ||
                action == WM_BOARD_ACTION_MEMO_READ ||
                action == WM_BOARD_ACTION_MEMO_MOVED) &&
               update->state_path) {
        char error[160] = {0};
        if (!wm_board_store_save(update->state_path, update->board, error,
                                 sizeof(error))) {
            fprintf(stderr, "Could not save Message Board memos: %s\n", error);
        }
    }
}

static void update_drag_audio(WmAppBoardUpdate *update) {
    float cue_pan = 0.0f;
    WmBoardDragCue cue;
    while ((cue = wm_board_scene_take_drag_cue(update->board, &cue_pan)) !=
           WM_BOARD_DRAG_CUE_NONE) {
        wm_audio_play_panned(update->audio,
                             cue == WM_BOARD_DRAG_CUE_HOLD ? "WIPL_SE_BOARD_HOLD"
                                                           : "WIPL_SE_BOARD_RELEASE",
                             cue_pan);
    }

    float drag_gain = 0.0f;
    float drag_pan = 0.0f;
    float memo_drag_pitch = 1.0f;
    if (wm_board_scene_drag_mix(update->board, &drag_gain, &drag_pan,
                                &memo_drag_pitch)) {
        wm_audio_hold_loop(update->audio, "WIPL_SE_BOARD_DRAG", drag_gain, drag_pan,
                           memo_drag_pitch);
    } else {
        wm_audio_stop_loop(update->audio, "WIPL_SE_BOARD_DRAG");
    }
    if (wm_board_scene_reader_scroll_sound_active(update->board)) {
        wm_audio_start_loop(update->audio, "WIPL_SE_MESSAGE_SCROLL");
    } else {
        wm_audio_stop_loop(update->audio, "WIPL_SE_MESSAGE_SCROLL");
    }
}

void wm_app_board_advance(WmAppBoardUpdate *update, float frames, bool active,
                          bool resource_mode, bool home_active) {
    if (update->menu->screen != WM_SCREEN_BOARD)
        *update->entry_hover_pending = false;
    if (update->menu->screen != WM_SCREEN_BOARD || home_active)
        update->input->hovered = (WmBoardHit){WM_BOARD_CONTROL_NONE, 0};

    if (resource_mode && active && update->board &&
        update->menu->screen == WM_SCREEN_BOARD) {
        wm_board_scene_advance(update->board, frames);
        wm_app_play_board_compose_cues(update->audio, update->board);
        wm_app_play_board_sound_events(update->audio, update->board);
        const char *reader_cue;
        while ((reader_cue = wm_board_scene_take_reader_cue(update->board)) != NULL) {
            wm_audio_play(update->audio, reader_cue);
        }
        reconcile_hover(update);
        handle_action(update);
        update_drag_audio(update);
    } else if (resource_mode && active && update->board &&
               update->menu->screen == WM_SCREEN_GRID) {
        /* Ordered arrivals continue behind ChannelSelect after a return to
         * today's date. The same clock and cues advance only once. */
        wm_board_scene_advance_parked(update->board, frames);
        wm_app_play_board_sound_events(update->audio, update->board);
    }

    if (resource_mode && update->resource_scene && update->board) {
        if (update->menu->screen == WM_SCREEN_GRID && !home_active)
            wm_board_scene_refresh_today(update->board, wm_app_today_date());
        wm_resource_scene_set_message_badge(
            update->resource_scene, wm_board_scene_today_count(update->board),
            !*update->visited && wm_board_scene_today_unread_count(update->board) > 0);
    }
}
