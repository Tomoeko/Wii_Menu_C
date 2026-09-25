#define _POSIX_C_SOURCE 200809L

#include "wii_menu/board_calendar.h"

#include "wii_menu/layout_present.h"
#include "wii_menu/layout_runtime.h"
#include "wii_menu/material_prepare.h"
#include "wii_menu/source_hit.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

enum { CALENDAR_PATH_CAPACITY = 4096, CALENDAR_MAX_CELLS = 42 };

typedef struct CalendarCell {
    WmBoardDate date;
    bool current;
    bool disabled;
    bool today;
    bool has_message;
    unsigned weekday;
} CalendarCell;

typedef struct CalendarFocus {
    bool active;
    bool entering;
    bool desired;
    bool playing;
    float frame;
} CalendarFocus;

struct WmBoardCalendar {
    WmPlatform *platform;
    WmTextureCache *textures;
    WmFontCache *fonts;
    WmLayout *sheet;
    WmLayout *day;
    WmLayout *footer;
    WmBoardDate month;
    WmBoardDate old_month;
    WmBoardDate today;
    WmBoardDate selected_date;
    WmBoardDate *message_dates;
    size_t message_date_count;
    CalendarCell cells[CALENDAR_MAX_CELLS];
    CalendarCell incoming[CALENDAR_MAX_CELLS];
    unsigned cell_count;
    unsigned incoming_count;
    float anchors[3][12];
    WmBoardCalendarPhase phase;
    float phase_frame;
    float age;
    bool selected;
    WmBoardCalendarHit hover;
    CalendarFocus day_focus[CALENDAR_MAX_CELLS];
    unsigned day_order[CALENDAR_MAX_CELLS];
    unsigned day_order_count;
    CalendarFocus back_focus;
    CalendarFocus arrow_focus[2];
    float arrow_press[2];
    WmBoardCalendarOutcome outcome;
};

static void pose_sheet(WmBoardCalendar *calendar);
static void pose_day(WmBoardCalendar *calendar, CalendarCell cell,
                     unsigned index, bool incoming);
static void day_matrix(const WmBoardCalendar *calendar, unsigned index,
                       unsigned anchor_index, float matrix[12]);
static void pose_footer(WmBoardCalendar *calendar);

static bool same_date(WmBoardDate first, WmBoardDate second) {
    return first.year == second.year && first.month == second.month &&
           first.day == second.day;
}

static WmLayout *load_layout(const char *directory, const char *relative) {
    char path[CALENDAR_PATH_CAPACITY];
    int length = snprintf(path, sizeof(path), "%s/%s", directory, relative);
    if (length < 0 || length >= (int)sizeof(path)) return NULL;
    char error[160] = {0};
    WmLayout *layout = wm_layout_load_json(path, error, sizeof(error));
    if (!layout) {
        fprintf(stderr, "Could not load Calendar layout %s: %s\n",
                relative, error);
    }
    return layout;
}

