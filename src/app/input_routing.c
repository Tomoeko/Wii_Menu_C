#define _POSIX_C_SOURCE 200809L

#include "input_routing.h"

#include <string.h>
#include <time.h>

WmBoardDate wm_app_today_date(void) {
    time_t now = time(NULL);
    struct tm date;
    if (!localtime_r(&now, &date)) {
        return (WmBoardDate){2000, 1, 1};
    }
    return (WmBoardDate){date.tm_year + 1900, date.tm_mon + 1, date.tm_mday};
}

bool wm_app_play_board_compose_cues_with_non_scroll(WmAudio *audio, WmBoardScene *board,
                                                    bool *non_scroll_played) {
    const char *cue;
    bool played = false;
    if (non_scroll_played) {
        *non_scroll_played = false;
    }
    while ((cue = wm_board_scene_take_compose_key_cue(board)) != NULL) {
        wm_audio_play(audio, cue);
        played = true;
        if (non_scroll_played && strcmp(cue, "WIPL_SE_LINE_SCROLL") != 0) {
            *non_scroll_played = true;
        }
    }
    return played;
}

bool wm_app_play_board_compose_cues(WmAudio *audio, WmBoardScene *board) {
    return wm_app_play_board_compose_cues_with_non_scroll(audio, board, NULL);
}

void wm_app_play_board_sound_events(WmAudio *audio, WmBoardScene *board) {
    WmBoardSoundEvent event;
    while (wm_board_scene_take_sound_event(board, &event)) {
        wm_audio_play_panned(audio, event.cue, event.pan);
    }
}

void wm_app_activate_hit(WmMenu *menu, WmAudio *audio, WmResourceScene *resource_scene,
                         WmBoardScene *board_scene, WmOptionsScene *options_scene,
                         WmAppSceneFade *fade, WmHit hit) {
    if (hit.type != WM_HIT_NONE) {
        wm_resource_scene_dismiss_balloon(resource_scene);
    }
    switch (hit.type) {
        case WM_HIT_CHANNEL:
            if (wm_menu_select(menu, hit.slot)) {
                wm_audio_play(audio, "click");
                wm_audio_play(audio, "select");
            }
            break;
        case WM_HIT_PAGE_PREVIOUS:
        case WM_HIT_PAGE_NEXT:
            if (wm_menu_change_page(menu, hit.type == WM_HIT_PAGE_NEXT ? 1 : -1)) {
                wm_resource_scene_press_arrow(resource_scene,
                                              hit.type == WM_HIT_PAGE_NEXT ? 1 : -1);
                wm_audio_play(audio, "page");
            }
            break;
        case WM_HIT_PREVIEW_PREVIOUS:
        case WM_HIT_PREVIEW_NEXT:
            if (wm_menu_change_preview(menu,
                                       hit.type == WM_HIT_PREVIEW_NEXT ? 1 : -1)) {
                wm_audio_play(audio, "page");
            }
            break;
        case WM_HIT_SETTINGS:
            if (options_scene && menu->screen == WM_SCREEN_GRID &&
                menu->transition == WM_TRANSITION_NONE &&
                wm_scene_fader_start(&fade->clock)) {
                fade->destination = WM_SCREEN_SETTINGS;
                wm_audio_play(audio, "confirm");
            }
            break;
        case WM_HIT_BOARD:
            if (wm_menu_open_screen(menu, WM_SCREEN_BOARD)) {
                wm_resource_scene_retire_footer_focus(resource_scene);
                wm_board_scene_set_grid_page(board_scene, menu->page);
                wm_board_scene_open(board_scene, wm_app_today_date());
                wm_audio_play(audio, "confirm");
            }
            break;
        case WM_HIT_SD:
            if (menu->screen == WM_SCREEN_GRID &&
                menu->transition == WM_TRANSITION_NONE &&
                wm_scene_fader_start(&fade->clock)) {
                fade->destination = WM_SCREEN_SD;
                wm_audio_play(audio, "confirm");
            }
            break;
        case WM_HIT_BACK: {
            bool preview = menu->screen == WM_SCREEN_PREVIEW;
            if (wm_menu_back(menu)) {
                if (preview) {
                    wm_audio_play(audio, "WIPL_SE_BT_PUSH");
                    wm_audio_play(audio, "WIPL_SE_CH_UNSELECT");
                } else {
                    wm_audio_play(audio, "back");
                }
            }
            break;
        }
        case WM_HIT_START:
            if (wm_menu_start_preview(menu)) {
                wm_audio_play(audio, "confirm");
            }
            break;
        case WM_HIT_NOTICE_DISMISS:
            if (wm_menu_dismiss_notice(menu)) {
                wm_audio_play(audio, "back");
            }
            break;
        case WM_HIT_HOME:
        case WM_HIT_HOME_CLOSE:
        case WM_HIT_HOME_MENU:
            break;
        case WM_HIT_NONE:
            break;
    }
}

