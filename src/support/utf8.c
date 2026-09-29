#include "wii_menu/support/utf8.h"

#include <string.h>

bool wm_utf8_next(const uint8_t *bytes, size_t length, size_t *offset,
                  uint32_t *codepoint, size_t *utf16_units) {
    if (!bytes || !offset || *offset >= length)
        return false;

    uint8_t first = bytes[*offset];
    size_t count = first < 0x80                     ? 1
                   : first >= 0xC2 && first <= 0xDF ? 2
                   : first >= 0xE0 && first <= 0xEF ? 3
                   : first >= 0xF0 && first <= 0xF4 ? 4
                                                    : 0;
    if (count == 0 || count > length - *offset)
        return false;

    uint32_t value = first & (count == 1   ? 0x7Fu
                              : count == 2 ? 0x1Fu
                              : count == 3 ? 0x0Fu
                                           : 0x07u);
    for (size_t index = 1; index < count; index++) {
        uint8_t next = bytes[*offset + index];
        if ((next & 0xC0u) != 0x80u)
            return false;
        value = (value << 6) | (next & 0x3Fu);
    }
    if ((count == 2 && value < 0x80u) || (count == 3 && value < 0x800u) ||
        (count == 4 && value < 0x10000u) || value > 0x10FFFFu ||
        (value >= 0xD800u && value <= 0xDFFFu))
        return false;

    *offset += count;
    if (codepoint)
        *codepoint = value;
    if (utf16_units)
        *utf16_units = count == 4 ? 2 : 1;
    return true;
}

bool wm_utf8_valid(const uint8_t *bytes, size_t length) {
    if (!bytes)
        return length == 0;
    size_t offset = 0;
    while (offset < length) {
        if (bytes[offset] < 0x80) {
            offset++;
            continue;
        }
        if (!wm_utf8_next(bytes, length, &offset, NULL, NULL))
            return false;
    }
    return true;
}

bool wm_utf8_cstr_valid(const char *text) {
    return text && wm_utf8_valid((const uint8_t *)text, strlen(text));
}
