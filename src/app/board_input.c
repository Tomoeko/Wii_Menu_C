#include "board_input.h"

#include "input_routing.h"

#include "wii_menu/audio/hover_audio.h"

#include <stdint.h>
#include <stddef.h>

static bool compose_scroll_arrow(WmBoardControl control) {
    return control == WM_BOARD_CONTROL_COMPOSE_SCROLL_UP ||
           control == WM_BOARD_CONTROL_COMPOSE_SCROLL_DOWN;
}

static bool compose_keyboard_control(WmBoardControl control) {
    return control >= WM_BOARD_CONTROL_COMPOSE_KEY_FIRST &&
           control <= WM_BOARD_CONTROL_COMPOSE_KEY_LAST;
}

void wm_app_board_play_hover(WmAudio *audio, WmBoardControl control) {
    const char *cue = wm_hover_audio_board_cue(control);
    if (cue)
        wm_audio_play(audio, cue);
}

static bool moved_beyond_press_radius(int x, int y, int press_x, int press_y) {
    int64_t dx = (int64_t)x - press_x;
    int64_t dy = (int64_t)y - press_y;
    if (dx < -3 || dx > 3 || dy < -3 || dy > 3)
        return true;
    return dx * dx + dy * dy > 9;
}

void wm_app_board_pointer_move(WmAppBoardInput *input, WmBoardScene *board,
                               WmAudio *audio, int x, int y) {
    if (input->pressed.control == WM_BOARD_CONTROL_MEMO &&
        !wm_board_scene_dragging(board)) {
        if (moved_beyond_press_radius(x, y, input->press_x, input->press_y) &&
            wm_board_scene_pointer_down(board, input->pressed, input->press_x,
                                        input->press_y)) {
            wm_board_scene_pointer_move(board, x, y);
        }
    } else if (wm_board_scene_dragging(board)) {
        wm_board_scene_pointer_move(board, x, y);
    }
    if (wm_board_scene_dragging(board))
        return;

    WmBoardHit next = wm_board_scene_hit(board, x, y);
    if (next.control != input->hovered.control ||
        next.memo_index != input->hovered.memo_index) {
        wm_app_board_play_hover(audio, next.control);
    }
    input->hovered = next;
    wm_board_scene_hover(board, next);
}

void wm_app_board_pointer_down(WmAppBoardInput *input, WmBoardScene *board,
                               WmAudio *audio, WmPointerButton button, int x, int y) {
    WmBoardHit hit = wm_board_scene_hit(board, x, y);
    WmBoardPhase phase = wm_board_scene_phase(board);
    if (phase == WM_BOARD_DATE_SCROLL || phase == WM_BOARD_MEMO_PAGE) {
        /* A press begun during a turn must not activate after it settles. */
        input->pressed = (WmBoardHit){WM_BOARD_CONTROL_NONE, SIZE_MAX};
        return;
    }
    if (button == WM_POINTER_RIGHT) {
        wm_board_scene_hover(board, hit);
        if (wm_board_scene_activate_secondary(board, hit)) {
            input->pressed = (WmBoardHit){WM_BOARD_CONTROL_NONE, 0};
            wm_app_play_board_compose_cues(audio, board);
            return;
        }
    }
    if ((button == WM_POINTER_MIDDLE || button == WM_POINTER_RIGHT) &&
        hit.control == WM_BOARD_CONTROL_MEMO &&
        wm_board_scene_pointer_down(board, hit, x, y)) {
        input->pressed = (WmBoardHit){WM_BOARD_CONTROL_NONE, 0};
        return;
    }
    input->pressed =
        button == WM_POINTER_LEFT ? hit : (WmBoardHit){WM_BOARD_CONTROL_NONE, 0};
    input->press_x = x;
    input->press_y = y;
    input->held_keyboard =
        button == WM_POINTER_LEFT && compose_keyboard_control(input->pressed.control) &&
        wm_board_scene_hold_compose_control(board, input->pressed.control);
    if (input->held_keyboard) {
        input->pressed = (WmBoardHit){WM_BOARD_CONTROL_NONE, 0};
        wm_app_play_board_compose_cues(audio, board);
    }
}

static const char *board_click_cue(WmBoardControl control) {
    switch (control) {
        case WM_BOARD_CONTROL_MEMO:
            return "WIPL_SE_BOARD_SELECT";
        case WM_BOARD_CONTROL_MEMO_BACK:
            return "WIPL_SE_BOARD_UNSELECT";
        case WM_BOARD_CONTROL_MEMO_TRASH:
            return "WIPL_SE_BT_PUSH";
        case WM_BOARD_CONTROL_CALENDAR_DAY:
        case WM_BOARD_CONTROL_PREVIOUS:
        case WM_BOARD_CONTROL_NEXT:
        case WM_BOARD_CONTROL_COMPOSE_ADDRESS_PREVIOUS:
        case WM_BOARD_CONTROL_COMPOSE_ADDRESS_NEXT:
        case WM_BOARD_CONTROL_MEMO_SCROLL_UP:
        case WM_BOARD_CONTROL_MEMO_SCROLL_DOWN:
        case WM_BOARD_CONTROL_COMPOSE_EDIT:
        case WM_BOARD_CONTROL_COMPOSE_ADDRESS_EDIT:
        case WM_BOARD_CONTROL_COMPOSE_ADDRESS_DIALOG_OK:
        case WM_BOARD_CONTROL_COMPOSE_ADDRESS_MII:
        case WM_BOARD_CONTROL_COMPOSE_ADDRESS_INFO:
        case WM_BOARD_CONTROL_COMPOSE_ADDRESS_CHANGE_NICKNAME:
        case WM_BOARD_CONTROL_COMPOSE_ADDRESS_ERASE:
        case WM_BOARD_CONTROL_COMPOSE_ADDRESS_DIALOG_YES:
        case WM_BOARD_CONTROL_COMPOSE_ADDRESS_DIALOG_NO:
            return NULL;
        case WM_BOARD_CONTROL_COMPOSE_ADDRESS:
        case WM_BOARD_CONTROL_COMPOSE_ADDRESS_WII:
        case WM_BOARD_CONTROL_COMPOSE_ADDRESS_OTHERS:
            return "WIPL_SE_DECIDE";
        default:
            if (compose_scroll_arrow(control))
                return "WIPL_SE_LINE_SCROLL";
            if (compose_keyboard_control(control))
                return NULL;
            return "confirm";
    }
}

