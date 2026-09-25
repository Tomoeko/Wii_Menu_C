#ifndef WII_MENU_BOARD_READER_SCROLL_H
#define WII_MENU_BOARD_READER_SCROLL_H

#include "wii_menu/layout_runtime.h"
#include "wii_menu/source_hit.h"

#include <stdbool.h>
#include <stddef.h>

typedef enum WmBoardReaderArrow {
    WM_BOARD_READER_ARROW_NONE,
    WM_BOARD_READER_ARROW_UP,
    WM_BOARD_READER_ARROW_DOWN
} WmBoardReaderArrow;

typedef struct WmBoardReaderArrowState {
    bool visible;
    bool appeared;
    float appearance_frame;
    bool focus_active;
    bool focus_entering;
    float focus_frame;
    bool press_active;
    float press_frame;
} WmBoardReaderArrowState;

/* One reader owns this state for its lifetime. Treat the fields as private;
 * use the functions below so appearance, focus, and movement stay in sync. */
typedef struct WmBoardReaderScroll {
    WmBoardReaderArrowState arrows[2];
    WmBoardReaderArrow hovered;
    size_t line_count;
    float line_height;
    float limit;
    float offset;
    float tween_start;
    float tween_delta;
    float tween_frame;
    bool active;
    bool moving;
    bool sound_enabled;
    bool sound_active;
} WmBoardReaderScroll;

/* Pass the wrapped line count measured with the reader's T_Letter font and
 * width, and the N_Body, N_Header, and N_Footer heights from my_Memo_a.
 * The source viewport expression is max(0, 160 + content height - 456).
 * Reconfiguration resets scroll, arrows, focus, and the movement sound. */
void wm_board_reader_scroll_reset(WmBoardReaderScroll *scroll);
bool wm_board_reader_scroll_configure(WmBoardReaderScroll *scroll,
                                       size_t measured_lines,
                                       float body_height,
                                       float header_height,
                                       float footer_height);

/* Appearance can outlive interactive reading during the Back/Trash press.
 * Phase changes do not cancel an already started movement, matching Scroller. */
void wm_board_reader_scroll_set_active(WmBoardReaderScroll *scroll,
                                        bool active);
/* Silence an in-progress scroll when leaving the reader, while the arrows
 * remain posed until the authored close animation starts. */
void wm_board_reader_scroll_set_sound_enabled(WmBoardReaderScroll *scroll,
                                               bool enabled);
bool wm_board_reader_scroll_advance(WmBoardReaderScroll *scroll,
                                     float frames);
bool wm_board_reader_scroll_hover(WmBoardReaderScroll *scroll,
                                   WmBoardReaderArrow arrow);
bool wm_board_reader_scroll_press(WmBoardReaderScroll *scroll,
                                   WmBoardReaderArrow arrow);

/* The display arrows map up to B_ArwR and down to B_ArwL. Supply their
 * current source rectangles after posing my_Memo_a to hit test. */
const char *wm_board_reader_scroll_pane(WmBoardReaderArrow arrow);
bool wm_board_reader_scroll_arrow_visible(const WmBoardReaderScroll *scroll,
                                           WmBoardReaderArrow arrow);
WmBoardReaderArrow wm_board_reader_scroll_hit(
    const WmBoardReaderScroll *scroll, float x, float y,
    const WmSourceRect *up, const WmSourceRect *down);

/* Append these after SelectLetter/ExitLetter and G_ArwRoop. Four clips are
 * always needed; up to two additional press clips may be active. If capacity
 * is too small, returns the required count without writing partial output. */
size_t wm_board_reader_scroll_clips(const WmBoardReaderScroll *scroll,
                                     WmLayoutClip *clips, size_t capacity);

size_t wm_board_reader_scroll_line_count(const WmBoardReaderScroll *scroll);
size_t wm_board_reader_scroll_repeated_rows(const WmBoardReaderScroll *scroll);
float wm_board_reader_scroll_footer_shift(const WmBoardReaderScroll *scroll);
float wm_board_reader_scroll_offset(const WmBoardReaderScroll *scroll);
float wm_board_reader_scroll_limit(const WmBoardReaderScroll *scroll);
bool wm_board_reader_scroll_moving(const WmBoardReaderScroll *scroll);
bool wm_board_reader_scroll_sound_active(const WmBoardReaderScroll *scroll);

#endif
