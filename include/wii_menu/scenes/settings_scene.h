#ifndef WII_MENU_SETTINGS_SCENE_H
#define WII_MENU_SETTINGS_SCENE_H

#include "wii_menu/fonts/font_cache.h"
#include "wii_menu/board/board_keyboard.h"
#include "wii_menu/platform/platform.h"
#include "wii_menu/render/texture_cache.h"

#include <stdbool.h>

typedef struct WmSettingsScene WmSettingsScene;

typedef enum WmSettingsPhase {
    WM_SETTINGS_CLOSED,
    WM_SETTINGS_APPEAR,
    WM_SETTINGS_READY,
    WM_SETTINGS_SCROLL
} WmSettingsPhase;

typedef enum WmSettingsControl {
    WM_SETTINGS_CONTROL_NONE,
    WM_SETTINGS_CONTROL_BACK,
    WM_SETTINGS_CONTROL_PREVIOUS,
    WM_SETTINGS_CONTROL_NEXT,
    WM_SETTINGS_CONTROL_ITEM_1,
    WM_SETTINGS_CONTROL_ITEM_2,
    WM_SETTINGS_CONTROL_ITEM_3,
    WM_SETTINGS_CONTROL_ITEM_4,
    WM_SETTINGS_CONTROL_ITEM_5,
    WM_SETTINGS_CONTROL_ITEM_6,
    WM_SETTINGS_CONTROL_NICKNAME_FIELD
} WmSettingsControl;

typedef struct WmSettingsSnapshot {
    unsigned page; /* One through three, as labeled by the source pages. */
    unsigned category; /* Zero on the index; otherwise the selected category. */
    unsigned detail; /* Zero on a category page; otherwise its selected screen. */
    unsigned selection; /* Staged zero-based choice on selectable screens. */
    unsigned year;
    unsigned month;
    unsigned day;
    unsigned hour;
    unsigned minute;
    unsigned sensitivity; /* One through five. */
    char nickname[11]; /* Local in-memory edit, up to ten ASCII characters. */
    bool parental_enabled;
    bool connect24_enabled;
    bool standby_enabled;
    unsigned slot_light;
    bool internet_agreement;
    unsigned connection_slot; /* One through three on Internet profile pages. */
    unsigned country_page;
    unsigned country_choice;
    bool local_format_complete;
    WmSettingsPhase phase;
    float phase_frame;
    WmSettingsControl hover;
    float hover_opacity; /* Source image rollover is immediate: zero or one. */
    float page_opacity; /* Current page in a category/detail raster crossfade. */
} WmSettingsSnapshot;

typedef struct WmSettingsProjection {
    float document_x;
    float document_width;
    float side_width;
} WmSettingsProjection;

/* The source System Menu's 608 x 456 Settings document is rendered locally.
 * This first-party scene renders its three index pages and US category
 * entry screens. Its index page-change curve comes from the exported
 * SceenChange_b WAD layout. Console services are represented by local state. */
WmSettingsScene *wm_settings_scene_create(WmPlatform *platform,
                                           const char *assets_directory,
                                           WmTextureCache *textures,
                                           WmFontCache *fonts);
void wm_settings_scene_destroy(WmSettingsScene *scene);
/* The source 16:9 surface places the 608-pixel document in an 832-pixel
 * composition fitted into the 640-pixel framebuffer. The C backdrop fills
 * that framebuffer independently of this foreground projection.
 * Leave disabled for the source 4:3 document and its original hit geometry. */
void wm_settings_scene_set_wide(WmSettingsScene *scene, bool wide);
WmSettingsProjection wm_settings_scene_projection(
    const WmSettingsScene *scene);
bool wm_settings_scene_open(WmSettingsScene *scene);
/* Open the Internet category directly from the Message Board's connection
 * prompt. Back from its category index exits instead of showing page two. */
bool wm_settings_scene_open_internet(WmSettingsScene *scene);
/* The Address Book prompt enters WiiConnect24 with the same direct return. */
bool wm_settings_scene_open_connect24(WmSettingsScene *scene);
/* Discard transient navigation and input when HOME restarts the menu. */
void wm_settings_scene_reset(WmSettingsScene *scene);
void wm_settings_scene_advance(WmSettingsScene *scene, float frames);
/* Coalesce Calendar hold repeats into one choice-change cue per update. */
bool wm_settings_scene_take_repeat_cue(WmSettingsScene *scene);
WmSettingsSnapshot wm_settings_scene_snapshot(const WmSettingsScene *scene);
bool wm_settings_scene_update_question(const WmSettingsScene *scene);
bool wm_settings_scene_back(WmSettingsScene *scene);
bool wm_settings_scene_take_exit(WmSettingsScene *scene);
/* Reserved for a category unavailable in another resource variant. The
 * current US category screens all return zero through this function. */
unsigned wm_settings_scene_take_category(WmSettingsScene *scene);
const char *wm_settings_category_label(unsigned category);

/* Settings index hit geometry, projected from 608 pixels into the centered
 * 640-pixel logical framebuffer. This pure function is also used for tests. */
WmSettingsControl wm_settings_index_hit(unsigned page, int x, int y);
WmSettingsControl wm_settings_scene_hit(const WmSettingsScene *scene,
                                        int x, int y);
bool wm_settings_scene_hover(WmSettingsScene *scene,
                             WmSettingsControl control);
/* Classify visible directional arrows separately from ordinary button rows. */
bool wm_settings_scene_directional_control(const WmSettingsScene *scene,
                                            WmSettingsControl control);
/* Return the source-requested Settings sound cue for a control. Call before
 * activation, while the source page and its button meaning are still known. */
const char *wm_settings_scene_click_cue(const WmSettingsScene *scene,
                                         WmSettingsControl control);
bool wm_settings_scene_activate(WmSettingsScene *scene,
                                WmSettingsControl control);
/* Date/Time arrows act on press and repeat after 400 ms, then every 150 ms.
 * A true result means the caller must suppress its ordinary release click. */
bool wm_settings_scene_pointer_down(WmSettingsScene *scene,
                                    WmSettingsControl control);
void wm_settings_scene_pointer_up(WmSettingsScene *scene);
/* The source Nickname input accepts ten characters. Physical keys edit at
 * the selected caret while the software keyboard is active. */
bool wm_settings_scene_type_ascii(WmSettingsScene *scene, char character);
bool wm_settings_scene_backspace(WmSettingsScene *scene);
bool wm_settings_scene_editing_nickname(const WmSettingsScene *scene);
bool wm_settings_scene_nickname_keyboard_visible(const WmSettingsScene *scene);
bool wm_settings_scene_place_nickname_caret(WmSettingsScene *scene, int x);
WmBoardKeyboardControl wm_settings_scene_keyboard_hit(WmSettingsScene *scene,
                                                       int x, int y);
bool wm_settings_scene_keyboard_place_caret(WmSettingsScene *scene,
                                             int x, int y);
void wm_settings_scene_keyboard_hover(WmSettingsScene *scene,
                                       WmBoardKeyboardControl control);
const char *wm_settings_scene_keyboard_activate(WmSettingsScene *scene,
                                                 WmBoardKeyboardControl control);
const char *wm_settings_scene_keyboard_close(WmSettingsScene *scene,
                                              bool accept);
bool wm_settings_scene_move_nickname_caret(WmSettingsScene *scene,
                                            int direction);
void wm_settings_scene_keyboard_modifiers(WmSettingsScene *scene,
                                           bool shift_down, bool caps_lock_on);

/* Draw inside a caller-owned platform frame, beneath its pointer. */
bool wm_settings_scene_draw(WmSettingsScene *scene);

#endif
