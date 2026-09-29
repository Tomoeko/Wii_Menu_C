#ifndef WM_BOARD_ADDRESS_INTERNAL_H
#define WM_BOARD_ADDRESS_INTERNAL_H

#include "wii_menu/board/board_address.h"
#include "wii_menu/layout/layout_runtime.h"

#include <math.h>

enum {
    ADDRESS_PAGE_COUNT = 20,
    ADDRESS_TEXT_CAPACITY = 400
};

typedef enum AddressDialogPhase {
    ADDRESS_DIALOG_CLOSED,
    ADDRESS_DIALOG_ENTER,
    ADDRESS_DIALOG_READY,
    ADDRESS_DIALOG_PRESS,
    ADDRESS_DIALOG_EXIT
} AddressDialogPhase;

typedef struct AddressEntryFocus {
    bool active;
    bool entering;
    float frame;
} AddressEntryFocus;

/* Opaque publicly. Layouts and caches are borrowed by presentation;
 * create/destroy retain exclusive ownership of the layout lifetimes. */
struct WmBoardAddress {
    WmPlatform *platform;
    WmTextureCache *textures;
    WmFontCache *fonts;
    WmLayout *book;
    WmLayout *kind_layout;
    WmLayout *form;
    WmLayout *review;
    WmLayout *dialog;
    WmLayout *erase_dialog;
    WmBoardContactStore *contacts;
    WmBoardAddressPhase phase;
    float frame;
    unsigned page;
    unsigned next_page;
    size_t selected_slot;
    bool forward;
    bool wii_kind;
    int hovered_entry;
    AddressEntryFocus entry_focus[5];
    int hovered_kind;
    bool kind_focus[2];
    bool kind_focus_entering[2];
    float kind_focus_frame[2];
    WmBoardAddressContactAction hovered_contact;
    bool contact_focus[2];
    bool contact_focus_entering[2];
    float contact_focus_frame[2];
    char text[ADDRESS_TEXT_CAPACITY];
    size_t text_bytes;
    size_t text_units;
    char nickname[44];
    size_t nickname_bytes;
    size_t nickname_units;
    WmBoardAddressIssue issue;
    AddressDialogPhase dialog_phase;
    float dialog_frame;
    bool dialog_focus;
    bool dialog_hovered;
    bool dialog_focus_entering;
    float dialog_focus_frame;
    bool erase_yes_selected;
    bool erase_yes_focus;
    bool erase_yes_hovered;
    bool erase_yes_focus_entering;
    float erase_yes_focus_frame;
    bool erase_success;
    bool erase_save_failed;
    float erase_message_frame;
    char save_error[160];
};

static inline float limit_frame(float frame, float last) {
    return fminf(fmaxf(frame, 0.0f), last);
}

#endif
