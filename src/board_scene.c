#define _POSIX_C_SOURCE 200809L

#include "wii_menu/board_scene.h"
#include "wii_menu/board_calendar.h"
#include "wii_menu/board_compose.h"
#include "wii_menu/board_erase.h"
#include "wii_menu/board_reader_scroll.h"

#include "wii_menu/layout_present.h"
#include "wii_menu/layout_runtime.h"
#include "wii_menu/material_prepare.h"
#include "wii_menu/source_hit.h"

#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

enum {
    BOARD_PATH_CAPACITY = 4096,
    BOARD_MEMOS_PER_PAGE = 10,
    BOARD_MAX_MEMOS = 4096,
    BOARD_MAX_TEXT_BYTES = 16 * 1024 * 1024,
    BOARD_CLIP_CAPACITY = 20
};

typedef struct BoardMemo {
    char *id;
    char *text;
    WmBoardDate date;
    int64_t created_at_ms;
    float x;
    float y;
    bool read;
    size_t source_index;
    WmBoardPinKind pin_kind;
    bool pin_sampled;
} BoardMemo;

typedef struct BoardMemoOrder {
    size_t index;
    int64_t created_at_ms;
} BoardMemoOrder;

typedef struct BoardPinState {
    WmBoardPinKind kind;
    bool sampled;
} BoardPinState;

typedef struct BoardFocus {
    bool active;
    bool entering;
    float frame;
} BoardFocus;

struct WmBoardScene {
    WmPlatform *platform;
    WmTextureCache *textures;
    WmFontCache *fonts;
    WmLayout *background;
    WmLayout *footer;
    WmLayout *mask;
    WmLayout *reader;
    WmLayout *card;
    WmBoardCalendar *calendar;
    WmBoardCompose *compose;
    WmBoardErase *erase;
    WmBoardReaderScroll reader_scroll;
    BoardMemo *memos;
    BoardMemoOrder *memo_order;
    size_t memo_count;
    size_t visible[BOARD_MEMOS_PER_PAGE];
    size_t card_order[BOARD_MEMOS_PER_PAGE];
    size_t visible_count;
    size_t day_count;
    size_t page;
    int grid_page;
    WmBoardDate date;
    WmBoardDate next_date;
    WmBoardDate today;
    WmBoardPhase phase;
    float phase_frame;
    float age;
    float pin_age;
    float card_age;
    size_t arriving_memo;
    float scroll_offset_x;
    float mask_age;
    int mask_direction;
    int direction;
    int return_direction;
    size_t selected;
    WmBoardHit hover;
    BoardFocus button_focus[WM_BOARD_CONTROL_MEMO_TRASH + 1];
    BoardFocus card_focus[BOARD_MEMOS_PER_PAGE];
    float arrow_press[2];
    WmBoardAction pending_action;
    size_t pending_memo_index;
    char *last_erased_id;
    bool dragging;
    size_t dragged_index;
    int drag_start_x;
    int drag_start_y;
    float drag_delta_x;
    float drag_delta_y;
    float drag_last_x;
    float drag_last_y;
    float drag_gain;
    float drag_pan;
    WmBoardDragCue drag_cues[8];
    float drag_cue_pans[8];
    unsigned drag_cue_count;
    const char *pending_reader_cue;
    uint64_t rng_state;
    WmBoardTimeNow pin_time_now;
    void *pin_time_context;
};

enum { BOARD_NEW_PIN_AGE_MS = 21600 * 1000 };

static int64_t local_time_milliseconds(void *context) {
    (void)context;
    struct timespec clock = {0};
    if (clock_gettime(CLOCK_REALTIME, &clock) != 0) return -1;
    return (int64_t)clock.tv_sec * INT64_C(1000) +
           clock.tv_nsec / 1000000;
}

void wm_board_scene_set_pin_clock(WmBoardScene *board,
                                   WmBoardTimeNow time_now, void *context) {
    if (!board) return;
    board->pin_time_now = time_now;
    board->pin_time_context = time_now ? context : NULL;
}

static WmBoardPinKind sample_pin_kind(WmBoardScene *board, size_t index) {
    BoardMemo *memo = &board->memos[index];
    if (memo->pin_sampled) return memo->pin_kind;
    /* Version 1 stores only the calendar day. Treat its unknown creation time
     * as an ordinary settled Memo rather than a newly arrived one. */
    if (memo->created_at_ms == 0) {
        memo->pin_kind = WM_BOARD_PIN_DEFAULT;
        memo->pin_sampled = true;
        return memo->pin_kind;
    }
    int64_t now = board->pin_time_now
                      ? board->pin_time_now(board->pin_time_context)
                      : local_time_milliseconds(NULL);
    if (now < 0) return WM_BOARD_PIN_NONE;
    int64_t elapsed = now - memo->created_at_ms;
    if (elapsed <= 0) memo->pin_kind = WM_BOARD_PIN_NONE;
    else if (elapsed < BOARD_NEW_PIN_AGE_MS)
        memo->pin_kind = WM_BOARD_PIN_NEW;
    else memo->pin_kind = WM_BOARD_PIN_DEFAULT;
    memo->pin_sampled = true;
    return memo->pin_kind;
}

static void reset_pin_choices(WmBoardScene *board) {
    for (size_t index = 0; index < board->memo_count; index++) {
        board->memos[index].pin_sampled = false;
    }
    board->pin_age = 0.0f;
}

static size_t visible_position(const WmBoardScene *board,
                               size_t source_index);
static void promote_card(WmBoardScene *board, size_t source_index);

static bool same_date(WmBoardDate first, WmBoardDate second) {
    return first.year == second.year && first.month == second.month &&
           first.day == second.day;
}

static bool leap_year(int year) {
    return year % 4 == 0 && (year % 100 != 0 || year % 400 == 0);
}

bool wm_board_date_valid(WmBoardDate date) {
    static const int month_days[12] = {
        31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31
    };
    if (date.year < 2000 || date.year > 2035 ||
        date.month < 1 || date.month > 12 || date.day < 1) return false;
    int maximum = month_days[date.month - 1];
    if (date.month == 2 && leap_year(date.year)) maximum++;
    return date.day <= maximum;
}

/* Days since 1970-01-01. The formula keeps all arithmetic in bounded signed
 * 64-bit integers for the Board calendar's source-supported year range. */
static int64_t civil_day(WmBoardDate date) {
    int64_t year = date.year - (date.month <= 2);
    int64_t era = year / 400;
    int64_t year_of_era = year - era * 400;
    int64_t month = date.month + (date.month > 2 ? -3 : 9);
    int64_t day_of_year = (153 * month + 2) / 5 + date.day - 1;
    int64_t year_day = year_of_era * 365 + year_of_era / 4 -
                       year_of_era / 100 + day_of_year;
    return era * 146097 + year_day - 719468;
}

static WmBoardDate date_from_day(int64_t day) {
    day += 719468;
    int64_t era = day / 146097;
    int64_t day_of_era = day - era * 146097;
    int64_t year_of_era = (day_of_era - day_of_era / 1460 +
                           day_of_era / 36524 - day_of_era / 146096) / 365;
    int64_t year = year_of_era + era * 400;
    int64_t day_of_year = day_of_era - (365 * year_of_era +
                           year_of_era / 4 - year_of_era / 100);
    int64_t month_part = (5 * day_of_year + 2) / 153;
    int month = (int)(month_part + (month_part < 10 ? 3 : -9));
    return (WmBoardDate){
        .year = (int)(year + (month <= 2)),
        .month = month,
        .day = (int)(day_of_year - (153 * month_part + 2) / 5 + 1)
    };
}

bool wm_board_date_shift(WmBoardDate date, int days, WmBoardDate *result) {
    if (!result || !wm_board_date_valid(date)) return false;
    int64_t shifted = civil_day(date) + (int64_t)days;
    WmBoardDate value = date_from_day(shifted);
    if (!wm_board_date_valid(value)) return false;
    *result = value;
    return true;
}

unsigned wm_board_badge_count(const WmBoardMemo *memos, size_t count,
                              WmBoardDate today) {
    if (!memos || !wm_board_date_valid(today)) return 0;
    unsigned found = 0;
    for (size_t index = 0; index < count; index++) {
        if (same_date(memos[index].date, today) && found < 99) found++;
    }
    return found;
}

static WmBoardDate local_today(void) {
    time_t now = time(NULL);
    struct tm value;
    if (now == (time_t)-1 || !localtime_r(&now, &value)) {
        return (WmBoardDate){2000, 1, 1};
    }
    WmBoardDate date = {
        .year = value.tm_year + 1900,
        .month = value.tm_mon + 1,
        .day = value.tm_mday
    };
    return wm_board_date_valid(date) ? date : (WmBoardDate){2000, 1, 1};
}

static char *copy_string(const char *value) {
    if (!value) return NULL;
    size_t length = strlen(value);
    if (length == SIZE_MAX) return NULL;
    char *copy = malloc(length + 1);
    if (copy) memcpy(copy, value, length + 1);
    return copy;
}

static void free_memos(BoardMemo *memos, size_t count) {
    if (!memos) return;
    for (size_t index = 0; index < count; index++) {
        free(memos[index].id);
        free(memos[index].text);
    }
    free(memos);
}

static int compare_memo_order(const void *left, const void *right) {
    const BoardMemoOrder *first = left;
    const BoardMemoOrder *second = right;
    if (first->created_at_ms > second->created_at_ms) return -1;
    if (first->created_at_ms < second->created_at_ms) return 1;
    if (first->index < second->index) return -1;
    if (first->index > second->index) return 1;
    return 0;
}

static float clamp_frame(float frame, float length) {
    return fminf(fmaxf(frame, 0.0f), length);
}

static float clamp_position(float value, float minimum, float maximum) {
    return fminf(fmaxf(value, minimum), maximum);
}

static WmLayout *load_layout(const char *directory, const char *relative) {
    char path[BOARD_PATH_CAPACITY];
    int length = snprintf(path, sizeof(path), "%s/%s", directory, relative);
    if (length < 0 || length >= (int)sizeof(path)) return NULL;
    char error[160] = {0};
    WmLayout *layout = wm_layout_load_json(path, error, sizeof(error));
    if (!layout) {
        fprintf(stderr, "Could not load Message Board layout %s: %s\n",
                relative, error);
    }
    return layout;
}

static void update_visible(WmBoardScene *board) {
    size_t first = board->page * BOARD_MEMOS_PER_PAGE;
    board->day_count = 0;
    board->visible_count = 0;
    for (size_t position = 0; position < board->memo_count; position++) {
        size_t index = board->memo_order[position].index;
        if (!same_date(board->memos[index].date, board->date)) continue;
        if (board->day_count >= first &&
            board->visible_count < BOARD_MEMOS_PER_PAGE) {
            board->visible[board->visible_count] = index;
            board->card_order[board->visible_count] = index;
            board->visible_count++;
        }
        board->day_count++;
    }
    size_t pages = (board->day_count + BOARD_MEMOS_PER_PAGE - 1) /
                   BOARD_MEMOS_PER_PAGE;
    if (pages == 0) pages = 1;
    if (board->page >= pages) {
        board->page = pages - 1;
        update_visible(board);
    }
}

WmBoardScene *wm_board_scene_create(WmPlatform *platform,
                                     const char *assets_directory,
                                     WmTextureCache *textures,
                                     WmFontCache *fonts) {
    if (!platform || !assets_directory || !assets_directory[0] ||
        !textures || !fonts) return NULL;
    WmBoardScene *board = calloc(1, sizeof(*board));
    if (!board) return NULL;
    board->platform = platform;
    board->textures = textures;
    board->fonts = fonts;
    board->background = load_layout(assets_directory,
                                     "layouts/board/my_IplTop_c.json");
    board->footer = load_layout(assets_directory,
                                 "layouts/cmnBtn/my_IplTop_e.json");
    board->mask = load_layout(assets_directory,
                               "layouts/board/my_BbsMask_a.json");
    board->reader = load_layout(assets_directory,
                                 "layouts/board/my_Memo_a.json");
    board->card = load_layout(assets_directory,
                               "layouts/board/LetterS_a.json");
    board->calendar = wm_board_calendar_create(platform, assets_directory,
                                                textures, fonts);
    board->compose = wm_board_compose_create(platform, assets_directory,
                                             textures, fonts);
    board->erase = wm_board_erase_create(platform, assets_directory,
                                        textures, fonts);
    if (!board->background || !board->footer || !board->mask ||
        !board->reader || !board->card || !board->calendar ||
        !board->compose || !board->erase) {
        wm_board_scene_destroy(board);
        return NULL;
    }
    wm_layout_prepare_materials(platform, board->background);
    wm_layout_prepare_materials(platform, board->footer);
    wm_layout_prepare_materials(platform, board->mask);
    wm_layout_prepare_materials(platform, board->reader);
    wm_layout_prepare_materials(platform, board->card);
    board->phase = WM_BOARD_CLOSED;
    board->date = local_today();
    board->today = board->date;
    board->selected = SIZE_MAX;
    board->dragged_index = SIZE_MAX;
    board->arriving_memo = SIZE_MAX;
    board->hover.memo_index = SIZE_MAX;
    wm_board_reader_scroll_reset(&board->reader_scroll);
    board->arrow_press[0] = -1.0f;
    board->arrow_press[1] = -1.0f;
    struct timespec seed;
    if (clock_gettime(CLOCK_REALTIME, &seed) == 0) {
        board->rng_state = ((uint64_t)seed.tv_sec << 32) ^
                           (uint64_t)seed.tv_nsec;
    }
    if (!board->rng_state) board->rng_state = UINT64_C(0x9e3779b97f4a7c15);
    return board;
}

void wm_board_scene_destroy(WmBoardScene *board) {
    if (!board) return;
    wm_layout_destroy(board->background);
    wm_layout_destroy(board->footer);
    wm_layout_destroy(board->mask);
    wm_layout_destroy(board->reader);
    wm_layout_destroy(board->card);
    wm_board_calendar_destroy(board->calendar);
    wm_board_compose_destroy(board->compose);
    wm_board_erase_destroy(board->erase);
    free(board->last_erased_id);
    free_memos(board->memos, board->memo_count);
    free(board->memo_order);
    free(board);
}

WmBoardContactStoreStatus wm_board_scene_load_contacts(
    WmBoardScene *board, const char *path,
    char *error, size_t error_capacity) {
    if (!board) return WM_BOARD_CONTACT_STORE_ERROR;
    return wm_board_compose_load_contacts(board->compose, path,
                                           error, error_capacity);
}

