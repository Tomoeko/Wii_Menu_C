#define _POSIX_C_SOURCE 200809L

#include "board_keyboard_internal.h"
#include "keyboard_text.h"

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

/* US csSignKeyUS order from the maintained HTML reference. */
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

/* USA phone key table from tiCpData.cpp, as retained by Wii_Menu_HTML. */
static const char *const phone_cycles[12] = {
    ".,?!-':@/$#&1", "abc2", "def3", "ghi4", "jkl5", "mno6",
    "pqrs7", "tuv8", "wxyz9", "", " 0", ""
};

static const char *const phone_labels[12] = {
    ".,?@", "abc", "def", "ghi", "jkl", "mno",
    "pqrs", "tuv", "wxyz", "", "\xee\x81\x97\x30", ""
};

/* The same compact, local fallback vocabulary as Wii_Menu_HTML. This is an
 * authored substitute for Zi8 and requires no extracted dictionary data. */
static const char *const dictionary_words[3] = {
    "a about after all also and are back be because before but can come day "
    "did do for from get good had has have hello here home how I if in into "
    "is it just know like look make me menu mom mon moo nom non money months "
    "more my new next no not now of on one only or other our out please right "
    "see so some thank thanks that the their them then there these they this "
    "time to today too two up use want was way we well what when where which "
    "who will with work would yes you your",
    "à après au aussi avec avoir bien bon bonjour ce ces comme dans de des "
    "deux dire du elle en encore est et faire ici il ils je jour la le les "
    "leur lui mais me merci mes moi mon ne nous on ou oui par pas plus pour "
    "pourquoi quand que quel qui sa sans se ses si son sur te temps toi ton "
    "toujours tout très tu un une va vais vous votre",
    "a ahora al algo aquí así bien buenos como con cuando de del día dos el "
    "ella en es esta este esto gracias gusta ha hacer hasta hay hola hoy la "
    "las le lo los más me mi mucho muy no nos nosotros o para pero poco por "
    "porque puede que qué quien se ser si sí sin sobre son su te tiempo todo "
    "tu tú un una uno usted vamos ver vez yo"
};

static const char *const oem_dictionary_names[3] = {
    "eZTNintendoENAM.znd",
    "eZTNintendoFRCA.znd",
    "eZTNintendoESSA.znd"
};

typedef struct VocabularyCursor {
    unsigned stage;
    size_t index;
    const char *fallback;
} VocabularyCursor;

static bool next_vocabulary_word(const WmBoardKeyboard *keyboard,
                                 VocabularyCursor *cursor,
                                 const char **word, size_t *length) {
    while (cursor->stage < 3) {
        if (cursor->stage == 0) {
            if (cursor->index < keyboard->learned_count) {
                *word = keyboard->learned_words[cursor->index++];
                *length = strlen(*word);
                return true;
            }
            cursor->stage++;
            cursor->index = 0;
        } else if (cursor->stage == 1) {
            if (!cursor->fallback)
                cursor->fallback = dictionary_words[
                    keyboard->dictionary_language];
            while (*cursor->fallback == ' ') cursor->fallback++;
            if (*cursor->fallback) {
                *word = cursor->fallback;
                while (*cursor->fallback && *cursor->fallback != ' ')
                    cursor->fallback++;
                *length = (size_t)(cursor->fallback - *word);
                return true;
            }
            cursor->stage++;
        } else {
            const WmKeyboardWordList *oem =
                &keyboard->oem_words[keyboard->dictionary_language];
            if (cursor->index < oem->count) {
                *word = oem->words[cursor->index++];
                *length = strlen(*word);
                return true;
            }
            cursor->stage++;
        }
    }
    return false;
}

static bool phone_uppercase(const WmBoardKeyboard *keyboard) {
    if (keyboard->phone_mode == 2) return true;
    if (keyboard->phone_mode != 0) return false;
    const char *text = keyboard->text_context ? keyboard->text_context : "";
    size_t length = strlen(text);
    if (length == 0) return true;
    while (length > 0 && (text[length - 1] == ' ' ||
                          text[length - 1] == '\t' ||
                          text[length - 1] == '\n' ||
                          text[length - 1] == '\r')) length--;
    return length > 0 && (text[length - 1] == '.' ||
                          text[length - 1] == '!' ||
                          text[length - 1] == '?');
}

