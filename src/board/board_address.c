#include "board_address_internal.h"
#include "board_text.h"

#include "wii_menu/layout/layout_runtime.h"
#include "wii_menu/render/material_prepare.h"

#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

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
    address->hovered_entry = -1;
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
    address->hovered_entry = -1;
    memset(address->entry_focus, 0, sizeof(address->entry_focus));
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
    address->hovered_entry = -1;
    memset(address->entry_focus, 0, sizeof(address->entry_focus));
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
    address->hovered_entry = -1;
    memset(address->entry_focus, 0, sizeof(address->entry_focus));
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
    address->text_units = wm_board_text_utf16_units(address->text,
                                                    address_bytes);
    address->nickname_units = wm_board_text_utf16_units(address->nickname,
                                                        nickname_bytes);
    address->wii_kind = contact.wii;
    address->selected_slot = slot;
    address->hovered_contact = WM_BOARD_ADDRESS_CONTACT_NONE;
    memset(address->contact_focus, 0, sizeof(address->contact_focus));
    address->phase = WM_BOARD_ADDRESS_BOOK_ENTRY_PRESS;
    address->frame = 0.0f;
    return true;
}

static void change_entry_focus(AddressEntryFocus *focus, bool entering) {
    if (focus->active && focus->entering != entering) {
        focus->frame = 5.0f - limit_frame(focus->frame, 5.0f);
    } else if (!focus->active) {
        focus->frame = 0.0f;
    }
    focus->active = true;
    focus->entering = entering;
}

void wm_board_address_hover_entry(WmBoardAddress *address, int row) {
    if (!address || address->phase != WM_BOARD_ADDRESS_READY ||
        address->page == 0 || wm_board_address_dialog_active(address)) return;
    if (row < 0 || row >= 5) row = -1;
    if (address->hovered_entry == row) return;
    if (address->hovered_entry >= 0)
        change_entry_focus(&address->entry_focus[address->hovered_entry], false);
    if (row >= 0) change_entry_focus(&address->entry_focus[row], true);
    address->hovered_entry = row;
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
        address->nickname_units = wm_board_text_utf16_units(
            address->nickname, address->nickname_bytes);
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
    for (size_t index = 0; index < 5; index++) {
        if (address->entry_focus[index].active)
            address->entry_focus[index].frame += frames;
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
        if (!wm_board_text_character(utf8 + offset, input_bytes - offset,
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
