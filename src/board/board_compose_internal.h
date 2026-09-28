#ifndef WII_MENU_BOARD_COMPOSE_INTERNAL_H
#define WII_MENU_BOARD_COMPOSE_INTERNAL_H

#include "wii_menu/board/board_address.h"
#include "wii_menu/board/board_compose.h"
#include "wii_menu/layout/layout_runtime.h"

#include "board_compose_draft.h"
#include "board_compose_scroll.h"

#include <math.h>

typedef enum ComposeNetworkDialog {
    COMPOSE_NETWORK_CLOSED,
    COMPOSE_NETWORK_ENTER,
    COMPOSE_NETWORK_READY,
    COMPOSE_NETWORK_SELECT,
    COMPOSE_NETWORK_EXIT
} ComposeNetworkDialog;

/* The controller and presentation share one stable scene allocation. Layouts
 * and child controllers are released by wm_board_compose_destroy. */
struct WmBoardCompose {
    WmPlatform *platform;
    WmTextureCache *textures;
    WmFontCache *fonts;
    WmLayout *selector;
    WmLayout *body;
    WmLayout *footer;
    WmLayout *network_dialog;
    WmBoardKeyboard *keyboard;
    WmBoardAddress *address;
    bool address_keyboard_open;
    ComposeNetworkDialog network_phase;
    bool network_wii_connect24;
    float network_frame;
    WmBoardComposeControl network_selected;
    WmBoardComposePhase phase;
    float frame;
    float age;
    float keyboard_age;
    BoardComposeDraft draft;
    BoardComposeScroll scroll;
    WmBoardComposeControl hover;
    ComposeFocus focus[WM_COMPOSE_CONTROL_ADDRESS_ENTRY_LAST + 1];
    float address_arrow_press[2];
    WmBoardComposeOutcome outcome;
    const char *key_cues[32];
    size_t key_cue_count;
    bool inserting_symbol;
    bool inserting_phone;
    bool repeating_keytop;
};

static inline float clamp_frame(float value, float maximum) {
    return fminf(fmaxf(value, 0.0f), maximum);
}

static inline const char *memo_display_text(WmBoardCompose *compose,
                                            size_t *display_bytes) {
    return board_compose_draft_display(&compose->draft,
                                       compose->phase == WM_COMPOSE_EDIT,
                                       compose->keyboard, display_bytes);
}

#endif
