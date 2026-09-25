#define _POSIX_C_SOURCE 200809L

#include "wii_menu/board_compose.h"
#include "wii_menu/board_address.h"

#include "wii_menu/layout_present.h"
#include "wii_menu/layout_runtime.h"
#include "wii_menu/material_prepare.h"
#include "wii_menu/source_hit.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

enum {
    COMPOSE_PATH_CAPACITY = 4096,
    COMPOSE_MAX_TEXT_BYTES = 4096,
    COMPOSE_CLIP_CAPACITY = 20
};

typedef struct ComposeFocus {
    bool active;
    bool entering;
    float frame;
} ComposeFocus;

typedef struct ComposeScrollArrow {
    bool visible;
    bool appeared;
    float appearance_frame;
    ComposeFocus focus;
    bool press_active;
    float press_frame;
} ComposeScrollArrow;

enum {
    COMPOSE_SCROLL_UP,
    COMPOSE_SCROLL_DOWN,
    COMPOSE_SCROLL_DIRECTIONS
};

typedef enum ComposeScrollMode {
    COMPOSE_SCROLL_DISPLAY,
    COMPOSE_SCROLL_EDITOR
} ComposeScrollMode;

typedef enum ComposeNetworkDialog {
    COMPOSE_NETWORK_CLOSED,
    COMPOSE_NETWORK_ENTER,
    COMPOSE_NETWORK_READY,
    COMPOSE_NETWORK_SELECT,
    COMPOSE_NETWORK_EXIT
} ComposeNetworkDialog;

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
    float network_frame;
    WmBoardComposeControl network_selected;
    WmBoardComposePhase phase;
    float frame;
    float age;
    float keyboard_age;
    char text[COMPOSE_MAX_TEXT_BYTES + 1];
    char display_text[COMPOSE_MAX_TEXT_BYTES +
                      WM_KEYBOARD_CANDIDATE_UTF8_CAPACITY];
    size_t text_bytes;
    WmBoardComposeControl hover;
    ComposeFocus focus[WM_COMPOSE_CONTROL_ADDRESS_ENTRY_LAST + 1];
    float address_arrow_press[2];
    ComposeScrollArrow arrows[2][COMPOSE_SCROLL_DIRECTIONS];
    float scroll_offset;
    float scroll_maximum;
    float scroll_line_height;
    float scroll_start;
    float scroll_target;
    float scroll_frame;
    size_t scroll_lines;
    bool scroll_moving;
    WmBoardComposeOutcome outcome;
    const char *key_cues[32];
    size_t key_cue_count;
    bool inserting_symbol;
    bool inserting_phone;
    bool repeating_keytop;
};

static bool close_address_keyboard(WmBoardCompose *compose, bool accepting);

static void queue_key_cue(WmBoardCompose *compose, const char *name) {
    if (compose->key_cue_count <
        sizeof(compose->key_cues) / sizeof(compose->key_cues[0])) {
        compose->key_cues[compose->key_cue_count++] = name;
    }
}

static float clamp_frame(float value, float maximum) {
    return fminf(fmaxf(value, 0.0f), maximum);
}

static void change_focus(ComposeFocus *focus, bool entering,
                         float enter_duration, float leave_duration) {
    float current_duration = focus->entering ? enter_duration : leave_duration;
    float current_progress = focus->active && current_duration > 0.0f
        ? clamp_frame(focus->frame, current_duration) / current_duration
        : 0.0f;
    float highlighted = focus->active
        ? (focus->entering ? current_progress : 1.0f - current_progress)
        : 0.0f;
    focus->active = true;
    focus->entering = entering;
    focus->frame = (entering ? highlighted : 1.0f - highlighted) *
        (entering ? enter_duration : leave_duration);
}

static bool visual_focus_control(WmBoardComposeControl control) {
    switch (control) {
        case WM_COMPOSE_CONTROL_BACK:
        case WM_COMPOSE_CONTROL_MEMO:
        case WM_COMPOSE_CONTROL_LETTER:
        case WM_COMPOSE_CONTROL_ADDRESS:
        case WM_COMPOSE_CONTROL_POST:
        case WM_COMPOSE_CONTROL_MII:
        case WM_COMPOSE_CONTROL_ADDRESS_PREVIOUS:
        case WM_COMPOSE_CONTROL_ADDRESS_NEXT:
            return true;
        default:
            return false;
    }
}

static float focus_duration(WmBoardComposeControl control, bool entering) {
    if (control == WM_COMPOSE_CONTROL_BACK ||
        control == WM_COMPOSE_CONTROL_POST) return entering ? 6.0f : 8.0f;
    if (control == WM_COMPOSE_CONTROL_ADDRESS_PREVIOUS ||
        control == WM_COMPOSE_CONTROL_ADDRESS_NEXT) return 15.0f;
    return 6.0f;
}

static WmLayout *load_layout(const char *directory, const char *relative) {
    char path[COMPOSE_PATH_CAPACITY];
    int length = snprintf(path, sizeof(path), "%s/%s", directory, relative);
    if (length < 0 || length >= (int)sizeof(path)) return NULL;
    char error[160] = {0};
    WmLayout *layout = wm_layout_load_json(path, error, sizeof(error));
    if (!layout) {
        fprintf(stderr, "Could not load Create Message layout %s: %s\n",
                relative, error);
    }
    return layout;
}

static void reset_scroll(WmBoardCompose *compose) {
    memset(compose->arrows, 0, sizeof(compose->arrows));
    for (size_t mode = 0; mode < 2; mode++) {
        for (size_t direction = 0; direction < COMPOSE_SCROLL_DIRECTIONS;
             direction++) {
            compose->arrows[mode][direction].appearance_frame = 11.0f;
            compose->arrows[mode][direction].focus.entering = true;
        }
    }
    compose->scroll_offset = 0.0f;
    compose->scroll_maximum = 0.0f;
    compose->scroll_start = 0.0f;
    compose->scroll_target = 0.0f;
    compose->scroll_frame = 0.0f;
    compose->scroll_lines = 1;
    compose->scroll_moving = false;
}

static const char *memo_display_text(WmBoardCompose *compose,
                                      size_t *display_bytes) {
    if (compose->phase == WM_COMPOSE_EDIT && compose->text_bytes > 0 &&
        compose->text[compose->text_bytes - 1] == ' ' &&
        wm_board_keyboard_phone_space_pending(compose->keyboard)) {
        static const char open_box[] = "\342\220\243"; /* U+2423 */
        size_t prefix = compose->text_bytes - 1;
        memcpy(compose->display_text, compose->text, prefix);
        memcpy(compose->display_text + prefix, open_box, sizeof(open_box));
        if (display_bytes) *display_bytes = prefix + sizeof(open_box) - 1;
        return compose->display_text;
    }
    WmBoardKeyboardComposition composition;
    if (compose->phase == WM_COMPOSE_EDIT &&
        wm_board_keyboard_composition(compose->keyboard, &composition) &&
        composition.prefix_bytes <= compose->text_bytes &&
        composition.preview_candidate &&
        strcmp(composition.preview_candidate, ">") != 0) {
        size_t prefix_start = compose->text_bytes - composition.prefix_bytes;
        size_t preview_bytes = strlen(composition.preview_candidate);
        if (prefix_start + preview_bytes < sizeof(compose->display_text)) {
            memcpy(compose->display_text, compose->text, prefix_start);
            memcpy(compose->display_text + prefix_start,
                   composition.preview_candidate, preview_bytes + 1);
            if (display_bytes) *display_bytes =
                composition.preview_hovered
                    ? prefix_start + preview_bytes : compose->text_bytes;
            return compose->display_text;
        }
    }
    if (display_bytes) *display_bytes = compose->text_bytes;
    return compose->text;
}

static size_t utf16_units(const char *text, size_t bytes) {
    size_t units = 0;
    for (size_t index = 0; index < bytes;) {
        unsigned char first = (unsigned char)text[index];
        size_t sequence = first >= 0xf0 && first <= 0xf4 ? 4 :
                          first >= 0xe0 && first <= 0xef ? 3 :
                          first >= 0xc2 && first <= 0xdf ? 2 : 1;
        if (sequence > bytes - index) sequence = 1;
        for (size_t tail = 1; tail < sequence; tail++) {
            if (((unsigned char)text[index + tail] & 0xc0u) != 0x80u) {
                sequence = 1;
                break;
            }
        }
        units += sequence == 4 ? 2 : 1;
        index += sequence;
    }
    return units;
}

static size_t memo_line_count(WmBoardCompose *compose) {
    WmFontPane pane;
    const char *font_name = NULL;
    const char *display = memo_display_text(compose, NULL);
    if (wm_layout_pane_font(compose->body, "T_Letter", &pane, &font_name)) {
        WmCachedFont *face = wm_font_cache_resolve(compose->fonts, font_name);
        const WmFontTextLayout *layout = face
            ? wm_font_cache_layout(face, display, &pane) : NULL;
        if (layout) return wm_font_text_layout_line_count(layout);
    }
    size_t lines = 1;
    for (const char *cursor = display; *cursor; cursor++) {
        if (*cursor == '\n') lines++;
    }
    return lines;
}

static ComposeScrollMode scroll_mode(const WmBoardCompose *compose) {
    return compose->phase == WM_COMPOSE_ENTER_EDIT ||
           compose->phase == WM_COMPOSE_EDIT ||
           compose->phase == WM_COMPOSE_LEAVE_EDIT
               ? COMPOSE_SCROLL_EDITOR : COMPOSE_SCROLL_DISPLAY;
}

static void set_arrow_visible(ComposeScrollArrow *arrow, bool visible) {
    if (arrow->visible == visible) return;
    arrow->visible = visible;
    arrow->appeared = arrow->appeared || visible;
    arrow->appearance_frame = 0.0f;
    if (!visible && arrow->focus.active && arrow->focus.entering) {
        arrow->focus = (ComposeFocus){true, false, 0.0f};
    }
}

static void refresh_scroll(WmBoardCompose *compose) {
    size_t lines = compose->scroll_lines;
    float height = compose->scroll_line_height;
    ComposeScrollMode mode = scroll_mode(compose);
    compose->scroll_maximum = mode == COMPOSE_SCROLL_EDITOR
        ? (lines > 2 ? (float)(lines - 2) * height : 0.0f)
        : fmaxf(0.0f, (float)(lines > 4 ? lines : 4) * height - 100.0f);
    if (compose->phase == WM_COMPOSE_EDIT &&
        compose->scroll_offset > compose->scroll_maximum &&
        !compose->scroll_moving) {
        compose->scroll_start = compose->scroll_offset;
        compose->scroll_target = compose->scroll_maximum;
        compose->scroll_frame = 0.0f;
        compose->scroll_moving = true;
    } else if (compose->phase != WM_COMPOSE_EDIT) {
        compose->scroll_offset = fminf(compose->scroll_offset,
                                       compose->scroll_maximum);
    }
    if (compose->scroll_moving) {
        compose->scroll_target = fminf(compose->scroll_target,
                                       compose->scroll_maximum);
    }
    bool display = compose->phase == WM_COMPOSE_ENTER_MEMO ||
                   compose->phase == WM_COMPOSE_MEMO;
    bool editor = compose->phase == WM_COMPOSE_EDIT;
    for (size_t direction = 0; direction < COMPOSE_SCROLL_DIRECTIONS;
         direction++) {
        bool within_range = direction == COMPOSE_SCROLL_UP
                                ? compose->scroll_offset > 0.001f
                                : compose->scroll_offset + 0.001f <
                                  compose->scroll_maximum;
        set_arrow_visible(&compose->arrows[COMPOSE_SCROLL_DISPLAY][direction],
                          display && within_range);
        if (compose->phase != WM_COMPOSE_LEAVE_EDIT) {
            set_arrow_visible(&compose->arrows[COMPOSE_SCROLL_EDITOR][direction],
                              editor && within_range);
        }
    }
}

static bool start_scroll(WmBoardCompose *compose, float target) {
    if (compose->scroll_moving) return false;
    target = fminf(fmaxf(target, 0.0f), compose->scroll_maximum);
    if (fabsf(target - compose->scroll_offset) < 0.001f) return false;
    compose->scroll_start = compose->scroll_offset;
    compose->scroll_target = target;
    compose->scroll_frame = 0.0f;
    compose->scroll_moving = true;
    return true;
}

WmBoardCompose *wm_board_compose_create(WmPlatform *platform,
                                         const char *assets_directory,
                                         WmTextureCache *textures,
                                         WmFontCache *fonts) {
    if (!platform || !assets_directory || !assets_directory[0] ||
        !textures || !fonts) return NULL;
    WmBoardCompose *compose = calloc(1, sizeof(*compose));
    if (!compose) return NULL;
    compose->platform = platform;
    compose->textures = textures;
    compose->fonts = fonts;
    compose->selector = load_layout(assets_directory,
                                     "layouts/mlAdSel/my_Mail_a.json");
    compose->body = load_layout(assets_directory,
                                 "layouts/sofkeybd/my_Memo_a.json");
    compose->footer = load_layout(assets_directory,
                                   "layouts/cmnBtn/my_IplTop_e.json");
    compose->network_dialog = load_layout(assets_directory,
        "layouts/dlgWdw/my_DialogWindow_a2.json");
    compose->keyboard = wm_board_keyboard_create(platform, assets_directory,
                                                   textures, fonts);
    compose->address = wm_board_address_create(platform, assets_directory,
                                                textures, fonts);
    if (!compose->selector || !compose->body || !compose->footer ||
        !compose->network_dialog ||
        !compose->keyboard || !compose->address) {
        wm_board_compose_destroy(compose);
        return NULL;
    }
    wm_layout_prepare_materials(platform, compose->selector);
    wm_layout_prepare_materials(platform, compose->body);
    wm_layout_prepare_materials(platform, compose->footer);
    wm_layout_prepare_materials(platform, compose->network_dialog);
    wm_board_keyboard_set_text_context(compose->keyboard, compose->text);
    WmLayoutPaneState body;
    compose->scroll_line_height =
        wm_layout_pane_state(compose->body, "N_Body", &body) &&
        body.size[1] > 0.0f ? body.size[1] : 42.0f;
    reset_scroll(compose);
    return compose;
}

void wm_board_compose_destroy(WmBoardCompose *compose) {
    if (!compose) return;
    wm_layout_destroy(compose->selector);
    wm_layout_destroy(compose->body);
    wm_layout_destroy(compose->footer);
    wm_layout_destroy(compose->network_dialog);
    wm_board_keyboard_destroy(compose->keyboard);
    wm_board_address_destroy(compose->address);
    free(compose);
}

WmBoardContactStoreStatus wm_board_compose_load_contacts(
    WmBoardCompose *compose, const char *path,
    char *error, size_t error_capacity) {
    if (!compose) return WM_BOARD_CONTACT_STORE_ERROR;
    return wm_board_address_load_contacts(compose->address, path,
                                           error, error_capacity);
}

