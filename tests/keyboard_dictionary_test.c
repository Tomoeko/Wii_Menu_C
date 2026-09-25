#define _POSIX_C_SOURCE 200809L

#include "wii_menu/keyboard_dictionary.h"

#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

static void be16(uint8_t *bytes, uint16_t value) {
    bytes[0] = (uint8_t)(value >> 8);
    bytes[1] = (uint8_t)value;
}

static void be32(uint8_t *bytes, uint32_t value) {
    bytes[0] = (uint8_t)(value >> 24);
    bytes[1] = (uint8_t)(value >> 16);
    bytes[2] = (uint8_t)(value >> 8);
    bytes[3] = (uint8_t)value;
}

int main(void) {
    uint8_t data[180] = {0};
    be32(data, 2);
    be32(data + 4, 12);
    be32(data + 8, 20);
    be16(data + 12, 0x00e9);
    be16(data + 14, 't');
    be16(data + 16, 0x00e9);
    be16(data + 18, 0);
    be16(data + 20, 'A');
    be16(data + 22, 0xd83d);
    be16(data + 24, 0xde00);
    be16(data + 26, 0);
    char error[128] = {0};
    WmKeyboardWordList words = {0};
    assert(wm_keyboard_oem_decode(data, 28, &words, error, sizeof(error)));
    assert(words.count == 2);
    assert(strcmp(words.words[0], "été") == 0);
    assert(strcmp(words.words[1], "A😀") == 0);
    wm_keyboard_word_list_free(&words);

    char temporary[] = "/tmp/wm-oem-dictionary-XXXXXX";
    int file = mkstemp(temporary);
    assert(file >= 0);
    assert(write(file, data, 28) == 28);
    assert(close(file) == 0);
    assert(wm_keyboard_oem_load(temporary, &words, error, sizeof(error)));
    assert(words.count == 2);
    wm_keyboard_word_list_free(&words);
    assert(unlink(temporary) == 0);

    assert(!wm_keyboard_oem_decode(data, 3, &words, error, sizeof(error)));
    be32(data + 4, 10);
    assert(!wm_keyboard_oem_decode(data, 28, &words, error, sizeof(error)));
    be32(data + 4, 12);
    be32(data + 8, 27);
    assert(!wm_keyboard_oem_decode(data, 28, &words, error, sizeof(error)));
    be32(data + 8, 20);
    be16(data + 22, 0xd83d);
    be16(data + 24, 'a');
    assert(!wm_keyboard_oem_decode(data, 28, &words, error, sizeof(error)));
    be16(data + 24, 0xde00);
    assert(!wm_keyboard_oem_decode(data, 26, &words, error, sizeof(error)));

    memset(data, 0, sizeof(data));
    be32(data, 2);
    be32(data + 4, 12);
    be32(data + 8, 16);
    be16(data + 12, 0x000a);
    be16(data + 14, 0);
    for (unsigned index = 0; index < 65; index++)
        be16(data + 16 + index * 2, 'a');
    be16(data + 146, 0);
    assert(wm_keyboard_oem_decode(data, 148, &words, error, sizeof(error)));
    assert(words.count == 0);
    wm_keyboard_word_list_free(&words);
    for (unsigned index = 0; index < 64; index++)
        be16(data + 16 + index * 2, 0x00e9);
    be16(data + 144, 0);
    assert(wm_keyboard_oem_decode(data, 146, &words, error, sizeof(error)));
    assert(words.count == 1);
    assert(strlen(words.words[0]) == 128);
    wm_keyboard_word_list_free(&words);
    puts("OEM dictionary offsets, UTF-16, filters, and loading passed.");
    return 0;
}