void wm_board_scene_reset(WmBoardScene *board) {
    if (!board) return;
    wm_board_calendar_reset(board->calendar);
    wm_board_compose_reset(board->compose);
    wm_board_erase_reset(board->erase);
    wm_board_reader_scroll_reset(&board->reader_scroll);
    board->phase = WM_BOARD_CLOSED;
    board->phase_frame = 0.0f;
    board->age = 0.0f;
    board->card_age = 0.0f;
    board->arriving_memo = SIZE_MAX;
    board->page = 0;
    board->selected = SIZE_MAX;
    board->dragging = false;
    board->dragged_index = SIZE_MAX;
    board->drag_gain = 0.0f;
    board->drag_cue_count = 0;
    board->pending_reader_cue = NULL;
    board->hover = (WmBoardHit){WM_BOARD_CONTROL_NONE, SIZE_MAX};
    memset(board->button_focus, 0, sizeof(board->button_focus));
    memset(board->card_focus, 0, sizeof(board->card_focus));
    board->arrow_press[0] = -1.0f;
    board->arrow_press[1] = -1.0f;
    board->pending_action = WM_BOARD_ACTION_NONE;
    board->pending_memo_index = SIZE_MAX;
    board->mask_direction = 0;
    board->mask_age = 0.0f;
    board->scroll_offset_x = 0.0f;
    board->return_direction = 0;
    board->today = local_today();
    board->date = board->today;
    reset_pin_choices(board);
    update_visible(board);
    free(board->last_erased_id);
    board->last_erased_id = NULL;
}

bool wm_board_scene_set_memos(WmBoardScene *board,
                               const WmBoardMemo *memos, size_t count) {
    if (!board || (count && !memos) || count > BOARD_MAX_MEMOS ||
        count > SIZE_MAX / sizeof(BoardMemo)) return false;
    BoardMemo *copy = count ? calloc(count, sizeof(*copy)) : NULL;
    BoardMemoOrder *order = count ? malloc(count * sizeof(*order)) : NULL;
    if (count && (!copy || !order)) {
        free(copy);
        free(order);
        return false;
    }
    size_t total_bytes = 0;
    for (size_t index = 0; index < count; index++) {
        const WmBoardMemo *source = &memos[index];
        if (!source->id || !source->text || !wm_board_date_valid(source->date) ||
            source->created_at_ms < 0 ||
            source->created_at_ms > INT64_C(253402300799999)) {
            free_memos(copy, count);
            free(order);
            return false;
        }
        size_t id_bytes = strlen(source->id);
        size_t text_bytes = strlen(source->text);
        if (id_bytes > BOARD_MAX_TEXT_BYTES - total_bytes ||
            text_bytes > BOARD_MAX_TEXT_BYTES - total_bytes - id_bytes) {
            free_memos(copy, count);
            free(order);
            return false;
        }
        total_bytes += id_bytes + text_bytes;
        copy[index].id = copy_string(source->id);
        copy[index].text = copy_string(source->text);
        if (!copy[index].id || !copy[index].text) {
            free_memos(copy, count);
            free(order);
            return false;
        }
        copy[index].date = source->date;
        copy[index].created_at_ms = source->created_at_ms;
        copy[index].x = source->has_position
                            ? clamp_position(source->x, -230.0f, 230.0f) : 0.0f;
        copy[index].y = source->has_position
                            ? clamp_position(source->y, -80.0f, 180.0f) : 53.0f;
        copy[index].read = source->read;
        copy[index].source_index = index;
        order[index] = (BoardMemoOrder){index, source->created_at_ms};
    }
    if (count > 1) qsort(order, count, sizeof(*order), compare_memo_order);
    WmBoardDate *message_dates = count ? malloc(count * sizeof(*message_dates)) : NULL;
    if (count && !message_dates) {
        free_memos(copy, count);
        free(order);
        return false;
    }
    for (size_t index = 0; index < count; index++) {
        message_dates[index] = copy[index].date;
    }
    bool dates_updated = wm_board_calendar_set_message_dates(
        board->calendar, message_dates, count);
    free(message_dates);
    if (!dates_updated) {
        free_memos(copy, count);
        free(order);
        return false;
    }
    free_memos(board->memos, board->memo_count);
    free(board->memo_order);
    board->memos = copy;
    board->memo_order = order;
    board->memo_count = count;
    board->selected = SIZE_MAX;
    board->dragging = false;
    board->dragged_index = SIZE_MAX;
    wm_board_reader_scroll_reset(&board->reader_scroll);
    board->page = 0;
    board->card_age = 100000.0f;
    board->arriving_memo = SIZE_MAX;
    update_visible(board);
    return true;
}

bool wm_board_scene_open(WmBoardScene *board, WmBoardDate date) {
    if (!board || board->phase != WM_BOARD_CLOSED ||
        !wm_board_date_valid(date)) return false;
    if (!same_date(board->date, date)) reset_pin_choices(board);
    board->date = date;
    board->today = date;
    board->page = 0;
    board->selected = SIZE_MAX;
    board->dragging = false;
    board->dragged_index = SIZE_MAX;
    wm_board_reader_scroll_reset(&board->reader_scroll);
    board->phase = WM_BOARD_ENTER;
    board->phase_frame = 0.0f;
    board->age = 0.0f;
    board->card_age = 100000.0f;
    board->arriving_memo = SIZE_MAX;
    board->hover = (WmBoardHit){WM_BOARD_CONTROL_NONE, SIZE_MAX};
    memset(board->button_focus, 0, sizeof(board->button_focus));
    memset(board->card_focus, 0, sizeof(board->card_focus));
    board->pending_action = WM_BOARD_ACTION_NONE;
    board->mask_direction = 0;
    board->mask_age = 0.0f;
    update_visible(board);
    return true;
}

void wm_board_scene_set_grid_page(WmBoardScene *board, int page) {
    if (board && page >= 0 && page < 4) board->grid_page = page;
}

bool wm_board_scene_sd_visible(const WmBoardScene *board) {
    return board && board->phase == WM_BOARD_EXIT &&
           board->phase_frame >= 20.0f;
}

WmBoardPhase wm_board_scene_phase(const WmBoardScene *board) {
    return board ? board->phase : WM_BOARD_CLOSED;
}

WmBoardChild wm_board_scene_child(const WmBoardScene *board) {
    if (!board) return WM_BOARD_CHILD_NONE;
    if (wm_board_calendar_phase(board->calendar) != WM_CALENDAR_CLOSED) {
        return WM_BOARD_CHILD_CALENDAR;
    }
    if (wm_board_compose_phase(board->compose) != WM_COMPOSE_CLOSED) {
        return WM_BOARD_CHILD_COMPOSE;
    }
    if (wm_board_erase_phase(board->erase) != WM_ERASE_CLOSED) {
        return WM_BOARD_CHILD_ERASE;
    }
    return WM_BOARD_CHILD_NONE;
}

bool wm_board_scene_compose_editor_active(const WmBoardScene *board) {
    return board &&
        (wm_board_compose_phase(board->compose) == WM_COMPOSE_EDIT ||
         wm_board_compose_address_editor_active(board->compose));
}

bool wm_board_scene_compose_keyboard_overlay_visible(
    const WmBoardScene *board) {
    return board &&
        wm_board_compose_keyboard_overlay_visible(board->compose);
}

bool wm_board_scene_address_editor_active(const WmBoardScene *board) {
    return board && wm_board_compose_address_editor_active(board->compose);
}

WmBoardDate wm_board_scene_date(const WmBoardScene *board) {
    return board ? board->date : (WmBoardDate){0, 0, 0};
}

size_t wm_board_scene_memo_page(const WmBoardScene *board) {
    return board ? board->page : 0;
}

size_t wm_board_scene_memo_page_count(const WmBoardScene *board) {
    if (!board || board->day_count == 0) return 1;
    return (board->day_count + BOARD_MEMOS_PER_PAGE - 1) /
           BOARD_MEMOS_PER_PAGE;
}

unsigned wm_board_scene_today_count(const WmBoardScene *board) {
    if (!board) return 0;
    unsigned found = 0;
    for (size_t index = 0; index < board->memo_count; index++) {
        if (same_date(board->memos[index].date, board->today) && found < 99) found++;
    }
    return found;
}

unsigned wm_board_scene_today_unread_count(const WmBoardScene *board) {
    if (!board) return 0;
    unsigned found = 0;
    for (size_t index = 0; index < board->memo_count; index++) {
        if (same_date(board->memos[index].date, board->today) &&
            !board->memos[index].read) found++;
    }
    return found;
}

bool wm_board_scene_refresh_today(WmBoardScene *board, WmBoardDate today) {
    if (!board || board->phase != WM_BOARD_CLOSED ||
        !wm_board_date_valid(today)) return false;
    if (!same_date(board->today, today)) reset_pin_choices(board);
    board->today = today;
    return true;
}

size_t wm_board_scene_memo_count(const WmBoardScene *board) {
    return board ? board->memo_count : 0;
}

bool wm_board_scene_get_memo(const WmBoardScene *board, size_t index,
                              WmBoardMemo *memo) {
    if (!board || !memo || index >= board->memo_count) return false;
    const BoardMemo *source = &board->memos[index];
    *memo = (WmBoardMemo){
        .id = source->id,
        .text = source->text,
        .date = source->date,
        .created_at_ms = source->created_at_ms,
        .x = source->x,
        .y = source->y,
        .has_position = true,
        .read = source->read
    };
    return true;
}

const char *wm_board_scene_last_erased_id(const WmBoardScene *board) {
    return board ? board->last_erased_id : NULL;
}

WmBoardAction wm_board_scene_take_action(WmBoardScene *board,
                                         size_t *memo_index) {
    if (!board) return WM_BOARD_ACTION_NONE;
    WmBoardAction action = board->pending_action;
    if (memo_index) *memo_index = board->pending_memo_index;
    board->pending_action = WM_BOARD_ACTION_NONE;
    return action;
}

bool wm_board_scene_insert_text(WmBoardScene *board, const char *utf8) {
    if (!board || wm_board_compose_phase(board->compose) == WM_COMPOSE_CLOSED)
        return false;
    bool inserted = wm_board_compose_insert_text(board->compose, utf8);
    if (inserted) wm_board_compose_press_physical(board->compose, utf8);
    return inserted;
}

const char *wm_board_scene_take_compose_key_cue(WmBoardScene *board) {
    return board ? wm_board_compose_take_key_cue(board->compose) : NULL;
}

static WmBoardComposeControl compose_control(WmBoardControl control);

bool wm_board_scene_hold_compose_control(WmBoardScene *board,
                                          WmBoardControl control) {
    return board && wm_board_compose_hold_control(board->compose,
                                                   compose_control(control));
}

void wm_board_scene_release_compose_control(WmBoardScene *board) {
    if (board) wm_board_compose_release_control(board->compose);
}

bool wm_board_scene_backspace(WmBoardScene *board) {
    if (!board || wm_board_compose_phase(board->compose) == WM_COMPOSE_CLOSED)
        return false;
    wm_board_compose_press_physical(board->compose, "\b");
    return wm_board_compose_backspace(board->compose);
}

bool wm_board_scene_finish_edit(WmBoardScene *board) {
    return board && wm_board_compose_phase(board->compose) != WM_COMPOSE_CLOSED &&
           wm_board_compose_finish_edit(board->compose);
}

static float clamp_pan(float value) {
    return fminf(1.0f, fmaxf(-1.0f, value));
}

static void queue_drag_cue(WmBoardScene *board, WmBoardDragCue cue, float pan) {
    if (board->drag_cue_count >= sizeof(board->drag_cues) /
                                  sizeof(board->drag_cues[0])) return;
    unsigned index = board->drag_cue_count++;
    board->drag_cues[index] = cue;
    board->drag_cue_pans[index] = pan;
}

bool wm_board_scene_dragging(const WmBoardScene *board) {
    return board && board->dragging;
}

bool wm_board_scene_pointer_down(WmBoardScene *board, WmBoardHit hit,
                                  int x, int y) {
    if (!board || board->phase != WM_BOARD_READY || board->dragging ||
        hit.control != WM_BOARD_CONTROL_MEMO ||
        hit.memo_index >= board->memo_count ||
        visible_position(board, hit.memo_index) == SIZE_MAX ||
        (hit.memo_index == board->arriving_memo &&
         board->card_age < 11.0f)) return false;
    board->dragging = true;
    board->dragged_index = hit.memo_index;
    board->drag_start_x = x;
    board->drag_start_y = y;
    board->drag_delta_x = 0.0f;
    board->drag_delta_y = 0.0f;
    board->drag_last_x = 0.0f;
    board->drag_last_y = 0.0f;
    board->drag_gain = 0.0f;
    board->drag_pan = clamp_pan(board->memos[hit.memo_index].x / 304.0f);
    promote_card(board, hit.memo_index);
    queue_drag_cue(board, WM_BOARD_DRAG_CUE_HOLD, board->drag_pan);
    return true;
}

bool wm_board_scene_pointer_move(WmBoardScene *board, int x, int y) {
    if (!board || !board->dragging) return false;
    /* HTML centeredPoint uses 832 logical X units. Converting the 640-pixel
     * raster delta back through its 832/608 IPL root gives 608/640. */
    board->drag_delta_x = ((float)x - (float)board->drag_start_x) *
                          (608.0f / (float)WM_FRAME_WIDTH);
    board->drag_delta_y = (float)board->drag_start_y - (float)y;
    return true;
}

bool wm_board_scene_pointer_up(WmBoardScene *board, int x, int y) {
    if (!board || !board->dragging ||
        board->dragged_index >= board->memo_count) return false;
    wm_board_scene_pointer_move(board, x, y);
    return wm_board_scene_pointer_finish(board);
}

bool wm_board_scene_pointer_finish(WmBoardScene *board) {
    if (!board || !board->dragging ||
        board->dragged_index >= board->memo_count) return false;
    size_t index = board->dragged_index;
    BoardMemo *memo = &board->memos[index];
    memo->x = clamp_position(memo->x + board->drag_delta_x,
                              -230.0f, 230.0f);
    memo->y = clamp_position(memo->y + board->drag_delta_y,
                              -80.0f, 180.0f);
    board->dragging = false;
    board->dragged_index = SIZE_MAX;
    board->drag_gain = 0.0f;
    board->drag_pan = clamp_pan(memo->x / 304.0f);
    queue_drag_cue(board, WM_BOARD_DRAG_CUE_RELEASE, board->drag_pan);
    board->pending_action = WM_BOARD_ACTION_MEMO_MOVED;
    board->pending_memo_index = index;
    return true;
}

bool wm_board_scene_cancel_pointer(WmBoardScene *board) {
    wm_board_scene_release_compose_control(board);
    if (!board || !board->dragging) return false;
    board->dragging = false;
    board->dragged_index = SIZE_MAX;
    board->drag_gain = 0.0f;
    board->drag_pan = 0.0f;
    queue_drag_cue(board, WM_BOARD_DRAG_CUE_RELEASE, 0.0f);
    return true;
}

WmBoardDragCue wm_board_scene_take_drag_cue(WmBoardScene *board, float *pan) {
    if (!board || !board->drag_cue_count) return WM_BOARD_DRAG_CUE_NONE;
    WmBoardDragCue cue = board->drag_cues[0];
    if (pan) *pan = board->drag_cue_pans[0];
    board->drag_cue_count--;
    if (board->drag_cue_count) {
        memmove(board->drag_cues, board->drag_cues + 1,
                board->drag_cue_count * sizeof(board->drag_cues[0]));
        memmove(board->drag_cue_pans, board->drag_cue_pans + 1,
                board->drag_cue_count * sizeof(board->drag_cue_pans[0]));
    }
    return cue;
}

