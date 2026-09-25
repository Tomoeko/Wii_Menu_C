#define _POSIX_C_SOURCE 200809L

#include "wii_menu/board_scene.h"
#include "wii_menu/board_calendar.h"
#include "wii_menu/board_compose.h"
#include "wii_menu/board_erase.h"
#include "wii_menu/board_store.h"
#include "wii_menu/layout_runtime.h"
#include "wii_menu/source_hit.h"
#include "wii_menu/texture_cache.h"

#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

static bool capture_text_draws;
static size_t captured_text_quads;
static size_t captured_light_text_quads;
static uint32_t next_texture_handle = 1;

/* The draw test records submitted glyphs without a GPU or display server. */
void wm_platform_begin(WmPlatform *platform, WmColor clear_color) {
    (void)platform;
    (void)clear_color;
    assert(false);
}

void wm_platform_end(WmPlatform *platform) {
    (void)platform;
    assert(false);
}

void wm_platform_prepare_material(WmPlatform *platform,
                                  const WmMaterialQuad *quad) {
    (void)platform;
    (void)quad;
}

void wm_platform_set_clip(WmPlatform *platform, const WmClipRect *rect) {
    (void)platform;
    (void)rect;
}

void wm_platform_draw_quad(WmPlatform *platform, const WmQuad *quad) {
    (void)platform;
    (void)quad;
}

void wm_platform_draw_vertices(WmPlatform *platform,
                               const WmDrawVertex vertices[4],
                               uint32_t texture) {
    (void)platform;
    (void)texture;
    if (!capture_text_draws) return;
    captured_text_quads++;
    if (vertices[0].color.r > 0.55f &&
        vertices[0].color.r < 0.85f) captured_light_text_quads++;
}

void wm_platform_draw_material_quad(WmPlatform *platform,
                                    const WmMaterialQuad *quad) {
    (void)platform;
    (void)quad;
}

uint32_t wm_platform_create_texture(WmPlatform *platform, int width,
                                    int height, const uint8_t *rgba) {
    (void)platform;
    assert(width > 0 && height > 0 && rgba);
    return next_texture_handle++;
}

void wm_platform_destroy_texture(WmPlatform *platform, uint32_t texture) {
    (void)platform;
    (void)texture;
}

static void test_date_arithmetic(void) {
    WmBoardDate date;
    assert(!wm_board_date_valid((WmBoardDate){2000, 2, 30}));
    assert(wm_board_date_valid((WmBoardDate){2000, 2, 29}));
    assert(!wm_board_date_shift((WmBoardDate){2000, 1, 1}, -1, &date));
    assert(wm_board_date_shift((WmBoardDate){2000, 2, 28}, 1, &date));
    assert(date.year == 2000 && date.month == 2 && date.day == 29);
    assert(wm_board_date_shift(date, 1, &date));
    assert(date.year == 2000 && date.month == 3 && date.day == 1);
    assert(wm_board_date_shift((WmBoardDate){2026, 12, 31}, 1, &date));
    assert(date.year == 2027 && date.month == 1 && date.day == 1);
    assert(!wm_board_date_shift((WmBoardDate){2035, 12, 31}, 1, &date));
    assert(wm_board_date_shift((WmBoardDate){2000, 1, 1}, 13148, &date));
    assert(date.year == 2035 && date.month == 12 && date.day == 31);
}

static void test_badge(void) {
    WmBoardMemo memos[120] = {0};
    WmBoardDate today = {2026, 9, 25};
    for (size_t index = 0; index < 120; index++) memos[index].date = today;
    memos[0].date.day = 24;
    assert(wm_board_badge_count(memos, 120, today) == 99);
    assert(wm_board_badge_count(memos, 1, today) == 0);
    assert(wm_board_badge_count(NULL, 0, today) == 0);
}

static WmFontCache *test_fonts;

static WmBoardScene *load_board(int argc, char **argv) {
    const char *assets = argc > 1 ? argv[1] : ".local/native-assets";
    char path[4096];
    int length = snprintf(path, sizeof(path),
                          "%s/layouts/board/my_IplTop_c.json", assets);
    assert(length > 0 && length < (int)sizeof(path));
    FILE *check = fopen(path, "rb");
    if (!check) {
        puts("Message Board resource test skipped: local WAD export absent.");
        return NULL;
    }
    fclose(check);
    if (!test_fonts) {
        test_fonts = wm_font_cache_create((WmPlatform *)1, assets,
                                           1024u * 1024u);
        assert(test_fonts);
    }
    WmBoardScene *board = wm_board_scene_create(
        (WmPlatform *)1, assets, (WmTextureCache *)1, test_fonts);
    assert(board);
    return board;
}

static void test_home_badge_midnight_refresh(int argc, char **argv) {
    WmBoardScene *board = load_board(argc, argv);
    assert(board);
    const WmBoardDate previous_day = {2026, 9, 24};
    const WmBoardDate current_day = {2026, 9, 25};
    const WmBoardMemo memos[] = {
        {
            .id = "previous-day", .text = "Yesterday",
            .date = {2026, 9, 24}, .read = true
        },
        {
            .id = "current-unread", .text = "Today",
            .date = {2026, 9, 25}
        },
        {
            .id = "current-read", .text = "Read today",
            .date = {2026, 9, 25}, .read = true
        }
    };
    assert(wm_board_scene_set_memos(board, memos, 3));
    assert(wm_board_scene_refresh_today(board, previous_day));
    assert(wm_board_scene_today_count(board) == 1);
    assert(wm_board_scene_today_unread_count(board) == 0);
    assert(wm_board_scene_refresh_today(board, current_day));
    assert(wm_board_scene_today_count(board) == 2);
    assert(wm_board_scene_today_unread_count(board) == 1);
    assert(!wm_board_scene_refresh_today(board, (WmBoardDate){2026, 2, 30}));
    assert(wm_board_scene_today_count(board) == 2);

    assert(wm_board_scene_open(board, current_day));
    wm_board_scene_advance(board, 40.0f);
    assert(wm_board_scene_phase(board) == WM_BOARD_READY);
    assert(wm_board_scene_activate(board,
        (WmBoardHit){WM_BOARD_CONTROL_NEXT, SIZE_MAX}));
    wm_board_scene_advance(board, 20.0f);
    WmBoardDate selected = wm_board_scene_date(board);
    assert(selected.year == 2026 && selected.month == 9 &&
           selected.day == 26);
    assert(!wm_board_scene_refresh_today(board, previous_day));
    assert(wm_board_scene_today_count(board) == 2);
    assert(wm_board_scene_today_unread_count(board) == 1);
    selected = wm_board_scene_date(board);
    assert(selected.day == 26);

    wm_board_scene_reset(board);
    assert(wm_board_scene_refresh_today(board, previous_day));
    assert(wm_board_scene_today_count(board) == 1);
    wm_board_scene_destroy(board);
}

static void test_state(WmBoardScene *board) {
    WmBoardMemo memos[11] = {0};
    char ids[11][8];
    for (size_t index = 0; index < 11; index++) {
        snprintf(ids[index], sizeof(ids[index]), "memo-%zu", index);
        memos[index] = (WmBoardMemo){
            .id = ids[index],
            .text = "Example memo",
            .date = {2026, 9, 25},
            .has_position = true,
            .x = (float)index * 8.0f,
            .y = 53.0f
        };
    }
    assert(wm_board_scene_set_memos(board, memos, 11));
    wm_board_scene_set_grid_page(board, 2);
    assert(wm_board_scene_open(board, (WmBoardDate){2026, 9, 25}));
    assert(!wm_board_scene_sd_visible(board));
    assert(wm_board_scene_memo_page_count(board) == 2);
    assert(wm_board_scene_today_count(board) == 11);
    float grid_frame = 0;
    assert(wm_board_scene_grid_overlay(board, &grid_frame));
    assert(fabsf(grid_frame - 70.0f) < 0.001f);
    wm_board_scene_advance(board, 19.0f);
    assert(wm_board_scene_grid_overlay(board, &grid_frame));
    assert(fabsf(grid_frame - 89.0f) < 0.001f);
    wm_board_scene_advance(board, 1.0f);
    assert(!wm_board_scene_grid_overlay(board, &grid_frame));
    wm_board_scene_advance(board, 20.0f);
    assert(wm_board_scene_phase(board) == WM_BOARD_READY);
    assert(wm_board_scene_hit(board, 582, 380).control ==
           WM_BOARD_CONTROL_BACK);
    assert(wm_board_scene_hit(board, 55, 380).control ==
           WM_BOARD_CONTROL_CALENDAR);
    assert(wm_board_scene_hit(board, 112, 380).control ==
           WM_BOARD_CONTROL_CREATE);
    assert(wm_board_scene_hit(board, 20, 175).control ==
           WM_BOARD_CONTROL_PREVIOUS);
    assert(wm_board_scene_hit(board, 615, 175).control ==
           WM_BOARD_CONTROL_NEXT);

    assert(wm_board_scene_activate(board,
        (WmBoardHit){WM_BOARD_CONTROL_PREVIOUS, SIZE_MAX}));
    assert(wm_board_scene_phase(board) == WM_BOARD_MEMO_PAGE);
    assert(!wm_board_scene_activate(board,
        (WmBoardHit){WM_BOARD_CONTROL_PREVIOUS, SIZE_MAX}));
    wm_board_scene_advance(board, 15.0f);
    assert(wm_board_scene_memo_page(board) == 1);
    assert(wm_board_scene_activate(board,
        (WmBoardHit){WM_BOARD_CONTROL_NEXT, SIZE_MAX}));
    wm_board_scene_advance(board, 15.0f);
    assert(wm_board_scene_memo_page(board) == 0);

    assert(wm_board_scene_activate(board,
        (WmBoardHit){WM_BOARD_CONTROL_NEXT, SIZE_MAX}));
    wm_board_scene_advance(board, 20.0f);
    WmBoardDate date = wm_board_scene_date(board);
    assert(date.year == 2026 && date.month == 9 && date.day == 26);
    assert(wm_board_scene_memo_page_count(board) == 1);
    assert(wm_board_scene_today_count(board) == 11);

    assert(wm_board_scene_activate(board,
        (WmBoardHit){WM_BOARD_CONTROL_CALENDAR, SIZE_MAX}));
    assert(wm_board_scene_child(board) == WM_BOARD_CHILD_CALENDAR);
    wm_board_scene_advance(board, 50.0f);
    assert(wm_board_scene_back(board));
    wm_board_scene_advance(board, 50.0f);
    assert(wm_board_scene_child(board) == WM_BOARD_CHILD_NONE);
    assert(wm_board_scene_take_action(board, NULL) ==
           WM_BOARD_ACTION_NONE);
    assert(wm_board_scene_back(board));
    assert(wm_board_scene_grid_overlay(board, &grid_frame));
    assert(fabsf(grid_frame - 100.0f) < 0.001f);
    wm_board_scene_advance(board, 39.0f);
    assert(wm_board_scene_sd_visible(board));
    assert(wm_board_scene_take_action(board, NULL) ==
           WM_BOARD_ACTION_NONE);
    wm_board_scene_advance(board, 1.0f);
    assert(wm_board_scene_phase(board) == WM_BOARD_CLOSED);
    assert(wm_board_scene_take_action(board, NULL) ==
           WM_BOARD_ACTION_EXITED);

    assert(wm_board_scene_open(board, (WmBoardDate){2026, 9, 25}));
    wm_board_scene_advance(board, 40.0f);
    assert(wm_board_scene_activate(board,
        (WmBoardHit){WM_BOARD_CONTROL_CREATE, SIZE_MAX}));
    assert(wm_board_scene_child(board) == WM_BOARD_CHILD_COMPOSE);
    assert(!wm_board_scene_compose_editor_active(board));
    wm_board_scene_advance(board, 39.0f);
    assert(wm_board_scene_activate(board,
        (WmBoardHit){WM_BOARD_CONTROL_COMPOSE_MEMO, SIZE_MAX}));
    wm_board_scene_advance(board, 26.0f);
    assert(wm_board_scene_insert_text(board, "A local memo"));
    assert(!wm_board_scene_compose_editor_active(board));
    wm_board_scene_advance(board, 30.0f);
    assert(wm_board_scene_compose_editor_active(board));
    assert(wm_board_scene_finish_edit(board));
    assert(!wm_board_scene_compose_editor_active(board));
    wm_board_scene_advance(board, 30.0f);
    assert(wm_board_scene_activate(board,
        (WmBoardHit){WM_BOARD_CONTROL_COMPOSE_POST, SIZE_MAX}));
    wm_board_scene_advance(board, 20.0f);
    wm_board_scene_advance(board, 51.0f);
    size_t posted_index = SIZE_MAX;
    assert(wm_board_scene_take_action(board, &posted_index) ==
           WM_BOARD_ACTION_MEMO_POSTED);
    assert(posted_index == 11);
    WmBoardMemo posted;
    assert(wm_board_scene_get_memo(board, posted_index, &posted));
    assert(strcmp(posted.text, "A local memo") == 0);
    assert(posted.created_at_ms > 0);
    wm_board_scene_advance(board, 21.0f);
    assert(wm_board_scene_child(board) == WM_BOARD_CHILD_NONE);
}

static void test_calendar(int argc, char **argv) {
    const char *assets = argc > 1 ? argv[1] : ".local/native-assets";
    WmBoardCalendar *calendar = wm_board_calendar_create(
        (WmPlatform *)1, assets, (WmTextureCache *)1, (WmFontCache *)1);
    assert(calendar);
    assert(wm_board_calendar_open(calendar, (WmBoardDate){2026, 9, 25},
                                   (WmBoardDate){2026, 9, 25}));
    assert(!wm_board_calendar_activate(calendar,
        (WmBoardCalendarHit){WM_CALENDAR_CONTROL_PREVIOUS, 0}));
    wm_board_calendar_advance(calendar, 39.0f);
    assert(wm_board_calendar_hit(calendar, 182, 91).control ==
           WM_CALENDAR_CONTROL_NONE);
    wm_board_calendar_advance(calendar, 1.0f);
    assert(wm_board_calendar_phase(calendar) == WM_CALENDAR_IDLE);
    /* The authored 66-unit 16:9 button spans about 51 of the 640 raster
     * pixels. This edge hit catches an omitted IPL root scale. */
    WmBoardCalendarHit edge = wm_board_calendar_hit(calendar, 182, 91);
    assert(edge.control == WM_CALENDAR_CONTROL_DAY && edge.day_index == 0);
    assert(!wm_board_calendar_activate(calendar,
        (WmBoardCalendarHit){WM_CALENDAR_CONTROL_BACK, 0}));
    assert(!wm_board_calendar_activate(calendar,
        (WmBoardCalendarHit){WM_CALENDAR_CONTROL_PREVIOUS, 0}));
    assert(!wm_board_calendar_back(calendar));
    wm_board_calendar_hover(calendar, edge);
    WmBoardCalendarDayPresentation early_day;
    assert(wm_board_calendar_day_presentation(calendar, 34, &early_day));
    assert(early_day.day_index == 0 && early_day.focus_frame == 0.0f);
    wm_board_calendar_advance(calendar, 1.0f);
    assert(wm_board_calendar_day_presentation(calendar, 34, &early_day));
    assert(early_day.day_index == 0 && early_day.focus_frame == 1.0f);
    wm_board_calendar_advance(calendar, 9.0f);
    edge = wm_board_calendar_hit(calendar, 182, 91);
    assert(edge.control == WM_CALENDAR_CONTROL_DAY && edge.day_index == 0);
    assert(wm_board_calendar_activate(calendar,
        (WmBoardCalendarHit){WM_CALENDAR_CONTROL_PREVIOUS, 0}));
    wm_board_calendar_advance(calendar, 30.0f);
    WmBoardDate month = wm_board_calendar_month(calendar);
    assert(month.year == 2026 && month.month == 8);
    assert(wm_board_calendar_activate(calendar,
        (WmBoardCalendarHit){WM_CALENDAR_CONTROL_NEXT, 0}));
    wm_board_calendar_advance(calendar, 30.0f);
    month = wm_board_calendar_month(calendar);
    assert(month.year == 2026 && month.month == 9);
    assert(wm_board_calendar_activate(calendar,
        (WmBoardCalendarHit){WM_CALENDAR_CONTROL_DAY, 2}));
    wm_board_calendar_advance(calendar, 30.0f);
    assert(wm_board_calendar_phase(calendar) == WM_CALENDAR_EXIT);
    wm_board_calendar_advance(calendar, 50.0f);
    WmBoardDate selected;
    assert(wm_board_calendar_take_outcome(calendar, &selected) ==
           WM_CALENDAR_OUTCOME_SELECTED);
    assert(selected.year == 2026 && selected.month == 9 && selected.day == 1);
    assert(wm_board_calendar_take_outcome(calendar, NULL) ==
           WM_CALENDAR_OUTCOME_NONE);
    wm_board_calendar_reset(calendar);
    assert(wm_board_calendar_open(calendar, (WmBoardDate){2026, 9, 25},
                                   (WmBoardDate){2026, 9, 25}));
    wm_board_calendar_advance(calendar, 40.0f);
    assert(wm_board_calendar_activate(calendar,
        (WmBoardCalendarHit){WM_CALENDAR_CONTROL_DAY, 0}));
    assert(wm_board_calendar_phase(calendar) == WM_CALENDAR_SELECT);
    wm_board_calendar_destroy(calendar);
}

