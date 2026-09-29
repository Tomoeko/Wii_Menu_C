#include "wii_menu/audio/hover_audio.h"

const char *wm_hover_audio_menu_cue(WmHitType hit) {
    switch (hit) {
        case WM_HIT_NONE:
        case WM_HIT_NOTICE_DISMISS:
            return NULL;
        case WM_HIT_CHANNEL:
            return "hover";
        case WM_HIT_PAGE_PREVIOUS:
        case WM_HIT_PAGE_NEXT:
        case WM_HIT_PREVIEW_PREVIOUS:
        case WM_HIT_PREVIEW_NEXT:
        case WM_HIT_SETTINGS:
        case WM_HIT_BOARD:
        case WM_HIT_SD:
        case WM_HIT_BACK:
            return "WIPL_SE_BOARD_FOCUS";
        default:
            return "buttonHover";
    }
}

static bool keyboard_arrow(WmBoardControl control) {
    int key = (int)control - (int)WM_BOARD_CONTROL_COMPOSE_KEY_FIRST + 1;
    return key == WM_KEYBOARD_SYMBOL_PREV || key == WM_KEYBOARD_SYMBOL_NEXT ||
           key == WM_KEYBOARD_CANDIDATE_PREVIOUS || key == WM_KEYBOARD_CANDIDATE_NEXT;
}

const char *wm_hover_audio_board_cue(WmBoardControl control) {
    if (control >= WM_BOARD_CONTROL_COMPOSE_ADDRESS_ENTRY_FIRST &&
        control <= WM_BOARD_CONTROL_COMPOSE_ADDRESS_ENTRY_LAST) {
        return "WIPL_SE_BOARD_FOCUS";
    }
    if (control >= WM_BOARD_CONTROL_COMPOSE_KEY_FIRST &&
        control <= WM_BOARD_CONTROL_COMPOSE_KEY_LAST) {
        return keyboard_arrow(control) ? "WIPL_SE_BOARD_FOCUS" : "WIPL_SE_CHAR_FOCUS";
    }
    switch (control) {
        case WM_BOARD_CONTROL_NONE:
        case WM_BOARD_CONTROL_COMPOSE_ADDRESS_INFO:
            return NULL;
        case WM_BOARD_CONTROL_MEMO:
        case WM_BOARD_CONTROL_BACK:
        case WM_BOARD_CONTROL_CALENDAR:
        case WM_BOARD_CONTROL_CREATE:
        case WM_BOARD_CONTROL_PREVIOUS:
        case WM_BOARD_CONTROL_NEXT:
        case WM_BOARD_CONTROL_MEMO_SCROLL_UP:
        case WM_BOARD_CONTROL_MEMO_SCROLL_DOWN:
        case WM_BOARD_CONTROL_CALENDAR_PREVIOUS:
        case WM_BOARD_CONTROL_CALENDAR_NEXT:
        case WM_BOARD_CONTROL_COMPOSE_SCROLL_UP:
        case WM_BOARD_CONTROL_COMPOSE_SCROLL_DOWN:
        case WM_BOARD_CONTROL_COMPOSE_ADDRESS_PREVIOUS:
        case WM_BOARD_CONTROL_COMPOSE_ADDRESS_NEXT:
        case WM_BOARD_CONTROL_COMPOSE_ADDRESS_MII:
            return "WIPL_SE_BOARD_FOCUS";
        case WM_BOARD_CONTROL_COMPOSE_ADDRESS:
        case WM_BOARD_CONTROL_COMPOSE_ADDRESS_WII:
        case WM_BOARD_CONTROL_COMPOSE_ADDRESS_OTHERS:
        case WM_BOARD_CONTROL_COMPOSE_ADDRESS_EDIT:
        case WM_BOARD_CONTROL_COMPOSE_ADDRESS_DIALOG_OK:
        case WM_BOARD_CONTROL_COMPOSE_ADDRESS_CHANGE_NICKNAME:
        case WM_BOARD_CONTROL_COMPOSE_ADDRESS_ERASE:
        case WM_BOARD_CONTROL_COMPOSE_ADDRESS_DIALOG_YES:
        case WM_BOARD_CONTROL_COMPOSE_ADDRESS_DIALOG_NO:
        case WM_BOARD_CONTROL_COMPOSE_MII:
        case WM_BOARD_CONTROL_COMPOSE_POST:
        case WM_BOARD_CONTROL_COMPOSE_BACK:
            return "WIPL_SE_BT_TARGETTING";
        default:
            return "buttonHover";
    }
}

const char *wm_hover_audio_sd_cue(WmSdControl control) {
    switch (control) {
        case WM_SD_CONTROL_NONE:
            return NULL;
        case WM_SD_CONTROL_BACK:
        case WM_SD_CONTROL_PREVIOUS:
        case WM_SD_CONTROL_NEXT:
            return "WIPL_SE_BOARD_FOCUS";
        default:
            return "buttonHover";
    }
}

const char *wm_hover_audio_storage_cue(WmStorageControl control) {
    switch (control) {
        case WM_STORAGE_CONTROL_NONE:
            return NULL;
        case WM_STORAGE_CONTROL_PREVIOUS:
        case WM_STORAGE_CONTROL_NEXT:
            return "WIPL_SE_BOARD_FOCUS";
        default:
            return "WIPL_SE_BT_TARGETTING";
    }
}