bool wm_board_scene_drag_mix(const WmBoardScene *board, float *gain,
                              float *pan) {
    if (!board || !board->dragging || !gain || !pan) return false;
    *gain = board->drag_gain;
    *pan = board->drag_pan;
    return true;
}

float wm_board_scene_reader_scroll_offset(const WmBoardScene *board) {
    return board ? wm_board_reader_scroll_offset(&board->reader_scroll) : 0.0f;
}

float wm_board_scene_reader_scroll_limit(const WmBoardScene *board) {
    return board ? wm_board_reader_scroll_limit(&board->reader_scroll) : 0.0f;
}

bool wm_board_scene_reader_scroll_sound_active(const WmBoardScene *board) {
    return board &&
           wm_board_reader_scroll_sound_active(&board->reader_scroll);
}

const char *wm_board_scene_take_reader_cue(WmBoardScene *board) {
    if (!board) return NULL;
    const char *cue = board->pending_reader_cue;
    board->pending_reader_cue = NULL;
    return cue;
}

bool wm_board_scene_reader_arrow_target_visible(const WmBoardScene *board,
                                                 WmBoardControl control) {
    if (!board) return false;
    WmBoardReaderArrow arrow =
        control == WM_BOARD_CONTROL_MEMO_SCROLL_UP
            ? WM_BOARD_READER_ARROW_UP :
        control == WM_BOARD_CONTROL_MEMO_SCROLL_DOWN
            ? WM_BOARD_READER_ARROW_DOWN : WM_BOARD_READER_ARROW_NONE;
    return wm_board_reader_scroll_arrow_visible(&board->reader_scroll, arrow);
}

bool wm_board_scene_grid_overlay(const WmBoardScene *board, float *grid_frame) {
    if (!board || !grid_frame) return false;
    if (board->phase == WM_BOARD_ENTER && board->phase_frame < 20.0f) {
        *grid_frame = 70.0f + board->phase_frame;
        return true;
    }
    if (board->phase == WM_BOARD_EXIT) {
        *grid_frame = 100.0f + clamp_frame(board->phase_frame, 20.0f);
        return true;
    }
    return false;
}

static unsigned weekday(WmBoardDate date) {
    int64_t value = civil_day(date) + 4; /* 1970-01-01 was Thursday. */
    int64_t remainder = value % 7;
    return (unsigned)(remainder < 0 ? remainder + 7 : remainder);
}

static void date_text(WmBoardDate date, int day_offset, char buffer[32]) {
    static const char *const weekdays[7] = {
        "Sun", "Mon", "Tue", "Wed", "Thu", "Fri", "Sat"
    };
    WmBoardDate displayed = date_from_day(civil_day(date) + day_offset);
    snprintf(buffer, 32, "%s %d/%d", weekdays[weekday(displayed)],
             displayed.month, displayed.day);
}

static void matrix_translation(float x, float y, float matrix[12]) {
    static const float identity[12] = {
        1, 0, 0, 0,
        0, 1, 0, 0,
        0, 0, 1, 0
    };
    memcpy(matrix, identity, sizeof(identity));
    matrix[3] = x;
    matrix[7] = y;
}

static void present(WmBoardScene *board, const WmLayout *layout,
                    const float matrix[12]) {
    wm_layout_present_with_fonts(board->platform, board->textures,
                                 board->fonts, layout, true,
                                 WM_LAYOUT_IPL, matrix);
}

static void append_clip(WmLayoutClip clips[BOARD_CLIP_CAPACITY],
                        size_t *count, const char *animation,
                        const char *group, float frame) {
    if (*count >= BOARD_CLIP_CAPACITY) return;
    clips[*count] = (WmLayoutClip){
        .animation = animation,
        .group = group,
        .frame = frame,
        .loop_override = 0
    };
    (*count)++;
}

static bool capture_scroll_offset(void *context,
                                  const WmLayoutPaneView *pane) {
    if (strcmp(pane->name, "N_TopBack") == 0) {
        *(float *)context = pane->matrix[3];
    }
    return true;
}

static void pose_background(WmBoardScene *board) {
    /* The authored base N_TopBack X is 121.6. Frame zero places it at the
     * screen center, which both the grid and Board use for the bottom date. */
    bool returning = board->phase == WM_BOARD_EXIT &&
                     board->return_direction != 0;
    bool scrolling = board->phase == WM_BOARD_DATE_SCROLL || returning;
    int direction = returning ? board->return_direction : board->direction;
    WmLayoutClip clip = {
        .animation = "my_IplTop_c",
        .frame = scrolling
                     ? (direction > 0 ? 30.0f : 0.0f) +
                       clamp_frame(board->phase_frame, 20.0f)
                     : 0.0f,
        .loop_override = 0
    };
    wm_layout_pose(board->background, &clip, 1);
    char before[32], current[32], after[32];
    date_text(board->date, -1, before);
    date_text(board->date, 0, current);
    date_text(board->date, 1, after);
    if (returning) {
        /* One authored page slide returns to today even when the selected
         * date is several days away. The entering label names the real date. */
        if (direction < 0) date_text(board->today, 0, before);
        else date_text(board->today, 0, after);
    }
    wm_layout_set_pose_text(board->background, "T_Day_a", before);
    /* On entry, idle and exit the visible date is retained above the Board
     * footer. Draw it once there so an opaque footer cannot cover it. */
    bool retain_date = board->phase == WM_BOARD_ENTER ||
                       board->phase == WM_BOARD_READY ||
                       (board->phase == WM_BOARD_EXIT && !returning);
    wm_layout_set_pose_text(board->background, "T_Day_b",
                            retain_date ? "" : current);
    wm_layout_set_pose_text(board->background, "T_Day_c", after);
    board->scroll_offset_x = 0.0f;
    if (scrolling) {
        WmLayoutDrawOptions options = {
            .wide = true,
            .mode = WM_LAYOUT_IPL,
            .alpha = 1.0f,
            .on_pane = capture_scroll_offset,
            .context = &board->scroll_offset_x
        };
        wm_layout_draw(board->background, &options);
    }
}

static size_t utf8_unit_length(const char *text, size_t offset,
                               unsigned *utf16_units) {
    unsigned char lead = (unsigned char)text[offset];
    if (lead < 0x80) {
        *utf16_units = 1;
        return 1;
    }
    size_t bytes = lead >= 0xF0 && lead <= 0xF4 ? 4 :
                   lead >= 0xE0 && lead <= 0xEF ? 3 :
                   lead >= 0xC2 && lead <= 0xDF ? 2 : 1;
    for (size_t index = 1; index < bytes; index++) {
        if (!text[offset + index] ||
            ((unsigned char)text[offset + index] & 0xC0) != 0x80) {
            bytes = 1;
            break;
        }
    }
    *utf16_units = bytes == 4 ? 2 : 1;
    return bytes;
}

static void thumbnail_text(const char *text, char buffer[64]) {
    size_t offset = 0;
    unsigned units = 0;
    while (text[offset] && text[offset] != '\n' && units < 6) {
        unsigned next_units = 0;
        size_t bytes = utf8_unit_length(text, offset, &next_units);
        if (units + next_units > 6) break;
        units += next_units;
        offset += bytes;
    }
    memcpy(buffer, text, offset);
    buffer[offset] = '\0';
    if (text[offset] && text[offset] != '\n') strcat(buffer, "...");
}

static bool memo_exit_phase(WmBoardPhase phase) {
    return phase == WM_BOARD_MEMO_CLOSE ||
           phase == WM_BOARD_MEMO_ERASE_CLOSE;
}

static bool memo_reader_phase(WmBoardPhase phase) {
    return phase == WM_BOARD_MEMO_OPEN ||
           phase == WM_BOARD_MEMO_READ ||
           phase == WM_BOARD_MEMO_BACK_SELECT ||
           phase == WM_BOARD_MEMO_CLOSE ||
           phase == WM_BOARD_MEMO_TRASH_SELECT ||
           phase == WM_BOARD_MEMO_DIALOG ||
           phase == WM_BOARD_MEMO_TRASH_CANCEL ||
           phase == WM_BOARD_MEMO_ERASE_CLOSE;
}

static void pose_card(WmBoardScene *board,
                      const WmBoardMemoPresentation *card, bool neutral) {
    WmLayoutClip clips[BOARD_CLIP_CAPACITY];
    size_t count = 0;
    append_clip(clips, &count, "LetterS_a_PasteLetter", NULL,
                card->paste_frame);
    size_t position = card->entering ? SIZE_MAX :
                      visible_position(board, card->memo_index);
    BoardFocus *focus = !neutral && position < BOARD_MEMOS_PER_PAGE
                            ? &board->card_focus[position] : NULL;
    if (focus && focus->active) {
        append_clip(clips, &count,
                    focus->entering ? "LetterS_a_FocusIn" :
                                      "LetterS_a_FocusOut",
                    NULL, clamp_frame(focus->frame, 6.0f));
    }
    if (!neutral && card->next_page_frame >= 0.0f) {
        append_clip(clips, &count, "LetterS_a_NextPage", NULL,
                    card->next_page_frame);
    }
    if (!neutral && !card->entering &&
        board->selected == card->memo_index &&
        memo_reader_phase(board->phase)) {
        if (board->phase == WM_BOARD_MEMO_CLOSE &&
            board->phase_frame >= 16.0f) {
            /* ExitLetter ends at the focused card size. Let its authored
             * FocusOut return the card to neutral before releasing it. */
            append_clip(clips, &count, "LetterS_a_FocusOut", NULL,
                        clamp_frame(board->phase_frame - 16.0f, 6.0f));
        } else {
            bool closing = memo_exit_phase(board->phase);
            float frame = board->phase == WM_BOARD_MEMO_OPEN || closing
                              ? clamp_frame(board->phase_frame, 16.0f)
                              : 16.0f;
            append_clip(clips, &count,
                        closing ? "LetterS_a_ExitLetter" :
                                  "LetterS_a_SelectLetter",
                        NULL, frame);
        }
    }
    const char *pin_animation = card->pin_kind == WM_BOARD_PIN_NEW
                                    ? "LetterS_a_NewAnim"
                                    : card->pin_kind == WM_BOARD_PIN_DEFAULT
                                          ? "LetterS_a_DefAnim" : NULL;
    if (pin_animation) {
        /* Home's parked presentation uses the source's settled-card age. */
        float frame = neutral ? 100000.0f : board->pin_age;
        append_clip(clips, &count, pin_animation, "G_New", frame);
        clips[count - 1].loop_override = 1;
    }
    wm_layout_pose(board->card, clips, count);
    char thumbnail[64];
    thumbnail_text(board->memos[card->memo_index].text, thumbnail);
    wm_layout_set_pose_text(board->card, "T_Letter", thumbnail);
    wm_layout_set_pane_visible(board->card, "Nigaoe", false);
}

static void card_matrix(const WmBoardScene *board, size_t position,
                        float matrix[12]) {
    const BoardMemo *memo = &board->memos[board->visible[position]];
    float x = memo->x;
    float y = memo->y;
    if (board->phase == WM_BOARD_MEMO_PAGE) {
        float progress = clamp_frame(board->phase_frame, 15.0f) / 15.0f;
        float target = board->direction < 0 ? 304.0f : -304.0f;
        x += (target - x) * progress;
        y += (53.0f - y) * progress;
    }
    if (board->dragging && board->visible[position] == board->dragged_index) {
        x += board->drag_delta_x;
        y += board->drag_delta_y;
    }
    matrix_translation(x * (832.0f / 608.0f) + board->scroll_offset_x,
                       y, matrix);
}

static void append_visible_card(
    WmBoardScene *board, size_t memo_index,
    WmBoardMemoPresentation cards[WM_BOARD_MAX_PRESENTED_MEMOS],
    size_t *count) {
    size_t position = visible_position(board, memo_index);
    if (position == SIZE_MAX || *count >= WM_BOARD_MAX_PRESENTED_MEMOS ||
        (board->phase == WM_BOARD_MEMO_ERASE_CLOSE &&
         memo_index == board->selected)) return;
    float matrix[12];
    card_matrix(board, position, matrix);
    cards[(*count)++] = (WmBoardMemoPresentation){
        .memo_index = memo_index,
        .x = matrix[3],
        .y = matrix[7],
        .paste_frame = memo_index == board->arriving_memo
                           ? clamp_frame(board->card_age, 10.0f) : 10.0f,
        .next_page_frame = board->phase == WM_BOARD_MEMO_PAGE
                               ? clamp_frame(board->phase_frame, 14.0f) : -1.0f,
        .pin_kind = sample_pin_kind(board, memo_index)
    };
}

static void append_entering_cards(
    WmBoardScene *board, WmBoardDate date, size_t page,
    int direction, bool date_slide,
    WmBoardMemoPresentation cards[WM_BOARD_MAX_PRESENTED_MEMOS],
    size_t *count) {
    size_t first = page * BOARD_MEMOS_PER_PAGE;
    size_t found = 0;
    size_t entered = 0;
    float progress = clamp_frame(board->phase_frame, 15.0f) / 15.0f;
    float start_x = direction < 0 ? -304.0f : 304.0f;
    float page_offset = direction < 0 ? -832.0f : 832.0f;
    for (size_t position = 0; position < board->memo_count &&
                           entered < BOARD_MEMOS_PER_PAGE; position++) {
        size_t index = board->memo_order[position].index;
        const BoardMemo *memo = &board->memos[index];
        if (!same_date(memo->date, date)) continue;
        if (found++ < first) continue;
        if (*count >= WM_BOARD_MAX_PRESENTED_MEMOS) break;
        float x = date_slide ? memo->x :
                  start_x + (memo->x - start_x) * progress;
        float y = date_slide ? memo->y :
                  53.0f + (memo->y - 53.0f) * progress;
        cards[(*count)++] = (WmBoardMemoPresentation){
            .memo_index = index,
            .x = x * (832.0f / 608.0f) + board->scroll_offset_x +
                 (date_slide ? page_offset : 0.0f),
            .y = y,
            .paste_frame = 10.0f,
            .next_page_frame = -1.0f,
            .pin_kind = sample_pin_kind(board, index),
            .entering = true
        };
        entered++;
    }
}