static void test_calendar_hover_order(int argc, char **argv) {
    const char *assets = argc > 1 ? argv[1] : ".local/native-assets";
    WmBoardCalendar *calendar = wm_board_calendar_create(
        (WmPlatform *)1, assets, (WmTextureCache *)1, (WmFontCache *)1);
    assert(calendar);
    assert(wm_board_calendar_open(calendar, (WmBoardDate){2026, 9, 25},
                                   (WmBoardDate){2026, 9, 25}));
    wm_board_calendar_advance(calendar, 50.0f);

    /* The WAD scales N_CalDay_r but leaves sibling B_Cal hit bounds fixed.
     * Capture the presentation order and authored focus frames directly. */
    WmBoardCalendarDayPresentation day;
    wm_board_calendar_hover(calendar,
                            (WmBoardCalendarHit){WM_CALENDAR_CONTROL_DAY, 2});
    assert(wm_board_calendar_day_presentation(calendar, 34, &day));
    assert(day.day_index == 2 && day.focus_frame == 0.0f);
    wm_board_calendar_advance(calendar, 3.0f);
    assert(wm_board_calendar_day_presentation(calendar, 34, &day));
    assert(day.day_index == 2 && day.focus_frame == 3.0f);
    wm_board_calendar_hover(calendar,
                            (WmBoardCalendarHit){WM_CALENDAR_CONTROL_DAY, 3});
    assert(wm_board_calendar_day_presentation(calendar, 33, &day));
    assert(day.day_index == 2 && day.focus_frame == 3.0f);
    assert(wm_board_calendar_day_presentation(calendar, 34, &day));
    assert(day.day_index == 3 && day.focus_frame == 0.0f);
    wm_board_calendar_advance(calendar, 3.0f);
    assert(wm_board_calendar_day_presentation(calendar, 33, &day));
    assert(day.day_index == 2 && day.focus_frame == 10.0f);
    assert(wm_board_calendar_day_presentation(calendar, 34, &day));
    assert(day.day_index == 3 && day.focus_frame == 3.0f);
    wm_board_calendar_hover(calendar,
                            (WmBoardCalendarHit){WM_CALENDAR_CONTROL_NONE, 0});
    assert(wm_board_calendar_day_presentation(calendar, 34, &day));
    assert(day.day_index == 3 && day.focus_frame == 3.0f);
    wm_board_calendar_advance(calendar, 3.0f);
    assert(wm_board_calendar_day_presentation(calendar, 34, &day));
    assert(day.day_index == 3 && day.focus_frame == 10.0f);
    wm_board_calendar_advance(calendar, 8.0f);
    assert(wm_board_calendar_day_presentation(calendar, 34, &day));
    assert(day.day_index == 3 && day.focus_frame == 18.0f);
    assert(wm_board_calendar_day_presentation(calendar, 33, &day));
    assert(day.day_index == 2 && day.focus_frame == 18.0f);

    wm_board_calendar_hover(calendar,
                            (WmBoardCalendarHit){WM_CALENDAR_CONTROL_DAY, 4});
    wm_board_calendar_advance(calendar, 2.0f);
    wm_board_calendar_hover(calendar,
                            (WmBoardCalendarHit){WM_CALENDAR_CONTROL_NONE, 0});
    wm_board_calendar_advance(calendar, 4.0f);
    assert(wm_board_calendar_day_presentation(calendar, 34, &day));
    assert(day.day_index == 4 && day.focus_frame == 10.0f);
    wm_board_calendar_advance(calendar, 3.0f);
    wm_board_calendar_hover(calendar,
                            (WmBoardCalendarHit){WM_CALENDAR_CONTROL_DAY, 4});
    wm_board_calendar_advance(calendar, 5.0f);
    assert(wm_board_calendar_day_presentation(calendar, 34, &day));
    assert(day.day_index == 4 && day.focus_frame == 0.0f);
    wm_board_calendar_advance(calendar, 6.0f);
    assert(wm_board_calendar_day_presentation(calendar, 34, &day));
    assert(day.day_index == 4 && day.focus_frame == 6.0f);

    assert(wm_board_calendar_activate(calendar,
        (WmBoardCalendarHit){WM_CALENDAR_CONTROL_NEXT, 0}));
    assert(!wm_board_calendar_day_presentation(calendar, 34, &day));
    wm_board_calendar_advance(calendar, 30.0f);
    assert(wm_board_calendar_day_presentation(calendar, 34, &day));
    assert(day.day_index == 34 && day.focus_frame == 0.0f);
    wm_board_calendar_destroy(calendar);
}

static void test_calendar_same_tile_motion(int argc, char **argv) {
    const char *assets = argc > 1 ? argv[1] : ".local/native-assets";
    WmBoardCalendar *calendar = wm_board_calendar_create(
        (WmPlatform *)1, assets, (WmTextureCache *)1, (WmFontCache *)1);
    assert(calendar);
    assert(wm_board_calendar_open(calendar, (WmBoardDate){2026, 9, 25},
                                   (WmBoardDate){2026, 9, 25}));
    wm_board_calendar_advance(calendar, 50.0f);

    /* WAD B_Cal remains fixed while N_CalDay_r grows. A small motion across
     * the middle of September 14 must keep date-15 focused continuously. */
    static const int points[][2] = {
        {208, 195}, {212, 195}, {216, 195},
        {216, 200}, {212, 200}, {208, 200}
    };
    for (unsigned step = 0; step < sizeof(points) / sizeof(points[0]);
         step++) {
        WmBoardCalendarHit hit = wm_board_calendar_hit(
            calendar, points[step][0], points[step][1]);
        assert(hit.control == WM_CALENDAR_CONTROL_DAY &&
               hit.day_index == 15);
        wm_board_calendar_hover(calendar, hit);
        wm_board_calendar_advance(calendar, 1.0f);
        WmBoardCalendarDayPresentation day;
        assert(wm_board_calendar_day_presentation(calendar, 34, &day));
        assert(day.day_index == 15 && day.focus_frame == (float)(step + 1));
    }
    wm_board_calendar_destroy(calendar);
}

static void test_calendar_hover_boundary_stability(int argc, char **argv) {
    const char *assets = argc > 1 ? argv[1] : ".local/native-assets";
    WmBoardCalendar *calendar = wm_board_calendar_create(
        (WmPlatform *)1, assets, (WmTextureCache *)1, (WmFontCache *)1);
    assert(calendar);
    assert(wm_board_calendar_open(calendar, (WmBoardDate){2026, 9, 25},
                                   (WmBoardDate){2026, 9, 25}));
    wm_board_calendar_advance(calendar, 50.0f);
    unsigned tested = 0;
    unsigned unstable = 0;
    for (int y = 170; y <= 225; y += 3) {
        for (int x = 175; x <= 245; x += 3) {
            WmBoardCalendarHit initial = wm_board_calendar_hit(calendar, x, y);
            if (initial.control != WM_CALENDAR_CONTROL_DAY ||
                initial.day_index != 15) continue;
            tested++;
            wm_board_calendar_hover(calendar, initial);
            for (unsigned frame = 0; frame < 8; frame++) {
                wm_board_calendar_advance(calendar, 1.0f);
                WmBoardCalendarHit next = wm_board_calendar_hit(calendar, x, y);
                if (next.control != initial.control ||
                    next.day_index != initial.day_index) {
                    if (unstable < 8) {
                        fprintf(stderr, "Calendar hover changed at (%d,%d), "
                                "frame %u: %d/%u\n", x, y, frame + 1,
                                next.control, next.day_index);
                    }
                    unstable++;
                    break;
                }
            }
            wm_board_calendar_hover(calendar,
                (WmBoardCalendarHit){WM_CALENDAR_CONTROL_NONE, 0});
            wm_board_calendar_advance(calendar, 16.0f);
        }
    }
    assert(tested > 20);
    assert(unstable == 0);

    /* The W_Cal tile extends three raster pixels past B_Cal on this row.
     * Crossing that visible rim must keep the same hover owner, while a
     * genuine departure beyond it can release the date. */
    int last_hit = -1;
    for (int x = 150; x < 270; x++) {
        WmBoardCalendarHit hit = wm_board_calendar_hit(calendar, x, 195);
        if (hit.control == WM_CALENDAR_CONTROL_DAY && hit.day_index == 15) {
            last_hit = x;
        }
    }
    assert(last_hit > 0);
    WmBoardCalendarHit day = wm_board_calendar_hit(calendar,
                                                    last_hit - 2, 195);
    assert(day.control == WM_CALENDAR_CONTROL_DAY && day.day_index == 15);
    wm_board_calendar_hover(calendar, day);
    for (int x = last_hit - 1; x <= last_hit + 3; x++) {
        wm_board_calendar_advance(calendar, 1.0f);
        WmBoardCalendarHit retained = wm_board_calendar_hit(calendar, x, 195);
        assert(retained.control == WM_CALENDAR_CONTROL_DAY &&
               retained.day_index == 15);
        wm_board_calendar_hover(calendar, retained);
    }
    WmBoardCalendarHit outside = wm_board_calendar_hit(calendar,
                                                        last_hit + 4, 195);
    assert(outside.control != WM_CALENDAR_CONTROL_DAY ||
           outside.day_index != 15);

    wm_board_calendar_hover(calendar,
        (WmBoardCalendarHit){WM_CALENDAR_CONTROL_NONE, 0});
    wm_board_calendar_advance(calendar, 16.0f);
    int first_row = 1000;
    for (int y = 150; y < 250; y++) {
        WmBoardCalendarHit hit = wm_board_calendar_hit(calendar, 212, y);
        if (hit.control == WM_CALENDAR_CONTROL_DAY && hit.day_index == 15 &&
            y < first_row) first_row = y;
    }
    assert(first_row < 250);
    day = wm_board_calendar_hit(calendar, 212, first_row + 2);
    assert(day.control == WM_CALENDAR_CONTROL_DAY && day.day_index == 15);
    wm_board_calendar_hover(calendar, day);
    for (int y = first_row + 1; y >= first_row - 3; y--) {
        wm_board_calendar_advance(calendar, 1.0f);
        WmBoardCalendarHit retained = wm_board_calendar_hit(calendar, 212, y);
        assert(retained.control == WM_CALENDAR_CONTROL_DAY &&
               retained.day_index == 15);
        wm_board_calendar_hover(calendar, retained);
    }
    outside = wm_board_calendar_hit(calendar, 212, first_row - 4);
    assert(outside.control != WM_CALENDAR_CONTROL_DAY ||
           outside.day_index != 15);
    wm_board_calendar_destroy(calendar);
}

static void test_compose(int argc, char **argv) {
    const char *assets = argc > 1 ? argv[1] : ".local/native-assets";
    WmBoardCompose *compose = wm_board_compose_create(
        (WmPlatform *)1, assets, (WmTextureCache *)1, test_fonts);
    assert(compose);
    assert(wm_board_compose_open(compose));
    assert(!wm_board_compose_activate(compose, WM_COMPOSE_CONTROL_MEMO));
    wm_board_compose_advance(compose, 39.0f);
    assert(wm_board_compose_phase(compose) == WM_COMPOSE_SELECTOR);
    assert(wm_board_compose_activate(compose, WM_COMPOSE_CONTROL_MEMO));
    wm_board_compose_advance(compose, 26.0f);
    assert(wm_board_compose_phase(compose) == WM_COMPOSE_MEMO);
    assert(!wm_board_compose_activate(compose, WM_COMPOSE_CONTROL_POST));
    assert(wm_board_compose_insert_text(compose, "Hello"));
    wm_board_compose_advance(compose, 30.0f);
    assert(wm_board_compose_phase(compose) == WM_COMPOSE_EDIT);
    assert(wm_board_compose_backspace(compose));
    assert(strcmp(wm_board_compose_text(compose), "Hell") == 0);
    assert(wm_board_compose_finish_edit(compose));
    wm_board_compose_advance(compose, 30.0f);
    assert(wm_board_compose_activate(compose, WM_COMPOSE_CONTROL_POST));
    wm_board_compose_advance(compose, 20.0f);
    assert(wm_board_compose_phase(compose) == WM_COMPOSE_SEND);
    wm_board_compose_advance(compose, 51.0f);
    assert(wm_board_compose_take_outcome(compose) ==
           WM_COMPOSE_OUTCOME_POSTED);
    assert(strcmp(wm_board_compose_text(compose), "Hell") == 0);
    wm_board_compose_advance(compose, 21.0f);
    assert(wm_board_compose_take_outcome(compose) ==
           WM_COMPOSE_OUTCOME_CLOSED);
    wm_board_compose_destroy(compose);
}

static void test_compose_network_dialog(int argc, char **argv) {
    const char *assets = argc > 1 ? argv[1] : ".local/native-assets";
    WmTextureCache *textures = wm_texture_cache_create(
        (WmPlatform *)1, assets, 64u * 1024u * 1024u);
    assert(textures);
    WmBoardCompose *compose = wm_board_compose_create(
        (WmPlatform *)1, assets, textures, test_fonts);
    assert(compose && wm_board_compose_open(compose));
    wm_board_compose_advance(compose, 39.0f);
    assert(wm_board_compose_activate(compose, WM_COMPOSE_CONTROL_LETTER));
    assert(strcmp(wm_board_compose_take_key_cue(compose),
                  "WIPL_SE_DECIDE") == 0);
    assert(strcmp(wm_board_compose_take_key_cue(compose),
                  "WIPL_SE_INFO_WINDOW") == 0);
    assert(!wm_board_compose_activate(compose,
        WM_COMPOSE_CONTROL_NETWORK_QUIT));
    wm_board_compose_advance(compose, 24.0f);
    assert(!wm_board_compose_activate(compose,
        WM_COMPOSE_CONTROL_NETWORK_SETTINGS));
    wm_board_compose_advance(compose, 1.0f);
    wm_board_compose_draw(compose);

    char path[4096];
    int length = snprintf(path, sizeof(path),
        "%s/layouts/dlgWdw/my_DialogWindow_a2.json", assets);
    assert(length > 0 && length < (int)sizeof(path));
    char error[160] = {0};
    WmLayout *dialog = wm_layout_load_json(path, error, sizeof(error));
    assert(dialog);
    WmLayoutClip entry = {
        .animation = "my_DialogWindow_a2_DialogIn",
        .group = "G_InOut", .frame = 25.0f
    };
    assert(wm_layout_pose(dialog, &entry, 1));
    WmSourceRect quit;
    WmSourceRect settings;
    assert(wm_source_pane_rect(dialog, "B_BtnA", true,
        WM_LAYOUT_IPL, NULL, &quit));
    assert(wm_source_pane_rect(dialog, "B_BtnB", true,
        WM_LAYOUT_IPL, NULL, &settings));
    int quit_x = (int)(quit.x + quit.width * 0.5f);
    int quit_y = (int)(quit.y + quit.height * 0.5f);
    int settings_x = (int)(settings.x + settings.width * 0.5f);
    int settings_y = (int)(settings.y + settings.height * 0.5f);
    assert(wm_board_compose_hit(compose, quit_x, quit_y) ==
           WM_COMPOSE_CONTROL_NETWORK_QUIT);
    assert(wm_board_compose_hit(compose, settings_x, settings_y) ==
           WM_COMPOSE_CONTROL_NETWORK_SETTINGS);
    wm_board_compose_hover(compose, WM_COMPOSE_CONTROL_NETWORK_QUIT);
    assert(strcmp(wm_board_compose_take_key_cue(compose),
                  "WIPL_SE_BT_TARGETTING") == 0);
    wm_board_compose_hover(compose, WM_COMPOSE_CONTROL_NETWORK_QUIT);
    assert(!wm_board_compose_take_key_cue(compose));
    assert(wm_board_compose_activate(compose,
        WM_COMPOSE_CONTROL_NETWORK_QUIT));
    assert(strcmp(wm_board_compose_take_key_cue(compose),
                  "WIPL_SE_CANCEL") == 0);
    wm_board_compose_advance(compose, 41.0f);
    assert(!wm_board_compose_activate(compose,
        WM_COMPOSE_CONTROL_NETWORK_SETTINGS));
    wm_board_compose_advance(compose, 1.0f);
    assert(wm_board_compose_take_outcome(compose) ==
           WM_COMPOSE_OUTCOME_NONE);
    assert(wm_board_compose_phase(compose) == WM_COMPOSE_SELECTOR);
    assert(wm_board_compose_activate(compose, WM_COMPOSE_CONTROL_LETTER));
    assert(strcmp(wm_board_compose_take_key_cue(compose),
                  "WIPL_SE_DECIDE") == 0);
    assert(strcmp(wm_board_compose_take_key_cue(compose),
                  "WIPL_SE_INFO_WINDOW") == 0);
    wm_board_compose_advance(compose, 25.0f);
    wm_board_compose_draw(compose);
    assert(wm_board_compose_hit(compose, settings_x, settings_y) ==
           WM_COMPOSE_CONTROL_NETWORK_SETTINGS);
    assert(wm_board_compose_activate(compose,
        WM_COMPOSE_CONTROL_NETWORK_SETTINGS));
    assert(strcmp(wm_board_compose_take_key_cue(compose),
                  "WIPL_SE_DECIDE") == 0);
    wm_board_compose_advance(compose, 42.0f);
    assert(wm_board_compose_take_outcome(compose) ==
           WM_COMPOSE_OUTCOME_OPEN_SETTINGS);
    assert(wm_board_compose_take_outcome(compose) ==
           WM_COMPOSE_OUTCOME_NONE);
    wm_layout_destroy(dialog);
    wm_board_compose_destroy(compose);
    wm_texture_cache_destroy(textures);
}

static void test_board_network_settings_action(int argc, char **argv) {
    WmBoardScene *board = load_board(argc, argv);
    assert(board);
    assert(wm_board_scene_open(board, (WmBoardDate){2026, 9, 25}));
    wm_board_scene_advance(board, 40.0f);
    assert(wm_board_scene_activate(board,
        (WmBoardHit){WM_BOARD_CONTROL_CREATE, SIZE_MAX}));
    wm_board_scene_advance(board, 39.0f);
    assert(wm_board_scene_activate(board,
        (WmBoardHit){WM_BOARD_CONTROL_COMPOSE_LETTER, SIZE_MAX}));
    assert(wm_board_scene_take_action(board, NULL) == WM_BOARD_ACTION_NONE);
    wm_board_scene_advance(board, 25.0f);
    assert(wm_board_scene_activate(board,
        (WmBoardHit){WM_BOARD_CONTROL_COMPOSE_NETWORK_SETTINGS, SIZE_MAX}));
    wm_board_scene_advance(board, 42.0f);
    assert(wm_board_scene_take_action(board, NULL) ==
           WM_BOARD_ACTION_OPEN_SETTINGS);
    assert(wm_board_scene_take_action(board, NULL) == WM_BOARD_ACTION_NONE);
    wm_board_scene_destroy(board);
}

static void test_compose_mii_notice(int argc, char **argv) {
    const char *assets = argc > 1 ? argv[1] : ".local/native-assets";
    char path[4096];
    int length = snprintf(path, sizeof(path),
                          "%s/layouts/sofkeybd/my_Memo_a.json", assets);
    assert(length > 0 && length < (int)sizeof(path));
    char error[160] = {0};
    WmLayout *layout = wm_layout_load_json(path, error, sizeof(error));
    assert(layout);
    WmLayoutClip entry = {
        .animation = "my_Memo_a_MailIn", .frame = 16.0f
    };
    assert(wm_layout_pose(layout, &entry, 1));
    WmSourceRect icon;
    assert(wm_source_pane_rect(layout, "B_Nigaoe", true, WM_LAYOUT_IPL,
                               NULL, &icon));

    WmBoardCompose *compose = wm_board_compose_create(
        (WmPlatform *)1, assets, (WmTextureCache *)1, test_fonts);
    assert(compose && wm_board_compose_open(compose));
    wm_board_compose_advance(compose, 39.0f);
    assert(wm_board_compose_activate(compose, WM_COMPOSE_CONTROL_MEMO));
    wm_board_compose_advance(compose, 26.0f);
    int x = (int)(icon.x + icon.width * 0.5f);
    int y = (int)(icon.y + icon.height * 0.5f);
    assert(wm_board_compose_hit(compose, x, y) ==
           WM_COMPOSE_CONTROL_MII);
    wm_board_compose_hover(compose, WM_COMPOSE_CONTROL_MII);
    wm_board_compose_advance(compose, 3.0f);
    assert(wm_board_compose_activate(compose, WM_COMPOSE_CONTROL_MII));
    assert(strcmp(wm_board_compose_take_key_cue(compose),
                  "WIPL_SE_INFO_WINDOW") == 0);
    wm_board_compose_advance(compose, 25.0f);
    assert(wm_board_compose_activate(compose,
                                    WM_COMPOSE_CONTROL_ADDRESS_DIALOG_OK));
    wm_board_compose_advance(compose, 38.0f);
    assert(wm_board_compose_phase(compose) == WM_COMPOSE_MEMO);
    assert(wm_board_compose_hit(compose, x, y) ==
           WM_COMPOSE_CONTROL_MII);
    wm_board_compose_destroy(compose);
    wm_layout_destroy(layout);
}

