#include "wii_menu/keyboard_dictionary.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

enum {
    OEM_MAX_WORDS = 10000,
    OEM_MAX_FILE_BYTES = 16 * 1024 * 1024,
    OEM_MAX_UTF16_UNITS = 64,
    OEM_MAX_UTF8_BYTES = OEM_MAX_UTF16_UNITS * 3
};

static uint16_t read_be16(const uint8_t *bytes) {
    return (uint16_t)(((uint16_t)bytes[0] << 8) | bytes[1]);
}

static uint32_t read_be32(const uint8_t *bytes) {
    return ((uint32_t)bytes[0] << 24) |
           ((uint32_t)bytes[1] << 16) |
           ((uint32_t)bytes[2] << 8) | bytes[3];
}

static bool fail(char *error, size_t capacity, const char *message) {
    if (error && capacity) snprintf(error, capacity, "%s", message);
    return false;
}

static size_t append_utf8(char output[OEM_MAX_UTF8_BYTES + 1],
                          size_t used, uint32_t point) {
    if (point < 0x80) {
        output[used++] = (char)point;
    } else if (point < 0x800) {
        output[used++] = (char)(0xc0 | (point >> 6));
        output[used++] = (char)(0x80 | (point & 0x3f));
    } else if (point < 0x10000) {
        output[used++] = (char)(0xe0 | (point >> 12));
        output[used++] = (char)(0x80 | ((point >> 6) & 0x3f));
        output[used++] = (char)(0x80 | (point & 0x3f));
    } else {
        output[used++] = (char)(0xf0 | (point >> 18));
        output[used++] = (char)(0x80 | ((point >> 12) & 0x3f));
        output[used++] = (char)(0x80 | ((point >> 6) & 0x3f));
        output[used++] = (char)(0x80 | (point & 0x3f));
    }
    return used;
}

void wm_keyboard_word_list_free(WmKeyboardWordList *words) {
    if (!words) return;
    for (size_t index = 0; index < words->count; index++)
        free(words->words[index]);
    free(words->words);
    *words = (WmKeyboardWordList){0};
}

bool wm_keyboard_oem_decode(const uint8_t *data, size_t size,
                            WmKeyboardWordList *words,
                            char *error, size_t error_size) {
    if (!words) return fail(error, error_size, "Missing output word list.");
    *words = (WmKeyboardWordList){0};
    if (!data || size < 4 || size > OEM_MAX_FILE_BYTES)
        return fail(error, error_size, "Invalid OEM dictionary size.");
    uint32_t count = read_be32(data);
    if (count > OEM_MAX_WORDS || count > (size - 4) / 4)
        return fail(error, error_size, "Truncated OEM offset table.");
    size_t table_end = 4 + (size_t)count * 4;
    char **values = calloc(count ? count : 1, sizeof(*values));
    if (!values) return fail(error, error_size, "Out of memory.");
    WmKeyboardWordList parsed = {.words = values};
    for (uint32_t index = 0; index < count; index++) {
        size_t offset = read_be32(data + 4 + (size_t)index * 4);
        if (offset < table_end || (offset & 1u) || offset >= size) {
            wm_keyboard_word_list_free(&parsed);
            return fail(error, error_size, "Invalid OEM word offset.");
        }
        char utf8[OEM_MAX_UTF8_BYTES + 1];
        size_t used = 0, units = 0;
        bool filtered = false, terminated = false;
        while (offset + 2 <= size) {
            uint16_t first = read_be16(data + offset);
            offset += 2;
            if (first == 0) {
                terminated = true;
                break;
            }
            units++;
            uint32_t point = first;
            if (first >= 0xd800 && first <= 0xdbff) {
                if (offset + 2 > size) break;
                uint16_t second = read_be16(data + offset);
                if (second < 0xdc00 || second > 0xdfff) break;
                offset += 2;
                units++;
                point = 0x10000u + (((uint32_t)first - 0xd800u) << 10) +
                        (uint32_t)second - 0xdc00u;
            } else if (first >= 0xdc00 && first <= 0xdfff) {
                break;
            }
            if (point < 0x20u || point == 0x7fu ||
                units > OEM_MAX_UTF16_UNITS) filtered = true;
            if (!filtered) used = append_utf8(utf8, used, point);
        }
        if (!terminated) {
            wm_keyboard_word_list_free(&parsed);
            return fail(error, error_size, "Unterminated or invalid UTF-16 word.");
        }
        if (filtered || used == 0) continue;
        utf8[used] = '\0';
        char *copy = malloc(used + 1);
        if (!copy) {
            wm_keyboard_word_list_free(&parsed);
            return fail(error, error_size, "Out of memory.");
        }
        memcpy(copy, utf8, used + 1);
        parsed.words[parsed.count++] = copy;
    }
    *words = parsed;
    return true;
}

bool wm_keyboard_oem_load(const char *path, WmKeyboardWordList *words,
                          char *error, size_t error_size) {
    if (!path || !words) return fail(error, error_size, "Missing OEM path.");
    *words = (WmKeyboardWordList){0};
    FILE *file = fopen(path, "rb");
    if (!file) return fail(error, error_size, "OEM dictionary is absent.");
    if (fseek(file, 0, SEEK_END) != 0) {
        fclose(file);
        return fail(error, error_size, "Could not size OEM dictionary.");
    }
    long length = ftell(file);
    if (length < 4 || length > OEM_MAX_FILE_BYTES ||
        fseek(file, 0, SEEK_SET) != 0) {
        fclose(file);
        return fail(error, error_size, "Invalid OEM dictionary size.");
    }
    uint8_t *bytes = malloc((size_t)length);
    if (!bytes) {
        fclose(file);
        return fail(error, error_size, "Out of memory.");
    }
    bool read_okay = fread(bytes, 1, (size_t)length, file) == (size_t)length;
    if (fclose(file) != 0) read_okay = false;
    bool okay = read_okay && wm_keyboard_oem_decode(bytes, (size_t)length,
                                                   words, error, error_size);
    free(bytes);
    if (!read_okay) return fail(error, error_size, "Could not read OEM dictionary.");
    return okay;
}
