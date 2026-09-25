#ifndef WII_MENU_BOARD_KEYBOARD_H
#define WII_MENU_BOARD_KEYBOARD_H

#include "wii_menu/font_cache.h"
#include "wii_menu/platform.h"
#include "wii_menu/texture_cache.h"

#include <stdbool.h>
#include <stddef.h>

typedef struct WmBoardKeyboard WmBoardKeyboard;

enum { WM_KEYBOARD_CANDIDATE_UTF8_CAPACITY = 256 };

typedef enum WmBoardKeyboardProfile {
    WM_BOARD_KEYBOARD_MEMO,
    WM_BOARD_KEYBOARD_ADDRESS_WII,
    WM_BOARD_KEYBOARD_ADDRESS_EMAIL,
    WM_BOARD_KEYBOARD_ADDRESS_NICKNAME
} WmBoardKeyboardProfile;

/* The first fifty IDs retain the source keytop order. Empty source keytops
 * are skipped by hit testing and drawing. */
typedef enum WmBoardKeyboardControl {
    WM_KEYBOARD_NONE = 0,
    WM_KEYBOARD_CHARACTER_FIRST = 1,
    WM_KEYBOARD_CHARACTER_LAST = 50,
    WM_KEYBOARD_DELETE = 51,
    WM_KEYBOARD_RETURN,
    WM_KEYBOARD_CAPS,
    WM_KEYBOARD_SHIFT,
    WM_KEYBOARD_SPACE,
    WM_KEYBOARD_BACK,
    WM_KEYBOARD_OK,
    WM_KEYBOARD_MORE,
    WM_KEYBOARD_SYMBOL_FIRST,
    WM_KEYBOARD_SYMBOL_LAST = WM_KEYBOARD_SYMBOL_FIRST + 19,
    WM_KEYBOARD_SYMBOL_CLOSE,
    WM_KEYBOARD_SYMBOL_PREV,
    WM_KEYBOARD_SYMBOL_NEXT,
    WM_KEYBOARD_QWERTY,
    WM_KEYBOARD_PHONE,
    WM_KEYBOARD_PHONE_FIRST,
    WM_KEYBOARD_PHONE_LAST = WM_KEYBOARD_PHONE_FIRST + 11,
    WM_KEYBOARD_PHONE_MODE_FIRST,
    WM_KEYBOARD_PHONE_MODE_LAST = WM_KEYBOARD_PHONE_MODE_FIRST + 3,
    WM_KEYBOARD_LANGUAGE,
    WM_KEYBOARD_PREDICTION,
    WM_KEYBOARD_LANGUAGE_ENGLISH,
    WM_KEYBOARD_LANGUAGE_FRENCH,
    WM_KEYBOARD_LANGUAGE_SPANISH,
    WM_KEYBOARD_CANDIDATE_FIRST,
    WM_KEYBOARD_CANDIDATE_LAST = WM_KEYBOARD_CANDIDATE_FIRST + 19,
    WM_KEYBOARD_CANDIDATE_PREVIOUS,
    WM_KEYBOARD_CANDIDATE_NEXT,
    WM_KEYBOARD_CONTROL_LAST = WM_KEYBOARD_CANDIDATE_NEXT
} WmBoardKeyboardControl;

typedef enum WmBoardKeyboardAction {
    WM_KEYBOARD_ACTION_NONE,
    WM_KEYBOARD_ACTION_HANDLED,
    WM_KEYBOARD_ACTION_INSERT,
    WM_KEYBOARD_ACTION_DELETE,
    WM_KEYBOARD_ACTION_CLOSE_BACK,
    WM_KEYBOARD_ACTION_CLOSE_OK,
    WM_KEYBOARD_ACTION_SYMBOL_OPEN,
    WM_KEYBOARD_ACTION_SYMBOL_CLOSE,
    WM_KEYBOARD_ACTION_SYMBOL_PAGE,
    WM_KEYBOARD_ACTION_REPLACE_LAST,
    WM_KEYBOARD_ACTION_LAYOUT_QWERTY,
    WM_KEYBOARD_ACTION_LAYOUT_PHONE,
    WM_KEYBOARD_ACTION_PHONE_MODE,
    WM_KEYBOARD_ACTION_DICTIONARY_OPEN,
    WM_KEYBOARD_ACTION_DICTIONARY_CLOSE,
    WM_KEYBOARD_ACTION_DICTIONARY_LANGUAGE,
    WM_KEYBOARD_ACTION_PREDICTION_TOGGLE,
    WM_KEYBOARD_ACTION_ACCEPT_CANDIDATE,
    WM_KEYBOARD_ACTION_PREDICT_PHONE,
    WM_KEYBOARD_ACTION_CANDIDATE_PAGE,
    WM_KEYBOARD_ACTION_PHONE_BOUNDARY
} WmBoardKeyboardAction;