static void test_compose_software_keyboard(int argc, char **argv) {
    const char *assets = argc > 1 ? argv[1] : ".local/native-assets";
    char path[4096];
    int length = snprintf(path, sizeof(path),
                          "%s/layouts/sofkeybd/fs_VK_ascii_keytop_a.json",
                          assets);
    assert(length > 0 && length < (int)sizeof(path));
    char error[160] = {0};
    WmLayout *keytop = wm_layout_load_json(path, error, sizeof(error));
    assert(keytop);
    WmSourceRect q;
    WmSourceRect shift;
    WmSourceRect space;
    assert(wm_source_pane_rect(keytop, "B_key_11", true, WM_LAYOUT_IPL,
                                NULL, &q));
    assert(wm_source_pane_rect(keytop, "B_key_SHIFT", true, WM_LAYOUT_IPL,
                                NULL, &shift));
    assert(wm_source_pane_rect(keytop, "B_key_SPACE", true, WM_LAYOUT_IPL,
                                NULL, &space));
    wm_layout_destroy(keytop);

    length = snprintf(path, sizeof(path),
                      "%s/layouts/sofkeybd/fs_VK_toolbar_a.json", assets);
    assert(length > 0 && length < (int)sizeof(path));
    WmLayout *toolbar = wm_layout_load_json(path, error, sizeof(error));
    assert(toolbar);
    WmSourceRect ok;
    WmSourceRect back;
    assert(wm_source_pane_rect(toolbar, "B_BT_confirm", true, WM_LAYOUT_IPL,
                                NULL, &ok));
    assert(wm_source_pane_rect(toolbar, "B_BT_cancel", true, WM_LAYOUT_IPL,
                                NULL, &back));
    wm_layout_destroy(toolbar);

    WmBoardCompose *compose = wm_board_compose_create(
        (WmPlatform *)1, assets, (WmTextureCache *)1, test_fonts);
    assert(compose);
    assert(wm_board_compose_open(compose));
    wm_board_compose_advance(compose, 39.0f);
    assert(wm_board_compose_activate(compose, WM_COMPOSE_CONTROL_MEMO));
    wm_board_compose_advance(compose, 26.0f);
    assert(wm_board_compose_activate(compose, WM_COMPOSE_CONTROL_EDIT));
    assert(strcmp(wm_board_compose_take_key_cue(compose),
                  "WIPL_SE_SK_OPEN") == 0);
    assert(wm_board_compose_take_key_cue(compose) == NULL);
    wm_board_compose_advance(compose, 29.0f);
    assert(wm_board_compose_phase(compose) == WM_COMPOSE_ENTER_EDIT);
    assert(wm_board_compose_hit(compose, (int)(q.x + q.width * 0.5f),
                                 (int)(q.y + q.height * 0.5f)) ==
           WM_COMPOSE_CONTROL_NONE);
    wm_board_compose_advance(compose, 1.0f);
    assert(wm_board_compose_phase(compose) == WM_COMPOSE_EDIT);
    WmBoardComposeScrollState empty_scroll;
    assert(wm_board_compose_scroll_state(compose, &empty_scroll));
    assert(!empty_scroll.up_target_visible &&
           !empty_scroll.down_target_visible);

    WmBoardComposeControl q_control = (WmBoardComposeControl)(
        WM_COMPOSE_CONTROL_KEY_FIRST + WM_KEYBOARD_CHARACTER_FIRST - 1 + 11);
    WmBoardComposeControl shift_control = (WmBoardComposeControl)(
        WM_COMPOSE_CONTROL_KEY_FIRST + WM_KEYBOARD_SHIFT - 1);
    WmBoardComposeControl space_control = (WmBoardComposeControl)(
        WM_COMPOSE_CONTROL_KEY_FIRST + WM_KEYBOARD_SPACE - 1);
    WmBoardComposeControl delete_control = (WmBoardComposeControl)(
        WM_COMPOSE_CONTROL_KEY_FIRST + WM_KEYBOARD_DELETE - 1);
    WmBoardComposeControl ok_control = (WmBoardComposeControl)(
        WM_COMPOSE_CONTROL_KEY_FIRST + WM_KEYBOARD_OK - 1);
    assert(wm_board_compose_hit(compose, (int)(q.x + q.width * 0.5f),
                                 (int)(q.y + q.height * 0.5f)) == q_control);
    assert(wm_board_compose_activate(compose, q_control));
    assert(strcmp(wm_board_compose_text(compose), "q") == 0);
    assert(strcmp(wm_board_compose_take_key_cue(compose),
                  "WIPL_SE_CHAR_INPUT") == 0);
    assert(wm_board_compose_hit(compose,
        (int)(shift.x + shift.width * 0.5f),
        (int)(shift.y + shift.height * 0.5f)) == shift_control);
    assert(wm_board_compose_activate(compose, shift_control));
    assert(strcmp(wm_board_compose_take_key_cue(compose),
                  "WIPL_SE_SK_SWITCHING_02") == 0);
    assert(wm_board_compose_activate(compose, q_control));
    assert(strcmp(wm_board_compose_text(compose), "qQ") == 0);
    assert(wm_board_compose_activate(compose, q_control));
    assert(strcmp(wm_board_compose_text(compose), "qQq") == 0);
    assert(wm_board_compose_hit(compose,
        (int)(space.x + space.width * 0.5f),
        (int)(space.y + space.height * 0.5f)) == space_control);
    assert(wm_board_compose_activate(compose, space_control));
    assert(strcmp(wm_board_compose_take_key_cue(compose),
                  "WIPL_SE_CHAR_DECIDE") == 0);
    assert(wm_board_compose_activate(compose, delete_control));
    assert(strcmp(wm_board_compose_text(compose), "qQq") == 0);
    assert(strcmp(wm_board_compose_take_key_cue(compose),
                  "WIPL_SE_CHAR_DELETE") == 0);
    assert(wm_board_compose_hit(compose,
        (int)(ok.x + ok.width * 0.5f),
        (int)(ok.y + ok.height * 0.5f)) == ok_control);
    assert(wm_board_compose_activate(compose, ok_control));
    assert(wm_board_compose_phase(compose) == WM_COMPOSE_LEAVE_EDIT);
    assert(strcmp(wm_board_compose_take_key_cue(compose),
                  "WIPL_SE_SK_DECIDE_CLOSE") == 0);
    assert(wm_board_compose_take_key_cue(compose) == NULL);
    wm_board_compose_advance(compose, 15.0f);
    WmBoardComposeScrollState scroll;
    assert(wm_board_compose_scroll_state(compose, &scroll));
    assert(fabsf(scroll.editor_opacity - 0.5f) < 0.01f);
    wm_board_compose_advance(compose, 15.0f);
    assert(wm_board_compose_phase(compose) == WM_COMPOSE_MEMO);
    assert(strcmp(wm_board_compose_text(compose), "qQq") == 0);
    assert(wm_board_compose_activate(compose, WM_COMPOSE_CONTROL_EDIT));
    assert(strcmp(wm_board_compose_take_key_cue(compose),
                  "WIPL_SE_SK_OPEN") == 0);
    wm_board_compose_advance(compose, 30.0f);
    WmBoardComposeControl back_control = (WmBoardComposeControl)(
        WM_COMPOSE_CONTROL_KEY_FIRST + WM_KEYBOARD_BACK - 1);
    assert(wm_board_compose_hit(compose,
        (int)(back.x + back.width * 0.5f),
        (int)(back.y + back.height * 0.5f)) == back_control);
    assert(wm_board_compose_activate(compose, back_control));
    assert(strcmp(wm_board_compose_take_key_cue(compose),
                  "WIPL_SE_SK_CANCEL_CLOSE") == 0);
    assert(wm_board_compose_take_key_cue(compose) == NULL);
    wm_board_compose_advance(compose, 30.0f);
    assert(wm_board_compose_phase(compose) == WM_COMPOSE_MEMO);
    assert(strcmp(wm_board_compose_text(compose), "qQq") == 0);
    wm_board_compose_destroy(compose);
}

static WmBoardCompose *editing_compose(const char *assets) {
    WmBoardCompose *compose = wm_board_compose_create(
        (WmPlatform *)1, assets, (WmTextureCache *)1, test_fonts);
    assert(compose);
    assert(wm_board_compose_open(compose));
    wm_board_compose_advance(compose, 39.0f);
    assert(wm_board_compose_activate(compose, WM_COMPOSE_CONTROL_MEMO));
    wm_board_compose_advance(compose, 26.0f);
    assert(wm_board_compose_activate(compose, WM_COMPOSE_CONTROL_EDIT));
    wm_board_compose_advance(compose, 30.0f);
    assert(wm_board_compose_phase(compose) == WM_COMPOSE_EDIT);
    assert(wm_board_compose_insert_text(compose, "abcdefghijklmnop"));
    return compose;
}

static WmBoardComposeControl compose_key(WmBoardKeyboardControl key) {
    return (WmBoardComposeControl)(WM_COMPOSE_CONTROL_KEY_FIRST + key - 1);
}

static void test_compose_phone_keyboard(int argc, char **argv) {
    const char *assets = argc > 1 ? argv[1] : ".local/native-assets";
    char path[4096];
    char error[160] = {0};
    int length = snprintf(path, sizeof(path),
                          "%s/layouts/sofkeybd/fs_VK_cellPhone_a.json",
                          assets);
    assert(length > 0 && length < (int)sizeof(path));
    WmLayout *phone = wm_layout_load_json(path, error, sizeof(error));
    assert(phone);
    WmSourceRect key_one;
    WmSourceRect mode_lowercase;
    assert(wm_source_pane_rect(phone, "B_CPkey_01", true, WM_LAYOUT_IPL,
                               NULL, &key_one));
    assert(wm_source_pane_rect(phone, "B_ChngTag_01", true,
                               WM_LAYOUT_IPL, NULL, &mode_lowercase));
    wm_layout_destroy(phone);

    length = snprintf(path, sizeof(path),
                      "%s/layouts/sofkeybd/fs_VK_toolbar_a.json", assets);
    assert(length > 0 && length < (int)sizeof(path));
    WmLayout *toolbar = wm_layout_load_json(path, error, sizeof(error));
    assert(toolbar);
    WmSourceRect phone_tab;
    WmSourceRect qwerty_tab;
    assert(wm_source_pane_rect(toolbar, "B_kyChng_CP", true,
                               WM_LAYOUT_IPL, NULL, &phone_tab));
    assert(wm_source_pane_rect(toolbar, "B_kyChng_QWERTY", true,
                               WM_LAYOUT_IPL, NULL, &qwerty_tab));
    wm_layout_destroy(toolbar);

    WmBoardCompose *compose = wm_board_compose_create(
        (WmPlatform *)1, assets, (WmTextureCache *)1, test_fonts);
    assert(compose);
    assert(wm_board_compose_open(compose));
    wm_board_compose_advance(compose, 39.0f);
    assert(wm_board_compose_activate(compose, WM_COMPOSE_CONTROL_MEMO));
    wm_board_compose_advance(compose, 26.0f);
    assert(wm_board_compose_activate(compose, WM_COMPOSE_CONTROL_EDIT));
    assert(strcmp(wm_board_compose_take_key_cue(compose),
                  "WIPL_SE_SK_OPEN") == 0);
    wm_board_compose_advance(compose, 30.0f);
    assert(wm_board_compose_phase(compose) == WM_COMPOSE_EDIT);

    WmBoardComposeControl phone_control = compose_key(WM_KEYBOARD_PHONE);
    WmBoardComposeControl qwerty_control = compose_key(WM_KEYBOARD_QWERTY);
    WmBoardComposeControl phone_one = (WmBoardComposeControl)(
        compose_key(WM_KEYBOARD_PHONE_FIRST) + 1);
    WmBoardComposeControl phone_two = (WmBoardComposeControl)(phone_one + 1);
    WmBoardComposeControl lower_mode = (WmBoardComposeControl)(
        compose_key(WM_KEYBOARD_PHONE_MODE_FIRST) + 1);
    WmBoardComposeControl upper_mode = (WmBoardComposeControl)(
        compose_key(WM_KEYBOARD_PHONE_MODE_FIRST) + 2);
    WmBoardComposeControl number_mode = (WmBoardComposeControl)(
        compose_key(WM_KEYBOARD_PHONE_MODE_FIRST) + 3);
    assert(wm_board_compose_hit(compose,
        (int)(phone_tab.x + phone_tab.width * 0.5f),
        (int)(phone_tab.y + phone_tab.height * 0.5f)) == phone_control);
    assert(wm_board_compose_activate(compose, phone_control));
    assert(strcmp(wm_board_compose_take_key_cue(compose),
                  "WIPL_SE_SK_SWITCH_TO_KETAI") == 0);
    assert(wm_board_compose_hit(compose,
        (int)(key_one.x + key_one.width * 0.5f),
        (int)(key_one.y + key_one.height * 0.5f)) == phone_one);
    wm_board_compose_hover(compose, phone_one);
    assert(wm_board_compose_activate(compose, phone_one));
    assert(strcmp(wm_board_compose_text(compose), "A") == 0);
    assert(wm_board_compose_activate(compose, phone_one));
    assert(strcmp(wm_board_compose_text(compose), "B") == 0);
    assert(wm_board_compose_activate_secondary(compose, phone_one));
    assert(strcmp(wm_board_compose_text(compose), "A") == 0);
    wm_board_compose_hover(compose, phone_two);
    wm_board_compose_hover(compose, phone_one);
    assert(wm_board_compose_activate(compose, phone_one));
    assert(strcmp(wm_board_compose_text(compose), "Aa") == 0);
    wm_board_compose_advance(compose, 90.0f);
    assert(wm_board_compose_activate(compose, phone_one));
    assert(strcmp(wm_board_compose_text(compose), "Aaa") == 0);

    assert(wm_board_compose_hit(compose,
        (int)(mode_lowercase.x + mode_lowercase.width * 0.5f),
        (int)(mode_lowercase.y + mode_lowercase.height * 0.5f)) == lower_mode);
    assert(wm_board_compose_activate(compose, lower_mode));
    assert(strcmp(wm_board_compose_take_key_cue(compose),
                  "WIPL_SE_SK_SWITCHING_02") == 0);
    assert(wm_board_compose_activate(compose, phone_two));
    assert(strcmp(wm_board_compose_text(compose), "Aaad") == 0);
    assert(wm_board_compose_activate(compose, phone_two));
    assert(strcmp(wm_board_compose_text(compose), "Aaae") == 0);
    assert(wm_board_compose_activate(compose, upper_mode));
    assert(wm_board_compose_activate(compose, phone_one));
    assert(strcmp(wm_board_compose_text(compose), "AaaeA") == 0);
    assert(wm_board_compose_activate(compose, number_mode));
    assert(wm_board_compose_activate(compose, phone_one));
    assert(wm_board_compose_activate_secondary(compose, phone_one));
    assert(strcmp(wm_board_compose_text(compose), "AaaeA22") == 0);
    assert(!wm_board_compose_hold_control(compose,
                                          compose_key(WM_KEYBOARD_SPACE)));
    assert(strcmp(wm_board_compose_text(compose), "AaaeA22") == 0);
    assert(wm_board_compose_hit(compose,
        (int)(qwerty_tab.x + qwerty_tab.width * 0.5f),
        (int)(qwerty_tab.y + qwerty_tab.height * 0.5f)) == qwerty_control);
    assert(wm_board_compose_activate(compose, qwerty_control));
    assert(strcmp(wm_board_compose_take_key_cue(compose),
                  "WIPL_SE_SK_SWITCHING_01") == 0);
    assert(!wm_board_compose_activate_secondary(compose, phone_one));
    wm_board_compose_destroy(compose);
}

static void test_compose_held_keytops(int argc, char **argv) {
    const char *assets = argc > 1 ? argv[1] : ".local/native-assets";
    WmBoardComposeControl deletion = (WmBoardComposeControl)(
        WM_COMPOSE_CONTROL_KEY_FIRST + WM_KEYBOARD_DELETE - 1);
    WmBoardComposeControl space = (WmBoardComposeControl)(
        WM_COMPOSE_CONTROL_KEY_FIRST + WM_KEYBOARD_SPACE - 1);
    WmBoardComposeControl letter = (WmBoardComposeControl)(
        WM_COMPOSE_CONTROL_KEY_FIRST + WM_KEYBOARD_CHARACTER_FIRST - 1);
    WmBoardCompose *compose = editing_compose(assets);

    assert(!wm_board_compose_hold_control(compose, letter));
    assert(strcmp(wm_board_compose_text(compose),
                  "abcdefghijklmnop") == 0);
    wm_board_compose_hover(compose, deletion);
    assert(wm_board_compose_hold_control(compose, deletion));
    assert(strcmp(wm_board_compose_text(compose),
                  "abcdefghijklmno") == 0);
    assert(strcmp(wm_board_compose_take_key_cue(compose),
                  "WIPL_SE_CHAR_DELETE") == 0);
    wm_board_compose_advance(compose, 35.75f);
    assert(strcmp(wm_board_compose_text(compose),
                  "abcdefghijklmno") == 0);
    wm_board_compose_advance(compose, 0.25f);
    assert(strcmp(wm_board_compose_text(compose),
                  "abcdefghijklmn") == 0);
    wm_board_compose_advance(compose, 8.0f);
    assert(strcmp(wm_board_compose_text(compose),
                  "abcdefghijklmn") == 0);
    wm_board_compose_advance(compose, 1.0f);
    assert(strcmp(wm_board_compose_text(compose),
                  "abcdefghijklm") == 0);
    assert(strcmp(wm_board_compose_take_key_cue(compose),
                  "WIPL_SE_CHAR_DELETE") == 0);
    assert(strcmp(wm_board_compose_take_key_cue(compose),
                  "WIPL_SE_CHAR_DELETE") == 0);
    wm_board_compose_hover(compose, WM_COMPOSE_CONTROL_NONE);
    wm_board_compose_advance(compose, 100.0f);
    assert(strcmp(wm_board_compose_text(compose),
                  "abcdefghijklm") == 0);

    wm_board_compose_hover(compose, space);
    assert(wm_board_compose_hold_control(compose, space));
    wm_board_compose_advance(compose, 45.0f);
    assert(strcmp(wm_board_compose_text(compose),
                  "abcdefghijklm   ") == 0);
    wm_board_compose_release_control(compose);
    wm_board_compose_advance(compose, 100.0f);
    assert(strcmp(wm_board_compose_text(compose),
                  "abcdefghijklm   ") == 0);
    wm_board_compose_destroy(compose);

    WmBoardCompose *split = editing_compose(assets);
    WmBoardCompose *bulk = editing_compose(assets);
    assert(wm_board_compose_hold_control(split, deletion));
    assert(wm_board_compose_hold_control(bulk, deletion));
    for (unsigned update = 0; update < 100; update++) {
        wm_board_compose_advance(split, 0.25f);
        wm_board_compose_advance(split, 0.75f);
    }
    wm_board_compose_advance(bulk, 100.0f);
    assert(strcmp(wm_board_compose_text(split), "abcdefg") == 0);
    assert(strcmp(wm_board_compose_text(split),
                  wm_board_compose_text(bulk)) == 0);
    wm_board_compose_destroy(split);
    wm_board_compose_destroy(bulk);
}