static float clamp_frame(float value, float maximum) {
    return fminf(fmaxf(value, 0.0f), maximum);
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

static WmBoardDate shift_month(WmBoardDate date, int amount) {
    return normalized_date(date.year, date.month + amount, 1, NULL);
}

static bool has_message(const WmBoardCalendar *calendar, WmBoardDate date) {
    for (size_t index = 0; index < calendar->message_date_count; index++) {
        if (same_date(calendar->message_dates[index], date)) return true;
    }
    return false;
}

static unsigned make_cells(WmBoardCalendar *calendar, WmBoardDate month,
                           CalendarCell cells[CALENDAR_MAX_CELLS]) {
    unsigned first_weekday = 0;
    if (!wm_board_date_valid(month)) return 0;
    normalized_date(month.year, month.month, 1, &first_weekday);
    WmBoardDate following = shift_month(month, 1);
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
            .today = same_date(date, calendar->today),
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

static bool month_at_limit(const WmBoardCalendar *calendar, bool previous) {
    return previous ? calendar->month.year == 2000 &&
                      calendar->month.month == 1
                    : calendar->month.year == 2035 &&
                      calendar->month.month == 12;
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
        if (month_at_limit(calendar, previous)) return false;
        calendar->old_month = calendar->month;
        calendar->month = shift_month(calendar->month, previous ? -1 : 1);
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
        float duration = calendar->phase == WM_CALENDAR_ENTER ? 40.0f :
                         calendar->phase == WM_CALENDAR_EXIT ? 50.0f : 30.0f;
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
        pose_footer(calendar);
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
        if (!month_at_limit(calendar, true) &&
            footer_hit(calendar, "B_ArwL", x, y, 0.0f)) {
            return (WmBoardCalendarHit){WM_CALENDAR_CONTROL_PREVIOUS,
                                        CALENDAR_MAX_CELLS};
        }
        if (!month_at_limit(calendar, false) &&
            footer_hit(calendar, "B_ArwR", x, y, 0.0f)) {
            return (WmBoardCalendarHit){WM_CALENDAR_CONTROL_NEXT,
                                        CALENDAR_MAX_CELLS};
        }
    }
    pose_sheet(calendar);
    unsigned hovered = calendar->hover.control == WM_CALENDAR_CONTROL_DAY
                           ? calendar->hover.day_index : CALENDAR_MAX_CELLS;
    if (hovered < calendar->cell_count &&
        !calendar->cells[hovered].disabled) {
        pose_day(calendar, calendar->cells[hovered], hovered, false);
        float matrix[12];
        day_matrix(calendar, hovered, 1, matrix);
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
        pose_day(calendar, calendar->cells[day], day, false);
        float matrix[12];
        day_matrix(calendar, day, 1, matrix);
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

static void present(WmBoardCalendar *calendar, const WmLayout *layout,
                    WmLayoutMode mode, const float matrix[12]) {
    wm_layout_present_with_fonts(calendar->platform, calendar->textures,
                                 calendar->fonts, layout, true, mode, matrix);
}

static void append_clip(WmLayoutClip *clips, size_t *count,
                        const char *animation, const char *group,
                        const char *target, float frame) {
    clips[*count] = (WmLayoutClip){
        .animation = animation,
        .group = group,
        .target_name = target,
        .frame = frame,
        .loop_override = 0
    };
    (*count)++;
}

static float sheet_frame(const WmBoardCalendar *calendar) {
    switch (calendar->phase) {
        case WM_CALENDAR_ENTER:
            return 1000.0f + clamp_frame(calendar->phase_frame, 40.0f);
        case WM_CALENDAR_SCROLL_NEXT:
            return 2000.0f + clamp_frame(calendar->phase_frame, 30.0f);
        case WM_CALENDAR_SCROLL_PREVIOUS:
            return 3000.0f + clamp_frame(calendar->phase_frame, 30.0f);
        case WM_CALENDAR_EXIT:
            return 4000.0f + clamp_frame(calendar->phase_frame, 20.0f);
        case WM_CALENDAR_CLOSED:
        case WM_CALENDAR_IDLE:
        case WM_CALENDAR_SELECT:
            return 1040.0f;
    }
    return 1040.0f;
}

static float footer_frame(const WmBoardCalendar *calendar) {
    if (calendar->phase == WM_CALENDAR_EXIT) {
        return (calendar->selected ? 3500.0f : 3000.0f) +
               clamp_frame(calendar->phase_frame, 50.0f);
    }
    return 2000.0f + clamp_frame(calendar->age, 50.0f);
}

static bool capture_anchor(void *context, const WmLayoutPaneView *pane) {
    WmBoardCalendar *calendar = context;
    static const char *const names[3] = {
        "N_CalPos_a", "N_CalPos_b", "N_CalPos_c"
    };
    for (unsigned index = 0; index < 3; index++) {
        if (strcmp(pane->name, names[index]) == 0) {
            memcpy(calendar->anchors[index], pane->matrix,
                   sizeof(calendar->anchors[index]));
            break;
        }
    }
    return true;
}

static void month_label(WmBoardDate month, char buffer[64]) {
    static const char *const names[12] = {
        "January", "February", "March", "April", "May", "June",
        "July", "August", "September", "October", "November", "December"
    };
    snprintf(buffer, 64, "%s %d", names[month.month - 1], month.year);
}

static void pose_sheet(WmBoardCalendar *calendar) {
    WmLayoutClip clips[2] = {
        {
            .animation = "my_IplTop_g",
            .group = "G_All",
            .frame = sheet_frame(calendar),
            .loop_override = 0
        },
        {
            .animation = "my_IplTop_g",
            .group = "G_Yobi",
            .frame = 0.0f,
            .loop_override = 0
        }
    };
    wm_layout_pose(calendar->sheet, clips, 2);
    WmBoardDate central = calendar->old_month.year
                              ? calendar->old_month : calendar->month;
    static const char *const weekday_names[7] = {
        "Sun", "Mon", "Tue", "Wed", "Thu", "Fri", "Sat"
    };
    static const unsigned week_panes[3][7] = {
        {0, 1, 2, 3, 4, 5, 6},
        {8, 9, 10, 11, 12, 13, 7},
        {16, 17, 18, 19, 20, 14, 15}
    };
    for (int block = 0; block < 3; block++) {
        WmBoardDate month = shift_month(central, block - 1);
        char pane[32], title[64];
        snprintf(pane, sizeof(pane), "T_CalMonth_%c", 'a' + block);
        month_label(month, title);
        wm_layout_set_pose_text(calendar->sheet, pane, title);
        for (unsigned day = 0; day < 7; day++) {
            snprintf(pane, sizeof(pane), "TextBox_%02u",
                     week_panes[block][day]);
            wm_layout_set_pose_text(calendar->sheet, pane,
                                     weekday_names[day]);
        }
    }
    WmLayoutDrawOptions options = {
        .wide = true,
        .mode = WM_LAYOUT_IPL,
        .alpha = 1.0f,
        .on_pane = capture_anchor,
        .context = calendar
    };
    wm_layout_draw(calendar->sheet, &options);
}

static void day_matrix(const WmBoardCalendar *calendar, unsigned index,
                       unsigned anchor_index, float matrix[12]) {
    const float *anchor = calendar->anchors[anchor_index];
    memcpy(matrix, anchor, sizeof(calendar->anchors[0]));
    float x = (float)(index % 7) * 70.0f;
    float y = -(float)(index / 7) * 48.0f;
    matrix[3] = anchor[0] * x + anchor[1] * y + anchor[3];
    matrix[7] = anchor[4] * x + anchor[5] * y + anchor[7];
    matrix[11] = anchor[8] * x + anchor[9] * y + anchor[11];
    /* Each date is a separate IPL layout. Its flagged N_CalDay_p pane
     * cancels an IPL root X scale. The captured sheet anchor already gives
     * the correct cell position, while the embedded draw mode omits that
     * root scale. Restore it to the matrix basis only, keeping the 70-unit
     * source spacing and the native button width. */
    const float root_scale_x = 832.0f / 608.0f;
    matrix[0] *= root_scale_x;
    matrix[4] *= root_scale_x;
    matrix[8] *= root_scale_x;
}

static float day_focus_frame(const WmBoardCalendar *calendar,
                             unsigned index) {
    if (index >= CALENDAR_MAX_CELLS ||
        !calendar->day_focus[index].active) return 0.0f;
    CalendarFocus focus = calendar->day_focus[index];
    return (focus.entering ? 0.0f : 10.0f) +
           clamp_frame(focus.frame, focus.entering ? 6.0f : 8.0f);
}

bool wm_board_calendar_day_presentation(
    const WmBoardCalendar *calendar, unsigned layer,
    WmBoardCalendarDayPresentation *presentation) {
    if (!calendar || !presentation || layer >= calendar->day_order_count ||
        calendar->phase == WM_CALENDAR_CLOSED ||
        calendar->phase == WM_CALENDAR_SCROLL_PREVIOUS ||
        calendar->phase == WM_CALENDAR_SCROLL_NEXT) return false;
    unsigned index = calendar->day_order[layer];
    *presentation = (WmBoardCalendarDayPresentation){
        .day_index = index,
        .focus_frame = day_focus_frame(calendar, index)
    };
    return true;
}

static void pose_day(WmBoardCalendar *calendar, CalendarCell cell,
                     unsigned index, bool incoming) {
    float focus_frame = incoming ? 0.0f : day_focus_frame(calendar, index);
    float select_frame = 50.0f;
    if (!incoming && calendar->selected &&
        same_date(calendar->selected_date, cell.date)) {
        select_frame = 50.0f +
                       (calendar->phase == WM_CALENDAR_SELECT
                            ? clamp_frame(calendar->phase_frame, 25.0f)
                            : 25.0f);
    }
    float text_frame = !cell.current ? 3.0f :
                       cell.weekday == 6 ? 1.0f :
                       cell.weekday == 0 ? 2.0f : 0.0f;
    WmLayoutClip clips[7];
    size_t count = 0;
    append_clip(clips, &count, "my_IplTop_f", NULL,
                "N_CalDay_r", focus_frame);
    append_clip(clips, &count, "my_IplTop_f", NULL,
                "Cal_Ac", select_frame);
    append_clip(clips, &count, "my_IplTop_f", NULL,
                "W_CalC ", cell.current ? (cell.today ? 1.0f : 0.0f) : 3.0f);
    append_clip(clips, &count, "my_IplTop_f", NULL,
                "W_CalLT", cell.current ? (cell.today ? 1.0f : 0.0f) : 3.0f);
    append_clip(clips, &count, "my_IplTop_f", NULL,
                "T_Cal", text_frame);
    append_clip(clips, &count, "my_IplTop_f", NULL,
                "Info_a", cell.has_message ? 1.0f : 0.0f);
    wm_layout_pose(calendar->day, clips, count);
    char number[4];
    snprintf(number, sizeof(number), "%d", cell.date.day);
    wm_layout_set_pose_text(calendar->day, "T_Cal", number);
}

static void draw_day(WmBoardCalendar *calendar, CalendarCell cell,
                     unsigned index, unsigned anchor, bool incoming) {
    pose_day(calendar, cell, index, incoming);
    float matrix[12];
    day_matrix(calendar, index, anchor, matrix);
    present(calendar, calendar->day, WM_LAYOUT_EMBEDDED, matrix);
}

static void pose_footer(WmBoardCalendar *calendar) {
    WmLayoutClip clips[16];
    size_t count = 0;
    append_clip(clips, &count, "my_IplTop_e", "G_SeenChange",
                NULL, footer_frame(calendar));
    append_clip(clips, &count, "my_IplTop_e", "G_ArwRoop",
                NULL, 10000.0f + fmodf(calendar->age, 55.0f));
    static const char *const end_groups[2] = {
        "G_ArwL_End", "G_ArwR_End"
    };
    static const char *const focus_groups[2] = {
        "G_ArwL_Focus", "G_ArwR_Focus"
    };
    static const char *const press_groups[2] = {
        "G_ArwL_Ac", "G_ArwR_Ac"
    };
    for (unsigned arrow = 0; arrow < 2; arrow++) {
        bool previous = arrow == 0;
        bool hidden = month_at_limit(calendar, previous);
        float arrow_frame = hidden || calendar->age < 40.0f
                                ? 10110.0f : 10160.0f;
        if (!hidden && calendar->age >= 40.0f && calendar->age < 50.0f) {
            arrow_frame = 10150.0f + calendar->age - 40.0f;
        }
        if (calendar->phase == WM_CALENDAR_EXIT && !hidden) {
            arrow_frame = 10100.0f +
                          clamp_frame(calendar->phase_frame, 10.0f);
        }
        append_clip(clips, &count, "my_IplTop_e", end_groups[arrow],
                    NULL, arrow_frame);
        CalendarFocus focus = calendar->arrow_focus[arrow];
        append_clip(clips, &count, "my_IplTop_e", focus_groups[arrow], NULL,
                    (focus.active && !focus.entering ? 10800.0f : 10600.0f) +
                    clamp_frame(focus.frame, 15.0f));
        if (calendar->arrow_press[arrow] >= 0.0f) {
            append_clip(clips, &count, "my_IplTop_e", press_groups[arrow],
                        NULL,
                        10700.0f +
                        clamp_frame(calendar->arrow_press[arrow], 30.0f));
        }
    }
    if (calendar->back_focus.active) {
        CalendarFocus focus = calendar->back_focus;
        append_clip(clips, &count, "my_IplTop_e", "G_CalExit", NULL,
                    (focus.entering ? 2900.0f : 2930.0f) +
                    clamp_frame(focus.frame, focus.entering ? 6.0f : 8.0f));
    }
    wm_layout_pose(calendar->footer, clips, count);
    static const char *const empty_text[] = {
        "T_BbsMark1", "T_CalAdd_R", "T_Add", "T_Dust"
    };
    for (size_t index = 0; index <
         sizeof(empty_text) / sizeof(empty_text[0]); index++) {
        wm_layout_set_pose_text(calendar->footer, empty_text[index], "");
    }
    wm_layout_set_pose_text(calendar->footer, "T_CalExit", "Back");
}

void wm_board_calendar_draw(WmBoardCalendar *calendar) {
    if (!calendar || calendar->phase == WM_CALENDAR_CLOSED) return;
    pose_sheet(calendar);
    present(calendar, calendar->sheet, WM_LAYOUT_IPL, NULL);
    bool scrolling = calendar->phase == WM_CALENDAR_SCROLL_PREVIOUS ||
                     calendar->phase == WM_CALENDAR_SCROLL_NEXT;
    if (scrolling) {
        unsigned side = calendar->phase == WM_CALENDAR_SCROLL_PREVIOUS ? 0 : 2;
        for (unsigned position = calendar->incoming_count; position > 0;
             position--) {
            unsigned index = position - 1;
            draw_day(calendar, calendar->incoming[index], index, side, true);
        }
        for (unsigned position = calendar->cell_count; position > 0;
             position--) {
            unsigned index = position - 1;
            draw_day(calendar, calendar->cells[index], index, 1, false);
        }
    } else {
        /* Retain recently hovered days near the front while their eight-frame
         * departure plays. The fixed B_Cal hit pane remains a sibling of the
         * scaled N_CalDay_r visual pane. */
        for (unsigned position = 0; position < calendar->day_order_count;
             position++) {
            WmBoardCalendarDayPresentation presentation;
            if (!wm_board_calendar_day_presentation(calendar, position,
                                                     &presentation)) continue;
            unsigned index = presentation.day_index;
            draw_day(calendar, calendar->cells[index], index, 1, false);
        }
    }
    pose_footer(calendar);
    present(calendar, calendar->footer, WM_LAYOUT_IPL, NULL);
}