const char *wm_board_keyboard_phone_label(const WmBoardKeyboard *keyboard,
                                unsigned index, char output[16]) {
    if (index >= 12) return "";
    if (keyboard->phone_mode == 3) {
        if (index < 9) {
            output[0] = (char)('1' + index);
            output[1] = '\0';
            return output;
        }
        return index == 9 ? "," : index == 10 ? "0" : "*";
    }
    const char *label = phone_labels[index];
    if (!phone_uppercase(keyboard)) return label;
    size_t length = strlen(label);
    if (length >= 16) return label;
    for (size_t position = 0; position <= length; position++) {
        char value = label[position];
        output[position] = value >= 'a' && value <= 'z'
                               ? (char)(value - 'a' + 'A') : value;
    }
    return output;
}

static void learn_word(WmBoardKeyboard *keyboard, const char *start,
                       size_t bytes, size_t points, size_t units) {
    if (points <= 1 || units > 64 || bytes >=
        WM_KEYBOARD_CANDIDATE_UTF8_CAPACITY) return;
    char lower[WM_KEYBOARD_CANDIDATE_UTF8_CAPACITY];
    size_t used = 0;
    const char *cursor = start;
    const char *end = start + bytes;
    while (cursor < end) {
        char encoded[4];
        uint32_t point = wm_keyboard_text_next_codepoint(&cursor, end);
        size_t encoded_bytes = wm_keyboard_text_encode_point(
            encoded, wm_keyboard_text_lower_point(point));
        if (used + encoded_bytes >= sizeof(lower)) return;
        memcpy(lower + used, encoded, encoded_bytes);
        used += encoded_bytes;
    }
    lower[used] = '\0';
    for (size_t index = 0; index < keyboard->learned_count; index++)
        if (strcmp(keyboard->learned_words[index], lower) == 0) return;
    if (keyboard->learned_count >= 2048) return;
    if (keyboard->learned_count == keyboard->learned_capacity) {
        size_t capacity = keyboard->learned_capacity
            ? keyboard->learned_capacity * 2 : 32;
        char **grown = realloc(keyboard->learned_words,
                               capacity * sizeof(*grown));
        if (!grown) return;
        keyboard->learned_words = grown;
        keyboard->learned_capacity = capacity;
    }
    char *copy = malloc(used + 1);
    if (!copy) return;
    memcpy(copy, lower, used + 1);
    keyboard->learned_words[keyboard->learned_count++] = copy;
}

static void learn_complete_words(WmBoardKeyboard *keyboard, const char *text,
                                 bool include_trailing) {
    if (!text) return;
    const char *cursor = text;
    const char *end = text + strlen(text);
    const char *word = NULL;
    size_t points = 0, units = 0;
    while (cursor < end) {
        const char *before = cursor;
        uint32_t point = wm_keyboard_text_next_codepoint(&cursor, end);
        if (wm_keyboard_text_is_word_point(point)) {
            if (!word) word = before;
            points++;
            units += point > 0xffffu ? 2u : 1u;
        } else if (word) {
            learn_word(keyboard, word, (size_t)(before - word),
                       points, units);
            word = NULL;
            points = 0;
            units = 0;
        }
    }
    if (word && include_trailing)
        learn_word(keyboard, word, (size_t)(end - word), points, units);
}