static void test_memo_caret_layout(int argc, char **argv) {
    const char *assets = argc > 1 ? argv[1] : ".local/native-assets";
    char path[4096];
    int length = snprintf(path, sizeof(path),
                          "%s/layouts/sofkeybd/my_Memo_a.json", assets);
    assert(length > 0 && length < (int)sizeof(path));
    char error[160] = {0};
    WmLayout *memo = wm_layout_load_json(path, error, sizeof(error));
    assert(memo);
    WmFontPane pane;
    const char *font_name = NULL;
    assert(wm_layout_pane_font(memo, "T_Letter", &pane, &font_name));
    if (!test_fonts) {
        test_fonts = wm_font_cache_create((WmPlatform *)1, assets,
                                           1024u * 1024u);
        assert(test_fonts);
    }
    WmCachedFont *face = wm_font_cache_resolve(test_fonts, font_name);
    assert(face);
    const WmFont *font = wm_cached_font_resource(face);
    assert(font);
    const WmFontTextLayout *layout = wm_font_cache_layout(face, "Wi", &pane);
    assert(layout);
    float first_x, first_y, second_x, second_y;
    assert(wm_font_text_layout_caret(layout, 1, &first_x, &first_y));
    assert(wm_font_text_layout_caret(layout, 2, &second_x, &second_y));
    float size[2] = {pane.font_size[0], pane.font_size[1]};
    float expected_advance = wm_font_text_width(font, "i", size, 0.0f) +
                             pane.char_space;
    assert(fabsf(second_x - first_x - expected_advance) < 0.01f);
    assert(fabsf(second_y - first_y) < 0.01f);

    layout = wm_font_cache_layout(face, "", &pane);
    assert(layout);
    float empty_x, empty_y;
    assert(wm_font_text_layout_caret(layout, 0, &empty_x, &empty_y));
    assert(isfinite(empty_x) && isfinite(empty_y));
    layout = wm_font_cache_layout(face, "W\n", &pane);
    assert(layout && wm_font_text_layout_line_count(layout) == 2);
    float newline_x, newline_y, next_x, next_y;
    assert(wm_font_text_layout_caret(layout, 1,
                                      &newline_x, &newline_y));
    assert(wm_font_text_layout_caret(layout, 2, &next_x, &next_y));
    assert(fabsf(next_x - empty_x) < 0.01f);
    assert(next_y < newline_y);

    pane.size[0] = wm_font_text_width(font, "W", size,
                                       pane.char_space) + 0.1f;
    pane.no_wrap = false;
    layout = wm_font_cache_layout(face, "WW", &pane);
    assert(layout && wm_font_text_layout_line_count(layout) == 2);
    float start_x, start_y, wrapped_x, wrapped_y;
    assert(wm_font_text_layout_caret(layout, 0, &start_x, &start_y));
    assert(wm_font_text_layout_caret(layout, 1, &wrapped_x, &wrapped_y));
    assert(fabsf(start_x - wrapped_x) < 0.01f);
    assert(wrapped_y < start_y);
    wm_layout_destroy(memo);
}

static void test_compose_symbol_pages(int argc, char **argv) {
    const char *assets = argc > 1 ? argv[1] : ".local/native-assets";
    char path[4096];
    int length = snprintf(path, sizeof(path),
                          "%s/layouts/sofkeybd/fs_signWindow_a.json",
                          assets);
    assert(length > 0 && length < (int)sizeof(path));
    char error[160] = {0};
    WmLayout *symbols = wm_layout_load_json(path, error, sizeof(error));
    assert(symbols);
    WmSourceRect first, previous, next, close;
    assert(wm_source_pane_rect(symbols, "B_SGNkey_00", true, WM_LAYOUT_IPL,
                                NULL, &first));
    assert(wm_source_pane_rect(symbols, "B_SGNkey_prev", true, WM_LAYOUT_IPL,
                                NULL, &previous));
    assert(wm_source_pane_rect(symbols, "B_SGNkey_next", true, WM_LAYOUT_IPL,
                                NULL, &next));
    assert(wm_source_pane_rect(symbols, "B_SGNkey_close", true, WM_LAYOUT_IPL,
                                NULL, &close));
    /* The native arrow bindings borrow both pane and material tracks from
     * Close. A pane-only rebind misses the pushed color animation. */
    WmLayoutClip arrow = {
        .animation = "fs_signWindow_a_SGN_Pushed",
        .frame = 3.0f,
        .target_name = "P_SGNkey_close",
        .rebind_name = "P_SGNkey_prev"
    };
    assert(wm_layout_pose(symbols, &arrow, 1));
    WmLayoutPaneState previous_state;
    assert(wm_layout_pane_state(symbols, "P_SGNkey_prev",
                                 &previous_state));
    assert(previous_state.scale[0] > 0.95f);
    bool material_found = false;
    for (size_t index = 0; index < wm_layout_material_count(symbols);
         index++) {
        WmLayoutMaterialInfo material;
        uint8_t wraps[4][2];
        assert(wm_layout_material_info(symbols, index, &material, wraps));
        if (strcmp(material.name, "P_SGNkey_prev") != 0) continue;
        assert(fabsf(material.registers[1][2] - 128.0f / 255.0f) <
               0.01f);
        material_found = true;
    }
    assert(material_found);
    WmLayoutClip settled = {
        .animation = "fs_signWindow_a_SGN_FADE-IN",
        .frame = 18.0f
    };
    assert(wm_layout_pose(symbols, &settled, 1));
    assert(wm_source_pane_rect(symbols, "B_SGNkey_00", true, WM_LAYOUT_IPL,
                                NULL, &first));
    assert(wm_source_pane_rect(symbols, "B_SGNkey_prev", true, WM_LAYOUT_IPL,
                                NULL, &previous));
    assert(wm_source_pane_rect(symbols, "B_SGNkey_next", true, WM_LAYOUT_IPL,
                                NULL, &next));
    assert(wm_source_pane_rect(symbols, "B_SGNkey_close", true, WM_LAYOUT_IPL,
                                NULL, &close));
    wm_layout_destroy(symbols);

    length = snprintf(path, sizeof(path),
                      "%s/layouts/sofkeybd/fs_VK_ascii_keytop_a.json",
                      assets);
    assert(length > 0 && length < (int)sizeof(path));
    WmLayout *keytop = wm_layout_load_json(path, error, sizeof(error));
    assert(keytop);
    WmSourceRect more;
    assert(wm_source_pane_rect(keytop, "B_USEU_Chng_sign", true,
                                WM_LAYOUT_IPL, NULL, &more));
    wm_layout_destroy(keytop);

    WmBoardCompose *compose = wm_board_compose_create(
        (WmPlatform *)1, assets, (WmTextureCache *)1, test_fonts);
    assert(compose);
    assert(wm_board_compose_open(compose));
    wm_board_compose_advance(compose, 39.0f);
    assert(wm_board_compose_activate(compose, WM_COMPOSE_CONTROL_MEMO));
    wm_board_compose_advance(compose, 26.0f);
    assert(wm_board_compose_activate(compose, WM_COMPOSE_CONTROL_EDIT));
    assert(strcmp(wm_board_compose_take_key_cue(compose),
                  "WIPL_SE_SK_OPEN") == 0);
    wm_board_compose_advance(compose, 30.0f);

    WmBoardComposeControl more_control = (WmBoardComposeControl)(
        WM_COMPOSE_CONTROL_KEY_FIRST + WM_KEYBOARD_MORE - 1);
    WmBoardComposeControl symbol_control = (WmBoardComposeControl)(
        WM_COMPOSE_CONTROL_KEY_FIRST + WM_KEYBOARD_SYMBOL_FIRST - 1);
    WmBoardComposeControl previous_control = (WmBoardComposeControl)(
        WM_COMPOSE_CONTROL_KEY_FIRST + WM_KEYBOARD_SYMBOL_PREV - 1);
    WmBoardComposeControl next_control = (WmBoardComposeControl)(
        WM_COMPOSE_CONTROL_KEY_FIRST + WM_KEYBOARD_SYMBOL_NEXT - 1);
    WmBoardComposeControl close_control = (WmBoardComposeControl)(
        WM_COMPOSE_CONTROL_KEY_FIRST + WM_KEYBOARD_SYMBOL_CLOSE - 1);
    assert(wm_board_compose_hit(compose,
        (int)(more.x + more.width * 0.5f),
        (int)(more.y + more.height * 0.5f)) == more_control);
    assert(wm_board_compose_activate(compose, more_control));
    assert(strcmp(wm_board_compose_take_key_cue(compose),
                  "WIPL_SE_SYMBOL_PAGE_OPEN") == 0);
    assert(!wm_board_compose_insert_text(compose, "x"));
    wm_board_compose_advance(compose, 17.0f);
    assert(wm_board_compose_hit(compose,
        (int)(first.x + first.width * 0.5f),
        (int)(first.y + first.height * 0.5f)) ==
           WM_COMPOSE_CONTROL_NONE);
    wm_board_compose_advance(compose, 1.0f);
    assert(wm_board_compose_hit(compose,
        (int)(first.x + first.width * 0.5f),
        (int)(first.y + first.height * 0.5f)) == symbol_control);
    assert(wm_board_compose_activate(compose, symbol_control));
    assert(strcmp(wm_board_compose_text(compose), ".") == 0);
    assert(strcmp(wm_board_compose_take_key_cue(compose),
                  "WIPL_SE_CHAR_INPUT") == 0);
    assert(wm_board_compose_hit(compose,
        (int)(next.x + next.width * 0.5f),
        (int)(next.y + next.height * 0.5f)) == next_control);
    assert(wm_board_compose_activate(compose, next_control));
    assert(strcmp(wm_board_compose_take_key_cue(compose),
                  "WSD_SELECT") == 0);
    wm_board_compose_advance(compose, 19.0f);
    assert(!wm_board_compose_activate(compose, symbol_control));
    wm_board_compose_advance(compose, 1.0f);
    assert(wm_board_compose_activate(compose, symbol_control));
    assert(strcmp(wm_board_compose_text(compose), ".[") == 0);
    assert(wm_board_compose_hit(compose,
        (int)(previous.x + previous.width * 0.5f),
        (int)(previous.y + previous.height * 0.5f)) == previous_control);
    assert(wm_board_compose_activate(compose, previous_control));
    wm_board_compose_advance(compose, 20.0f);
    assert(wm_board_compose_activate(compose, symbol_control));
    assert(strcmp(wm_board_compose_text(compose), ".[.") == 0);
    assert(wm_board_compose_activate(compose, previous_control));
    wm_board_compose_advance(compose, 20.0f);
    assert(wm_board_compose_activate(compose, symbol_control));
    assert(strcmp(wm_board_compose_text(compose), ".[.ΐ") == 0);
    assert(strcmp(wm_board_compose_take_key_cue(compose),
                  "WIPL_SE_CHAR_INPUT") == 0);
    assert(wm_board_compose_backspace(compose) == false);
    assert(wm_board_compose_activate(compose, next_control));
    wm_board_compose_advance(compose, 20.0f);
    assert(wm_board_compose_activate(compose, symbol_control));
    assert(strcmp(wm_board_compose_text(compose), ".[.ΐ.") == 0);
    assert(wm_board_compose_hit(compose,
        (int)(close.x + close.width * 0.5f),
        (int)(close.y + close.height * 0.5f)) == close_control);
    assert(wm_board_compose_activate(compose, close_control));
    assert(strcmp(wm_board_compose_take_key_cue(compose),
                  "WIPL_SE_CHAR_DECIDE") == 0);
    wm_board_compose_advance(compose, 13.0f);
    assert(wm_board_compose_phase(compose) == WM_COMPOSE_EDIT);
    assert(wm_board_compose_backspace(compose));
    assert(wm_board_compose_backspace(compose));
    assert(strcmp(wm_board_compose_text(compose), ".[.") == 0);
    assert(wm_board_compose_hit(compose,
        (int)(more.x + more.width * 0.5f),
        (int)(more.y + more.height * 0.5f)) == more_control);
    assert(wm_board_compose_activate(compose, more_control));
    wm_board_compose_advance(compose, 18.0f);
    assert(wm_board_compose_back(compose));
    assert(wm_board_compose_phase(compose) == WM_COMPOSE_EDIT);
    assert(wm_board_compose_take_key_cue(compose) == NULL);
    wm_board_compose_advance(compose, 13.0f);
    assert(wm_board_compose_back(compose));
    assert(wm_board_compose_phase(compose) == WM_COMPOSE_LEAVE_EDIT);
    assert(strcmp(wm_board_compose_take_key_cue(compose),
                  "WIPL_SE_SK_CANCEL_CLOSE") == 0);
    assert(wm_board_compose_take_key_cue(compose) == NULL);
    wm_board_compose_destroy(compose);
}

static void test_compose_memo_scroll_transitions(int argc, char **argv) {
    const char *assets = argc > 1 ? argv[1] : ".local/native-assets";
    char path[4096];
    int length = snprintf(path, sizeof(path),
                          "%s/layouts/sofkeybd/my_Memo_a.json", assets);
    assert(length > 0 && length < (int)sizeof(path));
    char error[160];
    WmLayout *body = wm_layout_load_json(path, error, sizeof(error));
    assert(body);
    WmLayoutAnimationInfo fade_in;
    WmLayoutAnimationInfo fade_out;
    assert(wm_layout_animation_info(body, "my_Memo_a_Fade_IN", &fade_in));
    assert(wm_layout_animation_info(body, "my_Memo_a_Fade_OUT", &fade_out));
    assert(fade_in.frames == 11.0f && fade_out.frames == 10.0f);
    WmLayoutClip mail_in = {
        .animation = "my_Memo_a_MailIn", .frame = 16.0f
    };
    assert(wm_layout_pose(body, &mail_in, 1));
    WmSourceRect display_down;
    WmSourceRect editor_up;
    assert(wm_source_pane_rect(body, "B_ArwL", true, WM_LAYOUT_IPL,
                                NULL, &display_down));
    assert(wm_source_pane_rect(body, "B_txtScrll_UP", true, WM_LAYOUT_IPL,
                                NULL, &editor_up));
    WmBoardCompose *compose = wm_board_compose_create(
        (WmPlatform *)1, assets, (WmTextureCache *)1, test_fonts);
    assert(compose);
    assert(wm_board_compose_open(compose));
    wm_board_compose_advance(compose, 39.0f);
    assert(wm_board_compose_activate(compose, WM_COMPOSE_CONTROL_MEMO));
    wm_board_compose_advance(compose, 26.0f);

    WmBoardComposeScrollState scroll;
    assert(wm_board_compose_scroll_state(compose, &scroll));
    assert(!scroll.editing && !scroll.up_target_visible &&
           scroll.down_target_visible);
    assert(fabsf(scroll.maximum - 68.0f) < 0.01f);
    assert(wm_board_compose_hit(compose,
        (int)(display_down.x + display_down.width * 0.5f),
        (int)(display_down.y + display_down.height * 0.5f)) ==
           WM_COMPOSE_CONTROL_SCROLL_DOWN);
    assert(wm_board_compose_activate(compose, WM_COMPOSE_CONTROL_SCROLL_DOWN));
    wm_board_compose_advance(compose, 15.0f);
    assert(wm_board_compose_scroll_state(compose, &scroll));
    assert(fabsf(scroll.offset - 68.0f) < 0.01f);
    assert(scroll.up_target_visible && !scroll.down_target_visible);

    assert(wm_board_compose_activate(compose, WM_COMPOSE_CONTROL_EDIT));
    assert(wm_board_compose_scroll_state(compose, &scroll));
    assert(scroll.editing && scroll.editor_opacity == 0.0f &&
           !scroll.up_target_visible && !scroll.down_target_visible);
    wm_board_compose_advance(compose, 29.0f);
    assert(wm_board_compose_scroll_state(compose, &scroll));
    assert(scroll.editor_opacity == 0.0f);
    wm_board_compose_advance(compose, 1.0f);
    assert(wm_board_compose_phase(compose) == WM_COMPOSE_EDIT);
    assert(wm_board_compose_insert_text(
        compose, "A\nB\nC\nD\nE\nF\nG\nH\nI\nJ\nK\nL"));
    assert(wm_board_compose_scroll_state(compose, &scroll));
    assert(fabsf(scroll.maximum - 420.0f) < 0.01f);
    assert(scroll.editor_opacity == 1.0f);
    wm_board_compose_advance(compose, 15.0f);
    assert(wm_board_compose_scroll_state(compose, &scroll));
    assert(fabsf(scroll.offset - 420.0f) < 0.01f);
    assert(scroll.up_target_visible && !scroll.down_target_visible);
    assert(wm_board_compose_hit(compose,
        (int)(editor_up.x + editor_up.width * 0.5f),
        (int)(editor_up.y + editor_up.height * 0.5f)) ==
           WM_COMPOSE_CONTROL_SCROLL_UP);
    assert(wm_board_compose_activate(compose, WM_COMPOSE_CONTROL_SCROLL_UP));
    assert(!wm_board_compose_activate(compose, WM_COMPOSE_CONTROL_SCROLL_UP));
    wm_board_compose_advance(compose, 15.0f);
    assert(wm_board_compose_scroll_state(compose, &scroll));
    assert(fabsf(scroll.offset - 378.0f) < 0.01f);
    assert(scroll.up_target_visible && scroll.down_target_visible);

    assert(wm_board_compose_back(compose));
    assert(wm_board_compose_scroll_state(compose, &scroll));
    assert(scroll.editor_opacity == 1.0f);
    wm_board_compose_advance(compose, 15.0f);
    assert(wm_board_compose_scroll_state(compose, &scroll));
    assert(fabsf(scroll.editor_opacity - 0.5f) < 0.01f);
    wm_board_compose_advance(compose, 15.0f);
    assert(wm_board_compose_phase(compose) == WM_COMPOSE_MEMO);
    assert(wm_board_compose_scroll_state(compose, &scroll));
    assert(!scroll.editing && scroll.editor_opacity == 0.0f);
    assert(wm_board_compose_back(compose));
    assert(wm_board_compose_scroll_state(compose, &scroll));
    assert(!scroll.up_target_visible && !scroll.down_target_visible);
    wm_board_compose_destroy(compose);
    wm_layout_destroy(body);
}

