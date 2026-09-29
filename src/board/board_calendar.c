#define _POSIX_C_SOURCE 200809L

#include "board_calendar_internal.h"

#include "wii_menu/layout/layout_assets.h"
#include "wii_menu/render/material_prepare.h"
#include "wii_menu/input/source_hit.h"

#include <math.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

static WmLayout *load_layout(const char *directory, const char *relative) {
    return wm_layout_load_asset(directory, relative, "Calendar");
}

static void reset_day_order(WmBoardCalendar *calendar) {
    calendar->day_order_count = calendar->cell_count;
    for (unsigned index = 0; index < calendar->cell_count; index++) {
        calendar->day_order[index] = index;
    }
}

static void bring_day_forward(WmBoardCalendar *calendar, unsigned index) {
    for (unsigned position = 0; position < calendar->day_order_count;
         position++) {
        if (calendar->day_order[position] != index) continue;
        memmove(calendar->day_order + position,
                calendar->day_order + position + 1,
                (calendar->day_order_count - position - 1) *
                    sizeof(calendar->day_order[0]));
        calendar->day_order[calendar->day_order_count - 1] = index;
        return;
    }
}

static WmBoardDate normalized_date(int year, int month, int day,
                                    unsigned *weekday) {
    struct tm value = {
        .tm_year = year - 1900,
        .tm_mon = month - 1,
        .tm_mday = day,
        .tm_hour = 12,
        .tm_isdst = -1
    };
    time_t timestamp = mktime(&value);
    if (timestamp == (time_t)-1) return (WmBoardDate){0, 0, 0};
    if (weekday) *weekday = (unsigned)value.tm_wday;
    return (WmBoardDate){value.tm_year + 1900,
                         value.tm_mon + 1, value.tm_mday};
}

WmBoardDate board_calendar_shift_month(WmBoardDate date, int amount) {
    return normalized_date(date.year, date.month + amount, 1, NULL);
}

static bool has_message(const WmBoardCalendar *calendar, WmBoardDate date) {
    for (size_t index = 0; index < calendar->message_date_count; index++) {
        if (board_calendar_same_date(calendar->message_dates[index], date)) {
            return true;
        }
    }
    return false;
}

static unsigned make_cells(WmBoardCalendar *calendar, WmBoardDate month,
                           CalendarCell cells[CALENDAR_MAX_CELLS]) {
    unsigned first_weekday = 0;
    if (!wm_board_date_valid(month)) return 0;
    normalized_date(month.year, month.month, 1, &first_weekday);
    WmBoardDate following = board_calendar_shift_month(month, 1);
    WmBoardDate last = normalized_date(following.year, following.month, 0, NULL);
    unsigned total = ((first_weekday + (unsigned)last.day + 6) / 7) * 7;
    if (total > CALENDAR_MAX_CELLS) total = CALENDAR_MAX_CELLS;
    for (unsigned index = 0; index < total; index++) {
        unsigned day_of_week = 0;
        WmBoardDate date = normalized_date(month.year, month.month,
                               (int)index - (int)first_weekday + 1,
                               &day_of_week);
        cells[index] = (CalendarCell){
            .date = date,
            .current = date.year == month.year && date.month == month.month,
            .disabled = !wm_board_date_valid(date),
            .today = board_calendar_same_date(date, calendar->today),
            .has_message = has_message(calendar, date),
            .weekday = day_of_week
        };
    }
    return total;
}

WmBoardCalendar *wm_board_calendar_create(WmPlatform *platform,
                                           const char *assets_directory,
                                           WmTextureCache *textures,
                                           WmFontCache *fonts) {
    if (!platform || !assets_directory || !assets_directory[0] ||
        !textures || !fonts) return NULL;
    WmBoardCalendar *calendar = calloc(1, sizeof(*calendar));
    if (!calendar) return NULL;
    calendar->platform = platform;
    calendar->textures = textures;
    calendar->fonts = fonts;
    calendar->sheet = load_layout(assets_directory,
                                   "layouts/calendar/my_IplTop_g.json");
    calendar->day = load_layout(assets_directory,
                                 "layouts/calendar/my_IplTop_f.json");
    calendar->footer = load_layout(assets_directory,
                                    "layouts/cmnBtn/my_IplTop_e.json");
    if (!calendar->sheet || !calendar->day || !calendar->footer) {
        wm_board_calendar_destroy(calendar);
        return NULL;
    }
    wm_layout_prepare_materials(platform, calendar->sheet);
    wm_layout_prepare_materials(platform, calendar->day);
    wm_layout_prepare_materials(platform, calendar->footer);
    calendar->arrow_press[0] = -1.0f;
    calendar->arrow_press[1] = -1.0f;
    calendar->hover.day_index = CALENDAR_MAX_CELLS;
    return calendar;
}

