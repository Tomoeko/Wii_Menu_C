#ifndef WII_MENU_BOARD_CALENDAR_H
#define WII_MENU_BOARD_CALENDAR_H

#include "wii_menu/board_scene.h"

typedef struct WmBoardCalendar WmBoardCalendar;

typedef enum WmBoardCalendarPhase {
    WM_CALENDAR_CLOSED,
    WM_CALENDAR_ENTER,
    WM_CALENDAR_IDLE,
    WM_CALENDAR_SCROLL_PREVIOUS,
    WM_CALENDAR_SCROLL_NEXT,
    WM_CALENDAR_SELECT,
    WM_CALENDAR_EXIT
} WmBoardCalendarPhase;

typedef enum WmBoardCalendarControl {
    WM_CALENDAR_CONTROL_NONE,
    WM_CALENDAR_CONTROL_BACK,
    WM_CALENDAR_CONTROL_PREVIOUS,
    WM_CALENDAR_CONTROL_NEXT,
    WM_CALENDAR_CONTROL_DAY
} WmBoardCalendarControl;

typedef struct WmBoardCalendarHit {
    WmBoardCalendarControl control;
    unsigned day_index; /* Index in the visible five or six week sheet. */
} WmBoardCalendarHit;

typedef struct WmBoardCalendarDayPresentation {
    unsigned day_index;
    float focus_frame;
} WmBoardCalendarDayPresentation;

typedef enum WmBoardCalendarOutcome {
    WM_CALENDAR_OUTCOME_NONE,
    WM_CALENDAR_OUTCOME_BACK,
    WM_CALENDAR_OUTCOME_SELECTED
} WmBoardCalendarOutcome;

WmBoardCalendar *wm_board_calendar_create(WmPlatform *platform,
                                           const char *assets_directory,
                                           WmTextureCache *textures,
                                           WmFontCache *fonts);
void wm_board_calendar_destroy(WmBoardCalendar *calendar);
void wm_board_calendar_reset(WmBoardCalendar *calendar);
bool wm_board_calendar_open(WmBoardCalendar *calendar, WmBoardDate date,
                             WmBoardDate today);
bool wm_board_calendar_set_message_dates(WmBoardCalendar *calendar,
                                          const WmBoardDate *dates,
                                          size_t count);
void wm_board_calendar_advance(WmBoardCalendar *calendar, float frames);
WmBoardCalendarPhase wm_board_calendar_phase(const WmBoardCalendar *calendar);
WmBoardDate wm_board_calendar_month(const WmBoardCalendar *calendar);
WmBoardCalendarOutcome wm_board_calendar_take_outcome(WmBoardCalendar *calendar,
                                                      WmBoardDate *selected_date);
void wm_board_calendar_draw(WmBoardCalendar *calendar);
WmBoardCalendarHit wm_board_calendar_hit(WmBoardCalendar *calendar,
                                         int x, int y);
/* Returns back-to-front day layers outside month scrolling. */
bool wm_board_calendar_day_presentation(
    const WmBoardCalendar *calendar, unsigned layer,
    WmBoardCalendarDayPresentation *presentation);
void wm_board_calendar_hover(WmBoardCalendar *calendar,
                             WmBoardCalendarHit hit);
bool wm_board_calendar_activate(WmBoardCalendar *calendar,
                                 WmBoardCalendarHit hit);
bool wm_board_calendar_back(WmBoardCalendar *calendar);

#endif