static void test_board_arrow_return_from_create(int argc, char **argv) {
    WmBoardScene *board = load_board(argc, argv);
    assert(board);
    assert(wm_board_scene_open(board, (WmBoardDate){2026, 9, 25}));
    wm_board_scene_advance(board, 40.0f);
    (void)wm_board_scene_hit(board, 320, 200);
    float settled_left = 0.0f;
    float settled_right = 0.0f;
    float y = 0.0f;
    assert(wm_board_scene_footer_button_anchor(
        board, WM_BOARD_CONTROL_PREVIOUS, &settled_left, &y));
    assert(wm_board_scene_footer_button_anchor(
        board, WM_BOARD_CONTROL_NEXT, &settled_right, &y));

    assert(wm_board_scene_activate(board,
        (WmBoardHit){WM_BOARD_CONTROL_CREATE, SIZE_MAX}));
    wm_board_scene_advance(board, 39.0f);
    assert(wm_board_scene_back(board));
    wm_board_scene_advance(board, 46.0f);
    assert(wm_board_scene_child(board) == WM_BOARD_CHILD_NONE);

    float left[3];
    float right[3];
    for (size_t sample = 0; sample < 3; sample++) {
        if (sample) wm_board_scene_advance(board, 5.0f);
        (void)wm_board_scene_hit(board, 320, 200);
        assert(wm_board_scene_footer_button_anchor(
            board, WM_BOARD_CONTROL_PREVIOUS, &left[sample], &y));
        assert(wm_board_scene_footer_button_anchor(
            board, WM_BOARD_CONTROL_NEXT, &right[sample], &y));
    }
    /* Source my_IplTop_e 10150–10160 moves the arrows 200 world units
     * inward; neither side should jump directly to its settled location. */
    assert(left[0] < left[1] && left[1] < left[2]);
    assert(right[0] > right[1] && right[1] > right[2]);
    assert(fabsf(left[2] - settled_left) < 0.01f);
    assert(fabsf(right[2] - settled_right) < 0.01f);
    wm_board_scene_destroy(board);
}

static void test_child_return_retires_footer_focus(int argc, char **argv) {
    WmBoardScene *board = load_board(argc, argv);
    assert(board);
    assert(wm_board_scene_open(board, (WmBoardDate){2026, 9, 25}));
    wm_board_scene_advance(board, 40.0f);

    const WmBoardControl children[] = {
        WM_BOARD_CONTROL_CALENDAR, WM_BOARD_CONTROL_CREATE
    };
    for (size_t index = 0; index < sizeof(children) / sizeof(children[0]);
         index++) {
        WmBoardControl control = children[index];
        float neutral_scale = 0.0f;
        (void)wm_board_scene_hit(board, 320, 200);
        assert(wm_board_scene_footer_button_visual_scale(
            board, control, &neutral_scale));
        wm_board_scene_hover(board, (WmBoardHit){control, SIZE_MAX});
        wm_board_scene_advance(board, 6.0f);
        float focused_scale = 0.0f;
        (void)wm_board_scene_hit(board, 320, 200);
        assert(wm_board_scene_footer_button_visual_scale(
            board, control, &focused_scale));
        assert(focused_scale > neutral_scale + 0.05f);

        assert(wm_board_scene_activate(board,
                                       (WmBoardHit){control, SIZE_MAX}));
        wm_board_scene_advance(board, 50.0f);
        assert(wm_board_scene_back(board));
        wm_board_scene_advance(board, 50.0f);
        assert(wm_board_scene_child(board) == WM_BOARD_CHILD_NONE);
        /* HTML neutralBoardFocus() resolves the button to its source neutral
         * pose before opening either child. The WAD focus visual must not
         * return at its enlarged frame-6 scale. */
        float returned_scale = 0.0f;
        (void)wm_board_scene_hit(board, 320, 200);
        assert(wm_board_scene_footer_button_visual_scale(
            board, control, &returned_scale));
        assert(fabsf(returned_scale - neutral_scale) < 0.001f);
    }
    wm_board_scene_destroy(board);
}

static void test_erase_dialog(int argc, char **argv) {
    const char *assets = argc > 1 ? argv[1] : ".local/native-assets";
    WmBoardErase *erase = wm_board_erase_create(
        (WmPlatform *)1, assets, (WmTextureCache *)1, (WmFontCache *)1);
    assert(erase);
    assert(wm_board_erase_open(erase));
    assert(wm_board_erase_phase(erase) == WM_ERASE_ENTER);
    assert(!wm_board_erase_activate(erase, WM_ERASE_CONTROL_OK));
    wm_board_erase_advance(erase, 26.0f);
    assert(wm_board_erase_phase(erase) == WM_ERASE_IDLE);
    assert(wm_board_erase_back(erase));
    wm_board_erase_advance(erase, 21.0f);
    assert(wm_board_erase_phase(erase) == WM_ERASE_EXIT);
    wm_board_erase_advance(erase, 26.0f);
    assert(wm_board_erase_take_outcome(erase) == WM_ERASE_OUTCOME_CANCEL);
    assert(wm_board_erase_take_outcome(erase) == WM_ERASE_OUTCOME_NONE);
    wm_board_erase_destroy(erase);
}

static void test_memo_erase(int argc, char **argv) {
    WmBoardScene *board = load_board(argc, argv);
    assert(board);
    WmBoardMemo memo = {
        .id = "erase-test",
        .text = "A\nB\nC\nD\nE\nF\nG\nH\nI\nJ\nK\nL",
        .date = {2026, 9, 25},
        .has_position = true,
        .y = 53.0f
    };
    assert(wm_board_scene_set_memos(board, &memo, 1));
    assert(wm_board_scene_open(board, memo.date));
    wm_board_scene_advance(board, 40.0f);
    assert(wm_board_scene_today_unread_count(board) == 1);
    assert(wm_board_scene_activate(board,
        (WmBoardHit){WM_BOARD_CONTROL_MEMO, 0}));
    assert(wm_board_scene_today_unread_count(board) == 0);
    size_t read_index = SIZE_MAX;
    assert(wm_board_scene_take_action(board, &read_index) ==
           WM_BOARD_ACTION_MEMO_READ);
    assert(read_index == 0);
    wm_board_scene_advance(board, 26.0f);
    assert(wm_board_scene_phase(board) == WM_BOARD_MEMO_READ);
    assert(wm_board_scene_reader_arrow_target_visible(
        board, WM_BOARD_CONTROL_MEMO_SCROLL_DOWN));
    float trash_neutral_scale = 0.0f;
    float trash_focused_scale = 0.0f;
    wm_board_scene_hit(board, 0, 0);
    assert(wm_board_scene_footer_button_visual_scale(
        board, WM_BOARD_CONTROL_MEMO_TRASH, &trash_neutral_scale));
    wm_board_scene_hover(board,
        (WmBoardHit){WM_BOARD_CONTROL_MEMO_TRASH, SIZE_MAX});
    wm_board_scene_advance(board, 6.0f);
    wm_board_scene_hit(board, 0, 0);
    assert(wm_board_scene_footer_button_visual_scale(
        board, WM_BOARD_CONTROL_MEMO_TRASH, &trash_focused_scale));
    assert(trash_focused_scale > trash_neutral_scale + 0.02f);
    assert(wm_board_scene_activate(board,
        (WmBoardHit){WM_BOARD_CONTROL_MEMO_TRASH, 0}));
    assert(wm_board_scene_phase(board) == WM_BOARD_MEMO_TRASH_SELECT);
    assert(wm_board_scene_reader_arrow_target_visible(
        board, WM_BOARD_CONTROL_MEMO_SCROLL_DOWN));
    assert(wm_board_scene_take_action(board, NULL) == WM_BOARD_ACTION_NONE);
    wm_board_scene_advance(board, 33.0f);
    assert(wm_board_scene_child(board) == WM_BOARD_CHILD_ERASE);
    assert(strcmp(wm_board_scene_take_reader_cue(board),
                  "WIPL_SE_INFO_WINDOW") == 0);
    assert(wm_board_scene_take_reader_cue(board) == NULL);
    wm_board_scene_advance(board, 26.0f);
    assert(wm_board_scene_activate(board,
        (WmBoardHit){WM_BOARD_CONTROL_ERASE_QUIT, SIZE_MAX}));
    wm_board_scene_advance(board, 47.0f);
    assert(wm_board_scene_phase(board) == WM_BOARD_MEMO_TRASH_CANCEL);
    wm_board_scene_advance(board, 13.0f);
    assert(wm_board_scene_phase(board) == WM_BOARD_MEMO_READ);
    float trash_returned_scale = 0.0f;
    wm_board_scene_hit(board, 0, 0);
    assert(wm_board_scene_footer_button_visual_scale(
        board, WM_BOARD_CONTROL_MEMO_TRASH, &trash_returned_scale));
    assert(fabsf(trash_returned_scale - trash_neutral_scale) < 0.01f);
    assert(wm_board_scene_reader_arrow_target_visible(
        board, WM_BOARD_CONTROL_MEMO_SCROLL_DOWN));
    assert(wm_board_scene_memo_count(board) == 1);
    assert(wm_board_scene_take_action(board, NULL) == WM_BOARD_ACTION_NONE);

    assert(wm_board_scene_activate(board,
        (WmBoardHit){WM_BOARD_CONTROL_MEMO_TRASH, 0}));
    wm_board_scene_advance(board, 59.0f);
    assert(wm_board_scene_child(board) == WM_BOARD_CHILD_ERASE);
    assert(wm_board_scene_activate(board,
        (WmBoardHit){WM_BOARD_CONTROL_ERASE_OK, SIZE_MAX}));
    wm_board_scene_advance(board, 47.0f);
    assert(wm_board_scene_phase(board) == WM_BOARD_MEMO_ERASE_CLOSE);
    assert(strcmp(wm_board_scene_take_reader_cue(board),
                  "WIPL_SE_BOARD_DUMP") == 0);
    assert(wm_board_scene_take_reader_cue(board) == NULL);
    assert(!wm_board_scene_reader_arrow_target_visible(
        board, WM_BOARD_CONTROL_MEMO_SCROLL_DOWN));
    assert(wm_board_scene_memo_count(board) == 1);
    wm_board_scene_advance(board, 17.0f);
    assert(wm_board_scene_phase(board) == WM_BOARD_READY);
    assert(wm_board_scene_memo_count(board) == 0);
    assert(strcmp(wm_board_scene_last_erased_id(board), "erase-test") == 0);
    size_t erased_index = SIZE_MAX;
    assert(wm_board_scene_take_action(board, &erased_index) ==
           WM_BOARD_ACTION_ERASE_MEMO);
    assert(erased_index == 0);
    wm_board_scene_destroy(board);
}

static void test_reader_text_draw(int argc, char **argv) {
    const char *assets = argc > 1 ? argv[1] : ".local/native-assets";
    WmTextureCache *textures = wm_texture_cache_create(
        (WmPlatform *)1, assets, 16u * 1024u * 1024u);
    assert(textures);
    WmFontCache *draw_fonts = wm_font_cache_create(
        (WmPlatform *)1, assets, 16u * 1024u * 1024u);
    assert(draw_fonts);
    WmBoardScene *board = wm_board_scene_create(
        (WmPlatform *)1, assets, textures, draw_fonts);
    assert(board);
    const WmBoardMemo memo = {
        .id = "reader-text",
        .text = "A posted memo",
        .date = {2026, 9, 25},
        .has_position = true,
        .x = 0.0f,
        .y = 53.0f
    };
    assert(wm_board_scene_set_memos(board, &memo, 1));
    assert(wm_board_scene_open(board, memo.date));
    wm_board_scene_advance(board, 40.0f);
    float left_ready = 0.0f;
    float right_ready = 0.0f;
    float unused_y = 0.0f;
    wm_board_scene_draw_footer(board);
    assert(wm_board_scene_footer_button_anchor(
        board, WM_BOARD_CONTROL_PREVIOUS, &left_ready, &unused_y));
    assert(wm_board_scene_footer_button_anchor(
        board, WM_BOARD_CONTROL_NEXT, &right_ready, &unused_y));
    assert(wm_board_scene_activate(board,
        (WmBoardHit){WM_BOARD_CONTROL_MEMO, 0}));
    wm_board_scene_advance(board, 10.0f);
    float left_hidden = 0.0f;
    float right_hidden = 0.0f;
    wm_board_scene_draw_footer(board);
    assert(wm_board_scene_footer_button_anchor(
        board, WM_BOARD_CONTROL_PREVIOUS, &left_hidden, &unused_y));
    assert(wm_board_scene_footer_button_anchor(
        board, WM_BOARD_CONTROL_NEXT, &right_hidden, &unused_y));
    assert(fabsf(left_hidden - left_ready + 200.0f) < 0.01f);
    assert(fabsf(right_hidden - right_ready - 200.0f) < 0.01f);
    wm_board_scene_advance(board, 16.0f);
    wm_texture_cache_begin_frame(textures);
    wm_font_cache_begin_frame(draw_fonts);
    captured_text_quads = 0;
    captured_light_text_quads = 0;
    capture_text_draws = true;
    wm_board_scene_draw_body(board);
    capture_text_draws = false;
    assert(captured_text_quads >= strlen(memo.text));
    assert(captured_text_quads < 65);
    assert(captured_light_text_quads == 0);
    captured_text_quads = 0;
    capture_text_draws = true;
    wm_board_scene_draw_footer(board);
    capture_text_draws = false;
    size_t neutral_footer_glyphs = captured_text_quads;
    assert(neutral_footer_glyphs >= 4);
    float back_neutral_scale = 0.0f;
    assert(wm_board_scene_footer_button_visual_scale(
        board, WM_BOARD_CONTROL_MEMO_BACK, &back_neutral_scale));
    wm_board_scene_hover(board,
        (WmBoardHit){WM_BOARD_CONTROL_MEMO_BACK, SIZE_MAX});
    wm_board_scene_advance(board, 6.0f);
    captured_text_quads = 0;
    capture_text_draws = true;
    wm_board_scene_draw_footer(board);
    capture_text_draws = false;
    float back_focused_scale = 0.0f;
    assert(wm_board_scene_footer_button_visual_scale(
        board, WM_BOARD_CONTROL_MEMO_BACK, &back_focused_scale));
    assert(back_focused_scale > back_neutral_scale + 0.05f);
    float trash_neutral_scale = 0.0f;
    assert(wm_board_scene_footer_button_visual_scale(
        board, WM_BOARD_CONTROL_MEMO_TRASH, &trash_neutral_scale));
    wm_board_scene_hover(board,
        (WmBoardHit){WM_BOARD_CONTROL_MEMO_TRASH, SIZE_MAX});
    wm_board_scene_advance(board, 6.0f);
    captured_text_quads = 0;
    capture_text_draws = true;
    wm_board_scene_draw_footer(board);
    capture_text_draws = false;
    assert(captured_text_quads >= neutral_footer_glyphs + 5);
    float trash_focused_scale = 0.0f;
    assert(wm_board_scene_footer_button_visual_scale(
        board, WM_BOARD_CONTROL_MEMO_TRASH, &trash_focused_scale));
    assert(trash_focused_scale > trash_neutral_scale + 0.02f);
    assert(wm_board_scene_back(board));
    wm_board_scene_advance(board, 20.0f);
    wm_board_scene_advance(board, 10.0f);
    wm_board_scene_draw_footer(board);
    float left_returning = 0.0f;
    float right_returning = 0.0f;
    assert(wm_board_scene_footer_button_anchor(
        board, WM_BOARD_CONTROL_PREVIOUS, &left_returning, &unused_y));
    assert(wm_board_scene_footer_button_anchor(
        board, WM_BOARD_CONTROL_NEXT, &right_returning, &unused_y));
    assert(fabsf(left_returning - left_ready) < 0.01f);
    assert(fabsf(right_returning - right_ready) < 0.01f);
    wm_board_scene_destroy(board);
    wm_font_cache_destroy(draw_fonts);
    wm_texture_cache_destroy(textures);
}

static void test_board_store(int argc, char **argv) {
    char directory[] = "/tmp/wm-board-store-XXXXXX";
    int temporary = mkstemp(directory);
    assert(temporary >= 0);
    assert(close(temporary) == 0);
    assert(unlink(directory) == 0);
    assert(mkdir(directory, 0700) == 0);
    char path[256];
    int length = snprintf(path, sizeof(path), "%s/memos.json", directory);
    assert(length > 0 && length < (int)sizeof(path));
    WmBoardScene *board = load_board(argc, argv);
    WmBoardScene *loaded = load_board(argc, argv);
    assert(board && loaded);
    char error[160];
    assert(wm_board_store_load(path, loaded, error, sizeof(error)) ==
           WM_BOARD_STORE_MISSING);
    WmBoardMemo input = {
        .id = "memo-\xE2\x98\x83",
        .text = "Line \"one\"\nLine two \\ end",
        .date = {2026, 9, 25},
        .created_at_ms = INT64_C(1790345678901),
        .x = 31.125f,
        .y = -14.5f,
        .has_position = true,
        .read = true
    };
    assert(wm_board_scene_set_memos(board, &input, 1));
    assert(wm_board_store_save(path, board, error, sizeof(error)));
    assert(wm_board_store_load(path, loaded, error, sizeof(error)) ==
           WM_BOARD_STORE_OK);
    assert(wm_board_scene_memo_count(loaded) == 1);
    WmBoardMemo actual;
    assert(wm_board_scene_get_memo(loaded, 0, &actual));
    assert(strcmp(actual.id, input.id) == 0);
    assert(strcmp(actual.text, input.text) == 0);
    assert(actual.date.year == 2026 && actual.date.month == 9 &&
           actual.date.day == 25);
    assert(actual.created_at_ms == input.created_at_ms);
    assert(fabsf(actual.x - input.x) < 0.0001f);
    assert(fabsf(actual.y - input.y) < 0.0001f);
    assert(actual.read);
    assert(wm_board_scene_open(loaded, input.date));
    assert(wm_board_scene_today_unread_count(loaded) == 0);

    FILE *file = fopen(path, "wb");
    assert(file);
    assert(fputs("{\"schemaVersion\":1,\"memos\":[{\"id\":\"bad\"}]}",
                 file) >= 0);
    assert(fclose(file) == 0);
    assert(wm_board_store_load(path, loaded, error, sizeof(error)) ==
           WM_BOARD_STORE_ERROR);
    assert(wm_board_scene_memo_count(loaded) == 1);
    assert(wm_board_scene_get_memo(loaded, 0, &actual));
    assert(strcmp(actual.id, input.id) == 0);

    file = fopen(path, "wb");
    assert(file);
    assert(fputs("{\"schemaVersion\":1,\"memos\":["
                 "{\"id\":\"old-first\",\"text\":\"First\","
                 "\"date\":{\"year\":2026,\"month\":9,\"day\":25},"
                 "\"position\":{\"x\":0,\"y\":53},\"read\":false},"
                 "{\"id\":\"old-second\",\"text\":\"Second\","
                 "\"date\":{\"year\":2026,\"month\":9,\"day\":25},"
                 "\"position\":{\"x\":0,\"y\":53},\"read\":false}]}",
                 file) >= 0);
    assert(fclose(file) == 0);
    assert(wm_board_store_load(path, loaded, error, sizeof(error)) ==
           WM_BOARD_STORE_OK);
    assert(wm_board_scene_memo_count(loaded) == 2);
    assert(wm_board_scene_get_memo(loaded, 0, &actual));
    assert(strcmp(actual.id, "old-first") == 0 && actual.created_at_ms == 0);
    assert(wm_board_scene_get_memo(loaded, 1, &actual));
    assert(strcmp(actual.id, "old-second") == 0 && actual.created_at_ms == 0);
    assert(wm_board_store_save(path, loaded, error, sizeof(error)));
    assert(wm_board_store_load(path, board, error, sizeof(error)) ==
           WM_BOARD_STORE_OK);
    assert(wm_board_scene_get_memo(board, 0, &actual));
    assert(actual.created_at_ms == 0);

    file = fopen(path, "wb");
    assert(file);
    assert(fputs("{\"schemaVersion\":1,\"memos\":["
                 "{\"id\":\"bad-time\",\"text\":\"Bad\","
                 "\"date\":{\"year\":2026,\"month\":9,\"day\":25},"
                 "\"createdAtMs\":999999999999999999999999999,"
                 "\"position\":{\"x\":0,\"y\":53},\"read\":false}]}",
                 file) >= 0);
    assert(fclose(file) == 0);
    assert(wm_board_store_load(path, loaded, error, sizeof(error)) ==
           WM_BOARD_STORE_ERROR);
    assert(wm_board_scene_memo_count(loaded) == 2);

    assert(wm_board_scene_set_memos(board, NULL, 0));
    assert(wm_board_store_save(path, board, error, sizeof(error)));
    assert(wm_board_store_load(path, loaded, error, sizeof(error)) ==
           WM_BOARD_STORE_OK);
    assert(wm_board_scene_memo_count(loaded) == 0);
    wm_board_scene_destroy(board);
    wm_board_scene_destroy(loaded);
    assert(unlink(path) == 0);
    assert(rmdir(directory) == 0);
}

