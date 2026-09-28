#define _POSIX_C_SOURCE 200809L

#include "board_keyboard_internal.h"
#include "board_keyboard_prediction.h"

#include "wii_menu/render/material_prepare.h"

#include <math.h>
#include <ctype.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

enum {
    KEYBOARD_PATH_CAPACITY = 4096,
    MAX_HOLD_REPEATS_PER_ADVANCE = 4096
};

static void mark_dirty(WmBoardKeyboard *keyboard,
                       WmBoardKeyboardControl control);
static bool profile_allows_control(const WmBoardKeyboard *keyboard,
                                   WmBoardKeyboardControl control);

/* US csSignKeyUS symbol order. */
const char *const
    wm_board_keyboard_symbols[SYMBOL_PAGE_COUNT][SYMBOLS_PER_PAGE] = {
    /* Page 1 */ {
        ".", ",", "‘", ":", ";",
        "„", "“", "”", "'", "\"",
        "?", "!", "(", ")", "_",
        "¿", "¡", "«", "»", "&"
    },
    /* Page 2 */ {
        "[", "]", "{", "}", "·",
        "<", ">", "+", "-", "×",
        "÷", "=", "±", "∞", "%",
        "\\", "/", "|", "§", "@"
    },
    /* Page 3 */ {
        "^", "~", "™", "©", "®",
        "º", "ª", "♭", "♪", "*",
        "←", "→", "↑", "↓", "#",
        "$", "¢", "€", "£", "¥"
    },
    /* Page 4 */ {
        "à", "á", "â", "ä", "å",
        "æ", "ã", "ç", "è", "é",
        "ê", "ë", "ì", "í", "î",
        "ï", "ñ", "ò", "ó", "ô"
    },
    /* Page 5 */ {
        "ö", "œ", "ø", "õ", "ß",
        "ù", "ú", "û", "ü", "ý",
        "ÿ", "À", "Á", "Â", "Ä",
        "Å", "Æ", "Ã", "Ç", "È"
    },
    /* Page 6 */ {
        "É", "Ê", "Ë", "Ì", "Í",
        "Î", "Ï", "Ñ", "Ò", "Ó",
        "Ô", "Ö", "Œ", "Ø", "Õ",
        "Ù", "Ú", "Û", "Ü", "Ý"
    },
    /* Page 7 */ {
        "Ÿ", "α", "β", "γ", "δ",
        "ε", "ζ", "η", "θ", "ι",
        "κ", "λ", "μ", "ν", "ξ",
        "ο", "π", "ρ", "σ", "τ"
    },
    /* Page 8 */ {
        "υ", "φ", "χ", "ψ", "ω",
        "Α", "Β", "Γ", "Δ", "Ε",
        "Ζ", "Η", "Θ", "Ι", "Κ",
        "Λ", "Μ", "Ν", "Ξ", "Ο"
    },
    /* Page 9 */ {
        "Π", "Ρ", "Σ", "Τ", "Υ",
        "Φ", "Χ", "Ψ", "Ω", ";",
        "΄", "΅", "Ά", "·", "Έ",
        "Ή", "Ί", "Ό", "Ύ", "Ώ"
    },
    /* Page 10 */ {
        "ΐ", "Ϊ", "Ϋ", "ά", "έ",
        "ή", "ί", "ΰ", "ς", "ϊ",
        "ϋ", "ό", "ύ", "ώ", "◎",
        "☆", "○", "◇", "□", "△"
    }
};

static const char *const keytop_extras[6] = {
    "[", "]", "'", "`", "/", "@"
};

static const char *const shifted_extras[6] = {
    "{", "}", "\"", "?", "~", "|"
};

static const char qwerty_normal[] =
    "1234567890-qwertyuiopasdfghjkl:zxcvbnm,.=";
static const char qwerty_shifted[] =
    "!\\#$%^&*()_QWERTYUIOPASDFGHJKL;ZXCVBNM<>+";

char wm_board_keyboard_key_character(const WmBoardKeyboard *keyboard, unsigned index) {
    bool shift = wm_board_keyboard_shift_active(keyboard);
    bool caps = wm_board_keyboard_caps_active(keyboard);
    if (index < sizeof(qwerty_normal) - 1) {
        char value = shift ? qwerty_shifted[index] : qwerty_normal[index];
        if (!shift && caps &&
            value >= 'a' && value <= 'z') value = (char)(value - 'a' + 'A');
        return value;
    }
    if (index >= 44 && index < 50) {
        return shift ? shifted_extras[index - 44][0]
                     : keytop_extras[index - 44][0];
    }
    return '\0';
}

static WmLayout *load_layout(const char *directory, const char *name) {
    char path[KEYBOARD_PATH_CAPACITY];
    int length = snprintf(path, sizeof(path),
                          "%s/layouts/sofkeybd/%s.json", directory, name);
    if (length < 0 || length >= (int)sizeof(path)) return NULL;
    char error[160] = {0};
    WmLayout *layout = wm_layout_load_json(path, error, sizeof(error));
    if (!layout) {
        fprintf(stderr, "Could not load software keyboard %s: %s\n",
                name, error);
    }
    return layout;
}

