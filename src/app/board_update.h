#ifndef WM_APP_BOARD_UPDATE_H
#define WM_APP_BOARD_UPDATE_H

#include "board_input.h"
#include "input_routing.h"

#include "wii_menu/menu/menu.h"

typedef struct WmAppBoardUpdate {
    WmMenu *menu;
    WmBoardScene *board;
    WmResourceScene *resource_scene;
    WmAudio *audio;
    WmAppSceneFade *fade;
    WmAppBoardInput *input;
    WmHit *menu_hover;
    WmBoardAction *settings_request;
    const char *state_path;
    bool *entry_hover_pending;
    bool *visited;
    bool pointer_inside;
    int pointer_x;
    int pointer_y;
} WmAppBoardUpdate;

/* Called once per frame before Settings and SD updates, including while the
 * Board is parked behind the grid. active excludes modal/fade pauses. */
void wm_app_board_advance(WmAppBoardUpdate *update, float frames, bool active,
                          bool resource_mode, bool home_active);

#endif
