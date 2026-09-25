#define _XOPEN_SOURCE 700

#include "wii_menu/board_address.h"
#include "wii_menu/board_compose.h"
#include "wii_menu/font_cache.h"
#include "wii_menu/layout_runtime.h"
#include "wii_menu/source_hit.h"
#include "wii_menu/texture_cache.h"

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

static unsigned next_texture = 1;
static unsigned material_draws;

void wm_platform_begin(WmPlatform *platform, WmColor clear_color) {
    (void)platform;
    (void)clear_color;
}

void wm_platform_end(WmPlatform *platform) {
    (void)platform;
}

void wm_platform_set_clip(WmPlatform *platform, const WmClipRect *clip) {
    (void)platform;
    (void)clip;
}

void wm_platform_draw_quad(WmPlatform *platform, const WmQuad *quad) {
    (void)platform;
    (void)quad;
}

void wm_platform_draw_vertices(WmPlatform *platform,
                               const WmDrawVertex vertices[4],
                               uint32_t texture) {
    (void)platform;
    (void)vertices;
    (void)texture;
}

void wm_platform_draw_material_quad(WmPlatform *platform,
                                    const WmMaterialQuad *quad) {
    (void)platform;
    material_draws++;
    (void)quad;
}

void wm_platform_prepare_material(WmPlatform *platform,
                                  const WmMaterialQuad *quad) {
    (void)platform;
    (void)quad;
}

uint32_t wm_platform_create_texture(WmPlatform *platform, int width,
                                    int height, const uint8_t *rgba) {
    (void)platform;
    assert(width > 0 && height > 0 && rgba);
    return next_texture++;
}

void wm_platform_destroy_texture(WmPlatform *platform, uint32_t texture) {
    (void)platform;
    (void)texture;
}

static int pane_center(const WmLayout *layout, const char *name,
                       bool x_coordinate) {
    WmSourceRect rect;
    assert(wm_source_pane_rect(layout, name, true, WM_LAYOUT_IPL,
                               NULL, &rect));
    return (int)(x_coordinate ? rect.x + rect.width * 0.5f :
                                rect.y + rect.height * 0.5f);
}