size_t wm_board_scene_memo_presentation(
    WmBoardScene *board,
    WmBoardMemoPresentation cards[WM_BOARD_MAX_PRESENTED_MEMOS]) {
    if (!board || !cards || board->phase == WM_BOARD_CLOSED) return 0;
    pose_background(board);
    size_t count = 0;
    for (size_t index = 0; index < board->visible_count; index++) {
        size_t memo_index = board->card_order[index];
        if (board->dragging && memo_index == board->dragged_index) continue;
        append_visible_card(board, memo_index, cards, &count);
    }
    if (board->dragging) {
        append_visible_card(board, board->dragged_index, cards, &count);
    }
    if (board->phase == WM_BOARD_MEMO_PAGE) {
        size_t next_page = board->direction < 0
                               ? board->page + 1 : board->page - 1;
        append_entering_cards(board, board->date, next_page,
                              board->direction, false, cards, &count);
    } else if (board->phase == WM_BOARD_DATE_SCROLL) {
        append_entering_cards(board, board->next_date, 0,
                              board->direction, true, cards, &count);
    } else if (board->phase == WM_BOARD_EXIT &&
               board->return_direction != 0) {
        append_entering_cards(board, board->today, 0,
                              board->return_direction, true, cards, &count);
    }
    return count;
}

size_t wm_board_scene_parked_memo_presentation(
    WmBoardScene *board, WmBoardDate today,
    WmBoardMemoPresentation cards[WM_BOARD_MAX_PRESENTED_MEMOS]) {
    if (!board || !cards || board->phase != WM_BOARD_CLOSED ||
        !wm_board_date_valid(today)) return 0;
    size_t count = 0;
    for (size_t position = 0; position < board->memo_count &&
                           count < BOARD_MEMOS_PER_PAGE; position++) {
        size_t index = board->memo_order[position].index;
        const BoardMemo *memo = &board->memos[index];
        if (!same_date(memo->date, today)) continue;
        cards[count++] = (WmBoardMemoPresentation){
            .memo_index = index,
            .x = memo->x * (832.0f / 608.0f),
            .y = memo->y,
            .paste_frame = 10.0f,
            .next_page_frame = -1.0f,
            .pin_kind = sample_pin_kind(board, index)
        };
    }
    return count;
}

static void card_camera_matrix(const WmBoardMemoPresentation *card,
                               const float camera[12], float matrix[12]) {
    matrix_translation(card->x, card->y, matrix);
    if (!camera) return;
    memcpy(matrix, camera, 12 * sizeof(*matrix));
    matrix[3] += camera[0] * card->x + camera[1] * card->y;
    matrix[7] += camera[4] * card->x + camera[5] * card->y;
    matrix[11] += camera[8] * card->x + camera[9] * card->y;
}

void wm_board_scene_draw_parked_memos(WmBoardScene *board,
                                       WmBoardDate today,
                                       const float camera[12]) {
    WmBoardMemoPresentation cards[WM_BOARD_MAX_PRESENTED_MEMOS];
    size_t count = wm_board_scene_parked_memo_presentation(board, today,
                                                            cards);
    for (size_t index = 0; index < count; index++) {
        pose_card(board, &cards[index], true);
        float matrix[12];
        card_camera_matrix(&cards[index], camera, matrix);
        present(board, board->card, matrix);
    }
}

static bool reading_memo(const WmBoardScene *board) {
    return memo_reader_phase(board->phase);
}

static size_t reader_line_count(WmBoardScene *board, const char *text) {
    WmFontPane pane;
    const char *font_name = NULL;
    if (wm_layout_pane_font(board->reader, "T_Letter", &pane, &font_name)) {
        WmCachedFont *face = wm_font_cache_resolve(board->fonts, font_name);
        const WmFontTextLayout *layout = face
            ? wm_font_cache_layout(face, text, &pane) : NULL;
        if (layout) return wm_font_text_layout_line_count(layout);
    }
    /* Missing local font exports still preserve explicit line breaks. */
    size_t lines = 1;
    for (const char *cursor = text; *cursor; cursor++) {
        if (*cursor == '\n') lines++;
    }
    return lines;
}

static bool configure_reader_scroll(WmBoardScene *board, const char *text) {
    WmLayoutPaneState body;
    WmLayoutPaneState header;
    WmLayoutPaneState footer;
    if (!wm_layout_pane_state(board->reader, "N_Body", &body) ||
        !wm_layout_pane_state(board->reader, "N_Header", &header) ||
        !wm_layout_pane_state(board->reader, "N_Footer", &footer)) {
        return false;
    }
    return wm_board_reader_scroll_configure(
        &board->reader_scroll, reader_line_count(board, text),
        body.size[1], header.size[1], footer.size[1]);
}

static void pose_reader(WmBoardScene *board) {
    WmLayoutClip clips[8] = {
        {
            .animation = memo_exit_phase(board->phase)
                             ? "my_Memo_a_ExitLetter" :
                               "my_Memo_a_SelectLetter",
            .frame = board->phase == WM_BOARD_MEMO_OPEN ||
                     memo_exit_phase(board->phase)
                         ? clamp_frame(board->phase_frame, 16.0f) : 16.0f,
            .loop_override = 0
        },
        {
            .animation = "my_Memo_a_Loop",
            .group = "G_ArwRoop",
            .frame = fmodf(board->age, 55.0f),
            .loop_override = 1
        }
    };
    size_t extra = wm_board_reader_scroll_clips(&board->reader_scroll,
                                                  clips + 2, 6);
    wm_layout_pose(board->reader, clips, 2 + extra);
    /* The source text panes contain visible runs of placeholder 'i' glyphs.
     * The HTML reader clears every authored text pane before filling the two
     * reader labels; leaving these defaults overlays the posted Memo. */
    wm_layout_set_pose_text(board->reader, "T_2l_TextBox", "");
    wm_layout_set_pose_text(board->reader, "T_TouchLetter", "");
    wm_layout_set_pose_text(board->reader, "T_Nigaoe", "");
    wm_layout_set_pose_text(board->reader, "T_Header", "Memo");
    wm_layout_set_pose_text(board->reader, "T_Letter",
                             board->memos[board->selected].text);
    wm_layout_set_pane_visible(board->reader, "Nigaoe", false);
    wm_layout_set_pane_visible(board->reader, "B_Nigaoe", false);
    wm_layout_set_pane_translation(board->reader, "N_Memo", 0.0f,
                  wm_board_reader_scroll_offset(&board->reader_scroll), 0.0f);
    wm_layout_set_pane_translation(board->reader, "N_Footer", 0.0f,
                  wm_board_reader_scroll_footer_shift(&board->reader_scroll),
                  0.0f);
}

static bool reader_body_pane(void *context, const WmLayoutPaneView *pane) {
    (void)context;
    static const char *const names[] = {
        "RootPane", "N_Memo", "N_MemoRoot", "N_Body", "Body_s", "Body3",
        "Picture_11", "Picture_12", "Picture_13", "Body3_04", "B_Body"
    };
    for (size_t index = 0; index < sizeof(names) / sizeof(names[0]); index++) {
        if (strcmp(pane->name, names[index]) == 0) return true;
    }
    return false;
}

typedef enum ReaderDrawPart {
    READER_DRAW_HEADER_AND_BODY,
    READER_DRAW_REST
} ReaderDrawPart;

typedef enum ReaderBranch {
    READER_BRANCH_COMMON,
    READER_BRANCH_HEADER,
    READER_BRANCH_BODY,
    READER_BRANCH_REST
} ReaderBranch;

typedef struct ReaderDrawFilter {
    ReaderDrawPart part;
    ReaderBranch branch;
} ReaderDrawFilter;

static bool reader_layer_pane(void *context, const WmLayoutPaneView *pane) {
    ReaderDrawFilter *filter = context;
    if (strcmp(pane->name, "RootPane") == 0 ||
        strcmp(pane->name, "N_Memo") == 0 ||
        strcmp(pane->name, "N_MemoRoot") == 0) {
        filter->branch = READER_BRANCH_COMMON;
        return true;
    }
    if (strcmp(pane->name, "N_Header") == 0) {
        filter->branch = READER_BRANCH_HEADER;
    } else if (strcmp(pane->name, "N_Body") == 0) {
        filter->branch = READER_BRANCH_BODY;
    } else if (strcmp(pane->name, "N_Footer") == 0 ||
               strcmp(pane->name, "Nigaoe") == 0 ||
               strcmp(pane->name, "B_Nigaoe") == 0 ||
               strcmp(pane->name, "T_2l_TextBox") == 0 ||
               strcmp(pane->name, "B_2l_TextBox") == 0 ||
               strcmp(pane->name, "T_TouchLetter") == 0 ||
               strcmp(pane->name, "T_Letter") == 0 ||
               strcmp(pane->name, "T_Nigaoe") == 0 ||
               strcmp(pane->name, "N_TopBtn") == 0) {
        filter->branch = READER_BRANCH_REST;
    }
    if (filter->branch == READER_BRANCH_COMMON) return true;
    if (filter->part == READER_DRAW_HEADER_AND_BODY) {
        return filter->branch == READER_BRANCH_HEADER ||
               filter->branch == READER_BRANCH_BODY;
    }
    return filter->branch == READER_BRANCH_REST;
}

static void draw_reader_body_rows(WmBoardScene *board,
                                  const float matrix[12]) {
    size_t rows = wm_board_reader_scroll_repeated_rows(
        &board->reader_scroll);
    float height = board->reader_scroll.line_height;
    if (!rows || height <= 0.0f) return;
    /* Body strips are identical authored panes. Draw only strips intersecting
     * the 456-unit reader viewport, keeping long memos at bounded draw cost. */
    float origin = matrix[7] +
                   wm_board_reader_scroll_offset(&board->reader_scroll);
    float first_float = ceilf((origin - 280.0f) / height);
    float last_float = floorf((origin + 280.0f) / height);
    if (last_float < 1.0f || first_float > (float)rows) return;
    size_t first = first_float > 1.0f ? (size_t)first_float : 1;
    size_t last = last_float < (float)rows ? (size_t)last_float : rows;
    for (size_t row = first; row <= last; row++) {
        wm_layout_set_pane_translation(board->reader, "N_Body", 0.0f,
                                         -height * (float)row, 0.0f);
        wm_layout_present_filtered_with_fonts(
            board->platform, board->textures, board->fonts, board->reader,
            true, WM_LAYOUT_IPL, matrix, reader_body_pane, NULL);
    }
    wm_layout_set_pane_translation(board->reader, "N_Body", 0.0f, 0.0f, 0.0f);
}

static void draw_reader(WmBoardScene *board) {
    if (!reading_memo(board) || board->selected >= board->memo_count) return;
    WmLayoutClip mask = {
        .animation = memo_exit_phase(board->phase)
                         ? "my_BbsMask_a_MaskOut" :
                           "my_BbsMask_a_MaskIn",
        .frame = board->phase == WM_BOARD_MEMO_OPEN ||
                 memo_exit_phase(board->phase)
                     ? clamp_frame(board->phase_frame, 20.0f) : 20.0f,
        .loop_override = 0
    };
    wm_layout_pose(board->mask, &mask, 1);
    present(board, board->mask, NULL);

    if (memo_exit_phase(board->phase) && board->phase_frame >= 17.0f) return;
    pose_reader(board);
    const BoardMemo *memo = &board->memos[board->selected];
    float fraction = board->phase == WM_BOARD_MEMO_OPEN
                         ? 1.0f - clamp_frame(board->phase_frame, 17.0f) / 17.0f
                         : memo_exit_phase(board->phase)
                               ? clamp_frame(board->phase_frame, 17.0f) / 17.0f
                               : 0.0f;
    float matrix[12];
    matrix_translation(memo->x * fraction * (832.0f / 608.0f),
                       memo->y * fraction, matrix);
    ReaderDrawFilter first = {READER_DRAW_HEADER_AND_BODY,
                               READER_BRANCH_COMMON};
    wm_layout_present_filtered_with_fonts(
        board->platform, board->textures, board->fonts, board->reader,
        true, WM_LAYOUT_IPL, matrix, reader_layer_pane, &first);
    draw_reader_body_rows(board, matrix);
    ReaderDrawFilter rest = {READER_DRAW_REST, READER_BRANCH_COMMON};
    wm_layout_present_filtered_with_fonts(
        board->platform, board->textures, board->fonts, board->reader,
        true, WM_LAYOUT_IPL, matrix, reader_layer_pane, &rest);
}

static void draw_child_mask(WmBoardScene *board) {
    if (board->mask_direction == 0) return;
    WmLayoutClip clip = {
        .animation = board->mask_direction > 0
                         ? "my_BbsMask_a_MaskIn" :
                           "my_BbsMask_a_MaskOut",
        .frame = clamp_frame(board->mask_age, 20.0f),
        .loop_override = 0
    };
    wm_layout_pose(board->mask, &clip, 1);
    present(board, board->mask, NULL);
}

void wm_board_scene_draw_body(WmBoardScene *board) {
    if (!board || board->phase == WM_BOARD_CLOSED) return;
    WmBoardMemoPresentation cards[WM_BOARD_MAX_PRESENTED_MEMOS];
    size_t count = wm_board_scene_memo_presentation(board, cards);
    present(board, board->background, NULL);
    for (size_t index = 0; index < count; index++) {
        pose_card(board, &cards[index], false);
        float matrix[12];
        matrix_translation(cards[index].x, cards[index].y, matrix);
        present(board, board->card, matrix);
    }
    draw_reader(board);
    draw_child_mask(board);
    if (wm_board_calendar_phase(board->calendar) != WM_CALENDAR_CLOSED) {
        wm_board_calendar_draw(board->calendar);
    }
    if (wm_board_compose_phase(board->compose) != WM_COMPOSE_CLOSED) {
        wm_board_compose_draw(board->compose);
    }
}

static bool at_first_day(const WmBoardScene *board) {
    return same_date(board->date, (WmBoardDate){2000, 1, 1});
}

static bool at_last_day(const WmBoardScene *board) {
    return same_date(board->date, (WmBoardDate){2035, 12, 31});
}

static float footer_scene_frame(const WmBoardScene *board) {
    if (board->phase == WM_BOARD_ENTER) {
        return 1000.0f + clamp_frame(board->phase_frame, 40.0f);
    }
    if (board->phase == WM_BOARD_EXIT) {
        return 6000.0f + clamp_frame(board->phase_frame, 40.0f);
    }
    if (board->phase == WM_BOARD_MEMO_OPEN) {
        return board->phase_frame < 13.0f
                   ? 3100.0f + board->phase_frame
                   : 3600.0f + clamp_frame(board->phase_frame - 13.0f, 13.0f);
    }
    if (board->phase == WM_BOARD_MEMO_READ) return 3613.0f;
    if (board->phase == WM_BOARD_MEMO_BACK_SELECT) {
        return 3613.0f;
    }
    if (board->phase == WM_BOARD_MEMO_CLOSE) {
        return board->phase_frame < 13.0f
                   ? 3620.0f + board->phase_frame
                   : 3426.0f + clamp_frame(board->phase_frame - 13.0f, 13.0f);
    }
    if (board->phase == WM_BOARD_MEMO_TRASH_SELECT) {
        return board->phase_frame < 20.0f
                   ? 3613.0f
                   : 3620.0f + clamp_frame(board->phase_frame - 20.0f, 13.0f);
    }
    if (board->phase == WM_BOARD_MEMO_DIALOG) return 3633.0f;
    if (board->phase == WM_BOARD_MEMO_TRASH_CANCEL) {
        return 3600.0f + clamp_frame(board->phase_frame, 13.0f);
    }
    if (board->phase == WM_BOARD_MEMO_ERASE_CLOSE) {
        return 3426.0f + clamp_frame(board->phase_frame, 13.0f);
    }
    return 1040.0f;
}

