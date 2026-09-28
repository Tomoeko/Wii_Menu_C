#include "board_address_internal.h"
#include "board_text.h"

#include "wii_menu/input/source_hit.h"
#include "wii_menu/layout/layout_present.h"

#include <math.h>
#include <stdio.h>
#include <string.h>

static const char *const entry_groups[5] = {"name_b_00", "name_b_01", "name_b_02",
                                            "name_b_03", "name_b_04"};

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

/* Address Book sheet counts and offsets for the page geometry. */
static AddressGeometry geometry(const WmBoardAddress *address) {
    unsigned page = address->page;
    AddressGeometry result = {.right_count = ADDRESS_PAGE_COUNT - page,
                              .left_count = page > 0 ? page - 1 : 0,
                              .base_steps = page,
                              .cover_count = 1,
                              .face_page = page > 0 ? page : 1,
                              .turn_page = page > 0 ? page : 1};
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
            result.left_count = result.left_count > 0 ? result.left_count - 1 : 0;
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

/* Keyboard type 12 keeps ASCII digits; type 7 validates entered text after
 * OK. Both fields count UTF-16 units while retaining UTF-8 for fonts. */
static void format_address(const WmBoardAddress *address, bool shorten, char *output,
                           size_t capacity) {
    if (!address || !output || capacity == 0)
        return;
    output[0] = '\0';
    if (address->wii_kind) {
        size_t written = 0;
        for (size_t index = 0; index < address->text_bytes; index++) {
            if (index && index % 4 == 0 && written + 1 < capacity)
                output[written++] = ' ';
            if (written + 1 >= capacity)
                break;
            output[written++] = address->text[index];
        }
        output[written] = '\0';
        return;
    }
    if (!shorten || address->text_units <= 16) {
        snprintf(output, capacity, "%s", address->text);
        return;
    }
    if (capacity < 4)
        return;
    size_t written = 0;
    size_t units = 0;
    for (size_t offset = 0; offset < address->text_bytes && units < 14;) {
        size_t bytes, character_units;
        if (!wm_board_text_character(address->text + offset,
                                     address->text_bytes - offset, &bytes,
                                     &character_units) ||
            units + character_units > 14 || written + bytes + 4 > capacity)
            break;
        memcpy(output + written, address->text + offset, bytes);
        written += bytes;
        offset += bytes;
        units += character_units;
    }
    memcpy(output + written, "...", 4);
}

static void pose_sheet(WmBoardAddress *address, char sheet, float x, float y,
                       float group_alpha) {
    static const char *const panes[] = {"N_note_a", "N_note_b", "N_note_c", "N_note_d",
                                        "N_note_e"};
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
                             sheet == 'b' || sheet == 'c' ? group_alpha : 255.0f);
}

static void show_sheet(WmBoardAddress *address, char sheet, float x, float y,
                       float group_alpha) {
    pose_sheet(address, sheet, x, y, group_alpha);
    wm_layout_present_with_fonts(address->platform, address->textures, address->fonts,
                                 address->book, true, WM_LAYOUT_IPL, NULL);
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
                            ? limit_frame(address->frame, 18.0f)
                            : 18.0f;
    clips[count++] = (WmLayoutClip){.animation = "th_Adress_d_btn_strt",
                                    .group = "G_btn_strt_fnsh",
                                    .frame = start_frame,
                                    .loop_override = 0};
    for (unsigned index = 0; index < 2; index++) {
        if (!address->kind_focus[index])
            continue;
        clips[count++] = (WmLayoutClip){
            .animation = address->kind_focus_entering[index] ? "th_Adress_d_btn_in"
                                                             : "th_Adress_d_btn_out",
            .group = index == 0 ? "G_btn_00" : "G_btn_01",
            .frame = limit_frame(address->kind_focus_frame[index], 6.0f),
            .loop_override = 0};
    }
    if (address->phase == WM_BOARD_ADDRESS_KIND_PRESS ||
        address->phase == WM_BOARD_ADDRESS_KIND_TO_FORM) {
        clips[count++] =
            (WmLayoutClip){.animation = "th_Adress_d_btn_psh",
                           .group = address->wii_kind ? "G_btn_00" : "G_btn_01",
                           .frame = address->phase == WM_BOARD_ADDRESS_KIND_PRESS
                                        ? limit_frame(address->frame, 20.0f)
                                        : 20.0f,
                           .loop_override = 0};
    }
    if (address->phase == WM_BOARD_ADDRESS_KIND_TO_FORM ||
        address->phase == WM_BOARD_ADDRESS_KIND_TO_BOOK) {
        clips[count++] = (WmLayoutClip){.animation = "th_Adress_d_btn_fnsh",
                                        .group = "G_btn_strt_fnsh",
                                        .frame = limit_frame(address->frame, 18.0f),
                                        .loop_override = 0};
    }
    wm_layout_pose(address->kind_layout, clips, count);
    wm_layout_set_pose_text(address->kind_layout, "T_btn_name_00", "Wii");
    wm_layout_set_pose_text(address->kind_layout, "T_btn_name_01", "Others");
}

