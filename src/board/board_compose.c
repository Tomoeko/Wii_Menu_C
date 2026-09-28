#define _POSIX_C_SOURCE 200809L

#include "board_compose_internal.h"
#include "board_compose_presentation.h"
#include "board_text.h"

#include "wii_menu/layout/layout_runtime.h"
#include "wii_menu/render/material_prepare.h"

#include <math.h>
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

enum {
    COMPOSE_PATH_CAPACITY = 4096
};

static bool close_address_keyboard(WmBoardCompose *compose, bool accepting);

static void queue_key_cue(WmBoardCompose *compose, const char *name) {
    if (compose->key_cue_count <
        sizeof(compose->key_cues) / sizeof(compose->key_cues[0])) {
        compose->key_cues[compose->key_cue_count++] = name;
    }
}

static void open_network_dialog(WmBoardCompose *compose, bool wii_connect24) {
    wm_board_compose_hover(compose, WM_COMPOSE_CONTROL_NONE);
    compose->network_wii_connect24 = wii_connect24;
    compose->network_phase = COMPOSE_NETWORK_ENTER;
    compose->network_frame = 0.0f;
    compose->network_selected = WM_COMPOSE_CONTROL_NONE;
    compose->focus[WM_COMPOSE_CONTROL_NETWORK_QUIT] = (ComposeFocus){0};
    compose->focus[WM_COMPOSE_CONTROL_NETWORK_SETTINGS] = (ComposeFocus){0};
    queue_key_cue(compose, "WIPL_SE_DECIDE");
    queue_key_cue(compose, "WIPL_SE_INFO_WINDOW");
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

static void follow_memo_caret(WmBoardCompose *compose) {
    if (!compose->scroll.follow_caret_pending || compose->phase != WM_COMPOSE_EDIT ||
        compose->scroll.moving) return;
    WmFontPane pane;
    const char *font_name = NULL;
    if (!wm_layout_pane_font(compose->body, "T_Letter", &pane, &font_name))
        return;
    WmCachedFont *face = wm_font_cache_resolve(compose->fonts, font_name);
    size_t caret_bytes;
    const char *display = memo_display_text(compose, &caret_bytes);
    const WmFontTextLayout *layout = face
        ? wm_font_cache_layout(face, display, &pane) : NULL;
    float first_x, first_y, caret_x, caret_y;
    if (!layout ||
        !wm_font_text_layout_caret(layout, 0, &first_x, &first_y) ||
        !wm_font_text_layout_caret(layout, caret_bytes, &caret_x, &caret_y))
        return;
    float height = compose->scroll.line_height;
    float line_y = roundf((first_y - caret_y) / height) * height;
    float midpoint = line_y + height * 0.5f;
    float offset = compose->scroll.offset;
    float target = offset;
    if (offset > midpoint) {
        target -= truncf((offset - line_y) / height) * height;
    } else if (offset + 2.0f * height < midpoint) {
        target += (truncf((midpoint - offset - 2.0f * height) / height) + 1.0f)
            * height;
    }
    compose->scroll.follow_caret_pending = false;
    if (board_compose_scroll_start(&compose->scroll, target)) {
        queue_key_cue(compose, "WIPL_SE_LINE_SCROLL");
    }
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
    wm_board_keyboard_set_text_context(compose->keyboard,
                                        compose->draft.keyboard_text);
    WmLayoutPaneState body;
    compose->scroll.line_height =
        wm_layout_pane_state(compose->body, "N_Body", &body) &&
        body.size[1] > 0.0f ? body.size[1] : 42.0f;
    board_compose_scroll_reset(&compose->scroll);
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
    compose->network_wii_connect24 = false;
    compose->network_frame = 0.0f;
    compose->network_selected = WM_COMPOSE_CONTROL_NONE;
    board_compose_draft_reset(&compose->draft);
    compose->scroll.follow_caret_pending = false;
    compose->hover = WM_COMPOSE_CONTROL_NONE;
    compose->outcome = WM_COMPOSE_OUTCOME_NONE;
    compose->key_cue_count = 0;
    compose->inserting_symbol = false;
    compose->repeating_keytop = false;
    memset(compose->focus, 0, sizeof(compose->focus));
    compose->address_arrow_press[0] = -1.0f;
    compose->address_arrow_press[1] = -1.0f;
    wm_board_keyboard_reset(compose->keyboard);
    wm_board_keyboard_set_text_context(compose->keyboard,
                                        compose->draft.keyboard_text);
    wm_board_address_reset(compose->address);
    board_compose_scroll_reset(&compose->scroll);
}

bool wm_board_compose_open(WmBoardCompose *compose) {
    if (!compose || compose->phase != WM_COMPOSE_CLOSED) return false;
    compose->phase = WM_COMPOSE_ENTER_SELECTOR;
    compose->frame = 0.0f;
    compose->age = 0.0f;
    compose->keyboard_age = 0.0f;
    compose->address_keyboard_open = false;
    compose->network_phase = COMPOSE_NETWORK_CLOSED;
    compose->network_wii_connect24 = false;
    compose->network_frame = 0.0f;
    compose->network_selected = WM_COMPOSE_CONTROL_NONE;
    compose->hover = WM_COMPOSE_CONTROL_NONE;
    compose->outcome = WM_COMPOSE_OUTCOME_NONE;
    compose->key_cue_count = 0;
    compose->inserting_symbol = false;
    compose->repeating_keytop = false;
    board_compose_draft_reset(&compose->draft);
    compose->scroll.follow_caret_pending = false;
    memset(compose->focus, 0, sizeof(compose->focus));
    compose->address_arrow_press[0] = -1.0f;
    compose->address_arrow_press[1] = -1.0f;
    wm_board_keyboard_reset(compose->keyboard);
    wm_board_keyboard_set_text_context(compose->keyboard,
                                        compose->draft.keyboard_text);
    wm_board_address_reset(compose->address);
    board_compose_scroll_reset(&compose->scroll);
    return true;
}

WmBoardComposePhase wm_board_compose_phase(const WmBoardCompose *compose) {
    return compose ? compose->phase : WM_COMPOSE_CLOSED;
}

float wm_board_compose_phase_frame(const WmBoardCompose *compose) {
    return compose ? compose->frame : 0.0f;
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
    return compose ? compose->draft.text : NULL;
}

const char *wm_board_compose_network_message(const WmBoardCompose *compose) {
    if (!compose || compose->network_phase == COMPOSE_NETWORK_CLOSED)
        return NULL;
    return compose->network_wii_connect24
        ? "WiiConnect24 is not turned on.\n"
          "Confirm your WiiConnect24 setting\nin Wii Settings."
        : "No Internet connection has been configured.\n"
          "Please configure your Internet settings.";
}

const char *wm_board_compose_display_text(WmBoardCompose *compose) {
    return compose ? memo_display_text(compose, NULL) : NULL;
}

size_t wm_board_compose_caret(const WmBoardCompose *compose) {
    return compose ? compose->draft.caret_bytes : 0;
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

float wm_board_compose_frames_to_boundary(const WmBoardCompose *compose) {
    if (!compose) return 0.0f;
    return fmaxf(0.0f, phase_duration(compose->phase) - compose->frame);
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
            if (open_settings) {
                compose->outcome = compose->network_wii_connect24
                    ? WM_COMPOSE_OUTCOME_OPEN_CONNECT24_SETTINGS
                    : WM_COMPOSE_OUTCOME_OPEN_SETTINGS;
            }
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
    board_compose_scroll_advance(&compose->scroll, frames, compose->phase);
    follow_memo_caret(compose);
    float remaining = frames;
    while (remaining > 0.0f) {
        float duration = phase_duration(compose->phase);
        if (duration == 0.0f) return;
        float amount = fminf(remaining, duration - compose->frame);
        compose->frame += amount;
        remaining -= amount;
        if (compose->phase == WM_COMPOSE_LEAVE_EDIT) {
            board_compose_scroll_leave_edit(&compose->scroll, compose->frame);
        }
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
            board_compose_scroll_finish_leave_edit(&compose->scroll);
        }
        board_compose_scroll_refresh(&compose->scroll, compose->phase);
        if (completed == WM_COMPOSE_ENTER_EDIT &&
            compose->phase == WM_COMPOSE_EDIT) {
            board_compose_scroll_enter_edit(&compose->scroll);
            follow_memo_caret(compose);
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
        compose->scroll.lines = memo_line_count(compose);
        compose->scroll.start = compose->scroll.offset;
        compose->scroll.target = fminf(compose->scroll.offset,
            board_compose_scroll_maximum(&compose->scroll, COMPOSE_SCROLL_DISPLAY));
        compose->scroll.moving = false;
        compose->scroll.follow_caret_pending = false;
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
    board_compose_scroll_refresh(&compose->scroll, compose->phase);
    return true;
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
        prefix_bytes > compose->draft.caret_bytes ||
        replacement_bytes > BOARD_COMPOSE_MAX_TEXT_BYTES -
                            (compose->draft.text_bytes - prefix_bytes)) return false;
    if (!board_compose_draft_replace_before_caret(
            &compose->draft, prefix_bytes, replacement, replacement_bytes)) {
        return false;
    }
    wm_board_keyboard_text_changed(compose->keyboard, phone_prediction);
    compose->keyboard_age = 0.0f;
    compose->scroll.lines = memo_line_count(compose);
    board_compose_scroll_refresh(&compose->scroll, compose->phase);
    compose->scroll.follow_caret_pending = true;
    follow_memo_caret(compose);
    return true;
}

static bool keyboard_insert_character(WmBoardCompose *compose,
                                      WmBoardKeyboardControl key,
                                      const char character[5]) {
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
    if (compose->key_cue_count == 0 ||
        (compose->key_cue_count == 1 &&
         strcmp(compose->key_cues[0], "WIPL_SE_LINE_SCROLL") == 0))
        queue_key_cue(compose,
                      key == WM_KEYBOARD_SPACE ||
                      key == WM_KEYBOARD_RETURN ||
                      phone_space
                          ? "WIPL_SE_CHAR_DECIDE" :
                            "WIPL_SE_CHAR_INPUT");
    return true;
}

static bool keyboard_replace_last(WmBoardCompose *compose,
                                  const char character[5]) {
    if (compose->address_keyboard_open) {
        bool replaced = wm_board_address_backspace(compose->address)
            && wm_board_address_insert_text(compose->address,
                                             character);
        queue_key_cue(compose, replaced ? "WIPL_SE_CHAR_INPUT" :
                                    "WIPL_SE_CHAR_DELETE_ERROR");
        return true;
    }
    if (!board_compose_draft_replace_last_byte(
            &compose->draft, character[0])) {
        wm_board_keyboard_clear_phone_pending(compose->keyboard);
        queue_key_cue(compose, "WIPL_SE_CHAR_DELETE_ERROR");
        return true;
    }
    compose->keyboard_age = 0.0f;
    wm_board_keyboard_text_changed(compose->keyboard, true);
    compose->scroll.lines = memo_line_count(compose);
    board_compose_scroll_refresh(&compose->scroll, compose->phase);
    compose->scroll.follow_caret_pending = true;
    follow_memo_caret(compose);
    queue_key_cue(compose, "WIPL_SE_CHAR_INPUT");
    return true;
}

static bool keyboard_phone_boundary(WmBoardCompose *compose,
                                    WmBoardKeyboardControl key,
                                    bool reverse, char character[5]) {
    WmBoardKeyboardComposition composition;
    if (compose->address_keyboard_open ||
        !wm_board_keyboard_composition(compose->keyboard,
                                        &composition) ||
        composition.prefix_bytes > compose->draft.caret_bytes ||
        !composition.selected_candidate) {
        queue_key_cue(compose, "WIPL_SE_CHAR_DELETE_ERROR");
        return true;
    }
    char selected[WM_KEYBOARD_CANDIDATE_UTF8_CAPACITY];
    size_t selected_bytes =
        strlen(composition.selected_candidate);
    size_t base = compose->draft.text_bytes - composition.prefix_bytes;
    if (selected_bytes == 0 ||
        selected_bytes >= sizeof(selected) ||
        selected_bytes >= BOARD_COMPOSE_MAX_TEXT_BYTES - base) {
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

static bool keyboard_accept_candidate(WmBoardCompose *compose,
                                      bool predicted) {
    char replacement[WM_KEYBOARD_CANDIDATE_UTF8_CAPACITY];
    const char *selected =
        wm_board_keyboard_candidate_text(compose->keyboard);
    if (!selected || strlen(selected) >= sizeof(replacement)) {
        queue_key_cue(compose, "WIPL_SE_CHAR_DELETE_ERROR");
        return true;
    }
    memcpy(replacement, selected, strlen(selected) + 1);
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

static bool activate_keyboard_control(WmBoardCompose *compose,
                                      WmBoardComposeControl control,
                                      bool reverse, bool *handled) {
    *handled = true;
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
        case WM_KEYBOARD_ACTION_INSERT:
            return keyboard_insert_character(compose, key, character);
        case WM_KEYBOARD_ACTION_REPLACE_LAST:
            return keyboard_replace_last(compose, character);
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
        case WM_KEYBOARD_ACTION_PHONE_BOUNDARY:
            return keyboard_phone_boundary(compose, key, reverse,
                                           character);
        case WM_KEYBOARD_ACTION_ACCEPT_CANDIDATE:
        case WM_KEYBOARD_ACTION_PREDICT_PHONE:
            return keyboard_accept_candidate(compose,
                action == WM_KEYBOARD_ACTION_PREDICT_PHONE);
        case WM_KEYBOARD_ACTION_NONE:
            return false;
    }
    /* Preserve the dispatcher fallthrough for an unknown action. */
    *handled = false;
    return false;
}

static bool activate_address_control(WmBoardCompose *compose,
                                     WmBoardComposeControl control) {
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
            if (wm_board_address_phase(compose->address) !=
                    WM_BOARD_ADDRESS_READY ||
                wm_board_address_page(compose->address) == 0)
                return false;
            open_network_dialog(compose, true);
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
    return false;
}

static bool activate_selector_control(WmBoardCompose *compose,
                                      WmBoardComposeControl control) {
    if (compose->phase == WM_COMPOSE_SELECTOR) {
        if (control == WM_COMPOSE_CONTROL_MEMO) {
            compose->phase = WM_COMPOSE_ENTER_MEMO;
            compose->frame = 0.0f;
            board_compose_scroll_refresh(&compose->scroll, compose->phase);
            return true;
        }
        if (control == WM_COMPOSE_CONTROL_LETTER) {
            open_network_dialog(compose, false);
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
    return false;
}

static bool activate_pointer_caret(WmBoardCompose *compose,
                                   WmBoardComposeControl control) {
    if (compose->phase == WM_COMPOSE_EDIT &&
        control == WM_COMPOSE_CONTROL_EDIT && compose->draft.pointer_caret_valid) {
        WmBoardKeyboardComposition composition;
        if (wm_board_keyboard_composition(compose->keyboard, &composition) &&
            composition.selected_candidate &&
            strcmp(composition.selected_candidate, ">") != 0) {
            bool committed = replace_keyboard_suffix(compose,
                composition.prefix_bytes, composition.selected_candidate, false);
            if (!committed) return false;
            /* The first text press commits the displayed composition. The
             * next fresh press selects a literal insertion boundary. */
            wm_board_keyboard_finish_composition(compose->keyboard);
            wm_board_keyboard_clear_phone_pending(compose->keyboard);
            compose->draft.pointer_caret_valid = false;
            queue_key_cue(compose, "WIPL_SE_CHAR_DECIDE");
            return true;
        }
        wm_board_keyboard_finish_composition(compose->keyboard);
        wm_board_keyboard_clear_phone_pending(compose->keyboard);
        compose->draft.caret_bytes = compose->draft.pointer_caret_bytes;
        compose->draft.pointer_caret_valid = false;
        board_compose_draft_sync_keyboard(&compose->draft);
        wm_board_keyboard_text_changed(compose->keyboard, false);
        wm_board_keyboard_finish_composition(compose->keyboard);
        compose->keyboard_age = 0.0f;
        compose->scroll.follow_caret_pending = true;
        follow_memo_caret(compose);
        queue_key_cue(compose, "WIPL_SE_CHAR_CURSOR");
        return true;
    }
    return false;
}

static bool activate_memo_control(WmBoardCompose *compose,
                                  WmBoardComposeControl control) {
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
            compose->draft.caret_bytes = compose->draft.pointer_caret_valid
                ? compose->draft.pointer_caret_bytes : compose->draft.text_bytes;
            compose->draft.pointer_caret_valid = false;
            board_compose_draft_sync_keyboard(&compose->draft);
            wm_board_keyboard_reset(compose->keyboard);
            wm_board_keyboard_set_text_context(compose->keyboard,
                                                compose->draft.keyboard_text);
            compose->phase = WM_COMPOSE_ENTER_EDIT;
            compose->frame = 0.0f;
            compose->keyboard_age = 0.0f;
            compose->key_cue_count = 0;
            queue_key_cue(compose, "WIPL_SE_SK_OPEN");
            board_compose_scroll_refresh(&compose->scroll, compose->phase);
            compose->scroll.follow_caret_pending = true;
            return true;
        }
        if (control == WM_COMPOSE_CONTROL_POST &&
            board_compose_draft_has_nonspace(&compose->draft)) {
            wm_board_compose_hover(compose, WM_COMPOSE_CONTROL_NONE);
            compose->phase = WM_COMPOSE_POST_PRESS;
            compose->frame = 0.0f;
            board_compose_scroll_refresh(&compose->scroll, compose->phase);
            return true;
        }
    }
    return false;
}

static bool activate_scroll_control(WmBoardCompose *compose,
                                    WmBoardComposeControl control) {
    if (compose->phase == WM_COMPOSE_EDIT &&
        (control == WM_COMPOSE_CONTROL_SCROLL_UP ||
         control == WM_COMPOSE_CONTROL_SCROLL_DOWN)) {
        if (wm_board_keyboard_symbols_visible(compose->keyboard)) return false;
        size_t direction = control == WM_COMPOSE_CONTROL_SCROLL_UP
                               ? COMPOSE_SCROLL_UP : COMPOSE_SCROLL_DOWN;
        BoardComposeScrollArrow *arrow =
            &compose->scroll.arrows[COMPOSE_SCROLL_EDITOR][direction];
        if (!arrow->visible || !board_compose_scroll_start(&compose->scroll,
            compose->scroll.offset + (direction == COMPOSE_SCROLL_UP
                ? -compose->scroll.line_height : compose->scroll.line_height))) {
            return false;
        }
        arrow->press_active = true;
        arrow->press_frame = 0.0f;
        compose->scroll.follow_caret_pending = false;
        return true;
    }
    if (compose->phase == WM_COMPOSE_MEMO &&
        (control == WM_COMPOSE_CONTROL_SCROLL_UP ||
         control == WM_COMPOSE_CONTROL_SCROLL_DOWN)) {
        size_t direction = control == WM_COMPOSE_CONTROL_SCROLL_UP
                               ? COMPOSE_SCROLL_UP : COMPOSE_SCROLL_DOWN;
        BoardComposeScrollArrow *arrow =
            &compose->scroll.arrows[COMPOSE_SCROLL_DISPLAY][direction];
        float step = 3.0f * compose->scroll.line_height;
        if (!arrow->visible || !board_compose_scroll_start(&compose->scroll,
            compose->scroll.offset + (direction == COMPOSE_SCROLL_UP
                ? -step : step))) return false;
        arrow->press_active = true;
        arrow->press_frame = 0.0f;
        return true;
    }
    return false;
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
        bool handled;
        bool activated = activate_keyboard_control(compose, control,
                                                   reverse, &handled);
        if (handled) return activated;
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
    if (compose->phase == WM_COMPOSE_ADDRESS)
        return activate_address_control(compose, control);
    if (compose->phase == WM_COMPOSE_SELECTOR)
        return activate_selector_control(compose, control);
    if (compose->phase == WM_COMPOSE_EDIT &&
        control == WM_COMPOSE_CONTROL_EDIT &&
        compose->draft.pointer_caret_valid)
        return activate_pointer_caret(compose, control);
    if (compose->phase == WM_COMPOSE_MEMO &&
        (control == WM_COMPOSE_CONTROL_MII ||
         control == WM_COMPOSE_CONTROL_EDIT ||
         control == WM_COMPOSE_CONTROL_POST))
        return activate_memo_control(compose, control);
    return activate_scroll_control(compose, control);
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
    size_t bytes;
    bool has_whitespace = false;
    if (!wm_board_text_memo_input(utf8, BOARD_COMPOSE_MAX_TEXT_BYTES,
                                   &bytes, &has_whitespace)) return false;
    WmBoardKeyboardComposition composition;
    size_t prefix_units = SIZE_MAX;
    bool has_composition = compose->phase == WM_COMPOSE_EDIT &&
        !has_whitespace && !compose->inserting_phone &&
        wm_board_keyboard_composition(compose->keyboard, &composition) &&
        composition.prefix_bytes <= compose->draft.caret_bytes;
    if (has_composition) {
        prefix_units = wm_board_text_utf16_units(
            compose->draft.text + compose->draft.caret_bytes - composition.prefix_bytes,
            composition.prefix_bytes);
    }
    bool commit_boundary = has_composition && prefix_units != SIZE_MAX &&
                           prefix_units >= 32;
    if (commit_boundary) {
        char candidate[WM_KEYBOARD_CANDIDATE_UTF8_CAPACITY];
        const char *selected = composition.selected_candidate;
        size_t selected_bytes = selected ? strlen(selected) : 0;
        size_t base = compose->draft.text_bytes - composition.prefix_bytes;
        if (!selected_bytes || selected_bytes >= sizeof(candidate) ||
            selected_bytes > BOARD_COMPOSE_MAX_TEXT_BYTES - base ||
            bytes > BOARD_COMPOSE_MAX_TEXT_BYTES - base - selected_bytes)
            return false;
        memcpy(candidate, selected, selected_bytes + 1);
        if (!replace_keyboard_suffix(compose, composition.prefix_bytes,
                                      candidate, false)) return false;
        wm_board_keyboard_finish_composition(compose->keyboard);
    }
    if (bytes > BOARD_COMPOSE_MAX_TEXT_BYTES - compose->draft.text_bytes) return false;
    if (compose->phase == WM_COMPOSE_MEMO) {
        board_compose_draft_sync_keyboard(&compose->draft);
        wm_board_keyboard_reset(compose->keyboard);
        wm_board_keyboard_set_text_context(compose->keyboard,
                                            compose->draft.keyboard_text);
        compose->phase = WM_COMPOSE_ENTER_EDIT;
        compose->frame = 0.0f;
        compose->keyboard_age = 0.0f;
        compose->key_cue_count = 0;
        queue_key_cue(compose, "WIPL_SE_SK_OPEN");
    }
    if (compose->phase == WM_COMPOSE_EDIT && has_whitespace)
        wm_board_keyboard_finish_composition(compose->keyboard);
    if (!board_compose_draft_insert(&compose->draft, utf8, bytes)) return false;
    wm_board_keyboard_text_changed(compose->keyboard,
                                    compose->inserting_phone);
    if (commit_boundary) queue_key_cue(compose, "WIPL_SE_CHAR_DECIDE");
    compose->keyboard_age = 0.0f;
    compose->scroll.lines = memo_line_count(compose);
    board_compose_scroll_refresh(&compose->scroll, compose->phase);
    compose->scroll.follow_caret_pending = true;
    follow_memo_caret(compose);
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
        compose->draft.caret_bytes == 0) return false;
    if (compose->phase == WM_COMPOSE_EDIT &&
        wm_board_keyboard_symbols_visible(compose->keyboard)) return false;
    if (!board_compose_draft_backspace(&compose->draft)) return false;
    wm_board_keyboard_text_changed(compose->keyboard, false);
    compose->keyboard_age = 0.0f;
    compose->scroll.lines = memo_line_count(compose);
    board_compose_scroll_refresh(&compose->scroll, compose->phase);
    compose->scroll.follow_caret_pending = true;
    follow_memo_caret(compose);
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
    return compose && board_compose_scroll_state(
        &compose->scroll, compose->phase, compose->frame, state);
}

WmBoardComposeControl wm_board_compose_hit(WmBoardCompose *compose,
                                           int x, int y) {
    if (compose) compose->draft.pointer_caret_valid = false;
    if (!compose || (compose->phase != WM_COMPOSE_SELECTOR &&
                     compose->phase != WM_COMPOSE_MEMO &&
                     compose->phase != WM_COMPOSE_EDIT &&
                     compose->phase != WM_COMPOSE_ADDRESS)) {
        return WM_COMPOSE_CONTROL_NONE;
    }
    if (compose->network_phase != COMPOSE_NETWORK_CLOSED) {
        if (compose->network_phase != COMPOSE_NETWORK_READY)
            return WM_COMPOSE_CONTROL_NONE;
        if (board_compose_hit_pane(compose->network_dialog, "B_BtnA", x, y))
            return WM_COMPOSE_CONTROL_NETWORK_QUIT;
        if (board_compose_hit_pane(compose->network_dialog, "B_BtnB", x, y))
            return WM_COMPOSE_CONTROL_NETWORK_SETTINGS;
        return WM_COMPOSE_CONTROL_NONE;
    }
    if (compose->phase == WM_COMPOSE_EDIT) {
        bool symbols = wm_board_keyboard_symbols_visible(compose->keyboard);
        if (!symbols) board_compose_pose_body(compose);
        if (!symbols &&
            compose->scroll.arrows[COMPOSE_SCROLL_EDITOR][COMPOSE_SCROLL_UP].visible &&
            board_compose_hit_pane(compose->body, "B_txtScrll_UP", x, y)) {
            return WM_COMPOSE_CONTROL_SCROLL_UP;
        }
        if (!symbols &&
            compose->scroll.arrows[COMPOSE_SCROLL_EDITOR]
                                  [COMPOSE_SCROLL_DOWN].visible &&
            board_compose_hit_pane(compose->body, "B_txtScrll_DOWN", x, y)) {
            return WM_COMPOSE_CONTROL_SCROLL_DOWN;
        }
        WmBoardKeyboardControl key = wm_board_keyboard_hit(compose->keyboard,
                                                            x, y);
        if (key == WM_KEYBOARD_NONE && !symbols &&
            board_compose_hit_memo_caret(compose, x, y)) {
            compose->draft.pointer_caret_valid = true;
            return WM_COMPOSE_CONTROL_EDIT;
        }
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
    board_compose_pose_footer(compose);
    if (board_compose_hit_pane(compose->footer, "B_CalExit", x, y)) {
        return WM_COMPOSE_CONTROL_BACK;
    }
    if (compose->phase == WM_COMPOSE_ADDRESS) {
        if (address_phase == WM_BOARD_ADDRESS_READY) {
            if (board_compose_hit_pane(compose->footer, "B_ArwL", x, y))
                return WM_COMPOSE_CONTROL_ADDRESS_PREVIOUS;
            if (board_compose_hit_pane(compose->footer, "B_ArwR", x, y))
                return WM_COMPOSE_CONTROL_ADDRESS_NEXT;
            if (board_compose_hit_pane(compose->footer, "B_Add_R", x, y))
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
                board_compose_hit_pane(compose->footer, "B_Add_R", x, y))
                return WM_COMPOSE_CONTROL_POST;
            if (wm_board_address_form_hit(compose->address, x, y))
                return WM_COMPOSE_CONTROL_ADDRESS_EDIT;
        } else if (address_phase == WM_BOARD_ADDRESS_MII_READY) {
            if (board_compose_hit_pane(compose->footer, "B_Add_R", x, y))
                return WM_COMPOSE_CONTROL_POST;
            if (wm_board_address_form_hit(compose->address, x, y))
                return WM_COMPOSE_CONTROL_ADDRESS_MII;
        } else if (address_phase == WM_BOARD_ADDRESS_REVIEW_READY) {
            if (board_compose_hit_pane(compose->footer, "B_Add_R", x, y))
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
        board_compose_pose_selector(compose);
        if (board_compose_hit_pane(compose->selector, "B_MailIn", x, y)) {
            return WM_COMPOSE_CONTROL_MEMO;
        }
        if (board_compose_hit_pane(compose->selector, "B_LetterIn", x, y)) {
            return WM_COMPOSE_CONTROL_LETTER;
        }
        if (board_compose_hit_pane(compose->selector, "B_AdressIn", x, y)) {
            return WM_COMPOSE_CONTROL_ADDRESS;
        }
    } else if (compose->phase == WM_COMPOSE_MEMO) {
        if (board_compose_hit_pane(compose->footer, "B_Add_R", x, y)) {
            return WM_COMPOSE_CONTROL_POST;
        }
        board_compose_pose_body(compose);
        if (board_compose_hit_pane(compose->body, "B_Nigaoe", x, y)) {
            return WM_COMPOSE_CONTROL_MII;
        }
        if (compose->scroll.arrows[COMPOSE_SCROLL_DISPLAY][COMPOSE_SCROLL_UP].visible &&
            board_compose_hit_pane(compose->body, "B_ArwR", x, y)) {
            return WM_COMPOSE_CONTROL_SCROLL_UP;
        }
        if (compose->scroll.arrows[COMPOSE_SCROLL_DISPLAY]
                                  [COMPOSE_SCROLL_DOWN].visible &&
            board_compose_hit_pane(compose->body, "B_ArwL", x, y)) {
            return WM_COMPOSE_CONTROL_SCROLL_DOWN;
        }
        if (board_compose_hit_pane(compose->body, "B_2l_TextBox", x, y)) {
            compose->draft.pointer_caret_valid =
                board_compose_hit_memo_caret(compose, x, y);
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
            compose->scroll.lines = memo_line_count(compose);
            board_compose_scroll_refresh(&compose->scroll, compose->phase);
        }
    }
    if (compose->phase == WM_COMPOSE_ADDRESS ||
        wm_board_address_dialog_active(compose->address)) {
        int entry_row = control >= WM_COMPOSE_CONTROL_ADDRESS_ENTRY_FIRST &&
                        control <= WM_COMPOSE_CONTROL_ADDRESS_ENTRY_LAST
            ? (int)(control - WM_COMPOSE_CONTROL_ADDRESS_ENTRY_FIRST) : -1;
        wm_board_address_hover_entry(compose->address, entry_row);
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
    ComposeScrollMode mode = board_compose_scroll_mode(compose->phase);
    if (compose->hover == WM_COMPOSE_CONTROL_SCROLL_UP ||
        compose->hover == WM_COMPOSE_CONTROL_SCROLL_DOWN) {
        size_t direction = compose->hover == WM_COMPOSE_CONTROL_SCROLL_UP
                               ? COMPOSE_SCROLL_UP : COMPOSE_SCROLL_DOWN;
        compose->scroll.arrows[mode][direction].focus =
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
        if (compose->scroll.arrows[mode][direction].visible) {
            compose->scroll.arrows[mode][direction].focus =
                (ComposeFocus){true, true, 0.0f};
        }
    }
}
