#include "board_keyboard_prediction.h"
#include "keyboard_text.h"

#include <ctype.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

enum { DICTIONARY_PATH_CAPACITY = 4096 };

/* US phone-key cycles for local text entry. */
static const char *const phone_cycles[12] = {".,?!-':@/$#&1", "abc2", "def3",  "ghi4",
                                             "jkl5",          "mno6", "pqrs7", "tuv8",
                                             "wxyz9",         "",     " 0",    ""};

static const char *const phone_labels[12] = {".,?@",
                                             "abc",
                                             "def",
                                             "ghi",
                                             "jkl",
                                             "mno",
                                             "pqrs",
                                             "tuv",
                                             "wxyz",
                                             "",
                                             "\xee\x81\x97\x30",
                                             ""};

/* A compact local fallback vocabulary substitutes for Zi8 and requires no
 * extracted dictionary data. */
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
    "tu tú un una uno usted vamos ver vez yo"};

static const char *const oem_dictionary_names[3] = {
    "eZTNintendoENAM.znd", "eZTNintendoFRCA.znd", "eZTNintendoESSA.znd"};

const char *wm_board_keyboard_prediction_phone_cycle(unsigned index) {
    return index < 12 ? phone_cycles[index] : "";
}

void wm_board_keyboard_prediction_load_oem(WmBoardKeyboard *keyboard,
                                           const char *assets_directory) {
    if (!keyboard || !assets_directory)
        return;
    for (unsigned language = 0; language < 3; language++) {
        char path[DICTIONARY_PATH_CAPACITY];
        int length = snprintf(path, sizeof(path), "%s/keyboard-dictionary/%s",
                              assets_directory, oem_dictionary_names[language]);
        if (length <= 0 || length >= (int)sizeof(path))
            continue;
        char error[160] = {0};
        (void)wm_keyboard_oem_load(path, &keyboard->oem_words[language], error,
                                   sizeof(error));
    }
}

void wm_board_keyboard_prediction_clear_learned(WmBoardKeyboard *keyboard) {
    if (!keyboard)
        return;
    for (size_t index = 0; index < keyboard->learned_count; index++)
        free(keyboard->learned_words[index]);
    keyboard->learned_count = 0;
}

void wm_board_keyboard_prediction_release(WmBoardKeyboard *keyboard) {
    if (!keyboard)
        return;
    for (unsigned language = 0; language < 3; language++)
        wm_keyboard_word_list_free(&keyboard->oem_words[language]);
    wm_board_keyboard_prediction_clear_learned(keyboard);
    free(keyboard->learned_words);
    keyboard->learned_words = NULL;
    keyboard->learned_capacity = 0;
}

typedef struct VocabularyCursor {
    unsigned stage;
    size_t index;
    const char *fallback;
} VocabularyCursor;