static void pose_form(WmBoardAddress *address) {
    bool contact_name = address->phase == WM_BOARD_ADDRESS_CONTACT_NAME_FORM_ENTER ||
                        address->phase == WM_BOARD_ADDRESS_CONTACT_NAME_FORM_READY ||
                        address->phase == WM_BOARD_ADDRESS_CONTACT_NAME_FORM_RETURN;
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
         .frame = card_entering ? limit_frame(address->frame, 18.0f) : 18.0f,
         .loop_override = 0},
        {.animation = "th_Adress_c_question_alp_in",
         .group = "G_question_00",
         .frame = limit_frame(frame, 20.0f),
         .loop_override = 0},
        {.animation = mii ? "th_Adress_c_mii_alp_in" : "th_Adress_c_name_alp_in",
         .group = mii ? "G_mii" : "G_name_00",
         .frame = limit_frame(frame, 20.0f),
         .loop_override = 0}};
    size_t count = 3;
    if (address->phase == WM_BOARD_ADDRESS_FORM_TO_BOOK ||
        address->phase == WM_BOARD_ADDRESS_FORM_TO_NICKNAME ||
        address->phase == WM_BOARD_ADDRESS_MII_TO_REVIEW ||
        address->phase == WM_BOARD_ADDRESS_CONTACT_NAME_FORM_RETURN) {
        clips[count++] = (WmLayoutClip){.animation = "th_Adress_c_card_fnsh",
                                        .group = "G_card_strt_fnsh",
                                        .frame = limit_frame(address->frame, 18.0f),
                                        .loop_override = 0};
    }
    if (address->phase == WM_BOARD_ADDRESS_NICKNAME_TO_FORM ||
        address->phase == WM_BOARD_ADDRESS_NICKNAME_TO_MII ||
        address->phase == WM_BOARD_ADDRESS_MII_TO_NICKNAME) {
        bool mii_out = address->phase == WM_BOARD_ADDRESS_MII_TO_NICKNAME;
        clips[count++] = (WmLayoutClip){.animation = "th_Adress_c_question_alp_out",
                                        .group = "G_question_00",
                                        .frame = limit_frame(address->frame, 20.0f),
                                        .loop_override = 0};
        clips[count++] =
            (WmLayoutClip){.animation = mii_out ? "th_Adress_c_mii_alp_out"
                                                : "th_Adress_c_name_alp_out",
                           .group = mii_out ? "G_mii" : "G_name_00",
                           .frame = limit_frame(address->frame, 20.0f),
                           .loop_override = 0};
    }
    wm_layout_pose(address->form, clips, count);
    bool nickname = wm_board_address_step(address) == WM_BOARD_ADDRESS_STEP_NICKNAME;
    wm_layout_set_pose_text(address->form, "T_question_00",
                            mii                 ? "You can attach a Mii."
                            : contact_name      ? "Nickname"
                            : nickname          ? "Apply a nickname."
                            : address->wii_kind ? "Enter a Wii Number."
                                                : "Enter an e-mail address.");
    wm_layout_set_pose_text(address->form, "T_name_00",
                            mii        ? ""
                            : nickname ? address->nickname
                                       : address->text);
    wm_layout_set_pose_text(address->form, "T_msg_00",
                            contact_name ? "Apply nickname" : "");
    wm_layout_set_pose_text(address->form, "T_mii_msg_00", mii ? "←Add a Mii" : "");
    wm_layout_set_pane_visible(address->form, "N_mii_all", mii);
}

