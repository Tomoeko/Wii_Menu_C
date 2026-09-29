#include "board_compose_draft.h"
#include "board_text.h"

#include <string.h>

void board_compose_draft_reset(BoardComposeDraft *draft) {
    if (!draft) {
        return;
    }
    draft->text[0] = '\0';
    draft->keyboard_text[0] = '\0';
    draft->text_bytes = 0;
    draft->caret_bytes = 0;
    draft->pointer_caret_bytes = 0;
    draft->pointer_caret_valid = false;
}

void board_compose_draft_sync_keyboard(BoardComposeDraft *draft) {
    if (!draft) {
        return;
    }
    memcpy(draft->keyboard_text, draft->text, draft->caret_bytes);
    draft->keyboard_text[draft->caret_bytes] = '\0';
}

const char *board_compose_draft_display(BoardComposeDraft *draft, bool editing,
                                        WmBoardKeyboard *keyboard,
                                        size_t *display_caret_bytes) {
    if (!draft || (editing && !keyboard)) {
        return NULL;
    }
    if (editing && draft->caret_bytes > 0 &&
        draft->text[draft->caret_bytes - 1] == ' ' &&
        wm_board_keyboard_phone_space_pending(keyboard)) {
        static const char open_box[] = "\342\220\243"; /* U+2423 */
        size_t prefix = draft->caret_bytes - 1;
        memcpy(draft->display_text, draft->text, prefix);
        memcpy(draft->display_text + prefix, open_box, sizeof(open_box) - 1);
        memcpy(draft->display_text + prefix + sizeof(open_box) - 1,
               draft->text + draft->caret_bytes,
               draft->text_bytes - draft->caret_bytes + 1);
        if (display_caret_bytes) {
            *display_caret_bytes = prefix + sizeof(open_box) - 1;
        }
        return draft->display_text;
    }

    WmBoardKeyboardComposition composition;
    if (editing && wm_board_keyboard_composition(keyboard, &composition) &&
        composition.prefix_bytes <= draft->caret_bytes &&
        composition.preview_candidate &&
        strcmp(composition.preview_candidate, ">") != 0) {
        size_t prefix_start = draft->caret_bytes - composition.prefix_bytes;
        size_t preview_bytes = strlen(composition.preview_candidate);
        size_t suffix_bytes = draft->text_bytes - draft->caret_bytes;
        if (prefix_start + preview_bytes + suffix_bytes < sizeof(draft->display_text)) {
            memcpy(draft->display_text, draft->text, prefix_start);
            memcpy(draft->display_text + prefix_start, composition.preview_candidate,
                   preview_bytes);
            memcpy(draft->display_text + prefix_start + preview_bytes,
                   draft->text + draft->caret_bytes, suffix_bytes + 1);
            if (display_caret_bytes) {
                *display_caret_bytes = composition.preview_hovered
                                           ? prefix_start + preview_bytes
                                           : draft->caret_bytes;
            }
            return draft->display_text;
        }
    }

    if (display_caret_bytes) {
        *display_caret_bytes = draft->caret_bytes;
    }
    return draft->text;
}

bool board_compose_draft_has_nonspace(const BoardComposeDraft *draft) {
    if (!draft) {
        return false;
    }
    for (size_t index = 0; index < draft->text_bytes; index++) {
        char character = draft->text[index];
        if (character != ' ' && character != '\n' && character != '\r' &&
            character != '\t') {
            return true;
        }
    }
    return false;
}

bool board_compose_draft_insert(BoardComposeDraft *draft, const char *utf8,
                                size_t bytes) {
    if (!draft) {
        return false;
    }
    if (!wm_board_text_insert(draft->text, sizeof(draft->text), &draft->text_bytes,
                              &draft->caret_bytes, utf8, bytes)) {
        return false;
    }
    board_compose_draft_sync_keyboard(draft);
    return true;
}

bool board_compose_draft_replace_before_caret(BoardComposeDraft *draft,
                                              size_t prefix_bytes,
                                              const char *replacement,
                                              size_t replacement_bytes) {
    if (!draft) {
        return false;
    }
    if (!wm_board_text_replace_before_caret(
            draft->text, sizeof(draft->text), &draft->text_bytes, &draft->caret_bytes,
            prefix_bytes, replacement, replacement_bytes)) {
        return false;
    }
    board_compose_draft_sync_keyboard(draft);
    return true;
}

bool board_compose_draft_backspace(BoardComposeDraft *draft) {
    if (!draft) {
        return false;
    }
    if (!wm_board_text_backspace(draft->text, sizeof(draft->text), &draft->text_bytes,
                                 &draft->caret_bytes)) {
        return false;
    }
    board_compose_draft_sync_keyboard(draft);
    return true;
}

bool board_compose_draft_replace_last_byte(BoardComposeDraft *draft, char byte) {
    if (!draft || draft->caret_bytes == 0 || draft->caret_bytes > draft->text_bytes) {
        return false;
    }
    draft->text[draft->caret_bytes - 1] = byte;
    board_compose_draft_sync_keyboard(draft);
    return true;
}