void wm_board_calendar_destroy(WmBoardCalendar *calendar) {
    if (!calendar) return;
    wm_layout_destroy(calendar->sheet);
    wm_layout_destroy(calendar->day);
    wm_layout_destroy(calendar->footer);
    free(calendar->message_dates);
    free(calendar);
}

void wm_board_calendar_reset(WmBoardCalendar *calendar) {
    if (!calendar) return;
    calendar->phase = WM_CALENDAR_CLOSED;
    calendar->phase_frame = 0.0f;
    calendar->age = 0.0f;
    calendar->selected = false;
    calendar->hover = (WmBoardCalendarHit){WM_CALENDAR_CONTROL_NONE,
                                            CALENDAR_MAX_CELLS};
    calendar->outcome = WM_CALENDAR_OUTCOME_NONE;
    calendar->arrow_press[0] = -1.0f;
    calendar->arrow_press[1] = -1.0f;
    memset(calendar->day_focus, 0, sizeof(calendar->day_focus));
    calendar->day_order_count = 0;
    memset(&calendar->back_focus, 0, sizeof(calendar->back_focus));
    memset(calendar->arrow_focus, 0, sizeof(calendar->arrow_focus));
}

bool wm_board_calendar_set_message_dates(WmBoardCalendar *calendar,
                                          const WmBoardDate *dates,
                                          size_t count) {
    if (!calendar || (count && !dates) ||
        count > SIZE_MAX / sizeof(*dates)) return false;
    WmBoardDate *copy = count ? malloc(count * sizeof(*copy)) : NULL;
    if (count && !copy) return false;
    if (copy) memcpy(copy, dates, count * sizeof(*copy));
    free(calendar->message_dates);
    calendar->message_dates = copy;
    calendar->message_date_count = count;
    if (calendar->phase != WM_CALENDAR_CLOSED) {
        calendar->cell_count = make_cells(calendar, calendar->old_month.year
                                         ? calendar->old_month : calendar->month,
                                         calendar->cells);
        if (calendar->incoming_count) {
            calendar->incoming_count = make_cells(calendar, calendar->month,
                                                   calendar->incoming);
        }
    }
    return true;
}

bool wm_board_calendar_open(WmBoardCalendar *calendar, WmBoardDate date,
                             WmBoardDate today) {
    if (!calendar || !wm_board_date_valid(date) ||
        !wm_board_date_valid(today) ||
        calendar->phase != WM_CALENDAR_CLOSED) return false;
    calendar->month = (WmBoardDate){date.year, date.month, 1};
    calendar->old_month = (WmBoardDate){0, 0, 0};
    calendar->today = today;
    calendar->selected = false;
    calendar->age = 0.0f;
    calendar->phase_frame = 0.0f;
    calendar->phase = WM_CALENDAR_ENTER;
    calendar->outcome = WM_CALENDAR_OUTCOME_NONE;
    calendar->hover = (WmBoardCalendarHit){WM_CALENDAR_CONTROL_NONE,
                                           CALENDAR_MAX_CELLS};
    memset(calendar->day_focus, 0, sizeof(calendar->day_focus));
    memset(&calendar->back_focus, 0, sizeof(calendar->back_focus));
    memset(calendar->arrow_focus, 0, sizeof(calendar->arrow_focus));
    calendar->cell_count = make_cells(calendar, calendar->month,
                                      calendar->cells);
    reset_day_order(calendar);
    calendar->incoming_count = 0;
    return calendar->cell_count > 0;
}

WmBoardCalendarPhase wm_board_calendar_phase(const WmBoardCalendar *calendar) {
    return calendar ? calendar->phase : WM_CALENDAR_CLOSED;
}

static float phase_duration(WmBoardCalendarPhase phase) {
    switch (phase) {
        case WM_CALENDAR_ENTER:
            return 40.0f;
        case WM_CALENDAR_EXIT:
            return 50.0f;
        case WM_CALENDAR_SELECT:
        case WM_CALENDAR_SCROLL_PREVIOUS:
        case WM_CALENDAR_SCROLL_NEXT:
            return 30.0f;
        case WM_CALENDAR_CLOSED:
        case WM_CALENDAR_IDLE:
            return 0.0f;
    }
    return 0.0f;
}

float wm_board_calendar_frames_to_boundary(const WmBoardCalendar *calendar) {
    if (!calendar) return 0.0f;
    return fmaxf(0.0f, phase_duration(calendar->phase) - calendar->phase_frame);
}