static void test_calendar_marker_material(int argc, char **argv) {
    const char *assets = argc > 1 ? argv[1] : ".local/native-assets";
    char path[4096];
    int length = snprintf(path, sizeof(path),
                          "%s/layouts/calendar/my_IplTop_f.json", assets);
    assert(length > 0 && length < (int)sizeof(path));
    char error[160];
    WmLayout *layout = wm_layout_load_json(path, error, sizeof(error));
    assert(layout);
    WmLayoutClip marker = {
        .animation = "my_IplTop_f",
        .target_name = "Info_a",
        .frame = 0.0f,
        .loop_override = 0
    };
    bool found = false;
    for (int frame = 0; frame <= 1; frame++) {
        marker.frame = (float)frame;
        assert(wm_layout_pose(layout, &marker, 1));
        for (size_t index = 0; index < wm_layout_material_count(layout);
             index++) {
            WmLayoutMaterialInfo info;
            uint8_t wraps[4][2];
            assert(wm_layout_material_info(layout, index, &info, wraps));
            if (strcmp(info.name, "Info_a") != 0) continue;
            found = true;
            assert(fabsf(info.registers[1][3] - (float)frame) < 0.001f);
        }
    }
    assert(found);
    wm_layout_destroy(layout);
}

static void test_memo_drag(int argc, char **argv) {
    WmBoardScene *board = load_board(argc, argv);
    assert(board);
    WmBoardMemo memo = {
        .id = "drag-test",
        .text = "Drag this card",
        .date = {2026, 9, 25},
        .x = 0.0f,
        .y = 53.0f,
        .has_position = true
    };
    assert(wm_board_scene_set_memos(board, &memo, 1));
    assert(wm_board_scene_open(board, memo.date));
    wm_board_scene_advance(board, 40.0f);
    assert(wm_board_scene_phase(board) == WM_BOARD_READY);

    WmBoardHit card = {WM_BOARD_CONTROL_MEMO, 0};
    assert(wm_board_scene_pointer_down(board, card, 320, 200));
    assert(wm_board_scene_dragging(board));
    assert(wm_board_scene_hit(board, 320, 200).control ==
           WM_BOARD_CONTROL_NONE);
    assert(!wm_board_scene_activate(board, card));
    float pan = 2.0f;
    assert(wm_board_scene_take_drag_cue(board, &pan) ==
           WM_BOARD_DRAG_CUE_HOLD);
    assert(fabsf(pan) < 0.001f);
    assert(wm_board_scene_pointer_move(board, 384, 180));
    wm_board_scene_advance(board, 1.0f);
    float gain = 0.0f;
    assert(wm_board_scene_drag_mix(board, &gain, &pan));
    float delta_x = 64.0f * 608.0f / 640.0f;
    float speed = hypotf(delta_x, 20.0f);
    assert(fabsf(gain - 2.0f * speed / 304.0f) < 0.001f);
    assert(fabsf(pan - delta_x / 304.0f) < 0.001f);
    assert(wm_board_scene_pointer_up(board, 384, 180));
    assert(!wm_board_scene_dragging(board));
    assert(!wm_board_scene_drag_mix(board, &gain, &pan));
    assert(wm_board_scene_take_drag_cue(board, &pan) ==
           WM_BOARD_DRAG_CUE_RELEASE);
    assert(fabsf(pan - delta_x / 304.0f) < 0.001f);
    assert(wm_board_scene_take_drag_cue(board, NULL) ==
           WM_BOARD_DRAG_CUE_NONE);
    size_t changed = SIZE_MAX;
    assert(wm_board_scene_take_action(board, &changed) ==
           WM_BOARD_ACTION_MEMO_MOVED);
    assert(changed == 0);
    WmBoardMemo moved;
    assert(wm_board_scene_get_memo(board, 0, &moved));
    assert(fabsf(moved.x - delta_x) < 0.001f);
    assert(fabsf(moved.y - 73.0f) < 0.001f);

    assert(wm_board_scene_pointer_down(board, card, 320, 200));
    assert(wm_board_scene_pointer_move(board, 640, 0));
    assert(wm_board_scene_pointer_up(board, 640, 0));
    assert(wm_board_scene_get_memo(board, 0, &moved));
    assert(fabsf(moved.x - 230.0f) < 0.001f);
    assert(fabsf(moved.y - 180.0f) < 0.001f);
    assert(wm_board_scene_take_action(board, NULL) ==
           WM_BOARD_ACTION_MEMO_MOVED);
    while (wm_board_scene_take_drag_cue(board, NULL) !=
           WM_BOARD_DRAG_CUE_NONE) {}

    assert(wm_board_scene_pointer_down(board, card, 320, 200));
    assert(wm_board_scene_pointer_move(board, 300, 220));
    assert(wm_board_scene_pointer_finish(board));
    assert(!wm_board_scene_dragging(board));
    assert(wm_board_scene_get_memo(board, 0, &moved));
    assert(fabsf(moved.x - (230.0f - 19.0f)) < 0.001f);
    assert(fabsf(moved.y - 160.0f) < 0.001f);
    assert(wm_board_scene_take_action(board, NULL) ==
           WM_BOARD_ACTION_MEMO_MOVED);
    while (wm_board_scene_take_drag_cue(board, NULL) !=
           WM_BOARD_DRAG_CUE_NONE) {}

    assert(wm_board_scene_pointer_down(board, card, 320, 200));
    assert(wm_board_scene_pointer_move(board, 100, 300));
    assert(wm_board_scene_back(board));
    assert(!wm_board_scene_dragging(board));
    assert(wm_board_scene_phase(board) == WM_BOARD_READY);
    assert(wm_board_scene_get_memo(board, 0, &moved));
    assert(fabsf(moved.x - 211.0f) < 0.001f);
    assert(fabsf(moved.y - 160.0f) < 0.001f);
    assert(wm_board_scene_take_action(board, NULL) == WM_BOARD_ACTION_NONE);
    assert(wm_board_scene_take_drag_cue(board, NULL) ==
           WM_BOARD_DRAG_CUE_HOLD);
    assert(wm_board_scene_take_drag_cue(board, NULL) ==
           WM_BOARD_DRAG_CUE_RELEASE);
    wm_board_scene_destroy(board);
}

static void test_memo_card_order(int argc, char **argv) {
    WmBoardScene *board = load_board(argc, argv);
    assert(board);
    WmBoardMemo memos[2] = {
        {.id = "lower", .text = "Lower", .date = {2026, 9, 25},
         .has_position = true, .x = 0.0f, .y = 53.0f},
        {.id = "upper", .text = "Upper", .date = {2026, 9, 25},
         .has_position = true, .x = 0.0f, .y = 53.0f}
    };
    assert(wm_board_scene_set_memos(board, memos, 2));
    assert(wm_board_scene_open(board, memos[0].date));
    wm_board_scene_advance(board, 40.0f);
    WmBoardHit hit = wm_board_scene_hit(board, 320, 175);
    assert(hit.control == WM_BOARD_CONTROL_MEMO && hit.memo_index == 1);
    wm_board_scene_hover(board, (WmBoardHit){WM_BOARD_CONTROL_MEMO, 0});
    hit = wm_board_scene_hit(board, 320, 175);
    assert(hit.control == WM_BOARD_CONTROL_MEMO && hit.memo_index == 0);
    wm_board_scene_destroy(board);
}

static void test_memo_reader_scroll_and_reset(int argc, char **argv) {
    WmBoardScene *board = load_board(argc, argv);
    assert(board);
    WmBoardMemo memo = {
        .id = "reader-test",
        .text = "A\nB\nC\nD\nE\nF\nG\nH\nI\nJ\nK\nL",
        .date = {2026, 9, 25},
        .has_position = true,
        .y = 53.0f
    };
    assert(wm_board_scene_set_memos(board, &memo, 1));
    assert(wm_board_scene_open(board, memo.date));
    wm_board_scene_advance(board, 40.0f);
    assert(wm_board_scene_activate(board,
        (WmBoardHit){WM_BOARD_CONTROL_MEMO, 0}));
    wm_board_scene_advance(board, 26.0f);
    assert(wm_board_scene_phase(board) == WM_BOARD_MEMO_READ);
    assert(fabsf(wm_board_scene_reader_scroll_limit(board) - 416.0f) <
           0.01f);
    assert(wm_board_scene_activate(board,
        (WmBoardHit){WM_BOARD_CONTROL_MEMO_SCROLL_DOWN, SIZE_MAX}));
    wm_board_scene_advance(board, 1.0f);
    assert(fabsf(wm_board_scene_reader_scroll_offset(board)) < 0.001f);
    wm_board_scene_advance(board, 10.0f);
    assert(fabsf(wm_board_scene_reader_scroll_offset(board) - 150.0f) <
           0.001f);
    assert(wm_board_scene_reader_scroll_sound_active(board));
    assert(!wm_board_scene_activate(board,
        (WmBoardHit){WM_BOARD_CONTROL_MEMO_SCROLL_UP, SIZE_MAX}));
    wm_board_scene_advance(board, 10.0f);
    assert(fabsf(wm_board_scene_reader_scroll_offset(board) - 300.0f) <
           0.001f);
    wm_board_scene_advance(board, 1.0f);
    assert(!wm_board_scene_reader_scroll_sound_active(board));
    assert(wm_board_scene_activate(board,
        (WmBoardHit){WM_BOARD_CONTROL_MEMO_SCROLL_UP, SIZE_MAX}));
    wm_board_scene_advance(board, 21.0f);
    assert(fabsf(wm_board_scene_reader_scroll_offset(board)) < 0.001f);

    wm_board_scene_reset(board);
    assert(wm_board_scene_phase(board) == WM_BOARD_CLOSED);
    assert(wm_board_scene_child(board) == WM_BOARD_CHILD_NONE);
    assert(wm_board_scene_memo_count(board) == 1);
    assert(wm_board_scene_take_action(board, NULL) == WM_BOARD_ACTION_NONE);
    assert(!wm_board_scene_reader_scroll_sound_active(board));
    assert(wm_board_scene_open(board, memo.date));
    wm_board_scene_advance(board, 40.0f);
    assert(wm_board_scene_activate(board,
        (WmBoardHit){WM_BOARD_CONTROL_CALENDAR, SIZE_MAX}));
    assert(wm_board_scene_child(board) == WM_BOARD_CHILD_CALENDAR);
    wm_board_scene_reset(board);
    assert(wm_board_scene_child(board) == WM_BOARD_CHILD_NONE);
    assert(wm_board_scene_open(board, memo.date));
    wm_board_scene_advance(board, 40.0f);
    assert(wm_board_scene_activate(board,
        (WmBoardHit){WM_BOARD_CONTROL_CREATE, SIZE_MAX}));
    assert(wm_board_scene_child(board) == WM_BOARD_CHILD_COMPOSE);
    wm_board_scene_reset(board);
    assert(wm_board_scene_child(board) == WM_BOARD_CHILD_NONE);
    assert(wm_board_scene_open(board, memo.date));
    wm_board_scene_advance(board, 40.0f);
    assert(wm_board_scene_activate(board,
        (WmBoardHit){WM_BOARD_CONTROL_MEMO, 0}));
    wm_board_scene_advance(board, 26.0f);
    assert(wm_board_scene_activate(board,
        (WmBoardHit){WM_BOARD_CONTROL_MEMO_TRASH, 0}));
    wm_board_scene_advance(board, 33.0f);
    assert(wm_board_scene_child(board) == WM_BOARD_CHILD_ERASE);
    wm_board_scene_reset(board);
    assert(wm_board_scene_child(board) == WM_BOARD_CHILD_NONE);
    assert(wm_board_scene_open(board, memo.date));
    wm_board_scene_destroy(board);
}

static void test_reader_arrow_exit_timing(int argc, char **argv) {
    WmBoardScene *board = load_board(argc, argv);
    assert(board);
    WmBoardMemo memo = {
        .id = "arrow-exit-test",
        .text = "A\nB\nC\nD\nE\nF\nG\nH\nI\nJ\nK\nL",
        .date = {2026, 9, 25},
        .has_position = true,
        .y = 53.0f
    };
    assert(wm_board_scene_set_memos(board, &memo, 1));
    assert(wm_board_scene_open(board, memo.date));
    wm_board_scene_advance(board, 40.0f);
    assert(wm_board_scene_activate(board,
        (WmBoardHit){WM_BOARD_CONTROL_MEMO, 0}));
    wm_board_scene_advance(board, 26.0f);
    assert(wm_board_scene_reader_arrow_target_visible(
        board, WM_BOARD_CONTROL_MEMO_SCROLL_DOWN));
    assert(wm_board_scene_activate(board,
        (WmBoardHit){WM_BOARD_CONTROL_MEMO_SCROLL_DOWN, SIZE_MAX}));
    wm_board_scene_advance(board, 11.0f);
    assert(wm_board_scene_reader_scroll_sound_active(board));

    assert(wm_board_scene_back(board));
    assert(wm_board_scene_phase(board) == WM_BOARD_MEMO_BACK_SELECT);
    assert(!wm_board_scene_reader_scroll_sound_active(board));
    assert(wm_board_scene_reader_arrow_target_visible(
        board, WM_BOARD_CONTROL_MEMO_SCROLL_DOWN));
    wm_board_scene_advance(board, 19.0f);
    assert(wm_board_scene_phase(board) == WM_BOARD_MEMO_BACK_SELECT);
    assert(wm_board_scene_reader_arrow_target_visible(
        board, WM_BOARD_CONTROL_MEMO_SCROLL_DOWN));
    wm_board_scene_advance(board, 1.0f);
    assert(wm_board_scene_phase(board) == WM_BOARD_MEMO_CLOSE);
    assert(!wm_board_scene_reader_arrow_target_visible(
        board, WM_BOARD_CONTROL_MEMO_SCROLL_DOWN));
    wm_board_scene_advance(board, 26.0f);
    assert(wm_board_scene_phase(board) == WM_BOARD_READY);
    wm_board_scene_destroy(board);
}

static bool find_board_control_point(WmBoardScene *board,
                                     WmBoardControl control,
                                     int *x, int *y) {
    for (int row = 0; row < 480; row += 8) {
        for (int column = 0; column < 832; column += 8) {
            if (wm_board_scene_hit(board, column, row).control != control)
                continue;
            *x = column;
            *y = row;
            return true;
        }
    }
    return false;
}

static void test_reader_back_hover_retired(int argc, char **argv) {
    const char *assets = argc > 1 ? argv[1] : ".local/native-assets";
    WmTextureCache *textures = wm_texture_cache_create(
        (WmPlatform *)1, assets, 16u * 1024u * 1024u);
    WmFontCache *fonts = wm_font_cache_create(
        (WmPlatform *)1, assets, 16u * 1024u * 1024u);
    assert(textures && fonts);
    WmBoardScene *board = wm_board_scene_create(
        (WmPlatform *)1, assets, textures, fonts);
    assert(board);
    const WmBoardMemo memo = {
        .id = "reader-back-hover",
        .text = "Posted memo",
        .date = {2026, 9, 25},
        .has_position = true,
        .y = 53.0f
    };
    assert(wm_board_scene_set_memos(board, &memo, 1));
    assert(wm_board_scene_open(board, memo.date));
    wm_board_scene_advance(board, 40.0f);
    int board_back_x = 0;
    int board_back_y = 0;
    assert(find_board_control_point(board, WM_BOARD_CONTROL_BACK,
                                    &board_back_x, &board_back_y));
    (void)wm_board_scene_hit(board, 0, 0);
    float board_back_neutral = 0.0f;
    assert(wm_board_scene_footer_button_visual_scale(
        board, WM_BOARD_CONTROL_BACK, &board_back_neutral));
    wm_board_scene_hover(board, wm_board_scene_hit(
        board, board_back_x, board_back_y));
    wm_board_scene_advance(board, 6.0f);
    (void)wm_board_scene_hit(board, 0, 0);
    float board_back_focused = 0.0f;
    assert(wm_board_scene_footer_button_visual_scale(
        board, WM_BOARD_CONTROL_BACK, &board_back_focused));
    assert(board_back_focused > board_back_neutral + 0.05f);
    int memo_x = 0;
    int memo_y = 0;
    assert(find_board_control_point(board, WM_BOARD_CONTROL_MEMO,
                                    &memo_x, &memo_y));
    WmBoardHit memo_hit = wm_board_scene_hit(board, memo_x, memo_y);
    /* Pointer down/up re-hit the click coordinate without synthesizing a
     * move event. The previous Board Back focus must not cross scenes. */
    assert(wm_board_scene_activate(board, memo_hit));
    wm_board_scene_draw_footer(board);
    float board_back_during_open = 0.0f;
    assert(wm_board_scene_footer_button_visual_scale(
        board, WM_BOARD_CONTROL_BACK, &board_back_during_open));
    assert(fabsf(board_back_during_open - board_back_neutral) < 0.001f);
    wm_board_scene_advance(board, 26.0f);
    int reader_back_x = 0;
    int reader_back_y = 0;
    assert(find_board_control_point(board, WM_BOARD_CONTROL_MEMO_BACK,
                                    &reader_back_x, &reader_back_y));
    (void)wm_board_scene_hit(board, 0, 0);
    float reader_back_neutral = 0.0f;
    assert(wm_board_scene_footer_button_visual_scale(
        board, WM_BOARD_CONTROL_MEMO_BACK, &reader_back_neutral));
    wm_board_scene_hover(board, wm_board_scene_hit(
        board, reader_back_x, reader_back_y));
    wm_board_scene_advance(board, 6.0f);
    (void)wm_board_scene_hit(board, 0, 0);
    float reader_back_focused = 0.0f;
    assert(wm_board_scene_footer_button_visual_scale(
        board, WM_BOARD_CONTROL_MEMO_BACK, &reader_back_focused));
    assert(reader_back_focused > reader_back_neutral + 0.05f);
    assert(wm_board_scene_activate(board, wm_board_scene_hit(
        board, reader_back_x, reader_back_y)));
    assert(wm_board_scene_phase(board) == WM_BOARD_MEMO_BACK_SELECT);
    assert(wm_board_scene_hit(board, reader_back_x, reader_back_y).control ==
           WM_BOARD_CONTROL_NONE);
    wm_board_scene_advance(board, 20.0f);
    assert(wm_board_scene_phase(board) == WM_BOARD_MEMO_CLOSE);
    wm_board_scene_draw_footer(board);
    float board_back_early_close = 0.0f;
    assert(wm_board_scene_footer_button_visual_scale(
        board, WM_BOARD_CONTROL_BACK, &board_back_early_close));
    assert(fabsf(board_back_early_close - board_back_neutral) < 0.001f);
    wm_board_scene_advance(board, 13.0f);
    wm_board_scene_draw_footer(board);
    float board_back_mid_close = 0.0f;
    assert(wm_board_scene_footer_button_visual_scale(
        board, WM_BOARD_CONTROL_BACK, &board_back_mid_close));
    assert(fabsf(board_back_mid_close - board_back_neutral) < 0.001f);
    wm_board_scene_advance(board, 13.0f);
    assert(wm_board_scene_phase(board) == WM_BOARD_READY);
    assert(wm_board_scene_hit(board, reader_back_x, reader_back_y).control !=
           WM_BOARD_CONTROL_BACK);
    (void)wm_board_scene_hit(board, 0, 0);
    float board_back_returned = 0.0f;
    assert(wm_board_scene_footer_button_visual_scale(
        board, WM_BOARD_CONTROL_BACK, &board_back_returned));
    assert(fabsf(board_back_returned - board_back_neutral) < 0.001f);
    assert(wm_board_scene_activate(board,
        (WmBoardHit){WM_BOARD_CONTROL_MEMO, 0}));
    wm_board_scene_advance(board, 26.0f);
    (void)wm_board_scene_hit(board, 0, 0);
    float reader_back_returned = 0.0f;
    assert(wm_board_scene_footer_button_visual_scale(
        board, WM_BOARD_CONTROL_MEMO_BACK, &reader_back_returned));
    assert(fabsf(reader_back_returned - reader_back_neutral) < 0.001f);
    wm_board_scene_destroy(board);
    wm_font_cache_destroy(fonts);
    wm_texture_cache_destroy(textures);
}