static void pose_footer(WmBoardScene *board) {
    WmLayoutClip clips[BOARD_CLIP_CAPACITY];
    size_t count = 0;
    append_clip(clips, &count, "my_IplTop_e", "G_SeenChange",
                footer_scene_frame(board));
    append_clip(clips, &count, "my_IplTop_e", "G_ArwRoop",
                10000.0f + fmodf(board->age, 55.0f));
    static const char *const arrow_end_groups[2] = {
        "G_ArwL_End", "G_ArwR_End"
    };
    static const char *const arrow_tab_groups[2] = {
        "G_TabaL", "G_TabaR"
    };
    static const char *const arrow_focus_groups[2] = {
        "G_ArwL_Focus", "G_ArwR_Focus"
    };
    static const char *const arrow_press_groups[2] = {
        "G_ArwL_Ac", "G_ArwR_Ac"
    };
    const WmBoardControl arrow_controls[2] = {
        WM_BOARD_CONTROL_PREVIOUS, WM_BOARD_CONTROL_NEXT
    };
    for (size_t arrow = 0; arrow < 2; arrow++) {
        WmBoardDate arrow_date = board->phase == WM_BOARD_EXIT &&
                                 board->return_direction != 0
                                     ? board->today : board->date;
        bool hidden = arrow == 0
                          ? same_date(arrow_date,
                                      (WmBoardDate){2000, 1, 1}) &&
                            board->page + 1 >= wm_board_scene_memo_page_count(board)
                          : same_date(arrow_date,
                                      (WmBoardDate){2035, 12, 31}) &&
                            board->page == 0;
        bool reader_opening = board->phase == WM_BOARD_MEMO_OPEN;
        bool reader_closing = board->phase == WM_BOARD_MEMO_CLOSE;
        bool reader_hidden = memo_reader_phase(board->phase) &&
                             !reader_closing;
        float arrow_frame = 10.0f;
        if (reader_opening || (reader_closing && !hidden)) {
            arrow_frame = clamp_frame(board->phase_frame, 10.0f);
        } else if (board->phase == WM_BOARD_EXIT) {
            arrow_frame = clamp_frame(board->phase_frame, 10.0f);
        } else if (board->phase == WM_BOARD_ENTER && !hidden) {
            arrow_frame = clamp_frame(board->phase_frame - 30.0f, 10.0f);
        } else if (board->mask_direction < 0 && !hidden) {
            arrow_frame = clamp_frame(board->mask_age, 10.0f);
        }
        append_clip(clips, &count, "my_IplTop_e", arrow_end_groups[arrow],
                    (board->phase == WM_BOARD_EXIT || hidden || reader_hidden ?
                         10100.0f : 10150.0f) + arrow_frame);
        bool memo_page = arrow == 0
                             ? board->page + 1 <
                               wm_board_scene_memo_page_count(board)
                             : board->page > 0;
        append_clip(clips, &count, "my_IplTop_e", arrow_tab_groups[arrow],
                    memo_page && board->phase == WM_BOARD_READY ? 10.0f : 40.0f);
        BoardFocus *focus = &board->button_focus[arrow_controls[arrow]];
        append_clip(clips, &count, "my_IplTop_e", arrow_focus_groups[arrow],
                    (focus->active && !focus->entering ? 10800.0f : 10600.0f) +
                    clamp_frame(focus->frame, 15.0f));
        if (board->arrow_press[arrow] >= 0.0f) {
            append_clip(clips, &count, "my_IplTop_e", arrow_press_groups[arrow],
                        10700.0f + clamp_frame(board->arrow_press[arrow], 30.0f));
        }
    }
    const struct {
        WmBoardControl control;
        const char *group;
        float enter;
        float leave;
        float enter_frames;
        float leave_frames;
    } buttons[] = {
        {WM_BOARD_CONTROL_BACK, "G_Ch", 5900, 5930, 6, 8},
        {WM_BOARD_CONTROL_CALENDAR, "G_Cal", 1900, 1930, 6, 8},
        {WM_BOARD_CONTROL_CREATE, "G_Add", 3900, 3930, 6, 8}
    };
    for (size_t index = 0; index < sizeof(buttons) / sizeof(buttons[0]); index++) {
        BoardFocus *focus = &board->button_focus[buttons[index].control];
        if (!focus->active) continue;
        append_clip(clips, &count, "my_IplTop_e", buttons[index].group,
                    (focus->entering ? buttons[index].enter : buttons[index].leave) +
                    clamp_frame(focus->frame,
                                focus->entering ? buttons[index].enter_frames :
                                                  buttons[index].leave_frames));
    }
    if (memo_reader_phase(board->phase)) {
        const struct {
            WmBoardControl control;
            const char *group;
            float enter_frames;
        } reader_buttons[] = {
            {WM_BOARD_CONTROL_MEMO_BACK, "G_CalExit", 6.0f},
            {WM_BOARD_CONTROL_MEMO_TRASH, "G_Dust", 9.0f}
        };
        for (size_t index = 0; index < sizeof(reader_buttons) /
                                      sizeof(reader_buttons[0]); index++) {
            BoardFocus *focus = &board->button_focus[
                reader_buttons[index].control];
            if (!focus->active) continue;
            append_clip(clips, &count, "my_IplTop_e",
                        reader_buttons[index].group,
                        (focus->entering ? 2900.0f : 2930.0f) +
                        clamp_frame(focus->frame,
                                    focus->entering
                                        ? reader_buttons[index].enter_frames
                                        : 8.0f));
        }
    }
    if (board->phase == WM_BOARD_MEMO_TRASH_SELECT) {
        append_clip(clips, &count, "my_IplTop_e", "G_Dust",
                    2800.0f + clamp_frame(board->phase_frame, 20.0f));
    }
    if (board->phase == WM_BOARD_MEMO_BACK_SELECT) {
        append_clip(clips, &count, "my_IplTop_e", "G_CalExit",
                    3000.0f + clamp_frame(board->phase_frame, 20.0f));
    }
    if ((board->phase == WM_BOARD_ENTER && board->phase_frame < 10.0f) ||
        (board->phase == WM_BOARD_EXIT && board->phase_frame >= 20.0f)) {
        bool entering_grid = board->phase == WM_BOARD_EXIT;
        float grid_frame = entering_grid
                               ? clamp_frame(board->phase_frame - 20.0f, 10.0f)
                               : clamp_frame(board->phase_frame, 10.0f);
        append_clip(clips, &count, "my_IplTop_e", "G_ArwL_End",
                    (board->grid_page > 0
                         ? (entering_grid ? 10150.0f : 10100.0f) + grid_frame
                         : 10110.0f));
        append_clip(clips, &count, "my_IplTop_e", "G_ArwR_End",
                    (board->grid_page < 3
                         ? (entering_grid ? 10150.0f : 10100.0f) + grid_frame
                         : 10110.0f));
    }
    unsigned badge = wm_board_scene_today_count(board);
    append_clip(clips, &count, "my_IplTop_e", "G_BbsSignal",
                badge ? 1.0f + fmodf(board->age, 399.0f) : 0.0f);
    append_clip(clips, &count, "my_IplTop_e", "G_BbsSignal_new", 0.0f);
    wm_layout_pose(board->footer, clips, count);
    bool reader = memo_reader_phase(board->phase);
    wm_layout_set_pose_text(board->footer, "T_CalAdd_R", "");
    wm_layout_set_pose_text(board->footer, "T_CalExit",
                             reader ? "Back" : "");
    wm_layout_set_pose_text(board->footer, "T_Add", "");
    wm_layout_set_pose_text(board->footer, "T_Dust",
                             reader ? "Trash" : "");
    char counter[4] = {0};
    if (badge) snprintf(counter, sizeof(counter), "%u", badge);
    wm_layout_set_pose_text(board->footer, "T_BbsMark1", counter);
}

void wm_board_scene_draw_footer(WmBoardScene *board) {
    if (!board || board->phase == WM_BOARD_CLOSED) return;
    WmBoardChild child = wm_board_scene_child(board);
    if (child == WM_BOARD_CHILD_CALENDAR ||
        child == WM_BOARD_CHILD_COMPOSE) return;
    pose_footer(board);
    present(board, board->footer, NULL);
    if (child == WM_BOARD_CHILD_ERASE) wm_board_erase_draw(board->erase);
    if (child == WM_BOARD_CHILD_NONE &&
        (board->phase == WM_BOARD_ENTER || board->phase == WM_BOARD_READY ||
         (board->phase == WM_BOARD_EXIT &&
          board->return_direction == 0))) {
        /* These picture panes are not alpha-influencing, so setting their
         * opacity to zero keeps the authored T_Day_b text transform intact. */
        wm_layout_set_pane_alpha(board->background, "TopBack_a", 0.0f);
        wm_layout_set_pane_alpha(board->background, "TopBack_b", 0.0f);
        wm_layout_set_pane_alpha(board->background, "TopBack_c", 0.0f);
        wm_layout_set_pose_text(board->background, "T_Day_a", "");
        wm_layout_set_pose_text(board->background, "T_Day_c", "");
        char date[32];
        date_text(board->date, 0, date);
        wm_layout_set_pose_text(board->background, "T_Day_b", date);
        present(board, board->background, NULL);
    }
}

typedef struct BoardFooterAnchorSearch {
    const char *name;
    float x;
    float y;
    bool found;
} BoardFooterAnchorSearch;

static bool capture_footer_anchor(void *context,
                                  const WmLayoutPaneView *pane) {
    BoardFooterAnchorSearch *search = context;
    if (strcmp(pane->name, search->name) == 0) {
        search->x = pane->matrix[3];
        search->y = pane->matrix[7];
        search->found = true;
    }
    return true;
}

bool wm_board_scene_footer_button_anchor(const WmBoardScene *board,
                                          WmBoardControl control,
                                          float *x, float *y) {
    bool page_arrow = control == WM_BOARD_CONTROL_PREVIOUS ||
                      control == WM_BOARD_CONTROL_NEXT;
    if (!board || !x || !y ||
        (board->phase != WM_BOARD_READY &&
         !(page_arrow && memo_reader_phase(board->phase))) ||
        wm_board_scene_child(board) != WM_BOARD_CHILD_NONE) return false;
    const char *name = control == WM_BOARD_CONTROL_BACK ? "B_Ch" :
                       control == WM_BOARD_CONTROL_CALENDAR ? "B_Cal" :
                       control == WM_BOARD_CONTROL_CREATE ? "B_Add" :
                       control == WM_BOARD_CONTROL_PREVIOUS ? "N_ArwL_End" :
                       control == WM_BOARD_CONTROL_NEXT ? "N_ArwR__End" : NULL;
    if (!name) return false;
    BoardFooterAnchorSearch search = {.name = name};
    WmLayoutDrawOptions draw = {
        .wide = true,
        .mode = WM_LAYOUT_IPL,
        .alpha = 1.0f,
        .on_pane = capture_footer_anchor,
        .context = &search
    };
    wm_layout_draw(board->footer, &draw);
    if (!search.found) return false;
    *x = search.x;
    *y = search.y;
    return true;
}

typedef struct BoardVisualScaleSearch {
    const char *name;
    float scale;
    bool found;
} BoardVisualScaleSearch;

static bool capture_footer_visual_scale(void *context,
                                        const WmLayoutPaneView *pane) {
    BoardVisualScaleSearch *search = context;
    if (strcmp(pane->name, search->name) == 0) {
        search->scale = hypotf(pane->matrix[0], pane->matrix[4]);
        search->found = true;
    }
    return true;
}

bool wm_board_scene_footer_button_visual_scale(const WmBoardScene *board,
                                                WmBoardControl control,
                                                float *scale) {
    if (!board || !scale || (board->phase != WM_BOARD_READY &&
                            !memo_reader_phase(board->phase)) ||
        wm_board_scene_child(board) != WM_BOARD_CHILD_NONE) return false;
    const char *name = control == WM_BOARD_CONTROL_MEMO_BACK &&
                               board->phase == WM_BOARD_MEMO_READ
                           ? "N_BtnL_a3_Cal" :
                       control == WM_BOARD_CONTROL_MEMO_TRASH &&
                               board->phase == WM_BOARD_MEMO_READ
                           ? "N_Dust_00" :
                       control == WM_BOARD_CONTROL_CALENDAR &&
                               board->phase == WM_BOARD_READY
                           ? "N_BtnL_a0_Cal" :
                       control == WM_BOARD_CONTROL_CREATE &&
                               board->phase == WM_BOARD_READY
                           ? "N_BtnL_a0_Add" :
                       control == WM_BOARD_CONTROL_BACK
                           ? "N_BtnR_a0_Ch" : NULL;
    if (!name) return false;
    BoardVisualScaleSearch search = {.name = name};
    wm_layout_visit_all_transforms(board->footer, true, WM_LAYOUT_IPL,
                                   NULL, capture_footer_visual_scale,
                                   &search);
    if (!search.found) return false;
    *scale = search.scale;
    return true;
}

bool wm_board_scene_back(WmBoardScene *board) {
    if (!board) return false;
    if (board->dragging) return wm_board_scene_cancel_pointer(board);
    if (wm_board_calendar_phase(board->calendar) != WM_CALENDAR_CLOSED) {
        return wm_board_calendar_back(board->calendar);
    }
    if (wm_board_compose_phase(board->compose) != WM_COMPOSE_CLOSED) {
        return wm_board_compose_back(board->compose);
    }
    if (wm_board_erase_phase(board->erase) != WM_ERASE_CLOSED) {
        return wm_board_erase_back(board->erase);
    }
    if (board->phase == WM_BOARD_MEMO_READ) {
        wm_board_reader_scroll_set_sound_enabled(&board->reader_scroll,
                                                  false);
        wm_board_reader_scroll_hover(&board->reader_scroll,
                                      WM_BOARD_READER_ARROW_NONE);
        board->phase = WM_BOARD_MEMO_BACK_SELECT;
        board->phase_frame = 0.0f;
        board->hover = (WmBoardHit){WM_BOARD_CONTROL_NONE, SIZE_MAX};
        return true;
    }
    if (board->phase != WM_BOARD_READY) return false;
    int64_t selected_day = civil_day(board->date);
    int64_t today = civil_day(board->today);
    board->return_direction = selected_day > today ? -1 :
                              selected_day < today ? 1 : 0;
    wm_board_scene_hover(board,
                         (WmBoardHit){WM_BOARD_CONTROL_NONE, SIZE_MAX});
    board->phase = WM_BOARD_EXIT;
    board->phase_frame = 0.0f;
    memset(board->button_focus, 0, sizeof(board->button_focus));
    return true;
}

