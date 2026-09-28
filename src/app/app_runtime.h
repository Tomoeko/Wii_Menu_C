#ifndef WM_APP_RUNTIME_H
#define WM_APP_RUNTIME_H

#include "app_resources.h"
#include "board_input.h"
#include "frame_render.h"
#include "menu_pointer.h"
#include "scene_input.h"

#include <stdbool.h>
#include <stdint.h>

/* Input ownership survives scene changes so an interrupted press cannot
 * activate a control after HOME, a fade, or a restart. */
typedef struct WmAppInputState {
    WmAppMenuPointer menu_pointer;
    WmAppBoardInput board;
    WmAppSceneInput scene;
    WmHomeControl home_hovered;
    WmHomeControl home_pressed;
    int pointer_x;
    int pointer_y;
    int focused_slot;
    bool pointer_inside;
    bool keyboard_focus;
} WmAppInputState;

/* App-level flow state shared by pre-event transitions, event routing, and
 * scene updates. The HOME clocks are paused and reset at explicit handoffs. */
typedef struct WmAppFlowState {
    WmAppSceneFade fade;
    WmMenuRestartClock restart;
    WmBoardAction board_settings_request;
    WmStorageScene *active_storage;
    unsigned sd_page;
    int preview_running_slot;
    uint64_t started;
    uint64_t preview_started;
    uint64_t home_opened_at;
    float entrance_frames;
    float home_underlay_elapsed;
    float home_underlay_preview_elapsed;
    bool board_entry_hover_pending;
    bool board_visited;
    bool sd_help_seen;
    bool entrance_active;
} WmAppFlowState;

/* Borrowed references only. The resources bundle and renderer keep their
 * existing ownership and are destroyed by main in their original order. */
typedef struct WmAppRuntime {
    WmMenu *menu;
    WmAppResources *resources;
    WmAppRenderer *renderer;
    WmAppInputState *input;
    WmAppFlowState *flow;
} WmAppRuntime;

#endif
