#ifndef WII_MENU_BOARD_COMPOSE_DRAFT_H
#define WII_MENU_BOARD_COMPOSE_DRAFT_H

#include "wii_menu/board/board_keyboard.h"

#include <stdbool.h>
#include <stddef.h>

enum { BOARD_COMPOSE_MAX_TEXT_BYTES = 4096 };

typedef struct BoardComposeDraft {
    char text[BOARD_COMPOSE_MAX_TEXT_BYTES + 1];
    char display_text[BOARD_COMPOSE_MAX_TEXT_BYTES +
                      WM_KEYBOARD_CANDIDATE_UTF8_CAPACITY];
    /* The keyboard keeps this pointer; the enclosing compose stays in place. */
    char keyboard_text[BOARD_COMPOSE_MAX_TEXT_BYTES + 1];
    size_t text_bytes;
    size_t caret_bytes;
    size_t pointer_caret_bytes;
    bool pointer_caret_valid;
} BoardComposeDraft;

void board_compose_draft_reset(BoardComposeDraft *draft);
void board_compose_draft_sync_keyboard(BoardComposeDraft *draft);

/* The display buffer is scratch storage owned by the draft. It is overwritten
 * by the next display call or edit; the stored text remains unchanged. */
const char *board_compose_draft_display(BoardComposeDraft *draft, bool editing,
                                        WmBoardKeyboard *keyboard,
                                        size_t *display_caret_bytes);
bool board_compose_draft_has_nonspace(const BoardComposeDraft *draft);

bool board_compose_draft_insert(BoardComposeDraft *draft, const char *utf8,
                                size_t bytes);
bool board_compose_draft_replace_before_caret(BoardComposeDraft *draft,
                                               size_t prefix_bytes,
                                               const char *replacement,
                                               size_t replacement_bytes);
bool board_compose_draft_backspace(BoardComposeDraft *draft);
/* Phone multitap changes the last ASCII keytop byte without moving the caret. */
bool board_compose_draft_replace_last_byte(BoardComposeDraft *draft, char byte);

#endif