static void pose_review(WmBoardAddress *address) {
    bool contact = wm_board_address_step(address) == WM_BOARD_ADDRESS_STEP_CONTACT;
    WmLayoutClip clips[18];
    size_t count = 0;
    static const char *const buttons[] = {"crd_btn_00", "crd_btn_10", "crd_btn_11",
                                          "crd_btn_gry"};
    for (size_t index = 0; index < 4; index++) {
        clips[count++] = (WmLayoutClip){.animation = "th_Adress_b_btn_scl_in",
                                        .group = buttons[index],
                                        .frame = contact ? 10.0f : 0.0f,
                                        .loop_override = 0};
    }
    for (size_t index = 0; index < 2; index++) {
        if (!contact || !address->contact_focus[index])
            continue;
        clips[count++] = (WmLayoutClip){
            .animation = address->contact_focus_entering[index] ? "th_Adress_b_btn_in"
                                                                : "th_Adress_b_btn_out",
            .group = index == 0 ? "crd_btn_10" : "crd_btn_11",
            .frame = limit_frame(address->contact_focus_frame[index], 6.0f),
            .loop_override = 0};
    }
    clips[count++] = (WmLayoutClip){
        .animation = "th_Adress_b_card_strt",
        .group = "card_strt_fnsh",
        .frame = address->phase == WM_BOARD_ADDRESS_REVIEW_ENTER ||
                         address->phase == WM_BOARD_ADDRESS_CONTACT_ENTER ||
                         address->phase == WM_BOARD_ADDRESS_CONTACT_NAME_CARD_ENTER
                     ? limit_frame(address->frame, 18.0f)
                     : 18.0f,
        .loop_override = 0};
    clips[count++] =
        (WmLayoutClip){.animation = "th_Adress_b_card_msg_alp_in",
                       .group = "card_msg",
                       .frame = address->phase == WM_BOARD_ADDRESS_REVIEW_ENTER
                                    ? limit_frame(address->frame, 20.0f)
                                    : 20.0f,
                       .loop_override = 0};
    if (address->phase == WM_BOARD_ADDRESS_CONTACT_NAME_PRESS ||
        address->phase == WM_BOARD_ADDRESS_CONTACT_ERASE_PRESS) {
        clips[count++] = (WmLayoutClip){
            .animation = "th_Adress_b_btn_psh",
            .group = address->phase == WM_BOARD_ADDRESS_CONTACT_NAME_PRESS
                         ? "crd_btn_10"
                         : "crd_btn_11",
            .frame = limit_frame(address->frame, 20.0f),
            .loop_override = 0};
    }
    bool erasing = address->phase == WM_BOARD_ADDRESS_CONTACT_ERASE_BUTTONS_OUT ||
                   address->phase == WM_BOARD_ADDRESS_CONTACT_ERASE_QUESTION ||
                   address->phase == WM_BOARD_ADDRESS_CONTACT_ERASE_MESSAGE_OUT ||
                   address->phase == WM_BOARD_ADDRESS_CONTACT_ERASED_NOTICE;
    if (erasing) {
        for (size_t index = 0; index < 3; index++) {
            clips[count++] = (WmLayoutClip){
                .animation = "th_Adress_b_btn_scl_out",
                .group = buttons[index],
                .frame = address->phase == WM_BOARD_ADDRESS_CONTACT_ERASE_BUTTONS_OUT
                             ? limit_frame(address->frame, 10.0f)
                             : 10.0f,
                .loop_override = 0};
        }
    }
    if (address->phase == WM_BOARD_ADDRESS_CONTACT_ERASE_BUTTONS_IN) {
        for (size_t index = 0; index < 3; index++) {
            clips[count++] = (WmLayoutClip){.animation = "th_Adress_b_btn_scl_in",
                                            .group = buttons[index],
                                            .frame = limit_frame(address->frame, 10.0f),
                                            .loop_override = 0};
        }
    }
    if (address->phase == WM_BOARD_ADDRESS_CONTACT_ERASE_QUESTION ||
        address->phase == WM_BOARD_ADDRESS_CONTACT_ERASE_MESSAGE_OUT ||
        address->phase == WM_BOARD_ADDRESS_CONTACT_ERASED_NOTICE) {
        clips[count++] = (WmLayoutClip){
            .animation = "th_Adress_b_card_msg_alp_in",
            .group = "card_msg",
            .frame = address->phase == WM_BOARD_ADDRESS_CONTACT_ERASE_QUESTION
                         ? address->erase_message_frame
                         : 20.0f,
            .loop_override = 0};
    }
    if (address->phase == WM_BOARD_ADDRESS_CONTACT_ERASE_MESSAGE_OUT ||
        address->phase == WM_BOARD_ADDRESS_CONTACT_ERASE_BUTTONS_IN ||
        address->phase == WM_BOARD_ADDRESS_CONTACT_ERASED_NOTICE) {
        clips[count++] = (WmLayoutClip){
            .animation = "th_Adress_b_card_msg_alp_out",
            .group = "card_msg",
            .frame = address->phase == WM_BOARD_ADDRESS_CONTACT_ERASE_MESSAGE_OUT
                         ? limit_frame(address->frame, 20.0f)
                         : 20.0f,
            .loop_override = 0};
    }
    if (address->phase == WM_BOARD_ADDRESS_REVIEW_TO_MII ||
        address->phase == WM_BOARD_ADDRESS_REVIEW_SAVE_EXIT ||
        address->phase == WM_BOARD_ADDRESS_REGISTERED_NOTICE ||
        address->phase == WM_BOARD_ADDRESS_CONTACT_TO_BOOK ||
        address->phase == WM_BOARD_ADDRESS_CONTACT_NAME_TO_FORM) {
        clips[count++] =
            (WmLayoutClip){.animation = "th_Adress_b_card_fnsh",
                           .group = "card_strt_fnsh",
                           .frame = address->phase == WM_BOARD_ADDRESS_REGISTERED_NOTICE
                                        ? 18.0f
                                        : limit_frame(address->frame, 18.0f),
                           .loop_override = 0};
    }
    wm_layout_pose(address->review, clips, count);
    char shown_address[ADDRESS_TEXT_CAPACITY + 4];
    format_address(address, true, shown_address, sizeof(shown_address));
    wm_layout_set_pose_text(address->review, "T_name_00", address->nickname);
    wm_layout_set_pose_text(address->review, "T_frnd_crd_00", shown_address);
    wm_layout_set_pose_text(
        address->review, "T_card_msg_00",
        erasing || address->phase == WM_BOARD_ADDRESS_CONTACT_ERASE_BUTTONS_IN
            ? "Erase this?"
        : contact ? ""
                  : "This information has been\nadded to your address book.");
    wm_layout_set_pose_text(address->review, "T_crd_btn_00", "Send Message");
    wm_layout_set_pose_text(address->review, "T_crd_btn_10", "Change\nNickname");
    wm_layout_set_pose_text(address->review, "T_crd_btn_11", "Erase");
    wm_layout_set_pose_text(address->review, "T_crd_btn_gry", "Send Message");
}

