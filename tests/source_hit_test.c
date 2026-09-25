#include "wii_menu/source_hit.h"

#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <string.h>

/* The unit test provides the legacy UI result so source geometry can be
 * checked without linking a graphics backend. */
static WmHit fallback;

WmHit wm_ui_hit(const WmMenu *menu, int x, int y)
{
    (void)menu;
    (void)x;
    (void)y;
    return fallback;
}

static bool near(float actual, float expected)
{
    return fabsf(actual - expected) < 0.01f;
}

int main(void)
{
    char error[160] = {0};
    WmLayout *grid = wm_layout_load_json("tests/source_hit_fixture.json",
                                          error, sizeof(error));
    assert(grid != NULL);

    WmSourceRect rect = {0};
    assert(wm_source_pane_rect(grid, "N_Ch_c01", true, WM_LAYOUT_IPL,
                                NULL, &rect));
    assert(near(rect.x, 320.0f + 60.0f * 640.0f / 608.0f));
    assert(near(rect.y, 178.0f));
    assert(near(rect.width, 80.0f * 640.0f / 608.0f));
    assert(near(rect.height, 100.0f));
    assert(!wm_source_pane_rect(grid, "N_Ch_c02", true, WM_LAYOUT_IPL,
                                 NULL, &rect));

    const float parent[12] = {
        1, 0, 0, 50,
        0, 1, 0, 0,
        0, 0, 1, 0
    };
    assert(wm_source_pane_rect(grid, "N_Ch_c01", true, WM_LAYOUT_IPL,
                                parent, &rect));
    assert(near(rect.x, 320.0f + 60.0f * 640.0f / 608.0f +
                        50.0f * 640.0f / 832.0f));

    WmMenu menu = {0};
    menu.screen = WM_SCREEN_GRID;
    menu.slots[0].occupied = true;
    menu.slots[1].occupied = true;
    fallback = (WmHit){WM_HIT_CHANNEL, 0};
    WmHit hit = wm_source_menu_hit(grid, &menu, 400, 200);
    assert(hit.type == WM_HIT_CHANNEL && hit.slot == 0);
    assert(wm_source_menu_slot_at(grid, &menu, 400, 200) == 0);
    menu.slots[0].occupied = false;
    assert(wm_source_menu_slot_at(grid, &menu, 400, 200) == 0);
    assert(wm_source_menu_hit(grid, &menu, 400, 200).type == WM_HIT_NONE);
    menu.slots[0].occupied = true;

    hit = wm_source_menu_hit(grid, &menu, 30, 60);
    assert(hit.type == WM_HIT_NONE);

    fallback = (WmHit){WM_HIT_SETTINGS, -1};
    hit = wm_source_menu_hit(grid, &menu, 30, 370);
    assert(hit.type == WM_HIT_SETTINGS);

    menu.home_open = true;
    fallback = (WmHit){WM_HIT_HOME_MENU, -1};
    hit = wm_source_menu_hit(grid, &menu, 400, 200);
    assert(hit.type == WM_HIT_HOME_MENU);

    wm_layout_destroy(grid);
    puts("Source pane hit tests passed.");
    return 0;
}