WmBoardKeyboard *wm_board_keyboard_create(WmPlatform *platform,
                                          const char *assets_directory,
                                          WmTextureCache *textures,
                                          WmFontCache *fonts) {
    if (!platform || !assets_directory || !textures || !fonts) return NULL;
    WmBoardKeyboard *keyboard = calloc(1, sizeof(*keyboard));
    if (!keyboard) return NULL;
    keyboard->platform = platform;
    keyboard->textures = textures;
    keyboard->fonts = fonts;
    keyboard->keytop = load_layout(assets_directory,
                                   "fs_VK_ascii_keytop_a");
    keyboard->toolbar = load_layout(assets_directory, "fs_VK_toolbar_a");
    keyboard->prediction = load_layout(assets_directory,
                                       "fs_VK_predictInput_a");
    keyboard->symbols = load_layout(assets_directory, "fs_signWindow_a");
    keyboard->language = load_layout(assets_directory, "fs_prdicSelWidw_a");
    keyboard->phone = load_layout(assets_directory, "fs_VK_cellPhone_a");
    keyboard->background = load_layout(assets_directory, "fs_VK_bg_a");
    keyboard->text_box_small = load_layout(assets_directory,
                                            "fs_VK_textBox_a");
    keyboard->text_box_big = load_layout(assets_directory,
                                          "fs_VK_textBox_b");
    if (!keyboard->keytop || !keyboard->toolbar || !keyboard->prediction ||
        !keyboard->symbols || !keyboard->language || !keyboard->phone ||
        !keyboard->background ||
        !keyboard->text_box_small || !keyboard->text_box_big) {
        wm_board_keyboard_destroy(keyboard);
        return NULL;
    }
    wm_layout_prepare_materials(platform, keyboard->keytop);
    wm_layout_prepare_materials(platform, keyboard->toolbar);
    wm_layout_prepare_materials(platform, keyboard->prediction);
    wm_layout_prepare_materials(platform, keyboard->symbols);
    wm_layout_prepare_materials(platform, keyboard->language);
    wm_layout_prepare_materials(platform, keyboard->phone);
    wm_layout_prepare_materials(platform, keyboard->background);
    wm_layout_prepare_materials(platform, keyboard->text_box_small);
    wm_layout_prepare_materials(platform, keyboard->text_box_big);
    wm_board_keyboard_prediction_load_oem(keyboard, assets_directory);
    wm_board_keyboard_reset(keyboard);
    return keyboard;
}

void wm_board_keyboard_destroy(WmBoardKeyboard *keyboard) {
    if (!keyboard) return;
    wm_layout_destroy(keyboard->keytop);
    wm_layout_destroy(keyboard->toolbar);
    wm_layout_destroy(keyboard->prediction);
    wm_layout_destroy(keyboard->symbols);
    wm_layout_destroy(keyboard->language);
    wm_layout_destroy(keyboard->phone);
    wm_layout_destroy(keyboard->background);
    wm_layout_destroy(keyboard->text_box_small);
    wm_layout_destroy(keyboard->text_box_big);
    wm_board_keyboard_prediction_release(keyboard);
    free(keyboard);
}

void wm_board_keyboard_reset(WmBoardKeyboard *keyboard) {
    if (!keyboard) return;
    wm_board_keyboard_prediction_clear_learned(keyboard);
    keyboard->profile = WM_BOARD_KEYBOARD_MEMO;
    keyboard->caret_visible = false;
    keyboard->caret_age = 0.0f;
    keyboard->phone_layout = keyboard->memo_phone_layout;
    keyboard->phone_mode = keyboard->memo_phone_mode;
    memset(keyboard->focus, 0, sizeof(keyboard->focus));
    keyboard->hovered = WM_KEYBOARD_NONE;
    keyboard->pressed = WM_KEYBOARD_NONE;
    keyboard->pressed_phone_hover = WM_KEYBOARD_NONE;
    keyboard->press_frame = 20.0f;
    keyboard->caps = false;
    keyboard->shift = false;
    keyboard->physical_caps = false;
    keyboard->physical_shift = false;
    keyboard->physical_caps_focus_frame = 0.0f;
    keyboard->physical_shift_focus_frame = 0.0f;
    keyboard->physical_caps_press_remaining = 0.0f;
    keyboard->keytop_dirty = true;
    keyboard->toolbar_dirty = true;
    keyboard->prediction_dirty = true;
    keyboard->symbols_dirty = true;
    keyboard->language_dirty = true;
    keyboard->phone_dirty = true;
    keyboard->symbol_phase = SYMBOL_CLOSED;
    keyboard->symbol_frame = 0.0f;
    keyboard->symbol_page = 0;
    keyboard->symbol_target_page = 0;
    keyboard->phone_pending = false;
    keyboard->phone_just_committed = false;
    keyboard->phone_pending_frames = 0.0f;
    keyboard->language_phase = LANGUAGE_CLOSED;
    keyboard->language_frame = 0.0f;
    keyboard->candidate_count = 0;
    keyboard->candidate_prefix_bytes = 0;
    keyboard->accepted_prefix_bytes = 0;
    keyboard->accepted_candidate[0] = '\0';
    keyboard->phone_prediction_digits[0] = '\0';
    keyboard->phone_prediction_bytes = 0;
    keyboard->phone_prediction_awaiting_edit = false;
    keyboard->prediction_animating = false;
    keyboard->prediction_frame = 0.0f;
    keyboard->composition_start = SIZE_MAX;
    keyboard->observed_text_bytes = keyboard->text_context
        ? strlen(keyboard->text_context) : 0;
    wm_board_keyboard_prediction_refresh(keyboard);
    wm_board_keyboard_release_hold(keyboard);
}

void wm_board_keyboard_set_physical_modifiers(WmBoardKeyboard *keyboard,
                                               bool shift_down,
                                               bool caps_lock_on) {
    if (!keyboard || (keyboard->physical_shift == shift_down &&
                      keyboard->physical_caps == caps_lock_on)) return;
    bool shift_changed = keyboard->physical_shift != shift_down;
    bool caps_changed = keyboard->physical_caps != caps_lock_on;
    keyboard->physical_shift = shift_down;
    keyboard->physical_caps = caps_lock_on;
    /* A physical modifier can press a keytop, but it cannot hover it. Only
     * pointer movement owns focus; otherwise Caps remains stuck highlighted. */
    if (shift_changed && shift_down) {
        keyboard->pressed = WM_KEYBOARD_SHIFT;
        keyboard->press_frame = 0.0f;
    } else if (shift_changed && keyboard->pressed == WM_KEYBOARD_SHIFT) {
        keyboard->pressed = WM_KEYBOARD_NONE;
    }
    if (caps_changed) {
        keyboard->pressed = WM_KEYBOARD_CAPS;
        keyboard->press_frame = 0.0f;
        keyboard->physical_caps_press_remaining = 20.0f;
    }
    keyboard->keytop_dirty = true;
}

