#include "board_compose_scroll.h"

#include <math.h>
#include <string.h>

void board_compose_scroll_reset(BoardComposeScroll *scroll) {
    /* line_height is measured from the loaded layout and survives resets. */
    memset(scroll->arrows, 0, sizeof(scroll->arrows));
    for (size_t mode = 0; mode < 2; mode++) {
        for (size_t direction = 0; direction < COMPOSE_SCROLL_DIRECTIONS;
             direction++) {
            scroll->arrows[mode][direction].appearance_frame = 11.0f;
            scroll->arrows[mode][direction].focus.entering = true;
        }
    }
    scroll->offset = 0.0f;
    scroll->maximum = 0.0f;
    scroll->start = 0.0f;
    scroll->target = 0.0f;
    scroll->display_return_offset = 0.0f;
    scroll->frame = 0.0f;
    scroll->lines = 1;
    scroll->moving = false;
    scroll->follow_caret_pending = false;
}

ComposeScrollMode board_compose_scroll_mode(WmBoardComposePhase phase) {
    if (phase == WM_COMPOSE_ENTER_EDIT || phase == WM_COMPOSE_EDIT ||
        phase == WM_COMPOSE_LEAVE_EDIT) {
        return COMPOSE_SCROLL_EDITOR;
    }
    return COMPOSE_SCROLL_DISPLAY;
}

static void set_arrow_visible(BoardComposeScrollArrow *arrow, bool visible) {
    if (arrow->visible == visible) {
        return;
    }
    arrow->visible = visible;
    arrow->appeared = arrow->appeared || visible;
    arrow->appearance_frame = 0.0f;
    if (!visible && arrow->focus.active && arrow->focus.entering) {
        arrow->focus = (ComposeFocus){true, false, 0.0f};
    }
}

float board_compose_scroll_maximum(const BoardComposeScroll *scroll,
                                   ComposeScrollMode mode) {
    size_t lines = scroll->lines;
    float height = scroll->line_height;
    if (mode == COMPOSE_SCROLL_EDITOR) {
        return lines > 2 ? (float)(lines - 2) * height : 0.0f;
    }
    return fmaxf(0.0f, (float)(lines > 4 ? lines : 4) * height - 100.0f);
}

void board_compose_scroll_refresh(BoardComposeScroll *scroll,
                                  WmBoardComposePhase phase) {
    ComposeScrollMode mode = board_compose_scroll_mode(phase);
    scroll->maximum = board_compose_scroll_maximum(scroll, mode);
    if (phase == WM_COMPOSE_EDIT && scroll->offset > scroll->maximum &&
        !scroll->moving) {
        scroll->start = scroll->offset;
        scroll->target = scroll->maximum;
        scroll->frame = 0.0f;
        scroll->moving = true;
    } else if (phase != WM_COMPOSE_EDIT && phase != WM_COMPOSE_ENTER_EDIT &&
               phase != WM_COMPOSE_LEAVE_EDIT) {
        scroll->offset = fminf(scroll->offset, scroll->maximum);
    }
    if (scroll->moving) {
        scroll->target = fminf(scroll->target, scroll->maximum);
    }
    bool display = phase == WM_COMPOSE_ENTER_MEMO || phase == WM_COMPOSE_MEMO;
    bool editor = phase == WM_COMPOSE_EDIT;
    for (size_t direction = 0; direction < COMPOSE_SCROLL_DIRECTIONS;
         direction++) {
        bool within_range = direction == COMPOSE_SCROLL_UP
                                ? scroll->offset > 0.001f
                                : scroll->offset + 0.001f < scroll->maximum;
        set_arrow_visible(&scroll->arrows[COMPOSE_SCROLL_DISPLAY][direction],
                          display && within_range);
        if (phase != WM_COMPOSE_LEAVE_EDIT) {
            set_arrow_visible(&scroll->arrows[COMPOSE_SCROLL_EDITOR][direction],
                              editor && within_range);
        }
    }
}

bool board_compose_scroll_start(BoardComposeScroll *scroll, float target) {
    if (scroll->moving) {
        return false;
    }
    target = fminf(fmaxf(target, 0.0f), scroll->maximum);
    if (fabsf(target - scroll->offset) < 0.001f) {
        return false;
    }
    scroll->start = scroll->offset;
    scroll->target = target;
    scroll->frame = 0.0f;
    scroll->moving = true;
    return true;
}

