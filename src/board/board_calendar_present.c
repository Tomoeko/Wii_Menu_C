#include "board_calendar_internal.h"

#include "wii_menu/layout/layout_present.h"

#include <math.h>
#include <stdio.h>
#include <string.h>

static float clamp_frame(float value, float maximum) {
    return fminf(fmaxf(value, 0.0f), maximum);
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

void board_calendar_pose_sheet(WmBoardCalendar *calendar) {
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
        WmBoardDate month = board_calendar_shift_month(central, block - 1);
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

void board_calendar_day_matrix(const WmBoardCalendar *calendar, unsigned index,
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

void board_calendar_pose_day(WmBoardCalendar *calendar, CalendarCell cell,
                             unsigned index, bool incoming) {
    float focus_frame = incoming ? 0.0f : day_focus_frame(calendar, index);
    float select_frame = 50.0f;
    if (!incoming && calendar->selected &&
        board_calendar_same_date(calendar->selected_date, cell.date)) {
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
    board_calendar_pose_day(calendar, cell, index, incoming);
    float matrix[12];
    board_calendar_day_matrix(calendar, index, anchor, matrix);
    present(calendar, calendar->day, WM_LAYOUT_EMBEDDED, matrix);
}

void board_calendar_pose_footer(WmBoardCalendar *calendar) {
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
        bool hidden = board_calendar_month_at_limit(calendar, previous);
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
    board_calendar_pose_sheet(calendar);
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
    board_calendar_pose_footer(calendar);
    present(calendar, calendar->footer, WM_LAYOUT_IPL, NULL);
}
