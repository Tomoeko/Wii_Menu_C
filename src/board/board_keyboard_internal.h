#ifndef WII_MENU_BOARD_KEYBOARD_INTERNAL_H
#define WII_MENU_BOARD_KEYBOARD_INTERNAL_H

#include "wii_menu/board/board_keyboard.h"
#include "wii_menu/board/keyboard_dictionary.h"
#include "wii_menu/layout/layout_runtime.h"

enum {
    SYMBOL_PAGE_COUNT = 10,
    SYMBOLS_PER_PAGE = 20,
    CANDIDATE_PANE_COUNT = 20,
    CANDIDATE_COUNT = 40,
    LOCAL_CANDIDATE_LIMIT = 20
};

typedef enum SymbolPhase {
    SYMBOL_CLOSED,
    SYMBOL_ENTERING,
    SYMBOL_OPEN,
    SYMBOL_SCROLL_PREV,
    SYMBOL_SCROLL_NEXT,
    SYMBOL_LEAVING
} SymbolPhase;

typedef enum LanguagePhase {
    LANGUAGE_CLOSED,
    LANGUAGE_ENTERING,
    LANGUAGE_OPEN,
    LANGUAGE_LEAVING
} LanguagePhase;

typedef struct KeyboardFocus {
    bool active;
    bool entering;
    bool resting;
    float frame;
} KeyboardFocus;

struct WmBoardKeyboard {
    WmPlatform *platform;
    WmTextureCache *textures;
    WmFontCache *fonts;
    WmLayout *keytop;
    WmLayout *toolbar;
    WmLayout *prediction;
    WmLayout *symbols;
    WmLayout *language;
    WmLayout *phone;
    WmLayout *background;
    WmLayout *text_box_small;
    WmLayout *text_box_big;
    WmBoardKeyboardProfile profile;
    bool memo_phone_layout;
    unsigned memo_phone_mode;
    KeyboardFocus focus[WM_KEYBOARD_CONTROL_LAST + 1];
    WmBoardKeyboardControl hovered;
    WmBoardKeyboardControl pressed;
    WmBoardKeyboardControl pressed_phone_hover;
    float press_frame;
    bool caps;
    bool shift;
    bool keytop_dirty;
    bool toolbar_dirty;
    bool prediction_dirty;
    bool symbols_dirty;
    bool language_dirty;
    bool phone_dirty;
    bool phone_layout;
    unsigned phone_mode;
    bool phone_pending;
    bool phone_just_committed;
    unsigned phone_pending_index;
    size_t phone_cycle_position;
    bool phone_pending_uppercase;
    float phone_pending_frames;
    bool prediction_enabled;
    bool prediction_animating;
    bool prediction_from;
    float prediction_frame;
    unsigned dictionary_language;
    LanguagePhase language_phase;
    float language_frame;
    size_t candidate_prefix_bytes;
    size_t accepted_prefix_bytes;
    char candidates[CANDIDATE_COUNT][WM_KEYBOARD_CANDIDATE_UTF8_CAPACITY];
    unsigned candidate_count;
    unsigned selected_candidate_index;
    float candidate_widths[CANDIDATE_COUNT];
    float candidate_screen_widths[CANDIDATE_COUNT];
    float candidate_positions[CANDIDATE_COUNT];
    unsigned candidate_pane_indices[CANDIDATE_PANE_COUNT];
    unsigned candidate_first;
    unsigned candidate_target;
    float candidate_scroll_frame;
    bool candidate_scrolling;
    char accepted_candidate[WM_KEYBOARD_CANDIDATE_UTF8_CAPACITY];
    WmKeyboardWordList oem_words[3];
    char **learned_words;
    size_t learned_count;
    size_t learned_capacity;
    char phone_prediction_digits[33];
    size_t phone_prediction_bytes;
    bool phone_prediction_uppercase;
    bool phone_prediction_awaiting_edit;
    char phone_prediction_previous_digits[33];
    size_t phone_prediction_previous_bytes;
    const char *text_context; /* Borrowed from the owning Memo composer. */
    size_t observed_text_bytes;
    size_t composition_start;
    SymbolPhase symbol_phase;
    float symbol_frame;
    unsigned symbol_page;
    unsigned symbol_target_page;
    WmBoardKeyboardControl held;
    uint16_t held_updates;
    double held_fraction;
};

static inline bool is_toolbar(WmBoardKeyboardControl control) {
    return control == WM_KEYBOARD_BACK || control == WM_KEYBOARD_OK ||
           control == WM_KEYBOARD_QWERTY || control == WM_KEYBOARD_PHONE;
}

static inline bool is_keytop(WmBoardKeyboardControl control) {
    return control >= WM_KEYBOARD_CHARACTER_FIRST &&
           (control <= WM_KEYBOARD_SPACE || control == WM_KEYBOARD_MORE ||
            control == WM_KEYBOARD_LANGUAGE);
}

static inline bool is_symbol(WmBoardKeyboardControl control) {
    return control >= WM_KEYBOARD_SYMBOL_FIRST &&
           control <= WM_KEYBOARD_SYMBOL_NEXT;
}

static inline bool is_phone_control(const WmBoardKeyboard *keyboard,
                                    WmBoardKeyboardControl control) {
    return (control >= WM_KEYBOARD_PHONE_FIRST &&
            control <= WM_KEYBOARD_PHONE_MODE_LAST) ||
           (keyboard->phone_layout &&
            (control == WM_KEYBOARD_DELETE ||
             control == WM_KEYBOARD_RETURN || control == WM_KEYBOARD_MORE ||
             control == WM_KEYBOARD_LANGUAGE));
}

static inline bool is_language_choice(WmBoardKeyboardControl control) {
    return control >= WM_KEYBOARD_LANGUAGE_ENGLISH &&
           control <= WM_KEYBOARD_LANGUAGE_SPANISH;
}

static inline bool is_prediction_control(WmBoardKeyboardControl control) {
    return control == WM_KEYBOARD_PREDICTION ||
           (control >= WM_KEYBOARD_CANDIDATE_FIRST &&
            control <= WM_KEYBOARD_CANDIDATE_NEXT);
}

static inline bool selected_tab(const WmBoardKeyboard *keyboard,
                                WmBoardKeyboardControl control) {
    if (control == WM_KEYBOARD_QWERTY) return !keyboard->phone_layout;
    if (control == WM_KEYBOARD_PHONE) return keyboard->phone_layout;
    return keyboard->phone_layout &&
           control >= WM_KEYBOARD_PHONE_MODE_FIRST &&
           control <= WM_KEYBOARD_PHONE_MODE_LAST &&
           (unsigned)(control - WM_KEYBOARD_PHONE_MODE_FIRST) ==
               keyboard->phone_mode;
}

/* The resource's symbol ordering is both displayed and inserted. Keep
 * presentation and activation indexed by the same immutable table. */
extern const char *const
    wm_board_keyboard_symbols[SYMBOL_PAGE_COUNT][SYMBOLS_PER_PAGE];

const char *wm_board_keyboard_phone_label(const WmBoardKeyboard *keyboard,
                                           unsigned index,
                                           char output[16]);
unsigned wm_board_keyboard_candidate_next_index(
    const WmBoardKeyboard *keyboard);
char wm_board_keyboard_key_character(const WmBoardKeyboard *keyboard,
                                     unsigned index);
void wm_board_keyboard_pose_prediction(WmBoardKeyboard *keyboard);
WmBoardKeyboardControl wm_board_keyboard_hit_unfiltered(
    WmBoardKeyboard *keyboard, int x, int y);

#endif