static void pose_dialog(WmBoardAddress *address) {
    bool erase = address->issue == WM_BOARD_ADDRESS_ISSUE_ERASE_CONFIRM;
    WmLayout *layout = erase ? address->erase_dialog : address->dialog;
    const char *prefix = erase ? "my_DialogWindow_b_" : "my_DialogWindow_a1_";
    WmLayoutClip clips[5];
    char animation[64];
    size_t count = 0;
    snprintf(animation, sizeof(animation), "%sDialogIn", prefix);
    clips[count++] = (WmLayoutClip){
        .animation = animation,
        .group = "G_InOut",
        .frame = address->dialog_phase == ADDRESS_DIALOG_ENTER
                     ? limit_frame(address->dialog_frame, erase ? 25.0f : 24.0f)
                 : erase ? 25.0f
                         : 24.0f,
        .loop_override = 0};
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
            .frame = limit_frame(address->dialog_focus_frame, erase ? 10.0f : 6.0f),
            .loop_override = 0};
    }
    if (erase && address->erase_yes_focus) {
        clips[count++] =
            (WmLayoutClip){.animation = focus_a,
                           .group = "G_FocusBtnA",
                           .frame = limit_frame(address->erase_yes_focus_frame, 10.0f),
                           .loop_override = 0};
    }
    if (address->dialog_phase == ADDRESS_DIALOG_PRESS ||
        address->dialog_phase == ADDRESS_DIALOG_EXIT) {
        clips[count++] = (WmLayoutClip){
            .animation = select,
            .group =
                erase && address->erase_yes_selected ? "G_SelectBtnA" : "G_SelectBtnB",
            .frame = address->dialog_phase == ADDRESS_DIALOG_PRESS
                         ? limit_frame(address->dialog_frame, erase ? 20.0f : 16.0f)
                     : erase ? 20.0f
                             : 16.0f,
            .loop_override = 0};
    }
    if (address->dialog_phase == ADDRESS_DIALOG_EXIT) {
        clips[count++] = (WmLayoutClip){
            .animation = dialog_out,
            .group = "G_InOut",
            .frame = limit_frame(address->dialog_frame, erase ? 25.0f : 20.0f),
            .loop_override = 0};
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
            format_address(address, false, shown_address, sizeof(shown_address));
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
    if (erase)
        wm_layout_set_pose_text(layout, "T_BtnA", "Yes");
}

