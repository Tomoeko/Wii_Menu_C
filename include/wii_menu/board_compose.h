#ifndef WII_MENU_BOARD_COMPOSE_H
#define WII_MENU_BOARD_COMPOSE_H

#include "wii_menu/board_keyboard.h"
#include "wii_menu/board_scene.h"

typedef struct WmBoardCompose WmBoardCompose;

typedef enum WmBoardComposePhase {
    WM_COMPOSE_CLOSED,
    WM_COMPOSE_ENTER_SELECTOR,
    WM_COMPOSE_SELECTOR,
    WM_COMPOSE_ENTER_MEMO,
    WM_COMPOSE_MEMO,
    WM_COMPOSE_ENTER_EDIT,
    WM_COMPOSE_EDIT,
    WM_COMPOSE_LEAVE_EDIT,
    WM_COMPOSE_POST_PRESS,
    WM_COMPOSE_SEND,
    WM_COMPOSE_EXIT_AFTER_POST,
    WM_COMPOSE_BACK_MEMO,
    WM_COMPOSE_BACK_SELECTOR,
    WM_COMPOSE_ADDRESS
} WmBoardComposePhase;

typedef enum WmBoardComposeControl {
    WM_COMPOSE_CONTROL_NONE,
    WM_COMPOSE_CONTROL_BACK,
    WM_COMPOSE_CONTROL_MEMO,
    WM_COMPOSE_CONTROL_LETTER,
    WM_COMPOSE_CONTROL_ADDRESS,
    WM_COMPOSE_CONTROL_EDIT,
    WM_COMPOSE_CONTROL_POST,
    WM_COMPOSE_CONTROL_MII,
    WM_COMPOSE_CONTROL_SCROLL_UP,
    WM_COMPOSE_CONTROL_SCROLL_DOWN,
    WM_COMPOSE_CONTROL_ADDRESS_PREVIOUS,
    WM_COMPOSE_CONTROL_ADDRESS_NEXT,
    WM_COMPOSE_CONTROL_ADDRESS_WII,
    WM_COMPOSE_CONTROL_ADDRESS_OTHERS,
    WM_COMPOSE_CONTROL_ADDRESS_EDIT,
    WM_COMPOSE_CONTROL_ADDRESS_DIALOG_OK,
    WM_COMPOSE_CONTROL_ADDRESS_MII,
    WM_COMPOSE_CONTROL_ADDRESS_INFO,
    WM_COMPOSE_CONTROL_ADDRESS_CHANGE_NICKNAME,
    WM_COMPOSE_CONTROL_ADDRESS_ERASE,
    WM_COMPOSE_CONTROL_ADDRESS_DIALOG_YES,
    WM_COMPOSE_CONTROL_ADDRESS_DIALOG_NO,
    WM_COMPOSE_CONTROL_NETWORK_QUIT,
    WM_COMPOSE_CONTROL_NETWORK_SETTINGS,
    WM_COMPOSE_CONTROL_ADDRESS_ENTRY_FIRST,
    WM_COMPOSE_CONTROL_ADDRESS_ENTRY_LAST =
        WM_COMPOSE_CONTROL_ADDRESS_ENTRY_FIRST + 4,
    WM_COMPOSE_CONTROL_KEY_FIRST = 100,
    WM_COMPOSE_CONTROL_KEY_LAST = WM_COMPOSE_CONTROL_KEY_FIRST +
                                  WM_KEYBOARD_CONTROL_LAST - 1
} WmBoardComposeControl;

typedef struct WmBoardComposeScrollState {
    float offset;
    float maximum;
    float editor_opacity;
    bool editing;
    bool up_target_visible;
    bool down_target_visible;
} WmBoardComposeScrollState;

typedef enum WmBoardComposeOutcome {
    WM_COMPOSE_OUTCOME_NONE,
    WM_COMPOSE_OUTCOME_CLOSED,
    WM_COMPOSE_OUTCOME_POSTED,
    WM_COMPOSE_OUTCOME_OPEN_SETTINGS
} WmBoardComposeOutcome;

WmBoardCompose *wm_board_compose_create(WmPlatform *platform,
                                         const char *assets_directory,
                                         WmTextureCache *textures,
                                         WmFontCache *fonts);
void wm_board_compose_destroy(WmBoardCompose *compose);
WmBoardContactStoreStatus wm_board_compose_load_contacts(
    WmBoardCompose *compose, const char *path,
    char *error, size_t error_capacity);
void wm_board_compose_reset(WmBoardCompose *compose);
bool wm_board_compose_open(WmBoardCompose *compose);
void wm_board_compose_advance(WmBoardCompose *compose, float frames);
WmBoardComposePhase wm_board_compose_phase(const WmBoardCompose *compose);
WmBoardComposeOutcome wm_board_compose_take_outcome(WmBoardCompose *compose);
const char *wm_board_compose_take_key_cue(WmBoardCompose *compose);
bool wm_board_compose_keyboard_overlay_visible(
    const WmBoardCompose *compose);
const char *wm_board_compose_text(const WmBoardCompose *compose);
/* Phone Space remains a literal space in the draft and is shown as U+2423
 * only until pointer departure commits that key's cycle. */
const char *wm_board_compose_display_text(WmBoardCompose *compose);
void wm_board_compose_draw(WmBoardCompose *compose);
WmBoardComposeControl wm_board_compose_hit(WmBoardCompose *compose,
                                           int x, int y);
void wm_board_compose_hover(WmBoardCompose *compose,
                            WmBoardComposeControl control);
bool wm_board_compose_activate(WmBoardCompose *compose,
                                WmBoardComposeControl control);
/* Wii Remote B reverses only a phone key's multitap cycle. */
bool wm_board_compose_activate_secondary(WmBoardCompose *compose,
                                          WmBoardComposeControl control);
/* Pointer-down ownership for the two source keytops that repeat while held.
 * Successful hold performs the first edit immediately. */
bool wm_board_compose_hold_control(WmBoardCompose *compose,
                                    WmBoardComposeControl control);
void wm_board_compose_release_control(WmBoardCompose *compose);
bool wm_board_compose_back(WmBoardCompose *compose);
/* The source book cover is page zero; numbered pages are one through twenty. */
unsigned wm_board_compose_address_page(const WmBoardCompose *compose);
bool wm_board_compose_address_editor_active(const WmBoardCompose *compose);
const char *wm_board_compose_address_text(const WmBoardCompose *compose);

/* First-party physical-key text path. The caller sends UTF-8 text produced by
 * its platform adapter; Backspace and Return are separate commands. */
bool wm_board_compose_insert_text(WmBoardCompose *compose,
                                   const char *utf8);
void wm_board_compose_press_physical(WmBoardCompose *compose,
                                      const char *utf8);
bool wm_board_compose_backspace(WmBoardCompose *compose);
bool wm_board_compose_finish_edit(WmBoardCompose *compose);
/* The Memo's display and two-line editor share a scroll position. The editor
 * arrows enter after the 30-frame edit transition and leave with its exit. */
bool wm_board_compose_scroll_state(const WmBoardCompose *compose,
                                    WmBoardComposeScrollState *state);

#endif