WmBoardDate wm_board_calendar_month(const WmBoardCalendar *calendar) {
    return calendar ? calendar->month : (WmBoardDate){0, 0, 0};
}

WmBoardCalendarOutcome wm_board_calendar_take_outcome(WmBoardCalendar *calendar,
                                                      WmBoardDate *selected_date) {
    if (!calendar) return WM_CALENDAR_OUTCOME_NONE;
    WmBoardCalendarOutcome outcome = calendar->outcome;
    if (selected_date && outcome == WM_CALENDAR_OUTCOME_SELECTED) {
        *selected_date = calendar->selected_date;
    }
    calendar->outcome = WM_CALENDAR_OUTCOME_NONE;
    return outcome;
}

bool wm_board_calendar_back(WmBoardCalendar *calendar) {
    if (!calendar || calendar->phase != WM_CALENDAR_IDLE ||
        calendar->age < 50.0f) return false;
    calendar->phase = WM_CALENDAR_EXIT;
    calendar->phase_frame = 0.0f;
    return true;
}

bool wm_board_calendar_activate(WmBoardCalendar *calendar,
                                 WmBoardCalendarHit hit) {
    if (!calendar || calendar->phase != WM_CALENDAR_IDLE) return false;
    /* Date::onPointDate becomes interactive when the body reaches frame 40.
     * The separate common footer is still entering until frame 50. */
    if (hit.control == WM_CALENDAR_CONTROL_DAY &&
        hit.day_index < calendar->cell_count &&
        !calendar->cells[hit.day_index].disabled) {
        calendar->selected_date = calendar->cells[hit.day_index].date;
        calendar->selected = true;
        calendar->phase = WM_CALENDAR_SELECT;
        calendar->phase_frame = 0.0f;
        return true;
    }
    if (calendar->age < 50.0f) return false;
    if (hit.control == WM_CALENDAR_CONTROL_BACK) {
        return wm_board_calendar_back(calendar);
    }
    if (hit.control == WM_CALENDAR_CONTROL_PREVIOUS ||
        hit.control == WM_CALENDAR_CONTROL_NEXT) {
        bool previous = hit.control == WM_CALENDAR_CONTROL_PREVIOUS;
        if (board_calendar_month_at_limit(calendar, previous)) return false;
        calendar->old_month = calendar->month;
        calendar->month = board_calendar_shift_month(calendar->month,
                                                     previous ? -1 : 1);
        calendar->incoming_count = make_cells(calendar, calendar->month,
                                               calendar->incoming);
        calendar->phase = previous ? WM_CALENDAR_SCROLL_PREVIOUS :
                                     WM_CALENDAR_SCROLL_NEXT;
        calendar->phase_frame = 0.0f;
        calendar->arrow_press[previous ? 0 : 1] = 0.0f;
        if (calendar->hover.control != WM_CALENDAR_CONTROL_PREVIOUS &&
            calendar->hover.control != WM_CALENDAR_CONTROL_NEXT) {
            calendar->hover = (WmBoardCalendarHit){WM_CALENDAR_CONTROL_NONE,
                                                   CALENDAR_MAX_CELLS};
        }
        memset(calendar->day_focus, 0, sizeof(calendar->day_focus));
        calendar->back_focus = (CalendarFocus){0};
        reset_day_order(calendar);
        return true;
    }
    return false;
}

