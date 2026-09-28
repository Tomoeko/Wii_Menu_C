#ifndef WII_MENU_BOARD_COMPOSE_PRESENTATION_H
#define WII_MENU_BOARD_COMPOSE_PRESENTATION_H

#include "wii_menu/board/board_compose.h"
#include "wii_menu/layout/layout_runtime.h"

/* Hit testing poses these same layouts before reading their source rectangles. */
void board_compose_pose_selector(WmBoardCompose *compose);
void board_compose_pose_body(WmBoardCompose *compose);
void board_compose_pose_footer(WmBoardCompose *compose);
bool board_compose_hit_pane(const WmLayout *layout, const char *pane, int x, int y);
bool board_compose_hit_memo_caret(WmBoardCompose *compose, int x, int y);

#endif