void wm_board_keyboard_set_profile(WmBoardKeyboard *keyboard,
                                    WmBoardKeyboardProfile profile) {
    if (!keyboard) return;
    if (keyboard->profile == WM_BOARD_KEYBOARD_MEMO) {
        keyboard->memo_phone_layout = keyboard->phone_layout;
        keyboard->memo_phone_mode = keyboard->phone_mode;
    }
    wm_board_keyboard_reset(keyboard);
    keyboard->profile = profile;
    if (profile == WM_BOARD_KEYBOARD_ADDRESS_WII) {
        keyboard->phone_layout = true;
        keyboard->phone_mode = 3;
    } else if (profile == WM_BOARD_KEYBOARD_ADDRESS_EMAIL) {
        keyboard->phone_layout = false;
    }
    keyboard->keytop_dirty = true;
    keyboard->phone_dirty = true;
    keyboard->toolbar_dirty = true;
    wm_board_keyboard_prediction_refresh(keyboard);
}

WmBoardKeyboardProfile wm_board_keyboard_profile(
    const WmBoardKeyboard *keyboard) {
    return keyboard ? keyboard->profile : WM_BOARD_KEYBOARD_MEMO;
}

void wm_board_keyboard_set_caret(WmBoardKeyboard *keyboard,
                                size_t byte_index, bool visible) {
    if (!keyboard) return;
    size_t length = keyboard->text_context
        ? strlen(keyboard->text_context) : 0;
    keyboard->caret_bytes = byte_index < length ? byte_index : length;
    keyboard->caret_visible = visible;
}

bool wm_board_keyboard_phone_space_pending(const WmBoardKeyboard *keyboard) {
    return keyboard && keyboard->phone_layout && keyboard->phone_pending &&
           keyboard->phone_pending_index == 10 &&
           keyboard->phone_cycle_position == 0;
}

bool wm_board_keyboard_phone_pending(const WmBoardKeyboard *keyboard) {
    return keyboard && keyboard->phone_layout && keyboard->phone_pending;
}

size_t wm_board_keyboard_phone_pending_bytes(
    const WmBoardKeyboard *keyboard) {
    return wm_board_keyboard_phone_pending(keyboard) ? 1u : 0u;
}

bool wm_board_keyboard_take_phone_commit(WmBoardKeyboard *keyboard) {
    if (!keyboard) return false;
    bool committed = keyboard->phone_just_committed;
    keyboard->phone_just_committed = false;
    return committed;
}

bool wm_board_keyboard_phone_mode(const WmBoardKeyboard *keyboard) {
    return keyboard && keyboard->phone_layout;
}

bool wm_board_keyboard_prediction_enabled(const WmBoardKeyboard *keyboard) {
    return keyboard && keyboard->prediction_enabled;
}

WmBoardKeyboardControl wm_board_keyboard_press_physical(
    WmBoardKeyboard *keyboard, const char *utf8) {
    if (!keyboard || !utf8 || !utf8[0] || utf8[1] ||
        keyboard->symbol_phase != SYMBOL_CLOSED ||
        keyboard->language_phase != LANGUAGE_CLOSED) return WM_KEYBOARD_NONE;

    unsigned char value = (unsigned char)utf8[0];
    WmBoardKeyboardControl control = WM_KEYBOARD_NONE;
    if (value == '\b' || value == 0x7f) {
        control = WM_KEYBOARD_DELETE;
    } else if (value == '\n' || value == '\r') {
        control = WM_KEYBOARD_RETURN;
    } else if (keyboard->phone_layout) {
        if (keyboard->phone_mode == 3) {
            for (unsigned index = 0; index < 12; index++) {
                char label_buffer[16];
                const char *label = wm_board_keyboard_phone_label(keyboard, index, label_buffer);
                if (label[0] == value && label[1] == '\0') {
                    control = (WmBoardKeyboardControl)(
                        WM_KEYBOARD_PHONE_FIRST + index);
                    break;
                }
            }
        } else {
            unsigned char lower = (unsigned char)tolower(value);
            for (unsigned index = 0; index < 12; index++) {
                if (strchr(wm_board_keyboard_prediction_phone_cycle(index), lower)) {
                    control = (WmBoardKeyboardControl)(
                        WM_KEYBOARD_PHONE_FIRST + index);
                    break;
                }
            }
        }
    } else if (value == ' ') {
        control = WM_KEYBOARD_SPACE;
    } else {
        for (unsigned index = 0; index < 50; index++) {
            char normal = '\0';
            char shifted = '\0';
            if (index < sizeof(qwerty_normal) - 1)
                normal = qwerty_normal[index];
            else if (index >= 44)
                normal = keytop_extras[index - 44][0];
            if (index < sizeof(qwerty_shifted) - 1)
                shifted = qwerty_shifted[index];
            else if (index >= 44)
                shifted = shifted_extras[index - 44][0];
            if (normal == value || shifted == value ||
                (isalpha(value) && normal == tolower(value))) {
                control = (WmBoardKeyboardControl)(
                    WM_KEYBOARD_CHARACTER_FIRST + index);
                break;
            }
        }
    }
    if (control == WM_KEYBOARD_NONE ||
        !profile_allows_control(keyboard, control)) return WM_KEYBOARD_NONE;
    keyboard->pressed = control;
    keyboard->press_frame = 0.0f;
    mark_dirty(keyboard, control);
    return control;
}

void wm_board_keyboard_release_hold(WmBoardKeyboard *keyboard) {
    if (!keyboard) return;
    keyboard->held = WM_KEYBOARD_NONE;
    keyboard->held_updates = 0;
    keyboard->held_fraction = 0.0;
}

WmBoardKeyboardControl wm_board_keyboard_held_control(
    const WmBoardKeyboard *keyboard) {
    return keyboard ? keyboard->held : WM_KEYBOARD_NONE;
}

bool wm_board_keyboard_begin_hold(WmBoardKeyboard *keyboard,
                                  WmBoardKeyboardControl control) {
    if (!keyboard || keyboard->symbol_phase != SYMBOL_CLOSED ||
        (control != WM_KEYBOARD_DELETE && control != WM_KEYBOARD_SPACE &&
         control != WM_KEYBOARD_CANDIDATE_PREVIOUS &&
         control != WM_KEYBOARD_CANDIDATE_NEXT) ||
        (keyboard->phone_layout && control == WM_KEYBOARD_SPACE)) {
        return false;
    }
    wm_board_keyboard_release_hold(keyboard);
    keyboard->held = control;
    return true;
}