void wm_board_compose_reset(WmBoardCompose *compose) {
    if (!compose) return;
    compose->phase = WM_COMPOSE_CLOSED;
    compose->frame = 0.0f;
    compose->age = 0.0f;
    compose->keyboard_age = 0.0f;
    compose->address_keyboard_open = false;
    compose->network_phase = COMPOSE_NETWORK_CLOSED;
    compose->network_frame = 0.0f;
    compose->network_selected = WM_COMPOSE_CONTROL_NONE;
    compose->text[0] = '\0';
    compose->text_bytes = 0;
    compose->hover = WM_COMPOSE_CONTROL_NONE;
    compose->outcome = WM_COMPOSE_OUTCOME_NONE;
    compose->key_cue_count = 0;
    compose->inserting_symbol = false;
    compose->repeating_keytop = false;
    memset(compose->focus, 0, sizeof(compose->focus));
    compose->address_arrow_press[0] = -1.0f;
    compose->address_arrow_press[1] = -1.0f;
    wm_board_keyboard_reset(compose->keyboard);
    wm_board_keyboard_set_text_context(compose->keyboard, compose->text);
    wm_board_address_reset(compose->address);
    reset_scroll(compose);
}

bool wm_board_compose_open(WmBoardCompose *compose) {
    if (!compose || compose->phase != WM_COMPOSE_CLOSED) return false;
    compose->phase = WM_COMPOSE_ENTER_SELECTOR;
    compose->frame = 0.0f;
    compose->age = 0.0f;
    compose->keyboard_age = 0.0f;
    compose->address_keyboard_open = false;
    compose->network_phase = COMPOSE_NETWORK_CLOSED;
    compose->network_frame = 0.0f;
    compose->network_selected = WM_COMPOSE_CONTROL_NONE;
    compose->hover = WM_COMPOSE_CONTROL_NONE;
    compose->outcome = WM_COMPOSE_OUTCOME_NONE;
    compose->key_cue_count = 0;
    compose->inserting_symbol = false;
    compose->repeating_keytop = false;
    compose->text[0] = '\0';
    compose->text_bytes = 0;
    memset(compose->focus, 0, sizeof(compose->focus));
    compose->address_arrow_press[0] = -1.0f;
    compose->address_arrow_press[1] = -1.0f;
    wm_board_keyboard_reset(compose->keyboard);
    wm_board_keyboard_set_text_context(compose->keyboard, compose->text);
    wm_board_address_reset(compose->address);
    reset_scroll(compose);
    return true;
}

WmBoardComposePhase wm_board_compose_phase(const WmBoardCompose *compose) {
    return compose ? compose->phase : WM_COMPOSE_CLOSED;
}

unsigned wm_board_compose_address_page(const WmBoardCompose *compose) {
    return compose ? wm_board_address_page(compose->address) : 0;
}

bool wm_board_compose_address_editor_active(const WmBoardCompose *compose) {
    return compose && compose->phase == WM_COMPOSE_ADDRESS &&
           compose->address_keyboard_open;
}

const char *wm_board_compose_address_text(const WmBoardCompose *compose) {
    return compose ? wm_board_address_text(compose->address) : NULL;
}

WmBoardComposeOutcome wm_board_compose_take_outcome(WmBoardCompose *compose) {
    if (!compose) return WM_COMPOSE_OUTCOME_NONE;
    WmBoardComposeOutcome outcome = compose->outcome;
    compose->outcome = WM_COMPOSE_OUTCOME_NONE;
    return outcome;
}

const char *wm_board_compose_take_key_cue(WmBoardCompose *compose) {
    if (!compose || compose->key_cue_count == 0) return NULL;
    const char *cue = compose->key_cues[0];
    compose->key_cue_count--;
    if (compose->key_cue_count) {
        memmove(compose->key_cues, compose->key_cues + 1,
                compose->key_cue_count * sizeof(compose->key_cues[0]));
    }
    return cue;
}

bool wm_board_compose_keyboard_overlay_visible(
    const WmBoardCompose *compose) {
    return compose &&
        (compose->phase == WM_COMPOSE_EDIT ||
         compose->address_keyboard_open) &&
        wm_board_keyboard_symbols_visible(compose->keyboard);
}

const char *wm_board_compose_text(const WmBoardCompose *compose) {
    return compose ? compose->text : NULL;
}

const char *wm_board_compose_display_text(WmBoardCompose *compose) {
    return compose ? memo_display_text(compose, NULL) : NULL;
}

static float phase_duration(WmBoardComposePhase phase) {
    switch (phase) {
        case WM_COMPOSE_ENTER_SELECTOR: return 39.0f;
        case WM_COMPOSE_ENTER_MEMO: return 26.0f;
        case WM_COMPOSE_ENTER_EDIT:
        case WM_COMPOSE_LEAVE_EDIT: return 30.0f;
        case WM_COMPOSE_POST_PRESS: return 20.0f;
        case WM_COMPOSE_SEND: return 51.0f;
        case WM_COMPOSE_EXIT_AFTER_POST: return 21.0f;
        case WM_COMPOSE_BACK_MEMO: return 63.0f;
        case WM_COMPOSE_BACK_SELECTOR: return 46.0f;
        case WM_COMPOSE_ADDRESS:
        case WM_COMPOSE_CLOSED:
        case WM_COMPOSE_SELECTOR:
        case WM_COMPOSE_MEMO:
        case WM_COMPOSE_EDIT: return 0.0f;
    }
    return 0.0f;
}

static void advance_network_dialog(WmBoardCompose *compose, float frames) {
    while (frames > 0.0f) {
        float duration = compose->network_phase == COMPOSE_NETWORK_ENTER
            ? 25.0f : compose->network_phase == COMPOSE_NETWORK_SELECT ||
                       compose->network_phase == COMPOSE_NETWORK_EXIT
                ? 21.0f : 0.0f;
        if (duration == 0.0f) return;
        float amount = fminf(frames, duration - compose->network_frame);
        compose->network_frame += amount;
        frames -= amount;
        if (compose->network_frame < duration) return;
        if (compose->network_phase == COMPOSE_NETWORK_ENTER) {
            compose->network_phase = COMPOSE_NETWORK_READY;
        } else if (compose->network_phase == COMPOSE_NETWORK_SELECT) {
            compose->network_phase = COMPOSE_NETWORK_EXIT;
        } else {
            bool open_settings = compose->network_selected ==
                WM_COMPOSE_CONTROL_NETWORK_SETTINGS;
            compose->network_phase = COMPOSE_NETWORK_CLOSED;
            compose->network_selected = WM_COMPOSE_CONTROL_NONE;
            if (open_settings)
                compose->outcome = WM_COMPOSE_OUTCOME_OPEN_SETTINGS;
        }
        compose->network_frame = 0.0f;
        if (compose->network_phase == COMPOSE_NETWORK_READY ||
            compose->network_phase == COMPOSE_NETWORK_CLOSED) return;
    }
}

void wm_board_compose_advance(WmBoardCompose *compose, float frames) {
    if (!compose || compose->phase == WM_COMPOSE_CLOSED ||
        !isfinite(frames) || frames <= 0.0f) return;
    compose->age += frames;
    advance_network_dialog(compose, frames);
    if (compose->phase == WM_COMPOSE_ADDRESS) {
        WmBoardAddressPhase previous_address_phase =
            wm_board_address_phase(compose->address);
        wm_board_address_advance(compose->address, frames);
        if ((previous_address_phase == WM_BOARD_ADDRESS_REVIEW_INFO_PRESS ||
             previous_address_phase == WM_BOARD_ADDRESS_CONTACT_INFO_PRESS ||
             previous_address_phase == WM_BOARD_ADDRESS_REVIEW_SAVE_EXIT) &&
            wm_board_address_dialog_active(compose->address))
            queue_key_cue(compose, "WIPL_SE_INFO_WINDOW");
        if (wm_board_address_phase(compose->address) ==
            WM_BOARD_ADDRESS_CLOSED) {
            compose->phase = WM_COMPOSE_SELECTOR;
            compose->frame = 0.0f;
        }
    }
    if (compose->phase != WM_COMPOSE_ADDRESS &&
        wm_board_address_dialog_active(compose->address)) {
        wm_board_address_advance(compose->address, frames);
    }
    if (compose->phase == WM_COMPOSE_ENTER_EDIT ||
        compose->phase == WM_COMPOSE_EDIT) compose->keyboard_age += frames;
    unsigned repeated = wm_board_keyboard_advance(compose->keyboard, frames);
    if ((compose->phase == WM_COMPOSE_EDIT ||
         compose->address_keyboard_open) && repeated > 0) {
        WmBoardKeyboardControl held = wm_board_keyboard_held_control(
            compose->keyboard);
        WmBoardComposeControl control = (WmBoardComposeControl)(
            WM_COMPOSE_CONTROL_KEY_FIRST + held - 1);
        compose->repeating_keytop = true;
        for (unsigned index = 0; index < repeated; index++)
            wm_board_compose_activate(compose, control);
        compose->repeating_keytop = false;
    }
    for (size_t index = 0; index <
         sizeof(compose->focus) / sizeof(compose->focus[0]); index++) {
        if (compose->focus[index].active) compose->focus[index].frame += frames;
    }
    for (size_t index = 0; index < 2; index++) {
        if (compose->address_arrow_press[index] >= 0.0f) {
            compose->address_arrow_press[index] += frames;
            if (compose->address_arrow_press[index] > 30.0f)
                compose->address_arrow_press[index] = -1.0f;
        }
    }
    for (size_t mode = 0; mode < 2; mode++) {
        for (size_t direction = 0; direction < COMPOSE_SCROLL_DIRECTIONS;
             direction++) {
            ComposeScrollArrow *arrow = &compose->arrows[mode][direction];
            arrow->appearance_frame += frames;
            if (arrow->focus.active) arrow->focus.frame += frames;
            if (arrow->press_active) {
                arrow->press_frame += frames;
                if (arrow->press_frame >= 7.0f) arrow->press_active = false;
            }
        }
    }
    if (compose->scroll_moving) {
        compose->scroll_frame = fminf(15.0f, compose->scroll_frame + frames);
        float progress = compose->scroll_frame / 15.0f;
        float eased = progress * progress * (3.0f - 2.0f * progress);
        compose->scroll_offset = compose->scroll_start +
            (compose->scroll_target - compose->scroll_start) * eased;
        if (compose->scroll_frame >= 15.0f) compose->scroll_moving = false;
        refresh_scroll(compose);
    }
    float remaining = frames;
    while (remaining > 0.0f) {
        float duration = phase_duration(compose->phase);
        if (duration == 0.0f) return;
        float amount = fminf(remaining, duration - compose->frame);
        compose->frame += amount;
        remaining -= amount;
        if (compose->frame < duration) return;
        WmBoardComposePhase completed = compose->phase;
        switch (compose->phase) {
            case WM_COMPOSE_ENTER_SELECTOR:
                compose->phase = WM_COMPOSE_SELECTOR;
                break;
            case WM_COMPOSE_ENTER_MEMO:
            case WM_COMPOSE_LEAVE_EDIT:
                compose->phase = WM_COMPOSE_MEMO;
                break;
            case WM_COMPOSE_ENTER_EDIT:
                compose->phase = WM_COMPOSE_EDIT;
                break;
            case WM_COMPOSE_POST_PRESS:
                compose->phase = WM_COMPOSE_SEND;
                break;
            case WM_COMPOSE_SEND:
                compose->phase = WM_COMPOSE_EXIT_AFTER_POST;
                compose->outcome = WM_COMPOSE_OUTCOME_POSTED;
                break;
            case WM_COMPOSE_EXIT_AFTER_POST:
            case WM_COMPOSE_BACK_SELECTOR:
                compose->phase = WM_COMPOSE_CLOSED;
                compose->outcome = WM_COMPOSE_OUTCOME_CLOSED;
                break;
            case WM_COMPOSE_BACK_MEMO:
                compose->phase = WM_COMPOSE_SELECTOR;
                break;
            case WM_COMPOSE_CLOSED:
            case WM_COMPOSE_SELECTOR:
            case WM_COMPOSE_MEMO:
            case WM_COMPOSE_EDIT:
            case WM_COMPOSE_ADDRESS:
                break;
        }
        compose->frame = 0.0f;
        if (completed == WM_COMPOSE_LEAVE_EDIT) {
            /* The arrows have already faded with the 30-frame editor exit.
             * Starting Fade_OUT at frame zero here would flash them back. */
            for (size_t direction = 0;
                 direction < COMPOSE_SCROLL_DIRECTIONS; direction++) {
                ComposeScrollArrow *editor =
                    &compose->arrows[COMPOSE_SCROLL_EDITOR][direction];
                editor->visible = false;
                editor->appearance_frame = 10.0f;
                editor->focus.active = false;
                editor->press_active = false;
            }
        }
        refresh_scroll(compose);
        if (completed == WM_COMPOSE_ENTER_EDIT &&
            compose->phase == WM_COMPOSE_EDIT) {
            (void)start_scroll(compose, compose->scroll_maximum);
        }
        if (compose->outcome != WM_COMPOSE_OUTCOME_NONE) return;
    }
}

static bool close_address_keyboard(WmBoardCompose *compose, bool accepting) {
    if (!compose || !compose->address_keyboard_open) return false;
    compose->address_keyboard_open = false;
    wm_board_compose_hover(compose, WM_COMPOSE_CONTROL_NONE);
    queue_key_cue(compose, accepting ? "WIPL_SE_SK_DECIDE_CLOSE" :
                                      "WIPL_SE_SK_CANCEL_CLOSE");
    if (accepting && wm_board_address_show_issue(compose->address))
        queue_key_cue(compose, "WIPL_SE_INFO_WINDOW");
    return true;
}

static bool select_network_dialog(WmBoardCompose *compose,
                                  WmBoardComposeControl control) {
    if (compose->network_phase != COMPOSE_NETWORK_READY ||
        (control != WM_COMPOSE_CONTROL_NETWORK_QUIT &&
         control != WM_COMPOSE_CONTROL_NETWORK_SETTINGS)) return false;
    wm_board_compose_hover(compose, WM_COMPOSE_CONTROL_NONE);
    compose->network_selected = control;
    compose->network_phase = COMPOSE_NETWORK_SELECT;
    compose->network_frame = 0.0f;
    queue_key_cue(compose, control == WM_COMPOSE_CONTROL_NETWORK_QUIT
        ? "WIPL_SE_CANCEL" : "WIPL_SE_DECIDE");
    return true;
}