void wm_app_board_pointer_up(WmAppBoardInput *input, WmBoardScene *board,
                             WmResourceScene *resource_scene, WmPointer *pointer,
                             WmAudio *audio, WmPointerButton button, int x, int y,
                             bool outside_viewport) {
    if (input->held_keyboard) {
        wm_board_scene_release_compose_control(board);
        input->held_keyboard = false;
        input->pressed = (WmBoardHit){WM_BOARD_CONTROL_NONE, 0};
        return;
    }
    if (wm_board_scene_dragging(board)) {
        wm_board_scene_pointer_up(board, x, y);
        if (outside_viewport)
            wm_pointer_hide(pointer);
        input->pressed = (WmBoardHit){WM_BOARD_CONTROL_NONE, 0};
        return;
    }
    WmBoardHit next = wm_board_scene_hit(board, x, y);
    bool confirmed = button == WM_POINTER_LEFT &&
                     input->pressed.control != WM_BOARD_CONTROL_NONE &&
                     next.control == input->pressed.control &&
                     next.memo_index == input->pressed.memo_index;
    if (confirmed && (next.control == WM_BOARD_CONTROL_BACK ||
                      next.control == WM_BOARD_CONTROL_CALENDAR ||
                      next.control == WM_BOARD_CONTROL_CREATE)) {
        wm_resource_scene_dismiss_balloon(resource_scene);
    }
    if (confirmed && wm_board_scene_activate(board, next)) {
        const char *cue = board_click_cue(next.control);
        if (!wm_app_play_board_compose_cues(audio, board) && cue)
            wm_audio_play(audio, cue);
        wm_app_play_board_sound_events(audio, board);
    }
    input->pressed = (WmBoardHit){WM_BOARD_CONTROL_NONE, 0};
}

void wm_app_board_pointer_leave(WmAppBoardInput *input, WmBoardScene *board,
                                bool cancel_capture) {
    wm_board_scene_hover(board, (WmBoardHit){WM_BOARD_CONTROL_NONE, 0});
    wm_board_scene_release_compose_control(board);
    if (cancel_capture)
        wm_board_scene_cancel_pointer(board);
    else
        wm_board_scene_pointer_finish(board);
    input->pressed = (WmBoardHit){WM_BOARD_CONTROL_NONE, 0};
    input->held_keyboard = false;
    input->hovered = (WmBoardHit){WM_BOARD_CONTROL_NONE, 0};
}

bool wm_app_board_compose_key(WmBoardScene *board, WmAudio *audio, WmKey key) {
    bool editor_active = wm_board_scene_compose_editor_active(board);
    if ((key == WM_KEY_LEFT || key == WM_KEY_RIGHT ||
         key == WM_KEY_UP || key == WM_KEY_DOWN) &&
        wm_board_scene_move_memo_caret(board, key)) return true;
    if (key == WM_KEY_BACKSPACE && wm_board_scene_backspace(board)) {
        wm_audio_play(audio, "WIPL_SE_CHAR_DELETE");
        return true;
    }
    if (key == WM_KEY_BACKSPACE && editor_active) {
        wm_audio_play(audio, "WIPL_SE_CHAR_DELETE_ERROR");
        return true;
    }
    if (key == WM_KEY_ENTER && wm_board_scene_address_editor_active(board)) {
        if (wm_board_scene_finish_edit(board))
            wm_app_play_board_compose_cues(audio, board);
        return true;
    }
    if (key == WM_KEY_ENTER && wm_board_scene_insert_text(board, "\n")) {
        wm_app_play_board_compose_cues(audio, board);
        wm_audio_play(audio, "WIPL_SE_CHAR_DECIDE");
        return true;
    }
    if (key == WM_KEY_ENTER && editor_active) {
        wm_audio_play(audio, "WIPL_SE_CHAR_DELETE_ERROR");
        return true;
    }
    if (key >= 32 && key <= 126) {
        char character[2] = {(char)key, '\0'};
        if (wm_board_scene_insert_text(board, character)) {
            bool non_scroll_played;
            wm_app_play_board_compose_cues_with_non_scroll(audio, board,
                                                           &non_scroll_played);
            if (!editor_active || !non_scroll_played)
                wm_audio_play(audio, key == ' ' ? "WIPL_SE_CHAR_DECIDE"
                                                : "WIPL_SE_CHAR_INPUT");
            return true;
        }
        if (editor_active) {
            wm_audio_play(audio, "WIPL_SE_CHAR_DELETE_ERROR");
            return true;
        }
    }
    return false;
}
