#ifndef WII_MENU_BOARD_KEYBOARD_TEXT_H
#define WII_MENU_BOARD_KEYBOARD_TEXT_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* All spans passed to these helpers must come from the same byte array.
 * Invalid UTF-8 consumes one byte and decodes as U+FFFD. */
uint32_t wm_keyboard_text_next_codepoint(const char **cursor, const char *end);

/* Prediction intentionally uses the menu's limited Latin case mapping. */
uint32_t wm_keyboard_text_lower_point(uint32_t point);
uint32_t wm_keyboard_text_upper_point(uint32_t point);
size_t wm_keyboard_text_encode_point(char output[4], uint32_t point);
bool wm_keyboard_text_is_word_point(uint32_t point);

bool wm_keyboard_text_prefix_matches(const char *word, size_t word_bytes,
                                     const char *prefix, size_t prefix_bytes);
bool wm_keyboard_text_phone_digits_match(const char *word, size_t word_bytes,
                                         const char *digits, bool *exact);
bool wm_keyboard_text_copy_candidate_case(char *output, size_t output_capacity,
                                          const char *word, size_t word_bytes,
                                          const char *prefix, size_t prefix_bytes,
                                          bool phone_digits, bool phone_uppercase);

#endif
