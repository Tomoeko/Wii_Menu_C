#ifndef WII_MENU_OPTIONS_SCENE_INTERNAL_H
#define WII_MENU_OPTIONS_SCENE_INTERNAL_H

#include "wii_menu/animation/scene_fader.h"
#include "wii_menu/layout/layout_runtime.h"
#include "wii_menu/scenes/options_scene.h"
#include "wii_menu/scenes/settings_scene.h"

#include <math.h>
#include <stddef.h>

enum {
    OPTIONS_POSE_CAPACITY = 40,
    OPTIONS_BUTTON_COUNT = 6,
    OPTIONS_FOCUS_COUNT = 7,
    OPTIONS_HISTORY_CAPACITY = 3
};

typedef struct OptionButton {
    WmOptionsControl control;
    const char *pane;
    const char *group;
    const char *stem;
    const char *label_pane;
    const char *label;
    WmOptionsPage destination;
} OptionButton;

typedef struct StoredClip {
    const char *suffix;
    float frame;
    bool active;
} StoredClip;

typedef struct FocusEffect {
    bool active;
    bool entering;
    bool requested;
    float frame;
} FocusEffect;

typedef struct HistoryEntry {
    WmOptionsPage page;
    int selected;
} HistoryEntry;

struct WmOptionsScene {
    WmPlatform *platform;
    WmTextureCache *textures;
    WmFontCache *fonts;
    WmLayout *background;
    WmLayout *back;
    WmLayout *objects;
    WmSettingsScene *settings;
    WmOptionsPage page;
    WmOptionsPage exiting_page;
    WmOptionsPhase phase;
    WmOptionsAction action;
    WmOptionsControl hover;
    float phase_frame;
    int selected;
    int sibling;
    StoredClip object_base[OPTIONS_BUTTON_COUNT][2];
    StoredClip retained_object_focus[OPTIONS_BUTTON_COUNT];
    StoredClip back_bar;
    StoredClip back_focus;
    StoredClip back_select;
    StoredClip back_wii;
    StoredClip retained_back_focus;
    FocusEffect focus[OPTIONS_FOCUS_COUNT];
    HistoryEntry history[OPTIONS_HISTORY_CAPACITY];
    size_t history_count;
    unsigned settings_category;
    WmSceneFader settings_exit_fader;
    bool settings_returning;
    bool direct_settings;
};

extern const OptionButton options_scene_buttons[OPTIONS_BUTTON_COUNT];

bool options_scene_page_pair(WmOptionsPage page, int *first, int *second);
WmOptionsControl options_scene_control_from_settings(WmSettingsControl control);
void options_scene_pose_objects(WmOptionsScene *scene);
void options_scene_pose_back(WmOptionsScene *scene);

static inline float options_scene_endpoint(float frame, float frames) {
    return fminf(fmaxf(frame, 0.0f), frames - 1.0f);
}

#endif
