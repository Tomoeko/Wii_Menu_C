#include "wii_menu/board_address.h"

#include "wii_menu/layout_present.h"
#include "wii_menu/layout_runtime.h"
#include "wii_menu/material_prepare.h"
#include "wii_menu/source_hit.h"

#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

enum {
    ADDRESS_PATH_CAPACITY = 4096,
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

typedef struct AddressGeometry {
    unsigned right_count;
    unsigned left_count;
    unsigned base_steps;
    unsigned cover_offset;
    unsigned cover_count;
    unsigned face_page;
    unsigned turn_page;
    unsigned turning_offset;
    bool turning_sheet;
    bool cover_turn;
    bool wrap_turn;
} AddressGeometry;

static float limit_frame(float frame, float last) {
    return fminf(fmaxf(frame, 0.0f), last);
}

static size_t utf16_units(const char *text);

static WmLayout *load_layout_in(const char *assets_directory,
                                const char *subdirectory,
                                const char *layout_name) {
    char path[ADDRESS_PATH_CAPACITY];
    int count = snprintf(path, sizeof(path),
                         "%s/layouts/%s/%s.json",
                         assets_directory, subdirectory, layout_name);
    if (count < 0 || count >= (int)sizeof(path)) return NULL;
    char error[160] = {0};
    WmLayout *layout = wm_layout_load_json(path, error, sizeof(error));
    if (!layout) {
        fprintf(stderr, "Could not load Address Book layout %s: %s\n",
                layout_name, error);
    }
    return layout;
}

static WmLayout *load_layout(const char *assets_directory,
                              const char *layout_name) {
    return load_layout_in(assets_directory, "board", layout_name);
}

/* Address::draw, recovered in the HTML reference from USA 4.3. These are
 * sheet counts and offsets, not a texture approximation of the book. */
static AddressGeometry geometry(const WmBoardAddress *address) {
    unsigned page = address->page;
    AddressGeometry result = {
        .right_count = ADDRESS_PAGE_COUNT - page,
        .left_count = page > 0 ? page - 1 : 0,
        .base_steps = page,
        .cover_count = 1,
        .face_page = page > 0 ? page : 1,
        .turn_page = page > 0 ? page : 1
    };
    if (address->phase == WM_BOARD_ADDRESS_TURN) {
        unsigned next = address->next_page;
        if (page == ADDRESS_PAGE_COUNT && next == 0) {
            result.left_count = 0;
            result.cover_offset = 1;
            result.cover_count = ADDRESS_PAGE_COUNT - 1;
            result.wrap_turn = true;
        } else if (page == 0 && next == ADDRESS_PAGE_COUNT) {
            result.right_count = 0;
            result.base_steps = ADDRESS_PAGE_COUNT;
            result.face_page = ADDRESS_PAGE_COUNT;
            result.cover_offset = 1;
            result.cover_count = ADDRESS_PAGE_COUNT - 1;
            result.wrap_turn = true;
        } else if (next > page) {
            result.right_count--;
            result.base_steps++;
            result.turning_offset = 1;
            result.face_page = next;
            result.turning_sheet = page != 0;
            result.cover_turn = page == 0;
        } else {
            result.left_count = result.left_count > 0
                                    ? result.left_count - 1 : 0;
            result.turning_offset = 1;
            result.turn_page = next > 0 ? next : 1;
            result.turning_sheet = next != 0;
            result.cover_turn = next == 0;
        }
    }
    if (!result.wrap_turn) {
        result.cover_offset = result.left_count + result.turning_offset;
    }
    return result;
}

WmBoardAddress *wm_board_address_create(WmPlatform *platform,
                                         const char *assets_directory,
                                         WmTextureCache *textures,
                                         WmFontCache *fonts) {
    if (!platform || !assets_directory || !textures || !fonts) return NULL;
    WmBoardAddress *address = calloc(1, sizeof(*address));
    if (!address) return NULL;
    address->book = load_layout(assets_directory, "th_Adress_a");
    address->kind_layout = load_layout(assets_directory, "th_Adress_d");
    address->form = load_layout(assets_directory, "th_Adress_c");
    address->review = load_layout(assets_directory, "th_Adress_b");
    address->dialog = load_layout_in(assets_directory, "dlgWdw",
                                     "my_DialogWindow_a1");
    address->erase_dialog = load_layout_in(assets_directory, "dlgWdw",
                                           "my_DialogWindow_b");
    if (!address->book || !address->kind_layout || !address->form ||
        !address->review ||
        !address->dialog || !address->erase_dialog) {
        wm_board_address_destroy(address);
        return NULL;
    }
    address->platform = platform;
    address->textures = textures;
    address->fonts = fonts;
    wm_layout_prepare_materials(platform, address->book);
    wm_layout_prepare_materials(platform, address->kind_layout);
    wm_layout_prepare_materials(platform, address->form);
    wm_layout_prepare_materials(platform, address->review);
    wm_layout_prepare_materials(platform, address->dialog);
    wm_layout_prepare_materials(platform, address->erase_dialog);
    address->hovered_kind = -1;
    return address;
}

void wm_board_address_destroy(WmBoardAddress *address) {
    if (!address) return;
    wm_board_contact_store_destroy(address->contacts);
    wm_layout_destroy(address->book);
    wm_layout_destroy(address->kind_layout);
    wm_layout_destroy(address->form);
    wm_layout_destroy(address->review);
    wm_layout_destroy(address->dialog);
    wm_layout_destroy(address->erase_dialog);
    free(address);
}

WmBoardContactStoreStatus wm_board_address_load_contacts(
    WmBoardAddress *address, const char *path,
    char *error, size_t error_capacity) {
    if (!address) return WM_BOARD_CONTACT_STORE_ERROR;
    WmBoardContactStoreStatus status;
    WmBoardContactStore *loaded = wm_board_contact_store_open(
        path, &status, error, error_capacity);
    wm_board_contact_store_destroy(address->contacts);
    address->contacts = loaded;
    return loaded ? status : WM_BOARD_CONTACT_STORE_ERROR;
}

size_t wm_board_address_contact_count(const WmBoardAddress *address) {
    return address ? wm_board_contact_store_occupied(address->contacts) : 0;
}

bool wm_board_address_contact(const WmBoardAddress *address, size_t slot,
                              WmBoardContact *contact) {
    return address && wm_board_contact_store_get(address->contacts, slot,
                                                  contact);
}

const char *wm_board_address_last_save_error(const WmBoardAddress *address) {
    return address ? address->save_error : NULL;
}

void wm_board_address_reset(WmBoardAddress *address) {
    if (!address) return;
    address->phase = WM_BOARD_ADDRESS_CLOSED;
    address->frame = 0.0f;
    address->page = 0;
    address->next_page = 0;
    address->selected_slot = SIZE_MAX;
    address->forward = true;
    address->wii_kind = true;
    address->hovered_kind = -1;
    address->hovered_contact = WM_BOARD_ADDRESS_CONTACT_NONE;
    memset(address->kind_focus, 0, sizeof(address->kind_focus));
    memset(address->kind_focus_entering, 0,
           sizeof(address->kind_focus_entering));
    memset(address->kind_focus_frame, 0,
           sizeof(address->kind_focus_frame));
    memset(address->contact_focus, 0, sizeof(address->contact_focus));
    memset(address->contact_focus_entering, 0,
           sizeof(address->contact_focus_entering));
    memset(address->contact_focus_frame, 0,
           sizeof(address->contact_focus_frame));
    address->text[0] = '\0';
    address->text_bytes = 0;
    address->text_units = 0;
    address->nickname[0] = '\0';
    address->nickname_bytes = 0;
    address->nickname_units = 0;
    address->issue = WM_BOARD_ADDRESS_ISSUE_NONE;
    address->dialog_phase = ADDRESS_DIALOG_CLOSED;
    address->dialog_frame = 0.0f;
    address->dialog_focus = false;
    address->dialog_hovered = false;
    address->erase_yes_focus = false;
    address->erase_yes_hovered = false;
    address->erase_success = false;
    address->erase_save_failed = false;
    address->erase_message_frame = 0.0f;
    address->save_error[0] = '\0';
}

bool wm_board_address_open(WmBoardAddress *address) {
    if (!address || address->phase != WM_BOARD_ADDRESS_CLOSED) return false;
    address->page = 0;
    address->next_page = 0;
    address->selected_slot = SIZE_MAX;
    address->phase = WM_BOARD_ADDRESS_ENTER;
    address->frame = 0.0f;
    address->wii_kind = true;
    address->hovered_kind = -1;
    address->hovered_contact = WM_BOARD_ADDRESS_CONTACT_NONE;
    memset(address->kind_focus, 0, sizeof(address->kind_focus));
    memset(address->kind_focus_entering, 0,
           sizeof(address->kind_focus_entering));
    memset(address->kind_focus_frame, 0,
           sizeof(address->kind_focus_frame));
    memset(address->contact_focus, 0, sizeof(address->contact_focus));
    memset(address->contact_focus_entering, 0,
           sizeof(address->contact_focus_entering));
    memset(address->contact_focus_frame, 0,
           sizeof(address->contact_focus_frame));
    address->text[0] = '\0';
    address->text_bytes = 0;
    address->text_units = 0;
    address->nickname[0] = '\0';
    address->nickname_bytes = 0;
    address->nickname_units = 0;
    address->issue = WM_BOARD_ADDRESS_ISSUE_NONE;
    address->dialog_phase = ADDRESS_DIALOG_CLOSED;
    address->dialog_focus = false;
    address->dialog_hovered = false;
    address->erase_yes_focus = false;
    address->erase_yes_hovered = false;
    address->erase_success = false;
    address->erase_save_failed = false;
    address->erase_message_frame = 0.0f;
    address->save_error[0] = '\0';
    return true;
}

bool wm_board_address_turn(WmBoardAddress *address, bool forward) {
    if (!address || address->phase != WM_BOARD_ADDRESS_READY) return false;
    address->forward = forward;
    address->next_page = forward
        ? (address->page + 1) % (ADDRESS_PAGE_COUNT + 1)
        : (address->page + ADDRESS_PAGE_COUNT) % (ADDRESS_PAGE_COUNT + 1);
    address->phase = WM_BOARD_ADDRESS_TURN;
    address->frame = 0.0f;
    return true;
}

bool wm_board_address_register(WmBoardAddress *address) {
    if (!address || address->phase != WM_BOARD_ADDRESS_READY) return false;
    if (wm_board_address_contact_count(address) >= WM_BOARD_CONTACT_CAPACITY) {
        address->issue = WM_BOARD_ADDRESS_ISSUE_BOOK_FULL;
        address->dialog_phase = ADDRESS_DIALOG_ENTER;
        address->dialog_frame = 0.0f;
        address->dialog_focus = false;
        address->dialog_hovered = false;
        return true;
    }
    address->phase = WM_BOARD_ADDRESS_REGISTER_PRESS;
    address->frame = 0.0f;
    address->selected_slot = SIZE_MAX;
    address->wii_kind = true;
    address->hovered_kind = -1;
    memset(address->kind_focus, 0, sizeof(address->kind_focus));
    memset(address->kind_focus_entering, 0,
           sizeof(address->kind_focus_entering));
    memset(address->kind_focus_frame, 0,
           sizeof(address->kind_focus_frame));
    address->text[0] = '\0';
    address->text_bytes = 0;
    address->text_units = 0;
    address->nickname[0] = '\0';
    address->nickname_bytes = 0;
    address->nickname_units = 0;
    address->issue = WM_BOARD_ADDRESS_ISSUE_NONE;
    address->dialog_phase = ADDRESS_DIALOG_CLOSED;
    address->dialog_focus = false;
    address->dialog_hovered = false;
    return true;
}

bool wm_board_address_select_kind(WmBoardAddress *address, bool wii) {
    if (!address || address->phase != WM_BOARD_ADDRESS_KIND_READY) return false;
    address->wii_kind = wii;
    address->phase = WM_BOARD_ADDRESS_KIND_PRESS;
    address->frame = 0.0f;
    return true;
}

bool wm_board_address_select_entry(WmBoardAddress *address, unsigned row) {
    if (!address || address->phase != WM_BOARD_ADDRESS_READY ||
        address->page == 0 || row >= 5) return false;
    size_t slot = (address->page - 1) * 5u + row;
    WmBoardContact contact;
    if (!wm_board_address_contact(address, slot, &contact)) return false;
    size_t address_bytes = strlen(contact.address);
    size_t nickname_bytes = strlen(contact.nickname);
    if (address_bytes >= sizeof(address->text) ||
        nickname_bytes >= sizeof(address->nickname)) return false;
    memcpy(address->text, contact.address, address_bytes + 1);
    memcpy(address->nickname, contact.nickname, nickname_bytes + 1);
    address->text_bytes = address_bytes;
    address->nickname_bytes = nickname_bytes;
    address->text_units = utf16_units(address->text);
    address->nickname_units = utf16_units(address->nickname);
    address->wii_kind = contact.wii;
    address->selected_slot = slot;
    address->hovered_contact = WM_BOARD_ADDRESS_CONTACT_NONE;
    memset(address->contact_focus, 0, sizeof(address->contact_focus));
    address->phase = WM_BOARD_ADDRESS_BOOK_ENTRY_PRESS;
    address->frame = 0.0f;
    return true;
}

void wm_board_address_hover_kind(WmBoardAddress *address, int choice) {
    if (!address || address->phase != WM_BOARD_ADDRESS_KIND_READY) return;
    if (choice < 0 || choice > 1) choice = -1;
    if (address->hovered_kind == choice) return;
    if (address->hovered_kind >= 0) {
        int old = address->hovered_kind;
        address->kind_focus[old] = true;
        address->kind_focus_entering[old] = false;
        address->kind_focus_frame[old] = 0.0f;
    }
    address->hovered_kind = choice;
    if (choice >= 0) {
        address->kind_focus[choice] = true;
        address->kind_focus_entering[choice] = true;
        address->kind_focus_frame[choice] = 0.0f;
    }
}

static int contact_focus_index(WmBoardAddressContactAction action) {
    return action == WM_BOARD_ADDRESS_CONTACT_CHANGE_NICKNAME ? 0 :
           action == WM_BOARD_ADDRESS_CONTACT_ERASE ? 1 : -1;
}

static void change_contact_focus(WmBoardAddress *address, int index,
                                 bool entering) {
    if (index < 0) return;
    /* The six-frame WAD focus tracks are symmetric. Reverse the current
     * sample instead of restarting when the pointer re-enters mid-transition. */
    if (address->contact_focus[index] &&
        address->contact_focus_entering[index] != entering) {
        address->contact_focus_frame[index] = 6.0f -
            limit_frame(address->contact_focus_frame[index], 6.0f);
    } else if (!address->contact_focus[index]) {
        address->contact_focus_frame[index] = 0.0f;
    }
    address->contact_focus[index] = true;
    address->contact_focus_entering[index] = entering;
}

void wm_board_address_hover_contact(WmBoardAddress *address,
                                    WmBoardAddressContactAction action) {
    if (!address || address->phase != WM_BOARD_ADDRESS_CONTACT_READY ||
        address->dialog_phase != ADDRESS_DIALOG_CLOSED) return;
    if (action != WM_BOARD_ADDRESS_CONTACT_CHANGE_NICKNAME &&
        action != WM_BOARD_ADDRESS_CONTACT_ERASE)
        action = WM_BOARD_ADDRESS_CONTACT_NONE;
    if (action == address->hovered_contact) return;
    change_contact_focus(address,
                         contact_focus_index(address->hovered_contact), false);
    change_contact_focus(address, contact_focus_index(action), true);
    address->hovered_contact = action;
}

bool wm_board_address_back(WmBoardAddress *address) {
    if (!address) return false;
    if (wm_board_address_dialog_active(address))
        return address->issue == WM_BOARD_ADDRESS_ISSUE_ERASE_CONFIRM
            ? wm_board_address_dialog_choose(address, false) :
              wm_board_address_dialog_accept(address);
    if (address->phase == WM_BOARD_ADDRESS_READY)
        address->phase = WM_BOARD_ADDRESS_EXIT;
    else if (address->phase == WM_BOARD_ADDRESS_KIND_READY) {
        wm_board_address_hover_kind(address, -1);
        address->phase = WM_BOARD_ADDRESS_KIND_TO_BOOK;
    } else if (address->phase == WM_BOARD_ADDRESS_FORM_READY)
        address->phase = WM_BOARD_ADDRESS_FORM_TO_BOOK;
    else if (address->phase == WM_BOARD_ADDRESS_NICKNAME_READY)
        address->phase = WM_BOARD_ADDRESS_NICKNAME_TO_FORM;
    else if (address->phase == WM_BOARD_ADDRESS_MII_READY)
        address->phase = WM_BOARD_ADDRESS_MII_TO_NICKNAME;
    else if (address->phase == WM_BOARD_ADDRESS_REVIEW_READY)
        address->phase = WM_BOARD_ADDRESS_REVIEW_TO_MII;
    else if (address->phase == WM_BOARD_ADDRESS_CONTACT_READY)
        address->phase = WM_BOARD_ADDRESS_CONTACT_TO_BOOK;
    else if (address->phase == WM_BOARD_ADDRESS_CONTACT_NAME_FORM_READY) {
        WmBoardContact saved;
        if (!wm_board_address_contact(address, address->selected_slot,
                                       &saved)) return false;
        snprintf(address->nickname, sizeof(address->nickname), "%s",
                 saved.nickname);
        address->nickname_bytes = strlen(address->nickname);
        address->nickname_units = utf16_units(address->nickname);
        address->phase = WM_BOARD_ADDRESS_CONTACT_NAME_FORM_RETURN;
    }
    else return false;
    address->frame = 0.0f;
    address->hovered_kind = -1;
    return true;
}

static void advance_dialog(WmBoardAddress *address, float frames) {
    if (address->dialog_phase == ADDRESS_DIALOG_CLOSED) return;
    address->dialog_focus_frame += frames;
    address->erase_yes_focus_frame += frames;
    bool erase = address->issue == WM_BOARD_ADDRESS_ISSUE_ERASE_CONFIRM;
    while (frames > 0.0f) {
        float duration = address->dialog_phase == ADDRESS_DIALOG_ENTER
            ? (erase ? 26.0f : 25.0f) :
            address->dialog_phase == ADDRESS_DIALOG_PRESS
            ? (erase ? 21.0f : 17.0f) :
            address->dialog_phase == ADDRESS_DIALOG_EXIT
            ? (erase ? 26.0f : 21.0f) : 0.0f;
        if (duration == 0.0f) return;
        float amount = fminf(frames, duration - address->dialog_frame);
        address->dialog_frame += amount;
        frames -= amount;
        if (address->dialog_frame < duration) return;
        address->dialog_phase = address->dialog_phase == ADDRESS_DIALOG_ENTER
            ? ADDRESS_DIALOG_READY :
            address->dialog_phase == ADDRESS_DIALOG_PRESS
                ? ADDRESS_DIALOG_EXIT : ADDRESS_DIALOG_CLOSED;
        address->dialog_frame = 0.0f;
    }
}

static void complete_registration(WmBoardAddress *address) {
    WmBoardContact contact = {
        .wii = address->wii_kind,
        .confirmed = true,
        .address = address->text,
        .nickname = address->nickname
    };
    size_t slot;
    if (!wm_board_contact_store_register(address->contacts, contact, &slot,
                                         address->save_error,
                                         sizeof(address->save_error))) {
        address->phase = WM_BOARD_ADDRESS_REVIEW_ENTER;
        address->issue = WM_BOARD_ADDRESS_ISSUE_SAVE_ERROR;
    } else {
        address->selected_slot = slot;
        address->page = (unsigned)(slot / 5) + 1;
        address->next_page = address->page;
        address->phase = WM_BOARD_ADDRESS_REGISTERED_NOTICE;
        address->issue = WM_BOARD_ADDRESS_ISSUE_REGISTERED;
    }
    address->dialog_phase = ADDRESS_DIALOG_ENTER;
    address->dialog_frame = 0.0f;
    address->dialog_focus = false;
    address->dialog_hovered = false;
}

void wm_board_address_advance(WmBoardAddress *address, float frames) {
    if (!address || !isfinite(frames) || frames <= 0.0f) return;
    bool registered_dialog = address->phase ==
                             WM_BOARD_ADDRESS_REGISTERED_NOTICE &&
                             wm_board_address_dialog_active(address);
    bool erase_question = address->phase ==
                          WM_BOARD_ADDRESS_CONTACT_ERASE_QUESTION &&
                          wm_board_address_dialog_active(address);
    bool erased_notice = address->phase ==
                         WM_BOARD_ADDRESS_CONTACT_ERASED_NOTICE &&
                         wm_board_address_dialog_active(address);
    advance_dialog(address, frames);
    if (registered_dialog && !wm_board_address_dialog_active(address)) {
        address->phase = WM_BOARD_ADDRESS_BOOK_RETURN;
        address->frame = 0.0f;
        return;
    }
    if (erase_question && !wm_board_address_dialog_active(address)) {
        address->erase_success = false;
        address->erase_save_failed = false;
        if (address->erase_yes_selected) {
            address->erase_success = wm_board_contact_store_erase(
                address->contacts, address->selected_slot,
                address->save_error, sizeof(address->save_error));
            address->erase_save_failed = !address->erase_success;
        }
        address->phase = WM_BOARD_ADDRESS_CONTACT_ERASE_MESSAGE_OUT;
        address->frame = 0.0f;
        return;
    }
    if (erased_notice && !wm_board_address_dialog_active(address)) {
        address->phase = WM_BOARD_ADDRESS_CONTACT_TO_BOOK;
        address->frame = 0.0f;
        return;
    }
    if (address->phase == WM_BOARD_ADDRESS_CONTACT_ERASE_QUESTION)
        address->erase_message_frame = limit_frame(
            address->erase_message_frame + frames, 20.0f);
    for (unsigned index = 0; index < 2; index++) {
        if (address->kind_focus[index])
            address->kind_focus_frame[index] += frames;
        if (address->contact_focus[index])
            address->contact_focus_frame[index] += frames;
    }
    while (frames > 0.0f) {
        float duration = address->phase == WM_BOARD_ADDRESS_ENTER ? 26.0f :
                         address->phase == WM_BOARD_ADDRESS_TURN ? 16.0f :
                         address->phase == WM_BOARD_ADDRESS_EXIT ? 48.0f :
                         address->phase == WM_BOARD_ADDRESS_REGISTER_PRESS
                             ? 21.0f :
                         address->phase == WM_BOARD_ADDRESS_BOOK_TO_KIND ||
                         address->phase == WM_BOARD_ADDRESS_BOOK_RETURN
                             ? 17.0f :
                         address->phase == WM_BOARD_ADDRESS_KIND_ENTER ||
                         address->phase == WM_BOARD_ADDRESS_KIND_TO_FORM ||
                         address->phase == WM_BOARD_ADDRESS_KIND_TO_BOOK ||
                         address->phase == WM_BOARD_ADDRESS_BOOK_TO_CONTACT ||
                         address->phase == WM_BOARD_ADDRESS_CONTACT_ENTER ||
                         address->phase == WM_BOARD_ADDRESS_CONTACT_TO_BOOK ||
                         address->phase == WM_BOARD_ADDRESS_FORM_TO_BOOK ||
                         address->phase == WM_BOARD_ADDRESS_FORM_TO_NICKNAME ||
                         address->phase == WM_BOARD_ADDRESS_MII_TO_REVIEW ||
                         address->phase == WM_BOARD_ADDRESS_REVIEW_TO_MII ||
                         address->phase == WM_BOARD_ADDRESS_REVIEW_SAVE_EXIT ||
                         address->phase == WM_BOARD_ADDRESS_MII_RESTORE
                             ? 19.0f :
                         address->phase == WM_BOARD_ADDRESS_CONTACT_NAME_TO_FORM ||
                         address->phase == WM_BOARD_ADDRESS_CONTACT_NAME_FORM_RETURN ||
                         address->phase == WM_BOARD_ADDRESS_CONTACT_NAME_CARD_ENTER
                             ? 19.0f :
                         address->phase == WM_BOARD_ADDRESS_CONTACT_ERASE_BUTTONS_OUT ||
                         address->phase == WM_BOARD_ADDRESS_CONTACT_ERASE_BUTTONS_IN
                             ? 11.0f :
                         address->phase == WM_BOARD_ADDRESS_CONTACT_ERASE_MESSAGE_OUT
                             ? 21.0f :
                         address->phase == WM_BOARD_ADDRESS_KIND_PRESS ||
                         address->phase == WM_BOARD_ADDRESS_BOOK_ENTRY_PRESS ||
                         address->phase == WM_BOARD_ADDRESS_FORM_ENTER ||
                         address->phase == WM_BOARD_ADDRESS_FORM_OK_PRESS ||
                         address->phase == WM_BOARD_ADDRESS_NICKNAME_ENTER ||
                         address->phase == WM_BOARD_ADDRESS_NICKNAME_OK_PRESS ||
                         address->phase == WM_BOARD_ADDRESS_NICKNAME_TO_MII ||
                         address->phase == WM_BOARD_ADDRESS_MII_ENTER ||
                         address->phase == WM_BOARD_ADDRESS_MII_OK_PRESS ||
                         address->phase == WM_BOARD_ADDRESS_REVIEW_ENTER ||
                         address->phase == WM_BOARD_ADDRESS_REVIEW_INFO_PRESS ||
                         address->phase == WM_BOARD_ADDRESS_CONTACT_INFO_PRESS ||
                         address->phase == WM_BOARD_ADDRESS_MII_TO_NICKNAME ||
                         address->phase == WM_BOARD_ADDRESS_NICKNAME_RESTORE ||
                         address->phase == WM_BOARD_ADDRESS_NICKNAME_TO_FORM ||
                         address->phase == WM_BOARD_ADDRESS_FORM_RESTORE
                         || address->phase == WM_BOARD_ADDRESS_CONTACT_NAME_PRESS
                         || address->phase == WM_BOARD_ADDRESS_CONTACT_NAME_FORM_ENTER
                         || address->phase == WM_BOARD_ADDRESS_CONTACT_ERASE_PRESS
                             ? 21.0f : 0.0f;
        if (duration == 0.0f) return;
        float amount = fminf(frames, duration - address->frame);
        address->frame += amount;
        frames -= amount;
        if (address->frame < duration) return;
        if (address->phase == WM_BOARD_ADDRESS_TURN) {
            address->page = address->next_page;
            address->phase = WM_BOARD_ADDRESS_READY;
        } else if (address->phase == WM_BOARD_ADDRESS_ENTER) {
            address->phase = WM_BOARD_ADDRESS_READY;
        } else if (address->phase == WM_BOARD_ADDRESS_REGISTER_PRESS) {
            address->phase = WM_BOARD_ADDRESS_BOOK_TO_KIND;
        } else if (address->phase == WM_BOARD_ADDRESS_BOOK_ENTRY_PRESS) {
            address->phase = WM_BOARD_ADDRESS_BOOK_TO_CONTACT;
        } else if (address->phase == WM_BOARD_ADDRESS_BOOK_TO_CONTACT) {
            address->phase = WM_BOARD_ADDRESS_CONTACT_ENTER;
        } else if (address->phase == WM_BOARD_ADDRESS_CONTACT_ENTER) {
            address->phase = WM_BOARD_ADDRESS_CONTACT_READY;
        } else if (address->phase == WM_BOARD_ADDRESS_CONTACT_NAME_PRESS) {
            address->phase = WM_BOARD_ADDRESS_CONTACT_NAME_TO_FORM;
        } else if (address->phase == WM_BOARD_ADDRESS_CONTACT_NAME_TO_FORM) {
            address->phase = WM_BOARD_ADDRESS_CONTACT_NAME_FORM_ENTER;
        } else if (address->phase == WM_BOARD_ADDRESS_CONTACT_NAME_FORM_ENTER) {
            address->phase = WM_BOARD_ADDRESS_CONTACT_NAME_FORM_READY;
        } else if (address->phase == WM_BOARD_ADDRESS_CONTACT_NAME_FORM_RETURN) {
            address->phase = WM_BOARD_ADDRESS_CONTACT_NAME_CARD_ENTER;
        } else if (address->phase == WM_BOARD_ADDRESS_CONTACT_NAME_CARD_ENTER) {
            address->phase = WM_BOARD_ADDRESS_CONTACT_READY;
        } else if (address->phase == WM_BOARD_ADDRESS_CONTACT_ERASE_PRESS) {
            address->phase = WM_BOARD_ADDRESS_CONTACT_ERASE_BUTTONS_OUT;
        } else if (address->phase == WM_BOARD_ADDRESS_CONTACT_ERASE_BUTTONS_OUT) {
            address->phase = WM_BOARD_ADDRESS_CONTACT_ERASE_QUESTION;
            address->erase_message_frame = 0.0f;
            address->issue = WM_BOARD_ADDRESS_ISSUE_ERASE_CONFIRM;
            address->dialog_phase = ADDRESS_DIALOG_ENTER;
            address->dialog_frame = 0.0f;
            address->dialog_focus = false;
            address->dialog_hovered = false;
            address->erase_yes_focus = false;
            address->erase_yes_hovered = false;
        } else if (address->phase == WM_BOARD_ADDRESS_CONTACT_ERASE_MESSAGE_OUT) {
            if (address->erase_success) {
                address->phase = WM_BOARD_ADDRESS_CONTACT_ERASED_NOTICE;
                address->issue = WM_BOARD_ADDRESS_ISSUE_ERASED;
                address->dialog_phase = ADDRESS_DIALOG_ENTER;
                address->dialog_frame = 0.0f;
                address->dialog_focus = false;
                address->dialog_hovered = false;
            } else {
                address->phase = WM_BOARD_ADDRESS_CONTACT_ERASE_BUTTONS_IN;
            }
        } else if (address->phase == WM_BOARD_ADDRESS_CONTACT_ERASE_BUTTONS_IN) {
            address->phase = WM_BOARD_ADDRESS_CONTACT_READY;
            if (address->erase_save_failed) {
                address->issue = WM_BOARD_ADDRESS_ISSUE_SAVE_ERROR;
                address->dialog_phase = ADDRESS_DIALOG_ENTER;
                address->dialog_frame = 0.0f;
            }
        } else if (address->phase == WM_BOARD_ADDRESS_CONTACT_TO_BOOK) {
            address->phase = WM_BOARD_ADDRESS_BOOK_RETURN;
        } else if (address->phase == WM_BOARD_ADDRESS_BOOK_TO_KIND) {
            address->phase = WM_BOARD_ADDRESS_KIND_ENTER;
        } else if (address->phase == WM_BOARD_ADDRESS_KIND_ENTER) {
            address->phase = WM_BOARD_ADDRESS_KIND_READY;
        } else if (address->phase == WM_BOARD_ADDRESS_KIND_PRESS) {
            address->phase = WM_BOARD_ADDRESS_KIND_TO_FORM;
        } else if (address->phase == WM_BOARD_ADDRESS_KIND_TO_FORM) {
            address->phase = WM_BOARD_ADDRESS_FORM_ENTER;
        } else if (address->phase == WM_BOARD_ADDRESS_FORM_ENTER) {
            address->phase = WM_BOARD_ADDRESS_FORM_READY;
        } else if (address->phase == WM_BOARD_ADDRESS_FORM_OK_PRESS) {
            address->phase = WM_BOARD_ADDRESS_FORM_TO_NICKNAME;
        } else if (address->phase == WM_BOARD_ADDRESS_FORM_TO_NICKNAME) {
            address->phase = WM_BOARD_ADDRESS_NICKNAME_ENTER;
        } else if (address->phase == WM_BOARD_ADDRESS_NICKNAME_ENTER) {
            address->phase = WM_BOARD_ADDRESS_NICKNAME_READY;
        } else if (address->phase == WM_BOARD_ADDRESS_NICKNAME_OK_PRESS) {
            address->phase = WM_BOARD_ADDRESS_NICKNAME_TO_MII;
        } else if (address->phase == WM_BOARD_ADDRESS_NICKNAME_TO_MII) {
            address->phase = WM_BOARD_ADDRESS_MII_ENTER;
        } else if (address->phase == WM_BOARD_ADDRESS_MII_ENTER) {
            address->phase = WM_BOARD_ADDRESS_MII_READY;
        } else if (address->phase == WM_BOARD_ADDRESS_MII_OK_PRESS) {
            address->phase = WM_BOARD_ADDRESS_MII_TO_REVIEW;
        } else if (address->phase == WM_BOARD_ADDRESS_MII_TO_REVIEW) {
            address->phase = WM_BOARD_ADDRESS_REVIEW_ENTER;
        } else if (address->phase == WM_BOARD_ADDRESS_REVIEW_ENTER) {
            address->phase = WM_BOARD_ADDRESS_REVIEW_READY;
        } else if (address->phase == WM_BOARD_ADDRESS_REVIEW_SAVE_EXIT) {
            complete_registration(address);
            if (frames > 0.0f) advance_dialog(address, frames);
        } else if (address->phase == WM_BOARD_ADDRESS_REVIEW_INFO_PRESS) {
            address->phase = WM_BOARD_ADDRESS_REVIEW_READY;
            address->issue = WM_BOARD_ADDRESS_ISSUE_ADDRESS_INFO;
            address->dialog_phase = ADDRESS_DIALOG_ENTER;
            address->dialog_frame = 0.0f;
            address->dialog_focus = false;
            address->dialog_hovered = false;
            if (frames > 0.0f) advance_dialog(address, frames);
        } else if (address->phase == WM_BOARD_ADDRESS_CONTACT_INFO_PRESS) {
            address->phase = WM_BOARD_ADDRESS_CONTACT_READY;
            address->issue = WM_BOARD_ADDRESS_ISSUE_ADDRESS_INFO;
            address->dialog_phase = ADDRESS_DIALOG_ENTER;
            address->dialog_frame = 0.0f;
            address->dialog_focus = false;
            address->dialog_hovered = false;
            if (frames > 0.0f) advance_dialog(address, frames);
        } else if (address->phase == WM_BOARD_ADDRESS_REVIEW_TO_MII) {
            address->phase = WM_BOARD_ADDRESS_MII_RESTORE;
        } else if (address->phase == WM_BOARD_ADDRESS_MII_RESTORE) {
            address->phase = WM_BOARD_ADDRESS_MII_READY;
        } else if (address->phase == WM_BOARD_ADDRESS_MII_TO_NICKNAME) {
            address->phase = WM_BOARD_ADDRESS_NICKNAME_RESTORE;
        } else if (address->phase == WM_BOARD_ADDRESS_NICKNAME_RESTORE) {
            address->phase = WM_BOARD_ADDRESS_NICKNAME_READY;
        } else if (address->phase == WM_BOARD_ADDRESS_NICKNAME_TO_FORM) {
            address->phase = WM_BOARD_ADDRESS_FORM_RESTORE;
        } else if (address->phase == WM_BOARD_ADDRESS_FORM_RESTORE) {
            address->phase = WM_BOARD_ADDRESS_FORM_READY;
        } else if (address->phase == WM_BOARD_ADDRESS_KIND_TO_BOOK ||
                   address->phase == WM_BOARD_ADDRESS_FORM_TO_BOOK) {
            address->phase = WM_BOARD_ADDRESS_BOOK_RETURN;
        } else if (address->phase == WM_BOARD_ADDRESS_BOOK_RETURN) {
            address->phase = WM_BOARD_ADDRESS_READY;
        } else {
            address->phase = WM_BOARD_ADDRESS_CLOSED;
        }
        address->frame = 0.0f;
    }
}

WmBoardAddressStep wm_board_address_step(const WmBoardAddress *address) {
    if (!address) return WM_BOARD_ADDRESS_STEP_BOOK;
    switch (address->phase) {
        case WM_BOARD_ADDRESS_KIND_ENTER:
        case WM_BOARD_ADDRESS_KIND_READY:
        case WM_BOARD_ADDRESS_KIND_PRESS:
        case WM_BOARD_ADDRESS_KIND_TO_FORM:
        case WM_BOARD_ADDRESS_KIND_TO_BOOK:
            return WM_BOARD_ADDRESS_STEP_KIND;
        case WM_BOARD_ADDRESS_FORM_ENTER:
        case WM_BOARD_ADDRESS_FORM_READY:
        case WM_BOARD_ADDRESS_FORM_OK_PRESS:
        case WM_BOARD_ADDRESS_FORM_TO_NICKNAME:
        case WM_BOARD_ADDRESS_FORM_RESTORE:
        case WM_BOARD_ADDRESS_FORM_TO_BOOK:
            return WM_BOARD_ADDRESS_STEP_FORM;
        case WM_BOARD_ADDRESS_NICKNAME_ENTER:
        case WM_BOARD_ADDRESS_NICKNAME_READY:
        case WM_BOARD_ADDRESS_NICKNAME_OK_PRESS:
        case WM_BOARD_ADDRESS_NICKNAME_TO_MII:
        case WM_BOARD_ADDRESS_NICKNAME_RESTORE:
        case WM_BOARD_ADDRESS_NICKNAME_TO_FORM:
        case WM_BOARD_ADDRESS_CONTACT_NAME_FORM_ENTER:
        case WM_BOARD_ADDRESS_CONTACT_NAME_FORM_READY:
        case WM_BOARD_ADDRESS_CONTACT_NAME_FORM_RETURN:
            return WM_BOARD_ADDRESS_STEP_NICKNAME;
        case WM_BOARD_ADDRESS_MII_ENTER:
        case WM_BOARD_ADDRESS_MII_READY:
        case WM_BOARD_ADDRESS_MII_OK_PRESS:
        case WM_BOARD_ADDRESS_MII_TO_REVIEW:
        case WM_BOARD_ADDRESS_MII_RESTORE:
        case WM_BOARD_ADDRESS_MII_TO_NICKNAME:
            return WM_BOARD_ADDRESS_STEP_MII;
        case WM_BOARD_ADDRESS_REVIEW_ENTER:
        case WM_BOARD_ADDRESS_REVIEW_READY:
        case WM_BOARD_ADDRESS_REVIEW_INFO_PRESS:
        case WM_BOARD_ADDRESS_REVIEW_TO_MII:
        case WM_BOARD_ADDRESS_REVIEW_SAVE_EXIT:
        case WM_BOARD_ADDRESS_REGISTERED_NOTICE:
            return WM_BOARD_ADDRESS_STEP_REVIEW;
        case WM_BOARD_ADDRESS_CONTACT_ENTER:
        case WM_BOARD_ADDRESS_CONTACT_READY:
        case WM_BOARD_ADDRESS_CONTACT_INFO_PRESS:
        case WM_BOARD_ADDRESS_CONTACT_NAME_PRESS:
        case WM_BOARD_ADDRESS_CONTACT_NAME_TO_FORM:
        case WM_BOARD_ADDRESS_CONTACT_NAME_CARD_ENTER:
        case WM_BOARD_ADDRESS_CONTACT_ERASE_PRESS:
        case WM_BOARD_ADDRESS_CONTACT_ERASE_BUTTONS_OUT:
        case WM_BOARD_ADDRESS_CONTACT_ERASE_QUESTION:
        case WM_BOARD_ADDRESS_CONTACT_ERASE_MESSAGE_OUT:
        case WM_BOARD_ADDRESS_CONTACT_ERASE_BUTTONS_IN:
        case WM_BOARD_ADDRESS_CONTACT_ERASED_NOTICE:
        case WM_BOARD_ADDRESS_CONTACT_TO_BOOK:
            return WM_BOARD_ADDRESS_STEP_CONTACT;
        case WM_BOARD_ADDRESS_BOOK_ENTRY_PRESS:
        case WM_BOARD_ADDRESS_BOOK_TO_CONTACT:
            return WM_BOARD_ADDRESS_STEP_BOOK;
        default:
            return WM_BOARD_ADDRESS_STEP_BOOK;
    }
}

bool wm_board_address_kind_is_wii(const WmBoardAddress *address) {
    return address ? address->wii_kind : true;
}

const char *wm_board_address_text(const WmBoardAddress *address) {
    return address ? address->text : NULL;
}

const char *wm_board_address_field_text(const WmBoardAddress *address) {
    if (!address) return NULL;
    return wm_board_address_step(address) == WM_BOARD_ADDRESS_STEP_NICKNAME
        ? address->nickname : address->text;
}

/* The original keyboard's type 12 filter keeps ASCII digits; type 7 keeps
 * entered text for validation after OK. Count UTF-16 units like the HTML
 * controller's maxLength field, while retaining UTF-8 for the C font path. */
static bool utf8_character(const char *text, size_t remaining,
                           size_t *bytes, size_t *units) {
    if (!text || remaining == 0 || !bytes || !units) return false;
    unsigned char lead = (unsigned char)text[0];
    uint32_t codepoint;
    if (lead < 0x80) {
        *bytes = 1;
        *units = 1;
        return true;
    }
    size_t length = lead >= 0xF0 && lead <= 0xF4 ? 4 :
                    lead >= 0xE0 && lead <= 0xEF ? 3 :
                    lead >= 0xC2 && lead <= 0xDF ? 2 : 0;
    if (length == 0 || length > remaining) return false;
    codepoint = lead & (length == 4 ? 0x07u : length == 3 ? 0x0Fu : 0x1Fu);
    for (size_t index = 1; index < length; index++) {
        unsigned char next = (unsigned char)text[index];
        if ((next & 0xC0u) != 0x80u) return false;
        codepoint = (codepoint << 6) | (next & 0x3Fu);
    }
    if (codepoint < (length == 2 ? 0x80u : length == 3 ? 0x800u :
                     0x10000u) || codepoint > 0x10FFFFu ||
        (codepoint >= 0xD800u && codepoint <= 0xDFFFu)) return false;
    *bytes = length;
    *units = length == 4 ? 2 : 1;
    return true;
}

static size_t utf16_units(const char *text) {
    size_t length = strlen(text);
    size_t count = 0;
    for (size_t offset = 0; offset < length;) {
        size_t bytes, units;
        if (!utf8_character(text + offset, length - offset,
                            &bytes, &units)) return SIZE_MAX;
        offset += bytes;
        count += units;
    }
    return count;
}

static void format_address(const WmBoardAddress *address, bool shorten,
                           char *output, size_t capacity) {
    if (!address || !output || capacity == 0) return;
    output[0] = '\0';
    if (address->wii_kind) {
        size_t written = 0;
        for (size_t index = 0; index < address->text_bytes; index++) {
            if (index && index % 4 == 0 && written + 1 < capacity)
                output[written++] = ' ';
            if (written + 1 >= capacity) break;
            output[written++] = address->text[index];
        }
        output[written] = '\0';
        return;
    }
    if (!shorten || address->text_units <= 16) {
        snprintf(output, capacity, "%s", address->text);
        return;
    }
    if (capacity < 4) return;
    size_t written = 0;
    size_t units = 0;
    for (size_t offset = 0; offset < address->text_bytes && units < 14;) {
        size_t bytes, character_units;
        if (!utf8_character(address->text + offset,
                            address->text_bytes - offset,
                            &bytes, &character_units) ||
            units + character_units > 14 ||
            written + bytes + 4 > capacity) break;
        memcpy(output + written, address->text + offset, bytes);
        written += bytes;
        offset += bytes;
        units += character_units;
    }
    memcpy(output + written, "...", 4);
}

bool wm_board_address_insert_text(WmBoardAddress *address,
                                   const char *utf8) {
    if (!address || (address->phase != WM_BOARD_ADDRESS_FORM_READY &&
                     address->phase != WM_BOARD_ADDRESS_NICKNAME_READY &&
                     address->phase != WM_BOARD_ADDRESS_CONTACT_NAME_FORM_READY) ||
        address->dialog_phase != ADDRESS_DIALOG_CLOSED || !utf8) return false;
    bool nickname = address->phase == WM_BOARD_ADDRESS_NICKNAME_READY ||
                    address->phase == WM_BOARD_ADDRESS_CONTACT_NAME_FORM_READY;
    bool numeric = !nickname && address->wii_kind;
    size_t input_bytes = strlen(utf8);
    char accepted[ADDRESS_TEXT_CAPACITY];
    size_t output_bytes = 0;
    size_t output_units = 0;
    for (size_t offset = 0; offset < input_bytes;) {
        size_t bytes, units;
        if (!utf8_character(utf8 + offset, input_bytes - offset,
                            &bytes, &units)) return false;
        unsigned char first = (unsigned char)utf8[offset];
        bool keep = numeric
            ? bytes == 1 && first >= '0' && first <= '9'
            : !(bytes == 1 && (first == '\r' || first == '\n'));
        if (keep) {
            if (bytes == 1 && first < 0x20u) return false;
            if (output_bytes + bytes >= sizeof(accepted)) return false;
            memcpy(accepted + output_bytes, utf8 + offset, bytes);
            output_bytes += bytes;
            output_units += units;
        }
        offset += bytes;
    }
    size_t limit = nickname ? 10 : numeric ? 16 : 99;
    char *field = nickname ? address->nickname : address->text;
    size_t *field_bytes = nickname ? &address->nickname_bytes :
                                     &address->text_bytes;
    size_t *field_units = nickname ? &address->nickname_units :
                                     &address->text_units;
    size_t capacity = nickname ? sizeof(address->nickname) :
                                 sizeof(address->text);
    if (output_bytes == 0 || *field_bytes + output_bytes >= capacity ||
        *field_units + output_units > limit) return false;
    memcpy(field + *field_bytes, accepted, output_bytes);
    *field_bytes += output_bytes;
    *field_units += output_units;
    field[*field_bytes] = '\0';
    return true;
}

bool wm_board_address_backspace(WmBoardAddress *address) {
    if (!address || (address->phase != WM_BOARD_ADDRESS_FORM_READY &&
                     address->phase != WM_BOARD_ADDRESS_NICKNAME_READY &&
                     address->phase != WM_BOARD_ADDRESS_CONTACT_NAME_FORM_READY) ||
        address->dialog_phase != ADDRESS_DIALOG_CLOSED) return false;
    bool nickname = address->phase == WM_BOARD_ADDRESS_NICKNAME_READY ||
                    address->phase == WM_BOARD_ADDRESS_CONTACT_NAME_FORM_READY;
    char *field = nickname ? address->nickname : address->text;
    size_t *field_bytes = nickname ? &address->nickname_bytes :
                                     &address->text_bytes;
    size_t *field_units = nickname ? &address->nickname_units :
                                     &address->text_units;
    if (*field_bytes == 0) return false;
    size_t first = *field_bytes - 1;
    while (first > 0 &&
           ((unsigned char)field[first] & 0xC0u) == 0x80u) first--;
    size_t removed = *field_bytes - first;
    *field_bytes = first;
    *field_units -= removed == 4 ? 2 : 1;
    field[first] = '\0';
    return true;
}

static uint64_t rotate53(uint64_t value, unsigned bits) {
    const uint64_t mask = (UINT64_C(1) << 53) - 1;
    return ((value << bits) | (value >> (53 - bits))) & mask;
}

static bool valid_wii_number(const char *text) {
    if (!text || strlen(text) != 16) return false;
    uint64_t decimal = 0;
    for (size_t index = 0; index < 16; index++) {
        if (text[index] < '0' || text[index] > '9') return false;
        decimal = decimal * 10 + (uint64_t)(text[index] - '0');
    }
    static const uint8_t nibble[16] = {
        13, 5, 9, 7, 0, 15, 10, 2, 12, 3, 14, 1, 8, 6, 11, 4
    };
    static const uint8_t permutation[6] = {1, 5, 0, 4, 2, 3};
    const uint64_t mask = (UINT64_C(1) << 53) - 1;
    uint64_t rotated = rotate53((decimal & mask) ^
                                UINT64_C(0x5e5e5e5e5e5e), 52);
    uint64_t transformed = rotated & (UINT64_C(31) << 48);
    for (unsigned target = 0; target < 6; target++) {
        unsigned byte = (unsigned)((rotated >>
                                   (permutation[target] * 8)) & 255u);
        unsigned substituted = ((unsigned)nibble[byte >> 4] << 4) |
                               (unsigned)nibble[byte & 15u];
        transformed |= (uint64_t)substituted << (target * 8);
    }
    uint64_t remainder = rotate53(transformed, 10) ^
                         UINT64_C(0xb3b3b3b3b3b3);
    for (int shift = 42; shift >= 0; shift--) {
        if (remainder & (UINT64_C(1) << (shift + 10)))
            remainder ^= UINT64_C(0x635) << shift;
    }
    return remainder == 0;
}

static bool valid_email(const char *text) {
    size_t length = strlen(text);
    if (length == 0 || length > 99) return false;
    const char *separator = strchr(text, '@');
    if (!separator || separator == text || !separator[1]) return false;
    for (const char *cursor = text; cursor < separator; cursor++) {
        unsigned char value = (unsigned char)*cursor;
        if (value < 0x21u || value > 0x7Eu ||
            strchr("()<>[]:;\\,\"", value)) return false;
    }
    const char *domain = separator + 1;
    if (*domain == '.') return false;
    for (const char *cursor = domain; *cursor; cursor++) {
        unsigned char value = (unsigned char)*cursor;
        if (!((value >= 'A' && value <= 'Z') ||
              (value >= 'a' && value <= 'z') ||
              (value >= '0' && value <= '9') ||
              value == '_' || value == '.' || value == '-')) return false;
        if (value == '.' && cursor[1] == '.') return false;
    }
    if (domain[strlen(domain) - 1] == '.') return false;
    static const char reserved[] = "wii.com";
    if (strlen(domain) == sizeof(reserved) - 1) {
        bool equal = true;
        for (size_t index = 0; index < sizeof(reserved) - 1; index++) {
            unsigned char value = (unsigned char)domain[index];
            if (value >= 'A' && value <= 'Z') value += 'a' - 'A';
            if (value != (unsigned char)reserved[index]) equal = false;
        }
        if (equal) return false;
    }
    return true;
}

WmBoardAddressIssue wm_board_address_validate(
    const WmBoardAddress *address) {
    if (!address) return WM_BOARD_ADDRESS_ISSUE_INVALID_WII;
    if (address->contacts && address->text[0]) {
        for (size_t slot = 0;
             slot < wm_board_contact_store_length(address->contacts);
             slot++) {
            WmBoardContact contact;
            if (wm_board_contact_store_get(address->contacts, slot,
                                            &contact) &&
                contact.wii == address->wii_kind &&
                strcmp(contact.address, address->text) == 0) {
                return address->wii_kind
                    ? WM_BOARD_ADDRESS_ISSUE_DUPLICATE_WII :
                      WM_BOARD_ADDRESS_ISSUE_DUPLICATE_EMAIL;
            }
        }
    }
    if (address->wii_kind)
        return valid_wii_number(address->text) ?
            WM_BOARD_ADDRESS_ISSUE_NONE : WM_BOARD_ADDRESS_ISSUE_INVALID_WII;
    return valid_email(address->text) ?
        WM_BOARD_ADDRESS_ISSUE_NONE : WM_BOARD_ADDRESS_ISSUE_INVALID_EMAIL;
}

bool wm_board_address_field_valid(const WmBoardAddress *address) {
    if (!address) return false;
    if (address->phase == WM_BOARD_ADDRESS_FORM_READY)
        return wm_board_address_validate(address) ==
               WM_BOARD_ADDRESS_ISSUE_NONE;
    if (address->phase == WM_BOARD_ADDRESS_NICKNAME_READY ||
        address->phase == WM_BOARD_ADDRESS_CONTACT_NAME_FORM_READY) {
        for (size_t index = 0; index < address->nickname_bytes; index++) {
            unsigned char value = (unsigned char)address->nickname[index];
            if (value != ' ' && value != '\t' && value != '\r' &&
                value != '\n') return true;
        }
    }
    if (address->phase == WM_BOARD_ADDRESS_MII_READY)
        return true;
    if (address->phase == WM_BOARD_ADDRESS_REVIEW_READY)
        return true;
    return false;
}

bool wm_board_address_submit(WmBoardAddress *address) {
    if (!wm_board_address_field_valid(address) ||
        address->dialog_phase != ADDRESS_DIALOG_CLOSED) return false;
    if (address->phase == WM_BOARD_ADDRESS_FORM_READY) {
        address->phase = WM_BOARD_ADDRESS_FORM_OK_PRESS;
        address->frame = 0.0f;
        return true;
    }
    if (address->phase == WM_BOARD_ADDRESS_NICKNAME_READY) {
        address->phase = WM_BOARD_ADDRESS_NICKNAME_OK_PRESS;
        address->frame = 0.0f;
        return true;
    }
    if (address->phase == WM_BOARD_ADDRESS_CONTACT_NAME_FORM_READY) {
        if (!wm_board_contact_store_rename(address->contacts,
                                            address->selected_slot,
                                            address->nickname,
                                            address->save_error,
                                            sizeof(address->save_error))) {
            address->issue = WM_BOARD_ADDRESS_ISSUE_SAVE_ERROR;
            address->dialog_phase = ADDRESS_DIALOG_ENTER;
            address->dialog_frame = 0.0f;
            return true;
        }
        address->phase = WM_BOARD_ADDRESS_CONTACT_NAME_FORM_RETURN;
        address->frame = 0.0f;
        return true;
    }
    if (address->phase == WM_BOARD_ADDRESS_MII_READY) {
        address->phase = WM_BOARD_ADDRESS_MII_OK_PRESS;
        address->frame = 0.0f;
        return true;
    }
    if (address->phase == WM_BOARD_ADDRESS_REVIEW_READY) {
        address->phase = WM_BOARD_ADDRESS_REVIEW_SAVE_EXIT;
        address->frame = 0.0f;
        address->save_error[0] = '\0';
        return true;
    }
    return false;
}

bool wm_board_address_show_issue(WmBoardAddress *address) {
    if (!address || address->phase != WM_BOARD_ADDRESS_FORM_READY ||
        address->dialog_phase != ADDRESS_DIALOG_CLOSED ||
        address->text_bytes == 0) return false;
    WmBoardAddressIssue issue = wm_board_address_validate(address);
    if (issue == WM_BOARD_ADDRESS_ISSUE_NONE) return false;
    address->issue = issue;
    address->dialog_phase = ADDRESS_DIALOG_ENTER;
    address->dialog_frame = 0.0f;
    address->dialog_focus = false;
    address->dialog_hovered = false;
    return true;
}

bool wm_board_address_show_no_mii(WmBoardAddress *address) {
    if (!address || address->phase != WM_BOARD_ADDRESS_MII_READY ||
        address->dialog_phase != ADDRESS_DIALOG_CLOSED) return false;
    address->issue = WM_BOARD_ADDRESS_ISSUE_NO_MII;
    address->dialog_phase = ADDRESS_DIALOG_ENTER;
    address->dialog_frame = 0.0f;
    address->dialog_focus = false;
    address->dialog_hovered = false;
    return true;
}

bool wm_board_address_show_memo_no_mii(WmBoardAddress *address) {
    if (!address || address->phase != WM_BOARD_ADDRESS_CLOSED ||
        address->dialog_phase != ADDRESS_DIALOG_CLOSED) return false;
    address->issue = WM_BOARD_ADDRESS_ISSUE_NO_MII;
    address->dialog_phase = ADDRESS_DIALOG_ENTER;
    address->dialog_frame = 0.0f;
    address->dialog_focus = false;
    address->dialog_hovered = false;
    return true;
}

bool wm_board_address_show_address_info(WmBoardAddress *address) {
    if (!address || (address->phase != WM_BOARD_ADDRESS_REVIEW_READY &&
                     address->phase != WM_BOARD_ADDRESS_CONTACT_READY) ||
        address->dialog_phase != ADDRESS_DIALOG_CLOSED) return false;
    /* The source waits for the 21-frame value-button press before opening
     * the WAD notice. Its per-pane binding contains no visual tracks here. */
    address->phase = address->phase == WM_BOARD_ADDRESS_CONTACT_READY
        ? WM_BOARD_ADDRESS_CONTACT_INFO_PRESS :
          WM_BOARD_ADDRESS_REVIEW_INFO_PRESS;
    address->frame = 0.0f;
    return true;
}

bool wm_board_address_change_nickname(WmBoardAddress *address) {
    if (!address || address->phase != WM_BOARD_ADDRESS_CONTACT_READY ||
        address->dialog_phase != ADDRESS_DIALOG_CLOSED ||
        address->selected_slot >= WM_BOARD_CONTACT_CAPACITY) return false;
    address->phase = WM_BOARD_ADDRESS_CONTACT_NAME_PRESS;
    address->frame = 0.0f;
    return true;
}

bool wm_board_address_erase(WmBoardAddress *address) {
    if (!address || address->phase != WM_BOARD_ADDRESS_CONTACT_READY ||
        address->dialog_phase != ADDRESS_DIALOG_CLOSED ||
        address->selected_slot >= WM_BOARD_CONTACT_CAPACITY) return false;
    address->phase = WM_BOARD_ADDRESS_CONTACT_ERASE_PRESS;
    address->frame = 0.0f;
    address->erase_yes_selected = false;
    address->erase_success = false;
    address->erase_save_failed = false;
    return true;
}

bool wm_board_address_dialog_active(const WmBoardAddress *address) {
    return address && address->dialog_phase != ADDRESS_DIALOG_CLOSED;
}

bool wm_board_address_dialog_accept(WmBoardAddress *address) {
    if (!address || address->dialog_phase != ADDRESS_DIALOG_READY)
        return false;
    if (address->issue == WM_BOARD_ADDRESS_ISSUE_ERASE_CONFIRM)
        return false;
    address->dialog_phase = ADDRESS_DIALOG_PRESS;
    address->dialog_frame = 0.0f;
    return true;
}

bool wm_board_address_dialog_choose(WmBoardAddress *address, bool yes) {
    if (!address || address->issue != WM_BOARD_ADDRESS_ISSUE_ERASE_CONFIRM ||
        address->dialog_phase != ADDRESS_DIALOG_READY) return false;
    address->erase_yes_selected = yes;
    address->dialog_phase = ADDRESS_DIALOG_PRESS;
    address->dialog_frame = 0.0f;
    return true;
}

void wm_board_address_dialog_hover(WmBoardAddress *address, bool hovering) {
    if (!address || address->dialog_phase != ADDRESS_DIALOG_READY ||
        address->dialog_hovered == hovering) return;
    address->dialog_hovered = hovering;
    address->dialog_focus = true;
    address->dialog_focus_entering = hovering;
    address->dialog_focus_frame = 0.0f;
}

void wm_board_address_dialog_hover_choice(WmBoardAddress *address,
                                          bool yes, bool hovering) {
    if (!address || address->issue != WM_BOARD_ADDRESS_ISSUE_ERASE_CONFIRM ||
        address->dialog_phase != ADDRESS_DIALOG_READY) return;
    bool *hovered = yes ? &address->erase_yes_hovered :
                          &address->dialog_hovered;
    bool *focus = yes ? &address->erase_yes_focus :
                        &address->dialog_focus;
    bool *entering = yes ? &address->erase_yes_focus_entering :
                           &address->dialog_focus_entering;
    float *frame = yes ? &address->erase_yes_focus_frame :
                         &address->dialog_focus_frame;
    if (*hovered == hovering) return;
    *hovered = hovering;
    if (*focus && *entering != hovering)
        *frame = 10.0f - limit_frame(*frame, 10.0f);
    else if (!*focus)
        *frame = 0.0f;
    *focus = true;
    *entering = hovering;
}

WmBoardAddressPhase wm_board_address_phase(const WmBoardAddress *address) {
    return address ? address->phase : WM_BOARD_ADDRESS_CLOSED;
}

unsigned wm_board_address_page(const WmBoardAddress *address) {
    return address ? address->page : 0;
}

float wm_board_address_phase_frame(const WmBoardAddress *address) {
    return address ? address->frame : 0.0f;
}

static void pose_sheet(WmBoardAddress *address, char sheet,
                       float x, float y, float group_alpha) {
    static const char *const panes[] = {
        "N_note_a", "N_note_b", "N_note_c", "N_note_d", "N_note_e"
    };
    for (unsigned index = 0; index < 5; index++) {
        wm_layout_set_pane_visible(address->book, panes[index],
                                   panes[index][7] == sheet);
    }
    char name[] = "N_note_a";
    name[7] = sheet;
    wm_layout_set_pane_translation(address->book, name, x, y, 0.0f);
    /* Native Address::draw recalculates a/d/e with an independent alpha root.
     * b/c remain under the parent G_note_all alpha during entrance and exit. */
    wm_layout_set_pane_alpha(address->book, "N_note_all",
                              sheet == 'b' || sheet == 'c'
                                  ? group_alpha : 255.0f);
}

static void show_sheet(WmBoardAddress *address, char sheet,
                       float x, float y, float group_alpha) {
    pose_sheet(address, sheet, x, y, group_alpha);
    wm_layout_present_with_fonts(address->platform, address->textures,
                                 address->fonts, address->book, true,
                                 WM_LAYOUT_IPL, NULL);
}

static bool capture_group_alpha(void *context, const WmLayoutPaneView *pane) {
    if (pane->name && strcmp(pane->name, "N_note_all") == 0) {
        *(float *)context = pane->alpha * 255.0f;
    }
    return true;
}

static void pose_kind(WmBoardAddress *address) {
    WmLayoutClip clips[5];
    size_t count = 0;
    float start_frame = address->phase == WM_BOARD_ADDRESS_KIND_ENTER
        ? limit_frame(address->frame, 18.0f) : 18.0f;
    clips[count++] = (WmLayoutClip){
        .animation = "th_Adress_d_btn_strt",
        .group = "G_btn_strt_fnsh",
        .frame = start_frame,
        .loop_override = 0
    };
    for (unsigned index = 0; index < 2; index++) {
        if (!address->kind_focus[index]) continue;
        clips[count++] = (WmLayoutClip){
            .animation = address->kind_focus_entering[index]
                ? "th_Adress_d_btn_in" : "th_Adress_d_btn_out",
            .group = index == 0 ? "G_btn_00" : "G_btn_01",
            .frame = limit_frame(address->kind_focus_frame[index], 6.0f),
            .loop_override = 0
        };
    }
    if (address->phase == WM_BOARD_ADDRESS_KIND_PRESS ||
        address->phase == WM_BOARD_ADDRESS_KIND_TO_FORM) {
        clips[count++] = (WmLayoutClip){
            .animation = "th_Adress_d_btn_psh",
            .group = address->wii_kind ? "G_btn_00" : "G_btn_01",
            .frame = address->phase == WM_BOARD_ADDRESS_KIND_PRESS
                ? limit_frame(address->frame, 20.0f) : 20.0f,
            .loop_override = 0
        };
    }
    if (address->phase == WM_BOARD_ADDRESS_KIND_TO_FORM ||
        address->phase == WM_BOARD_ADDRESS_KIND_TO_BOOK) {
        clips[count++] = (WmLayoutClip){
            .animation = "th_Adress_d_btn_fnsh",
            .group = "G_btn_strt_fnsh",
            .frame = limit_frame(address->frame, 18.0f),
            .loop_override = 0
        };
    }
    wm_layout_pose(address->kind_layout, clips, count);
    wm_layout_set_pose_text(address->kind_layout, "T_btn_name_00", "Wii");
    wm_layout_set_pose_text(address->kind_layout, "T_btn_name_01", "Others");
}

static void pose_form(WmBoardAddress *address) {
    bool contact_name = address->phase ==
                            WM_BOARD_ADDRESS_CONTACT_NAME_FORM_ENTER ||
                        address->phase ==
                            WM_BOARD_ADDRESS_CONTACT_NAME_FORM_READY ||
                        address->phase ==
                            WM_BOARD_ADDRESS_CONTACT_NAME_FORM_RETURN;
    bool entering = address->phase == WM_BOARD_ADDRESS_FORM_ENTER ||
                    address->phase == WM_BOARD_ADDRESS_NICKNAME_ENTER ||
                    address->phase == WM_BOARD_ADDRESS_CONTACT_NAME_FORM_ENTER ||
                    address->phase == WM_BOARD_ADDRESS_MII_ENTER ||
                    address->phase == WM_BOARD_ADDRESS_NICKNAME_RESTORE ||
                    address->phase == WM_BOARD_ADDRESS_FORM_RESTORE;
    float frame = entering ? address->frame : 21.0f;
    bool card_entering = address->phase == WM_BOARD_ADDRESS_FORM_ENTER ||
                         address->phase == WM_BOARD_ADDRESS_NICKNAME_ENTER ||
                         address->phase == WM_BOARD_ADDRESS_CONTACT_NAME_FORM_ENTER ||
                         address->phase == WM_BOARD_ADDRESS_MII_RESTORE;
    bool mii = wm_board_address_step(address) == WM_BOARD_ADDRESS_STEP_MII;
    if (address->phase == WM_BOARD_ADDRESS_MII_RESTORE)
        frame = 21.0f;
    WmLayoutClip clips[5] = {
        {.animation = "th_Adress_c_card_strt",
         .group = "G_card_strt_fnsh",
         .frame = card_entering ? limit_frame(address->frame, 18.0f) :
                                  18.0f,
         .loop_override = 0},
        {.animation = "th_Adress_c_question_alp_in",
         .group = "G_question_00",
         .frame = limit_frame(frame, 20.0f), .loop_override = 0},
        {.animation = mii ? "th_Adress_c_mii_alp_in" :
                            "th_Adress_c_name_alp_in",
         .group = mii ? "G_mii" : "G_name_00",
         .frame = limit_frame(frame, 20.0f), .loop_override = 0}
    };
    size_t count = 3;
    if (address->phase == WM_BOARD_ADDRESS_FORM_TO_BOOK ||
        address->phase == WM_BOARD_ADDRESS_FORM_TO_NICKNAME ||
        address->phase == WM_BOARD_ADDRESS_MII_TO_REVIEW ||
        address->phase == WM_BOARD_ADDRESS_CONTACT_NAME_FORM_RETURN) {
        clips[count++] = (WmLayoutClip){
            .animation = "th_Adress_c_card_fnsh",
            .group = "G_card_strt_fnsh",
            .frame = limit_frame(address->frame, 18.0f),
            .loop_override = 0
        };
    }
    if (address->phase == WM_BOARD_ADDRESS_NICKNAME_TO_FORM ||
        address->phase == WM_BOARD_ADDRESS_NICKNAME_TO_MII ||
        address->phase == WM_BOARD_ADDRESS_MII_TO_NICKNAME) {
        bool mii_out = address->phase == WM_BOARD_ADDRESS_MII_TO_NICKNAME;
        clips[count++] = (WmLayoutClip){
            .animation = "th_Adress_c_question_alp_out",
            .group = "G_question_00",
            .frame = limit_frame(address->frame, 20.0f),
            .loop_override = 0
        };
        clips[count++] = (WmLayoutClip){
            .animation = mii_out ? "th_Adress_c_mii_alp_out" :
                                    "th_Adress_c_name_alp_out",
            .group = mii_out ? "G_mii" : "G_name_00",
            .frame = limit_frame(address->frame, 20.0f),
            .loop_override = 0
        };
    }
    wm_layout_pose(address->form, clips, count);
    bool nickname = wm_board_address_step(address) ==
                    WM_BOARD_ADDRESS_STEP_NICKNAME;
    wm_layout_set_pose_text(address->form, "T_question_00",
        mii ? "You can attach a Mii." :
        contact_name ? "Nickname" :
        nickname ? "Apply a nickname." :
        address->wii_kind ? "Enter a Wii Number." :
                            "Enter an e-mail address.");
    wm_layout_set_pose_text(address->form, "T_name_00",
                              mii ? "" : nickname ? address->nickname :
                                               address->text);
    wm_layout_set_pose_text(address->form, "T_msg_00",
                            contact_name ? "Apply nickname" : "");
    wm_layout_set_pose_text(address->form, "T_mii_msg_00",
                              mii ? "←Add a Mii" : "");
    wm_layout_set_pane_visible(address->form, "N_mii_all", mii);
}

static void pose_review(WmBoardAddress *address) {
    bool contact = wm_board_address_step(address) ==
                   WM_BOARD_ADDRESS_STEP_CONTACT;
    WmLayoutClip clips[18];
    size_t count = 0;
    static const char *const buttons[] = {
        "crd_btn_00", "crd_btn_10", "crd_btn_11", "crd_btn_gry"
    };
    for (size_t index = 0; index < 4; index++) {
        clips[count++] = (WmLayoutClip){
            .animation = "th_Adress_b_btn_scl_in",
            .group = buttons[index],
            .frame = contact ? 10.0f : 0.0f,
            .loop_override = 0
        };
    }
    for (size_t index = 0; index < 2; index++) {
        if (!contact || !address->contact_focus[index]) continue;
        clips[count++] = (WmLayoutClip){
            .animation = address->contact_focus_entering[index]
                ? "th_Adress_b_btn_in" : "th_Adress_b_btn_out",
            .group = index == 0 ? "crd_btn_10" : "crd_btn_11",
            .frame = limit_frame(address->contact_focus_frame[index], 6.0f),
            .loop_override = 0
        };
    }
    clips[count++] = (WmLayoutClip){
        .animation = "th_Adress_b_card_strt",
        .group = "card_strt_fnsh",
        .frame = address->phase == WM_BOARD_ADDRESS_REVIEW_ENTER ||
                 address->phase == WM_BOARD_ADDRESS_CONTACT_ENTER ||
                 address->phase == WM_BOARD_ADDRESS_CONTACT_NAME_CARD_ENTER
            ? limit_frame(address->frame, 18.0f) : 18.0f,
        .loop_override = 0
    };
    clips[count++] = (WmLayoutClip){
        .animation = "th_Adress_b_card_msg_alp_in",
        .group = "card_msg",
        .frame = address->phase == WM_BOARD_ADDRESS_REVIEW_ENTER
            ? limit_frame(address->frame, 20.0f) : 20.0f,
        .loop_override = 0
    };
    if (address->phase == WM_BOARD_ADDRESS_CONTACT_NAME_PRESS ||
        address->phase == WM_BOARD_ADDRESS_CONTACT_ERASE_PRESS) {
        clips[count++] = (WmLayoutClip){
            .animation = "th_Adress_b_btn_psh",
            .group = address->phase == WM_BOARD_ADDRESS_CONTACT_NAME_PRESS
                ? "crd_btn_10" : "crd_btn_11",
            .frame = limit_frame(address->frame, 20.0f),
            .loop_override = 0
        };
    }
    bool erasing = address->phase ==
                       WM_BOARD_ADDRESS_CONTACT_ERASE_BUTTONS_OUT ||
                   address->phase == WM_BOARD_ADDRESS_CONTACT_ERASE_QUESTION ||
                   address->phase == WM_BOARD_ADDRESS_CONTACT_ERASE_MESSAGE_OUT ||
                   address->phase == WM_BOARD_ADDRESS_CONTACT_ERASED_NOTICE;
    if (erasing) {
        for (size_t index = 0; index < 3; index++) {
            clips[count++] = (WmLayoutClip){
                .animation = "th_Adress_b_btn_scl_out",
                .group = buttons[index],
                .frame = address->phase ==
                             WM_BOARD_ADDRESS_CONTACT_ERASE_BUTTONS_OUT
                    ? limit_frame(address->frame, 10.0f) : 10.0f,
                .loop_override = 0
            };
        }
    }
    if (address->phase == WM_BOARD_ADDRESS_CONTACT_ERASE_BUTTONS_IN) {
        for (size_t index = 0; index < 3; index++) {
            clips[count++] = (WmLayoutClip){
                .animation = "th_Adress_b_btn_scl_in",
                .group = buttons[index],
                .frame = limit_frame(address->frame, 10.0f),
                .loop_override = 0
            };
        }
    }
    if (address->phase == WM_BOARD_ADDRESS_CONTACT_ERASE_QUESTION ||
        address->phase == WM_BOARD_ADDRESS_CONTACT_ERASE_MESSAGE_OUT ||
        address->phase == WM_BOARD_ADDRESS_CONTACT_ERASED_NOTICE) {
        clips[count++] = (WmLayoutClip){
            .animation = "th_Adress_b_card_msg_alp_in",
            .group = "card_msg",
            .frame = address->phase == WM_BOARD_ADDRESS_CONTACT_ERASE_QUESTION
                ? address->erase_message_frame : 20.0f,
            .loop_override = 0
        };
    }
    if (address->phase == WM_BOARD_ADDRESS_CONTACT_ERASE_MESSAGE_OUT ||
        address->phase == WM_BOARD_ADDRESS_CONTACT_ERASE_BUTTONS_IN ||
        address->phase == WM_BOARD_ADDRESS_CONTACT_ERASED_NOTICE) {
        clips[count++] = (WmLayoutClip){
            .animation = "th_Adress_b_card_msg_alp_out",
            .group = "card_msg",
            .frame = address->phase == WM_BOARD_ADDRESS_CONTACT_ERASE_MESSAGE_OUT
                ? limit_frame(address->frame, 20.0f) : 20.0f,
            .loop_override = 0
        };
    }
    if (address->phase == WM_BOARD_ADDRESS_REVIEW_TO_MII ||
        address->phase == WM_BOARD_ADDRESS_REVIEW_SAVE_EXIT ||
        address->phase == WM_BOARD_ADDRESS_REGISTERED_NOTICE ||
        address->phase == WM_BOARD_ADDRESS_CONTACT_TO_BOOK ||
        address->phase == WM_BOARD_ADDRESS_CONTACT_NAME_TO_FORM) {
        clips[count++] = (WmLayoutClip){
            .animation = "th_Adress_b_card_fnsh",
            .group = "card_strt_fnsh",
            .frame = address->phase == WM_BOARD_ADDRESS_REGISTERED_NOTICE
                ? 18.0f : limit_frame(address->frame, 18.0f),
            .loop_override = 0
        };
    }
    wm_layout_pose(address->review, clips, count);
    char shown_address[ADDRESS_TEXT_CAPACITY + 4];
    format_address(address, true, shown_address, sizeof(shown_address));
    wm_layout_set_pose_text(address->review, "T_name_00", address->nickname);
    wm_layout_set_pose_text(address->review, "T_frnd_crd_00",
                            shown_address);
    wm_layout_set_pose_text(address->review, "T_card_msg_00",
        erasing || address->phase == WM_BOARD_ADDRESS_CONTACT_ERASE_BUTTONS_IN
            ? "Erase this?" :
        contact ? "" :
        "This information has been\nadded to your address book.");
    wm_layout_set_pose_text(address->review, "T_crd_btn_00", "Send Message");
    wm_layout_set_pose_text(address->review, "T_crd_btn_10",
                              "Change\nNickname");
    wm_layout_set_pose_text(address->review, "T_crd_btn_11", "Erase");
    wm_layout_set_pose_text(address->review, "T_crd_btn_gry", "Send Message");
}

static void pose_dialog(WmBoardAddress *address) {
    bool erase = address->issue == WM_BOARD_ADDRESS_ISSUE_ERASE_CONFIRM;
    WmLayout *layout = erase ? address->erase_dialog : address->dialog;
    const char *prefix = erase ? "my_DialogWindow_b_" :
                                 "my_DialogWindow_a1_";
    WmLayoutClip clips[5];
    char animation[64];
    size_t count = 0;
    snprintf(animation, sizeof(animation), "%sDialogIn", prefix);
    clips[count++] = (WmLayoutClip){
        .animation = animation,
        .group = "G_InOut",
        .frame = address->dialog_phase == ADDRESS_DIALOG_ENTER
            ? limit_frame(address->dialog_frame, erase ? 25.0f : 24.0f)
            : erase ? 25.0f : 24.0f,
        .loop_override = 0
    };
    char focus_b[64], focus_a[64], select[64], dialog_out[64];
    snprintf(focus_b, sizeof(focus_b), "%sFocusBtn_%s", prefix,
             address->dialog_focus_entering ? "on" : "off");
    snprintf(focus_a, sizeof(focus_a), "%sFocusBtn_%s", prefix,
             address->erase_yes_focus_entering ? "on" : "off");
    snprintf(select, sizeof(select), "%sSelectBtn_Ac", prefix);
    snprintf(dialog_out, sizeof(dialog_out), "%sDialogOut", prefix);
    if (address->dialog_focus) {
        clips[count++] = (WmLayoutClip){
            .animation = focus_b,
            .group = "G_FocusBtnB",
            .frame = limit_frame(address->dialog_focus_frame,
                                 erase ? 10.0f : 6.0f),
            .loop_override = 0
        };
    }
    if (erase && address->erase_yes_focus) {
        clips[count++] = (WmLayoutClip){
            .animation = focus_a,
            .group = "G_FocusBtnA",
            .frame = limit_frame(address->erase_yes_focus_frame, 10.0f),
            .loop_override = 0
        };
    }
    if (address->dialog_phase == ADDRESS_DIALOG_PRESS ||
        address->dialog_phase == ADDRESS_DIALOG_EXIT) {
        clips[count++] = (WmLayoutClip){
            .animation = select,
            .group = erase && address->erase_yes_selected
                ? "G_SelectBtnA" : "G_SelectBtnB",
            .frame = address->dialog_phase == ADDRESS_DIALOG_PRESS
                ? limit_frame(address->dialog_frame, erase ? 20.0f : 16.0f)
                : erase ? 20.0f : 16.0f,
            .loop_override = 0
        };
    }
    if (address->dialog_phase == ADDRESS_DIALOG_EXIT) {
        clips[count++] = (WmLayoutClip){
            .animation = dialog_out,
            .group = "G_InOut",
            .frame = limit_frame(address->dialog_frame,
                                 erase ? 25.0f : 20.0f),
            .loop_override = 0
        };
    }
    wm_layout_pose(layout, clips, count);
    wm_layout_set_pane_visible(layout, "N_Top", !erase);
    const char *message = "";
    char shown_address[ADDRESS_TEXT_CAPACITY + 4];
    switch (address->issue) {
        case WM_BOARD_ADDRESS_ISSUE_INVALID_WII:
            message = "This Wii Number is incorrect.";
            break;
        case WM_BOARD_ADDRESS_ISSUE_INVALID_EMAIL:
            message = "The information you entered\nis incorrect. Please check "
                      "the\ninformation and try again.";
            break;
        case WM_BOARD_ADDRESS_ISSUE_NO_MII:
            message = "No Miis have been registered.\nPlease use the Mii "
                      "Channel to\ncreate a Mii.";
            break;
        case WM_BOARD_ADDRESS_ISSUE_ADDRESS_INFO:
            format_address(address, false, shown_address,
                           sizeof(shown_address));
            message = shown_address;
            break;
        case WM_BOARD_ADDRESS_ISSUE_DUPLICATE_WII:
            message = "That Wii Number is already registered.";
            break;
        case WM_BOARD_ADDRESS_ISSUE_DUPLICATE_EMAIL:
            message = "That e-mail address is already registered.";
            break;
        case WM_BOARD_ADDRESS_ISSUE_BOOK_FULL:
            message = "Your address book is full, so you\ncan't register "
                      "a new address.";
            break;
        case WM_BOARD_ADDRESS_ISSUE_REGISTERED:
            message = "The address has been registered.\nTo exchange "
                      "messages, you must both\nregister one another and "
                      "configure\nyour Internet settings.";
            break;
        case WM_BOARD_ADDRESS_ISSUE_ERASE_CONFIRM:
            break;
        case WM_BOARD_ADDRESS_ISSUE_ERASED:
            message = "That Wii Friend has been erased.";
            break;
        case WM_BOARD_ADDRESS_ISSUE_SAVE_ERROR:
            message = "Could not save the Address Book.\nPlease try again.";
            break;
        case WM_BOARD_ADDRESS_ISSUE_NONE:
            break;
    }
    wm_layout_set_pose_text(layout, "T_Dialog", message);
    wm_layout_set_pose_text(layout, "T_BtnB", erase ? "No" : "OK");
    if (erase) wm_layout_set_pose_text(layout, "T_BtnA", "Yes");
}

static bool hit_layout_pane(WmLayout *layout, const char *name, int x, int y) {
    WmSourceRect rect;
    return wm_source_pane_rect(layout, name, true, WM_LAYOUT_IPL,
                               NULL, &rect) &&
           (float)x >= rect.x && (float)x < rect.x + rect.width &&
           (float)y >= rect.y && (float)y < rect.y + rect.height;
}

bool wm_board_address_form_hit(WmBoardAddress *address, int x, int y) {
    if (!address || (address->phase != WM_BOARD_ADDRESS_FORM_READY &&
                     address->phase != WM_BOARD_ADDRESS_NICKNAME_READY &&
                     address->phase != WM_BOARD_ADDRESS_CONTACT_NAME_FORM_READY &&
                     address->phase != WM_BOARD_ADDRESS_MII_READY) ||
        address->dialog_phase != ADDRESS_DIALOG_CLOSED) return false;
    pose_form(address);
    return hit_layout_pane(address->form, "B_crd_edgi_00", x, y);
}

bool wm_board_address_review_hit(WmBoardAddress *address, int x, int y) {
    if (!address || (address->phase != WM_BOARD_ADDRESS_REVIEW_READY &&
                     address->phase != WM_BOARD_ADDRESS_CONTACT_READY) ||
        address->dialog_phase != ADDRESS_DIALOG_CLOSED) return false;
    pose_review(address);
    return hit_layout_pane(address->review, "B_card_beta", x, y);
}

bool wm_board_address_contact_hit(WmBoardAddress *address, int x, int y,
                                  WmBoardAddressContactAction *action) {
    if (!address || !action ||
        address->phase != WM_BOARD_ADDRESS_CONTACT_READY ||
        address->dialog_phase != ADDRESS_DIALOG_CLOSED) return false;
    pose_review(address);
    if (hit_layout_pane(address->review, "B_crd_btn_10", x, y))
        *action = WM_BOARD_ADDRESS_CONTACT_CHANGE_NICKNAME;
    else if (hit_layout_pane(address->review, "B_crd_btn_11", x, y))
        *action = WM_BOARD_ADDRESS_CONTACT_ERASE;
    else if (hit_layout_pane(address->review, "B_card_beta", x, y))
        *action = WM_BOARD_ADDRESS_CONTACT_INFO;
    else
        return false;
    return true;
}

bool wm_board_address_entry_hit(WmBoardAddress *address, int x, int y,
                                unsigned *row) {
    if (!address || !row || address->phase != WM_BOARD_ADDRESS_READY ||
        address->page == 0 || wm_board_address_dialog_active(address))
        return false;
    WmLayoutClip clips[2] = {
        {.animation = "th_Adress_a_note_e_rtt",
         .group = "G_note_e_rtt", .frame = 15.0f,
         .loop_override = 0},
        {.animation = "th_Adress_a_note_c_rtt",
         .group = "note_c_rtt", .frame = 15.0f,
         .loop_override = 0}
    };
    wm_layout_pose(address->book, clips, 2);
    wm_layout_set_pane_translation(address->book, "N_note_base",
        -(float)address->page * (608.0f / 832.0f),
        -(float)address->page, 0.0f);
    pose_sheet(address, 'b', 0.0f, 0.0f, 255.0f);
    for (unsigned index = 0; index < 5; index++) {
        WmBoardContact contact;
        size_t slot = (address->page - 1) * 5u + index;
        if (!wm_board_address_contact(address, slot, &contact)) continue;
        char pane[] = "B_name_b_00";
        pane[10] = (char)('0' + index);
        if (hit_layout_pane(address->book, pane, x, y)) {
            *row = index;
            return true;
        }
    }
    return false;
}

bool wm_board_address_dialog_hit(WmBoardAddress *address, int x, int y) {
    if (!address || address->dialog_phase != ADDRESS_DIALOG_READY ||
        address->issue == WM_BOARD_ADDRESS_ISSUE_ERASE_CONFIRM)
        return false;
    pose_dialog(address);
    return hit_layout_pane(address->dialog, "B_BtnB", x, y);
}

bool wm_board_address_dialog_choice_hit(WmBoardAddress *address, int x,
                                         int y, bool *yes) {
    if (!address || !yes || address->dialog_phase != ADDRESS_DIALOG_READY ||
        address->issue != WM_BOARD_ADDRESS_ISSUE_ERASE_CONFIRM) return false;
    pose_dialog(address);
    if (hit_layout_pane(address->erase_dialog, "B_BtnA", x, y))
        *yes = true;
    else if (hit_layout_pane(address->erase_dialog, "B_BtnB", x, y))
        *yes = false;
    else
        return false;
    return true;
}

void wm_board_address_draw_dialog(WmBoardAddress *address) {
    if (!wm_board_address_dialog_active(address)) return;
    pose_dialog(address);
    wm_layout_present_with_fonts(address->platform, address->textures,
                                 address->fonts,
                                 address->issue == WM_BOARD_ADDRESS_ISSUE_ERASE_CONFIRM
                                     ? address->erase_dialog : address->dialog,
                                 true,
                                 WM_LAYOUT_IPL, NULL);
}

bool wm_board_address_kind_hit(WmBoardAddress *address, int x, int y,
                                bool *wii) {
    if (!address || address->phase != WM_BOARD_ADDRESS_KIND_READY || !wii)
        return false;
    pose_kind(address);
    for (unsigned index = 0; index < 2; index++) {
        WmSourceRect rect;
        const char *pane = index == 0 ? "B_btn_00" : "B_btn_01";
        if (!wm_source_pane_rect(address->kind_layout, pane, true,
                                  WM_LAYOUT_IPL, NULL, &rect)) continue;
        if ((float)x >= rect.x && (float)x < rect.x + rect.width &&
            (float)y >= rect.y && (float)y < rect.y + rect.height) {
            *wii = index == 0;
            return true;
        }
    }
    return false;
}

void wm_board_address_draw(WmBoardAddress *address) {
    if (!address || address->phase == WM_BOARD_ADDRESS_CLOSED) return;
    WmBoardAddressStep step = wm_board_address_step(address);
    if (step == WM_BOARD_ADDRESS_STEP_KIND) {
        pose_kind(address);
        wm_layout_present_with_fonts(address->platform, address->textures,
                                     address->fonts, address->kind_layout, true,
                                     WM_LAYOUT_IPL, NULL);
        return;
    }
    if (step == WM_BOARD_ADDRESS_STEP_FORM ||
        step == WM_BOARD_ADDRESS_STEP_NICKNAME ||
        step == WM_BOARD_ADDRESS_STEP_MII) {
        pose_form(address);
        wm_layout_present_with_fonts(address->platform, address->textures,
                                     address->fonts, address->form, true,
                                     WM_LAYOUT_IPL, NULL);
        return;
    }
    if (step == WM_BOARD_ADDRESS_STEP_REVIEW ||
        step == WM_BOARD_ADDRESS_STEP_CONTACT) {
        pose_review(address);
        wm_layout_present_with_fonts(address->platform, address->textures,
                                     address->fonts, address->review, true,
                                     WM_LAYOUT_IPL, NULL);
        return;
    }
    /* The authored translation has moved the entire book beyond the 640-pixel
     * viewport by the end of note_trns_out. Keep the remaining parent/footer
     * transition while avoiding repeated offscreen sheet submissions. */
    if ((address->phase == WM_BOARD_ADDRESS_EXIT ||
         address->phase == WM_BOARD_ADDRESS_BOOK_TO_KIND ||
         address->phase == WM_BOARD_ADDRESS_BOOK_TO_CONTACT) &&
        address->frame >= 16.0f)
        return;
    AddressGeometry view = geometry(address);
    WmLayoutClip clips[5] = {0};
    size_t count = 0;
    if (address->phase == WM_BOARD_ADDRESS_ENTER ||
        address->phase == WM_BOARD_ADDRESS_EXIT ||
        address->phase == WM_BOARD_ADDRESS_BOOK_TO_KIND ||
        address->phase == WM_BOARD_ADDRESS_BOOK_TO_CONTACT ||
        address->phase == WM_BOARD_ADDRESS_BOOK_RETURN) {
        bool leaving = address->phase == WM_BOARD_ADDRESS_EXIT ||
                       address->phase == WM_BOARD_ADDRESS_BOOK_TO_KIND ||
                       address->phase == WM_BOARD_ADDRESS_BOOK_TO_CONTACT;
        float frame = limit_frame(address->frame, 16.0f);
        clips[count++] = (WmLayoutClip){
            .animation = leaving ? "th_Adress_a_note_alp_out" :
                                   "th_Adress_a_note_alp_in",
            .group = "G_note_all", .frame = frame, .loop_override = 0
        };
        clips[count++] = (WmLayoutClip){
            .animation = leaving ? "th_Adress_a_note_trns_out" :
                                   "th_Adress_a_note_trns_in",
            .group = "G_note_all", .frame = frame, .loop_override = 0
        };
    }
    bool cover_turn = address->phase == WM_BOARD_ADDRESS_TURN &&
                      (view.cover_turn || view.wrap_turn);
    bool reverse = address->phase == WM_BOARD_ADDRESS_TURN &&
                   (view.wrap_turn ? address->forward : !address->forward);
    float turn_frame = address->phase == WM_BOARD_ADDRESS_TURN
        ? (reverse ? 15.0f - limit_frame(address->frame, 15.0f)
                   : limit_frame(address->frame, 15.0f))
        : (address->page == 0 ? 0.0f : 15.0f);
    clips[count++] = (WmLayoutClip){
        .animation = "th_Adress_a_note_e_rtt",
        .group = "G_note_e_rtt",
        .frame = cover_turn ? turn_frame :
                 address->page == 0 ? 0.0f : 15.0f,
        .loop_override = 0
    };
    clips[count++] = (WmLayoutClip){
        .animation = "th_Adress_a_note_c_rtt",
        .group = "note_c_rtt",
        .frame = !cover_turn && address->phase == WM_BOARD_ADDRESS_TURN
                     ? turn_frame : 15.0f,
        .loop_override = 0
    };
    if (address->phase == WM_BOARD_ADDRESS_BOOK_ENTRY_PRESS &&
        address->selected_slot < WM_BOARD_CONTACT_CAPACITY) {
        static const char *const row_groups[] = {
            "name_b_00", "name_b_01", "name_b_02", "name_b_03", "name_b_04"
        };
        clips[count++] = (WmLayoutClip){
            .animation = "th_Adress_a_name_psh",
            .group = row_groups[address->selected_slot % 5],
            .frame = limit_frame(address->frame, 20.0f),
            .loop_override = 0
        };
    }
    wm_layout_pose(address->book, clips, count);
    wm_layout_set_pane_visible(address->book, "N_note_move", false);
    wm_layout_set_pane_translation(address->book, "N_note_base",
        -(float)view.base_steps * (608.0f / 832.0f),
        -(float)view.base_steps, 0.0f);
    wm_layout_set_pose_text(address->book, "T_wii_msg",
                            "This console's Wii Number:");
    wm_layout_set_pose_text(address->book, "T_wii_name",
                            "0000 0000 0000 0000");
    wm_layout_set_pose_text(address->book, "T_adrs_00", "Address Book");
    char face_number[8], turn_number[8];
    snprintf(face_number, sizeof(face_number), "%u/20", view.face_page);
    snprintf(turn_number, sizeof(turn_number), "%u/20", view.turn_page);
    wm_layout_set_pose_text(address->book, "T_nmbr_b", face_number);
    wm_layout_set_pose_text(address->book, "T_nmbr_c", turn_number);
    for (unsigned index = 0; index < 5; index++) {
        char pane[] = "T_name_b_00";
        pane[10] = (char)('0' + index);
        WmBoardContact contact;
        size_t face_slot = (view.face_page - 1) * 5u + index;
        wm_layout_set_pose_text(address->book, pane,
            wm_board_address_contact(address, face_slot, &contact)
                ? contact.nickname : "");
        pane[7] = 'c';
        size_t turn_slot = (view.turn_page - 1) * 5u + index;
        wm_layout_set_pose_text(address->book, pane,
            wm_board_address_contact(address, turn_slot, &contact)
                ? contact.nickname : "");
    }
    float group_alpha = 255.0f;
    wm_layout_visit_all_transforms(address->book, true, WM_LAYOUT_IPL,
                                   NULL, capture_group_alpha, &group_alpha);
    for (unsigned count_right = view.right_count; count_right >= 1;
         count_right--) {
        float offset = -(float)count_right;
        show_sheet(address, 'a', offset, offset, group_alpha);
    }
    show_sheet(address, 'b', 0.0f, 0.0f, group_alpha);
    if (view.turning_sheet)
        show_sheet(address, 'c', 0.0f, 0.0f, group_alpha);
    for (unsigned index = 0; index < view.left_count; index++) {
        float offset = (float)(index + view.turning_offset);
        show_sheet(address, 'd', offset, offset, group_alpha);
    }
    for (unsigned index = 0; index < view.cover_count; index++) {
        float offset = (float)(view.cover_offset + index);
        show_sheet(address, 'e', offset, offset, group_alpha);
    }
}