static bool reader_arrow_center(WmBoardScene *board,
                                WmBoardControl control, int *x, int *y) {
    int left = 640;
    int top = 480;
    int right = -1;
    int bottom = -1;
    for (int row = 0; row < 480; row += 12) {
        for (int column = 0; column < 640; column += 12) {
            if (wm_board_scene_hit(board, column, row).control != control)
                continue;
            if (column < left) left = column;
            if (column > right) right = column;
            if (row < top) top = row;
            if (row > bottom) bottom = row;
        }
    }
    if (right < left || bottom < top) return false;
    *x = (left + right) / 2;
    *y = (top + bottom) / 2;
    return wm_board_scene_hit(board, *x, *y).control == control;
}

static void test_reader_arrow_hover_stability(int argc, char **argv) {
    WmBoardScene *board = load_board(argc, argv);
    assert(board);
    const WmBoardMemo memo = {
        .id = "reader-hover-test",
        .text = "A\nB\nC\nD\nE\nF\nG\nH\nI\nJ\nK\nL",
        .date = {2026, 9, 25},
        .has_position = true,
        .y = 53.0f
    };
    assert(wm_board_scene_set_memos(board, &memo, 1));
    assert(wm_board_scene_open(board, memo.date));
    wm_board_scene_advance(board, 40.0f);
    assert(wm_board_scene_activate(board,
        (WmBoardHit){WM_BOARD_CONTROL_MEMO, 0}));
    wm_board_scene_advance(board, 26.0f);
    assert(wm_board_scene_phase(board) == WM_BOARD_MEMO_READ);
    /* The down arrow begins its authored appearance when reading unlocks. */
    wm_board_scene_advance(board, 10.0f);

    int x = 0;
    int y = 0;
    assert(reader_arrow_center(board, WM_BOARD_CONTROL_MEMO_SCROLL_DOWN,
                               &x, &y));
    for (unsigned frame = 0; frame < 16; frame++) {
        WmBoardHit hit = wm_board_scene_hit(board, x, y);
        assert(hit.control == WM_BOARD_CONTROL_MEMO_SCROLL_DOWN);
        wm_board_scene_hover(board, hit);
        wm_board_scene_advance(board, 1.0f);
    }
    assert(wm_board_scene_hit(board, x, y).control ==
           WM_BOARD_CONTROL_MEMO_SCROLL_DOWN);

    assert(wm_board_scene_activate(board,
        (WmBoardHit){WM_BOARD_CONTROL_MEMO_SCROLL_DOWN, SIZE_MAX}));
    wm_board_scene_advance(board, 21.0f);
    /* Scrolling exposes the up arrow, which also needs its appearance clip. */
    wm_board_scene_advance(board, 10.0f);
    assert(reader_arrow_center(board, WM_BOARD_CONTROL_MEMO_SCROLL_UP,
                               &x, &y));
    for (unsigned frame = 0; frame < 16; frame++) {
        WmBoardHit hit = wm_board_scene_hit(board, x, y);
        assert(hit.control == WM_BOARD_CONTROL_MEMO_SCROLL_UP);
        wm_board_scene_hover(board, hit);
        wm_board_scene_advance(board, 1.0f);
    }
    assert(wm_board_scene_hit(board, x, y).control ==
           WM_BOARD_CONTROL_MEMO_SCROLL_UP);
    wm_board_scene_destroy(board);
}

static void test_return_to_today(int argc, char **argv) {
    WmBoardScene *board = load_board(argc, argv);
    assert(board);
    const WmBoardDate today = {2026, 9, 1};
    const struct {
        WmBoardControl arrow;
        WmBoardDate selected;
    } cases[] = {
        {WM_BOARD_CONTROL_NEXT, {2026, 9, 3}},
        {WM_BOARD_CONTROL_PREVIOUS, {2026, 8, 30}}
    };
    for (size_t index = 0; index < sizeof(cases) / sizeof(cases[0]); index++) {
        assert(wm_board_scene_open(board, today));
        wm_board_scene_advance(board, 40.0f);
        for (int day = 0; day < 2; day++) {
            assert(wm_board_scene_activate(board,
                (WmBoardHit){cases[index].arrow, SIZE_MAX}));
            wm_board_scene_advance(board, 20.0f);
        }
        WmBoardDate date = wm_board_scene_date(board);
        assert(date.year == cases[index].selected.year &&
               date.month == cases[index].selected.month &&
               date.day == cases[index].selected.day);

        assert(wm_board_scene_back(board));
        assert(wm_board_scene_phase(board) == WM_BOARD_EXIT);
        wm_board_scene_advance(board, 20.0f);
        date = wm_board_scene_date(board);
        assert(date.month == cases[index].selected.month &&
               date.day == cases[index].selected.day);
        wm_board_scene_advance(board, 19.0f);
        assert(wm_board_scene_phase(board) == WM_BOARD_EXIT);
        wm_board_scene_advance(board, 1.0f);
        assert(wm_board_scene_phase(board) == WM_BOARD_CLOSED);
        date = wm_board_scene_date(board);
        assert(date.year == today.year && date.month == today.month &&
               date.day == today.day);
        assert(wm_board_scene_take_action(board, NULL) ==
               WM_BOARD_ACTION_EXITED);
    }
    wm_board_scene_destroy(board);
}

static const WmBoardMemoPresentation *find_presented_memo(
    const WmBoardMemoPresentation *cards, size_t count, size_t memo_index) {
    for (size_t index = 0; index < count; index++) {
        if (cards[index].memo_index == memo_index) return &cards[index];
    }
    return NULL;
}

static void test_parked_memos(int argc, char **argv) {
    WmBoardScene *board = load_board(argc, argv);
    assert(board);
    WmBoardDate today = wm_board_scene_date(board);
    WmBoardDate yesterday;
    assert(wm_board_date_shift(today, -1, &yesterday));
    WmBoardMemo memos[12] = {0};
    char ids[12][12];
    for (size_t index = 0; index < 12; index++) {
        snprintf(ids[index], sizeof(ids[index]), "parked-%zu", index);
        memos[index] = (WmBoardMemo){
            .id = ids[index],
            .text = "Memo",
            .date = index == 0 ? yesterday : today,
            .has_position = true,
            .x = index % 2 ? -220.0f : 220.0f,
            .y = index % 2 ? -65.0f : 175.0f
        };
    }
    assert(wm_board_scene_set_memos(board, memos, 12));
    WmBoardMemoPresentation cards[WM_BOARD_MAX_PRESENTED_MEMOS];
    size_t count = wm_board_scene_parked_memo_presentation(board, today,
                                                             cards);
    assert(count == 10);
    for (size_t index = 0; index < count; index++) {
        assert(cards[index].memo_index == index + 1);
        assert(cards[index].paste_frame == 10.0f);
        assert(cards[index].next_page_frame == -1.0f);
        assert(!cards[index].entering);
        assert(fabsf(cards[index].x -
                     memos[index + 1].x * 832.0f / 608.0f) < 0.01f);
        assert(cards[index].y == memos[index + 1].y);
    }
    assert(wm_board_scene_parked_memo_presentation(board, yesterday,
                                                    cards) == 1);
    assert(cards[0].memo_index == 0);
    assert(wm_board_scene_open(board, today));
    assert(wm_board_scene_parked_memo_presentation(board, today,
                                                    cards) == 0);
    wm_board_scene_advance(board, 40.0f);
    assert(wm_board_scene_activate(board,
        (WmBoardHit){WM_BOARD_CONTROL_PREVIOUS, SIZE_MAX}));
    wm_board_scene_advance(board, 20.0f);
    assert(wm_board_scene_back(board));
    wm_board_scene_advance(board, 40.0f);
    assert(wm_board_scene_phase(board) == WM_BOARD_CLOSED);
    count = wm_board_scene_parked_memo_presentation(board, today, cards);
    assert(count == 10 && cards[0].memo_index == 1 &&
           cards[9].memo_index == 10);
    wm_board_scene_reset(board);
    count = wm_board_scene_parked_memo_presentation(board, today, cards);
    assert(count == 10 && cards[0].memo_index == 1);
    wm_board_scene_destroy(board);
}

static void test_memo_creation_order(int argc, char **argv) {
    WmBoardScene *board = load_board(argc, argv);
    assert(board);
    WmBoardMemo memos[12] = {0};
    char ids[12][12];
    WmBoardDate date = {2026, 9, 25};
    for (size_t index = 0; index < 12; index++) {
        snprintf(ids[index], sizeof(ids[index]), "ordered-%zu", index);
        memos[index] = (WmBoardMemo){
            .id = ids[index],
            .text = "Timestamped Memo",
            .date = date,
            .created_at_ms = index < 2 ? 0 :
                INT64_C(1790294400000) + (int64_t)index * 1000,
            .has_position = true,
            .x = (float)index,
            .y = 53.0f
        };
    }
    memos[3].created_at_ms = memos[2].created_at_ms;
    assert(wm_board_scene_set_memos(board, memos, 12));
    WmBoardMemo actual;
    assert(wm_board_scene_get_memo(board, 0, &actual));
    assert(strcmp(actual.id, "ordered-0") == 0);

    WmBoardMemoPresentation cards[WM_BOARD_MAX_PRESENTED_MEMOS];
    size_t count = wm_board_scene_parked_memo_presentation(board, date,
                                                             cards);
    assert(count == 10);
    for (size_t position = 0; position < 8; position++) {
        assert(cards[position].memo_index == 11 - position);
    }
    assert(cards[8].memo_index == 2 && cards[9].memo_index == 3);

    assert(wm_board_scene_open(board, date));
    wm_board_scene_advance(board, 40.0f);
    count = wm_board_scene_memo_presentation(board, cards);
    assert(count == 10);
    for (size_t position = 0; position < 10; position++) {
        size_t expected = position < 8 ? 11 - position : position - 6;
        assert(cards[position].memo_index == expected);
    }
    assert(wm_board_scene_activate(board,
        (WmBoardHit){WM_BOARD_CONTROL_PREVIOUS, SIZE_MAX}));
    count = wm_board_scene_memo_presentation(board, cards);
    assert(count == 12);
    assert(cards[10].memo_index == 0 && cards[10].entering);
    assert(cards[11].memo_index == 1 && cards[11].entering);
    wm_board_scene_advance(board, 15.0f);
    count = wm_board_scene_memo_presentation(board, cards);
    assert(count == 2);
    assert(cards[0].memo_index == 0 && cards[1].memo_index == 1);
    wm_board_scene_destroy(board);
}

static void test_memo_date_continuity(int argc, char **argv) {
    WmBoardScene *board = load_board(argc, argv);
    assert(board);
    WmBoardMemo memos[3] = {
        {.id = "before", .text = "Before", .date = {2026, 9, 24},
         .has_position = true, .x = -220.0f, .y = 170.0f},
        {.id = "today", .text = "Today", .date = {2026, 9, 25},
         .has_position = true, .x = -210.0f, .y = -60.0f},
        {.id = "after", .text = "After", .date = {2026, 9, 26},
         .has_position = true, .x = 220.0f, .y = -60.0f}
    };
    assert(wm_board_scene_set_memos(board, memos, 3));
    for (size_t case_index = 0; case_index < 2; case_index++) {
        size_t destination = case_index == 0 ? 2 : 0;
        WmBoardControl control = case_index == 0
                                     ? WM_BOARD_CONTROL_NEXT
                                     : WM_BOARD_CONTROL_PREVIOUS;
        assert(wm_board_scene_open(board, memos[1].date));
        wm_board_scene_advance(board, 40.0f);

        WmBoardMemoPresentation cards[WM_BOARD_MAX_PRESENTED_MEMOS];
        assert(wm_board_scene_activate(board,
            (WmBoardHit){control, SIZE_MAX}));
        size_t count = wm_board_scene_memo_presentation(board, cards);
        assert(count == 2);
        const WmBoardMemoPresentation *entering = find_presented_memo(
            cards, count, destination);
        const WmBoardMemoPresentation *leaving = find_presented_memo(
            cards, count, 1);
        assert(entering && leaving && entering->entering && !leaving->entering);
        assert(entering->paste_frame == 10.0f &&
               leaving->paste_frame == 10.0f);
        float entering_start = entering->x;
        float leaving_start = leaving->x;
        float date_offset = case_index == 0 ? 832.0f : -832.0f;
        assert(fabsf(entering_start -
                     (memos[destination].x * 832.0f / 608.0f +
                      date_offset)) < 0.01f);

        wm_board_scene_advance(board, 10.0f);
        count = wm_board_scene_memo_presentation(board, cards);
        entering = find_presented_memo(cards, count, destination);
        leaving = find_presented_memo(cards, count, 1);
        assert(entering && leaving && entering->paste_frame == 10.0f &&
               leaving->paste_frame == 10.0f);
        assert(fabsf((entering->x - entering_start) -
                     (leaving->x - leaving_start)) < 0.01f);
        assert(fabsf(entering->x - entering_start) > 1.0f);

        wm_board_scene_advance(board, 10.0f);
        assert(wm_board_scene_phase(board) == WM_BOARD_READY);
        count = wm_board_scene_memo_presentation(board, cards);
        assert(count == 1 && cards[0].memo_index == destination &&
               cards[0].paste_frame == 10.0f);
        assert(fabsf(cards[0].x -
                     memos[destination].x * 832.0f / 608.0f) < 0.01f);

        assert(wm_board_scene_back(board));
        count = wm_board_scene_memo_presentation(board, cards);
        entering = find_presented_memo(cards, count, 1);
        leaving = find_presented_memo(cards, count, destination);
        assert(count == 2 && entering && leaving && entering->entering);
        assert(entering->paste_frame == 10.0f);
        entering_start = entering->x;
        leaving_start = leaving->x;
        wm_board_scene_advance(board, 10.0f);
        count = wm_board_scene_memo_presentation(board, cards);
        entering = find_presented_memo(cards, count, 1);
        leaving = find_presented_memo(cards, count, destination);
        assert(entering && leaving);
        assert(fabsf((entering->x - entering_start) -
                     (leaving->x - leaving_start)) < 0.01f);
        wm_board_scene_advance(board, 10.0f);
        count = wm_board_scene_memo_presentation(board, cards);
        entering = find_presented_memo(cards, count, 1);
        assert(entering && entering->paste_frame == 10.0f);
        assert(fabsf(entering->x - memos[1].x * 832.0f / 608.0f) < 0.01f);
        wm_board_scene_advance(board, 20.0f);
        assert(wm_board_scene_phase(board) == WM_BOARD_CLOSED);
        assert(wm_board_scene_take_action(board, NULL) ==
               WM_BOARD_ACTION_EXITED);
    }
    wm_board_scene_destroy(board);
}

static void test_memo_page_continuity(int argc, char **argv) {
    WmBoardScene *board = load_board(argc, argv);
    assert(board);
    WmBoardMemo memos[11] = {0};
    char ids[11][12];
    for (size_t index = 0; index < 11; index++) {
        snprintf(ids[index], sizeof(ids[index]), "page-%zu", index);
        memos[index] = (WmBoardMemo){
            .id = ids[index], .text = "Posted Memo",
            .date = {2026, 9, 25}, .has_position = true,
            .x = (float)index * 10.0f, .y = 53.0f
        };
    }
    assert(wm_board_scene_set_memos(board, memos, 11));
    assert(wm_board_scene_open(board, memos[0].date));
    wm_board_scene_advance(board, 40.0f);
    assert(wm_board_scene_activate(board,
        (WmBoardHit){WM_BOARD_CONTROL_PREVIOUS, SIZE_MAX}));
    WmBoardMemoPresentation cards[WM_BOARD_MAX_PRESENTED_MEMOS];
    size_t count = wm_board_scene_memo_presentation(board, cards);
    assert(count == 11);
    const WmBoardMemoPresentation *incoming = find_presented_memo(cards,
                                                                   count, 10);
    assert(incoming && incoming->entering && incoming->paste_frame == 10.0f);
    assert(incoming->next_page_frame < 0.0f);
    assert(fabsf(incoming->x + 304.0f * 832.0f / 608.0f) < 0.01f);
    for (size_t index = 0; index < 10; index++) {
        const WmBoardMemoPresentation *outgoing = find_presented_memo(
            cards, count, index);
        assert(outgoing && !outgoing->entering &&
               outgoing->paste_frame == 10.0f &&
               outgoing->next_page_frame == 0.0f);
    }
    wm_board_scene_advance(board, 7.5f);
    count = wm_board_scene_memo_presentation(board, cards);
    incoming = find_presented_memo(cards, count, 10);
    assert(incoming && incoming->paste_frame == 10.0f &&
           incoming->x > -304.0f * 832.0f / 608.0f);
    wm_board_scene_advance(board, 7.5f);
    assert(wm_board_scene_phase(board) == WM_BOARD_READY);
    count = wm_board_scene_memo_presentation(board, cards);
    assert(count == 1 && cards[0].memo_index == 10 &&
           cards[0].paste_frame == 10.0f &&
           cards[0].next_page_frame < 0.0f);
    assert(fabsf(cards[0].x - memos[10].x * 832.0f / 608.0f) < 0.01f);
    assert(wm_board_scene_activate(board,
        (WmBoardHit){WM_BOARD_CONTROL_NEXT, SIZE_MAX}));
    count = wm_board_scene_memo_presentation(board, cards);
    assert(count == 11);
    incoming = find_presented_memo(cards, count, 0);
    assert(incoming && incoming->entering && incoming->paste_frame == 10.0f);
    assert(fabsf(incoming->x - 304.0f * 832.0f / 608.0f) < 0.01f);
    wm_board_scene_advance(board, 15.0f);
    assert(wm_board_scene_phase(board) == WM_BOARD_READY);
    count = wm_board_scene_memo_presentation(board, cards);
    assert(count == 10);
    for (size_t index = 0; index < count; index++) {
        assert(cards[index].paste_frame == 10.0f && !cards[index].entering);
    }
    wm_board_scene_destroy(board);
}