static unsigned count_repeat_values(unsigned first, unsigned last,
                                    bool candidate_arrow) {
    if (last < first) return 0;
    if (candidate_arrow) {
        unsigned multiples = last / 20 + 1;
        if (first > 0) multiples -= (first - 1) / 20 + 1;
        return last - first + 1 - multiples;
    }
    if (first < 36) first = 36;
    if (last < first) return 0;
    return last / 9 - (first - 1) / 9;
}

static unsigned held_repeats(WmBoardKeyboard *keyboard, float frames) {
    if (keyboard->held == WM_KEYBOARD_NONE) return 0;
    double whole;
    keyboard->held_fraction = modf(keyboard->held_fraction + (double)frames,
                                  &whole);
    if (whole < 1.0) return 0;
    /* The source's 16-bit hover counter wraps. Count full cycles and at most
     * two partial ranges instead of stepping through every elapsed update. */
    uint64_t updates = whole >= (double)UINT64_MAX
                           ? UINT64_MAX : (uint64_t)whole;
    bool candidate_arrow = keyboard->held == WM_KEYBOARD_CANDIDATE_PREVIOUS ||
                           keyboard->held == WM_KEYBOARD_CANDIDATE_NEXT;
    uint64_t repeats = (updates / 65536u) *
                       count_repeat_values(0, UINT16_MAX, candidate_arrow);
    unsigned remainder = (unsigned)(updates % 65536u);
    if (remainder > 0) {
        unsigned start = (unsigned)keyboard->held_updates + 1u;
        unsigned until_wrap = 65536u - start;
        if (remainder <= until_wrap) {
            repeats += count_repeat_values(start, start + remainder - 1u,
                                           candidate_arrow);
        } else {
            repeats += count_repeat_values(start, UINT16_MAX,
                                           candidate_arrow);
            repeats += count_repeat_values(0, remainder - until_wrap - 1u,
                                           candidate_arrow);
        }
    }
    keyboard->held_updates = (uint16_t)(keyboard->held_updates + updates);
    if (repeats > MAX_HOLD_REPEATS_PER_ADVANCE)
        return MAX_HOLD_REPEATS_PER_ADVANCE;
    return (unsigned)repeats;
}

static void mark_dirty(WmBoardKeyboard *keyboard,
                       WmBoardKeyboardControl control) {
    if (is_toolbar(control)) keyboard->toolbar_dirty = true;
    else if (is_symbol(control)) keyboard->symbols_dirty = true;
    else if (is_language_choice(control)) keyboard->language_dirty = true;
    else if (is_prediction_control(control)) keyboard->prediction_dirty = true;
    else if (is_phone_control(keyboard, control)) keyboard->phone_dirty = true;
    else if (is_keytop(control)) keyboard->keytop_dirty = true;
}

unsigned wm_board_keyboard_advance(WmBoardKeyboard *keyboard, float frames) {
    if (!keyboard || !isfinite(frames) || frames <= 0.0f) return 0;
    if (keyboard->caret_visible) keyboard->caret_age += frames;
    /* Physical modifiers drive their own focus clips. Pointer focus may move
     * independently while a key is held, and release must ease back to idle. */
    float caps_target = keyboard->physical_caps_press_remaining > 0.0f
        ? 5.0f : 0.0f;
    float shift_target = keyboard->physical_shift ? 5.0f : 0.0f;
    float caps_previous = keyboard->physical_caps_focus_frame;
    float shift_previous = keyboard->physical_shift_focus_frame;
    keyboard->physical_caps_focus_frame = caps_previous < caps_target
        ? fminf(caps_previous + frames, caps_target)
        : fmaxf(caps_previous - frames, caps_target);
    keyboard->physical_shift_focus_frame = shift_previous < shift_target
        ? fminf(shift_previous + frames, shift_target)
        : fmaxf(shift_previous - frames, shift_target);
    if (keyboard->physical_caps_focus_frame != caps_previous ||
        keyboard->physical_shift_focus_frame != shift_previous)
        keyboard->keytop_dirty = true;
    keyboard->physical_caps_press_remaining = fmaxf(
        0.0f, keyboard->physical_caps_press_remaining - frames);
    if (keyboard->phone_pending &&
        keyboard->hovered == (WmBoardKeyboardControl)(
            WM_KEYBOARD_PHONE_FIRST + keyboard->phone_pending_index)) {
        keyboard->phone_pending_frames += frames;
        if (keyboard->phone_pending_frames >= 90.0f)
            wm_board_keyboard_clear_phone_pending(keyboard);
    }
    for (unsigned index = 1; index <= WM_KEYBOARD_CONTROL_LAST; index++) {
        KeyboardFocus *focus = &keyboard->focus[index];
        if (!focus->active) continue;
        if (focus->resting) continue;
        WmBoardKeyboardControl control = (WmBoardKeyboardControl)index;
        float limit = focus->entering
            ? (is_symbol(control) || is_language_choice(control) ||
               is_prediction_control(control) ? 6.0f : 5.0f)
            : (is_prediction_control(control) ? 7.0f : 8.0f);
        if (focus->frame < limit) {
            focus->frame = fminf(focus->frame + frames, limit);
            mark_dirty(keyboard, control);
        }
        if (focus->entering && focus->frame >= limit) {
            focus->entering = false;
            focus->resting = true;
            focus->frame = 0.0f;
            mark_dirty(keyboard, control);
        } else if (!focus->entering && focus->frame >= limit) {
            focus->active = false;
            mark_dirty(keyboard, control);
        }
    }
    if (keyboard->pressed != WM_KEYBOARD_NONE) {
        float limit = is_symbol(keyboard->pressed) ? 7.0f : 20.0f;
        keyboard->press_frame = fminf(limit,
                                     keyboard->press_frame + frames);
        mark_dirty(keyboard, keyboard->pressed);
        if (keyboard->press_frame >= limit) {
            keyboard->pressed = WM_KEYBOARD_NONE;
        }
    }
    if (keyboard->symbol_phase != SYMBOL_CLOSED &&
        keyboard->symbol_phase != SYMBOL_OPEN) {
        float limit = keyboard->symbol_phase == SYMBOL_ENTERING ? 18.0f :
                      keyboard->symbol_phase == SYMBOL_LEAVING ? 13.0f : 20.0f;
        keyboard->symbol_frame = fminf(limit,
                                       keyboard->symbol_frame + frames);
        keyboard->symbols_dirty = true;
        if (keyboard->symbol_frame >= limit) {
            if (keyboard->symbol_phase == SYMBOL_LEAVING) {
                keyboard->symbol_phase = SYMBOL_CLOSED;
            } else {
                if (keyboard->symbol_phase == SYMBOL_SCROLL_PREV ||
                    keyboard->symbol_phase == SYMBOL_SCROLL_NEXT) {
                    keyboard->symbol_page = keyboard->symbol_target_page;
                }
                keyboard->symbol_phase = SYMBOL_OPEN;
            }
        }
    }
    if (keyboard->language_phase == LANGUAGE_ENTERING ||
        keyboard->language_phase == LANGUAGE_LEAVING) {
        float limit = keyboard->language_phase == LANGUAGE_ENTERING
                          ? 18.0f : 13.0f;
        keyboard->language_frame = fminf(limit,
                                          keyboard->language_frame + frames);
        keyboard->language_dirty = true;
        if (keyboard->language_frame >= limit)
            keyboard->language_phase = keyboard->language_phase ==
                LANGUAGE_ENTERING ? LANGUAGE_OPEN : LANGUAGE_CLOSED;
    }
    if (keyboard->prediction_animating) {
        keyboard->prediction_frame = fminf(12.0f,
                                            keyboard->prediction_frame + frames);
        keyboard->prediction_dirty = true;
        if (keyboard->prediction_frame >= 12.0f) {
            keyboard->prediction_animating = false;
            keyboard->keytop_dirty = true;
            keyboard->phone_dirty = true;
        }
    }
    if (keyboard->candidate_scrolling) {
        keyboard->candidate_scroll_frame = fminf(16.0f,
            keyboard->candidate_scroll_frame + frames);
        keyboard->prediction_dirty = true;
        if (keyboard->candidate_scroll_frame >= 16.0f) {
            keyboard->candidate_first = keyboard->candidate_target;
            keyboard->candidate_scrolling = false;
            bool previous_available = keyboard->candidate_first > 0;
            bool next_available = wm_board_keyboard_candidate_next_index(keyboard) >
                                  keyboard->candidate_first;
            WmBoardKeyboardControl unavailable = WM_KEYBOARD_NONE;
            if (!previous_available)
                unavailable = WM_KEYBOARD_CANDIDATE_PREVIOUS;
            if (!next_available && (keyboard->hovered ==
                WM_KEYBOARD_CANDIDATE_NEXT || keyboard->held ==
                WM_KEYBOARD_CANDIDATE_NEXT))
                unavailable = WM_KEYBOARD_CANDIDATE_NEXT;
            if (keyboard->held == unavailable)
                wm_board_keyboard_release_hold(keyboard);
            if (keyboard->hovered == unavailable)
                wm_board_keyboard_hover(keyboard, WM_KEYBOARD_NONE);
        }
    }
    return held_repeats(keyboard, frames);
}