typedef struct WmBoardKeyboardComposition {
    size_t prefix_bytes;
    const char *selected_candidate;
    const char *preview_candidate;
    bool preview_hovered;
} WmBoardKeyboardComposition;

WmBoardKeyboard *wm_board_keyboard_create(WmPlatform *platform,
                                          const char *assets_directory,
                                          WmTextureCache *textures,
                                          WmFontCache *fonts);
void wm_board_keyboard_destroy(WmBoardKeyboard *keyboard);
void wm_board_keyboard_reset(WmBoardKeyboard *keyboard);
/* Address profiles reuse the keytop controller while displaying the source
 * background/text box and masking controls disabled by native types 12/7/11. */
void wm_board_keyboard_set_profile(WmBoardKeyboard *keyboard,
                                    WmBoardKeyboardProfile profile);
WmBoardKeyboardProfile wm_board_keyboard_profile(
    const WmBoardKeyboard *keyboard);
/* A held Backspace or QWERTY Space acts on press, then repeats at updates
 * 36, 45, 54, ... while the pointer stays over that key. The returned count
 * is applied by the active Memo or Address editor through its ordinary path. */
unsigned wm_board_keyboard_advance(WmBoardKeyboard *keyboard, float frames);
bool wm_board_keyboard_begin_hold(WmBoardKeyboard *keyboard,
                                  WmBoardKeyboardControl control);
void wm_board_keyboard_release_hold(WmBoardKeyboard *keyboard);
WmBoardKeyboardControl wm_board_keyboard_held_control(
    const WmBoardKeyboard *keyboard);
void wm_board_keyboard_set_text_context(WmBoardKeyboard *keyboard,
                                         const char *utf8);
void wm_board_keyboard_text_changed(WmBoardKeyboard *keyboard,
                                     bool from_phone_key);
/* End the current predictive run at the editor's present text length. The
 * next non-whitespace insertion begins a new run there. Literal phone-key
 * pending state remains intact for the Open Box preview. */
void wm_board_keyboard_finish_composition(WmBoardKeyboard *keyboard);
void wm_board_keyboard_clear_phone_pending(WmBoardKeyboard *keyboard);
bool wm_board_keyboard_phone_space_pending(const WmBoardKeyboard *keyboard);
bool wm_board_keyboard_phone_pending(const WmBoardKeyboard *keyboard);
size_t wm_board_keyboard_phone_pending_bytes(
    const WmBoardKeyboard *keyboard);
/* A pending telephone key commits once when pointer focus leaves it. */
bool wm_board_keyboard_take_phone_commit(WmBoardKeyboard *keyboard);
bool wm_board_keyboard_phone_mode(const WmBoardKeyboard *keyboard);
bool wm_board_keyboard_prediction_enabled(const WmBoardKeyboard *keyboard);
/* For ACCEPT_CANDIDATE and PREDICT_PHONE, replace this many trailing UTF-8
 * bytes with candidate_text. The editor owns bounds and length validation. */
const char *wm_board_keyboard_candidate_text(const WmBoardKeyboard *keyboard);
size_t wm_board_keyboard_candidate_prefix_bytes(
    const WmBoardKeyboard *keyboard);
/* Candidate strings remain keyboard-owned until its next text or dictionary
 * update. The selected candidate persists after pointer departure. */
bool wm_board_keyboard_composition(const WmBoardKeyboard *keyboard,
                                   WmBoardKeyboardComposition *composition);
/* Show the authored key press for a physical character, Return, or Backspace.
 * This affects presentation only; the editor still owns text changes. */
WmBoardKeyboardControl wm_board_keyboard_press_physical(
    WmBoardKeyboard *keyboard, const char *utf8);
void wm_board_keyboard_draw(WmBoardKeyboard *keyboard, float progress,
                            bool entering);
WmBoardKeyboardControl wm_board_keyboard_hit(WmBoardKeyboard *keyboard,
                                             int x, int y);
void wm_board_keyboard_hover(WmBoardKeyboard *keyboard,
                             WmBoardKeyboardControl control);
bool wm_board_keyboard_symbols_visible(const WmBoardKeyboard *keyboard);
bool wm_board_keyboard_back(WmBoardKeyboard *keyboard);
WmBoardKeyboardAction wm_board_keyboard_activate(
    WmBoardKeyboard *keyboard, WmBoardKeyboardControl control,
    bool reverse, char utf8[5]);

#endif
