#include "keyboard_text.h"

#include <string.h>

uint32_t wm_keyboard_text_next_codepoint(const char **cursor, const char *end) {
    if (*cursor >= end) {
        return 0;
    }

    const unsigned char *bytes = (const unsigned char *)*cursor;
    size_t available = (size_t)(end - *cursor);
    unsigned count;
    uint32_t point;

    if (bytes[0] < 0x80u) {
        (*cursor)++;
        return bytes[0];
    }
    if (bytes[0] >= 0xc2u && bytes[0] <= 0xdfu) {
        count = 2;
        point = bytes[0] & 0x1fu;
    } else if (bytes[0] >= 0xe0u && bytes[0] <= 0xefu) {
        count = 3;
        point = bytes[0] & 0x0fu;
    } else if (bytes[0] >= 0xf0u && bytes[0] <= 0xf4u) {
        count = 4;
        point = bytes[0] & 0x07u;
    } else {
        (*cursor)++;
        return 0xfffdu;
    }

    if (available < count) {
        (*cursor)++;
        return 0xfffdu;
    }
    for (unsigned index = 1; index < count; index++) {
        if ((bytes[index] & 0xc0u) != 0x80u) {
            (*cursor)++;
            return 0xfffdu;
        }
        point = (point << 6) | (bytes[index] & 0x3fu);
    }

    /* Scalar checks also reject overlong encodings and UTF-16 surrogates. */
    if ((count == 2 && point < 0x80u) || (count == 3 && point < 0x800u) ||
        (count == 4 && point < 0x10000u) || (point >= 0xd800u && point <= 0xdfffu) ||
        point > 0x10ffffu) {
        (*cursor)++;
        return 0xfffdu;
    }

    *cursor += count;
    return point;
}

uint32_t wm_keyboard_text_lower_point(uint32_t point) {
    if (point >= 'A' && point <= 'Z') {
        return point + 0x20u;
    }
    if ((point >= 0xc0u && point <= 0xd6u) || (point >= 0xd8u && point <= 0xdeu)) {
        return point + 0x20u;
    }
    return point == 0x178u ? 0xffu : point;
}

uint32_t wm_keyboard_text_upper_point(uint32_t point) {
    if (point >= 'a' && point <= 'z') {
        return point - 0x20u;
    }
    if ((point >= 0xe0u && point <= 0xf6u) || (point >= 0xf8u && point <= 0xfeu)) {
        return point - 0x20u;
    }
    return point == 0xffu ? 0x178u : point;
}

size_t wm_keyboard_text_encode_point(char output[4], uint32_t point) {
    if (point > 0x10ffffu || (point >= 0xd800u && point <= 0xdfffu)) {
        point = 0xfffdu;
    }
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

bool wm_keyboard_text_is_word_point(uint32_t point) {
    if ((point >= 'A' && point <= 'Z') || (point >= 'a' && point <= 'z')) {
        return true;
    }
    return (point >= 0xc0u && point <= 0xffu && point != 0xd7u && point != 0xf7u) ||
           (point >= 0x100u && point <= 0x24fu) ||
           (point >= 0x300u && point <= 0x36fu) || (point >= 0x370u && point <= 0x3ffu);
}

bool wm_keyboard_text_prefix_matches(const char *word, size_t word_bytes,
                                     const char *prefix, size_t prefix_bytes) {
    const char *word_cursor = word;
    const char *word_end = word + word_bytes;
    const char *prefix_cursor = prefix;
    const char *prefix_end = prefix + prefix_bytes;

    while (prefix_cursor < prefix_end) {
        if (word_cursor >= word_end) {
            return false;
        }
        uint32_t word_point = wm_keyboard_text_next_codepoint(&word_cursor, word_end);
        uint32_t prefix_point =
            wm_keyboard_text_next_codepoint(&prefix_cursor, prefix_end);
        if (wm_keyboard_text_lower_point(word_point) !=
            wm_keyboard_text_lower_point(prefix_point)) {
            return false;
        }
    }
    return true;
}

static char phone_digit_for_letter(uint32_t letter) {
    /* One lookup per Latin letter keeps candidate scans inexpensive. */
    static const char digits_by_letter[] = "22233344455566677778889999";

    letter = wm_keyboard_text_lower_point(letter);
    if (letter >= 0xe0u && letter <= 0xe5u) {
        letter = 'a';
    } else if (letter == 0xe7u) {
        letter = 'c';
    } else if (letter >= 0xe8u && letter <= 0xebu) {
        letter = 'e';
    } else if (letter >= 0xecu && letter <= 0xefu) {
        letter = 'i';
    } else if (letter == 0xf1u) {
        letter = 'n';
    } else if (letter >= 0xf2u && letter <= 0xf6u) {
        letter = 'o';
    } else if (letter >= 0xf9u && letter <= 0xfcu) {
        letter = 'u';
    } else if (letter == 0xfdu || letter == 0xffu) {
        letter = 'y';
    }

    if (letter < 'a' || letter > 'z') {
        return '\0';
    }
    return digits_by_letter[letter - 'a'];
}

bool wm_keyboard_text_phone_digits_match(const char *word, size_t word_bytes,
                                         const char *digits, bool *exact) {
    if (exact) {
        *exact = false;
    }
    size_t count = strlen(digits);
    size_t matched = 0;
    const char *cursor = word;
    const char *end = word + word_bytes;

    while (cursor < end) {
        uint32_t point = wm_keyboard_text_next_codepoint(&cursor, end);
        char digit = phone_digit_for_letter(point);
        if (digit == '\0') {
            continue;
        }
        if (matched < count && digit != digits[matched]) {
            return false;
        }
        matched++;
    }
    if (exact) {
        *exact = matched == count;
    }
    return matched >= count;
}

bool wm_keyboard_text_copy_candidate_case(char *output, size_t output_capacity,
                                          const char *word, size_t word_bytes,
                                          const char *prefix, size_t prefix_bytes,
                                          bool phone_digits, bool phone_uppercase) {
    if (output_capacity == 0) {
        return false;
    }

    /* Match the predictor's all-caps and initial-capital choices. */
    bool all_upper = !phone_digits;
    bool first_upper = phone_digits && phone_uppercase;
    if (!phone_digits) {
        const char *cursor = prefix;
        const char *end = prefix + prefix_bytes;
        if (cursor < end) {
            uint32_t point = wm_keyboard_text_next_codepoint(&cursor, end);
            first_upper = wm_keyboard_text_upper_point(point) == point;
            if (wm_keyboard_text_lower_point(point) == point &&
                wm_keyboard_text_upper_point(point) != point) {
                all_upper = false;
            }
        }
        while (cursor < end) {
            uint32_t point = wm_keyboard_text_next_codepoint(&cursor, end);
            if (wm_keyboard_text_lower_point(point) == point &&
                wm_keyboard_text_upper_point(point) != point) {
                all_upper = false;
            }
        }
    }

    const char *cursor = word;
    const char *end = word + word_bytes;
    size_t used = 0;
    bool first = true;
    while (cursor < end) {
        uint32_t point = wm_keyboard_text_next_codepoint(&cursor, end);
        point = wm_keyboard_text_lower_point(point);
        if (all_upper || (first && first_upper)) {
            point = wm_keyboard_text_upper_point(point);
        }

        char encoded[4];
        size_t bytes = wm_keyboard_text_encode_point(encoded, point);
        if (bytes >= output_capacity - used) {
            return false;
        }
        memcpy(output + used, encoded, bytes);
        used += bytes;
        first = false;
    }
    output[used] = '\0';
    return used > 0;
}