static void phone_prediction_value(WmBoardKeyboard *keyboard,
                                    char output[256]) {
    const char *digits = keyboard->phone_prediction_digits;
    size_t count = strlen(digits);
    const char *chosen = NULL;
    size_t chosen_length = 0;
    bool chosen_exact = false;
    VocabularyCursor cursor = {0};
    const char *word;
    size_t length;
    while (next_vocabulary_word(keyboard, &cursor, &word, &length)) {
        bool exact = false;
        if (!wm_keyboard_text_phone_digits_match(word, length, digits,
                                                 &exact)) {
            continue;
        }
        if (!chosen || (!chosen_exact && exact)) {
            chosen = word;
            chosen_length = length;
            chosen_exact = exact;
        }
        if (chosen_exact) break;
    }
    if (chosen) {
        const char *end = chosen + chosen_length;
        const char *part = chosen;
        for (size_t index = 0; index < count && part < end; index++)
            (void)wm_keyboard_text_next_codepoint(&part, end);
        size_t bytes = (size_t)(part - chosen);
        memcpy(output, chosen, bytes);
        output[bytes] = '\0';
    } else {
        for (size_t index = 0; index < count; index++)
            output[index] = phone_cycles[digits[index] - '1'][0];
        output[count] = '\0';
    }
    if (keyboard->phone_prediction_uppercase && output[0]) {
        const char *part = output;
        uint32_t first = wm_keyboard_text_next_codepoint(
            &part, output + strlen(output));
        char converted[4];
        size_t converted_bytes = wm_keyboard_text_encode_point(
            converted, wm_keyboard_text_upper_point(first));
        size_t old_bytes = (size_t)(part - output);
        size_t rest = strlen(part);
        if (converted_bytes + rest < 256) {
            memmove(output + converted_bytes, part, rest + 1);
            memcpy(output, converted, converted_bytes);
        } else if (old_bytes == converted_bytes) {
            memcpy(output, converted, converted_bytes);
        }
    }
}

static void rollback_unapplied_phone_prediction(WmBoardKeyboard *keyboard) {
    if (!keyboard->phone_prediction_awaiting_edit) return;
    memcpy(keyboard->phone_prediction_digits,
           keyboard->phone_prediction_previous_digits,
           sizeof(keyboard->phone_prediction_digits));
    keyboard->phone_prediction_bytes =
        keyboard->phone_prediction_previous_bytes;
    keyboard->phone_prediction_awaiting_edit = false;
}

static void measure_candidates(WmBoardKeyboard *keyboard) {
    WmLayoutPaneState pane;
    bool measured = wm_layout_pane_state(keyboard->prediction,
                                          "T_prdc_Text_00", &pane);
    float position = 0.0f;
    for (unsigned index = 0; index < keyboard->candidate_count; index++) {
        const char *value = keyboard->candidates[index];
        float width = measured ? wm_font_cache_measure_text(keyboard->fonts,
            keyboard->prediction, &pane, value, strlen(value)) : 0.0f;
        if (width < 20.0f) width = (float)strlen(value) * 14.0f;
        width += 0.01f;
        keyboard->candidate_widths[index] = width;
        keyboard->candidate_screen_widths[index] = width * (608.0f / 832.0f);
        keyboard->candidate_positions[index] = position;
        position += keyboard->candidate_screen_widths[index] + 10.0f;
    }
}

unsigned wm_board_keyboard_candidate_next_index(const WmBoardKeyboard *keyboard) {
    if (keyboard->candidate_count == 0) return 0;
    unsigned index = keyboard->candidate_first;
    float width = -10.0f;
    while (width <= 390.0f && index < keyboard->candidate_count) {
        width += keyboard->candidate_screen_widths[index] + 10.0f;
        index++;
    }
    if (index == keyboard->candidate_count && width <= 390.0f)
        return keyboard->candidate_first;
    unsigned target = index > 0 ? index - 1 : 0;
    if (target <= keyboard->candidate_first)
        target = keyboard->candidate_first + 1;
    if (target >= keyboard->candidate_count)
        target = keyboard->candidate_count - 1;
    return target;
}

static unsigned candidate_previous_index(const WmBoardKeyboard *keyboard) {
    if (keyboard->candidate_first == 0) return 0;
    int index = (int)keyboard->candidate_first - 1;
    float width = -10.0f;
    while (width <= 390.0f && index >= 0) {
        width += keyboard->candidate_screen_widths[index] + 10.0f;
        index--;
    }
    unsigned target = (unsigned)(index + 2);
    return target < keyboard->candidate_first
        ? target : keyboard->candidate_first - 1;
}

static bool candidate_already_added(const WmBoardKeyboard *keyboard,
                                    const char *candidate) {
    for (unsigned index = 0; index < keyboard->candidate_count; index++)
        if (strcmp(keyboard->candidates[index], candidate) == 0) return true;
    return false;
}