static float next_unit_random(WmBoardScene *board) {
    uint64_t value = board->rng_state;
    value ^= value >> 12;
    value ^= value << 25;
    value ^= value >> 27;
    board->rng_state = value;
    value *= UINT64_C(0x2545f4914f6cdd1d);
    return (float)(value >> 40) * (1.0f / 16777216.0f);
}

static bool append_posted_memo(WmBoardScene *board, const char *text) {
    if (!text || !text[0] || board->memo_count >= BOARD_MAX_MEMOS) return false;
    size_t previous_count = board->memo_count;
    WmBoardMemo *records = calloc(previous_count + 1, sizeof(*records));
    BoardMemo *previous = board->memos;
    BoardPinState *pins = previous_count
                              ? malloc(previous_count * sizeof(*pins)) : NULL;
    if (!records || (previous_count && !pins)) {
        free(records);
        free(pins);
        return false;
    }
    for (size_t index = 0; index < previous_count; index++) {
        pins[index] = (BoardPinState){
            .kind = previous[index].pin_kind,
            .sampled = previous[index].pin_sampled
        };
        if (!wm_board_scene_get_memo(board, index, &records[index])) {
            free(records);
            free(pins);
            return false;
        }
    }
    struct timespec clock = {0};
    if (clock_gettime(CLOCK_REALTIME, &clock) != 0) {
        free(records);
        free(pins);
        return false;
    }
    char id[96];
    int length = snprintf(id, sizeof(id), "local-memo-%lld-%ld-%zu",
                          (long long)clock.tv_sec, clock.tv_nsec,
                          previous_count);
    if (length < 0 || length >= (int)sizeof(id)) {
        free(records);
        free(pins);
        return false;
    }
    struct tm local_time;
    if (!localtime_r(&clock.tv_sec, &local_time)) {
        free(records);
        free(pins);
        return false;
    }
    WmBoardDate today = {
        .year = local_time.tm_year + 1900,
        .month = local_time.tm_mon + 1,
        .day = local_time.tm_mday
    };
    if (!wm_board_date_valid(today)) {
        free(records);
        free(pins);
        return false;
    }
    int64_t created_at_ms = (int64_t)clock.tv_sec * INT64_C(1000) +
                            clock.tv_nsec / 1000000;
    /* Native receive() evaluates Y before X, with separate float products. */
    float vertical = next_unit_random(board);
    float horizontal = next_unit_random(board);
    float y = 180.0f - 260.0f * vertical;
    float x = -230.0f + 460.0f * horizontal;
    records[previous_count] = (WmBoardMemo){
        .id = id,
        .text = text,
        .date = today,
        .created_at_ms = created_at_ms,
        .x = x,
        .y = y,
        .has_position = true,
        .read = false
    };
    bool added = wm_board_scene_set_memos(board, records, previous_count + 1);
    free(records);
    if (!added) {
        free(pins);
        return false;
    }
    /* Posting adds one card without reclassifying existing pins. The source
     * clears only the newly posted record's cached animation. */
    for (size_t index = 0; index < previous_count; index++) {
        board->memos[index].pin_sampled = pins[index].sampled;
        board->memos[index].pin_kind = pins[index].kind;
    }
    free(pins);
    board->date = today;
    board->today = today;
    board->page = 0;
    board->card_age = 0.0f;
    board->arriving_memo = previous_count;
    update_visible(board);
    board->pending_action = WM_BOARD_ACTION_MEMO_POSTED;
    board->pending_memo_index = previous_count;
    return true;
}

static bool finish_memo_erase(WmBoardScene *board) {
    if (board->selected >= board->memo_count) return false;
    size_t deleted = board->selected;
    char *id = copy_string(board->memos[deleted].id);
    if (!id) return false;
    size_t new_count = board->memo_count - 1;
    WmBoardDate *dates = new_count ? malloc(new_count * sizeof(*dates)) : NULL;
    if (new_count && !dates) {
        free(id);
        return false;
    }
    for (size_t source = 0, target = 0; source < board->memo_count;
         source++) {
        if (source != deleted) dates[target++] = board->memos[source].date;
    }
    bool dates_updated = wm_board_calendar_set_message_dates(
        board->calendar, dates, new_count);
    free(dates);
    if (!dates_updated) {
        free(id);
        return false;
    }
    free(board->memos[deleted].id);
    free(board->memos[deleted].text);
    if (deleted + 1 < board->memo_count) {
        memmove(&board->memos[deleted], &board->memos[deleted + 1],
                (board->memo_count - deleted - 1) * sizeof(*board->memos));
    }
    for (size_t position = 0; position < board->memo_count; position++) {
        if (board->memo_order[position].index == deleted) {
            memmove(&board->memo_order[position],
                    &board->memo_order[position + 1],
                    (board->memo_count - position - 1) *
                        sizeof(*board->memo_order));
            break;
        }
    }
    for (size_t position = 0; position < new_count; position++) {
        if (board->memo_order[position].index > deleted) {
            board->memo_order[position].index--;
        }
    }
    board->memo_count = new_count;
    board->selected = SIZE_MAX;
    board->card_age = 100000.0f;
    board->arriving_memo = SIZE_MAX;
    update_visible(board);
    free(board->last_erased_id);
    board->last_erased_id = id;
    board->pending_action = WM_BOARD_ACTION_ERASE_MEMO;
    board->pending_memo_index = deleted;
    return true;
}

void wm_board_scene_advance(WmBoardScene *board, float frames) {
    if (!board || board->phase == WM_BOARD_CLOSED ||
        !isfinite(frames) || frames <= 0.0f) return;
    board->age += frames;
    board->pin_age += frames;
    board->card_age += frames;
    wm_board_reader_scroll_advance(&board->reader_scroll, frames);
    if (board->dragging && board->dragged_index < board->memo_count) {
        float delta_x = board->drag_delta_x - board->drag_last_x;
        float delta_y = board->drag_delta_y - board->drag_last_y;
        float speed = hypotf(delta_x, delta_y) / frames;
        board->drag_last_x = board->drag_delta_x;
        board->drag_last_y = board->drag_delta_y;
        board->drag_gain = fminf(1.0f, 2.0f * speed / 304.0f);
        board->drag_pan = clamp_pan((board->memos[board->dragged_index].x +
                                     board->drag_delta_x) / 304.0f);
    }
    if (board->mask_direction != 0) {
        board->mask_age += frames;
        if (board->mask_direction < 0 && board->mask_age >= 20.0f) {
            board->mask_direction = 0;
        }
    }
    if (wm_board_calendar_phase(board->calendar) != WM_CALENDAR_CLOSED) {
        wm_board_calendar_advance(board->calendar, frames);
        WmBoardDate selected_date;
        WmBoardCalendarOutcome outcome = wm_board_calendar_take_outcome(
            board->calendar, &selected_date);
        if (outcome != WM_CALENDAR_OUTCOME_NONE) {
            if (outcome == WM_CALENDAR_OUTCOME_SELECTED) {
                if (!same_date(board->date, selected_date))
                    reset_pin_choices(board);
                board->date = selected_date;
                board->page = 0;
                board->card_age = 100000.0f;
                board->arriving_memo = SIZE_MAX;
                update_visible(board);
            }
            board->mask_direction = -1;
            board->mask_age = 0.0f;
        }
    }
    if (wm_board_compose_phase(board->compose) != WM_COMPOSE_CLOSED) {
        wm_board_compose_advance(board->compose, frames);
        WmBoardComposeOutcome outcome = wm_board_compose_take_outcome(
            board->compose);
        if (outcome == WM_COMPOSE_OUTCOME_POSTED) {
            append_posted_memo(board, wm_board_compose_text(board->compose));
        } else if (outcome == WM_COMPOSE_OUTCOME_CLOSED) {
            board->mask_direction = -1;
            board->mask_age = 0.0f;
        } else if (outcome == WM_COMPOSE_OUTCOME_OPEN_SETTINGS) {
            board->pending_action = WM_BOARD_ACTION_OPEN_SETTINGS;
        }
    }
    if (wm_board_erase_phase(board->erase) != WM_ERASE_CLOSED) {
        wm_board_erase_advance(board->erase, frames);
        WmBoardEraseOutcome outcome = wm_board_erase_take_outcome(board->erase);
        if (outcome != WM_ERASE_OUTCOME_NONE) {
            /* The dialog replaces the reader's hover state. Its completed
             * action returns to neutral footer and card poses. */
            memset(board->button_focus, 0, sizeof(board->button_focus));
            memset(board->card_focus, 0, sizeof(board->card_focus));
        }
        if (outcome == WM_ERASE_OUTCOME_ACCEPT) {
            board->phase = WM_BOARD_MEMO_ERASE_CLOSE;
            board->phase_frame = 0.0f;
            /* The native Board dump cue starts with the accepted Trash
             * selection, before the memo's erase-close animation. Keep it on
             * the scene cue queue so the platform layer owns playback. */
            board->pending_reader_cue = "WIPL_SE_BOARD_DUMP";
            wm_board_reader_scroll_set_active(&board->reader_scroll,
                                                false);
        } else if (outcome == WM_ERASE_OUTCOME_CANCEL) {
            board->phase = WM_BOARD_MEMO_TRASH_CANCEL;
            board->phase_frame = 0.0f;
        }
        return;
    }
    for (size_t index = 0; index <
         sizeof(board->button_focus) / sizeof(board->button_focus[0]); index++) {
        if (board->button_focus[index].active) {
            board->button_focus[index].frame += frames;
        }
    }
    for (size_t index = 0; index < BOARD_MEMOS_PER_PAGE; index++) {
        if (board->card_focus[index].active) {
            board->card_focus[index].frame += frames;
        }
    }
    for (size_t index = 0; index < 2; index++) {
        if (board->arrow_press[index] >= 0.0f) {
            board->arrow_press[index] += frames;
            if (board->arrow_press[index] > 30.0f) {
                board->arrow_press[index] = -1.0f;
            }
        }
    }
    float remaining = frames;
    while (remaining > 0.0f) {
        float duration = 0.0f;
        switch (board->phase) {
            case WM_BOARD_ENTER:
            case WM_BOARD_EXIT:
                duration = 40.0f;
                break;
            case WM_BOARD_DATE_SCROLL:
                duration = 20.0f;
                break;
            case WM_BOARD_MEMO_PAGE:
                duration = 15.0f;
                break;
            case WM_BOARD_MEMO_OPEN:
            case WM_BOARD_MEMO_CLOSE:
                duration = 26.0f;
                break;
            case WM_BOARD_MEMO_BACK_SELECT:
                duration = 20.0f;
                break;
            case WM_BOARD_MEMO_TRASH_SELECT:
                duration = 33.0f;
                break;
            case WM_BOARD_MEMO_TRASH_CANCEL:
                duration = 13.0f;
                break;
            case WM_BOARD_MEMO_ERASE_CLOSE:
                duration = 17.0f;
                break;
            case WM_BOARD_CLOSED:
            case WM_BOARD_READY:
            case WM_BOARD_MEMO_READ:
            case WM_BOARD_MEMO_DIALOG:
                return;
        }
        float amount = fminf(remaining, duration - board->phase_frame);
        board->phase_frame += amount;
        remaining -= amount;
        if (board->phase_frame < duration) return;
        switch (board->phase) {
            case WM_BOARD_ENTER:
                board->phase = WM_BOARD_READY;
                break;
            case WM_BOARD_EXIT:
                if (!same_date(board->date, board->today))
                    reset_pin_choices(board);
                board->date = board->today;
                board->page = 0;
                update_visible(board);
                board->return_direction = 0;
                board->phase = WM_BOARD_CLOSED;
                board->pending_action = WM_BOARD_ACTION_EXITED;
                break;
            case WM_BOARD_DATE_SCROLL:
                if (!same_date(board->date, board->next_date))
                    reset_pin_choices(board);
                board->date = board->next_date;
                board->page = 0;
                board->card_age = 100000.0f;
                board->arriving_memo = SIZE_MAX;
                update_visible(board);
                memset(board->card_focus, 0, sizeof(board->card_focus));
                board->phase = WM_BOARD_READY;
                break;
            case WM_BOARD_MEMO_PAGE:
                if (board->direction < 0) board->page++;
                else if (board->page > 0) board->page--;
                board->card_age = 100000.0f;
                board->arriving_memo = SIZE_MAX;
                update_visible(board);
                memset(board->card_focus, 0, sizeof(board->card_focus));
                board->phase = WM_BOARD_READY;
                break;
            case WM_BOARD_MEMO_OPEN:
                board->phase = WM_BOARD_MEMO_READ;
                wm_board_reader_scroll_set_active(&board->reader_scroll,
                                                    true);
                break;
            case WM_BOARD_MEMO_BACK_SELECT:
                board->phase = WM_BOARD_MEMO_CLOSE;
                wm_board_reader_scroll_set_active(&board->reader_scroll,
                                                    false);
                board->hover = (WmBoardHit){WM_BOARD_CONTROL_NONE, SIZE_MAX};
                memset(board->button_focus, 0, sizeof(board->button_focus));
                break;
            case WM_BOARD_MEMO_CLOSE:
                board->phase = WM_BOARD_READY;
                board->selected = SIZE_MAX;
                wm_board_reader_scroll_reset(&board->reader_scroll);
                break;
            case WM_BOARD_MEMO_TRASH_SELECT:
                if (wm_board_erase_open(board->erase)) {
                    board->phase = WM_BOARD_MEMO_DIALOG;
                    board->pending_reader_cue = "WIPL_SE_INFO_WINDOW";
                    if (remaining > 0.0f) {
                        wm_board_erase_advance(board->erase, remaining);
                    }
                } else {
                    board->phase = WM_BOARD_MEMO_READ;
                    wm_board_reader_scroll_set_active(
                        &board->reader_scroll, true);
                }
                return;
            case WM_BOARD_MEMO_TRASH_CANCEL:
                board->phase = WM_BOARD_MEMO_READ;
                wm_board_reader_scroll_set_active(&board->reader_scroll,
                                                    true);
                break;
            case WM_BOARD_MEMO_ERASE_CLOSE:
                board->phase = finish_memo_erase(board)
                                   ? WM_BOARD_READY :
                                     WM_BOARD_MEMO_TRASH_CANCEL;
                break;
            case WM_BOARD_CLOSED:
            case WM_BOARD_READY:
            case WM_BOARD_MEMO_READ:
            case WM_BOARD_MEMO_DIALOG:
                break;
        }
        board->phase_frame = 0.0f;
    }
}

static bool same_hit(WmBoardHit first, WmBoardHit second) {
    return first.control == second.control &&
           (first.control != WM_BOARD_CONTROL_MEMO ||
            first.memo_index == second.memo_index);
}