void wm_board_calendar_advance(WmBoardCalendar *calendar, float frames) {
    if (!calendar || calendar->phase == WM_CALENDAR_CLOSED ||
        !isfinite(frames) || frames <= 0.0f) return;
    calendar->age += frames;
    for (unsigned index = 0; index < CALENDAR_MAX_CELLS; index++) {
        CalendarFocus *focus = &calendar->day_focus[index];
        float remaining_focus = frames;
        while (focus->active && focus->playing && remaining_focus > 0.0f) {
            float duration = focus->entering ? 6.0f : 8.0f;
            float step = fminf(remaining_focus, duration - focus->frame);
            focus->frame += step;
            remaining_focus -= step;
            if (focus->frame < duration) break;
            focus->playing = false;
            if (focus->desired != focus->entering) {
                focus->entering = focus->desired;
                focus->frame = 0.0f;
                focus->playing = true;
            }
        }
    }
    if (calendar->back_focus.active) calendar->back_focus.frame += frames;
    for (unsigned index = 0; index < 2; index++) {
        if (calendar->arrow_focus[index].active) {
            calendar->arrow_focus[index].frame += frames;
        }
        if (calendar->arrow_press[index] >= 0.0f) {
            calendar->arrow_press[index] += frames;
            if (calendar->arrow_press[index] > 30.0f) {
                calendar->arrow_press[index] = -1.0f;
            }
        }
    }
    float remaining = frames;
    while (remaining > 0.0f && calendar->phase != WM_CALENDAR_IDLE &&
           calendar->phase != WM_CALENDAR_CLOSED) {
        float duration = phase_duration(calendar->phase);
        float amount = fminf(remaining, duration - calendar->phase_frame);
        calendar->phase_frame += amount;
        remaining -= amount;
        if (calendar->phase_frame < duration) break;
        if (calendar->phase == WM_CALENDAR_SELECT) {
            calendar->phase = WM_CALENDAR_EXIT;
        } else if (calendar->phase == WM_CALENDAR_EXIT) {
            calendar->phase = WM_CALENDAR_CLOSED;
            calendar->outcome = calendar->selected
                                    ? WM_CALENDAR_OUTCOME_SELECTED
                                    : WM_CALENDAR_OUTCOME_BACK;
        } else {
            if (calendar->phase == WM_CALENDAR_SCROLL_PREVIOUS ||
                calendar->phase == WM_CALENDAR_SCROLL_NEXT) {
                memcpy(calendar->cells, calendar->incoming,
                       calendar->incoming_count * sizeof(calendar->cells[0]));
                calendar->cell_count = calendar->incoming_count;
                calendar->incoming_count = 0;
                calendar->old_month = (WmBoardDate){0, 0, 0};
                reset_day_order(calendar);
            }
            calendar->phase = WM_CALENDAR_IDLE;
        }
        calendar->phase_frame = 0.0f;
    }
}

static bool point_in_rect(int x, int y, WmSourceRect rect, float margin) {
    return (float)x >= rect.x - margin &&
           (float)x < rect.x + rect.width + margin &&
           (float)y >= rect.y - margin &&
           (float)y < rect.y + rect.height + margin;
}

static bool footer_hit(WmBoardCalendar *calendar, const char *pane,
                       int x, int y, float margin) {
    WmSourceRect rect;
    return wm_source_pane_rect(calendar->footer, pane, true,
                               WM_LAYOUT_IPL, NULL, &rect) &&
           point_in_rect(x, y, rect, margin);
}

WmBoardCalendarHit wm_board_calendar_hit(WmBoardCalendar *calendar,
                                         int x, int y) {
    WmBoardCalendarHit none = {WM_CALENDAR_CONTROL_NONE, CALENDAR_MAX_CELLS};
    if (!calendar ||
        (calendar->phase != WM_CALENDAR_IDLE &&
         calendar->phase != WM_CALENDAR_SCROLL_PREVIOUS &&
         calendar->phase != WM_CALENDAR_SCROLL_NEXT)) return none;
    if (calendar->age >= 50.0f) {
        board_calendar_pose_footer(calendar);
        if (calendar->hover.control == WM_CALENDAR_CONTROL_PREVIOUS &&
            footer_hit(calendar, "B_ArwL", x, y, 4.0f)) return calendar->hover;
        if (calendar->hover.control == WM_CALENDAR_CONTROL_NEXT &&
            footer_hit(calendar, "B_ArwR", x, y, 4.0f)) return calendar->hover;
    }
    if (calendar->phase != WM_CALENDAR_IDLE) return none;
    if (calendar->age >= 50.0f) {
        if (footer_hit(calendar, "B_CalExit", x, y, 0.0f)) {
            return (WmBoardCalendarHit){WM_CALENDAR_CONTROL_BACK,
                                        CALENDAR_MAX_CELLS};
        }
        if (!board_calendar_month_at_limit(calendar, true) &&
            footer_hit(calendar, "B_ArwL", x, y, 0.0f)) {
            return (WmBoardCalendarHit){WM_CALENDAR_CONTROL_PREVIOUS,
                                        CALENDAR_MAX_CELLS};
        }
        if (!board_calendar_month_at_limit(calendar, false) &&
            footer_hit(calendar, "B_ArwR", x, y, 0.0f)) {
            return (WmBoardCalendarHit){WM_CALENDAR_CONTROL_NEXT,
                                        CALENDAR_MAX_CELLS};
        }
    }
    board_calendar_pose_sheet(calendar);
    unsigned hovered = calendar->hover.control == WM_CALENDAR_CONTROL_DAY
                           ? calendar->hover.day_index : CALENDAR_MAX_CELLS;
    if (hovered < calendar->cell_count &&
        !calendar->cells[hovered].disabled) {
        board_calendar_pose_day(calendar, calendar->cells[hovered], hovered,
                                false);
        float matrix[12];
        board_calendar_day_matrix(calendar, hovered, 1, matrix);
        WmSourceRect rect;
        if (wm_source_pane_rect(calendar->day, "B_Cal", true,
                                WM_LAYOUT_EMBEDDED, matrix, &rect) &&
            /* W_Cal is 72 x 50 source units while the stationary B_Cal
             * button is 66 x 44. Retain focus through the narrow visible
             * rim so a one-pixel motion cannot leave and reacquire the same
             * date (and replay its hover cue). New hits still use B_Cal. */
            point_in_rect(x, y, rect, 3.0f)) {
            return (WmBoardCalendarHit){WM_CALENDAR_CONTROL_DAY, hovered};
        }
    }
    for (unsigned position = calendar->day_order_count; position > 0;
         position--) {
        unsigned day = calendar->day_order[position - 1];
        if (day == hovered || calendar->cells[day].disabled) continue;
        board_calendar_pose_day(calendar, calendar->cells[day], day, false);
        float matrix[12];
        board_calendar_day_matrix(calendar, day, 1, matrix);
        WmSourceRect rect;
        if (wm_source_pane_rect(calendar->day, "B_Cal", true,
                                WM_LAYOUT_EMBEDDED, matrix, &rect) &&
            point_in_rect(x, y, rect, 0.0f)) {
            return (WmBoardCalendarHit){WM_CALENDAR_CONTROL_DAY, day};
        }
    }
    return none;
}