bool wm_board_compose_back(WmBoardCompose *compose) {
    if (!compose) return false;
    wm_board_compose_release_control(compose);
    if (compose->network_phase != COMPOSE_NETWORK_CLOSED)
        return select_network_dialog(compose, WM_COMPOSE_CONTROL_NETWORK_QUIT);
    if (compose->address_keyboard_open) {
        if (wm_board_keyboard_symbols_visible(compose->keyboard)) {
            if (!wm_board_keyboard_back(compose->keyboard)) return false;
            compose->key_cue_count = 0;
            wm_board_compose_hover(compose, WM_COMPOSE_CONTROL_NONE);
            return true;
        }
        compose->key_cue_count = 0;
        return close_address_keyboard(compose, false);
    }
    if (compose->phase == WM_COMPOSE_MEMO &&
        wm_board_address_dialog_active(compose->address)) {
        if (!wm_board_address_dialog_accept(compose->address)) return false;
        wm_board_compose_hover(compose, WM_COMPOSE_CONTROL_NONE);
        queue_key_cue(compose, "WIPL_SE_DECIDE");
        return true;
    }
    if (compose->phase == WM_COMPOSE_EDIT) {
        if (wm_board_keyboard_symbols_visible(compose->keyboard)) {
            if (!wm_board_keyboard_back(compose->keyboard)) return false;
            compose->key_cue_count = 0;
            wm_board_compose_hover(compose, WM_COMPOSE_CONTROL_NONE);
            return true;
        }
        wm_board_keyboard_finish_composition(compose->keyboard);
        compose->phase = WM_COMPOSE_LEAVE_EDIT;
        compose->key_cue_count = 0;
        queue_key_cue(compose, "WIPL_SE_SK_CANCEL_CLOSE");
    } else if (compose->phase == WM_COMPOSE_MEMO) {
        compose->phase = WM_COMPOSE_BACK_MEMO;
        compose->key_cue_count = 0;
        queue_key_cue(compose, "WIPL_SE_CANCEL");
    } else if (compose->phase == WM_COMPOSE_ADDRESS) {
        bool modal = wm_board_address_dialog_active(compose->address);
        if (!wm_board_address_back(compose->address)) return false;
        compose->key_cue_count = 0;
        queue_key_cue(compose, modal ? "WIPL_SE_DECIDE" :
                                      "WIPL_SE_CANCEL");
    } else if (compose->phase == WM_COMPOSE_SELECTOR) {
        compose->phase = WM_COMPOSE_BACK_SELECTOR;
        compose->key_cue_count = 0;
        queue_key_cue(compose, "WIPL_SE_CANCEL");
    } else {
        return false;
    }
    compose->frame = 0.0f;
    wm_board_compose_hover(compose, WM_COMPOSE_CONTROL_NONE);
    refresh_scroll(compose);
    return true;
}

static bool has_nonspace_text(const WmBoardCompose *compose) {
    for (size_t index = 0; index < compose->text_bytes; index++) {
        if (compose->text[index] != ' ' && compose->text[index] != '\n' &&
            compose->text[index] != '\r' && compose->text[index] != '\t') {
            return true;
        }
    }
    return false;
}

static bool replace_keyboard_suffix(WmBoardCompose *compose,
                                    size_t prefix_bytes,
                                    const char *replacement,
                                    bool phone_prediction) {
    if (!compose || !replacement || !replacement[0]) return false;
    size_t replacement_bytes = strlen(replacement);
    if (compose->address_keyboard_open) {
        const char *field = wm_board_address_field_text(compose->address);
        if (!field) return false;
        size_t field_bytes = strlen(field);
        char previous_suffix[128];
        if (prefix_bytes > field_bytes ||
            prefix_bytes >= sizeof(previous_suffix)) return false;
        size_t start = field_bytes - prefix_bytes;
        if (start < field_bytes &&
            ((unsigned char)field[start] & 0xc0u) == 0x80u) return false;
        memcpy(previous_suffix, field + start, prefix_bytes);
        previous_suffix[prefix_bytes] = '\0';
        while (strlen(wm_board_address_field_text(compose->address)) > start) {
            if (!wm_board_address_backspace(compose->address)) return false;
        }
        if (!wm_board_address_insert_text(compose->address, replacement)) {
            if (prefix_bytes)
                (void)wm_board_address_insert_text(compose->address,
                                                   previous_suffix);
            return false;
        }
        wm_board_keyboard_text_changed(compose->keyboard,
                                        phone_prediction);
        compose->keyboard_age = 0.0f;
        return true;
    }
    if (compose->phase != WM_COMPOSE_EDIT ||
        prefix_bytes > compose->text_bytes ||
        replacement_bytes > COMPOSE_MAX_TEXT_BYTES -
                            (compose->text_bytes - prefix_bytes)) return false;
    size_t start = compose->text_bytes - prefix_bytes;
    if (start < compose->text_bytes &&
        ((unsigned char)compose->text[start] & 0xc0u) == 0x80u) return false;
    memcpy(compose->text + start, replacement, replacement_bytes + 1);
    compose->text_bytes = start + replacement_bytes;
    wm_board_keyboard_text_changed(compose->keyboard, phone_prediction);
    compose->keyboard_age = 0.0f;
    compose->scroll_lines = memo_line_count(compose);
    refresh_scroll(compose);
    return true;
}

