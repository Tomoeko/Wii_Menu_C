#ifndef WM_BOARD_CALENDAR_INTERNAL_H
#define WM_BOARD_CALENDAR_INTERNAL_H

#include "wii_menu/board/board_calendar.h"
#include "wii_menu/layout/layout_runtime.h"

enum { CALENDAR_MAX_CELLS = 42 };

typedef struct WmBoardCalendarDayPresentation {
    unsigned day_index;
    float focus_frame;
} WmBoardCalendarDayPresentation;

/* Returns back-to-front day layers outside month scrolling. */
bool wm_board_calendar_day_presentation(
    const WmBoardCalendar *calendar, unsigned layer,
    WmBoardCalendarDayPresentation *presentation);

/* The controller owns dates, focus, and the three layouts. Presentation
 * borrows them; hit testing poses the same layouts used by drawing. */
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

static inline bool board_calendar_same_date(WmBoardDate first,
                                            WmBoardDate second) {
    return first.year == second.year && first.month == second.month &&
           first.day == second.day;
}

static inline bool board_calendar_month_at_limit(const WmBoardCalendar *calendar,
                                                 bool previous) {
    return previous ? calendar->month.year == 2000 && calendar->month.month == 1
                    : calendar->month.year == 2035 && calendar->month.month == 12;
}

WmBoardDate board_calendar_shift_month(WmBoardDate date, int amount);
void board_calendar_pose_sheet(WmBoardCalendar *calendar);
void board_calendar_pose_day(WmBoardCalendar *calendar, CalendarCell cell,
                             unsigned index, bool incoming);
void board_calendar_day_matrix(const WmBoardCalendar *calendar, unsigned index,
                               unsigned anchor_index, float matrix[12]);
void board_calendar_pose_footer(WmBoardCalendar *calendar);

#endif