static bool profile_allows_control(const WmBoardKeyboard *keyboard,
                                    WmBoardKeyboardControl control) {
    if (keyboard->profile == WM_BOARD_KEYBOARD_MEMO) return true;
    if (control == WM_KEYBOARD_BACK || control == WM_KEYBOARD_OK ||
        control == WM_KEYBOARD_DELETE) return true;
    if (keyboard->profile == WM_BOARD_KEYBOARD_ADDRESS_WII)
        return control >= WM_KEYBOARD_PHONE_FIRST &&
               control <= WM_KEYBOARD_PHONE_LAST;
    if (keyboard->profile == WM_BOARD_KEYBOARD_ADDRESS_NICKNAME)
        return control != WM_KEYBOARD_RETURN;
    if (keyboard->profile == WM_BOARD_KEYBOARD_CONSOLE_NICKNAME) {
        if (control == WM_KEYBOARD_RETURN ||
            control == WM_KEYBOARD_LANGUAGE ||
            control == WM_KEYBOARD_PREDICTION ||
            (control >= WM_KEYBOARD_MORE &&
             control <= WM_KEYBOARD_SYMBOL_NEXT) ||
            is_language_choice(control) ||
            (control >= WM_KEYBOARD_CANDIDATE_FIRST &&
             control <= WM_KEYBOARD_CANDIDATE_NEXT))
            return false;
        return true;
    }
    return (control >= WM_KEYBOARD_CHARACTER_FIRST &&
            control <= WM_KEYBOARD_CHARACTER_LAST) ||
           control == WM_KEYBOARD_CAPS || control == WM_KEYBOARD_SHIFT ||
           control == WM_KEYBOARD_SPACE;
}

WmBoardKeyboardControl wm_board_keyboard_hit(WmBoardKeyboard *keyboard,
                                             int x, int y) {
    WmBoardKeyboardControl control = wm_board_keyboard_hit_unfiltered(keyboard, x, y);
    return keyboard && profile_allows_control(keyboard, control)
        ? control : WM_KEYBOARD_NONE;
}

bool wm_board_keyboard_symbols_visible(const WmBoardKeyboard *keyboard) {
    return keyboard && (keyboard->symbol_phase != SYMBOL_CLOSED ||
                        keyboard->language_phase != LANGUAGE_CLOSED);
}

bool wm_board_keyboard_back(WmBoardKeyboard *keyboard) {
    if (!keyboard) return false;
    if (keyboard->language_phase == LANGUAGE_OPEN) {
        keyboard->language_phase = LANGUAGE_LEAVING;
        keyboard->language_frame = 0.0f;
        keyboard->language_dirty = true;
        wm_board_keyboard_hover(keyboard, WM_KEYBOARD_NONE);
        return true;
    }
    if (keyboard->symbol_phase != SYMBOL_OPEN) return false;
    keyboard->symbol_phase = SYMBOL_LEAVING;
    keyboard->symbol_frame = 0.0f;
    keyboard->symbols_dirty = true;
    wm_board_keyboard_hover(keyboard, WM_KEYBOARD_NONE);
    return true;
}

