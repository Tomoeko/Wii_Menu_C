#ifndef WII_MENU_KEYBOARD_DICTIONARY_H
#define WII_MENU_KEYBOARD_DICTIONARY_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* OEM eZTNintendo*.znd contains BE offsets to NUL-terminated UTF-16BE
 * words. This reader yields ordered words only; it does not implement Zi8's
 * candidate-generation algorithm. Words outside the HTML fallback loader's
 * 64 UTF-16-unit/control-character limits are omitted. */
typedef struct WmKeyboardWordList {
    char **words;
    size_t count;
} WmKeyboardWordList;

bool wm_keyboard_oem_decode(const uint8_t *data, size_t size,
                            WmKeyboardWordList *words,
                            char *error, size_t error_size);
bool wm_keyboard_oem_load(const char *path, WmKeyboardWordList *words,
                          char *error, size_t error_size);
void wm_keyboard_word_list_free(WmKeyboardWordList *words);

#endif
