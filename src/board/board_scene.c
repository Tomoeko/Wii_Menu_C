#define _POSIX_C_SOURCE 200809L

#include "board_scene_internal.h"

#include "wii_menu/input/source_hit.h"
#include "wii_menu/layout/layout_assets.h"
#include "wii_menu/layout/layout_runtime.h"
#include "wii_menu/render/material_prepare.h"

#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

enum {
    /* Local arrival spacing is independent of the authored animation length.
     * Native asynchronous I/O has no fixed cadence. */
    BOARD_MEMO_ARRIVAL_INTERVAL = 5
};

enum { BOARD_NEW_PIN_AGE_MS = 21600 * 1000 };

static int64_t local_time_milliseconds(void *context) {
    (void)context;
    struct timespec clock = {0};
    if (clock_gettime(CLOCK_REALTIME, &clock) != 0)
        return -1;
    return (int64_t)clock.tv_sec * INT64_C(1000) + clock.tv_nsec / 1000000;
}

void wm_board_scene_set_pin_clock(WmBoardScene *board, WmBoardTimeNow time_now,
                                  void *context) {
    if (!board)
        return;
    board->pin_time_now = time_now;
    board->pin_time_context = time_now ? context : NULL;
}

WmBoardPinKind board_scene_sample_pin_kind(WmBoardScene *board, size_t index) {
    BoardMemo *memo = &board->model.memos[index];
    if (memo->pin_sampled)
        return memo->pin_kind;
    memo->pin_start_clock = board->pin_age;
    /* Version 1 stores only the calendar day. Treat its unknown creation time
     * as an ordinary settled Memo rather than a newly arrived one. */
    if (memo->created_at_ms == 0) {
        memo->pin_kind = WM_BOARD_PIN_DEFAULT;
        memo->pin_sampled = true;
        return memo->pin_kind;
    }
    int64_t now = board->pin_time_now ? board->pin_time_now(board->pin_time_context)
                                      : local_time_milliseconds(NULL);
    if (now < 0)
        return WM_BOARD_PIN_NONE;
    int64_t elapsed = now - memo->created_at_ms;
    if (elapsed <= 0)
        memo->pin_kind = WM_BOARD_PIN_NONE;
    else if (elapsed < BOARD_NEW_PIN_AGE_MS)
        memo->pin_kind = WM_BOARD_PIN_NEW;
    else
        memo->pin_kind = WM_BOARD_PIN_DEFAULT;
    memo->pin_sampled = true;
    return memo->pin_kind;
}

static void reset_pin_choices(WmBoardScene *board) {
    for (size_t index = 0; index < board->model.memo_count; index++) {
        board->model.memos[index].pin_sampled = false;
    }
    board->pin_age = 0.0f;
}

static void promote_card(WmBoardScene *board, size_t source_index);

static WmBoardDate local_today(void) {
    time_t now = time(NULL);
    struct tm value;
    if (now == (time_t)-1 || !localtime_r(&now, &value)) {
        return (WmBoardDate){2000, 1, 1};
    }
    WmBoardDate date = {
        .year = value.tm_year + 1900, .month = value.tm_mon + 1, .day = value.tm_mday};
    return wm_board_date_valid(date) ? date : (WmBoardDate){2000, 1, 1};
}

static WmLayout *load_layout(const char *directory, const char *relative) {
    return wm_layout_load_asset(directory, relative, "Message Board");
}

static void update_visible(WmBoardScene *board) {
    size_t first = board->page * BOARD_MEMOS_PER_PAGE;
    board->day_count = 0;
    board->visible_count = 0;
    for (size_t position = 0; position < board->model.memo_count; position++) {
        size_t index = board->model.memo_order[position].index;
        if (!board_model_same_date(board->model.memos[index].date, board->date))
            continue;
        if (board->day_count >= first && board->visible_count < BOARD_MEMOS_PER_PAGE) {
            board->visible[board->visible_count] = index;
            board->card_order[board->visible_count] = index;
            board->visible_count++;
        }
        board->day_count++;
    }
    size_t pages = (board->day_count + BOARD_MEMOS_PER_PAGE - 1) / BOARD_MEMOS_PER_PAGE;
    if (pages == 0)
        pages = 1;
    if (board->page >= pages) {
        board->page = pages - 1;
        update_visible(board);
    }
}