void wm_board_keyboard_hover(WmBoardKeyboard *keyboard,
                             WmBoardKeyboardControl control) {
    if (!keyboard || keyboard->hovered == control) return;
    wm_board_keyboard_prediction_rollback_phone(keyboard);
    bool candidate = control >= WM_KEYBOARD_CANDIDATE_FIRST &&
                     control <= WM_KEYBOARD_CANDIDATE_LAST;
    if (candidate && !keyboard->candidate_scrolling) {
        wm_board_keyboard_pose_prediction(keyboard);
        unsigned index = keyboard->candidate_pane_indices[
            control - WM_KEYBOARD_CANDIDATE_FIRST];
        if (index < keyboard->candidate_count)
            keyboard->selected_candidate_index = index;
    }
    bool leaving_phone = keyboard->hovered >= WM_KEYBOARD_PHONE_FIRST &&
                         keyboard->hovered <= WM_KEYBOARD_PHONE_LAST &&
                         control != keyboard->hovered && !candidate;
    if (leaving_phone &&
        keyboard->pressed_phone_hover == keyboard->hovered &&
        (keyboard->phone_pending || keyboard->phone_prediction_digits[0])) {
        bool predicted = keyboard->phone_prediction_digits[0] != '\0';
        keyboard->phone_just_committed = true;
        wm_board_keyboard_clear_phone_pending(keyboard);
        if (predicted) wm_board_keyboard_finish_composition(keyboard);
    }
    keyboard->pressed_phone_hover = WM_KEYBOARD_NONE;
    if (keyboard->held != WM_KEYBOARD_NONE && keyboard->held != control)
        wm_board_keyboard_release_hold(keyboard);
    if (keyboard->hovered != WM_KEYBOARD_NONE) {
        if (!selected_tab(keyboard, keyboard->hovered)) {
            keyboard->focus[keyboard->hovered] =
                (KeyboardFocus){.active = true, .frame = 0.0f};
            mark_dirty(keyboard, keyboard->hovered);
        }
    }
    keyboard->hovered = control;
    if (control != WM_KEYBOARD_NONE &&
        control <= WM_KEYBOARD_CONTROL_LAST &&
        !selected_tab(keyboard, control)) {
        keyboard->focus[control] = (KeyboardFocus){
            .active = true, .entering = true, .frame = 0.0f
        };
        mark_dirty(keyboard, control);
    }
}