static bool next_vocabulary_word(const WmBoardKeyboard *keyboard,
                                 VocabularyCursor *cursor, const char **word,
                                 size_t *length) {
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
                cursor->fallback = dictionary_words[keyboard->dictionary_language];
            while (*cursor->fallback == ' ')
                cursor->fallback++;
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

bool wm_board_keyboard_prediction_phone_uppercase(const WmBoardKeyboard *keyboard) {
    if (keyboard->phone_mode == 2)
        return true;
    if (keyboard->phone_mode != 0)
        return false;
    const char *text = keyboard->text_context ? keyboard->text_context : "";
    size_t length = strlen(text);
    if (length == 0)
        return true;
    while (length > 0 && (text[length - 1] == ' ' || text[length - 1] == '\t' ||
                          text[length - 1] == '\n' || text[length - 1] == '\r'))
        length--;
    return length > 0 && (text[length - 1] == '.' || text[length - 1] == '!' ||
                          text[length - 1] == '?');
}

const char *wm_board_keyboard_phone_label(const WmBoardKeyboard *keyboard,
                                          unsigned index, char output[16]) {
    if (index >= 12)
        return "";
    if (keyboard->phone_mode == 3) {
        if (index < 9) {
            output[0] = (char)('1' + index);
            output[1] = '\0';
            return output;
        }
        return index == 9 ? "," : index == 10 ? "0" : "*";
    }
    const char *label = phone_labels[index];
    if (!wm_board_keyboard_prediction_phone_uppercase(keyboard))
        return label;
    size_t length = strlen(label);
    if (length >= 16)
        return label;
    for (size_t position = 0; position <= length; position++) {
        char value = label[position];
        output[position] =
            value >= 'a' && value <= 'z' ? (char)(value - 'a' + 'A') : value;
    }
    return output;
}

static void learn_word(WmBoardKeyboard *keyboard, const char *start, size_t bytes,
                       size_t points, size_t units) {
    if (points <= 1 || units > 64 || bytes >= WM_KEYBOARD_CANDIDATE_UTF8_CAPACITY)
        return;
    char lower[WM_KEYBOARD_CANDIDATE_UTF8_CAPACITY];
    size_t used = 0;
    const char *cursor = start;
    const char *end = start + bytes;
    while (cursor < end) {
        char encoded[4];
        uint32_t point = wm_keyboard_text_next_codepoint(&cursor, end);
        size_t encoded_bytes =
            wm_keyboard_text_encode_point(encoded, wm_keyboard_text_lower_point(point));
        if (used + encoded_bytes >= sizeof(lower))
            return;
        memcpy(lower + used, encoded, encoded_bytes);
        used += encoded_bytes;
    }
    lower[used] = '\0';
    for (size_t index = 0; index < keyboard->learned_count; index++)
        if (strcmp(keyboard->learned_words[index], lower) == 0)
            return;
    if (keyboard->learned_count >= 2048)
        return;
    if (keyboard->learned_count == keyboard->learned_capacity) {
        size_t capacity =
            keyboard->learned_capacity ? keyboard->learned_capacity * 2 : 32;
        char **grown = realloc(keyboard->learned_words, capacity * sizeof(*grown));
        if (!grown)
            return;
        keyboard->learned_words = grown;
        keyboard->learned_capacity = capacity;
    }
    char *copy = malloc(used + 1);
    if (!copy)
        return;
    memcpy(copy, lower, used + 1);
    keyboard->learned_words[keyboard->learned_count++] = copy;
}

void wm_board_keyboard_prediction_learn_words(WmBoardKeyboard *keyboard,
                                              const char *text, bool include_trailing) {
    if (!text)
        return;
    const char *cursor = text;
    const char *end = text + strlen(text);
    const char *word = NULL;
    size_t points = 0, units = 0;
    while (cursor < end) {
        const char *before = cursor;
        uint32_t point = wm_keyboard_text_next_codepoint(&cursor, end);
        if (wm_keyboard_text_is_word_point(point)) {
            if (!word)
                word = before;
            points++;
            units += point > 0xffffu ? 2u : 1u;
        } else if (word) {
            learn_word(keyboard, word, (size_t)(before - word), points, units);
            word = NULL;
            points = 0;
            units = 0;
        }
    }
    if (word && include_trailing)
        learn_word(keyboard, word, (size_t)(end - word), points, units);
}

void wm_board_keyboard_prediction_phone_value(WmBoardKeyboard *keyboard,
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
        if (!wm_keyboard_text_phone_digits_match(word, length, digits, &exact)) {
            continue;
        }
        if (!chosen || (!chosen_exact && exact)) {
            chosen = word;
            chosen_length = length;
            chosen_exact = exact;
        }
        if (chosen_exact)
            break;
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
        uint32_t first =
            wm_keyboard_text_next_codepoint(&part, output + strlen(output));
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

void wm_board_keyboard_prediction_rollback_phone(WmBoardKeyboard *keyboard) {
    if (!keyboard->phone_prediction_awaiting_edit)
        return;
    memcpy(keyboard->phone_prediction_digits,
           keyboard->phone_prediction_previous_digits,
           sizeof(keyboard->phone_prediction_digits));
    keyboard->phone_prediction_bytes = keyboard->phone_prediction_previous_bytes;
    keyboard->phone_prediction_awaiting_edit = false;
}

static void measure_candidates(WmBoardKeyboard *keyboard) {
    WmLayoutPaneState pane;
    bool measured = wm_layout_pane_state(keyboard->prediction, "T_prdc_Text_00", &pane);
    float position = 0.0f;
    for (unsigned index = 0; index < keyboard->candidate_count; index++) {
        const char *value = keyboard->candidates[index];
        float width =
            measured ? wm_font_cache_measure_text(keyboard->fonts, keyboard->prediction,
                                                  &pane, value, strlen(value))
                     : 0.0f;
        if (width < 20.0f)
            width = (float)strlen(value) * 14.0f;
        width += 0.01f;
        keyboard->candidate_widths[index] = width;
        keyboard->candidate_screen_widths[index] = width * (608.0f / 832.0f);
        keyboard->candidate_positions[index] = position;
        position += keyboard->candidate_screen_widths[index] + 10.0f;
    }
}

unsigned wm_board_keyboard_candidate_next_index(const WmBoardKeyboard *keyboard) {
    if (keyboard->candidate_count == 0)
        return 0;
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

unsigned wm_board_keyboard_candidate_previous_index(const WmBoardKeyboard *keyboard) {
    if (keyboard->candidate_first == 0)
        return 0;
    int index = (int)keyboard->candidate_first - 1;
    float width = -10.0f;
    while (width <= 390.0f && index >= 0) {
        width += keyboard->candidate_screen_widths[index] + 10.0f;
        index--;
    }
    unsigned target = (unsigned)(index + 2);
    return target < keyboard->candidate_first ? target : keyboard->candidate_first - 1;
}

static bool candidate_already_added(const WmBoardKeyboard *keyboard,
                                    const char *candidate) {
    for (unsigned index = 0; index < keyboard->candidate_count; index++)
        if (strcmp(keyboard->candidates[index], candidate) == 0)
            return true;
    return false;
}

void wm_board_keyboard_prediction_refresh(WmBoardKeyboard *keyboard) {
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
    if (!keyboard->prediction_enabled || keyboard->profile != WM_BOARD_KEYBOARD_MEMO ||
        !keyboard->text_context)
        return;
    const char *text = keyboard->text_context;
    size_t end = strlen(text);
    size_t start = keyboard->composition_start;
    bool phone_digits = keyboard->phone_layout && keyboard->phone_prediction_digits[0];
    if (phone_digits) {
        if (keyboard->phone_prediction_bytes > end)
            return;
        start = end - keyboard->phone_prediction_bytes;
    } else if (start == SIZE_MAX || start > end)
        return;
    size_t length = end - start;
    if (length == 0 || length >= sizeof(keyboard->candidates[0]))
        return;
    keyboard->candidate_prefix_bytes = length;
    unsigned passes = phone_digits ? 2 : 1;
    for (unsigned pass = 0;
         pass < passes && keyboard->candidate_count < LOCAL_CANDIDATE_LIMIT; pass++) {
        VocabularyCursor cursor = {0};
        const char *word;
        size_t word_length;
        while (next_vocabulary_word(keyboard, &cursor, &word, &word_length) &&
               keyboard->candidate_count < LOCAL_CANDIDATE_LIMIT) {
            if (word_length == 0 || word_length >= WM_KEYBOARD_CANDIDATE_UTF8_CAPACITY)
                continue;
            if (phone_digits) {
                bool exact = false;
                if (!wm_keyboard_text_phone_digits_match(
                        word, word_length, keyboard->phone_prediction_digits, &exact) ||
                    exact != (pass == 0)) {
                    continue;
                }
            } else if (!wm_keyboard_text_prefix_matches(word, word_length, text + start,
                                                        length) ||
                       wm_keyboard_text_prefix_matches(text + start, length, word,
                                                       word_length)) {
                continue;
            }
            char candidate[WM_KEYBOARD_CANDIDATE_UTF8_CAPACITY];
            if (!wm_keyboard_text_copy_candidate_case(
                    candidate, sizeof(candidate), word, word_length, text + start,
                    length, phone_digits, keyboard->phone_prediction_uppercase) ||
                candidate_already_added(keyboard, candidate)) {
                continue;
            }
            memcpy(keyboard->candidates[keyboard->candidate_count++], candidate,
                   strlen(candidate) + 1);
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

void wm_board_keyboard_set_text_context(WmBoardKeyboard *keyboard, const char *utf8) {
    if (!keyboard)
        return;
    wm_board_keyboard_prediction_clear_learned(keyboard);
    keyboard->text_context = utf8;
    keyboard->observed_text_bytes = utf8 ? strlen(utf8) : 0;
    keyboard->composition_start = SIZE_MAX;
    wm_board_keyboard_prediction_learn_words(keyboard, utf8, true);
    keyboard->phone_dirty = true;
    wm_board_keyboard_prediction_refresh(keyboard);
}

void wm_board_keyboard_clear_phone_pending(WmBoardKeyboard *keyboard) {
    if (!keyboard)
        return;
    keyboard->phone_pending = false;
    keyboard->phone_pending_frames = 0.0f;
    keyboard->phone_prediction_digits[0] = '\0';
    keyboard->phone_prediction_bytes = 0;
    keyboard->phone_prediction_awaiting_edit = false;
    wm_board_keyboard_prediction_refresh(keyboard);
}

void wm_board_keyboard_text_changed(WmBoardKeyboard *keyboard, bool from_phone_key) {
    if (!keyboard)
        return;
    keyboard->phone_dirty = true;
    if (!from_phone_key) {
        wm_board_keyboard_clear_phone_pending(keyboard);
    } else
        keyboard->phone_prediction_awaiting_edit = false;
    size_t end = keyboard->text_context ? strlen(keyboard->text_context) : 0;
    if (keyboard->prediction_enabled && end > keyboard->observed_text_bytes) {
        size_t begin = keyboard->composition_start == SIZE_MAX
                           ? keyboard->observed_text_bytes
                           : keyboard->composition_start;
        for (size_t index = keyboard->observed_text_bytes; index < end; index++)
            if (isspace((unsigned char)keyboard->text_context[index]))
                begin = index + 1;
        keyboard->composition_start = begin == end ? SIZE_MAX : begin;
    } else if (keyboard->composition_start >= end ||
               (end > 0 && isspace((unsigned char)keyboard->text_context[end - 1]))) {
        keyboard->composition_start = SIZE_MAX;
    }
    keyboard->observed_text_bytes = end;
    wm_board_keyboard_prediction_learn_words(keyboard, keyboard->text_context, false);
    wm_board_keyboard_prediction_refresh(keyboard);
}

void wm_board_keyboard_finish_composition(WmBoardKeyboard *keyboard) {
    if (!keyboard)
        return;
    keyboard->composition_start = SIZE_MAX;
    keyboard->observed_text_bytes =
        keyboard->text_context ? strlen(keyboard->text_context) : 0;
    keyboard->phone_prediction_digits[0] = '\0';
    keyboard->phone_prediction_bytes = 0;
    keyboard->phone_prediction_awaiting_edit = false;
    wm_board_keyboard_prediction_refresh(keyboard);
}

const char *wm_board_keyboard_candidate_text(const WmBoardKeyboard *keyboard) {
    return keyboard ? keyboard->accepted_candidate : "";
}

size_t wm_board_keyboard_candidate_prefix_bytes(const WmBoardKeyboard *keyboard) {
    return keyboard ? keyboard->accepted_prefix_bytes : 0;
}

bool wm_board_keyboard_composition(const WmBoardKeyboard *keyboard,
                                   WmBoardKeyboardComposition *composition) {
    if (!keyboard || !composition)
        return false;
    *composition = (WmBoardKeyboardComposition){0};
    if (!keyboard->prediction_enabled || keyboard->candidate_count == 0 ||
        keyboard->candidate_prefix_bytes == 0)
        return false;
    composition->prefix_bytes = keyboard->candidate_prefix_bytes;
    unsigned selected = keyboard->selected_candidate_index;
    if (selected >= keyboard->candidate_count)
        selected = 0;
    composition->selected_candidate = keyboard->candidates[selected];
    unsigned preview = keyboard->candidate_count;
    if (!keyboard->candidate_scrolling &&
        keyboard->hovered >= WM_KEYBOARD_CANDIDATE_FIRST &&
        keyboard->hovered <= WM_KEYBOARD_CANDIDATE_LAST) {
        unsigned slot = keyboard->hovered - WM_KEYBOARD_CANDIDATE_FIRST;
        if (keyboard->candidate_pane_indices[slot] < keyboard->candidate_count) {
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
                wm_keyboard_text_prefix_matches(word, strlen(word), text + start,
                                                composition->prefix_bytes)) {
                preview = index;
                break;
            }
        }
    }
    composition->preview_candidate =
        preview < keyboard->candidate_count ? keyboard->candidates[preview] : NULL;
    return true;
}