static void refresh_candidates(WmBoardKeyboard *keyboard) {
    keyboard->candidate_count = 0;
    keyboard->candidate_prefix_bytes = 0;
    keyboard->candidate_first = 0;
    keyboard->candidate_target = 0;
    keyboard->candidate_scroll_frame = 0.0f;
    keyboard->candidate_scrolling = false;
    keyboard->selected_candidate_index = 0;
    for (unsigned slot = 0; slot < CANDIDATE_PANE_COUNT; slot++)
        keyboard->candidate_pane_indices[slot] = CANDIDATE_COUNT;
    keyboard->prediction_dirty = true;
    if (!keyboard->prediction_enabled ||
        keyboard->profile != WM_BOARD_KEYBOARD_MEMO ||
        !keyboard->text_context) return;
    const char *text = keyboard->text_context;
    size_t end = strlen(text);
    size_t start = keyboard->composition_start;
    bool phone_digits = keyboard->phone_layout &&
                        keyboard->phone_prediction_digits[0];
    if (phone_digits) {
        if (keyboard->phone_prediction_bytes > end) return;
        start = end - keyboard->phone_prediction_bytes;
    } else if (start == SIZE_MAX || start > end) return;
    size_t length = end - start;
    if (length == 0 || length >= sizeof(keyboard->candidates[0])) return;
    keyboard->candidate_prefix_bytes = length;
    unsigned passes = phone_digits ? 2 : 1;
    for (unsigned pass = 0; pass < passes &&
         keyboard->candidate_count < LOCAL_CANDIDATE_LIMIT; pass++) {
        VocabularyCursor cursor = {0};
        const char *word;
        size_t word_length;
        while (next_vocabulary_word(keyboard, &cursor, &word, &word_length) &&
               keyboard->candidate_count < LOCAL_CANDIDATE_LIMIT) {
            if (word_length == 0 ||
                word_length >= WM_KEYBOARD_CANDIDATE_UTF8_CAPACITY) continue;
            if (phone_digits) {
                bool exact = false;
                if (!wm_keyboard_text_phone_digits_match(
                        word, word_length, keyboard->phone_prediction_digits,
                        &exact) || exact != (pass == 0)) {
                    continue;
                }
            } else if (!wm_keyboard_text_prefix_matches(
                           word, word_length, text + start, length) ||
                       wm_keyboard_text_prefix_matches(
                           text + start, length, word, word_length)) {
                continue;
            }
            char candidate[WM_KEYBOARD_CANDIDATE_UTF8_CAPACITY];
            if (!wm_keyboard_text_copy_candidate_case(
                    candidate, sizeof(candidate), word, word_length,
                    text + start, length, phone_digits,
                    keyboard->phone_prediction_uppercase) ||
                candidate_already_added(keyboard, candidate)) {
                continue;
            }
            memcpy(keyboard->candidates[keyboard->candidate_count++],
                   candidate, strlen(candidate) + 1);
        }
    }
    if (keyboard->candidate_count == 0) {
        memcpy(keyboard->candidates[0], text + start, length);
        keyboard->candidates[0][length] = '\0';
        keyboard->candidate_count = 1;
    }
    measure_candidates(keyboard);
    keyboard->prediction_dirty = true;
}