/* Public hit queries pose the current authored clips before inspecting the
 * same pane geometry that drawing will use later in the frame. */
static bool hit_layout_pane(WmLayout *layout, const char *name, int x, int y) {
    WmSourceRect rect;
    return wm_source_pane_rect(layout, name, true, WM_LAYOUT_IPL, NULL, &rect) &&
           (float)x >= rect.x && (float)x < rect.x + rect.width && (float)y >= rect.y &&
           (float)y < rect.y + rect.height;
}

bool wm_board_address_form_hit(WmBoardAddress *address, int x, int y) {
    if (!address ||
        (address->phase != WM_BOARD_ADDRESS_FORM_READY &&
         address->phase != WM_BOARD_ADDRESS_NICKNAME_READY &&
         address->phase != WM_BOARD_ADDRESS_CONTACT_NAME_FORM_READY &&
         address->phase != WM_BOARD_ADDRESS_MII_READY) ||
        address->dialog_phase != ADDRESS_DIALOG_CLOSED)
        return false;
    pose_form(address);
    return hit_layout_pane(address->form, "B_crd_edgi_00", x, y);
}

bool wm_board_address_review_hit(WmBoardAddress *address, int x, int y) {
    if (!address ||
        (address->phase != WM_BOARD_ADDRESS_REVIEW_READY &&
         address->phase != WM_BOARD_ADDRESS_CONTACT_READY) ||
        address->dialog_phase != ADDRESS_DIALOG_CLOSED)
        return false;
    pose_review(address);
    return hit_layout_pane(address->review, "B_card_beta", x, y);
}