int main(int argc, char **argv) {
    const char *assets = argc > 1 ? argv[1] : ".local/native-assets";
    char path[4096];
    int length = snprintf(path, sizeof(path),
                          "%s/layouts/board/th_Adress_a.json", assets);
    assert(length > 0 && length < (int)sizeof(path));
    FILE *check = fopen(path, "rb");
    if (!check) {
        puts("Address Book WAD test skipped: local export absent.");
        return 0;
    }
    fclose(check);

    WmPlatform *platform = (WmPlatform *)1;
    WmTextureCache *textures = wm_texture_cache_create(
        platform, assets, 64u * 1024u * 1024u);
    WmFontCache *fonts = wm_font_cache_create(
        platform, assets, 8u * 1024u * 1024u);
    assert(textures && fonts);
    WmBoardAddress *book = wm_board_address_create(
        platform, assets, textures, fonts);
    assert(book);
    char error[160] = {0};
    char contacts_path[] = "/tmp/wm-board-address-XXXXXX";
    int contacts_file = mkstemp(contacts_path);
    assert(contacts_file >= 0);
    assert(close(contacts_file) == 0);
    assert(unlink(contacts_path) == 0);
    WmBoardContactStoreStatus contact_status =
        wm_board_address_load_contacts(book, contacts_path,
                                       error, sizeof(error));
    assert(contact_status == WM_BOARD_CONTACT_STORE_MISSING);
    assert(wm_board_address_phase(book) == WM_BOARD_ADDRESS_CLOSED);
    assert(wm_board_address_show_memo_no_mii(book));
    assert(!wm_board_address_show_memo_no_mii(book));
    wm_board_address_advance(book, 25.0f);
    material_draws = 0;
    wm_board_address_draw_dialog(book);
    assert(material_draws > 0);
    assert(wm_board_address_dialog_accept(book));
    wm_board_address_advance(book, 38.0f);
    assert(!wm_board_address_dialog_active(book));
    assert(wm_board_address_phase(book) == WM_BOARD_ADDRESS_CLOSED);
    assert(wm_board_address_open(book));
    assert(!wm_board_address_turn(book, true));
    material_draws = 0;
    wm_board_address_draw(book);
    assert(material_draws > 0);
    unsigned cover_draws = material_draws;
    wm_board_address_advance(book, 26.0f);
    assert(wm_board_address_phase(book) == WM_BOARD_ADDRESS_READY);
    assert(wm_board_address_page(book) == 0);

    for (unsigned page = 1; page <= 20; page++) {
        assert(wm_board_address_turn(book, true));
        assert(!wm_board_address_turn(book, true));
        wm_board_address_advance(book, 8.0f);
        material_draws = 0;
        wm_board_address_draw(book);
        assert(material_draws > 0);
        assert(wm_board_address_page(book) == page - 1);
        wm_board_address_advance(book, 8.0f);
        assert(wm_board_address_page(book) == page);
    }
    material_draws = 0;
    wm_board_address_draw(book);
    unsigned last_page_draws = material_draws;
    assert(wm_board_address_turn(book, true));
    material_draws = 0;
    wm_board_address_draw(book);
    unsigned wrapping_draws = material_draws;
    assert(cover_draws > last_page_draws);
    assert(wrapping_draws > cover_draws);
    wm_board_address_advance(book, 16.0f);
    assert(wm_board_address_page(book) == 0);
    assert(wm_board_address_turn(book, false));
    wm_board_address_advance(book, 16.0f);
    assert(wm_board_address_page(book) == 20);
    assert(wm_board_address_back(book));
    assert(!wm_board_address_turn(book, true));
    wm_board_address_advance(book, 10.0f);
    material_draws = 0;
    wm_board_address_draw(book);
    assert(material_draws > 0);
    wm_board_address_advance(book, 6.0f);
    material_draws = 0;
    wm_board_address_draw(book);
    assert(material_draws == 0);
    wm_board_address_advance(book, 32.0f);
    assert(wm_board_address_phase(book) == WM_BOARD_ADDRESS_CLOSED);
    assert(wm_board_address_open(book));
    assert(wm_board_address_page(book) == 0);
    wm_board_address_advance(book, 26.0f);
    assert(wm_board_address_step(book) == WM_BOARD_ADDRESS_STEP_BOOK);
    assert(wm_board_address_register(book));
    assert(!wm_board_address_turn(book, true));
    assert(wm_board_address_phase(book) == WM_BOARD_ADDRESS_REGISTER_PRESS);
    wm_board_address_advance(book, 20.0f);
    assert(wm_board_address_phase(book) == WM_BOARD_ADDRESS_REGISTER_PRESS);
    wm_board_address_advance(book, 1.0f);
    assert(wm_board_address_phase(book) == WM_BOARD_ADDRESS_BOOK_TO_KIND);
    wm_board_address_advance(book, 16.0f);
    material_draws = 0;
    wm_board_address_draw(book);
    assert(material_draws == 0);
    wm_board_address_advance(book, 1.0f);
    assert(wm_board_address_phase(book) == WM_BOARD_ADDRESS_KIND_ENTER);
    wm_board_address_advance(book, 19.0f);
    assert(wm_board_address_phase(book) == WM_BOARD_ADDRESS_KIND_READY);
    assert(wm_board_address_step(book) == WM_BOARD_ADDRESS_STEP_KIND);
    material_draws = 0;
    wm_board_address_draw(book);
    assert(material_draws > 0);
    length = snprintf(path, sizeof(path),
                      "%s/layouts/board/th_Adress_d.json", assets);
    assert(length > 0 && length < (int)sizeof(path));
    WmLayout *kind = wm_layout_load_json(path, error, sizeof(error));
    assert(kind);
    WmLayoutClip kind_pose = {
        .animation = "th_Adress_d_btn_strt",
        .group = "G_btn_strt_fnsh",
        .frame = 18.0f,
        .loop_override = 0
    };
    assert(wm_layout_pose(kind, &kind_pose, 1));
    int wii_x = pane_center(kind, "B_btn_00", true);
    int wii_y = pane_center(kind, "B_btn_00", false);
    int others_x = pane_center(kind, "B_btn_01", true);
    int others_y = pane_center(kind, "B_btn_01", false);
    bool wii = false;
    assert(wm_board_address_kind_hit(book, wii_x, wii_y, &wii) && wii);
    assert(wm_board_address_kind_hit(book, others_x, others_y, &wii) && !wii);
    wm_board_address_hover_kind(book, 0);
    wm_board_address_advance(book, 4.0f);
    assert(wm_board_address_select_kind(book, true));
    assert(!wm_board_address_back(book));
    wm_board_address_advance(book, 21.0f);
    assert(wm_board_address_phase(book) == WM_BOARD_ADDRESS_KIND_TO_FORM);
    wm_board_address_advance(book, 19.0f);
    assert(wm_board_address_phase(book) == WM_BOARD_ADDRESS_FORM_ENTER);
    wm_board_address_advance(book, 21.0f);
    assert(wm_board_address_phase(book) == WM_BOARD_ADDRESS_FORM_READY);
    assert(wm_board_address_step(book) == WM_BOARD_ADDRESS_STEP_FORM);
    assert(wm_board_address_kind_is_wii(book));
    material_draws = 0;
    wm_board_address_draw(book);
    assert(material_draws > 0);
    assert(wm_board_address_insert_text(book, "A8 742285515623182!"));
    assert(strcmp(wm_board_address_text(book), "8742285515623182") == 0);
    assert(wm_board_address_validate(book) == WM_BOARD_ADDRESS_ISSUE_NONE);
    assert(!wm_board_address_insert_text(book, "3"));
    assert(wm_board_address_backspace(book));
    assert(wm_board_address_validate(book) ==
           WM_BOARD_ADDRESS_ISSUE_INVALID_WII);
    assert(wm_board_address_insert_text(book, "3"));
    assert(wm_board_address_show_issue(book));
    assert(wm_board_address_dialog_active(book));
    material_draws = 0;
    wm_board_address_draw_dialog(book);
    assert(material_draws > 0);
    wm_board_address_advance(book, 25.0f);
    length = snprintf(path, sizeof(path),
                      "%s/layouts/dlgWdw/my_DialogWindow_a1.json", assets);
    assert(length > 0 && length < (int)sizeof(path));
    WmLayout *dialog = wm_layout_load_json(path, error, sizeof(error));
    assert(dialog);
    WmLayoutClip dialog_pose = {
        .animation = "my_DialogWindow_a1_DialogIn",
        .group = "G_InOut",
        .frame = 24.0f,
        .loop_override = 0
    };
    assert(wm_layout_pose(dialog, &dialog_pose, 1));
    int dialog_x = pane_center(dialog, "B_BtnB", true);
    int dialog_y = pane_center(dialog, "B_BtnB", false);
    wm_layout_destroy(dialog);
    assert(wm_board_address_dialog_hit(book, dialog_x, dialog_y));
    assert(wm_board_address_dialog_accept(book));
    wm_board_address_advance(book, 38.0f);
    assert(!wm_board_address_dialog_active(book));
    /* Synthetic structural fixtures from the maintained HTML validation
     * tests, checked there against the original USA 4.3 function. */
    static const char *const valid_numbers[] = {
        "7053433507880718", "8742285515623182", "8179332609414415",
        "0860984825562383", "3675735668494095", "9868184080303375"
    };
    static const char *const invalid_numbers[] = {
        "1234567812345678", "0000000000000000", "7053433507880719"
    };
    for (size_t index = 0; index <
         sizeof(valid_numbers) / sizeof(valid_numbers[0]); index++) {
        while (wm_board_address_backspace(book)) {}
        assert(wm_board_address_insert_text(book, valid_numbers[index]));
        assert(wm_board_address_validate(book) ==
               WM_BOARD_ADDRESS_ISSUE_NONE);
    }
    for (size_t index = 0; index <
         sizeof(invalid_numbers) / sizeof(invalid_numbers[0]); index++) {
        while (wm_board_address_backspace(book)) {}
        assert(wm_board_address_insert_text(book, invalid_numbers[index]));
        assert(wm_board_address_validate(book) ==
               WM_BOARD_ADDRESS_ISSUE_INVALID_WII);
    }
    while (wm_board_address_backspace(book)) {}
    assert(wm_board_address_insert_text(book, "8742285515623182"));
    assert(wm_board_address_field_valid(book));
    assert(wm_board_address_submit(book));
    assert(!wm_board_address_back(book));
    wm_board_address_advance(book, 21.0f);
    assert(wm_board_address_phase(book) ==
           WM_BOARD_ADDRESS_FORM_TO_NICKNAME);
    wm_board_address_advance(book, 40.0f);
    assert(wm_board_address_phase(book) ==
           WM_BOARD_ADDRESS_NICKNAME_READY);
    assert(!wm_board_address_field_valid(book));
    assert(wm_board_address_insert_text(book, "Local"));
    assert(strcmp(wm_board_address_field_text(book), "Local") == 0);
    assert(wm_board_address_field_valid(book));
    assert(wm_board_address_submit(book));
    wm_board_address_advance(book, 63.0f);
    assert(wm_board_address_phase(book) == WM_BOARD_ADDRESS_MII_READY);
    material_draws = 0;
    wm_board_address_draw(book);
    assert(material_draws > 0);
    assert(wm_board_address_show_no_mii(book));
    wm_board_address_advance(book, 25.0f);
    assert(wm_board_address_dialog_hit(book, dialog_x, dialog_y));
    assert(wm_board_address_dialog_accept(book));
    wm_board_address_advance(book, 38.0f);
    assert(!wm_board_address_dialog_active(book));
    assert(wm_board_address_submit(book));
    wm_board_address_advance(book, 61.0f);
    assert(wm_board_address_phase(book) == WM_BOARD_ADDRESS_REVIEW_READY);
    material_draws = 0;
    wm_board_address_draw(book);
    assert(material_draws > 0);
    length = snprintf(path, sizeof(path),
                      "%s/layouts/board/th_Adress_b.json", assets);
    assert(length > 0 && length < (int)sizeof(path));
    WmLayout *review = wm_layout_load_json(path, error, sizeof(error));
    assert(review);
    WmLayoutClip review_pose = {
        .animation = "th_Adress_b_card_strt",
        .group = "card_strt_fnsh",
        .frame = 18.0f,
        .loop_override = 0
    };
    assert(wm_layout_pose(review, &review_pose, 1));
    int review_x = pane_center(review, "B_card_beta", true);
    int review_y = pane_center(review, "B_card_beta", false);
    wm_layout_destroy(review);
    assert(wm_board_address_review_hit(book, review_x, review_y));
    assert(wm_board_address_show_address_info(book));
    assert(wm_board_address_phase(book) ==
           WM_BOARD_ADDRESS_REVIEW_INFO_PRESS);
    assert(!wm_board_address_dialog_active(book));
    wm_board_address_advance(book, 20.0f);
    assert(!wm_board_address_dialog_active(book));
    wm_board_address_advance(book, 1.0f);
    assert(wm_board_address_dialog_active(book));
    wm_board_address_advance(book, 25.0f);
    assert(wm_board_address_dialog_accept(book));
    wm_board_address_advance(book, 38.0f);
    assert(wm_board_address_back(book));
    wm_board_address_advance(book, 38.0f);
    assert(wm_board_address_phase(book) == WM_BOARD_ADDRESS_MII_READY);
    assert(wm_board_address_back(book));
    wm_board_address_advance(book, 42.0f);
    assert(wm_board_address_phase(book) == WM_BOARD_ADDRESS_NICKNAME_READY);
    assert(wm_board_address_back(book));
    wm_board_address_advance(book, 42.0f);
    assert(wm_board_address_phase(book) == WM_BOARD_ADDRESS_FORM_READY);
    assert(strcmp(wm_board_address_text(book), "8742285515623182") == 0);
    assert(wm_board_address_back(book));
    wm_board_address_advance(book, 19.0f);
    assert(wm_board_address_phase(book) == WM_BOARD_ADDRESS_BOOK_RETURN);
    wm_board_address_advance(book, 17.0f);
    assert(wm_board_address_phase(book) == WM_BOARD_ADDRESS_READY);
    assert(wm_board_address_page(book) == 0);
    assert(wm_board_address_register(book));
    wm_board_address_advance(book, 57.0f);
    assert(wm_board_address_phase(book) == WM_BOARD_ADDRESS_KIND_READY);
    assert(wm_board_address_back(book));
    wm_board_address_advance(book, 36.0f);
    assert(wm_board_address_phase(book) == WM_BOARD_ADDRESS_READY);
    assert(wm_board_address_register(book));
    wm_board_address_advance(book, 57.0f);
    assert(wm_board_address_select_kind(book, false));
    wm_board_address_advance(book, 61.0f);
    assert(wm_board_address_phase(book) == WM_BOARD_ADDRESS_FORM_READY);
    assert(!wm_board_address_kind_is_wii(book));
    assert(wm_board_address_insert_text(book, "a@WII.COM"));
    assert(wm_board_address_validate(book) ==
           WM_BOARD_ADDRESS_ISSUE_INVALID_EMAIL);
    while (wm_board_address_backspace(book)) {}
    assert(wm_board_address_insert_text(book, "a+tag@b.c"));
    assert(wm_board_address_validate(book) == WM_BOARD_ADDRESS_ISSUE_NONE);
    assert(wm_board_address_field_valid(book));
    static const char *const valid_emails[] = {
        "a@b", ".a..@b", "a@-b_", "a+tag@b.c", "a'b@c",
        "a@wii.com.example", "A@EXAMPLE.INVALID"
    };
    static const char *const invalid_emails[] = {
        "a@wii.com", "a@WII.COM", "a@.b", "a@b.", "a@b..c",
        "a@b+c", "a@@b", "@b", "a@", "a b@c", "\xc3\xa9@b",
        "a@\xc3\xa9"
    };
    for (size_t index = 0; index <
         sizeof(valid_emails) / sizeof(valid_emails[0]); index++) {
        while (wm_board_address_backspace(book)) {}
        assert(wm_board_address_insert_text(book, valid_emails[index]));
        assert(wm_board_address_validate(book) ==
               WM_BOARD_ADDRESS_ISSUE_NONE);
    }
    for (size_t index = 0; index <
         sizeof(invalid_emails) / sizeof(invalid_emails[0]); index++) {
        while (wm_board_address_backspace(book)) {}
        assert(wm_board_address_insert_text(book, invalid_emails[index]));
        assert(wm_board_address_validate(book) ==
               WM_BOARD_ADDRESS_ISSUE_INVALID_EMAIL);
    }
    while (wm_board_address_backspace(book)) {}
    assert(wm_board_address_insert_text(book, "a+tag@b.c"));
    assert(wm_board_address_submit(book));
    wm_board_address_advance(book, 61.0f);
    assert(wm_board_address_phase(book) ==
           WM_BOARD_ADDRESS_NICKNAME_READY);
    assert(wm_board_address_insert_text(book, "Local"));
    assert(wm_board_address_submit(book));
    wm_board_address_advance(book, 63.0f);
    assert(wm_board_address_phase(book) == WM_BOARD_ADDRESS_MII_READY);
    assert(wm_board_address_submit(book));
    wm_board_address_advance(book, 61.0f);
    assert(wm_board_address_phase(book) == WM_BOARD_ADDRESS_REVIEW_READY);
    assert(wm_board_address_contact_count(book) == 0);
    assert(wm_board_address_submit(book));
    wm_board_address_advance(book, 18.0f);
    assert(wm_board_address_phase(book) ==
           WM_BOARD_ADDRESS_REVIEW_SAVE_EXIT);
    wm_board_address_advance(book, 1.0f);
    assert(wm_board_address_phase(book) ==
           WM_BOARD_ADDRESS_REGISTERED_NOTICE);
    assert(wm_board_address_dialog_active(book));
    assert(wm_board_address_page(book) == 1);
    assert(wm_board_address_contact_count(book) == 1);
    WmBoardContact saved_contact;
    assert(wm_board_address_contact(book, 0, &saved_contact));
    assert(!saved_contact.wii &&
           strcmp(saved_contact.address, "a+tag@b.c") == 0 &&
           strcmp(saved_contact.nickname, "Local") == 0);
    wm_board_address_advance(book, 25.0f);
    assert(wm_board_address_dialog_accept(book));
    wm_board_address_advance(book, 38.0f);
    assert(wm_board_address_phase(book) == WM_BOARD_ADDRESS_BOOK_RETURN);
    wm_board_address_advance(book, 17.0f);
    assert(wm_board_address_phase(book) == WM_BOARD_ADDRESS_READY);
    material_draws = 0;
    wm_board_address_draw(book);
    assert(material_draws > 0);
    assert(wm_board_address_select_entry(book, 0));
    wm_board_address_advance(book, 59.0f);
    assert(wm_board_address_phase(book) == WM_BOARD_ADDRESS_CONTACT_READY);
    material_draws = 0;
    wm_board_address_draw(book);
    assert(material_draws > 0);
    assert(wm_board_address_show_address_info(book));
    wm_board_address_advance(book, 21.0f);
    assert(wm_board_address_dialog_active(book));
    wm_board_address_advance(book, 25.0f);
    assert(wm_board_address_dialog_accept(book));
    wm_board_address_advance(book, 38.0f);
    length = snprintf(path, sizeof(path),
                      "%s/layouts/board/th_Adress_b.json", assets);
    assert(length > 0 && length < (int)sizeof(path));
    WmLayout *card = wm_layout_load_json(path, error, sizeof(error));
    assert(card);
    WmLayoutClip card_clips[2] = {
        {.animation = "th_Adress_b_card_strt",
         .group = "card_strt_fnsh", .frame = 18.0f,
         .loop_override = 0},
        {.animation = "th_Adress_b_btn_scl_in",
         .group = "crd_btn_10", .frame = 10.0f,
         .loop_override = 0}
    };
    assert(wm_layout_pose(card, card_clips, 2));
    int rename_x = pane_center(card, "B_crd_btn_10", true);
    int rename_y = pane_center(card, "B_crd_btn_10", false);
    int erase_x = pane_center(card, "B_crd_btn_11", true);
    int erase_y = pane_center(card, "B_crd_btn_11", false);
    WmBoardAddressContactAction action;
    assert(wm_board_address_contact_hit(book, rename_x, rename_y, &action));
    assert(action == WM_BOARD_ADDRESS_CONTACT_CHANGE_NICKNAME);
    assert(wm_board_address_contact_hit(book, erase_x, erase_y, &action));
    assert(action == WM_BOARD_ADDRESS_CONTACT_ERASE);
    wm_board_address_hover_contact(book,
        WM_BOARD_ADDRESS_CONTACT_CHANGE_NICKNAME);
    wm_board_address_advance(book, 3.0f);
    wm_board_address_hover_contact(book, WM_BOARD_ADDRESS_CONTACT_NONE);
    wm_board_address_advance(book, 2.0f);
    wm_board_address_hover_contact(book,
        WM_BOARD_ADDRESS_CONTACT_CHANGE_NICKNAME);
    assert(wm_board_address_change_nickname(book));
    assert(!wm_board_address_erase(book));
    wm_board_address_advance(book, 61.0f);
    assert(wm_board_address_phase(book) ==
           WM_BOARD_ADDRESS_CONTACT_NAME_FORM_READY);
    assert(strcmp(wm_board_address_field_text(book), "Local") == 0);
    while (wm_board_address_backspace(book)) {}
    assert(!wm_board_address_field_valid(book));
    assert(wm_board_address_insert_text(book, "Renamed"));
    assert(wm_board_address_submit(book));
    wm_board_address_advance(book, 38.0f);
    assert(wm_board_address_phase(book) == WM_BOARD_ADDRESS_CONTACT_READY);
    assert(wm_board_address_contact(book, 0, &saved_contact));
    assert(strcmp(saved_contact.nickname, "Renamed") == 0);
    assert(wm_board_address_change_nickname(book));
    wm_board_address_advance(book, 61.0f);
    while (wm_board_address_backspace(book)) {}
    assert(wm_board_address_insert_text(book, "Temporary"));
    assert(wm_board_address_back(book));
    wm_board_address_advance(book, 38.0f);
    assert(wm_board_address_phase(book) == WM_BOARD_ADDRESS_CONTACT_READY);
    assert(wm_board_address_contact(book, 0, &saved_contact));
    assert(strcmp(saved_contact.nickname, "Renamed") == 0);
    assert(wm_board_address_erase(book));
    wm_board_address_advance(book, 32.0f);
    assert(wm_board_address_dialog_active(book));
    assert(!wm_board_address_dialog_accept(book));
    wm_board_address_advance(book, 26.0f);
    length = snprintf(path, sizeof(path),
                      "%s/layouts/dlgWdw/my_DialogWindow_b.json", assets);
    assert(length > 0 && length < (int)sizeof(path));
    WmLayout *confirm = wm_layout_load_json(path, error, sizeof(error));
    assert(confirm);
    WmLayoutClip confirm_pose = {
        .animation = "my_DialogWindow_b_DialogIn",
        .group = "G_InOut", .frame = 25.0f,
        .loop_override = 0
    };
    assert(wm_layout_pose(confirm, &confirm_pose, 1));
    bool yes;
    assert(wm_board_address_dialog_choice_hit(book,
        pane_center(confirm, "B_BtnA", true),
        pane_center(confirm, "B_BtnA", false), &yes) && yes);
    assert(wm_board_address_dialog_choice_hit(book,
        pane_center(confirm, "B_BtnB", true),
        pane_center(confirm, "B_BtnB", false), &yes) && !yes);
    wm_board_address_dialog_hover_choice(book, false, true);
    material_draws = 0;
    wm_board_address_draw_dialog(book);
    assert(material_draws > 0);
    wm_layout_destroy(confirm);
    assert(wm_board_address_dialog_choose(book, false));
    wm_board_address_advance(book, 47.0f);
    wm_board_address_advance(book, 32.0f);
    assert(wm_board_address_phase(book) == WM_BOARD_ADDRESS_CONTACT_READY);
    assert(wm_board_address_contact_count(book) == 1);
    assert(wm_board_address_erase(book));
    wm_board_address_advance(book, 32.0f);
    wm_board_address_advance(book, 26.0f);
    assert(wm_board_address_dialog_choose(book, true));
    wm_board_address_advance(book, 47.0f);
    assert(wm_board_address_contact_count(book) == 0);
    wm_board_address_advance(book, 21.0f);
    assert(wm_board_address_phase(book) ==
           WM_BOARD_ADDRESS_CONTACT_ERASED_NOTICE);
    wm_board_address_advance(book, 25.0f);
    assert(wm_board_address_dialog_accept(book));
    wm_board_address_advance(book, 38.0f);
    wm_board_address_advance(book, 36.0f);
    assert(wm_board_address_phase(book) == WM_BOARD_ADDRESS_READY);
    assert(!wm_board_address_contact(book, 0, &saved_contact));
    wm_layout_destroy(card);
    wm_layout_destroy(kind);
    wm_board_address_destroy(book);

    book = wm_board_address_create(platform, assets, textures, fonts);
    assert(book);
    assert(wm_board_address_load_contacts(book, contacts_path, error,
                                           sizeof(error)) ==
           WM_BOARD_CONTACT_STORE_OK);
    assert(wm_board_address_contact_count(book) == 0);
    assert(wm_board_address_open(book));
    wm_board_address_advance(book, 26.0f);
    assert(wm_board_address_turn(book, true));
    wm_board_address_advance(book, 16.0f);
    assert(wm_board_address_page(book) == 1);
    assert(!wm_board_address_select_entry(book, 0));
    wm_board_address_destroy(book);
    assert(unlink(contacts_path) == 0);

    WmBoardCompose *compose = wm_board_compose_create(
        platform, assets, textures, fonts);
    assert(compose);
    assert(wm_board_compose_open(compose));
    wm_board_compose_advance(compose, 39.0f);
    assert(wm_board_compose_phase(compose) == WM_COMPOSE_SELECTOR);
    assert(wm_board_compose_activate(compose, WM_COMPOSE_CONTROL_ADDRESS));
    assert(wm_board_compose_phase(compose) == WM_COMPOSE_ADDRESS);
    assert(wm_board_compose_take_outcome(compose) == WM_COMPOSE_OUTCOME_NONE);
    assert(!wm_board_compose_activate(compose,
                                      WM_COMPOSE_CONTROL_ADDRESS_NEXT));
    wm_board_compose_advance(compose, 26.0f);
    assert(wm_board_compose_address_page(compose) == 0);
    memset(error, 0, sizeof(error));
    length = snprintf(path, sizeof(path),
                      "%s/layouts/cmnBtn/my_IplTop_e.json", assets);
    assert(length > 0 && length < (int)sizeof(path));
    WmLayout *footer = wm_layout_load_json(path, error, sizeof(error));
    assert(footer);
    WmLayoutClip arrow_pose = {
        .animation = "my_IplTop_e",
        .group = "G_ArwR_End",
        .frame = 10160.0f,
        .loop_override = 0
    };
    assert(wm_layout_pose(footer, &arrow_pose, 1));
    int arrow_x = pane_center(footer, "B_ArwR", true);
    int arrow_y = pane_center(footer, "B_ArwR", false);
    WmLayoutClip footer_pose = {
        .animation = "my_IplTop_e",
        .group = "G_SeenChange",
        .frame = 3326.0f,
        .loop_override = 0
    };
    assert(wm_layout_pose(footer, &footer_pose, 1));
    int register_x = pane_center(footer, "B_Add_R", true);
    int register_y = pane_center(footer, "B_Add_R", false);
    wm_layout_destroy(footer);
    assert(wm_board_compose_hit(compose, arrow_x, arrow_y) ==
           WM_COMPOSE_CONTROL_ADDRESS_NEXT);
    wm_board_compose_hover(compose, WM_COMPOSE_CONTROL_ADDRESS_NEXT);
    assert(wm_board_compose_activate(compose,
                                      WM_COMPOSE_CONTROL_ADDRESS_NEXT));
    assert(strcmp(wm_board_compose_take_key_cue(compose),
                  "WIPL_SE_FL_PAGE_INC") == 0);
    assert(wm_board_compose_take_key_cue(compose) == NULL);
    assert(wm_board_compose_hit(compose, arrow_x, arrow_y) ==
           WM_COMPOSE_CONTROL_NONE);
    wm_board_compose_advance(compose, 16.0f);
    assert(wm_board_compose_address_page(compose) == 1);
    assert(wm_board_compose_hit(compose, register_x, register_y) ==
           WM_COMPOSE_CONTROL_POST);
    assert(wm_board_compose_activate(compose, WM_COMPOSE_CONTROL_POST));
    assert(strcmp(wm_board_compose_take_key_cue(compose),
                  "WIPL_SE_DECIDE") == 0);
    assert(wm_board_compose_hit(compose, register_x, register_y) ==
           WM_COMPOSE_CONTROL_NONE);
    wm_board_compose_advance(compose, 57.0f);
    assert(wm_board_compose_hit(compose, others_x, others_y) ==
           WM_COMPOSE_CONTROL_ADDRESS_OTHERS);
    wm_board_compose_hover(compose, WM_COMPOSE_CONTROL_ADDRESS_OTHERS);
    assert(wm_board_compose_activate(compose,
                                      WM_COMPOSE_CONTROL_ADDRESS_OTHERS));
    assert(strcmp(wm_board_compose_take_key_cue(compose),
                  "WIPL_SE_DECIDE") == 0);
    wm_board_compose_advance(compose, 61.0f);
    assert(wm_board_compose_hit(compose, register_x, register_y) ==
           WM_COMPOSE_CONTROL_NONE);
    length = snprintf(path, sizeof(path),
                      "%s/layouts/board/th_Adress_c.json", assets);
    assert(length > 0 && length < (int)sizeof(path));
    WmLayout *form = wm_layout_load_json(path, error, sizeof(error));
    assert(form);
    WmLayoutClip form_clips[3] = {
        {.animation = "th_Adress_c_card_strt",
         .group = "G_card_strt_fnsh", .frame = 18.0f,
         .loop_override = 0},
        {.animation = "th_Adress_c_question_alp_in",
         .group = "G_question_00", .frame = 20.0f,
         .loop_override = 0},
        {.animation = "th_Adress_c_name_alp_in",
         .group = "G_name_00", .frame = 20.0f,
         .loop_override = 0}
    };
    assert(wm_layout_pose(form, form_clips, 3));
    int field_x = pane_center(form, "B_crd_edgi_00", true);
    int field_y = pane_center(form, "B_crd_edgi_00", false);
    wm_layout_destroy(form);
    assert(wm_board_compose_hit(compose, field_x, field_y) ==
           WM_COMPOSE_CONTROL_ADDRESS_EDIT);
    assert(wm_board_compose_activate(compose, WM_COMPOSE_CONTROL_ADDRESS_EDIT));
    assert(wm_board_compose_address_editor_active(compose));
    assert(strcmp(wm_board_compose_take_key_cue(compose),
                  "WIPL_SE_SK_OPEN") == 0);
    assert(wm_board_compose_insert_text(compose, "a@WII.COM"));
    material_draws = 0;
    wm_board_compose_draw(compose);
    assert(material_draws > 0);
    assert(wm_board_compose_finish_edit(compose));
    assert(!wm_board_compose_address_editor_active(compose));
    assert(strcmp(wm_board_compose_take_key_cue(compose),
                  "WIPL_SE_SK_DECIDE_CLOSE") == 0);
    assert(strcmp(wm_board_compose_take_key_cue(compose),
                  "WIPL_SE_INFO_WINDOW") == 0);
    wm_board_compose_advance(compose, 25.0f);
    assert(wm_board_compose_hit(compose, dialog_x, dialog_y) ==
           WM_COMPOSE_CONTROL_ADDRESS_DIALOG_OK);
    assert(wm_board_compose_activate(compose,
                                      WM_COMPOSE_CONTROL_ADDRESS_DIALOG_OK));
    assert(strcmp(wm_board_compose_take_key_cue(compose),
                  "WIPL_SE_DECIDE") == 0);
    wm_board_compose_advance(compose, 38.0f);
    assert(wm_board_compose_hit(compose, field_x, field_y) ==
           WM_COMPOSE_CONTROL_ADDRESS_EDIT);
    assert(wm_board_compose_activate(compose, WM_COMPOSE_CONTROL_ADDRESS_EDIT));
    assert(strcmp(wm_board_compose_take_key_cue(compose),
                  "WIPL_SE_SK_OPEN") == 0);
    while (wm_board_compose_backspace(compose)) {}
    assert(wm_board_compose_insert_text(compose, "a+tag@b.c"));
    assert(wm_board_compose_finish_edit(compose));
    assert(strcmp(wm_board_compose_take_key_cue(compose),
                  "WIPL_SE_SK_DECIDE_CLOSE") == 0);
    assert(wm_board_compose_take_key_cue(compose) == NULL);
    assert(wm_board_compose_hit(compose, register_x, register_y) ==
           WM_COMPOSE_CONTROL_POST);
    assert(wm_board_compose_activate(compose, WM_COMPOSE_CONTROL_POST));
    assert(strcmp(wm_board_compose_take_key_cue(compose),
                  "WIPL_SE_DECIDE") == 0);
    wm_board_compose_advance(compose, 61.0f);
    assert(wm_board_compose_hit(compose, field_x, field_y) ==
           WM_COMPOSE_CONTROL_ADDRESS_EDIT);
    assert(wm_board_compose_activate(compose, WM_COMPOSE_CONTROL_ADDRESS_EDIT));
    assert(strcmp(wm_board_compose_take_key_cue(compose),
                  "WIPL_SE_SK_OPEN") == 0);
    assert(wm_board_compose_insert_text(compose, "Local"));
    assert(wm_board_compose_finish_edit(compose));
    assert(strcmp(wm_board_compose_take_key_cue(compose),
                  "WIPL_SE_SK_DECIDE_CLOSE") == 0);
    assert(wm_board_compose_hit(compose, register_x, register_y) ==
           WM_COMPOSE_CONTROL_POST);
    assert(wm_board_compose_activate(compose, WM_COMPOSE_CONTROL_POST));
    assert(strcmp(wm_board_compose_take_key_cue(compose),
                  "WIPL_SE_DECIDE") == 0);
    wm_board_compose_advance(compose, 63.0f);
    assert(wm_board_compose_hit(compose, field_x, field_y) ==
           WM_COMPOSE_CONTROL_ADDRESS_MII);
    assert(wm_board_compose_activate(compose, WM_COMPOSE_CONTROL_ADDRESS_MII));
    assert(strcmp(wm_board_compose_take_key_cue(compose),
                  "WIPL_SE_INFO_WINDOW") == 0);
    assert(strcmp(wm_board_compose_take_key_cue(compose),
                  "WIPL_SE_DECIDE") == 0);
    wm_board_compose_advance(compose, 25.0f);
    assert(wm_board_compose_hit(compose, dialog_x, dialog_y) ==
           WM_COMPOSE_CONTROL_ADDRESS_DIALOG_OK);
    assert(wm_board_compose_activate(compose,
                                      WM_COMPOSE_CONTROL_ADDRESS_DIALOG_OK));
    assert(strcmp(wm_board_compose_take_key_cue(compose),
                  "WIPL_SE_DECIDE") == 0);
    wm_board_compose_advance(compose, 38.0f);
    assert(wm_board_compose_activate(compose, WM_COMPOSE_CONTROL_POST));
    assert(strcmp(wm_board_compose_take_key_cue(compose),
                  "WIPL_SE_DECIDE") == 0);
    wm_board_compose_advance(compose, 61.0f);
    assert(wm_board_compose_hit(compose, review_x, review_y) ==
           WM_COMPOSE_CONTROL_ADDRESS_INFO);
    assert(wm_board_compose_activate(compose,
                                      WM_COMPOSE_CONTROL_ADDRESS_INFO));
    assert(strcmp(wm_board_compose_take_key_cue(compose),
                  "WIPL_SE_DECIDE") == 0);
    assert(wm_board_compose_take_key_cue(compose) == NULL);
    wm_board_compose_advance(compose, 21.0f);
    assert(strcmp(wm_board_compose_take_key_cue(compose),
                  "WIPL_SE_INFO_WINDOW") == 0);
    wm_board_compose_advance(compose, 25.0f);
    assert(wm_board_compose_hit(compose, dialog_x, dialog_y) ==
           WM_COMPOSE_CONTROL_ADDRESS_DIALOG_OK);
    assert(wm_board_compose_activate(compose,
                                      WM_COMPOSE_CONTROL_ADDRESS_DIALOG_OK));
    assert(strcmp(wm_board_compose_take_key_cue(compose),
                  "WIPL_SE_DECIDE") == 0);
    wm_board_compose_advance(compose, 38.0f);
    assert(wm_board_compose_back(compose));
    assert(strcmp(wm_board_compose_take_key_cue(compose),
                  "WIPL_SE_CANCEL") == 0);
    wm_board_compose_advance(compose, 38.0f);
    assert(wm_board_compose_hit(compose, field_x, field_y) ==
           WM_COMPOSE_CONTROL_ADDRESS_MII);
    assert(wm_board_compose_back(compose));
    assert(strcmp(wm_board_compose_take_key_cue(compose),
                  "WIPL_SE_CANCEL") == 0);
    wm_board_compose_advance(compose, 42.0f);
    assert(wm_board_compose_back(compose));
    assert(strcmp(wm_board_compose_take_key_cue(compose),
                  "WIPL_SE_CANCEL") == 0);
    wm_board_compose_advance(compose, 42.0f);
    assert(wm_board_compose_hit(compose, field_x, field_y) ==
           WM_COMPOSE_CONTROL_ADDRESS_EDIT);
    assert(wm_board_compose_back(compose));
    assert(strcmp(wm_board_compose_take_key_cue(compose),
                  "WIPL_SE_CANCEL") == 0);
    wm_board_compose_advance(compose, 36.0f);
    assert(wm_board_compose_phase(compose) == WM_COMPOSE_ADDRESS);
    assert(wm_board_compose_address_page(compose) == 1);
    assert(wm_board_compose_hit(compose, arrow_x, arrow_y) ==
           WM_COMPOSE_CONTROL_ADDRESS_NEXT);
    assert(wm_board_compose_back(compose));
    assert(strcmp(wm_board_compose_take_key_cue(compose),
                  "WIPL_SE_CANCEL") == 0);
    assert(!wm_board_compose_back(compose));
    wm_board_compose_advance(compose, 47.0f);
    assert(wm_board_compose_phase(compose) == WM_COMPOSE_ADDRESS);
    wm_board_compose_advance(compose, 1.0f);
    assert(wm_board_compose_phase(compose) == WM_COMPOSE_SELECTOR);
    wm_board_compose_destroy(compose);

    WmBoardKeyboard *keyboard = wm_board_keyboard_create(
        platform, assets, textures, fonts);
    assert(keyboard);
    char character[5];
    wm_board_keyboard_set_profile(keyboard, WM_BOARD_KEYBOARD_ADDRESS_WII);
    assert(wm_board_keyboard_profile(keyboard) ==
           WM_BOARD_KEYBOARD_ADDRESS_WII);
    assert(wm_board_keyboard_activate(keyboard, WM_KEYBOARD_PHONE_FIRST,
                                       false, character) ==
           WM_KEYBOARD_ACTION_INSERT);
    assert(strcmp(character, "1") == 0);
    assert(wm_board_keyboard_activate(keyboard, WM_KEYBOARD_RETURN,
                                       false, character) ==
           WM_KEYBOARD_ACTION_NONE);
    assert(wm_board_keyboard_activate(keyboard, WM_KEYBOARD_QWERTY,
                                       false, character) ==
           WM_KEYBOARD_ACTION_NONE);
    material_draws = 0;
    wm_board_keyboard_draw(keyboard, 1.0f, false);
    unsigned numeric_draws = material_draws;
    assert(numeric_draws > 0);
    wm_board_keyboard_set_profile(keyboard, WM_BOARD_KEYBOARD_ADDRESS_EMAIL);
    assert(wm_board_keyboard_activate(keyboard,
                                       WM_KEYBOARD_CHARACTER_FIRST,
                                       false, character) ==
           WM_KEYBOARD_ACTION_INSERT);
    assert(wm_board_keyboard_activate(keyboard, WM_KEYBOARD_MORE,
                                       false, character) ==
           WM_KEYBOARD_ACTION_NONE);
    material_draws = 0;
    wm_board_keyboard_draw(keyboard, 1.0f, false);
    assert(material_draws > 0);
    wm_board_keyboard_set_profile(keyboard,
                                   WM_BOARD_KEYBOARD_ADDRESS_NICKNAME);
    assert(wm_board_keyboard_activate(keyboard, WM_KEYBOARD_MORE,
                                       false, character) ==
           WM_KEYBOARD_ACTION_SYMBOL_OPEN);
    wm_board_keyboard_set_profile(keyboard, WM_BOARD_KEYBOARD_MEMO);
    assert(wm_board_keyboard_profile(keyboard) == WM_BOARD_KEYBOARD_MEMO);
    wm_board_keyboard_destroy(keyboard);
    wm_font_cache_destroy(fonts);
    wm_texture_cache_destroy(textures);
    puts("Address Book pages, registration choice, form, and Back passed.");
    return 0;
}
