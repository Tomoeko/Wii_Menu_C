#include "wii_menu/board_reader_scroll.h"

#include <math.h>
#include <string.h>

enum {
    READER_MAX_LINES = 16 * 1024 * 1024
};

static int arrow_index(WmBoardReaderArrow arrow) {
    if (arrow == WM_BOARD_READER_ARROW_UP) return 0;
    if (arrow == WM_BOARD_READER_ARROW_DOWN) return 1;
    return -1;
}

static float clamp(float value, float maximum) {
    return fminf(fmaxf(value, 0.0f), maximum);
}

static float position(const WmBoardReaderScroll *scroll, float frame) {
    float progress = clamp(frame, 20.0f) / 20.0f;
    float eased = progress * progress * (3.0f - 2.0f * progress);
    return clamp(scroll->tween_start + scroll->tween_delta * eased,
                 scroll->limit);
}

void wm_board_reader_scroll_reset(WmBoardReaderScroll *scroll) {
    if (!scroll) return;
    memset(scroll, 0, sizeof(*scroll));
    scroll->line_count = 1;
    scroll->hovered = WM_BOARD_READER_ARROW_NONE;
    for (size_t index = 0; index < 2; index++) {
        scroll->arrows[index].appearance_frame = 11.0f;
        scroll->arrows[index].focus_entering = true;
    }
}

bool wm_board_reader_scroll_configure(WmBoardReaderScroll *scroll,
                                       size_t measured_lines,
                                       float body_height,
                                       float header_height,
                                       float footer_height) {
    if (!scroll || measured_lines > READER_MAX_LINES ||
        !isfinite(body_height) || !isfinite(header_height) ||
        !isfinite(footer_height) || body_height <= 0.0f ||
        header_height < 0.0f || footer_height < 0.0f) return false;
    size_t lines = measured_lines ? measured_lines : 1;
    float total = 160.0f + (float)lines * body_height +
                  header_height + footer_height - 456.0f;
    if (!isfinite(total)) return false;
    wm_board_reader_scroll_reset(scroll);
    scroll->line_count = lines;
    scroll->line_height = body_height;
    scroll->limit = fmaxf(0.0f, total);
    return true;
}

static void update_visibility(WmBoardReaderScroll *scroll) {
    bool wanted[2] = {
        scroll->active && scroll->offset > 0.0f,
        scroll->active && scroll->offset < scroll->limit
    };
    for (int index = 0; index < 2; index++) {
        WmBoardReaderArrowState *state = &scroll->arrows[index];
        if (state->visible == wanted[index]) continue;
        state->visible = wanted[index];
        state->appearance_frame = 0.0f;
        state->appeared = state->appeared || wanted[index];
        if (!wanted[index] && arrow_index(scroll->hovered) == index) {
            wm_board_reader_scroll_hover(scroll, WM_BOARD_READER_ARROW_NONE);
        }
    }
}

void wm_board_reader_scroll_set_active(WmBoardReaderScroll *scroll,
                                        bool active) {
    if (!scroll) return;
    scroll->active = active;
    scroll->sound_enabled = active;
    if (!active) scroll->sound_active = false;
    update_visibility(scroll);
}

void wm_board_reader_scroll_set_sound_enabled(WmBoardReaderScroll *scroll,
                                               bool enabled) {
    if (!scroll) return;
    scroll->sound_enabled = enabled;
    if (!enabled) scroll->sound_active = false;
}

