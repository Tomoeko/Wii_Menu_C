#ifndef WII_MENU_BOARD_TEXT_H
#define WII_MENU_BOARD_TEXT_H

#include <stdbool.h>
#include <stddef.h>

/* Strictly decode one UTF-8 scalar. The span need not be NUL terminated. */
bool wm_board_text_character(const char *text, size_t remaining, size_t *bytes,
                             size_t *utf16_units);

/* Return SIZE_MAX if any byte is malformed. */
size_t wm_board_text_utf16_units(const char *text, size_t bytes);

/* Memo input permits line feeds, but no other C0 controls. The entire
 * insertion is checked before the caller changes its draft or keyboard. */
bool wm_board_text_memo_input(const char *text, size_t maximum_bytes, size_t *bytes,
                              bool *has_whitespace);

/* These edit an existing NUL-terminated UTF-8 draft in place. On failure,
 * neither the draft nor its byte counters change. Inserted/replacement spans
 * must already have passed the caller's text policy. */
bool wm_board_text_insert(char *text, size_t capacity, size_t *text_bytes,
                          size_t *caret_bytes, const char *inserted,
                          size_t inserted_bytes);
bool wm_board_text_replace_before_caret(char *text, size_t capacity, size_t *text_bytes,
                                        size_t *caret_bytes, size_t prefix_bytes,
                                        const char *replacement,
                                        size_t replacement_bytes);
bool wm_board_text_backspace(char *text, size_t capacity, size_t *text_bytes,
                             size_t *caret_bytes);

#endif
