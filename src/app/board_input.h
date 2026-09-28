#ifndef WM_APP_BOARD_INPUT_H
#define WM_APP_BOARD_INPUT_H

#include "wii_menu/audio/audio.h"
#include "wii_menu/board/board_scene.h"
#include "wii_menu/input/pointer.h"
#include "wii_menu/platform/platform.h"
#include "wii_menu/scenes/resource_scene.h"

#include <stdbool.h>

/* Press ownership stays with the Board even while its page-turn lock is
 * active. An inactive sentinel prevents a held press from activating later. */
typedef struct WmAppBoardInput {
    WmBoardHit hovered;
    WmBoardHit pressed;
    bool held_keyboard;
    int press_x;
    int press_y;
} WmAppBoardInput;

void wm_app_board_pointer_move(WmAppBoardInput *input, WmBoardScene *board,
                               WmAudio *audio, int x, int y);
void wm_app_board_pointer_down(WmAppBoardInput *input, WmBoardScene *board,
                               WmAudio *audio, WmPointerButton button, int x, int y);
void wm_app_board_pointer_up(WmAppBoardInput *input, WmBoardScene *board,
                             WmResourceScene *resource_scene, WmPointer *pointer,
                             WmAudio *audio, WmPointerButton button, int x, int y,
                             bool outside_viewport);
void wm_app_board_pointer_leave(WmAppBoardInput *input, WmBoardScene *board,
                                bool cancel_capture);
bool wm_app_board_compose_key(WmBoardScene *board, WmAudio *audio, WmKey key);
void wm_app_board_play_hover(WmAudio *audio, WmBoardControl control);

#endif