static WmBoardComposeControl compose_control(WmBoardControl control) {
    if (control >= WM_BOARD_CONTROL_COMPOSE_ADDRESS_ENTRY_FIRST &&
        control <= WM_BOARD_CONTROL_COMPOSE_ADDRESS_ENTRY_LAST) {
        return (WmBoardComposeControl)(
            WM_COMPOSE_CONTROL_ADDRESS_ENTRY_FIRST +
            control - WM_BOARD_CONTROL_COMPOSE_ADDRESS_ENTRY_FIRST);
    }
    if (control >= WM_BOARD_CONTROL_COMPOSE_KEY_FIRST &&
        control <= WM_BOARD_CONTROL_COMPOSE_KEY_LAST) {
        return (WmBoardComposeControl)(WM_COMPOSE_CONTROL_KEY_FIRST +
            control - WM_BOARD_CONTROL_COMPOSE_KEY_FIRST);
    }
    switch (control) {
        case WM_BOARD_CONTROL_COMPOSE_BACK:
            return WM_COMPOSE_CONTROL_BACK;
        case WM_BOARD_CONTROL_COMPOSE_MEMO:
            return WM_COMPOSE_CONTROL_MEMO;
        case WM_BOARD_CONTROL_COMPOSE_LETTER:
            return WM_COMPOSE_CONTROL_LETTER;
        case WM_BOARD_CONTROL_COMPOSE_ADDRESS:
            return WM_COMPOSE_CONTROL_ADDRESS;
        case WM_BOARD_CONTROL_COMPOSE_EDIT:
            return WM_COMPOSE_CONTROL_EDIT;
        case WM_BOARD_CONTROL_COMPOSE_POST:
            return WM_COMPOSE_CONTROL_POST;
        case WM_BOARD_CONTROL_COMPOSE_MII:
            return WM_COMPOSE_CONTROL_MII;
        case WM_BOARD_CONTROL_COMPOSE_SCROLL_UP:
            return WM_COMPOSE_CONTROL_SCROLL_UP;
        case WM_BOARD_CONTROL_COMPOSE_SCROLL_DOWN:
            return WM_COMPOSE_CONTROL_SCROLL_DOWN;
        case WM_BOARD_CONTROL_COMPOSE_ADDRESS_PREVIOUS:
            return WM_COMPOSE_CONTROL_ADDRESS_PREVIOUS;
        case WM_BOARD_CONTROL_COMPOSE_ADDRESS_NEXT:
            return WM_COMPOSE_CONTROL_ADDRESS_NEXT;
        case WM_BOARD_CONTROL_COMPOSE_ADDRESS_WII:
            return WM_COMPOSE_CONTROL_ADDRESS_WII;
        case WM_BOARD_CONTROL_COMPOSE_ADDRESS_OTHERS:
            return WM_COMPOSE_CONTROL_ADDRESS_OTHERS;
        case WM_BOARD_CONTROL_COMPOSE_ADDRESS_EDIT:
            return WM_COMPOSE_CONTROL_ADDRESS_EDIT;
        case WM_BOARD_CONTROL_COMPOSE_ADDRESS_DIALOG_OK:
            return WM_COMPOSE_CONTROL_ADDRESS_DIALOG_OK;
        case WM_BOARD_CONTROL_COMPOSE_ADDRESS_MII:
            return WM_COMPOSE_CONTROL_ADDRESS_MII;
        case WM_BOARD_CONTROL_COMPOSE_ADDRESS_INFO:
            return WM_COMPOSE_CONTROL_ADDRESS_INFO;
        case WM_BOARD_CONTROL_COMPOSE_ADDRESS_CHANGE_NICKNAME:
            return WM_COMPOSE_CONTROL_ADDRESS_CHANGE_NICKNAME;
        case WM_BOARD_CONTROL_COMPOSE_ADDRESS_ERASE:
            return WM_COMPOSE_CONTROL_ADDRESS_ERASE;
        case WM_BOARD_CONTROL_COMPOSE_ADDRESS_DIALOG_YES:
            return WM_COMPOSE_CONTROL_ADDRESS_DIALOG_YES;
        case WM_BOARD_CONTROL_COMPOSE_ADDRESS_DIALOG_NO:
            return WM_COMPOSE_CONTROL_ADDRESS_DIALOG_NO;
        case WM_BOARD_CONTROL_COMPOSE_NETWORK_QUIT:
            return WM_COMPOSE_CONTROL_NETWORK_QUIT;
        case WM_BOARD_CONTROL_COMPOSE_NETWORK_SETTINGS:
            return WM_COMPOSE_CONTROL_NETWORK_SETTINGS;
        default:
            return WM_COMPOSE_CONTROL_NONE;
    }
}

static WmBoardControl board_compose_control(WmBoardComposeControl control) {
    if (control >= WM_COMPOSE_CONTROL_ADDRESS_ENTRY_FIRST &&
        control <= WM_COMPOSE_CONTROL_ADDRESS_ENTRY_LAST) {
        return (WmBoardControl)(
            WM_BOARD_CONTROL_COMPOSE_ADDRESS_ENTRY_FIRST +
            control - WM_COMPOSE_CONTROL_ADDRESS_ENTRY_FIRST);
    }
    if (control >= WM_COMPOSE_CONTROL_KEY_FIRST &&
        control <= WM_COMPOSE_CONTROL_KEY_LAST) {
        return (WmBoardControl)(WM_BOARD_CONTROL_COMPOSE_KEY_FIRST +
            control - WM_COMPOSE_CONTROL_KEY_FIRST);
    }
    switch (control) {
        case WM_COMPOSE_CONTROL_BACK:
            return WM_BOARD_CONTROL_COMPOSE_BACK;
        case WM_COMPOSE_CONTROL_MEMO:
            return WM_BOARD_CONTROL_COMPOSE_MEMO;
        case WM_COMPOSE_CONTROL_LETTER:
            return WM_BOARD_CONTROL_COMPOSE_LETTER;
        case WM_COMPOSE_CONTROL_ADDRESS:
            return WM_BOARD_CONTROL_COMPOSE_ADDRESS;
        case WM_COMPOSE_CONTROL_EDIT:
            return WM_BOARD_CONTROL_COMPOSE_EDIT;
        case WM_COMPOSE_CONTROL_POST:
            return WM_BOARD_CONTROL_COMPOSE_POST;
        case WM_COMPOSE_CONTROL_MII:
            return WM_BOARD_CONTROL_COMPOSE_MII;
        case WM_COMPOSE_CONTROL_SCROLL_UP:
            return WM_BOARD_CONTROL_COMPOSE_SCROLL_UP;
        case WM_COMPOSE_CONTROL_SCROLL_DOWN:
            return WM_BOARD_CONTROL_COMPOSE_SCROLL_DOWN;
        case WM_COMPOSE_CONTROL_ADDRESS_PREVIOUS:
            return WM_BOARD_CONTROL_COMPOSE_ADDRESS_PREVIOUS;
        case WM_COMPOSE_CONTROL_ADDRESS_NEXT:
            return WM_BOARD_CONTROL_COMPOSE_ADDRESS_NEXT;
        case WM_COMPOSE_CONTROL_ADDRESS_WII:
            return WM_BOARD_CONTROL_COMPOSE_ADDRESS_WII;
        case WM_COMPOSE_CONTROL_ADDRESS_OTHERS:
            return WM_BOARD_CONTROL_COMPOSE_ADDRESS_OTHERS;
        case WM_COMPOSE_CONTROL_ADDRESS_EDIT:
            return WM_BOARD_CONTROL_COMPOSE_ADDRESS_EDIT;
        case WM_COMPOSE_CONTROL_ADDRESS_DIALOG_OK:
            return WM_BOARD_CONTROL_COMPOSE_ADDRESS_DIALOG_OK;
        case WM_COMPOSE_CONTROL_ADDRESS_MII:
            return WM_BOARD_CONTROL_COMPOSE_ADDRESS_MII;
        case WM_COMPOSE_CONTROL_ADDRESS_INFO:
            return WM_BOARD_CONTROL_COMPOSE_ADDRESS_INFO;
        case WM_COMPOSE_CONTROL_ADDRESS_CHANGE_NICKNAME:
            return WM_BOARD_CONTROL_COMPOSE_ADDRESS_CHANGE_NICKNAME;
        case WM_COMPOSE_CONTROL_ADDRESS_ERASE:
            return WM_BOARD_CONTROL_COMPOSE_ADDRESS_ERASE;
        case WM_COMPOSE_CONTROL_ADDRESS_DIALOG_YES:
            return WM_BOARD_CONTROL_COMPOSE_ADDRESS_DIALOG_YES;
        case WM_COMPOSE_CONTROL_ADDRESS_DIALOG_NO:
            return WM_BOARD_CONTROL_COMPOSE_ADDRESS_DIALOG_NO;
        case WM_COMPOSE_CONTROL_NETWORK_QUIT:
            return WM_BOARD_CONTROL_COMPOSE_NETWORK_QUIT;
        case WM_COMPOSE_CONTROL_NETWORK_SETTINGS:
            return WM_BOARD_CONTROL_COMPOSE_NETWORK_SETTINGS;
        case WM_COMPOSE_CONTROL_ADDRESS_ENTRY_FIRST:
        case WM_COMPOSE_CONTROL_ADDRESS_ENTRY_LAST:
        case WM_COMPOSE_CONTROL_NONE:
        case WM_COMPOSE_CONTROL_KEY_FIRST:
        case WM_COMPOSE_CONTROL_KEY_LAST:
            return WM_BOARD_CONTROL_NONE;
    }
    return WM_BOARD_CONTROL_NONE;
}

static bool point_in_rect(int x, int y, WmSourceRect rect, float margin) {
    return (float)x >= rect.x - margin &&
           (float)x < rect.x + rect.width + margin &&
           (float)y >= rect.y - margin &&
           (float)y < rect.y + rect.height + margin;
}

static bool footer_hit(WmBoardScene *board, const char *pane,
                       int x, int y, float margin) {
    WmSourceRect rect;
    return wm_source_pane_rect(board->footer, pane, true,
                               WM_LAYOUT_IPL, NULL, &rect) &&
           point_in_rect(x, y, rect, margin);
}

static size_t visible_position(const WmBoardScene *board,
                               size_t source_index) {
    for (size_t index = 0; index < board->visible_count; index++) {
        if (board->visible[index] == source_index) return index;
    }
    return SIZE_MAX;
}

static void promote_card(WmBoardScene *board, size_t source_index) {
    for (size_t index = 0; index < board->visible_count; index++) {
        if (board->card_order[index] != source_index) continue;
        if (index + 1 < board->visible_count) {
            memmove(&board->card_order[index], &board->card_order[index + 1],
                    (board->visible_count - index - 1) *
                        sizeof(board->card_order[0]));
        }
        board->card_order[board->visible_count - 1] = source_index;
        return;
    }
}

WmBoardHit wm_board_scene_hit(WmBoardScene *board, int x, int y) {
    WmBoardHit none = {WM_BOARD_CONTROL_NONE, SIZE_MAX};
    if (board && board->dragging) return none;
    if (board && wm_board_erase_phase(board->erase) != WM_ERASE_CLOSED) {
        WmBoardEraseControl control = wm_board_erase_hit(board->erase, x, y);
        if (control == WM_ERASE_CONTROL_QUIT) {
            return (WmBoardHit){WM_BOARD_CONTROL_ERASE_QUIT, SIZE_MAX};
        }
        if (control == WM_ERASE_CONTROL_OK) {
            return (WmBoardHit){WM_BOARD_CONTROL_ERASE_OK, SIZE_MAX};
        }
        return none;
    }
    if (board && wm_board_calendar_phase(board->calendar) !=
                     WM_CALENDAR_CLOSED) {
        WmBoardCalendarHit calendar_hit = wm_board_calendar_hit(
            board->calendar, x, y);
        switch (calendar_hit.control) {
            case WM_CALENDAR_CONTROL_BACK:
                return (WmBoardHit){WM_BOARD_CONTROL_CALENDAR_BACK, SIZE_MAX};
            case WM_CALENDAR_CONTROL_PREVIOUS:
                return (WmBoardHit){WM_BOARD_CONTROL_CALENDAR_PREVIOUS, SIZE_MAX};
            case WM_CALENDAR_CONTROL_NEXT:
                return (WmBoardHit){WM_BOARD_CONTROL_CALENDAR_NEXT, SIZE_MAX};
            case WM_CALENDAR_CONTROL_DAY:
                return (WmBoardHit){WM_BOARD_CONTROL_CALENDAR_DAY,
                                    calendar_hit.day_index};
            case WM_CALENDAR_CONTROL_NONE:
                return none;
        }
    }
    if (board && wm_board_compose_phase(board->compose) != WM_COMPOSE_CLOSED) {
        return (WmBoardHit){
            board_compose_control(wm_board_compose_hit(board->compose, x, y)),
            SIZE_MAX
        };
    }
    if (!board || (board->phase != WM_BOARD_READY &&
                   board->phase != WM_BOARD_MEMO_READ)) return none;
    pose_footer(board);
    if (board->phase == WM_BOARD_MEMO_READ) {
        if (footer_hit(board, "B_CalExit", x, y, 0)) {
            return (WmBoardHit){WM_BOARD_CONTROL_MEMO_BACK, SIZE_MAX};
        }
        if (footer_hit(board, "B_Dust", x, y, 0)) {
            return (WmBoardHit){WM_BOARD_CONTROL_MEMO_TRASH, board->selected};
        }
        pose_reader(board);
        WmSourceRect up;
        WmSourceRect down;
        bool up_found = wm_source_pane_rect(
            board->reader, "B_ArwR", true, WM_LAYOUT_IPL, NULL, &up);
        bool down_found = wm_source_pane_rect(
            board->reader, "B_ArwL", true, WM_LAYOUT_IPL, NULL, &down);
        WmBoardReaderArrow arrow = wm_board_reader_scroll_hit(
            &board->reader_scroll, (float)x, (float)y,
            up_found ? &up : NULL, down_found ? &down : NULL);
        if (arrow == WM_BOARD_READER_ARROW_UP) {
            return (WmBoardHit){WM_BOARD_CONTROL_MEMO_SCROLL_UP, SIZE_MAX};
        }
        if (arrow == WM_BOARD_READER_ARROW_DOWN) {
            return (WmBoardHit){WM_BOARD_CONTROL_MEMO_SCROLL_DOWN, SIZE_MAX};
        }
        return none;
    }
    /* A footer arrow already holding focus keeps a four-pixel exit margin.
     * Its animated hit pane can shift beneath a stationary pointer. */
    if (board->hover.control == WM_BOARD_CONTROL_PREVIOUS &&
        footer_hit(board, "B_ArwL", x, y, 4)) return board->hover;
    if (board->hover.control == WM_BOARD_CONTROL_NEXT &&
        footer_hit(board, "B_ArwR", x, y, 4)) return board->hover;
    if (footer_hit(board, "B_Ch", x, y, 0)) {
        return (WmBoardHit){WM_BOARD_CONTROL_BACK, SIZE_MAX};
    }
    if (footer_hit(board, "B_Cal", x, y, 0)) {
        return (WmBoardHit){WM_BOARD_CONTROL_CALENDAR, SIZE_MAX};
    }
    if (footer_hit(board, "B_Add", x, y, 0)) {
        return (WmBoardHit){WM_BOARD_CONTROL_CREATE, SIZE_MAX};
    }
    if ((!at_first_day(board) || board->page + 1 <
         wm_board_scene_memo_page_count(board)) &&
        footer_hit(board, "B_ArwL", x, y, 0)) {
        return (WmBoardHit){WM_BOARD_CONTROL_PREVIOUS, SIZE_MAX};
    }
    if ((!at_last_day(board) || board->page > 0) &&
        footer_hit(board, "B_ArwR", x, y, 0)) {
        return (WmBoardHit){WM_BOARD_CONTROL_NEXT, SIZE_MAX};
    }
    for (size_t position = board->visible_count; position > 0; position--) {
        size_t source_index = board->card_order[position - 1];
        size_t index = visible_position(board, source_index);
        if (index == SIZE_MAX) continue;
        WmBoardMemoPresentation card = {
            .memo_index = source_index,
            .paste_frame = source_index == board->arriving_memo
                               ? clamp_frame(board->card_age, 10.0f) : 10.0f,
            .next_page_frame = -1.0f,
            .pin_kind = sample_pin_kind(board, source_index)
        };
        pose_card(board, &card, false);
        float matrix[12];
        card_matrix(board, index, matrix);
        WmSourceRect rect;
        if (wm_source_pane_rect(board->card, "B_Letter", true,
                                WM_LAYOUT_IPL, matrix, &rect) &&
            point_in_rect(x, y, rect, 0)) {
            return (WmBoardHit){WM_BOARD_CONTROL_MEMO,
                                source_index};
        }
    }
    return none;
}

