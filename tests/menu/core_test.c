#include "wii_menu/menu/menu.h"
#include "wii_menu/support/json.h"

#include <assert.h>
#include <errno.h>
#include <limits.h>
#include <stdio.h>
#include <string.h>

static void finish_transition(WmMenu *menu) {
    wm_menu_tick(menu, 1.0f);
    assert(menu->transition == WM_TRANSITION_NONE);
}

static void test_json_text(void) {
    const char source[] = "{\"nul\":\"a\\u0000b\",\"literal\":\"\\\\u0000\","
                          "\"surrogate\":\"\\ud83c\\udf1f\",\"unpaired\":\"\\ud83c\"}";
    WmJson json;
    assert(wm_json_parse(&json, source, strlen(source)));
    char text[16];
    size_t nul = wm_json_member(&json, 0, "nul");
    assert(wm_json_copy(&json, nul, text, sizeof(text)));
    assert(text[0] == 'a' && text[1] == '\0' && text[2] == 'b');
    assert(!wm_json_copy_text(&json, nul, text, sizeof(text)));
    assert(wm_json_copy_text(&json, wm_json_member(&json, 0, "literal"), text,
                             sizeof(text)));
    assert(strcmp(text, "\\u0000") == 0);
    size_t surrogate = wm_json_member(&json, 0, "surrogate");
    assert(wm_json_copy_text(&json, surrogate, text, sizeof(text)));
    assert(strcmp(text, "\xf0\x9f\x8c\x9f") == 0);
    assert(!wm_json_copy_text(&json, surrogate, text, 4));
    assert(!wm_json_copy_text(&json, wm_json_member(&json, 0, "unpaired"), text,
                              sizeof(text)));
    wm_json_free(&json);
    const char escaped_nul[] = {'"', 'a', '\\', '\0', 'b', '"'};
    assert(!wm_json_parse(&json, escaped_nul, sizeof(escaped_nul)));
}

static void test_json_integer_bounds(void) {
    char source[256];
    int written = snprintf(source, sizeof(source),
                           "[%d,%d,99999999999999999999,"
                           "-99999999999999999999]",
                           INT_MAX, INT_MIN);
    assert(written > 0 && (size_t)written < sizeof(source));
    WmJson json;
    assert(wm_json_parse(&json, source, (size_t)written));
    int value = 0;
    errno = ERANGE;
    assert(wm_json_integer(&json, wm_json_index(&json, 0, 0), &value));
    assert(value == INT_MAX);
    assert(wm_json_integer(&json, wm_json_index(&json, 0, 1), &value));
    assert(value == INT_MIN);
    assert(!wm_json_integer(&json, wm_json_index(&json, 0, 2), &value));
    assert(!wm_json_integer(&json, wm_json_index(&json, 0, 3), &value));
    wm_json_free(&json);
}

int main(void) {
    test_json_text();
    test_json_integer_bounds();
    WmJson json;
    assert(wm_json_load(&json, "tests/fixtures/channels.json", 1024 * 1024));
    char escaped[16];
    assert(wm_json_copy(&json, wm_json_member(&json, 0, "escaped"), escaped,
                        sizeof(escaped)));
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
