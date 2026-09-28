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
    SETTINGS_SIDE_WIDTH = 112
};

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

/* Presentation values also appear in state snapshots for input/UI clients. */
float wm_settings_focus_opacity(const WmSettingsScene *scene,
                                WmSettingsControl control);
float wm_settings_page_opacity(const WmSettingsScene *scene);

#endif
