#ifndef WII_MENU_MENU_H
#define WII_MENU_MENU_H

#include <stdbool.h>

#define WM_PAGE_COUNT 4
#define WM_CHANNELS_PER_PAGE 12
#define WM_SLOT_COUNT (WM_PAGE_COUNT * WM_CHANNELS_PER_PAGE)

typedef enum WmScreen {
    WM_SCREEN_GRID,
    WM_SCREEN_PREVIEW,
    WM_SCREEN_SETTINGS,
    WM_SCREEN_BOARD,
    WM_SCREEN_SD
} WmScreen;

typedef enum WmTransition {
    WM_TRANSITION_NONE,
    WM_TRANSITION_PAGE,
    WM_TRANSITION_SELECT,
    WM_TRANSITION_BACK,
    WM_TRANSITION_PREVIEW,
    WM_TRANSITION_SETTINGS,
    WM_TRANSITION_HOME
} WmTransition;

typedef enum WmPreviewPhase {
    WM_PREVIEW_PHASE_NORMAL,
    WM_PREVIEW_PHASE_CHANGE_IN,
    WM_PREVIEW_PHASE_CHANGE_OUT
} WmPreviewPhase;

typedef struct WmGridPresentation {
    int page;
    int zoom_slot;
    float layout_frame;
    bool zooming;
    bool zoom_out;
} WmGridPresentation;

typedef struct WmPreviewPresentation {
    int slot;
    WmPreviewPhase phase;
    float frame;
} WmPreviewPresentation;

typedef struct WmChannel {
    char id[65];
    char title[128];
    char icon_layout[256];
    char banner_layout[256];
    bool occupied;
} WmChannel;

typedef struct WmMenu {
    WmChannel slots[WM_SLOT_COUNT];
    WmScreen screen;
    int page;
    int selected;
    bool home_open;
    char notice[256];
    WmTransition transition;
    float transition_elapsed;
    float transition_duration;
    WmScreen transition_from_screen;
    int transition_from_page;
    int transition_from_selected;
    bool transition_from_home_open;
    int transition_direction;
} WmMenu;

void wm_menu_init(WmMenu *menu);
void wm_menu_tick(WmMenu *menu, float elapsed_seconds);
float wm_menu_transition_progress(const WmMenu *menu);
float wm_menu_transition_frame(const WmMenu *menu);
WmGridPresentation wm_menu_grid_presentation(const WmMenu *menu);
WmPreviewPresentation wm_menu_preview_presentation(const WmMenu *menu);
bool wm_menu_change_page(WmMenu *menu, int direction);
bool wm_menu_select(WmMenu *menu, int slot);
bool wm_menu_change_preview(WmMenu *menu, int direction);
bool wm_menu_back(WmMenu *menu);
bool wm_menu_open_screen(WmMenu *menu, WmScreen screen);
/* SceneFader owns the timing for Options and SD. Switch only at full black. */
bool wm_menu_switch_screen_at_black(WmMenu *menu, WmScreen screen);
bool wm_menu_toggle_home(WmMenu *menu);
/* HOME owns its exit animation. Commit Return to Wii Menu when its restart
 * reaches the grid, without scheduling a second HOME transition. */
bool wm_menu_return_to_menu(WmMenu *menu);
bool wm_menu_move_channel(WmMenu *menu, int from, int to);
bool wm_menu_start_preview(WmMenu *menu);
bool wm_menu_dismiss_notice(WmMenu *menu);
bool wm_catalog_load(WmMenu *menu, const char *assets_directory);

#endif