void wm_app_handle_key(WmMenu *menu, WmAudio *audio, WmResourceScene *resource_scene,
                       WmBoardScene *board_scene, WmOptionsScene *options_scene,
                       WmAppSceneFade *fade, WmKey key, int *focused_slot) {
    if (menu->notice[0]) {
        if (key == WM_KEY_ENTER || key == WM_KEY_ESCAPE || key == WM_KEY_BACKSPACE) {
            wm_app_activate_hit(menu, audio, resource_scene, board_scene, options_scene,
                                fade, (WmHit){WM_HIT_NOTICE_DISMISS, -1});
        }
        return;
    }
    if (key == WM_KEY_ESCAPE || key == WM_KEY_BACKSPACE) {
        if (menu->screen == WM_SCREEN_BOARD && board_scene) {
            WmBoardPhase phase = wm_board_scene_phase(board_scene);
            WmBoardChild child = wm_board_scene_child(board_scene);
            bool silent_keyboard_overlay =
                wm_board_scene_compose_keyboard_overlay_visible(board_scene);
            if (wm_board_scene_back(board_scene)) {
                if (!wm_app_play_board_compose_cues(audio, board_scene) &&
                    !silent_keyboard_overlay) {
                    const char *cue =
                        phase == WM_BOARD_READY && child == WM_BOARD_CHILD_NONE
                            ? "confirm"
                        : phase == WM_BOARD_MEMO_READ ? "WIPL_SE_BOARD_UNSELECT"
                                                      : "back";
                    wm_audio_play(audio, cue);
                }
            }
        } else if (menu->screen == WM_SCREEN_SETTINGS && options_scene) {
            const char *cue =
                wm_options_scene_click_cue(options_scene, WM_OPTIONS_CONTROL_BACK);
            if (wm_options_scene_back(options_scene) && cue) {
                wm_audio_play(audio, cue);
            }
        } else {
            wm_app_activate_hit(menu, audio, resource_scene, board_scene, options_scene,
                                fade, (WmHit){WM_HIT_BACK, -1});
        }
        return;
    }
    if (menu->screen == WM_SCREEN_PREVIEW) {
        if (key == WM_KEY_LEFT) {
            wm_app_activate_hit(menu, audio, resource_scene, board_scene, options_scene,
                                fade, (WmHit){WM_HIT_PREVIEW_PREVIOUS, -1});
        }
        if (key == WM_KEY_RIGHT) {
            wm_app_activate_hit(menu, audio, resource_scene, board_scene, options_scene,
                                fade, (WmHit){WM_HIT_PREVIEW_NEXT, -1});
        }
        return;
    }
    if (menu->screen != WM_SCREEN_GRID) {
        return;
    }
    if (key == WM_KEY_LEFT) {
        if (*focused_slot % 4 > 0) {
            (*focused_slot)--;
        } else {
            wm_app_activate_hit(menu, audio, resource_scene, board_scene, options_scene,
                                fade, (WmHit){WM_HIT_PAGE_PREVIOUS, -1});
        }
    } else if (key == WM_KEY_RIGHT) {
        if (*focused_slot % 4 < 3) {
            (*focused_slot)++;
        } else {
            wm_app_activate_hit(menu, audio, resource_scene, board_scene, options_scene,
                                fade, (WmHit){WM_HIT_PAGE_NEXT, -1});
        }
    } else if (key == WM_KEY_UP && *focused_slot >= 4) {
        *focused_slot -= 4;
    } else if (key == WM_KEY_DOWN && *focused_slot < 8) {
        *focused_slot += 4;
    } else if (key == WM_KEY_ENTER) {
        wm_app_activate_hit(
            menu, audio, resource_scene, board_scene, options_scene, fade,
            (WmHit){WM_HIT_CHANNEL, menu->page * WM_CHANNELS_PER_PAGE + *focused_slot});
    }
}