bool wm_board_address_contact_hit(WmBoardAddress *address, int x, int y,
                                  WmBoardAddressContactAction *action) {
    if (!address || !action || address->phase != WM_BOARD_ADDRESS_CONTACT_READY ||
        address->dialog_phase != ADDRESS_DIALOG_CLOSED)
        return false;
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

bool wm_board_address_entry_hit(WmBoardAddress *address, int x, int y, unsigned *row) {
    if (!address || !row || address->phase != WM_BOARD_ADDRESS_READY ||
        address->page == 0 || wm_board_address_dialog_active(address))
        return false;
    WmLayoutClip clips[2] = {{.animation = "th_Adress_a_note_e_rtt",
                              .group = "G_note_e_rtt",
                              .frame = 15.0f,
                              .loop_override = 0},
                             {.animation = "th_Adress_a_note_c_rtt",
                              .group = "note_c_rtt",
                              .frame = 15.0f,
                              .loop_override = 0}};
    wm_layout_pose(address->book, clips, 2);
    wm_layout_set_pane_translation(address->book, "N_note_base",
                                   -(float)address->page * (608.0f / 832.0f),
                                   -(float)address->page, 0.0f);
    pose_sheet(address, 'b', 0.0f, 0.0f, 255.0f);
    for (unsigned index = 0; index < 5; index++) {
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

bool wm_board_address_dialog_choice_hit(WmBoardAddress *address, int x, int y,
                                        bool *yes) {
    if (!address || !yes || address->dialog_phase != ADDRESS_DIALOG_READY ||
        address->issue != WM_BOARD_ADDRESS_ISSUE_ERASE_CONFIRM)
        return false;
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
    if (!wm_board_address_dialog_active(address))
        return;
    pose_dialog(address);
    wm_layout_present_with_fonts(address->platform, address->textures, address->fonts,
                                 address->issue == WM_BOARD_ADDRESS_ISSUE_ERASE_CONFIRM
                                     ? address->erase_dialog
                                     : address->dialog,
                                 true, WM_LAYOUT_IPL, NULL);
}

bool wm_board_address_kind_hit(WmBoardAddress *address, int x, int y, bool *wii) {
    if (!address || address->phase != WM_BOARD_ADDRESS_KIND_READY || !wii)
        return false;
    pose_kind(address);
    for (unsigned index = 0; index < 2; index++) {
        WmSourceRect rect;
        const char *pane = index == 0 ? "B_btn_00" : "B_btn_01";
        if (!wm_source_pane_rect(address->kind_layout, pane, true, WM_LAYOUT_IPL, NULL,
                                 &rect))
            continue;
        if ((float)x >= rect.x && (float)x < rect.x + rect.width &&
            (float)y >= rect.y && (float)y < rect.y + rect.height) {
            *wii = index == 0;
            return true;
        }
    }
    return false;
}

void wm_board_address_draw(WmBoardAddress *address) {
    if (!address || address->phase == WM_BOARD_ADDRESS_CLOSED)
        return;
    WmBoardAddressStep step = wm_board_address_step(address);
    if (step == WM_BOARD_ADDRESS_STEP_KIND) {
        pose_kind(address);
        wm_layout_present_with_fonts(address->platform, address->textures,
                                     address->fonts, address->kind_layout, true,
                                     WM_LAYOUT_IPL, NULL);
        return;
    }
    if (step == WM_BOARD_ADDRESS_STEP_FORM || step == WM_BOARD_ADDRESS_STEP_NICKNAME ||
        step == WM_BOARD_ADDRESS_STEP_MII) {
        pose_form(address);
        wm_layout_present_with_fonts(address->platform, address->textures,
                                     address->fonts, address->form, true, WM_LAYOUT_IPL,
                                     NULL);
        return;
    }
    if (step == WM_BOARD_ADDRESS_STEP_REVIEW || step == WM_BOARD_ADDRESS_STEP_CONTACT) {
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
    WmLayoutClip clips[10] = {0};
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
        clips[count++] =
            (WmLayoutClip){.animation = leaving ? "th_Adress_a_note_alp_out"
                                                : "th_Adress_a_note_alp_in",
                           .group = "G_note_all",
                           .frame = frame,
                           .loop_override = 0};
        clips[count++] =
            (WmLayoutClip){.animation = leaving ? "th_Adress_a_note_trns_out"
                                                : "th_Adress_a_note_trns_in",
                           .group = "G_note_all",
                           .frame = frame,
                           .loop_override = 0};
    }
    bool cover_turn =
        address->phase == WM_BOARD_ADDRESS_TURN && (view.cover_turn || view.wrap_turn);
    bool reverse = address->phase == WM_BOARD_ADDRESS_TURN &&
                   (view.wrap_turn ? address->forward : !address->forward);
    float turn_frame = address->phase == WM_BOARD_ADDRESS_TURN
                           ? (reverse ? 15.0f - limit_frame(address->frame, 15.0f)
                                      : limit_frame(address->frame, 15.0f))
                           : (address->page == 0 ? 0.0f : 15.0f);
    clips[count++] = (WmLayoutClip){.animation = "th_Adress_a_note_e_rtt",
                                    .group = "G_note_e_rtt",
                                    .frame = cover_turn           ? turn_frame
                                             : address->page == 0 ? 0.0f
                                                                  : 15.0f,
                                    .loop_override = 0};
    clips[count++] = (WmLayoutClip){
        .animation = "th_Adress_a_note_c_rtt",
        .group = "note_c_rtt",
        .frame =
            !cover_turn && address->phase == WM_BOARD_ADDRESS_TURN ? turn_frame : 15.0f,
        .loop_override = 0};
    /* Imported cursor panes start yellow and fully opaque. The row focus
     * clips establish the source blue material and idle alpha-zero state. */
    for (size_t index = 0; index < 5; index++) {
        AddressEntryFocus focus = address->entry_focus[index];
        clips[count++] = (WmLayoutClip){
            .animation = focus.active && focus.entering ? "th_Adress_a_name_in"
                                                        : "th_Adress_a_name_out",
            .group = entry_groups[index],
            .frame = focus.active ? limit_frame(focus.frame, 5.0f) : 5.0f,
            .loop_override = 0};
    }
    if (address->phase == WM_BOARD_ADDRESS_BOOK_ENTRY_PRESS &&
        address->selected_slot < WM_BOARD_CONTACT_CAPACITY) {
        clips[count++] =
            (WmLayoutClip){.animation = "th_Adress_a_name_psh",
                           .group = entry_groups[address->selected_slot % 5],
                           .frame = limit_frame(address->frame, 20.0f),
                           .loop_override = 0};
    }
    wm_layout_pose(address->book, clips, count);
    wm_layout_set_pane_visible(address->book, "N_note_move", false);
    wm_layout_set_pane_translation(address->book, "N_note_base",
                                   -(float)view.base_steps * (608.0f / 832.0f),
                                   -(float)view.base_steps, 0.0f);
    wm_layout_set_pose_text(address->book, "T_wii_msg", "This console's Wii Number:");
    wm_layout_set_pose_text(address->book, "T_wii_name", "0000 0000 0000 0000");
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
                                    ? contact.nickname
                                    : "");
        pane[7] = 'c';
        size_t turn_slot = (view.turn_page - 1) * 5u + index;
        wm_layout_set_pose_text(address->book, pane,
                                wm_board_address_contact(address, turn_slot, &contact)
                                    ? contact.nickname
                                    : "");
    }
    float group_alpha = 255.0f;
    wm_layout_visit_all_transforms(address->book, true, WM_LAYOUT_IPL, NULL,
                                   capture_group_alpha, &group_alpha);
    for (unsigned count_right = view.right_count; count_right >= 1; count_right--) {
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