bool wm_board_reader_scroll_advance(WmBoardReaderScroll *scroll,
                                     float frames) {
    if (!scroll || !isfinite(frames) || frames < 0.0f) return false;
    for (size_t index = 0; index < 2; index++) {
        WmBoardReaderArrowState *state = &scroll->arrows[index];
        state->appearance_frame += frames;
        if (state->focus_active) state->focus_frame += frames;
        if (state->press_active) {
            if (state->press_frame + frames > 30.0f) {
                state->press_active = false;
            } else {
                state->press_frame += frames;
            }
        }
    }
    if (scroll->moving) {
        scroll->tween_frame += frames;
        scroll->offset = position(scroll, scroll->tween_frame);
        float previous = position(scroll, scroll->tween_frame - 1.0f);
        scroll->sound_active = frames > 0.0f && scroll->active &&
                               scroll->sound_enabled &&
                               fabsf(scroll->offset - previous) > 1.0f;
        if (scroll->tween_frame >= 20.0f) scroll->moving = false;
    } else if (frames > 0.0f) {
        scroll->sound_active = false;
    }
    update_visibility(scroll);
    return true;
}

bool wm_board_reader_scroll_arrow_visible(const WmBoardReaderScroll *scroll,
                                           WmBoardReaderArrow arrow) {
    int index = arrow_index(arrow);
    return scroll && index >= 0 && scroll->arrows[index].visible;
}

bool wm_board_reader_scroll_hover(WmBoardReaderScroll *scroll,
                                   WmBoardReaderArrow arrow) {
    if (!scroll) return false;
    if (!wm_board_reader_scroll_arrow_visible(scroll, arrow)) {
        arrow = WM_BOARD_READER_ARROW_NONE;
    }
    if (scroll->hovered == arrow) return false;
    int previous = arrow_index(scroll->hovered);
    if (previous >= 0) {
        scroll->arrows[previous].focus_active = true;
        scroll->arrows[previous].focus_entering = false;
        scroll->arrows[previous].focus_frame = 0.0f;
    }
    scroll->hovered = arrow;
    int next = arrow_index(arrow);
    if (next >= 0) {
        scroll->arrows[next].focus_active = true;
        scroll->arrows[next].focus_entering = true;
        scroll->arrows[next].focus_frame = 0.0f;
    }
    return true;
}

bool wm_board_reader_scroll_press(WmBoardReaderScroll *scroll,
                                   WmBoardReaderArrow arrow) {
    if (!scroll || scroll->moving ||
        !wm_board_reader_scroll_arrow_visible(scroll, arrow)) return false;
    int index = arrow_index(arrow);
    scroll->tween_start = scroll->offset;
    scroll->tween_delta = index == 0 ? -300.0f : 300.0f;
    scroll->tween_frame = -1.0f;
    scroll->moving = true;
    scroll->arrows[index].press_active = true;
    scroll->arrows[index].press_frame = 0.0f;
    return true;
}

const char *wm_board_reader_scroll_pane(WmBoardReaderArrow arrow) {
    if (arrow == WM_BOARD_READER_ARROW_UP) return "B_ArwR";
    if (arrow == WM_BOARD_READER_ARROW_DOWN) return "B_ArwL";
    return NULL;
}

static bool point_in_rect(float x, float y, const WmSourceRect *rect,
                          float margin) {
    return rect && isfinite(rect->x) && isfinite(rect->y) &&
           isfinite(rect->width) && isfinite(rect->height) &&
           rect->width > 0.0f && rect->height > 0.0f &&
           x >= rect->x - margin && x <= rect->x + rect->width + margin &&
           y >= rect->y - margin && y <= rect->y + rect->height + margin;
}