static void test_only_new_memo_pastes(int argc, char **argv) {
    WmBoardScene *board = load_board(argc, argv);
    assert(board);
    WmBoardMemo previous = {
        .id = "existing", .text = "Existing Memo",
        .date = {2026, 9, 25}, .has_position = true,
        .x = -210.0f, .y = -60.0f
    };
    assert(wm_board_scene_set_memos(board, &previous, 1));
    assert(wm_board_scene_open(board, previous.date));
    wm_board_scene_advance(board, 40.0f);
    assert(wm_board_scene_activate(board,
        (WmBoardHit){WM_BOARD_CONTROL_CREATE, SIZE_MAX}));
    wm_board_scene_advance(board, 39.0f);
    assert(wm_board_scene_activate(board,
        (WmBoardHit){WM_BOARD_CONTROL_COMPOSE_MEMO, SIZE_MAX}));
    wm_board_scene_advance(board, 26.0f);
    assert(wm_board_scene_insert_text(board, "New Memo"));
    wm_board_scene_advance(board, 30.0f);
    assert(wm_board_scene_finish_edit(board));
    wm_board_scene_advance(board, 30.0f);
    assert(wm_board_scene_activate(board,
        (WmBoardHit){WM_BOARD_CONTROL_COMPOSE_POST, SIZE_MAX}));
    wm_board_scene_advance(board, 20.0f);
    wm_board_scene_advance(board, 51.0f);
    size_t posted_index = SIZE_MAX;
    assert(wm_board_scene_take_action(board, &posted_index) ==
           WM_BOARD_ACTION_MEMO_POSTED);
    assert(posted_index == 1);

    WmBoardMemoPresentation cards[WM_BOARD_MAX_PRESENTED_MEMOS];
    size_t count = wm_board_scene_memo_presentation(board, cards);
    const WmBoardMemoPresentation *old = find_presented_memo(cards, count, 0);
    const WmBoardMemoPresentation *posted = find_presented_memo(cards,
                                                                  count, 1);
    assert(count == 2 && old && posted);
    assert(old->paste_frame == 10.0f && posted->paste_frame == 0.0f);
    wm_board_scene_advance(board, 5.0f);
    count = wm_board_scene_memo_presentation(board, cards);
    old = find_presented_memo(cards, count, 0);
    posted = find_presented_memo(cards, count, 1);
    assert(old && posted && old->paste_frame == 10.0f &&
           posted->paste_frame == 5.0f);
    wm_board_scene_destroy(board);
}

static void test_address_editor_scene_route(int argc, char **argv) {
    WmBoardScene *board = load_board(argc, argv);
    assert(board);
    assert(wm_board_scene_open(board, (WmBoardDate){2026, 9, 25}));
    wm_board_scene_advance(board, 40.0f);
    assert(wm_board_scene_activate(board,
        (WmBoardHit){WM_BOARD_CONTROL_CREATE, SIZE_MAX}));
    wm_board_scene_advance(board, 39.0f);
    assert(wm_board_scene_activate(board,
        (WmBoardHit){WM_BOARD_CONTROL_COMPOSE_ADDRESS, SIZE_MAX}));
    wm_board_scene_advance(board, 26.0f);
    assert(wm_board_scene_activate(board,
        (WmBoardHit){WM_BOARD_CONTROL_COMPOSE_POST, SIZE_MAX}));
    wm_board_scene_advance(board, 57.0f);
    assert(wm_board_scene_activate(board,
        (WmBoardHit){WM_BOARD_CONTROL_COMPOSE_ADDRESS_WII, SIZE_MAX}));
    wm_board_scene_advance(board, 61.0f);
    assert(!wm_board_scene_address_editor_active(board));
    assert(wm_board_scene_activate(board,
        (WmBoardHit){WM_BOARD_CONTROL_COMPOSE_ADDRESS_EDIT, SIZE_MAX}));
    assert(wm_board_scene_address_editor_active(board));
    assert(wm_board_scene_compose_editor_active(board));
    assert(wm_board_scene_insert_text(board, "8742285515623182"));
    assert(wm_board_scene_finish_edit(board));
    assert(!wm_board_scene_address_editor_active(board));
    assert(!wm_board_scene_compose_editor_active(board));
    assert(wm_board_scene_activate(board,
        (WmBoardHit){WM_BOARD_CONTROL_COMPOSE_ADDRESS_EDIT, SIZE_MAX}));
    assert(wm_board_scene_address_editor_active(board));
    assert(wm_board_scene_back(board));
    assert(!wm_board_scene_address_editor_active(board));
    wm_board_scene_destroy(board);
}

static void test_address_contact_scene_route(int argc, char **argv) {
    const char *assets = argc > 1 ? argv[1] : ".local/native-assets";
    char contacts_path[] = "/tmp/wm-board-contact-route-XXXXXX";
    int file_descriptor = mkstemp(contacts_path);
    assert(file_descriptor >= 0);
    FILE *file = fdopen(file_descriptor, "wb");
    assert(file);
    assert(fputs("[{\"kind\":\"email\",\"address\":\"local@example.test\","
                 "\"nickname\":\"Local\"}]", file) >= 0);
    assert(fclose(file) == 0);

    WmBoardScene *board = load_board(argc, argv);
    assert(board);
    char error[160] = {0};
    assert(wm_board_scene_load_contacts(board, contacts_path,
                                         error, sizeof(error)) ==
           WM_BOARD_CONTACT_STORE_OK);
    assert(wm_board_scene_open(board, (WmBoardDate){2026, 9, 25}));
    wm_board_scene_advance(board, 40.0f);
    assert(wm_board_scene_activate(board,
        (WmBoardHit){WM_BOARD_CONTROL_CREATE, SIZE_MAX}));
    wm_board_scene_advance(board, 39.0f);
    assert(wm_board_scene_activate(board,
        (WmBoardHit){WM_BOARD_CONTROL_COMPOSE_ADDRESS, SIZE_MAX}));
    wm_board_scene_advance(board, 26.0f);
    assert(wm_board_scene_activate(board,
        (WmBoardHit){WM_BOARD_CONTROL_COMPOSE_ADDRESS_NEXT, SIZE_MAX}));
    wm_board_scene_advance(board, 16.0f);
    assert(wm_board_scene_activate(board,
        (WmBoardHit){WM_BOARD_CONTROL_COMPOSE_ADDRESS_ENTRY_FIRST, SIZE_MAX}));
    wm_board_scene_advance(board, 59.0f);

    char path[4096];
    int length = snprintf(path, sizeof(path),
                          "%s/layouts/board/th_Adress_b.json", assets);
    assert(length > 0 && length < (int)sizeof(path));
    WmLayout *card = wm_layout_load_json(path, error, sizeof(error));
    assert(card);
    const WmLayoutClip card_entrance = {
        .animation = "th_Adress_b_card_strt",
        .group = "card_strt_fnsh", .frame = 18.0f
    };
    assert(wm_layout_pose(card, &card_entrance, 1));
    WmSourceRect rename_rect, erase_rect;
    assert(wm_source_pane_rect(card, "B_crd_btn_10", true,
                               WM_LAYOUT_IPL, NULL, &rename_rect));
    assert(wm_source_pane_rect(card, "B_crd_btn_11", true,
                               WM_LAYOUT_IPL, NULL, &erase_rect));
    WmBoardHit rename_hit = wm_board_scene_hit(board,
        (int)(rename_rect.x + rename_rect.width * 0.5f),
        (int)(rename_rect.y + rename_rect.height * 0.5f));
    assert(rename_hit.control ==
           WM_BOARD_CONTROL_COMPOSE_ADDRESS_CHANGE_NICKNAME);
    wm_board_scene_hover(board, rename_hit);
    assert(wm_board_scene_activate(board, rename_hit));
    wm_board_scene_advance(board, 61.0f);
    assert(wm_board_scene_back(board));
    wm_board_scene_advance(board, 38.0f);
    WmBoardHit erase_hit = wm_board_scene_hit(board,
        (int)(erase_rect.x + erase_rect.width * 0.5f),
        (int)(erase_rect.y + erase_rect.height * 0.5f));
    assert(erase_hit.control == WM_BOARD_CONTROL_COMPOSE_ADDRESS_ERASE);
    wm_board_scene_hover(board, erase_hit);
    assert(wm_board_scene_activate(board, erase_hit));
    wm_board_scene_advance(board, 32.0f);
    wm_board_scene_advance(board, 26.0f);

    length = snprintf(path, sizeof(path),
                      "%s/layouts/dlgWdw/my_DialogWindow_b.json", assets);
    assert(length > 0 && length < (int)sizeof(path));
    WmLayout *dialog = wm_layout_load_json(path, error, sizeof(error));
    assert(dialog);
    const WmLayoutClip dialog_entrance = {
        .animation = "my_DialogWindow_b_DialogIn",
        .group = "G_InOut", .frame = 25.0f
    };
    assert(wm_layout_pose(dialog, &dialog_entrance, 1));
    WmSourceRect yes_rect, no_rect;
    assert(wm_source_pane_rect(dialog, "B_BtnA", true,
                               WM_LAYOUT_IPL, NULL, &yes_rect));
    assert(wm_source_pane_rect(dialog, "B_BtnB", true,
                               WM_LAYOUT_IPL, NULL, &no_rect));
    WmBoardHit no_hit = wm_board_scene_hit(board,
        (int)(no_rect.x + no_rect.width * 0.5f),
        (int)(no_rect.y + no_rect.height * 0.5f));
    assert(no_hit.control == WM_BOARD_CONTROL_COMPOSE_ADDRESS_DIALOG_NO);
    assert(wm_board_scene_activate(board, no_hit));
    wm_board_scene_advance(board, 47.0f);
    wm_board_scene_advance(board, 32.0f);
    assert(wm_board_scene_activate(board,
        (WmBoardHit){WM_BOARD_CONTROL_COMPOSE_ADDRESS_ERASE, SIZE_MAX}));
    wm_board_scene_advance(board, 32.0f);
    wm_board_scene_advance(board, 26.0f);
    WmBoardHit yes_hit = wm_board_scene_hit(board,
        (int)(yes_rect.x + yes_rect.width * 0.5f),
        (int)(yes_rect.y + yes_rect.height * 0.5f));
    assert(yes_hit.control == WM_BOARD_CONTROL_COMPOSE_ADDRESS_DIALOG_YES);
    assert(wm_board_scene_activate(board, yes_hit));

    wm_layout_destroy(dialog);
    wm_layout_destroy(card);
    wm_board_scene_destroy(board);
    assert(unlink(contacts_path) == 0);
}

typedef struct PinClock {
    int64_t now_ms;
    unsigned calls;
} PinClock;

static int64_t pin_clock_now(void *context) {
    PinClock *clock = context;
    clock->calls++;
    return clock->now_ms;
}

static void test_memo_pin_animation_choice(int argc, char **argv) {
    WmBoardScene *board = load_board(argc, argv);
    assert(board);
    PinClock clock = {.now_ms = INT64_C(1790345678901)};
    wm_board_scene_set_pin_clock(board, pin_clock_now, &clock);
    const WmBoardDate day = {2026, 9, 25};
    const WmBoardMemo memos[] = {
        {.id = "recent", .text = "Recent", .date = day,
         .created_at_ms = clock.now_ms - INT64_C(21600000) + 1},
        {.id = "old", .text = "Old", .date = day,
         .created_at_ms = clock.now_ms - INT64_C(21600000)},
        {.id = "future", .text = "Future", .date = day,
         .created_at_ms = clock.now_ms + 1000},
        {.id = "current", .text = "Current", .date = day,
         .created_at_ms = clock.now_ms},
        {.id = "legacy", .text = "Legacy", .date = day}
    };
    assert(wm_board_scene_set_memos(board, memos,
                                     sizeof(memos) / sizeof(memos[0])));
    assert(wm_board_scene_open(board, day));
    WmBoardMemoPresentation cards[WM_BOARD_MAX_PRESENTED_MEMOS];
    size_t count = wm_board_scene_memo_presentation(board, cards);
    assert(count == 5 && clock.calls == 4);
    const WmBoardPinKind expected[] = {
        WM_BOARD_PIN_NEW, WM_BOARD_PIN_DEFAULT, WM_BOARD_PIN_NONE,
        WM_BOARD_PIN_NONE, WM_BOARD_PIN_DEFAULT
    };
    for (size_t index = 0; index < count; index++) {
        const WmBoardMemoPresentation *card =
            find_presented_memo(cards, count, index);
        assert(card && card->pin_kind == expected[index]);
    }

    /* Existing cards retain their choice as the clock crosses the boundary.
     * A date change recreates the cards and samples them again. */
    clock.now_ms += INT64_C(21600000);
    count = wm_board_scene_memo_presentation(board, cards);
    assert(count == 5 && clock.calls == 4);
    for (size_t index = 0; index < count; index++) {
        const WmBoardMemoPresentation *card =
            find_presented_memo(cards, count, index);
        assert(card && card->pin_kind == expected[index]);
    }
    wm_board_scene_advance(board, 40.0f);
    assert(wm_board_scene_activate(board,
        (WmBoardHit){WM_BOARD_CONTROL_NEXT, SIZE_MAX}));
    wm_board_scene_advance(board, 20.0f);
    assert(wm_board_scene_activate(board,
        (WmBoardHit){WM_BOARD_CONTROL_PREVIOUS, SIZE_MAX}));
    wm_board_scene_advance(board, 20.0f);
    count = wm_board_scene_memo_presentation(board, cards);
    assert(count == 5 && clock.calls == 8);
    const WmBoardPinKind after_return[] = {
        WM_BOARD_PIN_DEFAULT, WM_BOARD_PIN_DEFAULT, WM_BOARD_PIN_NEW,
        WM_BOARD_PIN_DEFAULT, WM_BOARD_PIN_DEFAULT
    };
    for (size_t index = 0; index < count; index++) {
        const WmBoardMemoPresentation *card =
            find_presented_memo(cards, count, index);
        assert(card && card->pin_kind == after_return[index]);
    }
    wm_board_scene_destroy(board);
}

typedef struct PinAlpha {
    float value;
    bool found;
} PinAlpha;

static bool capture_pin_alpha(void *context,
                               const WmLayoutPaneView *pane) {
    if (strcmp(pane->name, "Pin2") == 0) {
        PinAlpha *capture = context;
        capture->value = pane->alpha;
        capture->found = true;
    }
    return true;
}

static void test_pin_source_tracks(int argc, char **argv) {
    const char *assets = argc > 1 ? argv[1] : ".local/native-assets";
    char path[4096];
    int length = snprintf(path, sizeof(path),
                          "%s/layouts/board/LetterS_a.json", assets);
    assert(length > 0 && length < (int)sizeof(path));
    char error[160] = {0};
    WmLayout *layout = wm_layout_load_json(path, error, sizeof(error));
    assert(layout);
    WmLayoutAnimationInfo new_animation;
    WmLayoutAnimationInfo default_animation;
    assert(wm_layout_animation_info(layout, "LetterS_a_NewAnim",
                                     &new_animation));
    assert(wm_layout_animation_info(layout, "LetterS_a_DefAnim",
                                     &default_animation));
    assert(new_animation.loop && new_animation.frames == 90.0f);
    assert(default_animation.loop && default_animation.frames == 4.0f);
    const WmLayoutClip paste = {
        .animation = "LetterS_a_PasteLetter", .frame = 10.0f,
        .loop_override = 0
    };
    const char *const names[] = {
        NULL, "LetterS_a_NewAnim", "LetterS_a_DefAnim"
    };
    const float expected[] = {0.0f, 1.0f, 0.0f};
    for (size_t index = 0; index < sizeof(names) / sizeof(names[0]); index++) {
        WmLayoutClip clips[2] = {paste};
        size_t count = 1;
        if (names[index]) {
            clips[count++] = (WmLayoutClip){
                .animation = names[index], .frame = 0.0f,
                .group = "G_New", .loop_override = 1
            };
        }
        assert(wm_layout_pose(layout, clips, count));
        PinAlpha capture = {0};
        const WmLayoutDrawOptions draw = {
            .wide = true, .mode = WM_LAYOUT_IPL, .alpha = 1.0f,
            .on_pane = capture_pin_alpha, .context = &capture
        };
        wm_layout_draw(layout, &draw);
        assert(capture.found);
        assert(fabsf(capture.value - expected[index]) < 0.0001f);
    }
    wm_layout_destroy(layout);
}

int main(int argc, char **argv) {
    test_date_arithmetic();
    test_badge();
    WmBoardScene *board = load_board(argc, argv);
    if (!board) return 0;
    test_home_badge_midnight_refresh(argc, argv);
    test_state(board);
    test_calendar(argc, argv);
    test_calendar_hover_order(argc, argv);
    test_calendar_same_tile_motion(argc, argv);
    test_calendar_hover_boundary_stability(argc, argv);
    test_compose(argc, argv);
    test_compose_network_dialog(argc, argv);
    test_board_network_settings_action(argc, argv);
    test_compose_mii_notice(argc, argv);
    test_compose_software_keyboard(argc, argv);
    test_compose_phone_keyboard(argc, argv);
    test_compose_held_keytops(argc, argv);
    test_memo_caret_layout(argc, argv);
    test_compose_symbol_pages(argc, argv);
    test_compose_memo_scroll_transitions(argc, argv);
    test_board_arrow_return_from_create(argc, argv);
    test_child_return_retires_footer_focus(argc, argv);
    test_erase_dialog(argc, argv);
    test_memo_erase(argc, argv);
    test_reader_text_draw(argc, argv);
    test_board_store(argc, argv);
    test_calendar_marker_material(argc, argv);
    test_memo_drag(argc, argv);
    test_memo_card_order(argc, argv);
    test_memo_reader_scroll_and_reset(argc, argv);
    test_reader_arrow_exit_timing(argc, argv);
    test_reader_back_hover_retired(argc, argv);
    test_reader_arrow_hover_stability(argc, argv);
    test_return_to_today(argc, argv);
    test_parked_memos(argc, argv);
    test_memo_creation_order(argc, argv);
    test_memo_date_continuity(argc, argv);
    test_memo_page_continuity(argc, argv);
    test_only_new_memo_pastes(argc, argv);
    test_address_editor_scene_route(argc, argv);
    test_address_contact_scene_route(argc, argv);
    test_memo_pin_animation_choice(argc, argv);
    test_pin_source_tracks(argc, argv);
    wm_board_scene_destroy(board);
    wm_font_cache_destroy(test_fonts);
    puts("Message Board state tests passed.");
    return 0;
}
