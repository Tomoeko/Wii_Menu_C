#include "wii_menu/audio/hover_audio.h"

#include <assert.h>
#include <stdio.h>
#include <string.h>

static void assert_memo_cue(const char *cue) {
    assert(cue);
    assert(strcmp(cue, "WIPL_SE_BOARD_FOCUS") == 0);
}

int main(void) {
    const WmHitType footer_controls[] = {
        WM_HIT_PAGE_PREVIOUS,
        WM_HIT_PAGE_NEXT,
        WM_HIT_PREVIEW_PREVIOUS,
        WM_HIT_PREVIEW_NEXT,
        WM_HIT_SETTINGS,
        WM_HIT_BOARD,
        WM_HIT_SD,
        WM_HIT_BACK
    };
    for (size_t index = 0;
         index < sizeof(footer_controls) / sizeof(footer_controls[0]); index++) {
        assert_memo_cue(wm_hover_audio_menu_cue(footer_controls[index]));
    }
    const WmBoardControl board_controls[] = {
        WM_BOARD_CONTROL_MEMO,
        WM_BOARD_CONTROL_BACK,
        WM_BOARD_CONTROL_CALENDAR,
        WM_BOARD_CONTROL_CREATE,
        WM_BOARD_CONTROL_PREVIOUS,
        WM_BOARD_CONTROL_NEXT,
        WM_BOARD_CONTROL_MEMO_SCROLL_UP,
        WM_BOARD_CONTROL_MEMO_SCROLL_DOWN,
        WM_BOARD_CONTROL_CALENDAR_PREVIOUS,
        WM_BOARD_CONTROL_CALENDAR_NEXT,
        WM_BOARD_CONTROL_COMPOSE_SCROLL_UP,
        WM_BOARD_CONTROL_COMPOSE_SCROLL_DOWN,
        WM_BOARD_CONTROL_COMPOSE_ADDRESS_PREVIOUS,
        WM_BOARD_CONTROL_COMPOSE_ADDRESS_NEXT,
        WM_BOARD_CONTROL_COMPOSE_ADDRESS_MII,
        WM_BOARD_CONTROL_COMPOSE_KEY_FIRST + WM_KEYBOARD_SYMBOL_PREV - 1,
        WM_BOARD_CONTROL_COMPOSE_KEY_FIRST + WM_KEYBOARD_SYMBOL_NEXT - 1,
        WM_BOARD_CONTROL_COMPOSE_KEY_FIRST + WM_KEYBOARD_CANDIDATE_PREVIOUS - 1,
        WM_BOARD_CONTROL_COMPOSE_KEY_FIRST + WM_KEYBOARD_CANDIDATE_NEXT - 1
    };
    for (size_t index = 0;
         index < sizeof(board_controls) / sizeof(board_controls[0]); index++) {
        assert_memo_cue(wm_hover_audio_board_cue(board_controls[index]));
    }
    for (int control = WM_BOARD_CONTROL_COMPOSE_ADDRESS_ENTRY_FIRST;
         control <= WM_BOARD_CONTROL_COMPOSE_ADDRESS_ENTRY_LAST; control++) {
        assert_memo_cue(wm_hover_audio_board_cue((WmBoardControl)control));
    }
    assert_memo_cue(wm_hover_audio_sd_cue(WM_SD_CONTROL_PREVIOUS));
    assert_memo_cue(wm_hover_audio_sd_cue(WM_SD_CONTROL_NEXT));
    assert_memo_cue(wm_hover_audio_sd_cue(WM_SD_CONTROL_BACK));
    assert_memo_cue(wm_hover_audio_storage_cue(WM_STORAGE_CONTROL_PREVIOUS));
    assert_memo_cue(wm_hover_audio_storage_cue(WM_STORAGE_CONTROL_NEXT));

    /* Unrelated input retains its existing sound ownership. Empty areas and
     * text/info-only panes never synthesize a hover request. */
    assert(strcmp(wm_hover_audio_menu_cue(WM_HIT_CHANNEL), "hover") == 0);
    assert(strcmp(wm_hover_audio_board_cue(WM_BOARD_CONTROL_COMPOSE_KEY_FIRST),
                  "WIPL_SE_CHAR_FOCUS") == 0);
    assert(strcmp(wm_hover_audio_sd_cue(WM_SD_CONTROL_CHANNEL),
                  "buttonHover") == 0);
    assert(strcmp(wm_hover_audio_sd_cue(WM_SD_CONTROL_HELP),
                  "buttonHover") == 0);
    assert(strcmp(wm_hover_audio_storage_cue(WM_STORAGE_CONTROL_SLOT),
                  "WIPL_SE_BT_TARGETTING") == 0);
    assert(strcmp(wm_hover_audio_board_cue(WM_BOARD_CONTROL_COMPOSE_POST),
                  "WIPL_SE_BT_TARGETTING") == 0);
    assert(!wm_hover_audio_menu_cue(WM_HIT_NONE));
    assert(!wm_hover_audio_menu_cue(WM_HIT_NOTICE_DISMISS));
    assert(!wm_hover_audio_board_cue(WM_BOARD_CONTROL_NONE));
    assert(!wm_hover_audio_board_cue(WM_BOARD_CONTROL_COMPOSE_ADDRESS_INFO));
    assert(!wm_hover_audio_sd_cue(WM_SD_CONTROL_NONE));
    assert(!wm_hover_audio_storage_cue(WM_STORAGE_CONTROL_NONE));
    puts("Memo hover cue is shared by navigation arrows and footer icons.");
    return 0;
}
