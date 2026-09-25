#include "wii_menu/menu.h"
#include "wii_menu/json.h"

#include <assert.h>
#include <string.h>

static void finish_transition(WmMenu *menu) {
    wm_menu_tick(menu, 1.0f);
    assert(menu->transition == WM_TRANSITION_NONE);
}

int main(void) {
    WmJson json;
    assert(wm_json_load(&json, "tests/fixtures/channels.json", 1024 * 1024));
    char escaped[16];
    assert(wm_json_copy(&json, wm_json_member(&json, 0, "escaped"),
                        escaped, sizeof(escaped)));
    assert(strcmp(escaped, "\xc3\xa9 \xf0\x9f\x8c\x9f") == 0);
    wm_json_free(&json);

    WmMenu menu;
    wm_menu_init(&menu);
    assert(menu.slots[0].occupied);
    assert(menu.selected == -1);
    assert(wm_catalog_load(&menu, "tests/fixtures"));
    assert(strcmp(menu.slots[1].title, "Photo Channel") == 0);
    assert(strcmp(menu.slots[14].title, "News Channel") == 0);

    assert(wm_menu_select(&menu, 0));
    assert(menu.screen == WM_SCREEN_PREVIEW);
    assert(!wm_menu_back(&menu)); /* Input remains locked during the selection. */
    finish_transition(&menu);
    assert(wm_menu_change_preview(&menu, 1));
    assert(menu.selected == 1);
    finish_transition(&menu);
    assert(wm_menu_back(&menu));
    finish_transition(&menu);

    assert(wm_menu_change_page(&menu, 1));
    assert(menu.page == 1);
    assert(!wm_menu_select(&menu, 14));
    finish_transition(&menu);
    assert(wm_menu_select(&menu, 14));
    finish_transition(&menu);
    assert(wm_menu_toggle_home(&menu));
    assert(menu.home_open);
    assert(!wm_menu_change_preview(&menu, 1));
    finish_transition(&menu);
    assert(wm_menu_return_to_menu(&menu));
    assert(menu.page == 0 && menu.screen == WM_SCREEN_GRID && !menu.home_open);
    assert(menu.transition == WM_TRANSITION_NONE);
    assert(!menu.transition_from_home_open);

    assert(!wm_menu_move_channel(&menu, 0, 2));
    assert(wm_menu_move_channel(&menu, 1, 2));
    assert(!menu.slots[1].occupied && menu.slots[2].occupied);

    WmMenu preview;
    wm_menu_init(&preview);
    assert(wm_catalog_load(&preview, "tests/fixtures"));
    assert(wm_menu_select(&preview, 1));
    finish_transition(&preview);
    assert(wm_menu_start_preview(&preview));
    assert(strcmp(preview.notice, "Photo Channel\nChannel preview") == 0);
    assert(!wm_menu_change_preview(&preview, 1));
    assert(wm_menu_back(&preview));
    assert(preview.notice[0] == '\0' && preview.screen == WM_SCREEN_PREVIEW);
    assert(wm_menu_back(&preview));
    finish_transition(&preview);
    assert(wm_menu_select(&preview, 0));
    finish_transition(&preview);
    assert(!wm_menu_start_preview(&preview));
    return 0;
}