static bool same_hit(WmBoardCalendarHit first, WmBoardCalendarHit second) {
    return first.control == second.control &&
           (first.control != WM_CALENDAR_CONTROL_DAY ||
            first.day_index == second.day_index);
}

static void request_day_focus(CalendarFocus *focus, bool entering) {
    if (!focus->active) {
        *focus = (CalendarFocus){true, entering, entering, true, 0.0f};
        return;
    }
    focus->desired = entering;
    if (!focus->playing && focus->entering != entering) {
        focus->entering = entering;
        focus->frame = 0.0f;
        focus->playing = true;
    }
}

void wm_board_calendar_hover(WmBoardCalendar *calendar,
                             WmBoardCalendarHit hit) {
    if (!calendar ||
        (calendar->phase != WM_CALENDAR_IDLE &&
         calendar->phase != WM_CALENDAR_SCROLL_PREVIOUS &&
         calendar->phase != WM_CALENDAR_SCROLL_NEXT)) return;
    if (calendar->age < 50.0f &&
        hit.control != WM_CALENDAR_CONTROL_DAY &&
        hit.control != WM_CALENDAR_CONTROL_NONE) return;
    if (calendar->phase != WM_CALENDAR_IDLE &&
        hit.control != WM_CALENDAR_CONTROL_PREVIOUS &&
        hit.control != WM_CALENDAR_CONTROL_NEXT) {
        hit = (WmBoardCalendarHit){WM_CALENDAR_CONTROL_NONE,
                                   CALENDAR_MAX_CELLS};
    }
    if (same_hit(calendar->hover, hit)) return;
    WmBoardCalendarHit old = calendar->hover;
    if (old.control == WM_CALENDAR_CONTROL_DAY &&
        old.day_index < CALENDAR_MAX_CELLS) {
        request_day_focus(&calendar->day_focus[old.day_index], false);
    } else if (old.control == WM_CALENDAR_CONTROL_BACK) {
        calendar->back_focus = (CalendarFocus){true, false, false, true, 0};
    } else if (old.control == WM_CALENDAR_CONTROL_PREVIOUS ||
               old.control == WM_CALENDAR_CONTROL_NEXT) {
        calendar->arrow_focus[old.control == WM_CALENDAR_CONTROL_PREVIOUS ? 0 : 1] =
            (CalendarFocus){true, false, false, true, 0};
    }
    calendar->hover = hit;
    if (hit.control == WM_CALENDAR_CONTROL_DAY &&
        hit.day_index < calendar->cell_count) {
        request_day_focus(&calendar->day_focus[hit.day_index], true);
        bring_day_forward(calendar, hit.day_index);
    } else if (hit.control == WM_CALENDAR_CONTROL_BACK) {
        calendar->back_focus = (CalendarFocus){true, true, true, true, 0};
    } else if (hit.control == WM_CALENDAR_CONTROL_PREVIOUS ||
               hit.control == WM_CALENDAR_CONTROL_NEXT) {
        calendar->arrow_focus[hit.control == WM_CALENDAR_CONTROL_PREVIOUS ? 0 : 1] =
            (CalendarFocus){true, true, true, true, 0};
    }
}