char wm_board_keyboard_key_character(const WmBoardKeyboard *keyboard, unsigned index) {
    if (index < sizeof(qwerty_normal) - 1) {
        char value = keyboard->shift ? qwerty_shifted[index] :
                                       qwerty_normal[index];
        if (!keyboard->shift && keyboard->caps &&
            value >= 'a' && value <= 'z') value = (char)(value - 'a' + 'A');
        return value;
    }
    if (index >= 44 && index < 50) {
        return keyboard->shift ? shifted_extras[index - 44][0]
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
    for (unsigned language = 0; language < 3; language++) {
        char path[KEYBOARD_PATH_CAPACITY];
        int length = snprintf(path, sizeof(path),
            "%s/keyboard-dictionary/%s", assets_directory,
            oem_dictionary_names[language]);
        if (length > 0 && length < (int)sizeof(path)) {
            char error[160] = {0};
            (void)wm_keyboard_oem_load(path, &keyboard->oem_words[language],
                                       error, sizeof(error));
        }
    }
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
    for (unsigned language = 0; language < 3; language++)
        wm_keyboard_word_list_free(&keyboard->oem_words[language]);
    for (size_t index = 0; index < keyboard->learned_count; index++)
        free(keyboard->learned_words[index]);
    free(keyboard->learned_words);
    free(keyboard);
}

void wm_board_keyboard_reset(WmBoardKeyboard *keyboard) {
    if (!keyboard) return;
    for (size_t index = 0; index < keyboard->learned_count; index++)
        free(keyboard->learned_words[index]);
    keyboard->learned_count = 0;
    keyboard->profile = WM_BOARD_KEYBOARD_MEMO;
    keyboard->phone_layout = keyboard->memo_phone_layout;
    keyboard->phone_mode = keyboard->memo_phone_mode;
    memset(keyboard->focus, 0, sizeof(keyboard->focus));
    keyboard->hovered = WM_KEYBOARD_NONE;
    keyboard->pressed = WM_KEYBOARD_NONE;
    keyboard->pressed_phone_hover = WM_KEYBOARD_NONE;
    keyboard->press_frame = 20.0f;
    keyboard->caps = false;
    keyboard->shift = false;
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
    refresh_candidates(keyboard);
    wm_board_keyboard_release_hold(keyboard);
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
    refresh_candidates(keyboard);
}

WmBoardKeyboardProfile wm_board_keyboard_profile(
    const WmBoardKeyboard *keyboard) {
    return keyboard ? keyboard->profile : WM_BOARD_KEYBOARD_MEMO;
}

void wm_board_keyboard_set_text_context(WmBoardKeyboard *keyboard,
                                         const char *utf8) {
    if (!keyboard) return;
    for (size_t index = 0; index < keyboard->learned_count; index++)
        free(keyboard->learned_words[index]);
    keyboard->learned_count = 0;
    keyboard->text_context = utf8;
    keyboard->observed_text_bytes = utf8 ? strlen(utf8) : 0;
    keyboard->composition_start = SIZE_MAX;
    learn_complete_words(keyboard, utf8, true);
    keyboard->phone_dirty = true;
    refresh_candidates(keyboard);
}

void wm_board_keyboard_finish_composition(WmBoardKeyboard *keyboard) {
    if (!keyboard) return;
    keyboard->composition_start = SIZE_MAX;
    keyboard->observed_text_bytes = keyboard->text_context
        ? strlen(keyboard->text_context) : 0;
    keyboard->phone_prediction_digits[0] = '\0';
    keyboard->phone_prediction_bytes = 0;
    keyboard->phone_prediction_awaiting_edit = false;
    refresh_candidates(keyboard);
}

void wm_board_keyboard_clear_phone_pending(WmBoardKeyboard *keyboard) {
    if (!keyboard) return;
    keyboard->phone_pending = false;
    keyboard->phone_pending_frames = 0.0f;
    keyboard->phone_prediction_digits[0] = '\0';
    keyboard->phone_prediction_bytes = 0;
    keyboard->phone_prediction_awaiting_edit = false;
    refresh_candidates(keyboard);
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

void wm_board_keyboard_text_changed(WmBoardKeyboard *keyboard,
                                     bool from_phone_key) {
    if (!keyboard) return;
    keyboard->phone_dirty = true;
    if (!from_phone_key) {
        wm_board_keyboard_clear_phone_pending(keyboard);
    } else keyboard->phone_prediction_awaiting_edit = false;
    size_t end = keyboard->text_context ? strlen(keyboard->text_context) : 0;
    if (keyboard->prediction_enabled && end > keyboard->observed_text_bytes) {
        size_t begin = keyboard->composition_start == SIZE_MAX
            ? keyboard->observed_text_bytes : keyboard->composition_start;
        for (size_t index = keyboard->observed_text_bytes; index < end; index++)
            if (isspace((unsigned char)keyboard->text_context[index]))
                begin = index + 1;
        keyboard->composition_start = begin == end ? SIZE_MAX : begin;
    } else if (keyboard->composition_start >= end ||
               (end > 0 && isspace((unsigned char)
                   keyboard->text_context[end - 1]))) {
        keyboard->composition_start = SIZE_MAX;
    }
    keyboard->observed_text_bytes = end;
    learn_complete_words(keyboard, keyboard->text_context, false);
    refresh_candidates(keyboard);
}

bool wm_board_keyboard_phone_mode(const WmBoardKeyboard *keyboard) {
    return keyboard && keyboard->phone_layout;
}

bool wm_board_keyboard_prediction_enabled(const WmBoardKeyboard *keyboard) {
    return keyboard && keyboard->prediction_enabled;
}

const char *wm_board_keyboard_candidate_text(const WmBoardKeyboard *keyboard) {
    return keyboard ? keyboard->accepted_candidate : "";
}

size_t wm_board_keyboard_candidate_prefix_bytes(
    const WmBoardKeyboard *keyboard) {
    return keyboard ? keyboard->accepted_prefix_bytes : 0;
}

bool wm_board_keyboard_composition(const WmBoardKeyboard *keyboard,
                                   WmBoardKeyboardComposition *composition) {
    if (!keyboard || !composition) return false;
    *composition = (WmBoardKeyboardComposition){0};
    if (!keyboard->prediction_enabled || keyboard->candidate_count == 0 ||
        keyboard->candidate_prefix_bytes == 0) return false;
    composition->prefix_bytes = keyboard->candidate_prefix_bytes;
    unsigned selected = keyboard->selected_candidate_index;
    if (selected >= keyboard->candidate_count) selected = 0;
    composition->selected_candidate = keyboard->candidates[selected];
    unsigned preview = keyboard->candidate_count;
    if (!keyboard->candidate_scrolling &&
        keyboard->hovered >= WM_KEYBOARD_CANDIDATE_FIRST &&
        keyboard->hovered <= WM_KEYBOARD_CANDIDATE_LAST) {
        unsigned slot = keyboard->hovered - WM_KEYBOARD_CANDIDATE_FIRST;
        if (keyboard->candidate_pane_indices[slot] <
            keyboard->candidate_count) {
            preview = keyboard->candidate_pane_indices[slot];
            composition->preview_hovered = true;
        }
    }
    if (preview >= keyboard->candidate_count) {
        const char *text = keyboard->text_context;
        size_t start = strlen(text) - composition->prefix_bytes;
        for (unsigned index = 0; index < keyboard->candidate_count; index++) {
            const char *word = keyboard->candidates[index];
            if (strcmp(word, ">") &&
                wm_keyboard_text_prefix_matches(word, strlen(word),
                                                text + start,
                                                composition->prefix_bytes)) {
                preview = index;
                break;
            }
        }
    }
    composition->preview_candidate = preview < keyboard->candidate_count
        ? keyboard->candidates[preview] : NULL;
    return true;
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
                if (strchr(phone_cycles[index], lower)) {
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
            char normal = index < sizeof(qwerty_normal) - 1
                ? qwerty_normal[index] : keytop_extras[index - 44][0];
            char shifted = index < sizeof(qwerty_shifted) - 1
                ? qwerty_shifted[index] : shifted_extras[index - 44][0];
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
    rollback_unapplied_phone_prediction(keyboard);
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
    rollback_unapplied_phone_prediction(keyboard);
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
        refresh_candidates(keyboard);
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
            ? candidate_previous_index(keyboard) :
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
                keyboard->phone_prediction_uppercase = phone_uppercase(keyboard);
            keyboard->phone_prediction_digits[digits] = (char)('1' + index);
            keyboard->phone_prediction_digits[digits + 1] = '\0';
            keyboard->accepted_prefix_bytes = keyboard->phone_prediction_bytes;
            phone_prediction_value(keyboard, keyboard->accepted_candidate);
            keyboard->phone_prediction_bytes =
                strlen(keyboard->accepted_candidate);
            keyboard->phone_prediction_awaiting_edit = true;
            keyboard->prediction_dirty = true;
            return WM_KEYBOARD_ACTION_PREDICT_PHONE;
        }
        keyboard->phone_prediction_digits[0] = '\0';
        keyboard->phone_prediction_bytes = 0;
        const char *cycle = phone_cycles[index];
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
            keyboard->phone_pending_uppercase = phone_uppercase(keyboard);
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
            refresh_candidates(keyboard);
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
