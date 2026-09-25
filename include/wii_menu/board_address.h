#ifndef WII_MENU_BOARD_ADDRESS_H
#define WII_MENU_BOARD_ADDRESS_H

#include "wii_menu/board_contact_store.h"
#include "wii_menu/font_cache.h"
#include "wii_menu/platform.h"
#include "wii_menu/texture_cache.h"

#include <stdbool.h>

typedef struct WmBoardAddress WmBoardAddress;

typedef enum WmBoardAddressPhase {
    WM_BOARD_ADDRESS_CLOSED,
    WM_BOARD_ADDRESS_ENTER,
    WM_BOARD_ADDRESS_READY,
    WM_BOARD_ADDRESS_TURN,
    WM_BOARD_ADDRESS_EXIT,
    WM_BOARD_ADDRESS_REGISTER_PRESS,
    WM_BOARD_ADDRESS_BOOK_TO_KIND,
    WM_BOARD_ADDRESS_KIND_ENTER,
    WM_BOARD_ADDRESS_KIND_READY,
    WM_BOARD_ADDRESS_KIND_PRESS,
    WM_BOARD_ADDRESS_KIND_TO_FORM,
    WM_BOARD_ADDRESS_KIND_TO_BOOK,
    WM_BOARD_ADDRESS_FORM_ENTER,
    WM_BOARD_ADDRESS_FORM_READY,
    WM_BOARD_ADDRESS_FORM_OK_PRESS,
    WM_BOARD_ADDRESS_FORM_TO_NICKNAME,
    WM_BOARD_ADDRESS_NICKNAME_ENTER,
    WM_BOARD_ADDRESS_NICKNAME_READY,
    WM_BOARD_ADDRESS_NICKNAME_OK_PRESS,
    WM_BOARD_ADDRESS_NICKNAME_TO_MII,
    WM_BOARD_ADDRESS_MII_ENTER,
    WM_BOARD_ADDRESS_MII_READY,
    WM_BOARD_ADDRESS_MII_OK_PRESS,
    WM_BOARD_ADDRESS_MII_TO_REVIEW,
    WM_BOARD_ADDRESS_REVIEW_ENTER,
    WM_BOARD_ADDRESS_REVIEW_READY,
    WM_BOARD_ADDRESS_REVIEW_INFO_PRESS,
    WM_BOARD_ADDRESS_REVIEW_TO_MII,
    WM_BOARD_ADDRESS_REVIEW_SAVE_EXIT,
    WM_BOARD_ADDRESS_REGISTERED_NOTICE,
    WM_BOARD_ADDRESS_BOOK_ENTRY_PRESS,
    WM_BOARD_ADDRESS_BOOK_TO_CONTACT,
    WM_BOARD_ADDRESS_CONTACT_ENTER,
    WM_BOARD_ADDRESS_CONTACT_READY,
    WM_BOARD_ADDRESS_CONTACT_INFO_PRESS,
    WM_BOARD_ADDRESS_CONTACT_NAME_PRESS,
    WM_BOARD_ADDRESS_CONTACT_NAME_TO_FORM,
    WM_BOARD_ADDRESS_CONTACT_NAME_FORM_ENTER,
    WM_BOARD_ADDRESS_CONTACT_NAME_FORM_READY,
    WM_BOARD_ADDRESS_CONTACT_NAME_FORM_RETURN,
    WM_BOARD_ADDRESS_CONTACT_NAME_CARD_ENTER,
    WM_BOARD_ADDRESS_CONTACT_ERASE_PRESS,
    WM_BOARD_ADDRESS_CONTACT_ERASE_BUTTONS_OUT,
    WM_BOARD_ADDRESS_CONTACT_ERASE_QUESTION,
    WM_BOARD_ADDRESS_CONTACT_ERASE_MESSAGE_OUT,
    WM_BOARD_ADDRESS_CONTACT_ERASE_BUTTONS_IN,
    WM_BOARD_ADDRESS_CONTACT_ERASED_NOTICE,
    WM_BOARD_ADDRESS_CONTACT_TO_BOOK,
    WM_BOARD_ADDRESS_MII_RESTORE,
    WM_BOARD_ADDRESS_MII_TO_NICKNAME,
    WM_BOARD_ADDRESS_NICKNAME_RESTORE,
    WM_BOARD_ADDRESS_NICKNAME_TO_FORM,
    WM_BOARD_ADDRESS_FORM_RESTORE,
    WM_BOARD_ADDRESS_FORM_TO_BOOK,
    WM_BOARD_ADDRESS_BOOK_RETURN
} WmBoardAddressPhase;

typedef enum WmBoardAddressStep {
    WM_BOARD_ADDRESS_STEP_BOOK,
    WM_BOARD_ADDRESS_STEP_KIND,
    WM_BOARD_ADDRESS_STEP_FORM,
    WM_BOARD_ADDRESS_STEP_NICKNAME,
    WM_BOARD_ADDRESS_STEP_MII,
    WM_BOARD_ADDRESS_STEP_REVIEW,
    WM_BOARD_ADDRESS_STEP_CONTACT
} WmBoardAddressStep;

