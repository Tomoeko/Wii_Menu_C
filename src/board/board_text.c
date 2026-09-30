#include "board_text.h"

#include "wii_menu/support/utf8.h"

#include <stdint.h>
#include <string.h>

bool wm_board_text_character(const char *text, size_t remaining, size_t *bytes,
                             size_t *utf16_units) {
    if (!text || remaining == 0 || !bytes || !utf16_units)
        return false;

    const unsigned char lead = (unsigned char)text[0];
    if (lead < 0x80u) {
        *bytes = 1;
        *utf16_units = 1;
        return true;
    }

    size_t offset = 0;
    size_t units;
    if (!wm_utf8_next((const uint8_t *)text, remaining, &offset, NULL, &units))
        return false;
    *bytes = offset;
    *utf16_units = units;
    return true;
}

size_t wm_board_text_utf16_units(const char *text, size_t bytes) {
    if (!text)
        return SIZE_MAX;
    size_t units = 0;
    for (size_t offset = 0; offset < bytes;) {
        size_t character_bytes;
        size_t character_units;
        if (!wm_board_text_character(text + offset, bytes - offset, &character_bytes,
                                     &character_units)) {
            return SIZE_MAX;
        }
        units += character_units;
        offset += character_bytes;
    }
    return units;
}

bool wm_board_text_memo_input(const char *text, size_t maximum_bytes, size_t *bytes,
                              bool *has_whitespace) {
    if (!text || !bytes || !has_whitespace)
        return false;
    size_t length = 0;
    while (length < maximum_bytes && text[length] != '\0')
        length++;
    if (length == 0 || text[length] != '\0')
        return false;

    bool whitespace = false;
    for (size_t offset = 0; offset < length;) {
        size_t character_bytes;
        size_t character_units;
        if (!wm_board_text_character(text + offset, length - offset, &character_bytes,
                                     &character_units)) {
            return false;
        }
        unsigned char first = (unsigned char)text[offset];
        if (first < 0x20u && first != '\n')
            return false;
        if (first == ' ' || first == '\n')
            whitespace = true;
        offset += character_bytes;
    }
    *bytes = length;
    *has_whitespace = whitespace;
    return true;
}

static bool valid_draft(const char *text, size_t capacity, const size_t *text_bytes,
                        const size_t *caret_bytes) {
    return text && capacity > 0 && text_bytes && caret_bytes &&
           *text_bytes < capacity && *caret_bytes <= *text_bytes &&
           text[*text_bytes] == '\0' &&
           (*caret_bytes == *text_bytes ||
            ((unsigned char)text[*caret_bytes] & 0xc0u) != 0x80u);
}

bool wm_board_text_insert(char *text, size_t capacity, size_t *text_bytes,
                          size_t *caret_bytes, const char *inserted,
                          size_t inserted_bytes) {
    if (!valid_draft(text, capacity, text_bytes, caret_bytes) || !inserted ||
        inserted_bytes == 0 || inserted_bytes >= capacity - *text_bytes) {
        return false;
    }

    memmove(text + *caret_bytes + inserted_bytes, text + *caret_bytes,
            *text_bytes - *caret_bytes + 1);
    memcpy(text + *caret_bytes, inserted, inserted_bytes);
    *text_bytes += inserted_bytes;
    *caret_bytes += inserted_bytes;
    return true;
}

bool wm_board_text_replace_before_caret(char *text, size_t capacity, size_t *text_bytes,
                                        size_t *caret_bytes, size_t prefix_bytes,
                                        const char *replacement,
                                        size_t replacement_bytes) {
    if (!valid_draft(text, capacity, text_bytes, caret_bytes) || !replacement ||
        prefix_bytes > *caret_bytes)
        return false;
    if (wm_board_text_utf16_units(replacement, replacement_bytes) == SIZE_MAX)
        return false;

    size_t start = *caret_bytes - prefix_bytes;
    if (start < *text_bytes && ((unsigned char)text[start] & 0xc0u) == 0x80u)
        return false;
    size_t retained = *text_bytes - prefix_bytes;
    if (replacement_bytes >= capacity - retained)
        return false;

    memmove(text + start + replacement_bytes, text + *caret_bytes,
            *text_bytes - *caret_bytes + 1);
    memcpy(text + start, replacement, replacement_bytes);
    *text_bytes = retained + replacement_bytes;
    *caret_bytes = start + replacement_bytes;
    return true;
}

bool wm_board_text_backspace(char *text, size_t capacity, size_t *text_bytes,
                             size_t *caret_bytes) {
    if (!valid_draft(text, capacity, text_bytes, caret_bytes) || *caret_bytes == 0)
        return false;

    size_t start = *caret_bytes - 1;
    while (start > 0 && ((unsigned char)text[start] & 0xc0u) == 0x80u) {
        start--;
    }
    size_t character_bytes;
    size_t character_units;
    if (!wm_board_text_character(text + start, *caret_bytes - start, &character_bytes,
                                 &character_units) ||
        character_bytes != *caret_bytes - start)
        return false;

    memmove(text + start, text + *caret_bytes, *text_bytes - *caret_bytes + 1);
    *text_bytes -= character_bytes;
    *caret_bytes = start;
    return true;
}