WmBoardKeyboardAction wm_board_keyboard_activate(
    WmBoardKeyboard *keyboard, WmBoardKeyboardControl control,
    bool reverse, char utf8[5]) {
    if (!keyboard || !utf8 || control <= WM_KEYBOARD_NONE ||
        control > WM_KEYBOARD_CONTROL_LAST) return WM_KEYBOARD_ACTION_NONE;
    if (!profile_allows_control(keyboard, control))
        return WM_KEYBOARD_ACTION_NONE;
    wm_board_keyboard_prediction_rollback_phone(keyboard);
    memset(utf8, 0, 5);
    if (keyboard->language_phase != LANGUAGE_CLOSED) {
        if (keyboard->language_phase != LANGUAGE_OPEN ||
            !is_language_choice(control)) return WM_KEYBOARD_ACTION_NONE;
        wm_board_keyboard_finish_composition(keyboard);
        keyboard->dictionary_language = (unsigned)(control -
            WM_KEYBOARD_LANGUAGE_ENGLISH);
        keyboard->pressed = control;
        keyboard->press_frame = 0.0f;
        keyboard->language_phase = LANGUAGE_LEAVING;
        keyboard->language_frame = 0.0f;
        keyboard->language_dirty = true;
        keyboard->keytop_dirty = true;
        keyboard->phone_dirty = true;
        wm_board_keyboard_prediction_refresh(keyboard);
        wm_board_keyboard_hover(keyboard, WM_KEYBOARD_NONE);
        return WM_KEYBOARD_ACTION_DICTIONARY_LANGUAGE;
    }
    if (keyboard->symbol_phase != SYMBOL_CLOSED) {
        if (keyboard->symbol_phase != SYMBOL_OPEN || !is_symbol(control)) {
            return WM_KEYBOARD_ACTION_NONE;
        }
        keyboard->pressed = control;
        keyboard->press_frame = 0.0f;
        keyboard->symbols_dirty = true;
        if (control >= WM_KEYBOARD_SYMBOL_FIRST &&
            control <= WM_KEYBOARD_SYMBOL_LAST) {
            snprintf(utf8, 5, "%s",
                     wm_board_keyboard_symbols[keyboard->symbol_page]
                                [control - WM_KEYBOARD_SYMBOL_FIRST]);
            return WM_KEYBOARD_ACTION_INSERT;
        }
        if (control == WM_KEYBOARD_SYMBOL_CLOSE) {
            keyboard->symbol_phase = SYMBOL_LEAVING;
            keyboard->symbol_frame = 0.0f;
            wm_board_keyboard_hover(keyboard, WM_KEYBOARD_NONE);
            return WM_KEYBOARD_ACTION_SYMBOL_CLOSE;
        }
        keyboard->symbol_target_page = control == WM_KEYBOARD_SYMBOL_PREV
            ? (keyboard->symbol_page + SYMBOL_PAGE_COUNT - 1) %
                SYMBOL_PAGE_COUNT
            : (keyboard->symbol_page + 1) % SYMBOL_PAGE_COUNT;
        keyboard->symbol_phase = control == WM_KEYBOARD_SYMBOL_PREV
            ? SYMBOL_SCROLL_PREV : SYMBOL_SCROLL_NEXT;
        keyboard->symbol_frame = 0.0f;
        return WM_KEYBOARD_ACTION_SYMBOL_PAGE;
    }
    bool phone_key = control >= WM_KEYBOARD_PHONE_FIRST &&
                     control <= WM_KEYBOARD_PHONE_LAST;
    bool phone_tab = control >= WM_KEYBOARD_PHONE_MODE_FIRST &&
                     control <= WM_KEYBOARD_PHONE_MODE_LAST;
    bool qwerty_key = control >= WM_KEYBOARD_CHARACTER_FIRST &&
                      control <= WM_KEYBOARD_CHARACTER_LAST;
    if ((keyboard->phone_layout && (qwerty_key ||
         control == WM_KEYBOARD_CAPS || control == WM_KEYBOARD_SHIFT ||
         control == WM_KEYBOARD_SPACE ||
         (keyboard->phone_mode == 3 && control == WM_KEYBOARD_MORE))) ||
        (!keyboard->phone_layout && (phone_key || phone_tab)) ||
        (reverse && !phone_key)) return WM_KEYBOARD_ACTION_NONE;
    if (control == WM_KEYBOARD_LANGUAGE && keyboard->phone_layout &&
        keyboard->phone_mode == 3) return WM_KEYBOARD_ACTION_NONE;
    if (is_prediction_control(control) && keyboard->candidate_scrolling)
        return WM_KEYBOARD_ACTION_NONE;
    if (control == WM_KEYBOARD_CANDIDATE_PREVIOUS ||
        control == WM_KEYBOARD_CANDIDATE_NEXT) {
        unsigned target = control == WM_KEYBOARD_CANDIDATE_PREVIOUS
            ? wm_board_keyboard_candidate_previous_index(keyboard) :
              wm_board_keyboard_candidate_next_index(keyboard);
        if (target == keyboard->candidate_first) return WM_KEYBOARD_ACTION_NONE;
        keyboard->candidate_target = target;
        keyboard->candidate_scroll_frame = 0.0f;
        keyboard->candidate_scrolling = true;
        keyboard->prediction_dirty = true;
        keyboard->pressed = control;
        keyboard->press_frame = 0.0f;
        return WM_KEYBOARD_ACTION_CANDIDATE_PAGE;
    }
    if (control >= WM_KEYBOARD_CANDIDATE_FIRST &&
        control <= WM_KEYBOARD_CANDIDATE_LAST) {
        wm_board_keyboard_pose_prediction(keyboard);
        unsigned slot = control - WM_KEYBOARD_CANDIDATE_FIRST;
        unsigned index = keyboard->candidate_pane_indices[slot];
        if (index >= keyboard->candidate_count) return WM_KEYBOARD_ACTION_NONE;
        snprintf(keyboard->accepted_candidate,
                 sizeof(keyboard->accepted_candidate), "%s",
                 keyboard->candidates[index]);
        keyboard->accepted_prefix_bytes = keyboard->phone_prediction_digits[0]
            ? keyboard->phone_prediction_bytes :
              keyboard->candidate_prefix_bytes;
        keyboard->pressed = control;
        keyboard->press_frame = 0.0f;
        keyboard->prediction_dirty = true;
        return WM_KEYBOARD_ACTION_ACCEPT_CANDIDATE;
    }
    if (!phone_key) {
        wm_board_keyboard_clear_phone_pending(keyboard);
        keyboard->phone_prediction_digits[0] = '\0';
        keyboard->phone_prediction_bytes = 0;
    }
    keyboard->pressed = control;
    keyboard->press_frame = 0.0f;
    mark_dirty(keyboard, control);
    if (control == WM_KEYBOARD_QWERTY || control == WM_KEYBOARD_PHONE) {
        bool next_phone = control == WM_KEYBOARD_PHONE;
        if (next_phone == keyboard->phone_layout) {
            keyboard->pressed = WM_KEYBOARD_NONE;
            return WM_KEYBOARD_ACTION_NONE;
        }
        keyboard->phone_layout = next_phone;
        if (keyboard->profile == WM_BOARD_KEYBOARD_MEMO)
            keyboard->memo_phone_layout = next_phone;
        keyboard->toolbar_dirty = true;
        keyboard->keytop_dirty = true;
        keyboard->phone_dirty = true;
        wm_board_keyboard_hover(keyboard, WM_KEYBOARD_NONE);
        /* Both layout tabs share one toggle animation. An old hover on the
         * formerly selected tab must not return when it becomes unselected. */
        keyboard->focus[WM_KEYBOARD_QWERTY] = (KeyboardFocus){0};
        keyboard->focus[WM_KEYBOARD_PHONE] = (KeyboardFocus){0};
        keyboard->toolbar_dirty = true;
        return next_phone ? WM_KEYBOARD_ACTION_LAYOUT_PHONE :
                            WM_KEYBOARD_ACTION_LAYOUT_QWERTY;
    }
    if (phone_tab) {
        keyboard->phone_mode = (unsigned)(control -
                                          WM_KEYBOARD_PHONE_MODE_FIRST);
        if (keyboard->profile == WM_BOARD_KEYBOARD_MEMO)
            keyboard->memo_phone_mode = keyboard->phone_mode;
        keyboard->focus[control].active = false;
        keyboard->phone_dirty = true;
        return WM_KEYBOARD_ACTION_PHONE_MODE;
    }
    if (phone_key) {
        unsigned index = (unsigned)(control - WM_KEYBOARD_PHONE_FIRST);
        keyboard->pressed_phone_hover = keyboard->hovered == control
            ? control : WM_KEYBOARD_NONE;
        if (keyboard->phone_mode == 3) {
            char label_buffer[16];
            const char *label = wm_board_keyboard_phone_label(keyboard, index, label_buffer);
            if (label[0] == '\0') return WM_KEYBOARD_ACTION_NONE;
            utf8[0] = label[0];
            return WM_KEYBOARD_ACTION_INSERT;
        }
        if (keyboard->prediction_enabled && index >= 1 && index <= 8 &&
            keyboard->profile == WM_BOARD_KEYBOARD_MEMO) {
            size_t digits = strlen(keyboard->phone_prediction_digits);
            if (digits >= 32) return WM_KEYBOARD_ACTION_PHONE_BOUNDARY;
            memcpy(keyboard->phone_prediction_previous_digits,
                   keyboard->phone_prediction_digits,
                   sizeof(keyboard->phone_prediction_digits));
            keyboard->phone_prediction_previous_bytes =
                keyboard->phone_prediction_bytes;
            if (digits == 0)
                keyboard->phone_prediction_uppercase = wm_board_keyboard_prediction_phone_uppercase(keyboard);
            keyboard->phone_prediction_digits[digits] = (char)('1' + index);
            keyboard->phone_prediction_digits[digits + 1] = '\0';
            keyboard->accepted_prefix_bytes = keyboard->phone_prediction_bytes;
            wm_board_keyboard_prediction_phone_value(keyboard, keyboard->accepted_candidate);
            keyboard->phone_prediction_bytes =
                strlen(keyboard->accepted_candidate);
            keyboard->phone_prediction_awaiting_edit = true;
            keyboard->prediction_dirty = true;
            return WM_KEYBOARD_ACTION_PREDICT_PHONE;
        }
        keyboard->phone_prediction_digits[0] = '\0';
        keyboard->phone_prediction_bytes = 0;
        const char *cycle = wm_board_keyboard_prediction_phone_cycle(index);
        size_t length = strlen(cycle);
        if (length == 0) return WM_KEYBOARD_ACTION_NONE;
        bool continuing = keyboard->phone_pending &&
                          keyboard->phone_pending_index == index;
        if (continuing) {
            keyboard->phone_cycle_position =
                (keyboard->phone_cycle_position + length +
                 (reverse ? length - 1 : 1)) % length;
        } else {
            keyboard->phone_cycle_position = reverse ? length - 1 : 0;
            keyboard->phone_pending_uppercase = wm_board_keyboard_prediction_phone_uppercase(keyboard);
        }
        char value = cycle[keyboard->phone_cycle_position];
        if (keyboard->phone_pending_uppercase &&
            value >= 'a' && value <= 'z') value = (char)(value - 'a' + 'A');
        utf8[0] = value;
        keyboard->phone_pending = true;
        keyboard->phone_pending_index = index;
        keyboard->phone_pending_frames = 0.0f;
        keyboard->phone_dirty = true;
        return continuing ? WM_KEYBOARD_ACTION_REPLACE_LAST :
                            WM_KEYBOARD_ACTION_INSERT;
    }
    if (qwerty_key) {
        utf8[0] = wm_board_keyboard_key_character(keyboard,
                                 (unsigned)control -
                                 WM_KEYBOARD_CHARACTER_FIRST);
        keyboard->shift = false;
        keyboard->keytop_dirty = true;
        return utf8[0] ? WM_KEYBOARD_ACTION_INSERT :
                         WM_KEYBOARD_ACTION_NONE;
    }
    switch (control) {
        case WM_KEYBOARD_LANGUAGE:
            keyboard->language_phase = LANGUAGE_ENTERING;
            keyboard->language_frame = 0.0f;
            keyboard->language_dirty = true;
            wm_board_keyboard_hover(keyboard, WM_KEYBOARD_NONE);
            return WM_KEYBOARD_ACTION_DICTIONARY_OPEN;
        case WM_KEYBOARD_PREDICTION:
            if (keyboard->prediction_animating)
                return WM_KEYBOARD_ACTION_NONE;
            wm_board_keyboard_finish_composition(keyboard);
            keyboard->prediction_from = keyboard->prediction_enabled;
            keyboard->prediction_enabled = !keyboard->prediction_enabled;
            keyboard->prediction_animating = true;
            keyboard->prediction_frame = 0.0f;
            keyboard->prediction_dirty = true;
            keyboard->keytop_dirty = true;
            keyboard->phone_dirty = true;
            wm_board_keyboard_prediction_refresh(keyboard);
            return WM_KEYBOARD_ACTION_PREDICTION_TOGGLE;
        case WM_KEYBOARD_DELETE:
            return WM_KEYBOARD_ACTION_DELETE;
        case WM_KEYBOARD_RETURN:
            utf8[0] = '\n';
            return WM_KEYBOARD_ACTION_INSERT;
        case WM_KEYBOARD_SPACE:
            utf8[0] = ' ';
            return WM_KEYBOARD_ACTION_INSERT;
        case WM_KEYBOARD_CAPS:
            keyboard->caps = !keyboard->caps;
            keyboard->shift = false;
            keyboard->keytop_dirty = true;
            return WM_KEYBOARD_ACTION_HANDLED;
        case WM_KEYBOARD_SHIFT:
            keyboard->shift = !keyboard->shift;
            keyboard->caps = false;
            keyboard->keytop_dirty = true;
            return WM_KEYBOARD_ACTION_HANDLED;
        case WM_KEYBOARD_BACK:
            return WM_KEYBOARD_ACTION_CLOSE_BACK;
        case WM_KEYBOARD_OK:
            return WM_KEYBOARD_ACTION_CLOSE_OK;
        case WM_KEYBOARD_MORE:
            keyboard->symbol_phase = SYMBOL_ENTERING;
            keyboard->symbol_frame = 0.0f;
            keyboard->symbols_dirty = true;
            wm_board_keyboard_hover(keyboard, WM_KEYBOARD_NONE);
            return WM_KEYBOARD_ACTION_SYMBOL_OPEN;
        case WM_KEYBOARD_NONE:
        case WM_KEYBOARD_CHARACTER_FIRST:
        case WM_KEYBOARD_CHARACTER_LAST:
        case WM_KEYBOARD_SYMBOL_FIRST:
        case WM_KEYBOARD_SYMBOL_LAST:
        case WM_KEYBOARD_SYMBOL_CLOSE:
        case WM_KEYBOARD_SYMBOL_PREV:
        case WM_KEYBOARD_SYMBOL_NEXT:
        case WM_KEYBOARD_QWERTY:
        case WM_KEYBOARD_PHONE:
        case WM_KEYBOARD_PHONE_FIRST:
        case WM_KEYBOARD_PHONE_LAST:
        case WM_KEYBOARD_PHONE_MODE_FIRST:
        case WM_KEYBOARD_PHONE_MODE_LAST:
        case WM_KEYBOARD_LANGUAGE_ENGLISH:
        case WM_KEYBOARD_LANGUAGE_FRENCH:
        case WM_KEYBOARD_LANGUAGE_SPANISH:
        case WM_KEYBOARD_CANDIDATE_FIRST:
        case WM_KEYBOARD_CANDIDATE_LAST:
        case WM_KEYBOARD_CANDIDATE_PREVIOUS:
        case WM_KEYBOARD_CANDIDATE_NEXT:
            break;
    }
    return WM_KEYBOARD_ACTION_NONE;
}