static bool activate_control(WmBoardCompose *compose,
                             WmBoardComposeControl control, bool reverse) {
    if (!compose) return false;
    if (!compose->repeating_keytop)
        wm_board_compose_release_control(compose);
    if (control >= WM_COMPOSE_CONTROL_KEY_FIRST &&
        control <= WM_COMPOSE_CONTROL_KEY_LAST &&
        (compose->phase == WM_COMPOSE_EDIT ||
         compose->address_keyboard_open)) {
        WmBoardKeyboardControl key = (WmBoardKeyboardControl)(
            control - WM_COMPOSE_CONTROL_KEY_FIRST + 1);
        char character[5];
        WmBoardKeyboardAction action = wm_board_keyboard_activate(
            compose->keyboard, key, reverse, character);
        if (!compose->repeating_keytop) compose->key_cue_count = 0;
        switch (action) {
            case WM_KEYBOARD_ACTION_HANDLED:
                queue_key_cue(compose, "WIPL_SE_SK_SWITCHING_02");
                return true;
            case WM_KEYBOARD_ACTION_INSERT: {
                compose->inserting_symbol =
                    key >= WM_KEYBOARD_SYMBOL_FIRST &&
                    key <= WM_KEYBOARD_SYMBOL_LAST;
                compose->inserting_phone =
                    key >= WM_KEYBOARD_PHONE_FIRST &&
                    key <= WM_KEYBOARD_PHONE_LAST;
                bool phone_space = compose->inserting_phone &&
                                   character[0] == ' ';
                bool inserted = wm_board_compose_insert_text(compose,
                                                               character);
                compose->inserting_symbol = false;
                compose->inserting_phone = false;
                if (!inserted) {
                    wm_board_keyboard_clear_phone_pending(compose->keyboard);
                    queue_key_cue(compose, "WIPL_SE_CHAR_DELETE_ERROR");
                    return true;
                }
                if (compose->key_cue_count == 0)
                    queue_key_cue(compose,
                                  key == WM_KEYBOARD_SPACE ||
                                  key == WM_KEYBOARD_RETURN ||
                                  phone_space
                                      ? "WIPL_SE_CHAR_DECIDE" :
                                        "WIPL_SE_CHAR_INPUT");
                return true;
            }
            case WM_KEYBOARD_ACTION_REPLACE_LAST:
                if (compose->address_keyboard_open) {
                    bool replaced = wm_board_address_backspace(compose->address)
                        && wm_board_address_insert_text(compose->address,
                                                         character);
                    queue_key_cue(compose, replaced ? "WIPL_SE_CHAR_INPUT" :
                                                "WIPL_SE_CHAR_DELETE_ERROR");
                    return true;
                }
                if (compose->text_bytes == 0) {
                    wm_board_keyboard_clear_phone_pending(compose->keyboard);
                    queue_key_cue(compose, "WIPL_SE_CHAR_DELETE_ERROR");
                    return true;
                }
                compose->text[compose->text_bytes - 1] = character[0];
                compose->keyboard_age = 0.0f;
                wm_board_keyboard_text_changed(compose->keyboard, true);
                queue_key_cue(compose, "WIPL_SE_CHAR_INPUT");
                return true;
            case WM_KEYBOARD_ACTION_DELETE:
                queue_key_cue(compose,
                    wm_board_compose_backspace(compose)
                        ? "WIPL_SE_CHAR_DELETE" :
                          "WIPL_SE_CHAR_DELETE_ERROR");
                return true;
            case WM_KEYBOARD_ACTION_CLOSE_BACK:
            case WM_KEYBOARD_ACTION_CLOSE_OK:
                if (compose->address_keyboard_open)
                    return close_address_keyboard(compose,
                        action == WM_KEYBOARD_ACTION_CLOSE_OK);
                if (!wm_board_compose_finish_edit(compose)) return false;
                compose->key_cues[0] = action == WM_KEYBOARD_ACTION_CLOSE_OK
                    ? "WIPL_SE_SK_DECIDE_CLOSE" :
                      "WIPL_SE_SK_CANCEL_CLOSE";
                return true;
            case WM_KEYBOARD_ACTION_SYMBOL_OPEN:
                queue_key_cue(compose, "WIPL_SE_SYMBOL_PAGE_OPEN");
                return true;
            case WM_KEYBOARD_ACTION_SYMBOL_CLOSE:
                queue_key_cue(compose, "WIPL_SE_CHAR_DECIDE");
                return true;
            case WM_KEYBOARD_ACTION_SYMBOL_PAGE:
                queue_key_cue(compose, "WSD_SELECT");
                return true;
            case WM_KEYBOARD_ACTION_LAYOUT_QWERTY:
                queue_key_cue(compose, "WIPL_SE_SK_SWITCHING_01");
                return true;
            case WM_KEYBOARD_ACTION_LAYOUT_PHONE:
                queue_key_cue(compose, "WIPL_SE_SK_SWITCH_TO_KETAI");
                return true;
            case WM_KEYBOARD_ACTION_PHONE_MODE:
                queue_key_cue(compose, "WIPL_SE_SK_SWITCHING_02");
                return true;
            case WM_KEYBOARD_ACTION_DICTIONARY_OPEN:
                queue_key_cue(compose, "WIPL_SE_SYMBOL_PAGE_OPEN");
                return true;
            case WM_KEYBOARD_ACTION_DICTIONARY_CLOSE:
                queue_key_cue(compose, "WIPL_SE_CHAR_DECIDE");
                return true;
            case WM_KEYBOARD_ACTION_DICTIONARY_LANGUAGE:
                queue_key_cue(compose, "WIPL_SE_SK_SWITCHING_02");
                return true;
            case WM_KEYBOARD_ACTION_PREDICTION_TOGGLE:
                queue_key_cue(compose,
                    wm_board_keyboard_prediction_enabled(compose->keyboard)
                        ? "WIPL_SE_SK_PREDICT_ON" :
                          "WIPL_SE_SK_PREDICT_OFF");
                return true;
            case WM_KEYBOARD_ACTION_CANDIDATE_PAGE:
                queue_key_cue(compose, "WIPL_SE_LINE_SCROLL");
                return true;
            case WM_KEYBOARD_ACTION_PHONE_BOUNDARY: {
                WmBoardKeyboardComposition composition;
                if (compose->address_keyboard_open ||
                    !wm_board_keyboard_composition(compose->keyboard,
                                                    &composition) ||
                    composition.prefix_bytes > compose->text_bytes ||
                    !composition.selected_candidate) {
                    queue_key_cue(compose, "WIPL_SE_CHAR_DELETE_ERROR");
                    return true;
                }
                char selected[WM_KEYBOARD_CANDIDATE_UTF8_CAPACITY];
                size_t selected_bytes =
                    strlen(composition.selected_candidate);
                size_t base = compose->text_bytes - composition.prefix_bytes;
                if (selected_bytes == 0 ||
                    selected_bytes >= sizeof(selected) ||
                    selected_bytes >= COMPOSE_MAX_TEXT_BYTES - base) {
                    queue_key_cue(compose, "WIPL_SE_CHAR_DELETE_ERROR");
                    return true;
                }
                memcpy(selected, composition.selected_candidate,
                       selected_bytes + 1);
                if (!replace_keyboard_suffix(compose,
                                             composition.prefix_bytes,
                                             selected, false)) {
                    queue_key_cue(compose, "WIPL_SE_CHAR_DELETE_ERROR");
                    return true;
                }
                wm_board_keyboard_finish_composition(compose->keyboard);
                WmBoardKeyboardAction next = wm_board_keyboard_activate(
                    compose->keyboard, key, reverse, character);
                const char *first = wm_board_keyboard_candidate_text(
                    compose->keyboard);
                bool inserted = next == WM_KEYBOARD_ACTION_PREDICT_PHONE &&
                    first && first[0] &&
                    replace_keyboard_suffix(compose,
                        wm_board_keyboard_candidate_prefix_bytes(
                            compose->keyboard), first, true);
                if (!inserted)
                    wm_board_keyboard_clear_phone_pending(compose->keyboard);
                queue_key_cue(compose, inserted
                    ? "WIPL_SE_CHAR_DECIDE" : "WIPL_SE_CHAR_DELETE_ERROR");
                return true;
            }
            case WM_KEYBOARD_ACTION_ACCEPT_CANDIDATE:
            case WM_KEYBOARD_ACTION_PREDICT_PHONE: {
                char replacement[WM_KEYBOARD_CANDIDATE_UTF8_CAPACITY];
                const char *selected =
                    wm_board_keyboard_candidate_text(compose->keyboard);
                if (!selected || strlen(selected) >= sizeof(replacement)) {
                    queue_key_cue(compose, "WIPL_SE_CHAR_DELETE_ERROR");
                    return true;
                }
                memcpy(replacement, selected, strlen(selected) + 1);
                bool predicted = action == WM_KEYBOARD_ACTION_PREDICT_PHONE;
                bool inserted = replace_keyboard_suffix(compose,
                    wm_board_keyboard_candidate_prefix_bytes(
                        compose->keyboard), replacement, predicted);
                if (inserted && !predicted)
                    wm_board_keyboard_finish_composition(compose->keyboard);
                queue_key_cue(compose, inserted
                    ? predicted ? "WIPL_SE_CHAR_INPUT" :
                                  "WIPL_SE_CHAR_DECIDE"
                    : "WIPL_SE_CHAR_DELETE_ERROR");
                return true;
            }
            case WM_KEYBOARD_ACTION_NONE:
                return false;
        }
    }
    if (compose->network_phase != COMPOSE_NETWORK_CLOSED)
        return select_network_dialog(compose, control);
    if (control == WM_COMPOSE_CONTROL_BACK) return wm_board_compose_back(compose);
    if (compose->phase == WM_COMPOSE_MEMO &&
        wm_board_address_dialog_active(compose->address)) {
        if (control != WM_COMPOSE_CONTROL_ADDRESS_DIALOG_OK ||
            !wm_board_address_dialog_accept(compose->address)) return false;
        wm_board_compose_hover(compose, WM_COMPOSE_CONTROL_NONE);
        queue_key_cue(compose, "WIPL_SE_DECIDE");
        return true;
    }
    if (compose->phase == WM_COMPOSE_ADDRESS) {
        if (wm_board_address_dialog_active(compose->address)) {
            if (control == WM_COMPOSE_CONTROL_ADDRESS_DIALOG_YES ||
                control == WM_COMPOSE_CONTROL_ADDRESS_DIALOG_NO) {
                bool yes = control == WM_COMPOSE_CONTROL_ADDRESS_DIALOG_YES;
                if (!wm_board_address_dialog_choose(compose->address, yes))
                    return false;
                wm_board_compose_hover(compose, WM_COMPOSE_CONTROL_NONE);
                queue_key_cue(compose,
                    yes ? "WIPL_SE_DECIDE" : "WIPL_SE_CANCEL");
                return true;
            }
            if (control != WM_COMPOSE_CONTROL_ADDRESS_DIALOG_OK ||
                !wm_board_address_dialog_accept(compose->address))
                return false;
            wm_board_compose_hover(compose, WM_COMPOSE_CONTROL_NONE);
            queue_key_cue(compose, "WIPL_SE_DECIDE");
            return true;
        }
        if (compose->address_keyboard_open) return false;
        if (control >= WM_COMPOSE_CONTROL_ADDRESS_ENTRY_FIRST &&
            control <= WM_COMPOSE_CONTROL_ADDRESS_ENTRY_LAST) {
            unsigned row = (unsigned)(control -
                WM_COMPOSE_CONTROL_ADDRESS_ENTRY_FIRST);
            if (!wm_board_address_select_entry(compose->address, row))
                return false;
            wm_board_compose_hover(compose, WM_COMPOSE_CONTROL_NONE);
            queue_key_cue(compose, "WIPL_SE_DECIDE");
            return true;
        }
        if (control == WM_COMPOSE_CONTROL_ADDRESS_MII) {
            if (!wm_board_address_show_no_mii(compose->address)) return false;
            wm_board_compose_hover(compose, WM_COMPOSE_CONTROL_NONE);
            queue_key_cue(compose, "WIPL_SE_INFO_WINDOW");
            queue_key_cue(compose, "WIPL_SE_DECIDE");
            return true;
        }
        if (control == WM_COMPOSE_CONTROL_ADDRESS_INFO) {
            if (!wm_board_address_show_address_info(compose->address))
                return false;
            wm_board_compose_hover(compose, WM_COMPOSE_CONTROL_NONE);
            queue_key_cue(compose, "WIPL_SE_DECIDE");
            return true;
        }
        if (control == WM_COMPOSE_CONTROL_ADDRESS_CHANGE_NICKNAME ||
            control == WM_COMPOSE_CONTROL_ADDRESS_ERASE) {
            bool changed = control ==
                WM_COMPOSE_CONTROL_ADDRESS_CHANGE_NICKNAME
                    ? wm_board_address_change_nickname(compose->address)
                    : wm_board_address_erase(compose->address);
            if (!changed) return false;
            wm_board_compose_hover(compose, WM_COMPOSE_CONTROL_NONE);
            queue_key_cue(compose, "WIPL_SE_DECIDE");
            return true;
        }
        if (control == WM_COMPOSE_CONTROL_ADDRESS_EDIT) {
            WmBoardAddressPhase phase = wm_board_address_phase(compose->address);
            if (phase != WM_BOARD_ADDRESS_FORM_READY &&
                phase != WM_BOARD_ADDRESS_NICKNAME_READY) return false;
            WmBoardKeyboardProfile profile =
                phase == WM_BOARD_ADDRESS_NICKNAME_READY
                    ? WM_BOARD_KEYBOARD_ADDRESS_NICKNAME :
                wm_board_address_kind_is_wii(compose->address)
                    ? WM_BOARD_KEYBOARD_ADDRESS_WII :
                      WM_BOARD_KEYBOARD_ADDRESS_EMAIL;
            wm_board_keyboard_set_profile(compose->keyboard, profile);
            wm_board_keyboard_set_text_context(compose->keyboard,
                wm_board_address_field_text(compose->address));
            wm_board_compose_hover(compose, WM_COMPOSE_CONTROL_NONE);
            compose->address_keyboard_open = true;
            compose->keyboard_age = 0.0f;
            queue_key_cue(compose, "WIPL_SE_SK_OPEN");
            return true;
        }
        if (control == WM_COMPOSE_CONTROL_POST) {
            WmBoardAddressPhase phase = wm_board_address_phase(compose->address);
            if (phase == WM_BOARD_ADDRESS_FORM_READY ||
                phase == WM_BOARD_ADDRESS_NICKNAME_READY ||
                phase == WM_BOARD_ADDRESS_MII_READY ||
                phase == WM_BOARD_ADDRESS_REVIEW_READY) {
                if (wm_board_address_submit(compose->address)) {
                    wm_board_compose_hover(compose, WM_COMPOSE_CONTROL_NONE);
                    queue_key_cue(compose, "WIPL_SE_DECIDE");
                    return true;
                }
                if (phase != WM_BOARD_ADDRESS_FORM_READY ||
                    !wm_board_address_show_issue(compose->address))
                    return false;
                wm_board_compose_hover(compose, WM_COMPOSE_CONTROL_NONE);
                queue_key_cue(compose, "WIPL_SE_INFO_WINDOW");
                return true;
            }
            if (!wm_board_address_register(compose->address)) return false;
            wm_board_compose_hover(compose, WM_COMPOSE_CONTROL_NONE);
            queue_key_cue(compose,
                wm_board_address_dialog_active(compose->address)
                    ? "WIPL_SE_INFO_WINDOW" : "WIPL_SE_DECIDE");
            return true;
        }
        if (control == WM_COMPOSE_CONTROL_ADDRESS_WII ||
            control == WM_COMPOSE_CONTROL_ADDRESS_OTHERS) {
            bool wii = control == WM_COMPOSE_CONTROL_ADDRESS_WII;
            if (!wm_board_address_select_kind(compose->address, wii))
                return false;
            wm_board_compose_hover(compose, WM_COMPOSE_CONTROL_NONE);
            queue_key_cue(compose, "WIPL_SE_DECIDE");
            return true;
        }
        bool forward = control == WM_COMPOSE_CONTROL_ADDRESS_NEXT;
        if (!forward && control != WM_COMPOSE_CONTROL_ADDRESS_PREVIOUS)
            return false;
        bool turned = wm_board_address_turn(compose->address, forward);
        if (turned) {
            compose->address_arrow_press[forward ? 1 : 0] = 0.0f;
            queue_key_cue(compose, forward ? "WIPL_SE_FL_PAGE_INC" :
                                            "WIPL_SE_FL_PAGE_DEC");
        }
        return turned;
    }
    if (compose->phase == WM_COMPOSE_SELECTOR) {
        if (control == WM_COMPOSE_CONTROL_MEMO) {
            compose->phase = WM_COMPOSE_ENTER_MEMO;
            compose->frame = 0.0f;
            refresh_scroll(compose);
            return true;
        }
        if (control == WM_COMPOSE_CONTROL_LETTER) {
            wm_board_compose_hover(compose, WM_COMPOSE_CONTROL_NONE);
            compose->network_phase = COMPOSE_NETWORK_ENTER;
            compose->network_frame = 0.0f;
            compose->network_selected = WM_COMPOSE_CONTROL_NONE;
            compose->focus[WM_COMPOSE_CONTROL_NETWORK_QUIT] =
                (ComposeFocus){0};
            compose->focus[WM_COMPOSE_CONTROL_NETWORK_SETTINGS] =
                (ComposeFocus){0};
            queue_key_cue(compose, "WIPL_SE_DECIDE");
            queue_key_cue(compose, "WIPL_SE_INFO_WINDOW");
            return true;
        }
        if (control == WM_COMPOSE_CONTROL_ADDRESS) {
            if (!wm_board_address_open(compose->address)) return false;
            wm_board_compose_hover(compose, WM_COMPOSE_CONTROL_NONE);
            compose->focus[WM_COMPOSE_CONTROL_ADDRESS_PREVIOUS] =
                (ComposeFocus){0};
            compose->focus[WM_COMPOSE_CONTROL_ADDRESS_NEXT] =
                (ComposeFocus){0};
            compose->address_arrow_press[0] = -1.0f;
            compose->address_arrow_press[1] = -1.0f;
            compose->phase = WM_COMPOSE_ADDRESS;
            compose->frame = 0.0f;
            return true;
        }
    }
    if (compose->phase == WM_COMPOSE_MEMO) {
        if (control == WM_COMPOSE_CONTROL_MII) {
            if (!wm_board_address_show_memo_no_mii(compose->address))
                return false;
            wm_board_compose_hover(compose, WM_COMPOSE_CONTROL_NONE);
            queue_key_cue(compose, "WIPL_SE_INFO_WINDOW");
            queue_key_cue(compose, "WIPL_SE_DECIDE");
            return true;
        }
        if (control == WM_COMPOSE_CONTROL_EDIT) {
            wm_board_compose_hover(compose, WM_COMPOSE_CONTROL_NONE);
            wm_board_keyboard_reset(compose->keyboard);
            wm_board_keyboard_set_text_context(compose->keyboard,
                                                compose->text);
            compose->phase = WM_COMPOSE_ENTER_EDIT;
            compose->frame = 0.0f;
            compose->keyboard_age = 0.0f;
            compose->key_cue_count = 0;
            queue_key_cue(compose, "WIPL_SE_SK_OPEN");
            refresh_scroll(compose);
            return true;
        }
        if (control == WM_COMPOSE_CONTROL_POST && has_nonspace_text(compose)) {
            wm_board_compose_hover(compose, WM_COMPOSE_CONTROL_NONE);
            compose->phase = WM_COMPOSE_POST_PRESS;
            compose->frame = 0.0f;
            refresh_scroll(compose);
            return true;
        }
    }
    if (compose->phase == WM_COMPOSE_EDIT &&
        (control == WM_COMPOSE_CONTROL_SCROLL_UP ||
         control == WM_COMPOSE_CONTROL_SCROLL_DOWN)) {
        if (wm_board_keyboard_symbols_visible(compose->keyboard)) return false;
        size_t direction = control == WM_COMPOSE_CONTROL_SCROLL_UP
                               ? COMPOSE_SCROLL_UP : COMPOSE_SCROLL_DOWN;
        ComposeScrollArrow *arrow =
            &compose->arrows[COMPOSE_SCROLL_EDITOR][direction];
        if (!arrow->visible || !start_scroll(compose,
            compose->scroll_offset + (direction == COMPOSE_SCROLL_UP
                ? -compose->scroll_line_height : compose->scroll_line_height))) {
            return false;
        }
        arrow->press_active = true;
        arrow->press_frame = 0.0f;
        return true;
    }
    if (compose->phase == WM_COMPOSE_MEMO &&
        (control == WM_COMPOSE_CONTROL_SCROLL_UP ||
         control == WM_COMPOSE_CONTROL_SCROLL_DOWN)) {
        size_t direction = control == WM_COMPOSE_CONTROL_SCROLL_UP
                               ? COMPOSE_SCROLL_UP : COMPOSE_SCROLL_DOWN;
        ComposeScrollArrow *arrow =
            &compose->arrows[COMPOSE_SCROLL_DISPLAY][direction];
        float step = 3.0f * compose->scroll_line_height;
        if (!arrow->visible || !start_scroll(compose,
            compose->scroll_offset + (direction == COMPOSE_SCROLL_UP
                ? -step : step))) return false;
        arrow->press_active = true;
        arrow->press_frame = 0.0f;
        return true;
    }
    return false;
}

bool wm_board_compose_activate(WmBoardCompose *compose,
                                WmBoardComposeControl control) {
    return activate_control(compose, control, false);
}

bool wm_board_compose_activate_secondary(WmBoardCompose *compose,
                                          WmBoardComposeControl control) {
    if (!compose || control < WM_COMPOSE_CONTROL_KEY_FIRST ||
        control > WM_COMPOSE_CONTROL_KEY_LAST) return false;
    WmBoardKeyboardControl key = (WmBoardKeyboardControl)(
        control - WM_COMPOSE_CONTROL_KEY_FIRST + 1);
    if (
        key < WM_KEYBOARD_PHONE_FIRST || key > WM_KEYBOARD_PHONE_LAST ||
        !wm_board_keyboard_phone_mode(compose->keyboard)) return false;
    return activate_control(compose, control, true);
}

bool wm_board_compose_hold_control(WmBoardCompose *compose,
                                    WmBoardComposeControl control) {
    if (!compose || (compose->phase != WM_COMPOSE_EDIT &&
                     !compose->address_keyboard_open) ||
        control < WM_COMPOSE_CONTROL_KEY_FIRST ||
        control > WM_COMPOSE_CONTROL_KEY_LAST) return false;
    WmBoardKeyboardControl key = (WmBoardKeyboardControl)(
        control - WM_COMPOSE_CONTROL_KEY_FIRST + 1);
    if (key != WM_KEYBOARD_DELETE && key != WM_KEYBOARD_SPACE &&
        key != WM_KEYBOARD_CANDIDATE_PREVIOUS &&
        key != WM_KEYBOARD_CANDIDATE_NEXT) return false;
    if (wm_board_keyboard_symbols_visible(compose->keyboard)) return false;
    if (!wm_board_compose_activate(compose, control)) return false;
    return wm_board_keyboard_begin_hold(compose->keyboard, key);
}

void wm_board_compose_release_control(WmBoardCompose *compose) {
    if (compose) wm_board_keyboard_release_hold(compose->keyboard);
}

bool wm_board_compose_insert_text(WmBoardCompose *compose,
                                   const char *utf8) {
    if (compose && compose->address_keyboard_open) {
        bool inserted = wm_board_address_insert_text(compose->address, utf8);
        if (inserted) {
            wm_board_keyboard_text_changed(compose->keyboard,
                                            compose->inserting_phone);
            compose->keyboard_age = 0.0f;
        }
        return inserted;
    }
    if (!compose || !utf8 || (compose->phase != WM_COMPOSE_MEMO &&
                              compose->phase != WM_COMPOSE_EDIT)) return false;
    if (compose->phase == WM_COMPOSE_EDIT &&
        wm_board_keyboard_symbols_visible(compose->keyboard) &&
        !compose->inserting_symbol) return false;
    size_t bytes = strlen(utf8);
    if (bytes == 0) return false;
    bool has_whitespace = false;
    for (size_t index = 0; index < bytes; index++) {
        unsigned char value = (unsigned char)utf8[index];
        if (value < 0x20 && value != '\n') return false;
        if (value == ' ' || value == '\n') has_whitespace = true;
    }
    WmBoardKeyboardComposition composition;
    bool commit_boundary = compose->phase == WM_COMPOSE_EDIT &&
        !has_whitespace && !compose->inserting_phone &&
        wm_board_keyboard_composition(compose->keyboard, &composition) &&
        composition.prefix_bytes <= compose->text_bytes &&
        utf16_units(compose->text + compose->text_bytes -
                    composition.prefix_bytes,
                    composition.prefix_bytes) >= 32;
    if (commit_boundary) {
        char candidate[WM_KEYBOARD_CANDIDATE_UTF8_CAPACITY];
        const char *selected = composition.selected_candidate;
        size_t selected_bytes = selected ? strlen(selected) : 0;
        size_t base = compose->text_bytes - composition.prefix_bytes;
        if (!selected_bytes || selected_bytes >= sizeof(candidate) ||
            selected_bytes > COMPOSE_MAX_TEXT_BYTES - base ||
            bytes > COMPOSE_MAX_TEXT_BYTES - base - selected_bytes)
            return false;
        memcpy(candidate, selected, selected_bytes + 1);
        if (!replace_keyboard_suffix(compose, composition.prefix_bytes,
                                      candidate, false)) return false;
        wm_board_keyboard_finish_composition(compose->keyboard);
    }
    if (bytes > COMPOSE_MAX_TEXT_BYTES - compose->text_bytes) return false;
    if (compose->phase == WM_COMPOSE_MEMO) {
        wm_board_keyboard_reset(compose->keyboard);
        compose->phase = WM_COMPOSE_ENTER_EDIT;
        compose->frame = 0.0f;
        compose->keyboard_age = 0.0f;
        compose->key_cue_count = 0;
        queue_key_cue(compose, "WIPL_SE_SK_OPEN");
    }
    if (compose->phase == WM_COMPOSE_EDIT && has_whitespace)
        wm_board_keyboard_finish_composition(compose->keyboard);
    memcpy(compose->text + compose->text_bytes, utf8, bytes + 1);
    compose->text_bytes += bytes;
    wm_board_keyboard_text_changed(compose->keyboard,
                                    compose->inserting_phone);
    if (commit_boundary) queue_key_cue(compose, "WIPL_SE_CHAR_DECIDE");
    compose->keyboard_age = 0.0f;
    compose->scroll_lines = memo_line_count(compose);
    refresh_scroll(compose);
    if (compose->phase == WM_COMPOSE_EDIT) {
        if (compose->scroll_moving) {
            compose->scroll_target = compose->scroll_maximum;
        } else {
            (void)start_scroll(compose, compose->scroll_maximum);
        }
    }
    return true;
}

