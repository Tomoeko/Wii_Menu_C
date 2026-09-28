#ifndef WII_MENU_BOARD_SCENE_INTERNAL_H
#define WII_MENU_BOARD_SCENE_INTERNAL_H

#include "wii_menu/board/board_scene.h"
#include "board_model.h"
#include "wii_menu/board/board_calendar.h"
#include "wii_menu/board/board_compose.h"
#include "wii_menu/board/board_erase.h"
#include "wii_menu/board/board_reader_scroll.h"
#include "wii_menu/layout/layout_runtime.h"

#include <math.h>
#include <stdint.h>

/* Private to the Board scene and its presentation. Keep ownership and scene
 * state in one place; the public header remains opaque to callers. */
enum {
    BOARD_MEMOS_PER_PAGE = 10,
    BOARD_SOUND_CAPACITY = 32,
    BOARD_PASTE_DURATION = 11
};

typedef struct BoardPinState {
    WmBoardPinKind kind;
    bool sampled;
    float start_clock;
} BoardPinState;

typedef struct BoardFocus {
    bool active;
    bool entering;
    float frame;
} BoardFocus;

typedef struct BoardCardArrival {
    size_t memo_index;
    float age;
    bool started;
} BoardCardArrival;

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
    BoardModel model;
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
    BoardCardArrival card_arrivals[WM_BOARD_MAX_PRESENTED_MEMOS];
    size_t card_arrival_count;
    size_t pending_posted_memo;
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
    float drag_pitch;
    WmBoardDragCue drag_cues[8];
    float drag_cue_pans[8];
    unsigned drag_cue_count;
    const char *pending_reader_cue;
    WmBoardSoundEvent sound_events[BOARD_SOUND_CAPACITY];
    unsigned sound_count;
    uint64_t rng_state;
    WmBoardTimeNow pin_time_now;
    void *pin_time_context;
};

static inline float board_scene_clamp_frame(float frame, float length) {
    return fminf(fmaxf(frame, 0.0f), length);
}

static inline size_t board_scene_visible_position(const WmBoardScene *board,
                                                  size_t source_index) {
    for (size_t index = 0; index < board->visible_count; index++) {
        if (board->visible[index] == source_index)
            return index;
    }
    return SIZE_MAX;
}

static inline float board_scene_card_arrival_age(const WmBoardScene *board,
                                                 size_t memo_index) {
    if (memo_index == board->pending_posted_memo)
        return -1.0f;
    return memo_index < board->model.memo_count
               ? board->model.memos[memo_index].paste_age
               : BOARD_PASTE_DURATION;
}

/* Presentation and input both sample the same card state and pose the same
 * layouts, so hit boxes follow the rendered animation exactly. */
WmBoardPinKind board_scene_sample_pin_kind(WmBoardScene *board, size_t index);
void board_scene_pose_card(WmBoardScene *board, const WmBoardMemoPresentation *card,
                           bool parked);
void board_scene_card_matrix(const WmBoardScene *board, size_t position,
                             float matrix[12]);
void board_scene_pose_reader(WmBoardScene *board);
void board_scene_pose_footer(WmBoardScene *board);

#endif
