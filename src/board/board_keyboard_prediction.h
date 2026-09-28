#ifndef WII_MENU_BOARD_KEYBOARD_PREDICTION_H
#define WII_MENU_BOARD_KEYBOARD_PREDICTION_H

#include "board_keyboard_internal.h"

/* OEM lists live for the keyboard lifetime. Learned words are owned by the
 * current text context and are released on reset or context replacement. */
void wm_board_keyboard_prediction_load_oem(WmBoardKeyboard *keyboard,
                                           const char *assets_directory);
void wm_board_keyboard_prediction_release(WmBoardKeyboard *keyboard);
void wm_board_keyboard_prediction_clear_learned(WmBoardKeyboard *keyboard);
void wm_board_keyboard_prediction_learn_words(WmBoardKeyboard *keyboard,
                                              const char *text, bool include_trailing);
void wm_board_keyboard_prediction_refresh(WmBoardKeyboard *keyboard);
void wm_board_keyboard_prediction_rollback_phone(WmBoardKeyboard *keyboard);
void wm_board_keyboard_prediction_phone_value(WmBoardKeyboard *keyboard,
                                              char output[256]);
bool wm_board_keyboard_prediction_phone_uppercase(const WmBoardKeyboard *keyboard);
const char *wm_board_keyboard_prediction_phone_cycle(unsigned index);
unsigned wm_board_keyboard_candidate_previous_index(const WmBoardKeyboard *keyboard);

#endif