WmBoardReaderArrow wm_board_reader_scroll_hit(
    const WmBoardReaderScroll *scroll, float x, float y,
    const WmSourceRect *up, const WmSourceRect *down) {
    if (!scroll || !isfinite(x) || !isfinite(y)) {
        return WM_BOARD_READER_ARROW_NONE;
    }
    /* Focus clips move the hit pane under a stationary pointer. Retain its
     * target through that small motion so hover audio fires only on entry. */
    if (scroll->hovered == WM_BOARD_READER_ARROW_UP &&
        wm_board_reader_scroll_arrow_visible(scroll,
                                              WM_BOARD_READER_ARROW_UP) &&
        point_in_rect(x, y, up, 8.0f)) return WM_BOARD_READER_ARROW_UP;
    if (scroll->hovered == WM_BOARD_READER_ARROW_DOWN &&
        wm_board_reader_scroll_arrow_visible(scroll,
                                              WM_BOARD_READER_ARROW_DOWN) &&
        point_in_rect(x, y, down, 8.0f)) return WM_BOARD_READER_ARROW_DOWN;
    if (wm_board_reader_scroll_arrow_visible(scroll,
                                              WM_BOARD_READER_ARROW_DOWN) &&
        point_in_rect(x, y, down, 0.0f)) return WM_BOARD_READER_ARROW_DOWN;
    if (wm_board_reader_scroll_arrow_visible(scroll,
                                              WM_BOARD_READER_ARROW_UP) &&
        point_in_rect(x, y, up, 0.0f)) return WM_BOARD_READER_ARROW_UP;
    return WM_BOARD_READER_ARROW_NONE;
}

static WmLayoutClip clip(const char *animation, const char *group,
                         float frame) {
    return (WmLayoutClip){
        .animation = animation,
        .group = group,
        .frame = frame,
        .loop_override = 0
    };
}

size_t wm_board_reader_scroll_clips(const WmBoardReaderScroll *scroll,
                                     WmLayoutClip *clips, size_t capacity) {
    if (!scroll) return 0;
    size_t needed = 4;
    for (size_t index = 0; index < 2; index++) {
        if (scroll->arrows[index].press_active) needed++;
    }
    if (!clips || capacity < needed) return needed;
    static const char *end_groups[2] = {
        "G_ArwR_End", "G_ArwL_End"
    };
    static const char *focus_groups[2] = {
        "G_ArwR_Focus", "G_ArwL_Focus"
    };
    static const char *press_groups[2] = {
        "G_ArwR_Ac", "G_ArwL_Ac"
    };
    for (size_t index = 0; index < 2; index++) {
        const WmBoardReaderArrowState *state = &scroll->arrows[index];
        clips[index] = clip(state->visible ? "my_Memo_a_Appear" :
                            "my_Memo_a_Lost", end_groups[index],
                            clamp(state->appearance_frame, 10.0f));
        clips[index + 2] = clip(state->focus_active && !state->focus_entering
                                   ? "my_Memo_a_FocusOff" :
                                     "my_Memo_a_FocusOn",
                                focus_groups[index],
                                clamp(state->focus_active
                                          ? state->focus_frame : 0.0f,
                                      15.0f));
    }
    size_t written = 4;
    for (size_t index = 0; index < 2; index++) {
        const WmBoardReaderArrowState *state = &scroll->arrows[index];
        if (!state->press_active) continue;
        clips[written++] = clip("my_Memo_a_Select", press_groups[index],
                                clamp(state->press_frame, 30.0f));
    }
    return written;
}

size_t wm_board_reader_scroll_line_count(const WmBoardReaderScroll *scroll) {
    return scroll ? scroll->line_count : 0;
}

size_t wm_board_reader_scroll_repeated_rows(const WmBoardReaderScroll *scroll) {
    return scroll && scroll->line_count ? scroll->line_count - 1 : 0;
}

float wm_board_reader_scroll_footer_shift(const WmBoardReaderScroll *scroll) {
    return scroll ? -(float)wm_board_reader_scroll_repeated_rows(scroll) *
                        scroll->line_height : 0.0f;
}

float wm_board_reader_scroll_offset(const WmBoardReaderScroll *scroll) {
    return scroll ? scroll->offset : 0.0f;
}

float wm_board_reader_scroll_limit(const WmBoardReaderScroll *scroll) {
    return scroll ? scroll->limit : 0.0f;
}

bool wm_board_reader_scroll_moving(const WmBoardReaderScroll *scroll) {
    return scroll && scroll->moving;
}

bool wm_board_reader_scroll_sound_active(const WmBoardReaderScroll *scroll) {
    return scroll && scroll->sound_active;
}
