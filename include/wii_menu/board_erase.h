#ifndef WII_MENU_BOARD_ERASE_H
#define WII_MENU_BOARD_ERASE_H

#include "wii_menu/board_scene.h"

typedef struct WmBoardErase WmBoardErase;

typedef enum WmBoardErasePhase {
    WM_ERASE_CLOSED,
    WM_ERASE_ENTER,
    WM_ERASE_IDLE,
    WM_ERASE_SELECT,
    WM_ERASE_EXIT
} WmBoardErasePhase;

typedef enum WmBoardEraseControl {
    WM_ERASE_CONTROL_NONE,
    WM_ERASE_CONTROL_QUIT,
    WM_ERASE_CONTROL_OK
} WmBoardEraseControl;

typedef enum WmBoardEraseOutcome {
    WM_ERASE_OUTCOME_NONE,
    WM_ERASE_OUTCOME_CANCEL,
    WM_ERASE_OUTCOME_ACCEPT
} WmBoardEraseOutcome;

WmBoardErase *wm_board_erase_create(WmPlatform *platform,
                                     const char *assets_directory,
                                     WmTextureCache *textures,
                                     WmFontCache *fonts);
void wm_board_erase_destroy(WmBoardErase *erase);
void wm_board_erase_reset(WmBoardErase *erase);
bool wm_board_erase_open(WmBoardErase *erase);
WmBoardErasePhase wm_board_erase_phase(const WmBoardErase *erase);
void wm_board_erase_advance(WmBoardErase *erase, float frames);
WmBoardEraseOutcome wm_board_erase_take_outcome(WmBoardErase *erase);
void wm_board_erase_draw(WmBoardErase *erase);
WmBoardEraseControl wm_board_erase_hit(WmBoardErase *erase, int x, int y);
void wm_board_erase_hover(WmBoardErase *erase, WmBoardEraseControl control);
bool wm_board_erase_activate(WmBoardErase *erase,
                              WmBoardEraseControl control);
bool wm_board_erase_back(WmBoardErase *erase);

#endif
