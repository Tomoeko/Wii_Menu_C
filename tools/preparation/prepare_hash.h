#ifndef WM_PREPARATION_PREPARE_HASH_H
#define WM_PREPARATION_PREPARE_HASH_H

#include <stddef.h>
#include <stdint.h>

/* Exactly 40 lowercase hexadecimal digits followed by NUL. */
static inline void prepare_format_sha1(const uint8_t digest[20], char hexadecimal[41]) {
    static const char digits[] = "0123456789abcdef";
    for (size_t byte = 0; byte < 20; byte++) {
        hexadecimal[byte * 2] = digits[digest[byte] >> 4];
        hexadecimal[byte * 2 + 1] = digits[digest[byte] & 15];
    }
    hexadecimal[40] = '\0';
}

#endif
