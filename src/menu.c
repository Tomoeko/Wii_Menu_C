#include "wii_menu/menu.h"

#include <math.h>
#include <stdio.h>
#include <string.h>

static bool begin_transition(WmMenu *menu, WmTransition transition, float frames) {
    if (menu->transition != WM_TRANSITION_NONE) return false;
    menu->transition_from_screen = menu->screen;
    menu->transition_from_page = menu->page;
    menu->transition_from_selected = menu->selected;
    menu->transition_from_home_open = menu->home_open;
    menu->transition_direction = 0;
    menu->transition = transition;
    menu->transition_elapsed = 0.0f;
    menu->transition_duration = frames / 60.0f;
    return true;
}

void wm_menu_init(WmMenu *menu) {
    memset(menu, 0, sizeof(*menu));
    menu->screen = WM_SCREEN_GRID;
    menu->selected = -1;
    menu->slots[0].occupied = true;
    strcpy(menu->slots[0].id, "disc");
    strcpy(menu->slots[0].title, "Disc Channel");
}

void wm_menu_tick(WmMenu *menu, float elapsed_seconds) {
    if (!menu || menu->transition == WM_TRANSITION_NONE ||
        !isfinite(elapsed_seconds) || elapsed_seconds <= 0.0f) return;
    menu->transition_elapsed = fminf(menu->transition_duration,
                                     menu->transition_elapsed + elapsed_seconds);
    if (menu->transition_elapsed >= menu->transition_duration) {
        menu->transition = WM_TRANSITION_NONE;
        menu->transition_elapsed = 0.0f;
        menu->transition_duration = 0.0f;
    }
}

float wm_menu_transition_progress(const WmMenu *menu) {
    if (!menu || menu->transition == WM_TRANSITION_NONE ||
        menu->transition_duration <= 0.0f) return 1.0f;
    return fminf(1.0f, fmaxf(0.0f,
                             menu->transition_elapsed / menu->transition_duration));
}

float wm_menu_transition_frame(const WmMenu *menu) {
    if (!menu || menu->transition == WM_TRANSITION_NONE) return 0.0f;
    return wm_menu_transition_progress(menu) * menu->transition_duration * 60.0f;
}

WmGridPresentation wm_menu_grid_presentation(const WmMenu *menu) {
    WmGridPresentation result = {
        .page = menu ? menu->page : 0,
        .zoom_slot = -1
    };
    if (!menu) return result;
    float progress = wm_menu_transition_progress(menu);
    if (menu->transition == WM_TRANSITION_PAGE) {
        result.page = menu->transition_from_page;
        result.layout_frame = (menu->transition_direction > 0 ? 40.0f : 0.0f) +
                              progress * 20.0f;
    } else if (menu->transition == WM_TRANSITION_SELECT ||
               menu->transition == WM_TRANSITION_BACK) {
        result.page = menu->transition_from_page;
        result.zoom_slot = menu->transition == WM_TRANSITION_SELECT
                               ? menu->selected : menu->transition_from_selected;
        result.zooming = true;
        result.zoom_out = menu->transition == WM_TRANSITION_BACK;
        result.layout_frame = 200.0f + (result.zoom_out
                                          ? (1.0f - progress) * 28.0f
                                          : progress * 28.0f);
    }
    return result;
}

WmPreviewPresentation wm_menu_preview_presentation(const WmMenu *menu) {
    WmPreviewPresentation result = {
        .slot = menu ? menu->selected : -1,
        .phase = WM_PREVIEW_PHASE_NORMAL,
        .frame = 0.0f
    };
    if (!menu || menu->transition != WM_TRANSITION_PREVIEW) return result;
    float frame = wm_menu_transition_progress(menu) * 20.0f;
    if (frame < 10.0f) {
        result.slot = menu->transition_from_selected;
        result.phase = WM_PREVIEW_PHASE_CHANGE_IN;
        result.frame = frame;
    } else {
        result.phase = WM_PREVIEW_PHASE_CHANGE_OUT;
        result.frame = fminf(10.0f, frame - 10.0f);
    }
    return result;
}

bool wm_menu_change_page(WmMenu *menu, int direction) {
    if (menu->screen != WM_SCREEN_GRID || menu->home_open ||
        (direction != -1 && direction != 1) ||
        menu->page + direction < 0 || menu->page + direction >= WM_PAGE_COUNT ||
        !begin_transition(menu, WM_TRANSITION_PAGE, 20.0f)) return false;
    menu->page += direction;
    menu->transition_direction = direction;
    return true;
}

bool wm_menu_select(WmMenu *menu, int slot) {
    if (menu->screen != WM_SCREEN_GRID || menu->home_open || slot < 0 ||
        slot >= WM_SLOT_COUNT || slot / WM_CHANNELS_PER_PAGE != menu->page ||
        !menu->slots[slot].occupied ||
        !begin_transition(menu, WM_TRANSITION_SELECT, 28.0f)) return false;
    menu->screen = WM_SCREEN_PREVIEW;
    menu->selected = slot;
    return true;
}