void wm_board_compose_press_physical(WmBoardCompose *compose,
                                      const char *utf8) {
    if (!compose || (compose->phase != WM_COMPOSE_EDIT &&
                     !compose->address_keyboard_open)) return;
    (void)wm_board_keyboard_press_physical(compose->keyboard, utf8);
}

bool wm_board_compose_backspace(WmBoardCompose *compose) {
    if (compose && compose->address_keyboard_open) {
        bool erased = wm_board_address_backspace(compose->address);
        if (erased) {
            wm_board_keyboard_text_changed(compose->keyboard, false);
            compose->keyboard_age = 0.0f;
        }
        return erased;
    }
    if (!compose || (compose->phase != WM_COMPOSE_EDIT &&
                     compose->phase != WM_COMPOSE_MEMO) ||
        compose->text_bytes == 0) return false;
    if (compose->phase == WM_COMPOSE_EDIT &&
        wm_board_keyboard_symbols_visible(compose->keyboard)) return false;
    size_t start = compose->text_bytes - 1;
    while (start > 0 &&
           ((unsigned char)compose->text[start] & 0xC0) == 0x80) {
        start--;
    }
    compose->text[start] = '\0';
    compose->text_bytes = start;
    wm_board_keyboard_text_changed(compose->keyboard, false);
    compose->keyboard_age = 0.0f;
    compose->scroll_lines = memo_line_count(compose);
    refresh_scroll(compose);
    return true;
}

bool wm_board_compose_finish_edit(WmBoardCompose *compose) {
    if (compose && compose->address_keyboard_open) {
        compose->key_cue_count = 0;
        return close_address_keyboard(compose, true);
    }
    return compose && compose->phase == WM_COMPOSE_EDIT &&
           wm_board_compose_back(compose);
}

bool wm_board_compose_scroll_state(const WmBoardCompose *compose,
                                    WmBoardComposeScrollState *state) {
    if (!compose || !state) return false;
    ComposeScrollMode mode = scroll_mode(compose);
    *state = (WmBoardComposeScrollState){
        .offset = compose->scroll_offset,
        .maximum = compose->scroll_maximum,
        .editor_opacity = compose->phase == WM_COMPOSE_EDIT ? 1.0f :
                          compose->phase == WM_COMPOSE_LEAVE_EDIT
                              ? 1.0f - clamp_frame(compose->frame, 30.0f) /
                                30.0f : 0.0f,
        .editing = mode == COMPOSE_SCROLL_EDITOR,
        .up_target_visible = compose->arrows[mode][COMPOSE_SCROLL_UP].visible,
        .down_target_visible =
            compose->arrows[mode][COMPOSE_SCROLL_DOWN].visible
    };
    return true;
}

static void append_clip(WmLayoutClip clips[COMPOSE_CLIP_CAPACITY],
                        size_t *count, const char *animation,
                        const char *group, float frame) {
    if (*count >= COMPOSE_CLIP_CAPACITY) return;
    clips[*count] = (WmLayoutClip){
        .animation = animation,
        .group = group,
        .frame = frame,
        .loop_override = 0
    };
    (*count)++;
}

static void append_target_clip(WmLayoutClip clips[COMPOSE_CLIP_CAPACITY],
                                size_t *count, const char *animation,
                                const char *target_name, float frame) {
    if (*count >= COMPOSE_CLIP_CAPACITY) return;
    clips[*count] = (WmLayoutClip){
        .animation = animation,
        .target_name = target_name,
        .frame = frame,
        .loop_override = 0
    };
    (*count)++;
}

static void pose_selector(WmBoardCompose *compose) {
    WmLayoutClip clips[COMPOSE_CLIP_CAPACITY];
    size_t count = 0;
    float entry = compose->phase == WM_COMPOSE_ENTER_SELECTOR
                      ? clamp_frame(compose->frame, 30.0f) : 30.0f;
    append_clip(clips, &count, "my_Mail_a_SelectIn",
                "G_SelectInOut", entry);
    if (compose->phase == WM_COMPOSE_ADDRESS) {
        WmBoardAddressPhase phase = wm_board_address_phase(compose->address);
        float frame = wm_board_address_phase_frame(compose->address);
        append_clip(clips, &count,
                    phase == WM_BOARD_ADDRESS_EXIT
                        ? "my_Mail_a_AdressOut" : "my_Mail_a_AdressIn",
                    NULL,
                    phase == WM_BOARD_ADDRESS_EXIT ?
                        clamp_frame(frame - 1.0f, 16.0f) :
                        phase == WM_BOARD_ADDRESS_ENTER
                            ? clamp_frame(frame, 16.0f) : 16.0f);
    } else if (compose->phase == WM_COMPOSE_BACK_SELECTOR) {
        append_clip(clips, &count, "my_Mail_a_SelectOut",
                    "G_SelectInOut", clamp_frame(compose->frame, 20.0f));
    } else if (compose->phase == WM_COMPOSE_EXIT_AFTER_POST) {
        /* SelectOut starts from MailIn's hidden choice cards. Restoring the
         * source pose here makes Memo, Letter, and Address flash after Post. */
        append_clip(clips, &count, "my_Mail_a_MailIn", NULL, 16.0f);
        append_clip(clips, &count, "my_Mail_a_SelectOut",
                    "G_SelectInOut", clamp_frame(compose->frame, 20.0f));
    } else if (compose->phase == WM_COMPOSE_ENTER_MEMO ||
               compose->phase == WM_COMPOSE_MEMO ||
               compose->phase == WM_COMPOSE_ENTER_EDIT ||
               compose->phase == WM_COMPOSE_EDIT ||
               compose->phase == WM_COMPOSE_LEAVE_EDIT ||
               compose->phase == WM_COMPOSE_POST_PRESS ||
               compose->phase == WM_COMPOSE_SEND) {
        append_clip(clips, &count, "my_Mail_a_MailIn",
                    NULL,
                    compose->phase == WM_COMPOSE_ENTER_MEMO
                        ? clamp_frame(compose->frame, 16.0f) : 16.0f);
    } else if (compose->phase == WM_COMPOSE_BACK_MEMO) {
        if (compose->frame < 46.0f) {
            append_clip(clips, &count, "my_Mail_a_MailIn",
                        NULL, 16.0f);
        } else {
            append_clip(clips, &count, "my_Mail_a_MailOut",
                        NULL,
                        clamp_frame(compose->frame - 46.0f, 16.0f));
        }
    }
    static const struct {
        WmBoardComposeControl control;
        const char *group;
        const char *enter;
        const char *leave;
    } buttons[] = {
        {WM_COMPOSE_CONTROL_MEMO, "G_MailFoucus",
         "my_Mail_a_MailFoucusIn", "my_Mail_a_MailFoucusOut"},
        {WM_COMPOSE_CONTROL_LETTER, "G_LetterFoucus",
         "my_Mail_a_LetterFoucusIn", "my_Mail_a_LetterFoucusOut"},
        {WM_COMPOSE_CONTROL_ADDRESS, "G_AdressFoucus",
         "my_Mail_a_AdressFoucusIn", "my_Mail_a_AdressFoucusOut"}
    };
    /* Selector focus has a constant card alpha track. After selection it
     * would override the full MailIn/AdressIn clip's card exit fade. */
    if (compose->phase == WM_COMPOSE_ENTER_SELECTOR ||
        compose->phase == WM_COMPOSE_SELECTOR ||
        compose->phase == WM_COMPOSE_BACK_SELECTOR) {
        for (size_t index = 0; index < sizeof(buttons) / sizeof(buttons[0]);
             index++) {
            ComposeFocus focus = compose->focus[buttons[index].control];
            if (!focus.active) continue;
            append_clip(clips, &count,
                        focus.entering ? buttons[index].enter :
                                         buttons[index].leave,
                        buttons[index].group,
                        clamp_frame(focus.frame, 6.0f));
        }
    }
    wm_layout_pose(compose->selector, clips, count);
    wm_layout_set_pose_text(compose->selector, "T_Mail", "Memo");
    wm_layout_set_pose_text(compose->selector, "T_Adress", "Address Book");
}

static bool body_visible(const WmBoardCompose *compose) {
    return compose->phase == WM_COMPOSE_ENTER_MEMO ||
           compose->phase == WM_COMPOSE_MEMO ||
           compose->phase == WM_COMPOSE_ENTER_EDIT ||
           compose->phase == WM_COMPOSE_EDIT ||
           compose->phase == WM_COMPOSE_LEAVE_EDIT ||
           compose->phase == WM_COMPOSE_POST_PRESS ||
           compose->phase == WM_COMPOSE_SEND ||
           (compose->phase == WM_COMPOSE_BACK_MEMO &&
            compose->frame < 46.0f);
}

