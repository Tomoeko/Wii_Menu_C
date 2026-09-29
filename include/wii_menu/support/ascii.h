#ifndef WII_MENU_SUPPORT_ASCII_H
#define WII_MENU_SUPPORT_ASCII_H

#include <stdbool.h>
#include <stdint.h>

/* Resource names use ASCII case folding. Other bytes, including UTF-8
 * sequences, stay unchanged regardless of the host locale. */
static inline unsigned char wm_ascii_lower(unsigned char value) {
    if (value >= 'A' && value <= 'Z')
        return (unsigned char)(value + ('a' - 'A'));
    return value;
}

static inline bool wm_ascii_equal_ignore_case(const char *left, const char *right) {
    for (;; left++, right++) {
        unsigned char first = (unsigned char)*left;
        unsigned char second = (unsigned char)*right;
        if (wm_ascii_lower(first) != wm_ascii_lower(second))
            return false;
        if (first == '\0')
            return true;
    }
}

/* FNV-1a over folded bytes; equality and hash lookup use the same folding. */
static inline uint64_t wm_ascii_hash_ignore_case(const char *text) {
    uint64_t hash = UINT64_C(14695981039346656037);
    for (; *text != '\0'; text++) {
        hash ^= wm_ascii_lower((unsigned char)*text);
        hash *= UINT64_C(1099511628211);
    }
    return hash;
}

#endif
