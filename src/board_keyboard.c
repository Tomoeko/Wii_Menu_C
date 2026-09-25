#define _POSIX_C_SOURCE 200809L

#include "wii_menu/board_keyboard.h"

#include "wii_menu/keyboard_dictionary.h"
#include "wii_menu/layout_present.h"
#include "wii_menu/layout_runtime.h"
#include "wii_menu/material_prepare.h"
#include "wii_menu/source_hit.h"

#include <math.h>
#include <ctype.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

enum {
    KEYBOARD_PATH_CAPACITY = 4096,
    KEYBOARD_CLIP_CAPACITY = WM_KEYBOARD_CONTROL_LAST + 12,
    SYMBOL_PAGE_COUNT = 10,
    SYMBOLS_PER_PAGE = 20,
    CANDIDATE_PANE_COUNT = 20,
    CANDIDATE_COUNT = 40,
    LOCAL_CANDIDATE_LIMIT = 20,
    MAX_HOLD_REPEATS_PER_ADVANCE = 4096
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

static void mark_dirty(WmBoardKeyboard *keyboard,
                       WmBoardKeyboardControl control);
static bool profile_allows_control(const WmBoardKeyboard *keyboard,
                                   WmBoardKeyboardControl control);

/* US csSignKeyUS order from the maintained HTML reference. */
static const char *const symbol_text[SYMBOL_PAGE_COUNT][SYMBOLS_PER_PAGE] = {
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

static const char *const phone_modes[4] = {"Abc", "abc", "ABC", "123"};

static const char *const language_names[3] = {
    "English", "Français", "Español"
};

static const char *const language_short[3] = {"Eng", "Fra", "Esp"};

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

static const char *phone_label(const WmBoardKeyboard *keyboard,
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

static uint32_t next_codepoint(const char **cursor, const char *end) {
    if (*cursor >= end) return 0;
    const unsigned char *bytes = (const unsigned char *)*cursor;
    uint32_t point = bytes[0];
    unsigned continuation = 0;
    if ((point & 0xe0u) == 0xc0u) {
        point &= 0x1fu;
        continuation = 1;
    } else if ((point & 0xf0u) == 0xe0u) {
        point &= 0x0fu;
        continuation = 2;
    } else if ((point & 0xf8u) == 0xf0u) {
        point &= 0x07u;
        continuation = 3;
    } else {
        (*cursor)++;
        return point;
    }
    if ((size_t)(end - *cursor) <= continuation) {
        (*cursor)++;
        return bytes[0];
    }
    for (unsigned index = 1; index <= continuation; index++) {
        if ((bytes[index] & 0xc0u) != 0x80u) {
            (*cursor)++;
            return bytes[0];
        }
        point = (point << 6) | (bytes[index] & 0x3fu);
    }
    *cursor += continuation + 1;
    return point;
}

static uint32_t lower_point(uint32_t point) {
    if (point >= 'A' && point <= 'Z') return point + 0x20u;
    if ((point >= 0xc0u && point <= 0xd6u) ||
        (point >= 0xd8u && point <= 0xdeu)) return point + 0x20u;
    if (point == 0x178u) return 0xffu;
    return point;
}

static uint32_t upper_point(uint32_t point) {
    if (point >= 'a' && point <= 'z') return point - 0x20u;
    if ((point >= 0xe0u && point <= 0xf6u) ||
        (point >= 0xf8u && point <= 0xfeu)) return point - 0x20u;
    if (point == 0xffu) return 0x178u;
    return point;
}

static size_t encode_point(char output[4], uint32_t point) {
    if (point < 0x80u) {
        output[0] = (char)point;
        return 1;
    }
    if (point < 0x800u) {
        output[0] = (char)(0xc0u | (point >> 6));
        output[1] = (char)(0x80u | (point & 0x3fu));
        return 2;
    }
    if (point < 0x10000u) {
        output[0] = (char)(0xe0u | (point >> 12));
        output[1] = (char)(0x80u | ((point >> 6) & 0x3fu));
        output[2] = (char)(0x80u | (point & 0x3fu));
        return 3;
    }
    output[0] = (char)(0xf0u | (point >> 18));
    output[1] = (char)(0x80u | ((point >> 12) & 0x3fu));
    output[2] = (char)(0x80u | ((point >> 6) & 0x3fu));
    output[3] = (char)(0x80u | (point & 0x3fu));
    return 4;
}

static bool is_word_point(uint32_t point) {
    if ((point >= 'A' && point <= 'Z') ||
        (point >= 'a' && point <= 'z')) return true;
    if ((point >= 0xc0u && point <= 0xffu &&
         point != 0xd7u && point != 0xf7u) ||
        (point >= 0x100u && point <= 0x24fu) ||
        (point >= 0x300u && point <= 0x36fu) ||
        (point >= 0x370u && point <= 0x3ffu)) return true;
    return false;
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
        size_t encoded_bytes = encode_point(encoded,
            lower_point(next_codepoint(&cursor, end)));
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
        uint32_t point = next_codepoint(&cursor, end);
        if (is_word_point(point)) {
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

static bool prefix_matches(const char *word, size_t word_bytes,
                           const char *prefix, size_t prefix_bytes) {
    const char *word_cursor = word;
    const char *word_end = word + word_bytes;
    const char *prefix_cursor = prefix;
    const char *prefix_end = prefix + prefix_bytes;
    while (prefix_cursor < prefix_end) {
        if (word_cursor >= word_end) return false;
        if (lower_point(next_codepoint(&word_cursor, word_end)) !=
            lower_point(next_codepoint(&prefix_cursor, prefix_end)))
            return false;
    }
    return true;
}

static char phone_digit_for_letter(uint32_t letter) {
    static const char *const groups[8] = {
        "abc", "def", "ghi", "jkl", "mno", "pqrs", "tuv", "wxyz"
    };
    letter = lower_point(letter);
    if (letter >= 0xe0u && letter <= 0xe5u) letter = 'a';
    else if (letter == 0xe7u) letter = 'c';
    else if (letter >= 0xe8u && letter <= 0xebu) letter = 'e';
    else if (letter >= 0xecu && letter <= 0xefu) letter = 'i';
    else if (letter == 0xf1u) letter = 'n';
    else if (letter >= 0xf2u && letter <= 0xf6u) letter = 'o';
    else if (letter >= 0xf9u && letter <= 0xfcu) letter = 'u';
    else if (letter == 0xfdu || letter == 0xffu) letter = 'y';
    if (letter > 0x7fu) return '\0';
    for (unsigned index = 0; index < 8; index++)
        if (strchr(groups[index], (int)letter)) return (char)('2' + index);
    return '\0';
}

static bool phone_digits_match(const char *word, size_t word_bytes,
                               const char *digits, bool *exact) {
    size_t count = strlen(digits);
    size_t matched = 0;
    const char *cursor = word;
    const char *end = word + word_bytes;
    while (cursor < end) {
        uint32_t point = next_codepoint(&cursor, end);
        char digit = phone_digit_for_letter(point);
        if (digit == '\0') continue;
        if (matched < count && digit != digits[matched]) return false;
        matched++;
    }
    if (exact) *exact = matched == count;
    return matched >= count;
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
        if (!phone_digits_match(word, length, digits, &exact)) continue;
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
            (void)next_codepoint(&part, end);
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
        uint32_t first = next_codepoint(&part, output + strlen(output));
        char converted[4];
        size_t converted_bytes = encode_point(converted, upper_point(first));
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

static unsigned candidate_next_index(const WmBoardKeyboard *keyboard) {
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

static float candidate_offset(const WmBoardKeyboard *keyboard) {
    if (keyboard->candidate_count == 0) return 0.0f;
    float from = keyboard->candidate_positions[keyboard->candidate_first];
    if (!keyboard->candidate_scrolling) return from;
    float to = keyboard->candidate_positions[keyboard->candidate_target];
    float t = fminf(keyboard->candidate_scroll_frame, 15.0f) / 15.0f;
    t = t * t * (3.0f - 2.0f * t);
    return from + (to - from) * t;
}

static bool copy_candidate_case(char output[WM_KEYBOARD_CANDIDATE_UTF8_CAPACITY],
                                const char *word, size_t word_bytes,
                                const char *prefix, size_t prefix_bytes,
                                bool phone_digits,
                                bool phone_uppercase) {
    bool all_upper = !phone_digits;
    bool first_upper = phone_digits && phone_uppercase;
    if (!phone_digits) {
        const char *cursor = prefix;
        const char *end = prefix + prefix_bytes;
        if (cursor < end) {
            uint32_t point = next_codepoint(&cursor, end);
            first_upper = upper_point(point) == point;
            if (lower_point(point) == point && upper_point(point) != point)
                all_upper = false;
        }
        while (cursor < end) {
            uint32_t point = next_codepoint(&cursor, end);
            if (lower_point(point) == point && upper_point(point) != point)
                all_upper = false;
        }
    }
    const char *cursor = word;
    const char *end = word + word_bytes;
    size_t used = 0;
    bool first = true;
    while (cursor < end) {
        uint32_t point = lower_point(next_codepoint(&cursor, end));
        if (all_upper || (first && first_upper)) point = upper_point(point);
        char encoded[4];
        size_t bytes = encode_point(encoded, point);
        if (used + bytes >= WM_KEYBOARD_CANDIDATE_UTF8_CAPACITY)
            return false;
        memcpy(output + used, encoded, bytes);
        used += bytes;
        first = false;
    }
    output[used] = '\0';
    return used > 0;
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
                if (!phone_digits_match(word, word_length,
                    keyboard->phone_prediction_digits, &exact) ||
                    exact != (pass == 0)) continue;
            } else if (!prefix_matches(word, word_length,
                       text + start, length) ||
                       prefix_matches(text + start, length,
                                      word, word_length)) {
                continue;
            }
            char candidate[WM_KEYBOARD_CANDIDATE_UTF8_CAPACITY];
            if (!copy_candidate_case(candidate, word, word_length,
                text + start, length, phone_digits,
                keyboard->phone_prediction_uppercase) ||
                candidate_already_added(keyboard, candidate)) continue;
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

static char key_character(const WmBoardKeyboard *keyboard, unsigned index) {
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
            if (strcmp(word, ">") && prefix_matches(word, strlen(word),
                text + start, composition->prefix_bytes)) {
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
                const char *label = phone_label(keyboard, index, label_buffer);
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

static bool is_toolbar(WmBoardKeyboardControl control) {
    return control == WM_KEYBOARD_BACK || control == WM_KEYBOARD_OK ||
           control == WM_KEYBOARD_QWERTY || control == WM_KEYBOARD_PHONE;
}

static bool is_keytop(WmBoardKeyboardControl control) {
    return control >= WM_KEYBOARD_CHARACTER_FIRST &&
           (control <= WM_KEYBOARD_SPACE || control == WM_KEYBOARD_MORE ||
            control == WM_KEYBOARD_LANGUAGE);
}

static bool is_symbol(WmBoardKeyboardControl control) {
    return control >= WM_KEYBOARD_SYMBOL_FIRST &&
           control <= WM_KEYBOARD_SYMBOL_NEXT;
}

static bool is_phone_control(const WmBoardKeyboard *keyboard,
                              WmBoardKeyboardControl control) {
    return (control >= WM_KEYBOARD_PHONE_FIRST &&
            control <= WM_KEYBOARD_PHONE_MODE_LAST) ||
           (keyboard->phone_layout &&
            (control == WM_KEYBOARD_DELETE ||
             control == WM_KEYBOARD_RETURN || control == WM_KEYBOARD_MORE ||
             control == WM_KEYBOARD_LANGUAGE));
}

static bool is_language_choice(WmBoardKeyboardControl control) {
    return control >= WM_KEYBOARD_LANGUAGE_ENGLISH &&
           control <= WM_KEYBOARD_LANGUAGE_SPANISH;
}

static bool is_prediction_control(WmBoardKeyboardControl control) {
    return control == WM_KEYBOARD_PREDICTION ||
           (control >= WM_KEYBOARD_CANDIDATE_FIRST &&
            control <= WM_KEYBOARD_CANDIDATE_NEXT);
}

static bool selected_tab(const WmBoardKeyboard *keyboard,
                         WmBoardKeyboardControl control) {
    if (control == WM_KEYBOARD_QWERTY) return !keyboard->phone_layout;
    if (control == WM_KEYBOARD_PHONE) return keyboard->phone_layout;
    return keyboard->phone_layout &&
           control >= WM_KEYBOARD_PHONE_MODE_FIRST &&
           control <= WM_KEYBOARD_PHONE_MODE_LAST &&
           (unsigned)(control - WM_KEYBOARD_PHONE_MODE_FIRST) ==
               keyboard->phone_mode;
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
            bool next_available = candidate_next_index(keyboard) >
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

static const char *picture_name(WmBoardKeyboardControl control,
                                char name[24], bool phone_layout) {
    if (control >= WM_KEYBOARD_CHARACTER_FIRST &&
        control <= WM_KEYBOARD_CHARACTER_LAST) {
        snprintf(name, 24, "P_key_%02u",
                 (unsigned)control - WM_KEYBOARD_CHARACTER_FIRST);
        return name;
    }
    if (control >= WM_KEYBOARD_SYMBOL_FIRST &&
        control <= WM_KEYBOARD_SYMBOL_LAST) {
        snprintf(name, 24, "P_SGNkey_%02u",
                 (unsigned)control - WM_KEYBOARD_SYMBOL_FIRST);
        return name;
    }
    if (control >= WM_KEYBOARD_PHONE_FIRST &&
        control <= WM_KEYBOARD_PHONE_LAST) {
        snprintf(name, 24, "W_CPkey_%02u",
                 (unsigned)control - WM_KEYBOARD_PHONE_FIRST);
        return name;
    }
    if (control >= WM_KEYBOARD_PHONE_MODE_FIRST &&
        control <= WM_KEYBOARD_PHONE_MODE_LAST) {
        snprintf(name, 24, "W_ChngTag_%02u",
                 (unsigned)control - WM_KEYBOARD_PHONE_MODE_FIRST);
        return name;
    }
    if (is_language_choice(control)) {
        static const char *const names[3] = {
            "P_PRDC_US_US", "P_PRDC_US_Fre", "P_PRDC_US_Spa"
        };
        return names[control - WM_KEYBOARD_LANGUAGE_ENGLISH];
    }
    if (control >= WM_KEYBOARD_CANDIDATE_FIRST &&
        control <= WM_KEYBOARD_CANDIDATE_LAST) {
        snprintf(name, 24, "T_prdc_Text_%02u",
                 (unsigned)control - WM_KEYBOARD_CANDIDATE_FIRST);
        return name;
    }
    if (control == WM_KEYBOARD_CANDIDATE_PREVIOUS)
        return "P_prdc_scrl_Left";
    if (control == WM_KEYBOARD_CANDIDATE_NEXT)
        return "P_prdc_scrl_Rght";
    switch (control) {
        case WM_KEYBOARD_DELETE:
            return phone_layout ? "W_CPkey_DELETE" : "P_key_DELETE";
        case WM_KEYBOARD_RETURN:
            return phone_layout ? "W_CPkey_LF" : "P_key_LF";
        case WM_KEYBOARD_CAPS: return "P_key_CAPS";
        case WM_KEYBOARD_SHIFT: return "P_key_SHIFT";
        case WM_KEYBOARD_SPACE: return "P_key_SPACE";
        case WM_KEYBOARD_BACK: return "P_BT_cancel";
        case WM_KEYBOARD_OK: return "P_BT_confirm";
        case WM_KEYBOARD_MORE:
            return phone_layout ? "W_othersBT_EU" :
                                  "W_USEU_Chng_sign";
        case WM_KEYBOARD_QWERTY: return "P_kyChng_QWERTY";
        case WM_KEYBOARD_PHONE: return "P_kyChng_CP";
        case WM_KEYBOARD_LANGUAGE:
            return phone_layout ? "W_prdcModeBT_EU" : "W_USEU_prdc_lang";
        case WM_KEYBOARD_PREDICTION: return "P_OffBtn";
        case WM_KEYBOARD_SYMBOL_CLOSE: return "P_SGNkey_close";
        case WM_KEYBOARD_SYMBOL_PREV: return "P_SGNkey_prev";
        case WM_KEYBOARD_SYMBOL_NEXT: return "P_SGNkey_next";
        case WM_KEYBOARD_NONE:
        case WM_KEYBOARD_CHARACTER_FIRST:
        case WM_KEYBOARD_CHARACTER_LAST:
        case WM_KEYBOARD_SYMBOL_FIRST:
        case WM_KEYBOARD_SYMBOL_LAST:
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
    return NULL;
}

static void append_clip(WmLayoutClip clips[KEYBOARD_CLIP_CAPACITY],
                        size_t *count, const char *animation,
                        const char *target, float frame) {
    if (*count >= KEYBOARD_CLIP_CAPACITY) return;
    clips[(*count)++] = (WmLayoutClip){
        .animation = animation,
        .target_name = target,
        .frame = frame,
        .loop_override = 0
    };
}

static void append_rebound_clip(WmLayoutClip clips[KEYBOARD_CLIP_CAPACITY],
                                size_t *count, const char *animation,
                                const char *prototype,
                                const char *destination, float frame) {
    if (*count >= KEYBOARD_CLIP_CAPACITY) return;
    clips[(*count)++] = (WmLayoutClip){
        .animation = animation,
        .target_name = prototype,
        .rebind_name = destination,
        .frame = frame,
        .loop_override = 0
    };
}

static const char *focus_prototype(WmBoardKeyboardControl control,
                                   const char *picture, bool phone_layout) {
    if (control >= WM_KEYBOARD_CHARACTER_FIRST &&
        control <= WM_KEYBOARD_CHARACTER_LAST) return "P_key_00";
    if (control == WM_KEYBOARD_OK) return "P_BT_cancel";
    if (control == WM_KEYBOARD_PHONE) return "P_kyChng_QWERTY";
    if (control >= WM_KEYBOARD_PHONE_FIRST &&
        control <= WM_KEYBOARD_PHONE_LAST) return "W_CPkey_00";
    if (control >= WM_KEYBOARD_PHONE_MODE_FIRST &&
        control <= WM_KEYBOARD_PHONE_MODE_LAST) return "W_ChngTag_00";
    if (phone_layout && (control == WM_KEYBOARD_DELETE ||
                         control == WM_KEYBOARD_RETURN)) return "W_CPkey_00";
    if (control >= WM_KEYBOARD_SYMBOL_FIRST &&
        control <= WM_KEYBOARD_SYMBOL_LAST) return "P_SGNkey_00";
    if (is_language_choice(control)) return "P_PRDC_US_US";
    if (control == WM_KEYBOARD_CANDIDATE_NEXT)
        return "P_prdc_scrl_Left";
    if (control >= WM_KEYBOARD_CANDIDATE_FIRST &&
        control <= WM_KEYBOARD_CANDIDATE_LAST) return "T_prdc_Text_00";
    if (control == WM_KEYBOARD_SYMBOL_PREV ||
        control == WM_KEYBOARD_SYMBOL_NEXT) return "P_SGNkey_close";
    return picture;
}

static void raise_focused_key(WmBoardKeyboard *keyboard, WmLayout *layout,
                              bool phone_layout) {
    WmBoardKeyboardControl control = keyboard->hovered != WM_KEYBOARD_NONE
        ? keyboard->hovered : keyboard->pressed;
    char name[24];
    const char *picture = picture_name(control, name, phone_layout);
    if (picture) (void)wm_layout_raise_pane(layout, picture);
}

static void pose_controls(WmBoardKeyboard *keyboard, bool toolbar) {
    WmLayout *layout = toolbar ? keyboard->toolbar : keyboard->keytop;
    const char *stem = toolbar ? "fs_VK_toolbar_a_" :
                                 "fs_VK_ascii_keytop_a_";
    char name[80];
    WmLayoutClip clips[KEYBOARD_CLIP_CAPACITY];
    size_t count = 0;
    snprintf(name, sizeof(name), "%snormal", stem);
    append_clip(clips, &count, name, NULL, 0.0f);
    /* Names in clips must outlive wm_layout_pose; use stable constants below. */
    static const char *const keytop_motion[3] = {
        "fs_VK_ascii_keytop_a_Focus-IN",
        "fs_VK_ascii_keytop_a_Focus-OUT",
        "fs_VK_ascii_keytop_a_Pushed"
    };
    static const char *const selected_motion[4] = {
        "fs_VK_ascii_keytop_a_normal_toggle-ON",
        "fs_VK_ascii_keytop_a_toggleON_Focus-IN",
        "fs_VK_ascii_keytop_a_toggleON_Focus-OUT",
        "fs_VK_ascii_keytop_a_toggleON_Pushed"
    };
    static const char *const toolbar_motion[3] = {
        "fs_VK_toolbar_a_Focus-IN",
        "fs_VK_toolbar_a_Focus-OUT",
        "fs_VK_toolbar_a_Pushed"
    };
    const char *const *motion = toolbar ? toolbar_motion : keytop_motion;
    char target_names[WM_KEYBOARD_CONTROL_LAST + 1][24];
    if (toolbar) {
        append_rebound_clip(clips, &count,
                            "fs_VK_toolbar_a_toggle-ON",
                            "P_kyChng_QWERTY",
                            keyboard->phone_layout ? "P_kyChng_CP" :
                                                     "P_kyChng_QWERTY", 0.0f);
    } else {
        if (keyboard->caps) {
            append_rebound_clip(clips, &count, selected_motion[0],
                                "P_key_CAPS", "P_key_CAPS", 0.0f);
        }
        if (keyboard->shift) {
            append_rebound_clip(clips, &count, selected_motion[0],
                                "P_key_SHIFT", "P_key_SHIFT", 0.0f);
        }
    }
    for (unsigned index = 1; index <= WM_KEYBOARD_CONTROL_LAST; index++) {
        KeyboardFocus focus = keyboard->focus[index];
        if (!focus.active ||
            (toolbar ? !is_toolbar((WmBoardKeyboardControl)index) :
                       !is_keytop((WmBoardKeyboardControl)index)) ||
            selected_tab(keyboard, (WmBoardKeyboardControl)index))
            continue;
        const char *target = picture_name((WmBoardKeyboardControl)index,
                                           target_names[index],
                                           keyboard->phone_layout);
        if (!target) continue;
        bool selected = !toolbar &&
            ((index == WM_KEYBOARD_CAPS && keyboard->caps) ||
             (index == WM_KEYBOARD_SHIFT && keyboard->shift));
        append_rebound_clip(clips, &count,
                    focus.resting
                        ? selected ? "fs_VK_ascii_keytop_a_toggle-ON" :
                          toolbar ? "fs_VK_toolbar_a_Roll_over" :
                                    "fs_VK_ascii_keytop_a_Roll_over"
                        : selected ? selected_motion[focus.entering ? 1 : 2] :
                                     motion[focus.entering ? 0 : 1],
                    focus_prototype((WmBoardKeyboardControl)index, target,
                                    keyboard->phone_layout),
                    target, focus.frame);
    }
    if (keyboard->pressed != WM_KEYBOARD_NONE &&
        is_toolbar(keyboard->pressed) == toolbar) {
        unsigned index = (unsigned)keyboard->pressed;
        const char *target = picture_name(keyboard->pressed,
                                           target_names[index],
                                           keyboard->phone_layout);
        bool selected = !toolbar &&
            ((index == WM_KEYBOARD_CAPS && keyboard->caps) ||
             (index == WM_KEYBOARD_SHIFT && keyboard->shift));
        if (target && (toolbar ? is_toolbar(keyboard->pressed) :
                                is_keytop(keyboard->pressed))) {
            append_rebound_clip(clips, &count,
                                selected ? selected_motion[3] : motion[2],
                                focus_prototype(keyboard->pressed, target,
                                                keyboard->phone_layout),
                                target, keyboard->press_frame);
        }
    }
    wm_layout_pose(layout, clips, count);
}

static void pose_keytop(WmBoardKeyboard *keyboard) {
    if (!keyboard->keytop_dirty) return;
    pose_controls(keyboard, false);
    keyboard->keytop_dirty = false;
    static const char *const hidden[] = {
        "N_KeyChange_JP", "N_VK_grid", "N_modeSelect_all",
        "N_modeSelect_kr", "N_VK_grd_Bnd_ALL", "P_SHIFTMark",
        "P_CAPSMark", "P_key_HENKAN"
    };
    for (size_t index = 0; index < sizeof(hidden) / sizeof(hidden[0]);
         index++) wm_layout_set_pane_visible(keyboard->keytop, hidden[index],
                                              false);
    for (unsigned index = 0; index < 50; index++) {
        char pane[24];
        char value[2] = {key_character(keyboard, index), '\0'};
        snprintf(pane, sizeof(pane), "T_key_%02u", index);
        wm_layout_set_pose_text(keyboard->keytop, pane, value);
        if (!value[0]) {
            snprintf(pane, sizeof(pane), "P_key_%02u", index);
            wm_layout_set_pane_visible(keyboard->keytop, pane, false);
        }
    }
    wm_layout_set_pose_text(keyboard->keytop, "T_key_CAPS", "Caps");
    wm_layout_set_pose_text(keyboard->keytop, "T_key_SHIFT", "Shift");
    wm_layout_set_pose_text(keyboard->keytop, "T_key_SPACE", "Space");
    wm_layout_set_pose_text(keyboard->keytop, "T_USEU_Chng_sign", "More");
    wm_layout_set_pose_text(keyboard->keytop, "T_USEU_prdc_lang",
                             language_short[keyboard->dictionary_language]);
    wm_layout_set_pane_visible(keyboard->keytop, "P_prdc_ON",
                                keyboard->prediction_animating
                                    ? keyboard->prediction_from :
                                      keyboard->prediction_enabled);
    wm_layout_set_pane_visible(keyboard->keytop, "P_prdc_OFF",
                                keyboard->prediction_animating
                                    ? !keyboard->prediction_from :
                                      !keyboard->prediction_enabled);
    wm_layout_set_pane_visible(keyboard->keytop, "W_USEU_prdc_lang",
                                keyboard->profile == WM_BOARD_KEYBOARD_MEMO);
    if (keyboard->profile == WM_BOARD_KEYBOARD_ADDRESS_WII ||
        keyboard->profile == WM_BOARD_KEYBOARD_ADDRESS_EMAIL) {
        wm_layout_set_pane_visible(keyboard->keytop,
                                    "W_USEU_Chng_sign", false);
    }
    if (keyboard->profile != WM_BOARD_KEYBOARD_MEMO) {
        wm_layout_set_pane_visible(keyboard->keytop, "P_key_LF", false);
    }
    raise_focused_key(keyboard, keyboard->keytop, false);
}

static void pose_phone(WmBoardKeyboard *keyboard) {
    if (!keyboard->phone_dirty) return;
    WmLayoutClip clips[KEYBOARD_CLIP_CAPACITY];
    size_t count = 0;
    append_clip(clips, &count, "fs_VK_cellPhone_a_normal", NULL, 0.0f);
    char selected[24];
    snprintf(selected, sizeof(selected), "W_ChngTag_%02u",
             keyboard->phone_mode);
    append_rebound_clip(clips, &count, "fs_VK_cellPhone_a_toggle-ON",
                        "W_ChngTag_00", selected, 0.0f);
    char targets[WM_KEYBOARD_CONTROL_LAST + 1][24];
    for (unsigned index = 1; index <= WM_KEYBOARD_CONTROL_LAST; index++) {
        WmBoardKeyboardControl control = (WmBoardKeyboardControl)index;
        KeyboardFocus focus = keyboard->focus[index];
        if (!is_phone_control(keyboard, control) || !focus.active ||
            selected_tab(keyboard, control)) continue;
        const char *target = picture_name(control, targets[index], true);
        if (!target) continue;
        append_rebound_clip(clips, &count,
                    focus.resting ? "fs_VK_cellPhone_a_Roll_over" :
                    focus.entering ? "fs_VK_cellPhone_a_Focus-IN" :
                                     "fs_VK_cellPhone_a_Focus-OUT",
                    focus_prototype(control, target, true), target,
                    focus.frame);
    }
    if (is_phone_control(keyboard, keyboard->pressed)) {
        unsigned index = keyboard->pressed;
        const char *target = picture_name(keyboard->pressed, targets[index],
                                           true);
        if (target) {
            append_rebound_clip(clips, &count,
                                "fs_VK_cellPhone_a_Pushed",
                                focus_prototype(keyboard->pressed, target,
                                                true), target,
                                keyboard->press_frame);
        }
    }
    wm_layout_pose(keyboard->phone, clips, count);
    keyboard->phone_dirty = false;
    static const char *const hidden[] = {
        "N_CP_onlyJP", "W_smlCptChngeBT", "P_CPkey_dakuten",
        "N_prdc_EU_ON"
    };
    for (size_t index = 0; index < sizeof(hidden) / sizeof(hidden[0]);
         index++) wm_layout_set_pane_visible(keyboard->phone, hidden[index],
                                              false);
    wm_layout_set_pane_visible(keyboard->phone, "N_CP_onlyEU",
                               keyboard->phone_mode != 3);
    wm_layout_set_pane_visible(keyboard->phone, "W_prdcModeBT_EU",
                               keyboard->profile == WM_BOARD_KEYBOARD_MEMO &&
                               keyboard->phone_mode != 3);
    wm_layout_set_pane_visible(keyboard->phone, "N_prdc_EU_OFF",
                                keyboard->prediction_animating
                                    ? !keyboard->prediction_from :
                                      !keyboard->prediction_enabled);
    wm_layout_set_pane_visible(keyboard->phone, "N_prdc_EU_ON",
                                keyboard->prediction_animating
                                    ? keyboard->prediction_from :
                                      keyboard->prediction_enabled);
    wm_layout_set_pane_visible(keyboard->phone, "W_othersBT_EU",
                               keyboard->profile != WM_BOARD_KEYBOARD_ADDRESS_WII &&
                               keyboard->profile != WM_BOARD_KEYBOARD_ADDRESS_EMAIL &&
                               keyboard->phone_mode != 3);
    if (keyboard->profile != WM_BOARD_KEYBOARD_MEMO) {
        wm_layout_set_pane_visible(keyboard->phone, "W_CPkey_LF", false);
    }
    if (keyboard->profile == WM_BOARD_KEYBOARD_ADDRESS_WII) {
        for (unsigned index = 0; index < 4; index++) {
            char pane[24];
            snprintf(pane, sizeof(pane), "W_ChngTag_%02u", index);
            wm_layout_set_pane_visible(keyboard->phone, pane, false);
        }
    }
    wm_layout_set_pose_text(keyboard->phone, "T_othersBT_EU", "More");
    wm_layout_set_pose_text(keyboard->phone, "N_prdc_EU_lang",
                             language_short[keyboard->dictionary_language]);
    for (unsigned index = 0; index < 12; index++) {
        char pane[24];
        char label_buffer[16];
        const char *label = phone_label(keyboard, index, label_buffer);
        snprintf(pane, sizeof(pane), "T_CPkey_%02u", index);
        wm_layout_set_pose_text(keyboard->phone, pane, label);
        snprintf(pane, sizeof(pane), "W_CPkey_%02u", index);
        wm_layout_set_pane_visible(keyboard->phone, pane, label[0] != '\0');
    }
    for (unsigned index = 0; index < 4; index++) {
        char pane[24];
        snprintf(pane, sizeof(pane), "T_ChngTag_%02u", index);
        wm_layout_set_pose_text(keyboard->phone, pane, phone_modes[index]);
    }
    raise_focused_key(keyboard, keyboard->phone, true);
}

static void pose_toolbar(WmBoardKeyboard *keyboard) {
    if (!keyboard->toolbar_dirty) return;
    pose_controls(keyboard, true);
    keyboard->toolbar_dirty = false;
    wm_layout_set_pose_text(keyboard->toolbar, "T_BT_cancel",
                              keyboard->profile == WM_BOARD_KEYBOARD_MEMO
                                  ? "Back" : "Quit");
    wm_layout_set_pose_text(keyboard->toolbar, "T_BT_confirm", "OK");
    wm_layout_set_pane_visible(keyboard->toolbar, "N_keyboardChange",
        keyboard->profile == WM_BOARD_KEYBOARD_MEMO ||
        keyboard->profile == WM_BOARD_KEYBOARD_ADDRESS_NICKNAME);
}

static void pose_prediction(WmBoardKeyboard *keyboard) {
    if (!keyboard->prediction_dirty) return;
    WmLayoutClip clips[KEYBOARD_CLIP_CAPACITY];
    size_t count = 0;
    append_clip(clips, &count, "fs_VK_predictInput_a_normal", NULL, 1.0f);
    append_rebound_clip(clips, &count,
        keyboard->prediction_enabled ? "fs_VK_predictInput_a_predict_ON" :
                                       "fs_VK_predictInput_a_Predict_OFF",
        "W_predictWindow", "W_predictWindow",
        keyboard->prediction_animating ? keyboard->prediction_frame : 12.0f);
    KeyboardFocus focus = keyboard->focus[WM_KEYBOARD_PREDICTION];
    const char *button = keyboard->prediction_enabled ? "P_OnBtn" :
                                                         "P_OffBtn";
    if (focus.active) append_rebound_clip(clips, &count,
        focus.resting ? "fs_VK_predictInput_a_Roll_over" :
        focus.entering ? "fs_VK_predictInput_a_Foucus_IN" :
                         "fs_VK_predictInput_a_Focus_OUT",
        button, button, focus.frame);
    if (keyboard->pressed == WM_KEYBOARD_PREDICTION)
        append_rebound_clip(clips, &count,
            "fs_VK_predictInput_a_OnOffButton_Pushed", button, button,
            keyboard->press_frame);
    for (unsigned index = 0; index < CANDIDATE_PANE_COUNT; index++) {
        WmBoardKeyboardControl control = (WmBoardKeyboardControl)(
            WM_KEYBOARD_CANDIDATE_FIRST + index);
        KeyboardFocus candidate_focus = keyboard->focus[control];
        if (!candidate_focus.active || keyboard->candidate_scrolling) continue;
        char candidate_name[24];
        snprintf(candidate_name, sizeof(candidate_name),
                 "T_prdc_Text_%02u", index);
        append_rebound_clip(clips, &count,
            candidate_focus.resting ? "fs_VK_predictInput_a_Roll_over" :
            candidate_focus.entering ? "fs_VK_predictInput_a_Foucus_IN" :
                                       "fs_VK_predictInput_a_Focus_OUT",
            "T_prdc_Text_00", candidate_name,
            candidate_focus.frame);
    }
    if (keyboard->pressed >= WM_KEYBOARD_CANDIDATE_FIRST &&
        keyboard->pressed <= WM_KEYBOARD_CANDIDATE_LAST) {
        unsigned index = keyboard->pressed - WM_KEYBOARD_CANDIDATE_FIRST;
        if (keyboard->candidate_pane_indices[index] <
            keyboard->candidate_count) {
            char candidate_name[24];
            snprintf(candidate_name, sizeof(candidate_name),
                     "T_prdc_Text_%02u", index);
            append_rebound_clip(clips, &count,
                "fs_VK_predictInput_a_Pushed", "T_prdc_Text_00",
                candidate_name, keyboard->press_frame);
        }
    }
    static const WmBoardKeyboardControl arrows[2] = {
        WM_KEYBOARD_CANDIDATE_PREVIOUS,
        WM_KEYBOARD_CANDIDATE_NEXT
    };
    for (unsigned index = 0; index < 2; index++) {
        WmBoardKeyboardControl control = arrows[index];
        const char *picture = index == 0 ? "P_prdc_scrl_Left" :
                                           "P_prdc_scrl_Rght";
        KeyboardFocus arrow_focus = keyboard->focus[control];
        if (arrow_focus.active) append_rebound_clip(clips, &count,
            arrow_focus.resting ? "fs_VK_predictInput_a_Roll_over" :
            arrow_focus.entering ? "fs_VK_predictInput_a_Foucus_IN" :
                                   "fs_VK_predictInput_a_Focus_OUT",
            "P_prdc_scrl_Left", picture, arrow_focus.frame);
        if (keyboard->pressed == control)
            append_rebound_clip(clips, &count,
                "fs_VK_predictInput_a_Pushed", "P_prdc_scrl_Left",
                picture, keyboard->press_frame);
    }
    wm_layout_pose(keyboard->prediction, clips, count);
    static const char *const hidden[] = {
        "P_JPOffBtn", "P_CNOffBtn", "P_CNOnBtn"
    };
    for (size_t index = 0; index < sizeof(hidden) / sizeof(hidden[0]);
         index++) wm_layout_set_pane_visible(keyboard->prediction,
                                              hidden[index], false);
    wm_layout_set_pane_visible(keyboard->prediction, "P_OnBtn",
                                keyboard->prediction_animating
                                    ? keyboard->prediction_from :
                                      keyboard->prediction_enabled);
    wm_layout_set_pane_visible(keyboard->prediction, "P_OffBtn",
                                keyboard->prediction_animating
                                    ? !keyboard->prediction_from :
                                      !keyboard->prediction_enabled);
    wm_layout_set_pane_visible(keyboard->prediction, "N_prdc_Texts",
                                keyboard->candidate_count > 0);
    wm_layout_set_pane_visible(keyboard->prediction, "P_prdc_scrl_Left",
        keyboard->candidate_first > 0);
    wm_layout_set_pane_visible(keyboard->prediction, "P_prdc_scrl_Rght",
        candidate_next_index(keyboard) > keyboard->candidate_first);
    float offset = candidate_offset(keyboard);
    const float area_width = 390.0f;
    for (unsigned index = 0; index < CANDIDATE_PANE_COUNT; index++) {
        char text_name[24];
        char bounds_name[24];
        snprintf(text_name, sizeof(text_name), "T_prdc_Text_%02u", index);
        snprintf(bounds_name, sizeof(bounds_name), "B_prdc_Text_%02u", index);
        keyboard->candidate_pane_indices[index] = CANDIDATE_COUNT;
        wm_layout_set_pane_visible(keyboard->prediction, text_name, false);
        wm_layout_set_pane_size(keyboard->prediction, bounds_name, 0.0f,
                                 38.0f);
    }
    for (unsigned index = 0; index < keyboard->candidate_count; index++) {
        float position = keyboard->candidate_positions[index] - offset;
        float screen_width = keyboard->candidate_screen_widths[index];
        float left = fmaxf(0.0f, position);
        float right = fminf(area_width, position + screen_width);
        if (right <= left) continue;
        unsigned slot = index % CANDIDATE_PANE_COUNT;
        char text_name[24];
        char bounds_name[24];
        snprintf(text_name, sizeof(text_name), "T_prdc_Text_%02u", slot);
        snprintf(bounds_name, sizeof(bounds_name), "B_prdc_Text_%02u", slot);
        keyboard->candidate_pane_indices[slot] = index;
        wm_layout_set_pane_visible(keyboard->prediction, text_name, true);
        wm_layout_set_pose_text(keyboard->prediction, text_name,
                                keyboard->candidates[index]);
        wm_layout_set_pane_size(keyboard->prediction, text_name,
                                 keyboard->candidate_widths[index],
                                 38.0f);
        wm_layout_set_pane_translation(keyboard->prediction, text_name,
                                        -477.0f + position +
                                            screen_width * 0.5f, 0.0f, 0.0f);
        wm_layout_set_pane_size(keyboard->prediction, bounds_name,
                                 right - left, 38.0f);
        wm_layout_set_pane_translation(keyboard->prediction, bounds_name,
            -477.0f + (left + right) * 0.5f, 0.0f, 0.0f);
    }
    if (keyboard->hovered >= WM_KEYBOARD_CANDIDATE_FIRST &&
        keyboard->hovered <= WM_KEYBOARD_CANDIDATE_LAST) {
        unsigned index = keyboard->hovered - WM_KEYBOARD_CANDIDATE_FIRST;
        if (keyboard->candidate_pane_indices[index] <
            keyboard->candidate_count && !keyboard->candidate_scrolling) {
            char name[24];
            snprintf(name, sizeof(name), "T_prdc_Text_%02u", index);
            (void)wm_layout_raise_pane(keyboard->prediction, name);
        }
    }
    keyboard->prediction_dirty = false;
}

static void pose_language(WmBoardKeyboard *keyboard) {
    if (!keyboard->language_dirty) return;
    WmLayoutClip clips[KEYBOARD_CLIP_CAPACITY];
    size_t count = 0;
    append_clip(clips, &count, "fs_prdicSelWidw_a_PRDC_normal", NULL,
                0.0f);
    append_clip(clips, &count, "fs_prdicSelWidw_a_PRDC_FADE-IN", NULL,
                18.0f);
    if (keyboard->language_phase == LANGUAGE_ENTERING ||
        keyboard->language_phase == LANGUAGE_LEAVING)
        append_clip(clips, &count,
            keyboard->language_phase == LANGUAGE_ENTERING
                ? "fs_prdicSelWidw_a_PRDC_FADE-IN" :
                  "fs_prdicSelWidw_a_PRDC_FADE-OUT",
            NULL, keyboard->language_frame);
    char names[WM_KEYBOARD_CONTROL_LAST + 1][24];
    for (unsigned index = WM_KEYBOARD_LANGUAGE_ENGLISH;
         index <= WM_KEYBOARD_LANGUAGE_SPANISH; index++) {
        KeyboardFocus focus = keyboard->focus[index];
        if (!focus.active) continue;
        WmBoardKeyboardControl control = (WmBoardKeyboardControl)index;
        const char *target = picture_name(control, names[index], false);
        append_rebound_clip(clips, &count,
            focus.resting ? "fs_prdicSelWidw_a_PRDC_Roll_over" :
            focus.entering ? "fs_prdicSelWidw_a_PRDC_Focus-IN" :
                             "fs_prdicSelWidw_a_PRDC_Focus-OUT",
            "P_PRDC_US_US", target, focus.frame);
    }
    if (is_language_choice(keyboard->pressed)) {
        const char *target = picture_name(keyboard->pressed,
                                           names[keyboard->pressed], false);
        append_rebound_clip(clips, &count,
            "fs_prdicSelWidw_a_PRDC_Pushed", "P_PRDC_US_US", target,
            keyboard->press_frame);
    }
    wm_layout_pose(keyboard->language, clips, count);
    keyboard->language_dirty = false;
    wm_layout_set_pane_visible(keyboard->language, "N_PRDCkey_EU", false);
    wm_layout_set_pose_text(keyboard->language, "T_PRDC_title", "Dictionary");
    static const char *const text_panes[3] = {
        "T_PRDC_US_US", "T_PRDC_US_Fre", "T_PRDC_US_Spa"
    };
    for (unsigned index = 0; index < 3; index++)
        wm_layout_set_pose_text(keyboard->language, text_panes[index],
                                 language_names[index]);
    if (is_language_choice(keyboard->hovered)) {
        const char *target = picture_name(keyboard->hovered,
                                           names[keyboard->hovered], false);
        (void)wm_layout_raise_pane(keyboard->language, target);
    }
}

static void pose_symbols(WmBoardKeyboard *keyboard) {
    if (!keyboard->symbols_dirty) return;
    WmLayoutClip clips[KEYBOARD_CLIP_CAPACITY];
    size_t count = 0;
    const char *fade_in = "fs_signWindow_a_SGN_FADE-IN";
    append_clip(clips, &count, fade_in, NULL, 18.0f);
    append_rebound_clip(clips, &count, fade_in, "P_SGNkey_close",
                        "P_SGNkey_prev", 18.0f);
    append_rebound_clip(clips, &count, fade_in, "P_SGNkey_close",
                        "P_SGNkey_next", 18.0f);
    const char *transition = NULL;
    switch (keyboard->symbol_phase) {
        case SYMBOL_ENTERING: transition = fade_in; break;
        case SYMBOL_SCROLL_PREV:
            transition = "fs_signWindow_a_SGN_scroll_next";
            break;
        case SYMBOL_SCROLL_NEXT:
            transition = "fs_signWindow_a_SGN_scroll_prev";
            break;
        case SYMBOL_LEAVING:
            transition = "fs_signWindow_a_Scroll_FADE-OUT";
            break;
        case SYMBOL_CLOSED:
        case SYMBOL_OPEN: break;
    }
    if (transition) {
        append_clip(clips, &count, transition, NULL,
                    keyboard->symbol_frame);
        append_rebound_clip(clips, &count, transition, "P_SGNkey_close",
                            "P_SGNkey_prev", keyboard->symbol_frame);
        append_rebound_clip(clips, &count, transition, "P_SGNkey_close",
                            "P_SGNkey_next", keyboard->symbol_frame);
    }
    char target_names[WM_KEYBOARD_CONTROL_LAST + 1][24];
    for (unsigned index = WM_KEYBOARD_SYMBOL_FIRST;
         index <= WM_KEYBOARD_SYMBOL_NEXT; index++) {
        KeyboardFocus focus = keyboard->focus[index];
        if (!focus.active) continue;
        WmBoardKeyboardControl control = (WmBoardKeyboardControl)index;
        const char *target = picture_name(control, target_names[index],
                                          false);
        append_rebound_clip(clips, &count,
            focus.resting ? "fs_signWindow_a_SGN_Roll_over" :
            focus.entering ? "fs_signWindow_a_SGN_Focus-IN" :
                             "fs_signWindow_a_SGN_Focus-OUT",
            focus_prototype(control, target, false), target, focus.frame);
    }
    if (is_symbol(keyboard->pressed)) {
        const char *target = picture_name(keyboard->pressed,
                                           target_names[keyboard->pressed],
                                           false);
        append_rebound_clip(clips, &count, "fs_signWindow_a_SGN_Pushed",
                            focus_prototype(keyboard->pressed, target, false),
                            target, keyboard->press_frame);
    }
    unsigned second_page = keyboard->symbol_phase == SYMBOL_SCROLL_PREV ||
                           keyboard->symbol_phase == SYMBOL_SCROLL_NEXT
                               ? keyboard->symbol_target_page :
                                 keyboard->symbol_page;
    for (unsigned index = 0; index < SYMBOLS_PER_PAGE * 2; index++) {
        char pane[24];
        snprintf(pane, sizeof(pane), "T_SGNkey_%02u", index);
        unsigned page = index < SYMBOLS_PER_PAGE ? keyboard->symbol_page :
                                                  second_page;
        wm_layout_set_text(keyboard->symbols, pane,
                           symbol_text[page][index % SYMBOLS_PER_PAGE]);
    }
    char number[16];
    snprintf(number, sizeof(number), "%u/%u", keyboard->symbol_page + 1,
             SYMBOL_PAGE_COUNT);
    wm_layout_set_text(keyboard->symbols, "T_SGN_pageNumber", number);
    wm_layout_set_text(keyboard->symbols, "T_SGNkey_close", "Close");
    wm_layout_set_text(keyboard->symbols, "T_SGNkey_prev", "←");
    wm_layout_set_text(keyboard->symbols, "T_SGNkey_next", "→");
    wm_layout_pose(keyboard->symbols, clips, count);
    keyboard->symbols_dirty = false;
}

static void position_layout(WmLayout *layout, float y) {
    wm_layout_set_pane_translation(layout, "RootPane", 0.0f, y, 0.0f);
}

typedef struct PredictionDrawPass {
    bool text_pass;
    bool selected_only;
    const char *selected_name;
} PredictionDrawPass;

static bool draw_prediction_pass(void *context, const char *pane_name) {
    const PredictionDrawPass *pass = context;
    bool candidate_text = strncmp(pane_name, "T_prdc_Text_", 12) == 0;
    if (!pass->text_pass) return !candidate_text;
    if (!candidate_text) return false;
    if (!pass->selected_name) return true;
    bool selected = strcmp(pane_name, pass->selected_name) == 0;
    return pass->selected_only ? selected : !selected;
}

static void pose_address_text_box(WmBoardKeyboard *keyboard) {
    bool large = keyboard->profile != WM_BOARD_KEYBOARD_ADDRESS_EMAIL;
    bool numeric = keyboard->profile == WM_BOARD_KEYBOARD_ADDRESS_WII;
    WmLayout *box = large ? keyboard->text_box_big :
                              keyboard->text_box_small;
    WmLayoutClip clip = {
        .animation = large ? NULL : "fs_VK_textBox_a_normal",
        .frame = 0.0f,
        .loop_override = 0
    };
    wm_layout_pose(box, large ? NULL : &clip, large ? 0 : 1);
    wm_layout_set_pose_text(box, "T_2l_TextBox",
                              keyboard->text_context ?
                                  keyboard->text_context : "");
    wm_layout_set_pose_text(box, "T_title_text", "");
    wm_layout_set_pane_visible(box, "P_txtScrll_UP", false);
    wm_layout_set_pane_visible(box, "P_txtScrll_DOWN", false);
    if (large) {
        wm_layout_set_pane_visible(box, "N_separateBarAll", numeric);
        wm_layout_set_pane_visible(box, "N_KOR", false);
        wm_layout_set_pane_visible(box, "N_CHN", false);
        wm_layout_set_pane_visible(box, "N_separateBarKOR", false);
        wm_layout_set_pane_visible(box, "N_separateBarCHN", false);
    }
}

void wm_board_keyboard_draw(WmBoardKeyboard *keyboard, float progress,
                            bool entering) {
    if (!keyboard) return;
    if (keyboard->phone_layout) pose_phone(keyboard);
    else pose_keytop(keyboard);
    pose_toolbar(keyboard);
    pose_prediction(keyboard);
    if (keyboard->language_phase != LANGUAGE_CLOSED)
        pose_language(keyboard);
    progress = fminf(fmaxf(progress, 0.0f), 1.0f);
    float smooth = progress * progress * (3.0f - 2.0f * progress);
    float offset = -200.0f * (1.0f - smooth);
    float opacity = floorf(255.0f * smooth) / 255.0f;
    position_layout(keyboard->prediction, offset);
    WmLayout *active = keyboard->phone_layout ? keyboard->phone :
                                                 keyboard->keytop;
    position_layout(active, offset);
    if (keyboard->profile != WM_BOARD_KEYBOARD_MEMO) {
        WmLayoutClip clip = {
            .animation = "fs_VK_bg_a_normal",
            .frame = 0.0f,
            .loop_override = 0
        };
        wm_layout_pose(keyboard->background, &clip, 1);
        wm_layout_present_with_fonts(keyboard->platform, keyboard->textures,
                                     keyboard->fonts, keyboard->background,
                                     true, WM_LAYOUT_IPL, NULL);
        pose_address_text_box(keyboard);
        WmLayout *box = keyboard->profile != WM_BOARD_KEYBOARD_ADDRESS_EMAIL
            ? keyboard->text_box_big : keyboard->text_box_small;
        wm_layout_present_with_fonts(keyboard->platform, keyboard->textures,
                                     keyboard->fonts, box, true,
                                     WM_LAYOUT_IPL, NULL);
    }
    /* Native toolbar halves move in opposite directions during entrance. */
    position_layout(keyboard->toolbar, 0.0f);
    wm_layout_set_pane_translation(keyboard->toolbar, "N_UP", 0.0f,
                                     -offset / 3.0f, 0.0f);
    wm_layout_set_pane_translation(keyboard->toolbar, "N_DOWN", 0.0f,
                                      offset / 3.0f, 0.0f);
    wm_layout_set_pane_visible(keyboard->toolbar, "N_UP", !entering);
    wm_layout_present_with_fonts_opacity(
        keyboard->platform, keyboard->textures, keyboard->fonts,
        keyboard->toolbar, true, WM_LAYOUT_IPL, NULL, opacity);
    char selected_name[24];
    const char *selected = NULL;
    WmSourceRect area;
    WmSourceRect window;
    bool candidate_area_visible = keyboard->profile == WM_BOARD_KEYBOARD_MEMO &&
        keyboard->candidate_count > 0 &&
        wm_source_pane_rect(keyboard->prediction, "N_prdcTextArea",
            true, WM_LAYOUT_IPL, NULL, &area) &&
        wm_source_pane_rect(keyboard->prediction, "W_predictWindow",
            true, WM_LAYOUT_IPL, NULL, &window);
    if (candidate_area_visible && !keyboard->candidate_scrolling &&
        keyboard->hovered >= WM_KEYBOARD_CANDIDATE_FIRST &&
        keyboard->hovered <= WM_KEYBOARD_CANDIDATE_LAST) {
        unsigned slot = keyboard->hovered - WM_KEYBOARD_CANDIDATE_FIRST;
        if (keyboard->candidate_pane_indices[slot] < keyboard->candidate_count) {
            snprintf(selected_name, sizeof(selected_name),
                     "T_prdc_Text_%02u", slot);
            selected = selected_name;
        }
    }
    if (keyboard->profile == WM_BOARD_KEYBOARD_MEMO) {
        PredictionDrawPass pass = {.selected_name = selected};
        wm_layout_present_with_fonts_opacity_masked(
            keyboard->platform, keyboard->textures, keyboard->fonts,
            keyboard->prediction, true, WM_LAYOUT_IPL, NULL, opacity,
            draw_prediction_pass, &pass);
        if (candidate_area_visible) {
            /* Keep the strip's right edge at the authored text area. Its
             * left glyphs can extend over the rounded window corner. */
            float left = fmaxf(0.0f, window.x - 1.0f);
            WmClipRect clip = {
                .x = left, .y = 0.0f,
                .width = area.x + area.width - left,
                .height = (float)WM_FRAME_HEIGHT
            };
            wm_platform_set_clip(keyboard->platform, &clip);
            pass.text_pass = true;
            wm_layout_present_with_fonts_opacity_masked(
                keyboard->platform, keyboard->textures, keyboard->fonts,
                keyboard->prediction, true, WM_LAYOUT_IPL, NULL, opacity,
                draw_prediction_pass, &pass);
            wm_platform_set_clip(keyboard->platform, NULL);
        }
    }
    wm_layout_present_with_fonts_opacity(
        keyboard->platform, keyboard->textures, keyboard->fonts,
        active, true, WM_LAYOUT_IPL, NULL, opacity);
    if (selected) {
        /* Focus scales the first word across the rounded left edge. Draw
         * every glyph above the keytops and window at its authored position. */
        WmClipRect clip = {
            .x = -1.0f, .y = 0.0f,
            .width = area.x + area.width + 1.0f,
            .height = (float)WM_FRAME_HEIGHT
        };
        PredictionDrawPass pass = {
            .text_pass = true, .selected_only = true,
            .selected_name = selected
        };
        wm_platform_set_clip(keyboard->platform, &clip);
        wm_layout_present_with_fonts_opacity_masked(
            keyboard->platform, keyboard->textures, keyboard->fonts,
            keyboard->prediction, true, WM_LAYOUT_IPL, NULL, opacity,
            draw_prediction_pass, &pass);
        wm_platform_set_clip(keyboard->platform, NULL);
    }
    if (keyboard->symbol_phase != SYMBOL_CLOSED) {
        pose_symbols(keyboard);
        position_layout(keyboard->symbols, offset);
        wm_layout_present_with_fonts_opacity(
            keyboard->platform, keyboard->textures, keyboard->fonts,
            keyboard->symbols, true, WM_LAYOUT_IPL, NULL, opacity);
    }
    if (keyboard->language_phase != LANGUAGE_CLOSED) {
        position_layout(keyboard->language, offset);
        wm_layout_present_with_fonts_opacity(
            keyboard->platform, keyboard->textures, keyboard->fonts,
            keyboard->language, true, WM_LAYOUT_IPL, NULL, opacity);
    }
}

static bool hit_pane(WmLayout *layout, const char *name, int x, int y) {
    WmSourceRect rect;
    return wm_source_pane_rect(layout, name, true, WM_LAYOUT_IPL,
                               NULL, &rect) &&
           (float)x >= rect.x && (float)x < rect.x + rect.width &&
           (float)y >= rect.y && (float)y < rect.y + rect.height;
}

static WmBoardKeyboardControl hit_unfiltered(WmBoardKeyboard *keyboard,
                                              int x, int y) {
    if (!keyboard) return WM_KEYBOARD_NONE;
    if (keyboard->language_phase != LANGUAGE_CLOSED) {
        pose_language(keyboard);
        position_layout(keyboard->language, 0.0f);
        if (keyboard->language_phase != LANGUAGE_OPEN)
            return WM_KEYBOARD_NONE;
        static const char *const panes[3] = {
            "B_PRDC_US_US", "B_PRDC_US_Fre", "B_PRDC_US_Spa"
        };
        for (unsigned index = 0; index < 3; index++)
            if (hit_pane(keyboard->language, panes[index], x, y))
                return (WmBoardKeyboardControl)(
                    WM_KEYBOARD_LANGUAGE_ENGLISH + index);
        return WM_KEYBOARD_NONE;
    }
    if (keyboard->symbol_phase != SYMBOL_CLOSED) {
        pose_symbols(keyboard);
        position_layout(keyboard->symbols, 0.0f);
        if (keyboard->symbol_phase != SYMBOL_OPEN) {
            if ((keyboard->symbol_phase == SYMBOL_SCROLL_PREV ||
                 keyboard->symbol_phase == SYMBOL_SCROLL_NEXT) &&
                (keyboard->hovered == WM_KEYBOARD_SYMBOL_PREV ||
                 keyboard->hovered == WM_KEYBOARD_SYMBOL_NEXT)) {
                const char *pane = keyboard->hovered == WM_KEYBOARD_SYMBOL_PREV
                    ? "B_SGNkey_prev" : "B_SGNkey_next";
                return hit_pane(keyboard->symbols, pane, x, y)
                    ? keyboard->hovered : WM_KEYBOARD_NONE;
            }
            return WM_KEYBOARD_NONE;
        }
        static const struct {
            WmBoardKeyboardControl control;
            const char *pane;
        } actions[] = {
            {WM_KEYBOARD_SYMBOL_CLOSE, "B_SGNkey_close"},
            {WM_KEYBOARD_SYMBOL_PREV, "B_SGNkey_prev"},
            {WM_KEYBOARD_SYMBOL_NEXT, "B_SGNkey_next"}
        };
        for (size_t index = 0; index < sizeof(actions) / sizeof(actions[0]);
             index++) {
            if (hit_pane(keyboard->symbols, actions[index].pane, x, y)) {
                return actions[index].control;
            }
        }
        for (unsigned index = 0; index < SYMBOLS_PER_PAGE; index++) {
            char pane[24];
            snprintf(pane, sizeof(pane), "B_SGNkey_%02u", index);
            if (hit_pane(keyboard->symbols, pane, x, y)) {
                return (WmBoardKeyboardControl)(WM_KEYBOARD_SYMBOL_FIRST +
                                                index);
            }
        }
        return WM_KEYBOARD_NONE;
    }
    if (keyboard->phone_layout) pose_phone(keyboard);
    else pose_keytop(keyboard);
    pose_toolbar(keyboard);
    WmLayout *active = keyboard->phone_layout ? keyboard->phone :
                                                 keyboard->keytop;
    position_layout(active, 0.0f);
    position_layout(keyboard->toolbar, 0.0f);
    wm_layout_set_pane_translation(keyboard->toolbar, "N_UP", 0, 0, 0);
    wm_layout_set_pane_translation(keyboard->toolbar, "N_DOWN", 0, 0, 0);
    if (hit_pane(keyboard->toolbar, "B_BT_cancel", x, y))
        return WM_KEYBOARD_BACK;
    if (hit_pane(keyboard->toolbar, "B_BT_confirm", x, y))
        return WM_KEYBOARD_OK;
    if (hit_pane(keyboard->toolbar, "B_kyChng_QWERTY", x, y))
        return WM_KEYBOARD_QWERTY;
    if (hit_pane(keyboard->toolbar, "B_kyChng_CP", x, y))
        return WM_KEYBOARD_PHONE;
    if (keyboard->profile == WM_BOARD_KEYBOARD_MEMO) {
        pose_prediction(keyboard);
        position_layout(keyboard->prediction, 0.0f);
        if (hit_pane(keyboard->prediction,
            keyboard->prediction_enabled ? "B_OnBtn" : "B_OffBtn", x, y))
            return WM_KEYBOARD_PREDICTION;
        if (keyboard->candidate_first > 0 &&
            hit_pane(keyboard->prediction, "B_prdc_scrl_Left", x, y))
            return WM_KEYBOARD_CANDIDATE_PREVIOUS;
        if (candidate_next_index(keyboard) > keyboard->candidate_first &&
            hit_pane(keyboard->prediction, "B_prdc_scrl_Rght", x, y))
            return WM_KEYBOARD_CANDIDATE_NEXT;
        for (unsigned index = 0; index < CANDIDATE_PANE_COUNT;
             index++) {
            if (keyboard->candidate_pane_indices[index] >=
                keyboard->candidate_count) continue;
            char pane[24];
            snprintf(pane, sizeof(pane), "B_prdc_Text_%02u", index);
            if (hit_pane(keyboard->prediction, pane, x, y))
                return (WmBoardKeyboardControl)(
                    WM_KEYBOARD_CANDIDATE_FIRST + index);
        }
    }
    if (keyboard->phone_layout) {
        if (keyboard->profile == WM_BOARD_KEYBOARD_MEMO &&
            keyboard->phone_mode != 3 &&
            hit_pane(keyboard->phone, "B_prdcModeBT_EU", x, y))
            return WM_KEYBOARD_LANGUAGE;
        if (keyboard->phone_mode != 3 &&
            hit_pane(keyboard->phone, "B_othersBT_EU", x, y))
            return WM_KEYBOARD_MORE;
        if (hit_pane(keyboard->phone, "B_CPkey_DELETE", x, y))
            return WM_KEYBOARD_DELETE;
        if (hit_pane(keyboard->phone, "B_CPkey_LF", x, y))
            return WM_KEYBOARD_RETURN;
        for (unsigned index = 0; index < 4; index++) {
            char pane[24];
            snprintf(pane, sizeof(pane), "B_ChngTag_%02u", index);
            if (hit_pane(keyboard->phone, pane, x, y)) {
                return (WmBoardKeyboardControl)(
                    WM_KEYBOARD_PHONE_MODE_FIRST + index);
            }
        }
        for (unsigned index = 0; index < 12; index++) {
            char pane[24];
            char label_buffer[16];
            if (!phone_label(keyboard, index, label_buffer)[0]) continue;
            snprintf(pane, sizeof(pane), "B_CPkey_%02u", index);
            if (hit_pane(keyboard->phone, pane, x, y)) {
                return (WmBoardKeyboardControl)(WM_KEYBOARD_PHONE_FIRST +
                                                index);
            }
        }
        return WM_KEYBOARD_NONE;
    }
    if (keyboard->profile == WM_BOARD_KEYBOARD_MEMO &&
        hit_pane(keyboard->keytop, "B_USEU_prdc_lang", x, y))
        return WM_KEYBOARD_LANGUAGE;
    if (hit_pane(keyboard->keytop, "B_USEU_Chng_sign", x, y))
        return WM_KEYBOARD_MORE;
    static const struct {
        WmBoardKeyboardControl control;
        const char *pane;
    } special[] = {
        {WM_KEYBOARD_DELETE, "B_key_DELETE"},
        {WM_KEYBOARD_RETURN, "B_key_LF"},
        {WM_KEYBOARD_CAPS, "B_key_CAPS"},
        {WM_KEYBOARD_SHIFT, "B_key_SHIFT"},
        {WM_KEYBOARD_SPACE, "B_key_SPACE"}
    };
    for (size_t index = 0; index < sizeof(special) / sizeof(special[0]);
         index++) {
        if (hit_pane(keyboard->keytop, special[index].pane, x, y))
            return special[index].control;
    }
    for (unsigned index = 0; index < 50; index++) {
        if (!key_character(keyboard, index)) continue;
        char name[24];
        snprintf(name, sizeof(name), "B_key_%02u", index);
        if (hit_pane(keyboard->keytop, name, x, y)) {
            return (WmBoardKeyboardControl)(WM_KEYBOARD_CHARACTER_FIRST +
                                            index);
        }
    }
    return WM_KEYBOARD_NONE;
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
    WmBoardKeyboardControl control = hit_unfiltered(keyboard, x, y);
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
        pose_prediction(keyboard);
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
                     symbol_text[keyboard->symbol_page]
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
              candidate_next_index(keyboard);
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
        pose_prediction(keyboard);
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
            const char *label = phone_label(keyboard, index, label_buffer);
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
        utf8[0] = key_character(keyboard,
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