static void pose_body(WmBoardCompose *compose) {
    WmLayoutClip clips[COMPOSE_CLIP_CAPACITY];
    size_t count = 0;
    if (compose->phase == WM_COMPOSE_ENTER_MEMO) {
        append_clip(clips, &count, "my_Memo_a_MailIn", NULL,
                    clamp_frame(compose->frame, 16.0f));
    } else if (compose->phase == WM_COMPOSE_SEND) {
        append_clip(clips, &count, "my_Memo_a_MailIn", NULL, 16.0f);
        append_clip(clips, &count, "my_Memo_a_SendOut", NULL,
                    clamp_frame(compose->frame, 50.0f));
    } else if (compose->phase == WM_COMPOSE_BACK_MEMO &&
               compose->frame >= 20.0f) {
        append_clip(clips, &count, "my_Memo_a_MailOut", NULL,
                    clamp_frame(compose->frame - 20.0f, 16.0f));
    } else {
        append_clip(clips, &count, "my_Memo_a_MailIn", NULL, 16.0f);
    }
    if (compose->phase == WM_COMPOSE_ENTER_EDIT ||
        compose->phase == WM_COMPOSE_EDIT ||
        compose->phase == WM_COMPOSE_LEAVE_EDIT) {
        float hint_frame = compose->phase == WM_COMPOSE_ENTER_EDIT
            ? clamp_frame(compose->frame, 9.0f) : 9.0f;
        if (compose->phase == WM_COMPOSE_LEAVE_EDIT &&
            compose->text_bytes == 0) {
            /* The source prompt returns during the final ten keyboard-exit
             * updates by reversing its nine-frame TouchLetter alpha curve. */
            hint_frame = 9.0f *
                (1.0f - clamp_frame(compose->frame - 20.0f, 10.0f) / 10.0f);
        }
        append_clip(clips, &count, "my_Memo_a_TouchLetter", NULL,
                    hint_frame);
    }
    static const char *const display_end_groups[COMPOSE_SCROLL_DIRECTIONS] = {
        "G_ArwR_End", "G_ArwL_End"
    };
    static const char *const display_focus_groups[COMPOSE_SCROLL_DIRECTIONS] = {
        "G_ArwR_Focus", "G_ArwL_Focus"
    };
    static const char *const display_press_groups[COMPOSE_SCROLL_DIRECTIONS] = {
        "G_ArwR_Ac", "G_ArwL_Ac"
    };
    static const char *const editor_panes[COMPOSE_SCROLL_DIRECTIONS] = {
        "P_txtScrll_UP", "P_txtScrll_DOWN"
    };
    append_clip(clips, &count, "my_Memo_a_Loop", "G_ArwRoop",
                fmodf(compose->age, 55.0f));
    ComposeFocus mii_focus = compose->focus[WM_COMPOSE_CONTROL_MII];
    bool body_exiting = compose->phase == WM_COMPOSE_SEND ||
        (compose->phase == WM_COMPOSE_BACK_MEMO && compose->frame >= 20.0f);
    if (mii_focus.active && !body_exiting) {
        /* The focus clip has a constant 255-alpha track for Nigaoe. During
         * MailOut or SendOut it would override the source icon fade. */
        append_target_clip(clips, &count,
            mii_focus.entering ? "my_Memo_a_NigaoeFoucusIn" :
                                 "my_Memo_a_NigaoeFoucusOut",
            "Nigaoe", clamp_frame(mii_focus.frame, 6.0f));
    }
    for (size_t direction = 0; direction < COMPOSE_SCROLL_DIRECTIONS;
         direction++) {
        const ComposeScrollArrow *display =
            &compose->arrows[COMPOSE_SCROLL_DISPLAY][direction];
        append_clip(clips, &count,
                    display->visible ? "my_Memo_a_Appear" :
                                       "my_Memo_a_Lost",
                    display_end_groups[direction],
                    clamp_frame(display->appearance_frame, 10.0f));
        if (display->visible && display->appearance_frame >= 10.0f &&
            display->focus.active) {
            append_clip(clips, &count,
                        display->focus.entering ? "my_Memo_a_FocusOn" :
                                                   "my_Memo_a_FocusOff",
                        display_focus_groups[direction],
                        clamp_frame(display->focus.frame, 15.0f));
        }
        if (display->visible && display->press_active) {
            append_clip(clips, &count, "my_Memo_a_Select",
                        display_press_groups[direction],
                        clamp_frame(display->press_frame, 7.0f));
        }

        const ComposeScrollArrow *editor =
            &compose->arrows[COMPOSE_SCROLL_EDITOR][direction];
        if (!editor->appeared) continue;
        append_target_clip(clips, &count, "my_Memo_a_Fade_IN",
                           editor_panes[direction],
                           editor->visible
                               ? clamp_frame(editor->appearance_frame, 11.0f)
                               : 11.0f);
        if (!editor->visible) {
            append_target_clip(clips, &count, "my_Memo_a_Fade_OUT",
                               editor_panes[direction],
                               clamp_frame(editor->appearance_frame, 10.0f));
        } else if (editor->appearance_frame >= 11.0f) {
            if (editor->focus.active) {
                append_target_clip(clips, &count,
                    editor->focus.entering ? "my_Memo_a_Foucus_IN" :
                                             "my_Memo_a_Focus-OUT",
                    editor_panes[direction],
                    editor->focus.entering
                        ? 1.0f + clamp_frame(editor->focus.frame, 5.0f)
                        : clamp_frame(editor->focus.frame, 8.0f));
            }
            if (editor->press_active) {
                append_target_clip(clips, &count, "my_Memo_a_Pushed",
                                   editor_panes[direction],
                                   clamp_frame(editor->press_frame, 7.0f));
            }
        }
    }
    wm_layout_pose(compose->body, clips, count);
    for (size_t direction = 0; direction < COMPOSE_SCROLL_DIRECTIONS;
         direction++) {
        if (!compose->arrows[COMPOSE_SCROLL_EDITOR][direction].appeared) {
            wm_layout_set_pane_visible(compose->body,
                                        editor_panes[direction], false);
        }
    }
    if (compose->phase == WM_COMPOSE_LEAVE_EDIT) {
        float alpha = 255.0f *
            (1.0f - clamp_frame(compose->frame, 30.0f) / 30.0f);
        for (size_t direction = 0; direction < COMPOSE_SCROLL_DIRECTIONS;
             direction++) {
            wm_layout_set_pane_alpha(compose->body,
                                      editor_panes[direction], alpha);
        }
    }
    float keyboard_progress = compose->phase == WM_COMPOSE_ENTER_EDIT
        ? clamp_frame(compose->frame, 30.0f) / 30.0f
        : compose->phase == WM_COMPOSE_LEAVE_EDIT
            ? 1.0f - clamp_frame(compose->frame, 30.0f) / 30.0f
            : compose->phase == WM_COMPOSE_EDIT ? 1.0f : 0.0f;
    float keyboard_smooth = keyboard_progress * keyboard_progress *
                            (3.0f - 2.0f * keyboard_progress);
    wm_layout_set_pane_translation(compose->body, "N_Memo", 0.0f,
                                     compose->scroll_offset +
                                     145.0f * keyboard_smooth, 0.0f);
    size_t lines = compose->scroll_lines > 4 ? compose->scroll_lines : 4;
    WmLayoutPaneState footer;
    if (wm_layout_pane_state(compose->body, "N_Footer", &footer)) {
        wm_layout_set_pane_translation(compose->body, "N_Footer",
                                         footer.translation[0],
                                         footer.translation[1] -
                                         (float)(lines - 1) *
                                         compose->scroll_line_height,
                                         footer.translation[2]);
    }
    WmLayoutPaneState text_hit;
    if (wm_layout_pane_state(compose->body, "B_2l_TextBox", &text_hit)) {
        float height = (float)lines * compose->scroll_line_height;
        wm_layout_set_pane_size(compose->body, "B_2l_TextBox",
                                  text_hit.size[0], height);
        wm_layout_set_pane_translation(compose->body, "B_2l_TextBox",
                                         text_hit.translation[0],
                                         text_hit.translation[1] -
                                         (height - text_hit.size[1]) * 0.5f,
                                         text_hit.translation[2]);
    }
    if (keyboard_progress > 0.0f) {
        WmLayoutPaneState viewport;
        if (wm_layout_pane_state(compose->body, "T_2l_TextBox",
                                  &viewport)) {
            float height = 2.0f * compose->scroll_line_height;
            wm_layout_set_pane_size(compose->body, "T_2l_TextBox",
                                      viewport.size[0], height);
            wm_layout_set_pane_translation(compose->body, "T_2l_TextBox",
                                             viewport.translation[0],
                                             viewport.translation[1] -
                                             (height - viewport.size[1]) *
                                             0.5f - compose->scroll_offset,
                                             viewport.translation[2]);
        }
    }
    wm_layout_set_pose_text(compose->body, "T_Header", "Memo");
    wm_layout_set_pose_text(compose->body, "T_TouchLetter",
                             compose->text_bytes ? "" : "Write a memo");
    wm_layout_set_pose_text(compose->body, "T_Nigaoe", "\342\206\220Add a Mii");
    wm_layout_set_pose_text(compose->body, "T_Letter",
                             memo_display_text(compose, NULL));
    if (compose->phase == WM_COMPOSE_EDIT) {
        WmBoardKeyboardComposition composition;
        if (wm_board_keyboard_composition(compose->keyboard,
                                           &composition) &&
            composition.prefix_bytes <= compose->text_bytes) {
            size_t start = compose->text_bytes - composition.prefix_bytes;
            size_t preview_bytes = composition.preview_candidate &&
                                   strcmp(composition.preview_candidate,
                                          ">") != 0
                ? strlen(composition.preview_candidate) : 0;
            WmLayoutTextColorRange colors[2] = {
                {start, compose->text_bytes, {255, 50, 50, 255}},
                {compose->text_bytes, start + preview_bytes,
                 {192, 192, 192, 255}}
            };
            size_t count = 1;
            if (preview_bytes > composition.prefix_bytes) {
                if (composition.preview_hovered) {
                    colors[1].rgba[0] = 50;
                    colors[1].rgba[1] = 100;
                    colors[1].rgba[2] = 50;
                }
                count = 2;
            }
            (void)wm_layout_set_pose_text_colors(compose->body,
                                                  "T_Letter", colors, count);
        } else if (wm_board_keyboard_phone_pending(compose->keyboard) &&
                   compose->text_bytes > 0) {
            size_t pending = wm_board_keyboard_phone_pending_bytes(
                compose->keyboard);
            if (pending <= compose->text_bytes) {
                size_t displayed_end = compose->text_bytes;
                if (compose->text[compose->text_bytes - 1] == ' ' &&
                    wm_board_keyboard_phone_space_pending(compose->keyboard))
                    displayed_end += 2; /* U+2423 replaces one byte with three. */
                WmLayoutTextColorRange color = {
                    .first_byte = compose->text_bytes - pending,
                    .end_byte = displayed_end,
                    .rgba = {255, 50, 50, 255}
                };
                (void)wm_layout_set_pose_text_colors(compose->body,
                                                      "T_Letter", &color, 1);
            }
        }
    }
}

static bool body_row_pane(void *context, const WmLayoutPaneView *pane) {
    (void)context;
    static const char *const names[] = {
        "RootPane", "N_Memo", "N_MemoRoot", "N_Body", "Body_s",
        "Body3", "Picture_11", "Picture_12", "Picture_13",
        "Body3_04", "B_Body"
    };
    for (size_t index = 0; index < sizeof(names) / sizeof(names[0]); index++) {
        if (strcmp(pane->name, names[index]) == 0) return true;
    }
    return false;
}

static bool body_header_pane(void *context, const WmLayoutPaneView *pane) {
    (void)context;
    static const char *const names[] = {
        "RootPane", "N_Memo", "N_MemoRoot", "N_Header", "Header_s0",
        "Header_s2", "Picture_07", "Picture_04", "Picture_08",
        "Picture_09", "Picture_05", "Picture_00", "Picture_22",
        "Picture_10", "Picture_15", "Picture_01", "Picture_02",
        "T_Header"
    };
    for (size_t index = 0; index < sizeof(names) / sizeof(names[0]); index++) {
        if (strcmp(pane->name, names[index]) == 0) return true;
    }
    return false;
}

static bool draw_body_rows(WmBoardCompose *compose) {
    size_t lines = compose->scroll_lines > 4 ? compose->scroll_lines : 4;
    if (lines <= 1) return false;
    WmSourceRect rect;
    WmLayoutPaneState body;
    if (!wm_source_pane_rect(compose->body, "N_Body", true, WM_LAYOUT_IPL,
                              NULL, &rect) ||
        !wm_layout_pane_state(compose->body, "N_Body", &body) ||
        rect.height <= 0.0f) return false;
    /* The WAD orders its header, original body strip, repeated strips, then
     * footer and text. Drawing the repeated strips first exposes a ruled
     * strip's gray edge, while drawing the header last exposes its shadow. */
    wm_layout_present_filtered_with_fonts(
        compose->platform, compose->textures, compose->fonts,
        compose->body, true, WM_LAYOUT_IPL, NULL, body_header_pane, NULL);
    wm_layout_present_filtered_with_fonts(
        compose->platform, compose->textures, compose->fonts,
        compose->body, true, WM_LAYOUT_IPL, NULL, body_row_pane, NULL);
    float first_float = floorf((-rect.y - rect.height) / rect.height) - 1.0f;
    float last_float = ceilf((456.0f - rect.y) / rect.height) + 1.0f;
    if (last_float < 1.0f || first_float > (float)(lines - 1))
        return true;
    size_t first = first_float > 1.0f ? (size_t)first_float : 1;
    size_t last = last_float < (float)(lines - 1)
                      ? (size_t)last_float : lines - 1;
    for (size_t row = first; row <= last; row++) {
        wm_layout_set_pane_translation(compose->body, "N_Body",
                                         body.translation[0],
                                         body.translation[1] -
                                         (float)row * compose->scroll_line_height,
                                         body.translation[2]);
        wm_layout_present_filtered_with_fonts(
            compose->platform, compose->textures, compose->fonts,
            compose->body, true, WM_LAYOUT_IPL, NULL, body_row_pane, NULL);
    }
    wm_layout_set_pane_translation(compose->body, "N_Body",
                                     body.translation[0], body.translation[1],
                                     body.translation[2]);
    return true;
}

typedef struct MemoCaretPane {
    bool visible;
    float matrix[12];
    float alpha;
    float marker_alpha;
    float marker_tint[3];
    WmFontPane font;
    const char *font_name;
} MemoCaretPane;

static bool capture_memo_caret_pane(void *context,
                                     const WmLayoutPaneView *pane) {
    MemoCaretPane *caret = context;
    if (strcmp(pane->name, "T_Letter") == 0 && pane->text) {
        caret->visible = pane->alpha > 0.0f;
        memcpy(caret->matrix, pane->matrix, sizeof(caret->matrix));
        caret->alpha = pane->alpha;
        caret->font = pane->text->pane;
        caret->font_name = pane->text->font_name;
        const float *foreground = pane->text->material
            ? pane->text->material->registers[1] : NULL;
        caret->marker_alpha = pane->alpha *
            fminf(1.0f, fmaxf(0.0f,
                  pane->text->colors[0][3] / 255.0f)) *
            (foreground ? fminf(1.0f, fmaxf(0.0f, foreground[3])) : 1.0f);
        for (size_t channel = 0; channel < 3; channel++) {
            caret->marker_tint[channel] = foreground
                ? fminf(1.0f, fmaxf(0.0f, foreground[channel])) : 1.0f;
        }
    }
    return true;
}

static bool capture_memo_caret_without_sheet(void *context,
                                               const WmLayoutPaneView *pane) {
    if (strcmp(pane->name, "N_Header") == 0 ||
        strcmp(pane->name, "N_Body") == 0) return false;
    return capture_memo_caret_pane(context, pane);
}

static void draw_memo_caret(WmBoardCompose *compose,
                             const MemoCaretPane *pane) {
    if (!pane->visible || !isfinite(compose->keyboard_age)) return;
    WmCachedFont *face = wm_font_cache_resolve(compose->fonts,
                                                pane->font_name);
    size_t display_bytes = 0;
    const char *display = memo_display_text(compose, &display_bytes);
    const WmFontTextLayout *layout = face
        ? wm_font_cache_layout(face, display, &pane->font) : NULL;
    float position_x, position_y;
    if (!layout || !wm_font_text_layout_caret(layout, display_bytes,
                                               &position_x, &position_y)) {
        return;
    }
    float height = fmaxf(0.0f, pane->font.font_size[1] - 4.0f);
    if (height <= 0.0f) return;

    /* Base::drawCursor uses a centered source-space strip and a 45-update
     * sine pulse. The layout matrix is the same one used for its glyphs. */
    const float width = (float)(14592 / 832) / 6.0f;
    const float radians = fmodf(compose->keyboard_age, 45.0f) *
                          (8.0f * 3.14159265358979323846f / 180.0f);
    const float opacity = floorf(127.0f * (1.0f + sinf(radians))) /
                          255.0f * pane->alpha;
    const float xs[4] = {
        position_x - width * 0.5f, position_x + width * 0.5f,
        position_x - width * 0.5f, position_x + width * 0.5f
    };
    const float ys[4] = {
        position_y - 2.0f, position_y - 2.0f,
        position_y - 2.0f - height, position_y - 2.0f - height
    };
    WmDrawVertex vertices[4] = {0};
    for (size_t index = 0; index < 4; index++) {
        float world_x = pane->matrix[0] * xs[index] +
                        pane->matrix[1] * ys[index] + pane->matrix[3];
        float world_y = pane->matrix[4] * xs[index] +
                        pane->matrix[5] * ys[index] + pane->matrix[7];
        vertices[index].x = WM_FRAME_WIDTH * 0.5f +
                            world_x * (float)WM_FRAME_WIDTH / 832.0f;
        vertices[index].y = WM_FRAME_HEIGHT * 0.5f - world_y;
        vertices[index].color = (WmColor){
            1.0f, 50.0f / 255.0f, 50.0f / 255.0f, opacity
        };
    }
    wm_platform_draw_vertices(compose->platform, vertices, 0);
}

typedef struct MemoLineFeedDraw {
    WmBoardCompose *compose;
    WmCachedFont *face;
    const MemoCaretPane *pane;
} MemoLineFeedDraw;

static bool memo_line_feed_sheet(void *context, size_t sheet,
                                 uint32_t *texture) {
    MemoLineFeedDraw *draw = context;
    return wm_font_cache_sheet(draw->face, sheet, texture);
}

static void memo_line_feed_quad(void *context, const WmFontQuad *quad) {
    MemoLineFeedDraw *draw = context;
    if (!quad || !quad->texture) return;
    WmDrawVertex vertices[4];
    for (size_t index = 0; index < 4; index++) {
        const WmFontVertex *source = &quad->vertices[index];
        vertices[index] = (WmDrawVertex){
            .x = WM_FRAME_WIDTH * 0.5f +
                 source->position[0] * (float)WM_FRAME_WIDTH / 832.0f,
            .y = WM_FRAME_HEIGHT * 0.5f - source->position[1],
            .u = source->uv[0],
            .v = source->uv[1],
            .color = {
                source->color[0] * draw->pane->marker_tint[0],
                source->color[1] * draw->pane->marker_tint[1],
                source->color[2] * draw->pane->marker_tint[2],
                source->color[3]
            }
        };
    }
    wm_platform_draw_vertices(draw->compose->platform, vertices,
                              quad->texture);
}

