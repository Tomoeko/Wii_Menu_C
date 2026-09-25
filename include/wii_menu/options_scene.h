#ifndef WII_MENU_OPTIONS_SCENE_H
#define WII_MENU_OPTIONS_SCENE_H

#include "wii_menu/font_cache.h"
#include "wii_menu/platform.h"
#include "wii_menu/texture_cache.h"

#include <stdbool.h>

typedef struct WmOptionsScene WmOptionsScene;

typedef enum WmOptionsPage {
    WM_OPTIONS_PAGE_OPTIONS,
    WM_OPTIONS_PAGE_DATA,
    WM_OPTIONS_PAGE_SAVE,
    WM_OPTIONS_PAGE_SYSTEM_SETTINGS,
    WM_OPTIONS_PAGE_CHANNEL_STORAGE,
    WM_OPTIONS_PAGE_WII_STORAGE,
    WM_OPTIONS_PAGE_GAMECUBE_STORAGE
} WmOptionsPage;

typedef enum WmOptionsControl {
    WM_OPTIONS_CONTROL_NONE,
    WM_OPTIONS_CONTROL_DATA,
    WM_OPTIONS_CONTROL_SYSTEM,
    WM_OPTIONS_CONTROL_SAVE,
    WM_OPTIONS_CONTROL_CHANNELS,
    WM_OPTIONS_CONTROL_WII,
    WM_OPTIONS_CONTROL_GAMECUBE,
    WM_OPTIONS_CONTROL_BACK,
    WM_OPTIONS_CONTROL_SETTINGS_PREVIOUS,
    WM_OPTIONS_CONTROL_SETTINGS_NEXT,
    WM_OPTIONS_CONTROL_SETTINGS_ITEM_1,
    WM_OPTIONS_CONTROL_SETTINGS_ITEM_2,
    WM_OPTIONS_CONTROL_SETTINGS_ITEM_3,
    WM_OPTIONS_CONTROL_SETTINGS_ITEM_4,
    WM_OPTIONS_CONTROL_SETTINGS_ITEM_5,
    WM_OPTIONS_CONTROL_SETTINGS_ITEM_6
} WmOptionsControl;

typedef enum WmOptionsPhase {
    WM_OPTIONS_CLOSED,
    WM_OPTIONS_ENTER_BACK,
    WM_OPTIONS_ENTER_BUTTONS,
    WM_OPTIONS_READY,
    WM_OPTIONS_SELECT_FLASH,
    WM_OPTIONS_ENTER_LEVEL,
    WM_OPTIONS_BACK_FLASH,
    WM_OPTIONS_BACK_LEVEL,
    WM_OPTIONS_EXIT_HOLD
} WmOptionsPhase;

typedef enum WmOptionsAction {
    WM_OPTIONS_ACTION_NONE,
    WM_OPTIONS_ACTION_EXITED,
    WM_OPTIONS_ACTION_SYSTEM_SETTINGS,
    WM_OPTIONS_ACTION_CHANNEL_STORAGE,
    WM_OPTIONS_ACTION_WII_STORAGE,
    WM_OPTIONS_ACTION_GAMECUBE_STORAGE,
    WM_OPTIONS_ACTION_SETTINGS_CATEGORY
} WmOptionsAction;

typedef struct WmOptionsSnapshot {
    WmOptionsPage page;
    WmOptionsPhase phase;
    WmOptionsControl hover;
    float phase_frame;
    float phase_duration;
    bool locked;
} WmOptionsSnapshot;

/* The scene owns only parsed source layouts. Platform, texture and font
 * caches remain caller-owned. Assets are exported locally from the USA 4.3
 * System Menu WAD, never bundled with the program. */
WmOptionsScene *wm_options_scene_create(WmPlatform *platform,
                                         const char *assets_directory,
                                         WmTextureCache *textures,
                                         WmFontCache *fonts);
void wm_options_scene_destroy(WmOptionsScene *scene);

bool wm_options_scene_open(WmOptionsScene *scene);
/* Enter the Internet Settings page directly from the Message Board prompt. */
bool wm_options_scene_open_internet(WmOptionsScene *scene);
/* Close the retained Options hierarchy and Settings child on menu restart. */
void wm_options_scene_reset(WmOptionsScene *scene);
void wm_options_scene_advance(WmOptionsScene *scene, float frames);
WmOptionsSnapshot wm_options_scene_snapshot(const WmOptionsScene *scene);
/* The source System Update question gives its left Yes and right No buttons
 * different click cues from ordinary Settings Back/Next controls. */
bool wm_options_scene_update_question(const WmOptionsScene *scene);
/* Return the source cue for a successful click, sampled before activation
 * changes the current Settings page. */
const char *wm_options_scene_click_cue(const WmOptionsScene *scene,
                                        WmOptionsControl control);
WmOptionsAction wm_options_scene_take_action(WmOptionsScene *scene);
/* A category event identifies the selected source index item (1–12). The
 * category's full Opera page has not yet been ported into C. */
unsigned wm_options_scene_take_settings_category(WmOptionsScene *scene);

/* A leaf action hands rendering and interaction to its external scene. Call
 * back after that scene closes to restore the retained Options hierarchy. */
bool wm_options_scene_back(WmOptionsScene *scene);
WmOptionsControl wm_options_scene_hit(WmOptionsScene *scene, int x, int y);
bool wm_options_scene_hover(WmOptionsScene *scene, WmOptionsControl control);
bool wm_options_scene_activate(WmOptionsScene *scene,
                                WmOptionsControl control);
/* Forward source Settings date/time arrow holds to its native scene. A true
 * result applies the first step on down; the caller skips click on up. */
bool wm_options_scene_pointer_down(WmOptionsScene *scene,
                                    WmOptionsControl control);
void wm_options_scene_pointer_up(WmOptionsScene *scene);
/* Console Nickname is a Settings text field. Consume typed keys while it is
 * active so the menu's single-key shortcuts do not interrupt the edit. */
bool wm_options_scene_text_editing(const WmOptionsScene *scene);
bool wm_options_scene_type_ascii(WmOptionsScene *scene, char character);
bool wm_options_scene_backspace(WmOptionsScene *scene);

/* Draw inside an already begun platform frame. Order matches the HTML scene:
 * setup background, opaque Back bars, then option objects and headings. */
bool wm_options_scene_draw(WmOptionsScene *scene);

/* Storage replaces the Options Back layer but retains its selected heading.
 * Draw the storage Back layer between these two calls. */
bool wm_options_scene_draw_background(WmOptionsScene *scene);
bool wm_options_scene_draw_objects(WmOptionsScene *scene);

#endif