WmBoardScene *wm_board_scene_create(WmPlatform *platform, const char *assets_directory,
                                    WmTextureCache *textures, WmFontCache *fonts) {
    if (!platform || !assets_directory || !assets_directory[0] || !textures || !fonts)
        return NULL;
    WmBoardScene *board = calloc(1, sizeof(*board));
    if (!board)
        return NULL;
    board->platform = platform;
    board->textures = textures;
    board->fonts = fonts;
    board->background = load_layout(assets_directory, "layouts/board/my_IplTop_c.json");
    board->footer = load_layout(assets_directory, "layouts/cmnBtn/my_IplTop_e.json");
    board->mask = load_layout(assets_directory, "layouts/board/my_BbsMask_a.json");
    board->reader = load_layout(assets_directory, "layouts/board/my_Memo_a.json");
    board->card = load_layout(assets_directory, "layouts/board/LetterS_a.json");
    board->calendar =
        wm_board_calendar_create(platform, assets_directory, textures, fonts);
    board->compose =
        wm_board_compose_create(platform, assets_directory, textures, fonts);
    board->erase = wm_board_erase_create(platform, assets_directory, textures, fonts);
    if (!board->background || !board->footer || !board->mask || !board->reader ||
        !board->card || !board->calendar || !board->compose || !board->erase) {
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
    board->pending_posted_memo = SIZE_MAX;
    board->hover.memo_index = SIZE_MAX;
    wm_board_reader_scroll_reset(&board->reader_scroll);
    board->arrow_press[0] = -1.0f;
    board->arrow_press[1] = -1.0f;
    struct timespec seed;
    if (clock_gettime(CLOCK_REALTIME, &seed) == 0) {
        board->rng_state = ((uint64_t)seed.tv_sec << 32) ^ (uint64_t)seed.tv_nsec;
    }
    if (!board->rng_state)
        board->rng_state = UINT64_C(0x9e3779b97f4a7c15);
    return board;
}

void wm_board_scene_destroy(WmBoardScene *board) {
    if (!board)
        return;
    wm_layout_destroy(board->background);
    wm_layout_destroy(board->footer);
    wm_layout_destroy(board->mask);
    wm_layout_destroy(board->reader);
    wm_layout_destroy(board->card);
    wm_board_calendar_destroy(board->calendar);
    wm_board_compose_destroy(board->compose);
    wm_board_erase_destroy(board->erase);
    free(board->last_erased_id);
    board_model_dispose(&board->model);
    free(board);
}

WmBoardContactStoreStatus wm_board_scene_load_contacts(WmBoardScene *board,
                                                       const char *path, char *error,
                                                       size_t error_capacity) {
    if (!board)
        return WM_BOARD_CONTACT_STORE_ERROR;
    return wm_board_compose_load_contacts(board->compose, path, error, error_capacity);
}

void wm_board_scene_reset(WmBoardScene *board) {
    if (!board)
        return;
    wm_board_calendar_reset(board->calendar);
    wm_board_compose_reset(board->compose);
    wm_board_erase_reset(board->erase);
    wm_board_reader_scroll_reset(&board->reader_scroll);
    board->phase = WM_BOARD_CLOSED;
    board->phase_frame = 0.0f;
    board->age = 0.0f;
    board->card_arrival_count = 0;
    board->pending_posted_memo = SIZE_MAX;
    for (size_t index = 0; index < board->model.memo_count; index++) {
        board->model.memos[index].paste_age = BOARD_PASTE_DURATION;
    }
    board->page = 0;
    board->selected = SIZE_MAX;
    board->dragging = false;
    board->dragged_index = SIZE_MAX;
    board->drag_gain = 0.0f;
    board->drag_pitch = 1.0f;
    board->drag_cue_count = 0;
    board->pending_reader_cue = NULL;
    board->sound_count = 0;
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

bool wm_board_scene_set_memos(WmBoardScene *board, const WmBoardMemo *memos,
                              size_t count) {
    if (!board)
        return false;
    BoardModel replacement = {0};
    if (!board_model_copy(&replacement, memos, count, BOARD_PASTE_DURATION)) {
        return false;
    }
    WmBoardDate *message_dates = count ? malloc(count * sizeof(*message_dates)) : NULL;
    if (count && !message_dates) {
        board_model_dispose(&replacement);
        return false;
    }
    for (size_t index = 0; index < count; index++) {
        message_dates[index] = replacement.memos[index].date;
    }
    bool dates_updated =
        wm_board_calendar_set_message_dates(board->calendar, message_dates, count);
    free(message_dates);
    if (!dates_updated) {
        board_model_dispose(&replacement);
        return false;
    }
    /* Keep the old model intact until the calendar accepts the same dates. */
    board_model_dispose(&board->model);
    board->model = replacement;
    board->selected = SIZE_MAX;
    board->dragging = false;
    board->dragged_index = SIZE_MAX;
    wm_board_reader_scroll_reset(&board->reader_scroll);
    board->page = 0;
    board->card_arrival_count = 0;
    board->pending_posted_memo = SIZE_MAX;
    update_visible(board);
    return true;
}

bool wm_board_scene_open(WmBoardScene *board, WmBoardDate date) {
    if (!board || board->phase != WM_BOARD_CLOSED || !wm_board_date_valid(date))
        return false;
    if (!board_model_same_date(board->date, date))
        reset_pin_choices(board);
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
    board->menu_arrow_clock_set = false;
    board->card_arrival_count = 0;
    board->pending_posted_memo = SIZE_MAX;
    board->hover = (WmBoardHit){WM_BOARD_CONTROL_NONE, SIZE_MAX};
    memset(board->button_focus, 0, sizeof(board->button_focus));
    memset(board->card_focus, 0, sizeof(board->card_focus));
    board->pending_action = WM_BOARD_ACTION_NONE;
    board->mask_direction = 0;
    board->mask_age = 0.0f;
    update_visible(board);
    for (size_t position = 0; position < board->visible_count; position++) {
        /* Today's existing cards are already present behind the Home Menu.
         * Opening the Board retains their settled pose throughout entry. */
        board->model.memos[board->visible[position]].paste_age = BOARD_PASTE_DURATION;
    }
    return true;
}

void wm_board_scene_set_grid_page(WmBoardScene *board, int page) {
    if (board && page >= 0 && page < 4)
        board->grid_page = page;
}

bool wm_board_scene_sd_button_frame(const WmBoardScene *board, float *frame) {
    if (!board || !frame)
        return false;
    if (board->phase == WM_BOARD_ENTER && board->phase_frame < 15.0f) {
        *frame = board->phase_frame;
        return true;
    }
    if (board->phase == WM_BOARD_EXIT && board->phase_frame >= 20.0f) {
        *frame = fmaxf(0.0f, 35.0f - board->phase_frame);
        return true;
    }
    return false;
}

WmBoardPhase wm_board_scene_phase(const WmBoardScene *board) {
    return board ? board->phase : WM_BOARD_CLOSED;
}

WmBoardChild wm_board_scene_child(const WmBoardScene *board) {
    if (!board)
        return WM_BOARD_CHILD_NONE;
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
    return board && (wm_board_compose_phase(board->compose) == WM_COMPOSE_EDIT ||
                     wm_board_compose_address_editor_active(board->compose));
}

void wm_board_scene_keyboard_modifiers(WmBoardScene *board, bool shift_down,
                                       bool caps_lock_on) {
    if (board)
        wm_board_compose_keyboard_modifiers(board->compose, shift_down, caps_lock_on);
}

bool wm_board_scene_move_memo_caret(WmBoardScene *board, WmKey direction) {
    return board && wm_board_compose_move_caret(board->compose, direction);
}

bool wm_board_scene_compose_keyboard_overlay_visible(const WmBoardScene *board) {
    return board && wm_board_compose_keyboard_overlay_visible(board->compose);
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
    if (!board || board->day_count == 0)
        return 1;
    return (board->day_count + BOARD_MEMOS_PER_PAGE - 1) / BOARD_MEMOS_PER_PAGE;
}

unsigned wm_board_scene_today_count(const WmBoardScene *board) {
    if (!board)
        return 0;
    unsigned found = 0;
    for (size_t index = 0; index < board->model.memo_count; index++) {
        if (board_model_same_date(board->model.memos[index].date, board->today) &&
            found < 99)
            found++;
    }
    return found;
}

unsigned wm_board_scene_today_unread_count(const WmBoardScene *board) {
    if (!board)
        return 0;
    unsigned found = 0;
    for (size_t index = 0; index < board->model.memo_count; index++) {
        if (board_model_same_date(board->model.memos[index].date, board->today) &&
            !board->model.memos[index].read)
            found++;
    }
    return found;
}

bool wm_board_scene_refresh_today(WmBoardScene *board, WmBoardDate today) {
    if (!board || board->phase != WM_BOARD_CLOSED || !wm_board_date_valid(today))
        return false;
    if (!board_model_same_date(board->today, today))
        reset_pin_choices(board);
    board->today = today;
    return true;
}

size_t wm_board_scene_memo_count(const WmBoardScene *board) {
    return board ? board->model.memo_count : 0;
}

bool wm_board_scene_get_memo(const WmBoardScene *board, size_t index,
                             WmBoardMemo *memo) {
    return board && board_model_get_memo(&board->model, index, memo);
}

const char *wm_board_scene_last_erased_id(const WmBoardScene *board) {
    return board ? board->last_erased_id : NULL;
}

WmBoardAction wm_board_scene_take_action(WmBoardScene *board, size_t *memo_index) {
    if (!board)
        return WM_BOARD_ACTION_NONE;
    WmBoardAction action = board->pending_action;
    if (memo_index)
        *memo_index = board->pending_memo_index;
    board->pending_action = WM_BOARD_ACTION_NONE;
    return action;
}

bool wm_board_scene_insert_text(WmBoardScene *board, const char *utf8) {
    if (!board || wm_board_compose_phase(board->compose) == WM_COMPOSE_CLOSED)
        return false;
    bool inserted = wm_board_compose_insert_text(board->compose, utf8);
    if (inserted)
        wm_board_compose_press_physical(board->compose, utf8);
    return inserted;
}

const char *wm_board_scene_take_compose_key_cue(WmBoardScene *board) {
    return board ? wm_board_compose_take_key_cue(board->compose) : NULL;
}

static WmBoardComposeControl compose_control(WmBoardControl control);

bool wm_board_scene_hold_compose_control(WmBoardScene *board, WmBoardControl control) {
    return board &&
           wm_board_compose_hold_control(board->compose, compose_control(control));
}

void wm_board_scene_release_compose_control(WmBoardScene *board) {
    if (board)
        wm_board_compose_release_control(board->compose);
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

static void queue_sound_event(WmBoardScene *board, const char *cue, float pan,
                              size_t memo_index) {
    if (board->sound_count >= BOARD_SOUND_CAPACITY)
        return;
    board->sound_events[board->sound_count++] = (WmBoardSoundEvent){
        .cue = cue, .pan = clamp_pan(pan), .memo_index = memo_index};
}

bool wm_board_scene_take_sound_event(WmBoardScene *board, WmBoardSoundEvent *event) {
    if (!board || !event || !board->sound_count)
        return false;
    *event = board->sound_events[0];
    board->sound_count--;
    if (board->sound_count) {
        memmove(board->sound_events, board->sound_events + 1,
                board->sound_count * sizeof(board->sound_events[0]));
    }
    return true;
}

static void advance_card_arrivals(WmBoardScene *board, float frames) {
    for (size_t index = 0; index < board->card_arrival_count; index++) {
        board->card_arrivals[index].age =
            fminf(100000.0f, board->card_arrivals[index].age + frames);
        board->model.memos[board->card_arrivals[index].memo_index].paste_age =
            board->card_arrivals[index].age;
    }
    for (;;) {
        size_t next = SIZE_MAX;
        for (size_t index = 0; index < board->card_arrival_count; index++) {
            const BoardCardArrival *arrival = &board->card_arrivals[index];
            if (arrival->started || arrival->age < 0.0f)
                continue;
            /* Larger final age means an earlier crossing within this update.
             * Equal starts retain their serial record-read order. */
            if (next == SIZE_MAX || arrival->age > board->card_arrivals[next].age)
                next = index;
        }
        if (next == SIZE_MAX)
            break;
        BoardCardArrival *arrival = &board->card_arrivals[next];
        arrival->started = true;
        board_scene_sample_pin_kind(board, arrival->memo_index);
        board->model.memos[arrival->memo_index].pin_start_clock =
            board->pin_age - arrival->age;
        /* Native audio divides object X by the projection's right edge.
         * The Board's shared renderer currently uses the wide IPL view. */
        queue_sound_event(board, "WIPL_SE_MSG_DISP",
                          board->model.memos[arrival->memo_index].x / 416.0f,
                          arrival->memo_index);
    }
}

static void start_date_arrivals(WmBoardScene *board, WmBoardDate date, size_t page,
                                bool preserve_outgoing, float elapsed) {
    size_t retained = 0;
    if (preserve_outgoing) {
        for (size_t index = 0; index < board->card_arrival_count; index++) {
            BoardCardArrival arrival = board->card_arrivals[index];
            /* Keep the outgoing card already in its appearance, but cancel
             * unread local records when navigation replaces their sheet. */
            if (arrival.age >= 0.0f && arrival.age < BOARD_PASTE_DURATION &&
                board_scene_visible_position(board, arrival.memo_index) != SIZE_MAX) {
                board->card_arrivals[retained++] = arrival;
            }
        }
    }
    board->card_arrival_count = retained;
    size_t skipped = 0;
    size_t arriving = 0;
    for (size_t position = 0;
         position < board->model.memo_count && arriving < BOARD_MEMOS_PER_PAGE;
         position++) {
        size_t index = board->model.memo_order[position].index;
        if (!board_model_same_date(board->model.memos[index].date, date))
            continue;
        if (skipped++ < page * BOARD_MEMOS_PER_PAGE)
            continue;
        if (board->phase == WM_BOARD_EXIT) {
            /* Home already owns today's cards. Preserve them during the
             * return slide; ordinary Board page changes replay arrivals. */
            board->model.memos[index].paste_age = BOARD_PASTE_DURATION;
            arriving++;
            continue;
        }
        board->model.memos[index].pin_sampled = false;
        board->card_arrivals[board->card_arrival_count++] = (BoardCardArrival){
            .memo_index = index,
            .age = elapsed - (float)arriving * BOARD_MEMO_ARRIVAL_INTERVAL};
        arriving++;
    }
    advance_card_arrivals(board, 0.0f);
}

static void cancel_unstarted_arrivals(WmBoardScene *board) {
    size_t retained = 0;
    for (size_t index = 0; index < board->card_arrival_count; index++) {
        if (board->card_arrivals[index].started) {
            board->card_arrivals[retained++] = board->card_arrivals[index];
        }
    }
    board->card_arrival_count = retained;
}

static bool begin_date_transition(WmBoardScene *board, WmBoardDate target,
                                  WmBoardPhase phase, bool play_page_cue,
                                  float elapsed) {
    if (!wm_board_date_valid(target))
        return false;
    int64_t selected_day = board_model_day_number(board->date);
    int64_t target_day = board_model_day_number(target);
    int direction = target_day > selected_day ? 1 : target_day < selected_day ? -1 : 0;
    if (phase == WM_BOARD_DATE_SCROLL && direction == 0)
        return false;
    board->next_date = target;
    board->direction = direction;
    board->return_direction = phase == WM_BOARD_EXIT ? direction : 0;
    board->phase = phase;
    board->phase_frame = 0.0f;
    if (direction != 0 && play_page_cue) {
        queue_sound_event(board, "page", 0.0f, SIZE_MAX);
    }
    if (phase == WM_BOARD_EXIT) {
        /* Home always parks today's first page, already present. This also
         * cancels delayed appearances when leaving shortly after a flip. */
        if (direction == 0) {
            /* An older overflow page is still visible during the return.
             * Its cards also retain a settled pose until the grid covers it. */
            for (size_t position = 0; position < board->visible_count; position++) {
                board->model.memos[board->visible[position]].paste_age =
                    BOARD_PASTE_DURATION;
            }
        }
        start_date_arrivals(board, target, 0, direction != 0, elapsed);
    } else if (direction != 0) {
        start_date_arrivals(board, target, 0, true, elapsed);
    }
    return true;
}

static void reveal_posted_memo(WmBoardScene *board, float elapsed) {
    size_t index = board->pending_posted_memo;
    if (index >= board->model.memo_count)
        return;
    board->pending_posted_memo = SIZE_MAX;
    board->card_arrival_count = 1;
    board->card_arrivals[0] = (BoardCardArrival){.memo_index = index, .age = elapsed};
    advance_card_arrivals(board, 0.0f);
}

void wm_board_scene_advance_parked(WmBoardScene *board, float frames) {
    if (!board || board->phase != WM_BOARD_CLOSED || !isfinite(frames) ||
        frames <= 0.0f)
        return;
    board->age += frames;
    board->pin_age += frames;
    advance_card_arrivals(board, frames);
}

static void queue_drag_cue(WmBoardScene *board, WmBoardDragCue cue, float pan) {
    if (board->drag_cue_count >= sizeof(board->drag_cues) / sizeof(board->drag_cues[0]))
        return;
    unsigned index = board->drag_cue_count++;
    board->drag_cues[index] = cue;
    board->drag_cue_pans[index] = pan;
}

bool wm_board_scene_dragging(const WmBoardScene *board) {
    return board && board->dragging;
}

bool wm_board_scene_pointer_down(WmBoardScene *board, WmBoardHit hit, int x, int y) {
    if (!board || board->phase != WM_BOARD_READY || board->dragging ||
        hit.control != WM_BOARD_CONTROL_MEMO ||
        hit.memo_index >= board->model.memo_count ||
        board_scene_visible_position(board, hit.memo_index) == SIZE_MAX ||
        board_scene_card_arrival_age(board, hit.memo_index) < 11.0f)
        return false;
    board->dragging = true;
    board->dragged_index = hit.memo_index;
    board->drag_start_x = x;
    board->drag_start_y = y;
    board->drag_delta_x = 0.0f;
    board->drag_delta_y = 0.0f;
    board->drag_last_x = 0.0f;
    board->drag_last_y = 0.0f;
    board->drag_gain = 0.0f;
    board->drag_pan = clamp_pan(board->model.memos[hit.memo_index].x / 304.0f);
    board->drag_pitch = 1.0f;
    promote_card(board, hit.memo_index);
    queue_drag_cue(board, WM_BOARD_DRAG_CUE_HOLD, board->drag_pan);
    return true;
}

bool wm_board_scene_pointer_move(WmBoardScene *board, int x, int y) {
    if (!board || !board->dragging)
        return false;
    /* The board uses 832 logical X units. Converting the 640-pixel raster
     * delta back through its 832/608 IPL root gives 608/640. */
    board->drag_delta_x =
        ((float)x - (float)board->drag_start_x) * (608.0f / (float)WM_FRAME_WIDTH);
    board->drag_delta_y = (float)board->drag_start_y - (float)y;
    return true;
}

bool wm_board_scene_pointer_up(WmBoardScene *board, int x, int y) {
    if (!board || !board->dragging || board->dragged_index >= board->model.memo_count)
        return false;
    wm_board_scene_pointer_move(board, x, y);
    return wm_board_scene_pointer_finish(board);
}

bool wm_board_scene_pointer_finish(WmBoardScene *board) {
    if (!board || !board->dragging || board->dragged_index >= board->model.memo_count)
        return false;
    size_t index = board->dragged_index;
    BoardMemo *memo = &board->model.memos[index];
    memo->x =
        board_model_clamp_position(memo->x + board->drag_delta_x, -230.0f, 230.0f);
    memo->y = board_model_clamp_position(memo->y + board->drag_delta_y, -80.0f, 180.0f);
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
    if (!board || !board->dragging)
        return false;
    board->dragging = false;
    board->dragged_index = SIZE_MAX;
    board->drag_gain = 0.0f;
    board->drag_pan = 0.0f;
    queue_drag_cue(board, WM_BOARD_DRAG_CUE_RELEASE, 0.0f);
    return true;
}

WmBoardDragCue wm_board_scene_take_drag_cue(WmBoardScene *board, float *pan) {
    if (!board || !board->drag_cue_count)
        return WM_BOARD_DRAG_CUE_NONE;
    WmBoardDragCue cue = board->drag_cues[0];
    if (pan)
        *pan = board->drag_cue_pans[0];
    board->drag_cue_count--;
    if (board->drag_cue_count) {
        memmove(board->drag_cues, board->drag_cues + 1,
                board->drag_cue_count * sizeof(board->drag_cues[0]));
        memmove(board->drag_cue_pans, board->drag_cue_pans + 1,
                board->drag_cue_count * sizeof(board->drag_cue_pans[0]));
    }
    return cue;
}

bool wm_board_scene_drag_mix(const WmBoardScene *board, float *gain, float *pan,
                             float *pitch) {
    if (!board || !board->dragging || !gain || !pan || !pitch)
        return false;
    *gain = board->drag_gain;
    *pan = board->drag_pan;
    *pitch = board->drag_pitch;
    return true;
}

float wm_board_scene_reader_scroll_offset(const WmBoardScene *board) {
    return board ? wm_board_reader_scroll_offset(&board->reader_scroll) : 0.0f;
}

float wm_board_scene_reader_scroll_limit(const WmBoardScene *board) {
    return board ? wm_board_reader_scroll_limit(&board->reader_scroll) : 0.0f;
}

bool wm_board_scene_reader_scroll_sound_active(const WmBoardScene *board) {
    return board && wm_board_reader_scroll_sound_active(&board->reader_scroll);
}

const char *wm_board_scene_take_reader_cue(WmBoardScene *board) {
    if (!board)
        return NULL;
    const char *cue = board->pending_reader_cue;
    board->pending_reader_cue = NULL;
    return cue;
}

bool wm_board_scene_reader_arrow_target_visible(const WmBoardScene *board,
                                                WmBoardControl control) {
    if (!board)
        return false;
    WmBoardReaderArrow arrow =
        control == WM_BOARD_CONTROL_MEMO_SCROLL_UP     ? WM_BOARD_READER_ARROW_UP
        : control == WM_BOARD_CONTROL_MEMO_SCROLL_DOWN ? WM_BOARD_READER_ARROW_DOWN
                                                       : WM_BOARD_READER_ARROW_NONE;
    return wm_board_reader_scroll_arrow_visible(&board->reader_scroll, arrow);
}

bool wm_board_scene_grid_overlay(const WmBoardScene *board, float *grid_frame) {
    if (!board || !grid_frame)
        return false;
    if (board->phase == WM_BOARD_ENTER && board->phase_frame < 20.0f) {
        *grid_frame = 70.0f + board->phase_frame;
        return true;
    }
    if (board->phase == WM_BOARD_EXIT) {
        *grid_frame = 100.0f + board_scene_clamp_frame(board->phase_frame, 20.0f);
        return true;
    }
    return false;
}

static size_t reader_line_count(WmBoardScene *board, const char *text) {
    WmFontPane pane;
    const char *font_name = NULL;
    if (wm_layout_pane_font(board->reader, "T_Letter", &pane, &font_name)) {
        WmCachedFont *face = wm_font_cache_resolve(board->fonts, font_name);
        const WmFontTextLayout *layout =
            face ? wm_font_cache_layout(face, text, &pane) : NULL;
        if (layout)
            return wm_font_text_layout_line_count(layout);
    }
    /* Missing local font exports still preserve explicit line breaks. */
    size_t lines = 1;
    for (const char *cursor = text; *cursor; cursor++) {
        if (*cursor == '\n')
            lines++;
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
        &board->reader_scroll, reader_line_count(board, text), body.size[1],
        header.size[1], footer.size[1]);
}

static bool at_first_day(const WmBoardScene *board) {
    return board_model_same_date(board->date, (WmBoardDate){2000, 1, 1});
}

static bool at_last_day(const WmBoardScene *board) {
    return board_model_same_date(board->date, (WmBoardDate){2035, 12, 31});
}

bool wm_board_scene_back(WmBoardScene *board) {
    if (!board)
        return false;
    if (board->dragging)
        return wm_board_scene_cancel_pointer(board);
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
        wm_board_reader_scroll_set_sound_enabled(&board->reader_scroll, false);
        wm_board_reader_scroll_hover(&board->reader_scroll, WM_BOARD_READER_ARROW_NONE);
        board->phase = WM_BOARD_MEMO_BACK_SELECT;
        board->phase_frame = 0.0f;
        board->hover = (WmBoardHit){WM_BOARD_CONTROL_NONE, SIZE_MAX};
        return true;
    }
    if (board->phase != WM_BOARD_READY)
        return false;
    wm_board_scene_hover(board, (WmBoardHit){WM_BOARD_CONTROL_NONE, SIZE_MAX});
    /* Returning to the Home Menu reuses the page slide, but does not press
     * either Board page arrow and therefore must not play its cue. */
    if (!begin_date_transition(board, board->today, WM_BOARD_EXIT, false, 0.0f))
        return false;
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
    if (!text || !text[0] || board->model.memo_count >= BOARD_MODEL_MAX_MEMOS)
        return false;
    size_t previous_count = board->model.memo_count;
    WmBoardMemo *records = calloc(previous_count + 1, sizeof(*records));
    BoardMemo *previous = board->model.memos;
    BoardPinState *pins =
        previous_count ? malloc(previous_count * sizeof(*pins)) : NULL;
    if (!records || (previous_count && !pins)) {
        free(records);
        free(pins);
        return false;
    }
    for (size_t index = 0; index < previous_count; index++) {
        pins[index] = (BoardPinState){.kind = previous[index].pin_kind,
                                      .sampled = previous[index].pin_sampled,
                                      .start_clock = previous[index].pin_start_clock};
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
                          (long long)clock.tv_sec, clock.tv_nsec, previous_count);
    if (length < 0 || length >= (int)sizeof(id)) {
        free(records);
        free(pins);
        return false;
    }
    int64_t created_at_ms =
        (int64_t)clock.tv_sec * INT64_C(1000) + clock.tv_nsec / 1000000;
    /* Native receive() evaluates Y before X, with separate float products. */
    float vertical = next_unit_random(board);
    float horizontal = next_unit_random(board);
    float y = 180.0f - 260.0f * vertical;
    float x = -230.0f + 460.0f * horizontal;
    records[previous_count] = (WmBoardMemo){
        .id = id,
        .text = text,
        /* The selected sheet owns the post's date. Its real creation time
         * remains independent metadata for ordering and the new-card pin. */
        .date = board->date,
        .created_at_ms = created_at_ms,
        .x = x,
        .y = y,
        .has_position = true,
        .read = false};
    bool added = wm_board_scene_set_memos(board, records, previous_count + 1);
    free(records);
    if (!added) {
        free(pins);
        return false;
    }
    /* Posting adds one card without reclassifying existing pins. The source
     * clears only the newly posted record's cached animation. */
    for (size_t index = 0; index < previous_count; index++) {
        board->model.memos[index].pin_sampled = pins[index].sampled;
        board->model.memos[index].pin_kind = pins[index].kind;
        board->model.memos[index].pin_start_clock = pins[index].start_clock;
    }
    free(pins);
    board->page = 0;
    board->pending_posted_memo = previous_count;
    update_visible(board);
    board->pending_action = WM_BOARD_ACTION_MEMO_POSTED;
    board->pending_memo_index = previous_count;
    return true;
}

static bool finish_memo_erase(WmBoardScene *board) {
    if (board->selected >= board->model.memo_count)
        return false;
    size_t deleted = board->selected;
    size_t new_count = board->model.memo_count - 1;
    WmBoardDate *dates = new_count ? malloc(new_count * sizeof(*dates)) : NULL;
    if (new_count && !dates)
        return false;
    for (size_t source = 0, target = 0; source < board->model.memo_count; source++) {
        if (source != deleted)
            dates[target++] = board->model.memos[source].date;
    }
    bool dates_updated =
        wm_board_calendar_set_message_dates(board->calendar, dates, new_count);
    free(dates);
    if (!dates_updated)
        return false;
    /* The model hands the removed ID to the scene for the persistence action. */
    char *id = board_model_remove(&board->model, deleted);
    board->selected = SIZE_MAX;
    board->card_arrival_count = 0;
    board->pending_posted_memo = SIZE_MAX;
    update_visible(board);
    free(board->last_erased_id);
    board->last_erased_id = id;
    board->pending_action = WM_BOARD_ACTION_ERASE_MEMO;
    board->pending_memo_index = deleted;
    return true;
}

static void advance_compose_child(WmBoardScene *board, float frames) {
    float remaining = frames;
    while (remaining > 0.0f &&
           wm_board_compose_phase(board->compose) != WM_COMPOSE_CLOSED) {
        float amount = remaining;
        /* Consume outcomes at their owning child's boundary: a large update
         * must retain the posted record before composer exit reports CLOSED. */
        float boundary = wm_board_compose_frames_to_boundary(board->compose);
        if (boundary > 0.0f)
            amount = fminf(amount, boundary);
        if (amount <= 0.0f)
            return;
        wm_board_compose_advance(board->compose, amount);
        remaining -= amount;
        WmBoardComposeOutcome outcome = wm_board_compose_take_outcome(board->compose);
        if (outcome == WM_COMPOSE_OUTCOME_POSTED) {
            append_posted_memo(board, wm_board_compose_text(board->compose));
        } else if (outcome == WM_COMPOSE_OUTCOME_CLOSED) {
            /* Local delivery begins when its card can be seen. The native
             * transport's delivery time is not an authored composer clock. */
            reveal_posted_memo(board, remaining);
            board->mask_direction = -1;
            board->mask_age = remaining;
        } else if (outcome == WM_COMPOSE_OUTCOME_OPEN_SETTINGS) {
            board->pending_action = WM_BOARD_ACTION_OPEN_SETTINGS;
        } else if (outcome == WM_COMPOSE_OUTCOME_OPEN_CONNECT24_SETTINGS) {
            board->pending_action = WM_BOARD_ACTION_OPEN_CONNECT24_SETTINGS;
        }
    }
}

static float advance_calendar_child(WmBoardScene *board, float frames) {
    float remaining = frames;
    while (remaining > 0.0f &&
           wm_board_calendar_phase(board->calendar) != WM_CALENDAR_CLOSED) {
        float amount = remaining;
        float boundary = wm_board_calendar_frames_to_boundary(board->calendar);
        if (boundary > 0.0f)
            amount = fminf(amount, boundary);
        if (amount <= 0.0f)
            return 0.0f;
        wm_board_calendar_advance(board->calendar, amount);
        remaining -= amount;
        WmBoardDate selected_date;
        WmBoardCalendarOutcome outcome =
            wm_board_calendar_take_outcome(board->calendar, &selected_date);
        if (outcome == WM_CALENDAR_OUTCOME_NONE)
            continue;
        if (outcome == WM_CALENDAR_OUTCOME_SELECTED) {
            /* Date selection already played its own source cue at the click.
             * Use the same slide without also playing the arrow's cue. */
            begin_date_transition(board, selected_date, WM_BOARD_DATE_SCROLL, false,
                                  remaining);
        }
        board->mask_direction = -1;
        board->mask_age = remaining;
        return remaining;
    }
    return remaining;
}

void wm_board_scene_advance(WmBoardScene *board, float frames) {
    if (!board || board->phase == WM_BOARD_CLOSED || !isfinite(frames) ||
        frames <= 0.0f)
        return;
    board->age += frames;
    board->pin_age += frames;
    advance_card_arrivals(board, frames);
    wm_board_reader_scroll_advance(&board->reader_scroll, frames);
    if (board->dragging && board->dragged_index < board->model.memo_count) {
        float delta_x = board->drag_delta_x - board->drag_last_x;
        float delta_y = board->drag_delta_y - board->drag_last_y;
        float speed = hypotf(delta_x, delta_y) / frames;
        board->drag_last_x = board->drag_delta_x;
        board->drag_last_y = board->drag_delta_y;
        board->drag_gain = fminf(1.0f, 2.0f * speed / 304.0f);
        board->drag_pan = clamp_pan(
            (board->model.memos[board->dragged_index].x + board->drag_delta_x) /
            304.0f);
        /* holdSEwithPosDis changes pitch only above 30 units per update.
         * Slower motion keeps the held voice's previous pitch. */
        if (speed > 30.0f)
            board->drag_pitch = speed / 30.0f;
    }
    if (board->mask_direction != 0) {
        board->mask_age += frames;
        if (board->mask_direction < 0 && board->mask_age >= 20.0f) {
            board->mask_direction = 0;
        }
    }
    float phase_frames = frames;
    if (wm_board_calendar_phase(board->calendar) != WM_CALENDAR_CLOSED)
        phase_frames = advance_calendar_child(board, frames);
    if (wm_board_compose_phase(board->compose) != WM_COMPOSE_CLOSED) {
        advance_compose_child(board, frames);
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
            wm_board_reader_scroll_set_active(&board->reader_scroll, false);
        } else if (outcome == WM_ERASE_OUTCOME_CANCEL) {
            board->phase = WM_BOARD_MEMO_TRASH_CANCEL;
            board->phase_frame = 0.0f;
        }
        return;
    }
    for (size_t index = 0;
         index < sizeof(board->button_focus) / sizeof(board->button_focus[0]);
         index++) {
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
    float remaining = phase_frames;
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
                return;
            case WM_BOARD_READY:
            case WM_BOARD_MEMO_READ:
            case WM_BOARD_MEMO_DIALOG:
                return;
        }
        float amount = fminf(remaining, duration - board->phase_frame);
        board->phase_frame += amount;
        remaining -= amount;
        if (board->phase_frame < duration)
            return;
        switch (board->phase) {
            case WM_BOARD_ENTER:
                board->phase = WM_BOARD_READY;
                break;
            case WM_BOARD_EXIT:
                board->date = board->next_date;
                board->page = 0;
                update_visible(board);
                board->return_direction = 0;
                board->phase = WM_BOARD_CLOSED;
                board->pending_action = WM_BOARD_ACTION_EXITED;
                break;
            case WM_BOARD_DATE_SCROLL:
                board->date = board->next_date;
                board->page = 0;
                board->pending_posted_memo = SIZE_MAX;
                update_visible(board);
                memset(board->card_focus, 0, sizeof(board->card_focus));
                board->phase = WM_BOARD_READY;
                break;
            case WM_BOARD_MEMO_PAGE:
                if (board->direction < 0)
                    board->page++;
                else if (board->page > 0)
                    board->page--;
                board->pending_posted_memo = SIZE_MAX;
                update_visible(board);
                start_date_arrivals(board, board->date, board->page, false, remaining);
                memset(board->card_focus, 0, sizeof(board->card_focus));
                board->phase = WM_BOARD_READY;
                break;
            case WM_BOARD_MEMO_OPEN:
                board->phase = WM_BOARD_MEMO_READ;
                wm_board_reader_scroll_set_active(&board->reader_scroll, true);
                break;
            case WM_BOARD_MEMO_BACK_SELECT:
                board->phase = WM_BOARD_MEMO_CLOSE;
                wm_board_reader_scroll_set_active(&board->reader_scroll, false);
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
                    wm_board_reader_scroll_set_active(&board->reader_scroll, true);
                }
                return;
            case WM_BOARD_MEMO_TRASH_CANCEL:
                board->phase = WM_BOARD_MEMO_READ;
                wm_board_reader_scroll_set_active(&board->reader_scroll, true);
                break;
            case WM_BOARD_MEMO_ERASE_CLOSE:
                board->phase = finish_memo_erase(board) ? WM_BOARD_READY
                                                        : WM_BOARD_MEMO_TRASH_CANCEL;
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
    return first.control == second.control && (first.control != WM_BOARD_CONTROL_MEMO ||
                                               first.memo_index == second.memo_index);
}

static WmBoardComposeControl compose_control(WmBoardControl control) {
    if (control >= WM_BOARD_CONTROL_COMPOSE_ADDRESS_ENTRY_FIRST &&
        control <= WM_BOARD_CONTROL_COMPOSE_ADDRESS_ENTRY_LAST) {
        return (WmBoardComposeControl)(WM_COMPOSE_CONTROL_ADDRESS_ENTRY_FIRST +
                                       control -
                                       WM_BOARD_CONTROL_COMPOSE_ADDRESS_ENTRY_FIRST);
    }
    if (control >= WM_BOARD_CONTROL_COMPOSE_KEY_FIRST &&
        control <= WM_BOARD_CONTROL_COMPOSE_KEY_LAST) {
        return (WmBoardComposeControl)(WM_COMPOSE_CONTROL_KEY_FIRST + control -
                                       WM_BOARD_CONTROL_COMPOSE_KEY_FIRST);
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
        return (WmBoardControl)(WM_BOARD_CONTROL_COMPOSE_ADDRESS_ENTRY_FIRST + control -
                                WM_COMPOSE_CONTROL_ADDRESS_ENTRY_FIRST);
    }
    if (control >= WM_COMPOSE_CONTROL_KEY_FIRST &&
        control <= WM_COMPOSE_CONTROL_KEY_LAST) {
        return (WmBoardControl)(WM_BOARD_CONTROL_COMPOSE_KEY_FIRST + control -
                                WM_COMPOSE_CONTROL_KEY_FIRST);
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
    return (float)x >= rect.x - margin && (float)x < rect.x + rect.width + margin &&
           (float)y >= rect.y - margin && (float)y < rect.y + rect.height + margin;
}

static bool footer_hit(WmBoardScene *board, const char *pane, int x, int y,
                       float margin) {
    WmSourceRect rect;
    return wm_source_pane_rect(board->footer, pane, true, WM_LAYOUT_IPL, NULL, &rect) &&
           point_in_rect(x, y, rect, margin);
}

static void promote_card(WmBoardScene *board, size_t source_index) {
    for (size_t index = 0; index < board->visible_count; index++) {
        if (board->card_order[index] != source_index)
            continue;
        if (index + 1 < board->visible_count) {
            memmove(&board->card_order[index], &board->card_order[index + 1],
                    (board->visible_count - index - 1) * sizeof(board->card_order[0]));
        }
        board->card_order[board->visible_count - 1] = source_index;
        return;
    }
}

WmBoardHit wm_board_scene_hit(WmBoardScene *board, int x, int y) {
    WmBoardHit none = {WM_BOARD_CONTROL_NONE, SIZE_MAX};
    if (board && board->dragging)
        return none;
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
    if (board && wm_board_calendar_phase(board->calendar) != WM_CALENDAR_CLOSED) {
        WmBoardCalendarHit calendar_hit = wm_board_calendar_hit(board->calendar, x, y);
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
            SIZE_MAX};
    }
    bool turning_page = board && (board->phase == WM_BOARD_DATE_SCROLL ||
                                  board->phase == WM_BOARD_MEMO_PAGE);
    if (!board || (board->phase != WM_BOARD_READY &&
                   board->phase != WM_BOARD_MEMO_READ && !turning_page))
        return none;
    board_scene_pose_footer(board);
    if (board->phase == WM_BOARD_MEMO_READ) {
        if (footer_hit(board, "B_CalExit", x, y, 0)) {
            return (WmBoardHit){WM_BOARD_CONTROL_MEMO_BACK, SIZE_MAX};
        }
        if (footer_hit(board, "B_Dust", x, y, 0)) {
            return (WmBoardHit){WM_BOARD_CONTROL_MEMO_TRASH, board->selected};
        }
        board_scene_pose_reader(board);
        WmSourceRect up;
        WmSourceRect down;
        bool up_found = wm_source_pane_rect(board->reader, "B_ArwR", true,
                                            WM_LAYOUT_IPL, NULL, &up);
        bool down_found = wm_source_pane_rect(board->reader, "B_ArwL", true,
                                              WM_LAYOUT_IPL, NULL, &down);
        WmBoardReaderArrow arrow = wm_board_reader_scroll_hit(
            &board->reader_scroll, (float)x, (float)y, up_found ? &up : NULL,
            down_found ? &down : NULL);
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
        footer_hit(board, "B_ArwL", x, y, 4))
        return board->hover;
    if (board->hover.control == WM_BOARD_CONTROL_NEXT &&
        footer_hit(board, "B_ArwR", x, y, 4))
        return board->hover;
    if (!turning_page && footer_hit(board, "B_Ch", x, y, 0)) {
        return (WmBoardHit){WM_BOARD_CONTROL_BACK, SIZE_MAX};
    }
    if (!turning_page && footer_hit(board, "B_Cal", x, y, 0)) {
        return (WmBoardHit){WM_BOARD_CONTROL_CALENDAR, SIZE_MAX};
    }
    if (!turning_page && footer_hit(board, "B_Add", x, y, 0)) {
        return (WmBoardHit){WM_BOARD_CONTROL_CREATE, SIZE_MAX};
    }
    if ((!at_first_day(board) ||
         board->page + 1 < wm_board_scene_memo_page_count(board)) &&
        footer_hit(board, "B_ArwL", x, y, 0)) {
        return (WmBoardHit){WM_BOARD_CONTROL_PREVIOUS, SIZE_MAX};
    }
    if ((!at_last_day(board) || board->page > 0) &&
        footer_hit(board, "B_ArwR", x, y, 0)) {
        return (WmBoardHit){WM_BOARD_CONTROL_NEXT, SIZE_MAX};
    }
    if (turning_page)
        return none;
    for (size_t position = board->visible_count; position > 0; position--) {
        size_t source_index = board->card_order[position - 1];
        size_t index = board_scene_visible_position(board, source_index);
        float arrival_age = board_scene_card_arrival_age(board, source_index);
        if (index == SIZE_MAX || arrival_age < 11.0f)
            continue;
        WmBoardPinKind pin = board_scene_sample_pin_kind(board, source_index);
        WmBoardMemoPresentation card = {
            .memo_index = source_index,
            .paste_frame = board_scene_clamp_frame(arrival_age, 10.0f),
            .next_page_frame = -1.0f,
            .pin_kind = pin,
            .pin_frame =
                board->pin_age - board->model.memos[source_index].pin_start_clock};
        board_scene_pose_card(board, &card, false);
        float matrix[12];
        board_scene_card_matrix(board, index, matrix);
        WmSourceRect rect;
        if (wm_source_pane_rect(board->card, "B_Letter", true, WM_LAYOUT_IPL, matrix,
                                &rect) &&
            point_in_rect(x, y, rect, 0)) {
            return (WmBoardHit){WM_BOARD_CONTROL_MEMO, source_index};
        }
    }
    return none;
}

void wm_board_scene_hover(WmBoardScene *board, WmBoardHit hit) {
    if (board && board->dragging)
        return;
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
    if (board && wm_board_calendar_phase(board->calendar) != WM_CALENDAR_CLOSED) {
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
        wm_board_calendar_hover(
            board->calendar, (WmBoardCalendarHit){control, (unsigned)hit.memo_index});
        return;
    }
    bool turning_page = board && (board->phase == WM_BOARD_DATE_SCROLL ||
                                  board->phase == WM_BOARD_MEMO_PAGE);
    if (!board || (board->phase != WM_BOARD_READY &&
                   board->phase != WM_BOARD_MEMO_READ && !turning_page))
        return;
    if (turning_page && hit.control != WM_BOARD_CONTROL_PREVIOUS &&
        hit.control != WM_BOARD_CONTROL_NEXT) {
        hit = (WmBoardHit){WM_BOARD_CONTROL_NONE, SIZE_MAX};
    }
    if (board->phase == WM_BOARD_MEMO_READ) {
        WmBoardReaderArrow arrow = WM_BOARD_READER_ARROW_NONE;
        if (hit.control == WM_BOARD_CONTROL_MEMO_SCROLL_UP) {
            arrow = WM_BOARD_READER_ARROW_UP;
        } else if (hit.control == WM_BOARD_CONTROL_MEMO_SCROLL_DOWN) {
            arrow = WM_BOARD_READER_ARROW_DOWN;
        }
        wm_board_reader_scroll_hover(&board->reader_scroll, arrow);
    }
    if (same_hit(board->hover, hit))
        return;
    WmBoardHit old = board->hover;
    if (old.control > WM_BOARD_CONTROL_NONE &&
        old.control <= WM_BOARD_CONTROL_MEMO_TRASH &&
        old.control != WM_BOARD_CONTROL_MEMO) {
        board->button_focus[old.control] = (BoardFocus){true, false, 0};
    } else if (old.control == WM_BOARD_CONTROL_MEMO) {
        size_t position = board_scene_visible_position(board, old.memo_index);
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
        size_t position = board_scene_visible_position(board, hit.memo_index);
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
    if (!board)
        return false;
    if (board->dragging)
        return false;
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
        return wm_board_compose_activate(board->compose, compose_control(hit.control));
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
        bool activated = wm_board_calendar_activate(
            board->calendar, (WmBoardCalendarHit){control, (unsigned)hit.memo_index});
        if (activated && control == WM_CALENDAR_CONTROL_DAY) {
            queue_sound_event(board, "dateSelect", 0.0f, SIZE_MAX);
        }
        return activated;
    }
    if (hit.control == WM_BOARD_CONTROL_MEMO_BACK && board->phase == WM_BOARD_MEMO_READ)
        return wm_board_scene_back(board);
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
        board->selected < board->model.memo_count) {
        wm_board_reader_scroll_set_sound_enabled(&board->reader_scroll, false);
        wm_board_reader_scroll_hover(&board->reader_scroll, WM_BOARD_READER_ARROW_NONE);
        board->phase = WM_BOARD_MEMO_TRASH_SELECT;
        board->phase_frame = 0.0f;
        board->hover = (WmBoardHit){WM_BOARD_CONTROL_NONE, SIZE_MAX};
        return true;
    }
    if (board->phase != WM_BOARD_READY)
        return false;
    switch (hit.control) {
        case WM_BOARD_CONTROL_BACK:
            return wm_board_scene_back(board);
        case WM_BOARD_CONTROL_CALENDAR:
            if (!wm_board_calendar_open(board->calendar, board->date, board->today))
                return false;
            board->mask_direction = 1;
            board->mask_age = 0.0f;
            /* The child owns input until it closes. Retire the Board's
             * completed hover pose so it cannot return with the footer. */
            retire_footer_focus(board);
            return true;
        case WM_BOARD_CONTROL_CREATE:
            if (!wm_board_compose_open(board->compose))
                return false;
            board->mask_direction = 1;
            board->mask_age = 0.0f;
            retire_footer_focus(board);
            return true;
        case WM_BOARD_CONTROL_PREVIOUS:
        case WM_BOARD_CONTROL_NEXT: {
            bool previous = hit.control == WM_BOARD_CONTROL_PREVIOUS;
            board->arrow_press[previous ? 0 : 1] = 0.0f;
            board->direction = previous ? -1 : 1;
            if ((previous && board->page + 1 < wm_board_scene_memo_page_count(board)) ||
                (!previous && board->page > 0)) {
                board->phase = WM_BOARD_MEMO_PAGE;
                board->phase_frame = 0.0f;
                cancel_unstarted_arrivals(board);
                queue_sound_event(board, "WIPL_SE_MSG_HOUSE",
                                  previous ? 300.0f / 416.0f : -300.0f / 416.0f,
                                  SIZE_MAX);
                return true;
            }
            WmBoardDate next;
            if (!wm_board_date_shift(board->date, previous ? -1 : 1, &next))
                return false;
            return begin_date_transition(board, next, WM_BOARD_DATE_SCROLL, true, 0.0f);
        }
        case WM_BOARD_CONTROL_MEMO:
            if (hit.memo_index >= board->model.memo_count ||
                board_scene_visible_position(board, hit.memo_index) == SIZE_MAX ||
                board_scene_card_arrival_age(board, hit.memo_index) < 11.0f)
                return false;
            if (!configure_reader_scroll(board,
                                         board->model.memos[hit.memo_index].text))
                return false;
            board->selected = hit.memo_index;
            board->phase = WM_BOARD_MEMO_OPEN;
            board->phase_frame = 0.0f;
            /* The board footer hands control to the reader at selection.
             * Retire its old button and arrow focus before reader clips run. */
            retire_footer_focus(board);
            if (!board->model.memos[hit.memo_index].read) {
                board->model.memos[hit.memo_index].read = true;
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