static void draw_memo_line_feeds(WmBoardCompose *compose,
                                  const MemoCaretPane *pane) {
    if (!pane->visible || pane->marker_alpha <= 0.0f ||
        !strchr(compose->text, '\n')) return;
    WmCachedFont *face = wm_font_cache_resolve(compose->fonts,
                                                pane->font_name);
    const WmFont *font = face ? wm_cached_font_resource(face) : NULL;
    const WmFontTextLayout *layout = font
        ? wm_font_cache_layout(face, compose->text, &pane->font) : NULL;
    if (!layout || !wm_font_glyph(font, 0xe056)) return;
    MemoLineFeedDraw draw = {compose, face, pane};
    WmFontDrawOptions options = {
        .size = {pane->font.font_size[0], pane->font.font_size[1]},
        .alpha = pane->marker_alpha,
        .align = WM_FONT_ALIGN_LEFT,
        .matrix = pane->matrix,
        .sheet_provider = memo_line_feed_sheet,
        .on_quad = memo_line_feed_quad,
        .context = &draw
    };
    for (size_t channel = 0; channel < 4; channel++) {
        options.top_color[channel] = channel == 3 ? 255 : 200;
        options.bottom_color[channel] = options.top_color[channel];
    }
    for (size_t index = 0; index < compose->text_bytes; index++) {
        if (compose->text[index] != '\n' ||
            !wm_font_text_layout_caret(layout, index,
                                        &options.x, &options.y)) continue;
        /* TextDrawer::draw uses the source font's E056 glyph only while the
         * Memo input form is open. The stored newline and caret stay intact. */
        wm_font_emit_line(font, "\xee\x81\x96", &options);
    }
}

static float footer_scene_frame(const WmBoardCompose *compose) {
    switch (compose->phase) {
        case WM_COMPOSE_ENTER_SELECTOR:
            return compose->frame < 26.0f
                       ? 4000.0f + compose->frame
                       : 3113.0f + clamp_frame(compose->frame - 26.0f, 13.0f);
        case WM_COMPOSE_SELECTOR:
            return 3126.0f;
        case WM_COMPOSE_ENTER_MEMO:
            return compose->frame < 13.0f
                       ? 3213.0f + compose->frame
                       : 3313.0f + clamp_frame(compose->frame - 13.0f, 13.0f);
        case WM_COMPOSE_MEMO:
        case WM_COMPOSE_POST_PRESS:
            return 3326.0f;
        case WM_COMPOSE_ENTER_EDIT:
            return 3413.0f + clamp_frame(compose->frame, 13.0f);
        case WM_COMPOSE_EDIT:
            return 3426.0f;
        case WM_COMPOSE_LEAVE_EDIT:
            return 3313.0f + clamp_frame(compose->frame, 13.0f);
        case WM_COMPOSE_SEND:
            return 3413.0f + clamp_frame(compose->frame, 13.0f);
        case WM_COMPOSE_EXIT_AFTER_POST:
            return 3426.0f + clamp_frame(compose->frame, 13.0f);
        case WM_COMPOSE_BACK_MEMO:
            if (compose->frame < 20.0f) return 3326.0f;
            if (compose->frame < 33.0f)
                return 3413.0f + compose->frame - 20.0f;
            return 3113.0f + clamp_frame(compose->frame - 33.0f, 13.0f);
        case WM_COMPOSE_BACK_SELECTOR:
            return compose->frame < 20.0f
                       ? 3213.0f + clamp_frame(compose->frame, 13.0f)
                       : 3426.0f + clamp_frame(compose->frame - 33.0f, 13.0f);
        case WM_COMPOSE_ADDRESS: {
            WmBoardAddressPhase phase = wm_board_address_phase(compose->address);
            float frame = wm_board_address_phase_frame(compose->address);
            if (phase == WM_BOARD_ADDRESS_ENTER) {
                return frame < 13.0f ? 3213.0f + frame :
                       3313.0f + clamp_frame(frame - 13.0f, 13.0f);
            }
            if (phase == WM_BOARD_ADDRESS_EXIT) {
                if (frame < 21.0f) return 3326.0f;
                if (frame < 35.0f)
                    return 3413.0f + clamp_frame(frame - 21.0f, 13.0f);
                return 3113.0f + clamp_frame(frame - 35.0f, 13.0f);
            }
            return 3326.0f;
        }
        case WM_COMPOSE_CLOSED:
            return 1040.0f;
    }
    return 1040.0f;
}

static void pose_footer(WmBoardCompose *compose) {
    WmLayoutClip clips[COMPOSE_CLIP_CAPACITY];
    size_t count = 0;
    append_clip(clips, &count, "my_IplTop_e", "G_SeenChange",
                footer_scene_frame(compose));
    append_clip(clips, &count, "my_IplTop_e", "G_ArwRoop",
                10000.0f + fmodf(compose->age, 55.0f));
    if (compose->phase == WM_COMPOSE_ENTER_SELECTOR) {
        float frame = clamp_frame(compose->frame, 10.0f);
        append_clip(clips, &count, "my_IplTop_e", "G_ArwL_End",
                    10100.0f + frame);
        append_clip(clips, &count, "my_IplTop_e", "G_ArwR_End",
                    10100.0f + frame);
    } else if (compose->phase == WM_COMPOSE_ADDRESS) {
        WmBoardAddressPhase phase = wm_board_address_phase(compose->address);
        float frame = wm_board_address_phase_frame(compose->address);
        float arrow_frame = phase == WM_BOARD_ADDRESS_ENTER
            ? 10150.0f + clamp_frame(frame - 1.0f, 10.0f)
            : phase == WM_BOARD_ADDRESS_EXIT ||
              phase == WM_BOARD_ADDRESS_BOOK_TO_KIND
                ? 10100.0f + clamp_frame(frame, 10.0f)
            : phase == WM_BOARD_ADDRESS_BOOK_RETURN
                ? 10150.0f + clamp_frame(frame, 10.0f)
            : wm_board_address_step(compose->address) ==
                WM_BOARD_ADDRESS_STEP_BOOK ? 10160.0f : 10110.0f;
        static const char *const ends[] = {
            "G_ArwL_End", "G_ArwR_End"
        };
        static const char *const focus_groups[] = {
            "G_ArwL_Focus", "G_ArwR_Focus"
        };
        static const char *const press_groups[] = {
            "G_ArwL_Ac", "G_ArwR_Ac"
        };
        for (size_t side = 0; side < 2; side++) {
            append_clip(clips, &count, "my_IplTop_e", ends[side],
                        arrow_frame);
            ComposeFocus focus = compose->focus[
                side ? WM_COMPOSE_CONTROL_ADDRESS_NEXT :
                       WM_COMPOSE_CONTROL_ADDRESS_PREVIOUS];
            append_clip(clips, &count, "my_IplTop_e", focus_groups[side],
                        (focus.active && !focus.entering ? 10800.0f :
                                                            10600.0f) +
                        clamp_frame(focus.frame, 15.0f));
            if (compose->address_arrow_press[side] >= 0.0f) {
                append_clip(clips, &count, "my_IplTop_e", press_groups[side],
                    10700.0f + clamp_frame(
                        compose->address_arrow_press[side], 30.0f));
            }
        }
    } else {
        append_clip(clips, &count, "my_IplTop_e", "G_ArwL_End", 10110.0f);
        append_clip(clips, &count, "my_IplTop_e", "G_ArwR_End", 10110.0f);
    }
    const WmBoardComposeControl controls[2] = {
        WM_COMPOSE_CONTROL_BACK, WM_COMPOSE_CONTROL_POST
    };
    const char *const groups[2] = {"G_CalExit", "G_Cmn_R"};
    for (size_t index = 0; index < 2; index++) {
        ComposeFocus focus = compose->focus[controls[index]];
        if (!focus.active) continue;
        append_clip(clips, &count, "my_IplTop_e", groups[index],
                    (focus.entering ? 2900.0f : 2930.0f) +
                    clamp_frame(focus.frame, focus.entering ? 6.0f : 8.0f));
    }
    if (compose->phase == WM_COMPOSE_POST_PRESS) {
        append_clip(clips, &count, "my_IplTop_e", "G_Cmn_R",
                    3000.0f + clamp_frame(compose->frame, 20.0f));
    }
    if (compose->phase == WM_COMPOSE_ADDRESS &&
        wm_board_address_phase(compose->address) ==
            WM_BOARD_ADDRESS_REGISTER_PRESS) {
        append_clip(clips, &count, "my_IplTop_e", "G_Cmn_R",
            3000.0f + clamp_frame(
                wm_board_address_phase_frame(compose->address), 20.0f));
    }
    if (compose->phase == WM_COMPOSE_ADDRESS &&
        (wm_board_address_phase(compose->address) ==
             WM_BOARD_ADDRESS_FORM_OK_PRESS ||
         wm_board_address_phase(compose->address) ==
             WM_BOARD_ADDRESS_NICKNAME_OK_PRESS ||
         wm_board_address_phase(compose->address) ==
             WM_BOARD_ADDRESS_MII_OK_PRESS)) {
        append_clip(clips, &count, "my_IplTop_e", "G_Cmn_R",
            3000.0f + clamp_frame(
                wm_board_address_phase_frame(compose->address), 20.0f));
    }
    if ((compose->phase == WM_COMPOSE_BACK_MEMO &&
         compose->frame < 20.0f) ||
        compose->phase == WM_COMPOSE_BACK_SELECTOR) {
        /* HTML's Memo Back press is committed when its 20-frame first
         * stage ends. The following MailOut stage restores the neutral
         * button pose while the selector returns. Holding frame 3020
         * retains its 1.1-scale press track until the entire exit ends. */
        append_clip(clips, &count, "my_IplTop_e", "G_CalExit",
                    3000.0f + clamp_frame(compose->frame, 20.0f));
    }
    if (compose->phase == WM_COMPOSE_ADDRESS &&
        wm_board_address_phase(compose->address) == WM_BOARD_ADDRESS_EXIT) {
        append_clip(clips, &count, "my_IplTop_e", "G_CalExit",
            3000.0f + clamp_frame(
                wm_board_address_phase_frame(compose->address), 20.0f));
    }
    wm_layout_pose(compose->footer, clips, count);
    wm_layout_set_pose_text(compose->footer, "T_BbsMark1", "");
    wm_layout_set_pose_text(compose->footer, "T_CalExit", "Back");
    wm_layout_set_pose_text(compose->footer, "T_Add", "Back");
    const char *right_label = "Post";
    if (compose->phase == WM_COMPOSE_ADDRESS) {
        WmBoardAddressStep step = wm_board_address_step(compose->address);
        right_label = step == WM_BOARD_ADDRESS_STEP_BOOK ? "Register" :
                      step == WM_BOARD_ADDRESS_STEP_FORM ||
                      step == WM_BOARD_ADDRESS_STEP_NICKNAME ||
                      step == WM_BOARD_ADDRESS_STEP_MII ||
                      step == WM_BOARD_ADDRESS_STEP_REVIEW ? "OK" : "";
    }
    wm_layout_set_pose_text(compose->footer, "T_CalAdd_R", right_label);
    wm_layout_set_pose_text(compose->footer, "T_Dust", "");
}

static void draw_network_dialog(WmBoardCompose *compose) {
    if (compose->network_phase == COMPOSE_NETWORK_CLOSED) return;
    WmLayoutClip clips[COMPOSE_CLIP_CAPACITY];
    size_t count = 0;
    append_clip(clips, &count, "my_DialogWindow_a2_DialogIn", "G_InOut",
                compose->network_phase == COMPOSE_NETWORK_ENTER
                    ? compose->network_frame : 25.0f);
    static const WmBoardComposeControl controls[2] = {
        WM_COMPOSE_CONTROL_NETWORK_QUIT,
        WM_COMPOSE_CONTROL_NETWORK_SETTINGS
    };
    static const char *const focus_groups[2] = {
        "G_FocusBtnA", "G_FocusBtnB"
    };
    for (size_t index = 0; index < 2; index++) {
        ComposeFocus focus = compose->focus[controls[index]];
        if (!focus.active) continue;
        append_clip(clips, &count,
            focus.entering ? "my_DialogWindow_a2_FocusBtn_on" :
                             "my_DialogWindow_a2_FocusBtn_off",
            focus_groups[index], clamp_frame(focus.frame, 6.0f));
    }
    if (compose->network_selected != WM_COMPOSE_CONTROL_NONE) {
        append_clip(clips, &count, "my_DialogWindow_a2_SelectBtn_Ac",
            compose->network_selected == WM_COMPOSE_CONTROL_NETWORK_QUIT
                ? "G_SelectBtnA" : "G_SelectBtnB",
            compose->network_phase == COMPOSE_NETWORK_SELECT
                ? compose->network_frame : 20.0f);
    }
    if (compose->network_phase == COMPOSE_NETWORK_EXIT)
        append_clip(clips, &count, "my_DialogWindow_a2_DialogOut",
                    "G_InOut", compose->network_frame);
    wm_layout_pose(compose->network_dialog, clips, count);
    wm_layout_set_pose_text(compose->network_dialog, "T_Dialog",
        "No Internet connection has been configured.\n"
        "Please configure your Internet settings.");
    wm_layout_set_pose_text(compose->network_dialog, "T_BtnA", "Quit");
    wm_layout_set_pose_text(compose->network_dialog, "T_BtnB", "Settings");
    wm_layout_present_with_fonts(compose->platform, compose->textures,
        compose->fonts, compose->network_dialog, true, WM_LAYOUT_IPL, NULL);
}

void wm_board_compose_draw(WmBoardCompose *compose) {
    if (!compose || compose->phase == WM_COMPOSE_CLOSED) return;
    bool dialog_active = wm_board_address_dialog_active(compose->address);
    bool memo_notice = dialog_active && compose->phase != WM_COMPOSE_ADDRESS;
    pose_selector(compose);
    wm_layout_present_with_fonts(compose->platform, compose->textures,
                                 compose->fonts, compose->selector, true,
                                 WM_LAYOUT_IPL, NULL);
    if (compose->phase == WM_COMPOSE_ADDRESS)
        wm_board_address_draw(compose->address);
    if (body_visible(compose)) {
        pose_body(compose);
        bool body_drawn = draw_body_rows(compose);
        MemoCaretPane caret = {0};
        wm_layout_present_filtered_with_fonts(
            compose->platform, compose->textures, compose->fonts,
            compose->body, true, WM_LAYOUT_IPL, NULL,
            body_drawn ? capture_memo_caret_without_sheet :
                         capture_memo_caret_pane, &caret);
        if (compose->phase == WM_COMPOSE_ENTER_EDIT ||
            compose->phase == WM_COMPOSE_EDIT ||
            compose->phase == WM_COMPOSE_LEAVE_EDIT)
            draw_memo_line_feeds(compose, &caret);
        if (compose->phase == WM_COMPOSE_ENTER_EDIT ||
            compose->phase == WM_COMPOSE_EDIT) {
            draw_memo_caret(compose, &caret);
        }
    }
    if (!dialog_active || memo_notice) {
        /* The source modal shade dims the Memo footer, but Back and Post
         * remain drawn under it. Input still belongs only to the notice. */
        pose_footer(compose);
        wm_layout_present_with_fonts(compose->platform, compose->textures,
                                     compose->fonts, compose->footer, true,
                                     WM_LAYOUT_IPL, NULL);
    }
    if (compose->phase == WM_COMPOSE_ENTER_EDIT ||
        compose->phase == WM_COMPOSE_EDIT ||
        compose->phase == WM_COMPOSE_LEAVE_EDIT) {
        float progress = compose->phase == WM_COMPOSE_ENTER_EDIT
            ? clamp_frame(compose->frame, 30.0f) / 30.0f
            : compose->phase == WM_COMPOSE_LEAVE_EDIT
                ? 1.0f - clamp_frame(compose->frame, 30.0f) / 30.0f
                : 1.0f;
        wm_board_keyboard_draw(compose->keyboard, progress,
                                compose->phase == WM_COMPOSE_ENTER_EDIT);
    }
    if (compose->address_keyboard_open)
        wm_board_keyboard_draw(compose->keyboard, 1.0f, false);
    if (dialog_active)
        wm_board_address_draw_dialog(compose->address);
    draw_network_dialog(compose);
}