void wm_board_scene_hover(WmBoardScene *board, WmBoardHit hit) {
    if (board && board->dragging) return;
    if (board && wm_board_erase_phase(board->erase) != WM_ERASE_CLOSED) {
        WmBoardEraseControl control = WM_ERASE_CONTROL_NONE;
        if (hit.control == WM_BOARD_CONTROL_ERASE_QUIT) {
            control = WM_ERASE_CONTROL_QUIT;
        } else if (hit.control == WM_BOARD_CONTROL_ERASE_OK) {
            control = WM_ERASE_CONTROL_OK;
        }
        wm_board_erase_hover(board->erase, control);
        return;
    }
    if (board && wm_board_compose_phase(board->compose) != WM_COMPOSE_CLOSED) {
        wm_board_compose_hover(board->compose, compose_control(hit.control));
        return;
    }
    if (board && wm_board_calendar_phase(board->calendar) !=
                     WM_CALENDAR_CLOSED) {
        WmBoardCalendarControl control = WM_CALENDAR_CONTROL_NONE;
        switch (hit.control) {
            case WM_BOARD_CONTROL_CALENDAR_BACK:
                control = WM_CALENDAR_CONTROL_BACK;
                break;
            case WM_BOARD_CONTROL_CALENDAR_PREVIOUS:
                control = WM_CALENDAR_CONTROL_PREVIOUS;
                break;
            case WM_BOARD_CONTROL_CALENDAR_NEXT:
                control = WM_CALENDAR_CONTROL_NEXT;
                break;
            case WM_BOARD_CONTROL_CALENDAR_DAY:
                control = WM_CALENDAR_CONTROL_DAY;
                break;
            default:
                break;
        }
        wm_board_calendar_hover(board->calendar,
                                (WmBoardCalendarHit){control,
                                                    (unsigned)hit.memo_index});
        return;
    }
    if (!board || (board->phase != WM_BOARD_READY &&
                   board->phase != WM_BOARD_MEMO_READ)) return;
    if (board->phase == WM_BOARD_MEMO_READ) {
        WmBoardReaderArrow arrow = WM_BOARD_READER_ARROW_NONE;
        if (hit.control == WM_BOARD_CONTROL_MEMO_SCROLL_UP) {
            arrow = WM_BOARD_READER_ARROW_UP;
        } else if (hit.control == WM_BOARD_CONTROL_MEMO_SCROLL_DOWN) {
            arrow = WM_BOARD_READER_ARROW_DOWN;
        }
        wm_board_reader_scroll_hover(&board->reader_scroll, arrow);
    }
    if (same_hit(board->hover, hit)) return;
    WmBoardHit old = board->hover;
    if (old.control > WM_BOARD_CONTROL_NONE &&
        old.control <= WM_BOARD_CONTROL_MEMO_TRASH &&
        old.control != WM_BOARD_CONTROL_MEMO) {
        board->button_focus[old.control] = (BoardFocus){true, false, 0};
    } else if (old.control == WM_BOARD_CONTROL_MEMO) {
        size_t position = visible_position(board, old.memo_index);
        if (position != SIZE_MAX) {
            board->card_focus[position] = (BoardFocus){true, false, 0};
        }
    }
    board->hover = hit;
    if (hit.control > WM_BOARD_CONTROL_NONE &&
        hit.control <= WM_BOARD_CONTROL_MEMO_TRASH &&
        hit.control != WM_BOARD_CONTROL_MEMO) {
        board->button_focus[hit.control] = (BoardFocus){true, true, 0};
    } else if (hit.control == WM_BOARD_CONTROL_MEMO) {
        size_t position = visible_position(board, hit.memo_index);
        if (position != SIZE_MAX) {
            board->card_focus[position] = (BoardFocus){true, true, 0};
            promote_card(board, hit.memo_index);
        }
    }
}

static void retire_footer_focus(WmBoardScene *board) {
    board->hover = (WmBoardHit){WM_BOARD_CONTROL_NONE, SIZE_MAX};
    memset(board->button_focus, 0, sizeof(board->button_focus));
    board->arrow_press[0] = -1.0f;
    board->arrow_press[1] = -1.0f;
}

bool wm_board_scene_activate_secondary(WmBoardScene *board, WmBoardHit hit) {
    if (!board || board->dragging ||
        wm_board_compose_phase(board->compose) != WM_COMPOSE_EDIT) {
        return false;
    }
    return wm_board_compose_activate_secondary(board->compose,
                                                compose_control(hit.control));
}

bool wm_board_scene_activate(WmBoardScene *board, WmBoardHit hit) {
    if (!board) return false;
    if (board->dragging) return false;
    if (wm_board_erase_phase(board->erase) != WM_ERASE_CLOSED) {
        WmBoardEraseControl control = WM_ERASE_CONTROL_NONE;
        if (hit.control == WM_BOARD_CONTROL_ERASE_QUIT) {
            control = WM_ERASE_CONTROL_QUIT;
        } else if (hit.control == WM_BOARD_CONTROL_ERASE_OK) {
            control = WM_ERASE_CONTROL_OK;
        }
        return wm_board_erase_activate(board->erase, control);
    }
    if (wm_board_compose_phase(board->compose) != WM_COMPOSE_CLOSED) {
        return wm_board_compose_activate(board->compose,
                                         compose_control(hit.control));
    }
    if (wm_board_calendar_phase(board->calendar) != WM_CALENDAR_CLOSED) {
        WmBoardCalendarControl control = WM_CALENDAR_CONTROL_NONE;
        switch (hit.control) {
            case WM_BOARD_CONTROL_CALENDAR_BACK:
                control = WM_CALENDAR_CONTROL_BACK;
                break;
            case WM_BOARD_CONTROL_CALENDAR_PREVIOUS:
                control = WM_CALENDAR_CONTROL_PREVIOUS;
                break;
            case WM_BOARD_CONTROL_CALENDAR_NEXT:
                control = WM_CALENDAR_CONTROL_NEXT;
                break;
            case WM_BOARD_CONTROL_CALENDAR_DAY:
                control = WM_CALENDAR_CONTROL_DAY;
                break;
            default:
                break;
        }
        return wm_board_calendar_activate(board->calendar,
                 (WmBoardCalendarHit){control, (unsigned)hit.memo_index});
    }
    if (hit.control == WM_BOARD_CONTROL_MEMO_BACK &&
        board->phase == WM_BOARD_MEMO_READ) return wm_board_scene_back(board);
    if (board->phase == WM_BOARD_MEMO_READ &&
        hit.control == WM_BOARD_CONTROL_MEMO_SCROLL_UP) {
        return wm_board_reader_scroll_press(&board->reader_scroll,
                                             WM_BOARD_READER_ARROW_UP);
    }
    if (board->phase == WM_BOARD_MEMO_READ &&
        hit.control == WM_BOARD_CONTROL_MEMO_SCROLL_DOWN) {
        return wm_board_reader_scroll_press(&board->reader_scroll,
                                             WM_BOARD_READER_ARROW_DOWN);
    }
    if (hit.control == WM_BOARD_CONTROL_MEMO_TRASH &&
        board->phase == WM_BOARD_MEMO_READ &&
        board->selected < board->memo_count) {
        wm_board_reader_scroll_set_sound_enabled(&board->reader_scroll,
                                                  false);
        wm_board_reader_scroll_hover(&board->reader_scroll,
                                      WM_BOARD_READER_ARROW_NONE);
        board->phase = WM_BOARD_MEMO_TRASH_SELECT;
        board->phase_frame = 0.0f;
        board->hover = (WmBoardHit){WM_BOARD_CONTROL_NONE, SIZE_MAX};
        return true;
    }
    if (board->phase != WM_BOARD_READY) return false;
    switch (hit.control) {
        case WM_BOARD_CONTROL_BACK:
            return wm_board_scene_back(board);
        case WM_BOARD_CONTROL_CALENDAR:
            if (!wm_board_calendar_open(board->calendar, board->date,
                                         board->today)) return false;
            board->mask_direction = 1;
            board->mask_age = 0.0f;
            /* The child owns input until it closes. Retire the Board's
             * completed hover pose so it cannot return with the footer. */
            retire_footer_focus(board);
            return true;
        case WM_BOARD_CONTROL_CREATE:
            if (!wm_board_compose_open(board->compose)) return false;
            board->mask_direction = 1;
            board->mask_age = 0.0f;
            retire_footer_focus(board);
            return true;
        case WM_BOARD_CONTROL_PREVIOUS:
        case WM_BOARD_CONTROL_NEXT: {
            bool previous = hit.control == WM_BOARD_CONTROL_PREVIOUS;
            board->arrow_press[previous ? 0 : 1] = 0.0f;
            board->direction = previous ? -1 : 1;
            if ((previous && board->page + 1 <
                 wm_board_scene_memo_page_count(board)) ||
                (!previous && board->page > 0)) {
                board->phase = WM_BOARD_MEMO_PAGE;
                board->phase_frame = 0.0f;
                return true;
            }
            WmBoardDate next;
            if (!wm_board_date_shift(board->date,
                                     previous ? -1 : 1, &next)) return false;
            board->next_date = next;
            board->phase = WM_BOARD_DATE_SCROLL;
            board->phase_frame = 0.0f;
            return true;
        }
        case WM_BOARD_CONTROL_MEMO:
            if (hit.memo_index >= board->memo_count ||
                visible_position(board, hit.memo_index) == SIZE_MAX ||
                (hit.memo_index == board->arriving_memo &&
                 board->card_age < 11.0f)) return false;
            if (!configure_reader_scroll(
                    board, board->memos[hit.memo_index].text)) return false;
            board->selected = hit.memo_index;
            board->phase = WM_BOARD_MEMO_OPEN;
            board->phase_frame = 0.0f;
            /* The board footer hands control to the reader at selection.
             * Retire its old button and arrow focus before reader clips run. */
            retire_footer_focus(board);
            if (!board->memos[hit.memo_index].read) {
                board->memos[hit.memo_index].read = true;
                board->pending_action = WM_BOARD_ACTION_MEMO_READ;
                board->pending_memo_index = hit.memo_index;
            }
            return true;
        case WM_BOARD_CONTROL_NONE:
        case WM_BOARD_CONTROL_MEMO_BACK:
        case WM_BOARD_CONTROL_MEMO_TRASH:
        case WM_BOARD_CONTROL_MEMO_SCROLL_UP:
        case WM_BOARD_CONTROL_MEMO_SCROLL_DOWN:
        case WM_BOARD_CONTROL_CALENDAR_BACK:
        case WM_BOARD_CONTROL_CALENDAR_PREVIOUS:
        case WM_BOARD_CONTROL_CALENDAR_NEXT:
        case WM_BOARD_CONTROL_CALENDAR_DAY:
        case WM_BOARD_CONTROL_COMPOSE_BACK:
        case WM_BOARD_CONTROL_COMPOSE_MEMO:
        case WM_BOARD_CONTROL_COMPOSE_LETTER:
        case WM_BOARD_CONTROL_COMPOSE_ADDRESS:
        case WM_BOARD_CONTROL_COMPOSE_EDIT:
        case WM_BOARD_CONTROL_COMPOSE_POST:
        case WM_BOARD_CONTROL_COMPOSE_MII:
        case WM_BOARD_CONTROL_COMPOSE_SCROLL_UP:
        case WM_BOARD_CONTROL_COMPOSE_SCROLL_DOWN:
        case WM_BOARD_CONTROL_COMPOSE_ADDRESS_PREVIOUS:
        case WM_BOARD_CONTROL_COMPOSE_ADDRESS_NEXT:
        case WM_BOARD_CONTROL_COMPOSE_ADDRESS_WII:
        case WM_BOARD_CONTROL_COMPOSE_ADDRESS_OTHERS:
        case WM_BOARD_CONTROL_COMPOSE_ADDRESS_EDIT:
        case WM_BOARD_CONTROL_COMPOSE_ADDRESS_DIALOG_OK:
        case WM_BOARD_CONTROL_COMPOSE_ADDRESS_MII:
        case WM_BOARD_CONTROL_COMPOSE_ADDRESS_INFO:
        case WM_BOARD_CONTROL_COMPOSE_ADDRESS_CHANGE_NICKNAME:
        case WM_BOARD_CONTROL_COMPOSE_ADDRESS_ERASE:
        case WM_BOARD_CONTROL_COMPOSE_ADDRESS_DIALOG_YES:
        case WM_BOARD_CONTROL_COMPOSE_ADDRESS_DIALOG_NO:
        case WM_BOARD_CONTROL_COMPOSE_NETWORK_QUIT:
        case WM_BOARD_CONTROL_COMPOSE_NETWORK_SETTINGS:
        case WM_BOARD_CONTROL_COMPOSE_ADDRESS_ENTRY_FIRST:
        case WM_BOARD_CONTROL_COMPOSE_ADDRESS_ENTRY_LAST:
        case WM_BOARD_CONTROL_COMPOSE_KEY_FIRST:
        case WM_BOARD_CONTROL_COMPOSE_KEY_LAST:
        case WM_BOARD_CONTROL_ERASE_QUIT:
        case WM_BOARD_CONTROL_ERASE_OK:
            return false;
    }
    return false;
}
