#include "wii_menu/menu.h"
#include "wii_menu/menu_transition.h"

#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <string.h>

static void close_to(float actual, float expected) {
    if (fabsf(actual - expected) >= 0.0002f) {
        fprintf(stderr, "Expected %.6f, received %.6f\n", expected, actual);
    }
    assert(fabsf(actual - expected) < 0.0002f);
}

static void finish(WmMenu *menu) {
    wm_menu_tick(menu, 2.0f);
    assert(menu->transition == WM_TRANSITION_NONE);
}

static void test_page_and_selection(void) {
    WmMenu menu;
    wm_menu_init(&menu);
    menu.slots[12].occupied = true;

    assert(wm_menu_change_page(&menu, 1));
    assert(menu.transition_from_page == 0);
    assert(menu.transition_direction == 1);
    WmGridPresentation grid = wm_menu_grid_presentation(&menu);
    assert(grid.page == 0 && !grid.zooming);
    close_to(grid.layout_frame, 40.0f);
    wm_menu_tick(&menu, 10.0f / 60.0f);
    grid = wm_menu_grid_presentation(&menu);
    close_to(grid.layout_frame, 50.0f);
    assert(!wm_menu_select(&menu, 12));
    finish(&menu);
    assert(wm_menu_select(&menu, 12));
    assert(menu.transition_from_screen == WM_SCREEN_GRID);
    assert(menu.transition_from_selected == -1);
    grid = wm_menu_grid_presentation(&menu);
    assert(grid.page == 1 && grid.zoom_slot == 12 && grid.zooming);
    close_to(grid.layout_frame, 200.0f);
    wm_menu_tick(&menu, 14.0f / 60.0f);
    grid = wm_menu_grid_presentation(&menu);
    close_to(grid.layout_frame, 214.0f);
    finish(&menu);

    assert(wm_menu_back(&menu));
    assert(menu.transition_from_screen == WM_SCREEN_PREVIEW);
    assert(menu.transition_from_selected == 12);
    grid = wm_menu_grid_presentation(&menu);
    assert(grid.zoom_out && grid.zoom_slot == 12);
    close_to(grid.layout_frame, 228.0f);
    wm_menu_tick(&menu, 14.0f / 60.0f);
    close_to(wm_menu_grid_presentation(&menu).layout_frame, 214.0f);
    finish(&menu);
    assert(menu.screen == WM_SCREEN_GRID);

    assert(wm_menu_change_page(&menu, -1));
    assert(menu.transition_from_page == 1);
    close_to(wm_menu_grid_presentation(&menu).layout_frame, 0.0f);
    wm_menu_tick(&menu, 10.0f / 60.0f);
    close_to(wm_menu_grid_presentation(&menu).layout_frame, 10.0f);
}

static void test_preview_two_phases(void) {
    WmMenu menu;
    wm_menu_init(&menu);
    menu.slots[1].occupied = true;
    assert(wm_menu_select(&menu, 0));
    finish(&menu);
    assert(wm_menu_change_preview(&menu, 1));
    assert(menu.transition_from_selected == 0);
    assert(menu.transition_direction == 1);
    WmPreviewPresentation preview = wm_menu_preview_presentation(&menu);
    assert(preview.slot == 0 && preview.phase == WM_PREVIEW_PHASE_CHANGE_IN);
    wm_menu_tick(&menu, 5.0f / 60.0f);
    preview = wm_menu_preview_presentation(&menu);
    assert(preview.slot == 0 && preview.phase == WM_PREVIEW_PHASE_CHANGE_IN);
    close_to(preview.frame, 5.0f);
    wm_menu_tick(&menu, 5.0f / 60.0f);
    preview = wm_menu_preview_presentation(&menu);
    assert(preview.slot == 1 && preview.phase == WM_PREVIEW_PHASE_CHANGE_OUT);
    close_to(preview.frame, 0.0f);
    wm_menu_tick(&menu, 5.0f / 60.0f);
    close_to(wm_menu_preview_presentation(&menu).frame, 5.0f);
    finish(&menu);
    preview = wm_menu_preview_presentation(&menu);
    assert(preview.slot == 1 && preview.phase == WM_PREVIEW_PHASE_NORMAL);
}

static void test_home_origin(void) {
    WmMenu menu;
    wm_menu_init(&menu);
    assert(wm_menu_toggle_home(&menu));
    assert(!menu.transition_from_home_open);
    assert(menu.home_open);
    close_to(menu.transition_duration, 21.0f / 60.0f);
    finish(&menu);
    assert(wm_menu_toggle_home(&menu));
    assert(menu.transition_from_home_open);
    assert(!menu.home_open);
    close_to(menu.transition_duration, 39.0f / 60.0f);
}

static void test_channel_zoom(void) {
    WmChannelZoom start;
    WmChannelZoom midpoint;
    WmChannelZoom end;
    WmChannelZoom reverse;
    assert(wm_channel_zoom(0.0f, false, -192.0f, 145.0f, false, &start));
    close_to(start.camera_matrix[0], 1.0f);
    close_to(start.camera_matrix[5], 1.0f);
    close_to(start.camera_matrix[3], 0.0f);
    close_to(start.preview_rect.x, 48.0f);
    close_to(start.preview_rect.y, 35.0f);
    close_to(start.preview_rect.width, 128.0f);
    close_to(start.preview_rect.height, 96.0f);
    close_to(start.alpha, 0.0f);

    assert(wm_channel_zoom(14.0f, false, -240.0f, 145.0f, true, &midpoint));
    assert(wm_channel_zoom(14.0f, true, -240.0f, 145.0f, true, &reverse));
    for (size_t index = 0; index < 12; index++) {
        close_to(midpoint.camera_matrix[index], reverse.camera_matrix[index]);
        close_to(midpoint.preview_matrix[index], reverse.preview_matrix[index]);
    }
    close_to(midpoint.alpha, 127.0f / 255.0f);
    float covered = midpoint.preview_rect.width * midpoint.preview_rect.height;
    for (size_t index = 0; index < midpoint.outside_count; index++) {
        covered += midpoint.outside[index].width * midpoint.outside[index].height;
    }
    assert(fabsf(covered - 832.0f * 456.0f) < 0.25f);

    assert(wm_channel_zoom(28.0f, false, -192.0f, 145.0f, false, &end));
    close_to(end.preview_matrix[0], 1.0f);
    close_to(end.preview_matrix[5], 1.0f);
    close_to(end.preview_matrix[3], 0.0f);
    close_to(end.preview_matrix[7], 0.0f);
    close_to(end.alpha, 1.0f);
    assert(end.outside_count == 0 && end.banner_starts);
    assert(!wm_channel_zoom(NAN, false, 0.0f, 0.0f, true, &end));
}

int main(void) {
    test_page_and_selection();
    test_preview_two_phases();
    test_home_origin();
    test_channel_zoom();
    return 0;
}