void board_compose_scroll_advance(BoardComposeScroll *scroll, float frames,
                                  WmBoardComposePhase phase) {
    for (size_t mode = 0; mode < 2; mode++) {
        for (size_t direction = 0; direction < COMPOSE_SCROLL_DIRECTIONS;
             direction++) {
            BoardComposeScrollArrow *arrow = &scroll->arrows[mode][direction];
            arrow->appearance_frame += frames;
            if (arrow->focus.active) {
                arrow->focus.frame += frames;
            }
            if (arrow->press_active) {
                arrow->press_frame += frames;
                if (arrow->press_frame >= 7.0f) {
                    arrow->press_active = false;
                }
            }
        }
    }
    if (scroll->moving) {
        scroll->frame = fminf(15.0f, scroll->frame + frames);
        float progress = scroll->frame / 15.0f;
        float eased = progress * progress * (3.0f - 2.0f * progress);
        scroll->offset = scroll->start +
                         (scroll->target - scroll->start) * eased;
        if (scroll->frame >= 15.0f) {
            scroll->moving = false;
        }
        board_compose_scroll_refresh(scroll, phase);
    }
}

void board_compose_scroll_begin_enter_edit(BoardComposeScroll *scroll) {
    /* A display page can scroll farther than a two-line editor. Keep the
     * visible pose at the click and settle into the editor's valid range as
     * the keyboard rises, instead of clamping the memo on the first frame. */
    scroll->display_return_offset = scroll->offset;
    scroll->start = scroll->offset;
    scroll->target = fminf(scroll->maximum,
        roundf(scroll->offset / scroll->line_height) * scroll->line_height);
    scroll->moving = false;
}

void board_compose_scroll_enter_edit_frame(BoardComposeScroll *scroll,
                                            float frame) {
    float progress = fminf(fmaxf(frame, 0.0f), 30.0f) / 30.0f;
    float eased = progress * progress * (3.0f - 2.0f * progress);
    scroll->offset = scroll->start +
                     (scroll->target - scroll->start) * eased;
}

void board_compose_scroll_leave_edit(BoardComposeScroll *scroll, float frame) {
    float progress = fminf(fmaxf(frame, 0.0f), 30.0f) / 30.0f;
    float eased = progress * progress * (3.0f - 2.0f * progress);
    scroll->offset = scroll->start +
                     (scroll->target - scroll->start) * eased;
}

void board_compose_scroll_finish_leave_edit(BoardComposeScroll *scroll) {
    /* The editor arrows have already faded; restarting Fade_OUT flashes them. */
    for (size_t direction = 0; direction < COMPOSE_SCROLL_DIRECTIONS;
         direction++) {
        BoardComposeScrollArrow *editor =
            &scroll->arrows[COMPOSE_SCROLL_EDITOR][direction];
        editor->visible = false;
        editor->appearance_frame = 10.0f;
        editor->focus.active = false;
        editor->press_active = false;
    }
}

void board_compose_scroll_enter_edit(BoardComposeScroll *scroll) {
    scroll->offset = scroll->target;
    scroll->moving = false;
}

bool board_compose_scroll_state(const BoardComposeScroll *scroll,
                                WmBoardComposePhase phase, float phase_frame,
                                WmBoardComposeScrollState *state) {
    if (!scroll || !state) {
        return false;
    }
    ComposeScrollMode mode = board_compose_scroll_mode(phase);
    float editor_opacity = 0.0f;
    if (phase == WM_COMPOSE_EDIT) {
        editor_opacity = 1.0f;
    } else if (phase == WM_COMPOSE_LEAVE_EDIT) {
        editor_opacity = 1.0f -
            fminf(fmaxf(phase_frame, 0.0f), 30.0f) / 30.0f;
    }
    *state = (WmBoardComposeScrollState){
        .offset = scroll->offset,
        .maximum = scroll->maximum,
        .editor_opacity = editor_opacity,
        .editing = mode == COMPOSE_SCROLL_EDITOR,
        .up_target_visible = scroll->arrows[mode][COMPOSE_SCROLL_UP].visible,
        .down_target_visible =
            scroll->arrows[mode][COMPOSE_SCROLL_DOWN].visible
    };
    return true;
}
