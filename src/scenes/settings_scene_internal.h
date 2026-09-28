#ifndef WII_MENU_SETTINGS_SCENE_INTERNAL_H
#define WII_MENU_SETTINGS_SCENE_INTERNAL_H

#include "wii_menu/scenes/settings_scene.h"

#include "wii_menu/fonts/outline_font.h"
#include "wii_menu/layout/layout_runtime.h"

enum {
    SETTINGS_INDEX_PAGES = 3,
    SETTINGS_ITEMS_PER_PAGE = 4,
    SETTINGS_NICKNAME_LIMIT = 10,
    SETTINGS_WIDE_WIDTH = 832,
    SETTINGS_SIDE_WIDTH = 112,
    SETTINGS_NICKNAME_FIELD_X = 104,
    SETTINGS_NICKNAME_FIELD_Y = 190,
    SETTINGS_NICKNAME_FIELD_WIDTH = 432,
    SETTINGS_NICKNAME_FIELD_HEIGHT = 56
};

typedef enum SettingsNicknameKeyboardPhase {
    SETTINGS_NICKNAME_KEYBOARD_CLOSED,
    SETTINGS_NICKNAME_KEYBOARD_PENDING,
    SETTINGS_NICKNAME_KEYBOARD_OPENING,
    SETTINGS_NICKNAME_KEYBOARD_OPEN,
    SETTINGS_NICKNAME_KEYBOARD_CLOSING
} SettingsNicknameKeyboardPhase;

enum SettingsCategory {
    SETTINGS_NICKNAME = 1,
    SETTINGS_CALENDAR = 2,
    SETTINGS_SCREEN = 3,
    SETTINGS_SOUND = 4,
    SETTINGS_PARENTAL = 5,
    SETTINGS_SENSOR = 6,
    SETTINGS_INTERNET = 7,
    SETTINGS_CONNECT24 = 8,
    SETTINGS_LANGUAGE = 9,
    SETTINGS_COUNTRY = 10,
    SETTINGS_UPDATE = 11,
    SETTINGS_FORMAT = 12
};

enum InternetDetail {
    INTERNET_CONNECTION_SELECT = 4,
    INTERNET_WIRELESS_CHOICES = 5,
    INTERNET_WIRED_PROMPT = 6,
    INTERNET_ACCESS_POINT_SEARCH = 7,
    INTERNET_NO_ACCESS_POINT = 8,
    INTERNET_USB_INSTRUCTIONS = 9,
    INTERNET_USB_REGISTRATION = 10,
    INTERNET_USB_EXISTING_CONNECTOR = 11
};

/* Shared state is private to the Settings scene's interaction and drawing
 * modules. The prior-page snapshot owns no resources; destroy it before the
 * live scene's resources. */
struct WmSettingsScene {
    WmPlatform *platform;
    WmTextureCache *textures;
    WmFontCache *fonts;
    WmCachedFont *font;
    WmOutlineFont *outline_font;
    WmLayout *scroll_layout;
    WmSettingsPhase phase;
    WmSettingsControl hover;
    unsigned page;
    unsigned previous_page;
    unsigned pending_category;
    unsigned active_category;
    unsigned detail;
    unsigned selection;
    unsigned sound_choice;
    unsigned language_choice;
    unsigned sensor_position;
    unsigned screen_position;
    unsigned widescreen_choice;
    unsigned resolution_choice;
    unsigned burn_in_choice;
    unsigned year;
    unsigned month;
    unsigned day;
    unsigned hour;
    unsigned minute;
    unsigned edit_year;
    unsigned edit_month;
    unsigned edit_day;
    unsigned edit_hour;
    unsigned edit_minute;
    unsigned sensitivity;
    unsigned edit_sensitivity;
    bool sensitivity_instructions;
    char nickname[SETTINGS_NICKNAME_LIMIT + 1];
    char edit_nickname[SETTINGS_NICKNAME_LIMIT + 1];
    char nickname_before_keyboard[SETTINGS_NICKNAME_LIMIT + 1];
    char nickname_keyboard_display[SETTINGS_NICKNAME_LIMIT + 1];
    char *assets_directory;
    WmBoardKeyboard *nickname_keyboard;
    SettingsNicknameKeyboardPhase nickname_keyboard_phase;
    float nickname_keyboard_frame;
    unsigned nickname_caret;
    bool parental_enabled;
    bool connect24_enabled;
    bool standby_enabled;
    unsigned slot_light;
    bool internet_agreement;
    unsigned connection_slot;
    float connection_search_frames;
    unsigned country_page;
    unsigned country_choice;
    unsigned edit_country_choice;
    bool local_format_complete;
    WmSettingsControl held_control;
    bool repeat_cue_pending;
    float hold_elapsed;
    float next_repeat;
    int direction;
    float phase_frame;
    float page_frame;
    float draw_opacity;
    bool page_crossfade;
    bool wide;
    WmSettingsScene *prior_page;
    bool exit_pending;
    bool direct_entry;
};

extern const unsigned wm_settings_country_page_start[10];

/* Category input owns the local settings transitions. The scene module owns
 * lifecycle and applies the page crossfade after a successful activation. */
void settings_scene_reset_local_values(WmSettingsScene *scene);
bool settings_scene_back_control(WmSettingsScene *scene);
bool settings_scene_activate_category(WmSettingsScene *scene,
                                      WmSettingsControl control);

/* Presentation values also appear in state snapshots for input/UI clients. */
float wm_settings_focus_opacity(const WmSettingsScene *scene,
                                WmSettingsControl control);
float wm_settings_page_opacity(const WmSettingsScene *scene);
void settings_scene_advance_nickname_keyboard(WmSettingsScene *scene,
                                               float frames);
bool settings_scene_open_nickname_keyboard(WmSettingsScene *scene);
void settings_scene_draw_nickname_keyboard(WmSettingsScene *scene);
float settings_scene_nickname_caret_x(const WmSettingsScene *scene);

#endif
