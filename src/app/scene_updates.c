#include "scene_updates.h"

#include "wii_menu/audio/hover_audio.h"

#include <stddef.h>

void wm_app_settings_advance(WmAppSettingsUpdate *update, float frames) {
    if (update->options) {
        if (!wm_scene_fader_active(&update->fade->clock))
            wm_options_scene_advance(update->options, frames);

        WmOptionsAction action = wm_options_scene_take_action(update->options);
        if (action == WM_OPTIONS_ACTION_EXITED &&
            wm_scene_fader_start(&update->fade->clock)) {
            update->fade->destination =
                *update->board_settings_request != WM_BOARD_ACTION_NONE
                    ? WM_SCREEN_BOARD
                    : WM_SCREEN_GRID;
            *update->board_settings_request = WM_BOARD_ACTION_NONE;
        } else if (action == WM_OPTIONS_ACTION_CHANNEL_STORAGE ||
                   action == WM_OPTIONS_ACTION_WII_STORAGE ||
                   action == WM_OPTIONS_ACTION_GAMECUBE_STORAGE) {
            WmStorageKind kind = WM_STORAGE_CHANNELS;
            if (action == WM_OPTIONS_ACTION_WII_STORAGE)
                kind = WM_STORAGE_WII_SAVES;
            else if (action == WM_OPTIONS_ACTION_GAMECUBE_STORAGE)
                kind = WM_STORAGE_GAMECUBE_SAVES;
            *update->active_storage = update->storage_scenes[kind];
            if (*update->active_storage)
                wm_storage_scene_open(*update->active_storage, WM_STORAGE_WII);
        }

        /* Re-hit a settled Settings page even when the hand has not moved. */
        if (!*update->active_storage && !update->menu->notice[0] &&
            update->pointer_inside && !wm_scene_fader_active(&update->fade->clock)) {
            WmOptionsControl next = wm_options_scene_hit(
                update->options, update->pointer_x, update->pointer_y);
            if (next != *update->hovered) {
                if (next != WM_OPTIONS_CONTROL_NONE)
                    wm_audio_play(update->audio,
                                  wm_options_scene_hover_cue(update->options, next));
                *update->hovered = next;
                wm_options_scene_hover(update->options, next);
            }
        }
    }

    if (*update->active_storage) {
        WmStorageScene *storage = *update->active_storage;
        wm_storage_scene_advance(storage, frames);
        WmStorageAction action = wm_storage_scene_take_action(storage, NULL, NULL);
        if (action == WM_STORAGE_ACTION_EXITED) {
            *update->active_storage = NULL;
            wm_options_scene_back(update->options);
        }
        /* Keep the same local storage pointer after EXITED: the scene may
         * have queued its balloon cue before returning to Options. */
        if (wm_storage_scene_take_balloon_cue(storage))
            wm_audio_play(update->audio, "balloon");
    }
}

void wm_app_sd_advance(WmAppSdUpdate *update, float frames) {
    if (update->fade->clock.phase != WM_SCENE_FADER_OUT)
        wm_sd_scene_advance(update->scene, frames,
                            update->fade->clock.phase == WM_SCENE_FADER_IN);

    WmSdEvent event;
    while (wm_sd_scene_take_event(update->scene, &event)) {
        switch (event.type) {
            case WM_SD_EVENT_EXIT:
                if (wm_scene_fader_start(&update->fade->clock))
                    update->fade->destination = WM_SCREEN_GRID;
                break;
            case WM_SD_EVENT_PAGE_CHANGED:
                *update->page = wm_sd_scene_page(update->scene);
                break;
            case WM_SD_EVENT_HELP_CLOSE:
                *update->help_seen = wm_sd_scene_help_seen(update->scene);
                break;
            case WM_SD_EVENT_HOVER_SOUND:
                wm_audio_play(update->audio, wm_hover_audio_sd_cue(event.control));
                break;
            case WM_SD_EVENT_CONFIRM_SOUND:
                wm_audio_play(update->audio, "confirm");
                break;
            case WM_SD_EVENT_CANCEL_SOUND:
                wm_audio_play(update->audio, "WIPL_SE_CANCEL");
                break;
            case WM_SD_EVENT_PAGE_SOUND:
                wm_audio_play(update->audio, "page");
                break;
            case WM_SD_EVENT_INFO_SOUND:
                wm_audio_play(update->audio, "infoWindow");
                break;
            case WM_SD_EVENT_BALLOON_SOUND:
                wm_audio_play(update->audio, "balloon");
                break;
            case WM_SD_EVENT_CHANNEL_SELECTED:
            case WM_SD_EVENT_HELP_OPEN:
            case WM_SD_EVENT_NONE:
                break;
        }
    }
}
