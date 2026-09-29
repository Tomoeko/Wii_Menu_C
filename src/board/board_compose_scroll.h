#ifndef WII_MENU_BOARD_COMPOSE_SCROLL_H
#define WII_MENU_BOARD_COMPOSE_SCROLL_H

#include "wii_menu/board/board_compose.h"

#include <stdbool.h>
#include <stddef.h>

typedef struct ComposeFocus {
    bool active;
    bool entering;
    float frame;
} ComposeFocus;

typedef struct BoardComposeScrollArrow {
    bool visible;
    bool appeared;
    float appearance_frame;
    ComposeFocus focus;
    bool press_active;
    float press_frame;
} BoardComposeScrollArrow;

enum {
    COMPOSE_SCROLL_UP,
    COMPOSE_SCROLL_DOWN,
    COMPOSE_SCROLL_DIRECTIONS
};

typedef enum ComposeScrollMode {
    COMPOSE_SCROLL_DISPLAY,
    COMPOSE_SCROLL_EDITOR
} ComposeScrollMode;

typedef struct BoardComposeScroll {
    BoardComposeScrollArrow arrows[2][COMPOSE_SCROLL_DIRECTIONS];
    float offset;
    float maximum;
    float line_height;
    float start;
    float target;
    float display_return_offset;
    float frame;
    size_t lines;
    bool moving;
    bool follow_caret_pending;
} BoardComposeScroll;

void board_compose_scroll_reset(BoardComposeScroll *scroll);
ComposeScrollMode board_compose_scroll_mode(WmBoardComposePhase phase);
float board_compose_scroll_maximum(const BoardComposeScroll *scroll,
                                   ComposeScrollMode mode);
void board_compose_scroll_refresh(BoardComposeScroll *scroll,
                                  WmBoardComposePhase phase);
bool board_compose_scroll_start(BoardComposeScroll *scroll, float target);
void board_compose_scroll_advance(BoardComposeScroll *scroll, float frames,
                                  WmBoardComposePhase phase);
void board_compose_scroll_begin_enter_edit(BoardComposeScroll *scroll);
void board_compose_scroll_enter_edit_frame(BoardComposeScroll *scroll, float frame);
void board_compose_scroll_leave_edit(BoardComposeScroll *scroll, float frame);
void board_compose_scroll_finish_leave_edit(BoardComposeScroll *scroll);
void board_compose_scroll_enter_edit(BoardComposeScroll *scroll);
bool board_compose_scroll_state(const BoardComposeScroll *scroll,
                                WmBoardComposePhase phase, float phase_frame,
                                WmBoardComposeScrollState *state);

#endif