bool wm_menu_change_preview(WmMenu *menu, int direction) {
    if (menu->screen != WM_SCREEN_PREVIEW || menu->home_open || menu->notice[0] ||
        (direction != -1 && direction != 1) ||
        menu->transition != WM_TRANSITION_NONE) return false;
    for (int offset = 1; offset < WM_SLOT_COUNT; offset++) {
        int slot = (menu->selected + direction * offset + WM_SLOT_COUNT * 2) % WM_SLOT_COUNT;
        if (!menu->slots[slot].occupied) continue;
        if (!begin_transition(menu, WM_TRANSITION_PREVIEW, 20.0f)) return false;
        menu->selected = slot;
        menu->page = slot / WM_CHANNELS_PER_PAGE;
        menu->transition_direction = direction;
        return true;
    }
    return false;
}

bool wm_menu_back(WmMenu *menu) {
    if (menu->notice[0]) return wm_menu_dismiss_notice(menu);
    if (menu->home_open) return wm_menu_toggle_home(menu);
    if (menu->screen == WM_SCREEN_GRID ||
        !begin_transition(menu,
                          menu->screen == WM_SCREEN_PREVIEW ? WM_TRANSITION_BACK
                                                            : WM_TRANSITION_SETTINGS,
                          menu->screen == WM_SCREEN_PREVIEW ? 28.0f : 18.0f)) return false;
    menu->screen = WM_SCREEN_GRID;
    menu->selected = -1;
    return true;
}

bool wm_menu_open_screen(WmMenu *menu, WmScreen screen) {
    if (menu->screen != WM_SCREEN_GRID || menu->home_open ||
        (screen != WM_SCREEN_SETTINGS && screen != WM_SCREEN_BOARD &&
         screen != WM_SCREEN_SD) ||
        !begin_transition(menu, WM_TRANSITION_SETTINGS, 18.0f)) return false;
    menu->screen = screen;
    return true;
}

bool wm_menu_switch_screen_at_black(WmMenu *menu, WmScreen screen) {
    if (!menu || menu->home_open ||
        (screen != WM_SCREEN_GRID && screen != WM_SCREEN_BOARD &&
         screen != WM_SCREEN_SETTINGS &&
         screen != WM_SCREEN_SD)) return false;
    menu->screen = screen;
    menu->selected = -1;
    menu->transition = WM_TRANSITION_NONE;
    menu->transition_elapsed = 0.0f;
    menu->transition_duration = 0.0f;
    return true;
}

bool wm_menu_toggle_home(WmMenu *menu) {
    if (!begin_transition(menu, WM_TRANSITION_HOME,
                          menu->home_open ? 39.0f : 21.0f)) return false;
    menu->home_open = !menu->home_open;
    return true;
}

bool wm_menu_return_to_menu(WmMenu *menu) {
    if (!menu || !menu->home_open ||
        menu->transition != WM_TRANSITION_NONE) return false;
    menu->home_open = false;
    menu->screen = WM_SCREEN_GRID;
    menu->selected = -1;
    menu->page = 0;
    menu->transition = WM_TRANSITION_NONE;
    menu->transition_elapsed = 0.0f;
    menu->transition_duration = 0.0f;
    menu->transition_from_screen = WM_SCREEN_GRID;
    menu->transition_from_page = 0;
    menu->transition_from_selected = -1;
    menu->transition_from_home_open = false;
    menu->transition_direction = 0;
    return true;
}

bool wm_menu_move_channel(WmMenu *menu, int from, int to) {
    if (menu->screen != WM_SCREEN_GRID || menu->home_open ||
        menu->transition != WM_TRANSITION_NONE || from <= 0 || from >= WM_SLOT_COUNT ||
        to <= 0 || to >= WM_SLOT_COUNT || !menu->slots[from].occupied ||
        menu->slots[to].occupied) return false;
    menu->slots[to] = menu->slots[from];
    memset(&menu->slots[from], 0, sizeof(menu->slots[from]));
    return true;
}

bool wm_menu_start_preview(WmMenu *menu) {
    if (!menu || menu->screen != WM_SCREEN_PREVIEW || menu->home_open ||
        menu->notice[0] || menu->transition != WM_TRANSITION_NONE ||
        menu->selected < 0 || menu->selected >= WM_SLOT_COUNT ||
        !menu->slots[menu->selected].occupied ||
        strcmp(menu->slots[menu->selected].id, "disc") == 0) return false;
    int length = snprintf(menu->notice, sizeof(menu->notice),
                          "%s\nChannel preview",
                          menu->slots[menu->selected].title);
    if (length < 0 || (size_t)length >= sizeof(menu->notice)) {
        menu->notice[0] = '\0';
        return false;
    }
    return true;
}

bool wm_menu_dismiss_notice(WmMenu *menu) {
    if (!menu || !menu->notice[0]) return false;
    menu->notice[0] = '\0';
    return true;
}