static bool hit_pane(const WmLayout *layout, const char *pane,
                     int x, int y) {
    WmSourceRect rect;
    return wm_source_pane_rect(layout, pane, true, WM_LAYOUT_IPL,
                               NULL, &rect) &&
           (float)x >= rect.x && (float)x < rect.x + rect.width &&
           (float)y >= rect.y && (float)y < rect.y + rect.height;
}

WmBoardComposeControl wm_board_compose_hit(WmBoardCompose *compose,
                                           int x, int y) {
    if (!compose || (compose->phase != WM_COMPOSE_SELECTOR &&
                     compose->phase != WM_COMPOSE_MEMO &&
                     compose->phase != WM_COMPOSE_EDIT &&
                     compose->phase != WM_COMPOSE_ADDRESS)) {
        return WM_COMPOSE_CONTROL_NONE;
    }
    if (compose->network_phase != COMPOSE_NETWORK_CLOSED) {
        if (compose->network_phase != COMPOSE_NETWORK_READY)
            return WM_COMPOSE_CONTROL_NONE;
        if (hit_pane(compose->network_dialog, "B_BtnA", x, y))
            return WM_COMPOSE_CONTROL_NETWORK_QUIT;
        if (hit_pane(compose->network_dialog, "B_BtnB", x, y))
            return WM_COMPOSE_CONTROL_NETWORK_SETTINGS;
        return WM_COMPOSE_CONTROL_NONE;
    }
    if (compose->phase == WM_COMPOSE_EDIT) {
        bool symbols = wm_board_keyboard_symbols_visible(compose->keyboard);
        if (!symbols) pose_body(compose);
        if (!symbols &&
            compose->arrows[COMPOSE_SCROLL_EDITOR][COMPOSE_SCROLL_UP].visible &&
            hit_pane(compose->body, "B_txtScrll_UP", x, y)) {
            return WM_COMPOSE_CONTROL_SCROLL_UP;
        }
        if (!symbols &&
            compose->arrows[COMPOSE_SCROLL_EDITOR][COMPOSE_SCROLL_DOWN].visible &&
            hit_pane(compose->body, "B_txtScrll_DOWN", x, y)) {
            return WM_COMPOSE_CONTROL_SCROLL_DOWN;
        }
        WmBoardKeyboardControl key = wm_board_keyboard_hit(compose->keyboard,
                                                            x, y);
        return key == WM_KEYBOARD_NONE ? WM_COMPOSE_CONTROL_NONE :
               (WmBoardComposeControl)(WM_COMPOSE_CONTROL_KEY_FIRST + key - 1);
    }
    if (compose->address_keyboard_open) {
        WmBoardKeyboardControl key = wm_board_keyboard_hit(compose->keyboard,
                                                            x, y);
        return key == WM_KEYBOARD_NONE ? WM_COMPOSE_CONTROL_NONE :
               (WmBoardComposeControl)(WM_COMPOSE_CONTROL_KEY_FIRST + key - 1);
    }
    if (wm_board_address_dialog_active(compose->address)) {
        bool yes = false;
        if (wm_board_address_dialog_choice_hit(compose->address,
                                                x, y, &yes)) {
            return yes ? WM_COMPOSE_CONTROL_ADDRESS_DIALOG_YES :
                         WM_COMPOSE_CONTROL_ADDRESS_DIALOG_NO;
        }
        return wm_board_address_dialog_hit(compose->address, x, y)
            ? WM_COMPOSE_CONTROL_ADDRESS_DIALOG_OK :
              WM_COMPOSE_CONTROL_NONE;
    }
    WmBoardAddressPhase address_phase = WM_BOARD_ADDRESS_CLOSED;
    if (compose->phase == WM_COMPOSE_ADDRESS) {
        address_phase = wm_board_address_phase(compose->address);
        if (address_phase != WM_BOARD_ADDRESS_READY &&
            address_phase != WM_BOARD_ADDRESS_KIND_READY &&
            address_phase != WM_BOARD_ADDRESS_FORM_READY &&
            address_phase != WM_BOARD_ADDRESS_NICKNAME_READY &&
            address_phase != WM_BOARD_ADDRESS_MII_READY &&
            address_phase != WM_BOARD_ADDRESS_REVIEW_READY &&
            address_phase != WM_BOARD_ADDRESS_CONTACT_READY)
            return WM_COMPOSE_CONTROL_NONE;
    }
    pose_footer(compose);
    if (hit_pane(compose->footer, "B_CalExit", x, y)) {
        return WM_COMPOSE_CONTROL_BACK;
    }
    if (compose->phase == WM_COMPOSE_ADDRESS) {
        if (address_phase == WM_BOARD_ADDRESS_READY) {
            if (hit_pane(compose->footer, "B_ArwL", x, y))
                return WM_COMPOSE_CONTROL_ADDRESS_PREVIOUS;
            if (hit_pane(compose->footer, "B_ArwR", x, y))
                return WM_COMPOSE_CONTROL_ADDRESS_NEXT;
            if (hit_pane(compose->footer, "B_Add_R", x, y))
                return WM_COMPOSE_CONTROL_POST;
            unsigned row;
            if (wm_board_address_entry_hit(compose->address, x, y, &row))
                return (WmBoardComposeControl)(
                    WM_COMPOSE_CONTROL_ADDRESS_ENTRY_FIRST + row);
        } else if (address_phase == WM_BOARD_ADDRESS_KIND_READY) {
            bool wii = false;
            if (wm_board_address_kind_hit(compose->address, x, y, &wii))
                return wii ? WM_COMPOSE_CONTROL_ADDRESS_WII :
                             WM_COMPOSE_CONTROL_ADDRESS_OTHERS;
        } else if (address_phase == WM_BOARD_ADDRESS_FORM_READY ||
                   address_phase == WM_BOARD_ADDRESS_NICKNAME_READY) {
            if (wm_board_address_field_valid(compose->address) &&
                hit_pane(compose->footer, "B_Add_R", x, y))
                return WM_COMPOSE_CONTROL_POST;
            if (wm_board_address_form_hit(compose->address, x, y))
                return WM_COMPOSE_CONTROL_ADDRESS_EDIT;
        } else if (address_phase == WM_BOARD_ADDRESS_MII_READY) {
            if (hit_pane(compose->footer, "B_Add_R", x, y))
                return WM_COMPOSE_CONTROL_POST;
            if (wm_board_address_form_hit(compose->address, x, y))
                return WM_COMPOSE_CONTROL_ADDRESS_MII;
        } else if (address_phase == WM_BOARD_ADDRESS_REVIEW_READY) {
            if (hit_pane(compose->footer, "B_Add_R", x, y))
                return WM_COMPOSE_CONTROL_POST;
            if (wm_board_address_review_hit(compose->address, x, y))
                return WM_COMPOSE_CONTROL_ADDRESS_INFO;
        } else if (address_phase == WM_BOARD_ADDRESS_CONTACT_READY) {
            WmBoardAddressContactAction action;
            if (wm_board_address_contact_hit(compose->address, x, y,
                                              &action)) {
                if (action == WM_BOARD_ADDRESS_CONTACT_CHANGE_NICKNAME)
                    return WM_COMPOSE_CONTROL_ADDRESS_CHANGE_NICKNAME;
                if (action == WM_BOARD_ADDRESS_CONTACT_ERASE)
                    return WM_COMPOSE_CONTROL_ADDRESS_ERASE;
                return WM_COMPOSE_CONTROL_ADDRESS_INFO;
            }
        }
    } else if (compose->phase == WM_COMPOSE_SELECTOR) {
        pose_selector(compose);
        if (hit_pane(compose->selector, "B_MailIn", x, y)) {
            return WM_COMPOSE_CONTROL_MEMO;
        }
        if (hit_pane(compose->selector, "B_LetterIn", x, y)) {
            return WM_COMPOSE_CONTROL_LETTER;
        }
        if (hit_pane(compose->selector, "B_AdressIn", x, y)) {
            return WM_COMPOSE_CONTROL_ADDRESS;
        }
    } else if (compose->phase == WM_COMPOSE_MEMO) {
        if (hit_pane(compose->footer, "B_Add_R", x, y)) {
            return WM_COMPOSE_CONTROL_POST;
        }
        pose_body(compose);
        if (hit_pane(compose->body, "B_Nigaoe", x, y)) {
            return WM_COMPOSE_CONTROL_MII;
        }
        if (compose->arrows[COMPOSE_SCROLL_DISPLAY][COMPOSE_SCROLL_UP].visible &&
            hit_pane(compose->body, "B_ArwR", x, y)) {
            return WM_COMPOSE_CONTROL_SCROLL_UP;
        }
        if (compose->arrows[COMPOSE_SCROLL_DISPLAY][COMPOSE_SCROLL_DOWN].visible &&
            hit_pane(compose->body, "B_ArwL", x, y)) {
            return WM_COMPOSE_CONTROL_SCROLL_DOWN;
        }
        if (hit_pane(compose->body, "B_2l_TextBox", x, y)) {
            return WM_COMPOSE_CONTROL_EDIT;
        }
    }
    return WM_COMPOSE_CONTROL_NONE;
}

void wm_board_compose_hover(WmBoardCompose *compose,
                            WmBoardComposeControl control) {
    if (!compose || compose->hover == control) return;
    if (compose->network_phase != COMPOSE_NETWORK_CLOSED) {
        if (control != WM_COMPOSE_CONTROL_NETWORK_QUIT &&
            control != WM_COMPOSE_CONTROL_NETWORK_SETTINGS)
            control = WM_COMPOSE_CONTROL_NONE;
        if (compose->network_phase != COMPOSE_NETWORK_READY)
            control = WM_COMPOSE_CONTROL_NONE;
        if (compose->hover == control) return;
        if (compose->hover == WM_COMPOSE_CONTROL_NETWORK_QUIT ||
            compose->hover == WM_COMPOSE_CONTROL_NETWORK_SETTINGS) {
            change_focus(&compose->focus[compose->hover], false, 6.0f, 6.0f);
        }
        compose->hover = control;
        if (control != WM_COMPOSE_CONTROL_NONE) {
            change_focus(&compose->focus[control], true, 6.0f, 6.0f);
            queue_key_cue(compose, "WIPL_SE_BT_TARGETTING");
        }
        return;
    }
    WmBoardKeyboardControl keyboard_control = WM_KEYBOARD_NONE;
    if (control >= WM_COMPOSE_CONTROL_KEY_FIRST &&
        control <= WM_COMPOSE_CONTROL_KEY_LAST) {
        keyboard_control = (WmBoardKeyboardControl)(
            control - WM_COMPOSE_CONTROL_KEY_FIRST + 1);
    }
    wm_board_keyboard_hover(compose->keyboard, keyboard_control);
    bool committed_phone_key =
        wm_board_keyboard_take_phone_commit(compose->keyboard);
    if (committed_phone_key &&
        (compose->phase == WM_COMPOSE_EDIT ||
         compose->address_keyboard_open)) {
        queue_key_cue(compose, "WIPL_SE_CHAR_DECIDE");
        compose->keyboard_age = 0.0f;
        if (compose->phase == WM_COMPOSE_EDIT) {
            compose->scroll_lines = memo_line_count(compose);
            refresh_scroll(compose);
        }
    }
    if (compose->phase == WM_COMPOSE_ADDRESS ||
        wm_board_address_dialog_active(compose->address)) {
        wm_board_address_dialog_hover(compose->address,
            control == WM_COMPOSE_CONTROL_ADDRESS_DIALOG_OK);
        if (compose->hover == WM_COMPOSE_CONTROL_ADDRESS_DIALOG_YES ||
            compose->hover == WM_COMPOSE_CONTROL_ADDRESS_DIALOG_NO) {
            wm_board_address_dialog_hover_choice(compose->address,
                compose->hover == WM_COMPOSE_CONTROL_ADDRESS_DIALOG_YES,
                false);
        }
        if (control == WM_COMPOSE_CONTROL_ADDRESS_DIALOG_YES ||
            control == WM_COMPOSE_CONTROL_ADDRESS_DIALOG_NO) {
            wm_board_address_dialog_hover_choice(compose->address,
                control == WM_COMPOSE_CONTROL_ADDRESS_DIALOG_YES, true);
        }
        int choice = control == WM_COMPOSE_CONTROL_ADDRESS_WII ? 0 :
                     control == WM_COMPOSE_CONTROL_ADDRESS_OTHERS ? 1 : -1;
        wm_board_address_hover_kind(compose->address, choice);
        WmBoardAddressContactAction action = WM_BOARD_ADDRESS_CONTACT_NONE;
        if (control == WM_COMPOSE_CONTROL_ADDRESS_INFO)
            action = WM_BOARD_ADDRESS_CONTACT_INFO;
        else if (control == WM_COMPOSE_CONTROL_ADDRESS_CHANGE_NICKNAME)
            action = WM_BOARD_ADDRESS_CONTACT_CHANGE_NICKNAME;
        else if (control == WM_COMPOSE_CONTROL_ADDRESS_ERASE)
            action = WM_BOARD_ADDRESS_CONTACT_ERASE;
        wm_board_address_hover_contact(compose->address, action);
    }
    ComposeScrollMode mode = scroll_mode(compose);
    if (compose->hover == WM_COMPOSE_CONTROL_SCROLL_UP ||
        compose->hover == WM_COMPOSE_CONTROL_SCROLL_DOWN) {
        size_t direction = compose->hover == WM_COMPOSE_CONTROL_SCROLL_UP
                               ? COMPOSE_SCROLL_UP : COMPOSE_SCROLL_DOWN;
        compose->arrows[mode][direction].focus =
            (ComposeFocus){true, false, 0.0f};
    }
    if (visual_focus_control(compose->hover)) {
        change_focus(&compose->focus[compose->hover], false,
                     focus_duration(compose->hover, true),
                     focus_duration(compose->hover, false));
    }
    compose->hover = control;
    if (visual_focus_control(control)) {
        change_focus(&compose->focus[control], true,
                     focus_duration(control, true),
                     focus_duration(control, false));
    } else if (control == WM_COMPOSE_CONTROL_SCROLL_UP ||
               control == WM_COMPOSE_CONTROL_SCROLL_DOWN) {
        size_t direction = control == WM_COMPOSE_CONTROL_SCROLL_UP
                               ? COMPOSE_SCROLL_UP : COMPOSE_SCROLL_DOWN;
        if (compose->arrows[mode][direction].visible) {
            compose->arrows[mode][direction].focus =
                (ComposeFocus){true, true, 0.0f};
        }
    }
}