typedef enum WmBoardAddressIssue {
    WM_BOARD_ADDRESS_ISSUE_NONE,
    WM_BOARD_ADDRESS_ISSUE_INVALID_WII,
    WM_BOARD_ADDRESS_ISSUE_INVALID_EMAIL,
    WM_BOARD_ADDRESS_ISSUE_NO_MII,
    WM_BOARD_ADDRESS_ISSUE_ADDRESS_INFO,
    WM_BOARD_ADDRESS_ISSUE_DUPLICATE_WII,
    WM_BOARD_ADDRESS_ISSUE_DUPLICATE_EMAIL,
    WM_BOARD_ADDRESS_ISSUE_BOOK_FULL,
    WM_BOARD_ADDRESS_ISSUE_REGISTERED,
    WM_BOARD_ADDRESS_ISSUE_ERASE_CONFIRM,
    WM_BOARD_ADDRESS_ISSUE_ERASED,
    WM_BOARD_ADDRESS_ISSUE_SAVE_ERROR
} WmBoardAddressIssue;

typedef enum WmBoardAddressContactAction {
    WM_BOARD_ADDRESS_CONTACT_NONE,
    WM_BOARD_ADDRESS_CONTACT_INFO,
    WM_BOARD_ADDRESS_CONTACT_CHANGE_NICKNAME,
    WM_BOARD_ADDRESS_CONTACT_ERASE
} WmBoardAddressContactAction;

/* The offline Address Book begins at its closed cover, page zero. Numbered
 * pages one through twenty each hold five local contact slots. */
WmBoardAddress *wm_board_address_create(WmPlatform *platform,
                                         const char *assets_directory,
                                         WmTextureCache *textures,
                                         WmFontCache *fonts);
void wm_board_address_destroy(WmBoardAddress *address);
WmBoardContactStoreStatus wm_board_address_load_contacts(
    WmBoardAddress *address, const char *path,
    char *error, size_t error_capacity);
size_t wm_board_address_contact_count(const WmBoardAddress *address);
bool wm_board_address_contact(const WmBoardAddress *address, size_t slot,
                              WmBoardContact *contact);
const char *wm_board_address_last_save_error(const WmBoardAddress *address);
void wm_board_address_reset(WmBoardAddress *address);
bool wm_board_address_open(WmBoardAddress *address);
bool wm_board_address_turn(WmBoardAddress *address, bool forward);
bool wm_board_address_register(WmBoardAddress *address);
bool wm_board_address_select_kind(WmBoardAddress *address, bool wii);
bool wm_board_address_select_entry(WmBoardAddress *address, unsigned row);
void wm_board_address_hover_kind(WmBoardAddress *address, int choice);
bool wm_board_address_back(WmBoardAddress *address);
void wm_board_address_advance(WmBoardAddress *address, float frames);
void wm_board_address_draw(WmBoardAddress *address);
WmBoardAddressPhase wm_board_address_phase(const WmBoardAddress *address);
unsigned wm_board_address_page(const WmBoardAddress *address);
float wm_board_address_phase_frame(const WmBoardAddress *address);
WmBoardAddressStep wm_board_address_step(const WmBoardAddress *address);
bool wm_board_address_kind_is_wii(const WmBoardAddress *address);
const char *wm_board_address_text(const WmBoardAddress *address);
const char *wm_board_address_field_text(const WmBoardAddress *address);
bool wm_board_address_insert_text(WmBoardAddress *address, const char *utf8);
bool wm_board_address_backspace(WmBoardAddress *address);
WmBoardAddressIssue wm_board_address_validate(const WmBoardAddress *address);
bool wm_board_address_field_valid(const WmBoardAddress *address);
bool wm_board_address_submit(WmBoardAddress *address);
bool wm_board_address_show_issue(WmBoardAddress *address);
bool wm_board_address_show_no_mii(WmBoardAddress *address);
/* Show the same WAD notice for Memo's Add a Mii action while the Address Book
 * scene itself is closed. The Compose owner advances and draws this dialog. */
bool wm_board_address_show_memo_no_mii(WmBoardAddress *address);
bool wm_board_address_show_address_info(WmBoardAddress *address);
bool wm_board_address_change_nickname(WmBoardAddress *address);
bool wm_board_address_erase(WmBoardAddress *address);
void wm_board_address_hover_contact(WmBoardAddress *address,
                                    WmBoardAddressContactAction action);
bool wm_board_address_contact_hit(WmBoardAddress *address, int x, int y,
                                  WmBoardAddressContactAction *action);
bool wm_board_address_dialog_active(const WmBoardAddress *address);
bool wm_board_address_dialog_accept(WmBoardAddress *address);
bool wm_board_address_dialog_choose(WmBoardAddress *address, bool yes);
void wm_board_address_dialog_hover(WmBoardAddress *address, bool hovering);
void wm_board_address_dialog_hover_choice(WmBoardAddress *address,
                                          bool yes, bool hovering);
bool wm_board_address_form_hit(WmBoardAddress *address, int x, int y);
bool wm_board_address_review_hit(WmBoardAddress *address, int x, int y);
bool wm_board_address_dialog_hit(WmBoardAddress *address, int x, int y);
bool wm_board_address_dialog_choice_hit(WmBoardAddress *address, int x,
                                         int y, bool *yes);
void wm_board_address_draw_dialog(WmBoardAddress *address);
/* Hit rectangles are provided by the extracted th_Adress_d source layout. */
bool wm_board_address_kind_hit(WmBoardAddress *address, int x, int y,
                                bool *wii);
bool wm_board_address_entry_hit(WmBoardAddress *address, int x, int y,
                                unsigned *row);

#endif
