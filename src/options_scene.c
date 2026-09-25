#include "wii_menu/options_scene.h"

#include "wii_menu/layout_present.h"
#include "wii_menu/layout_runtime.h"
#include "wii_menu/material_prepare.h"
#include "wii_menu/scene_fader.h"
#include "wii_menu/settings_scene.h"
#include "wii_menu/source_hit.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

enum {
    OPTIONS_PATH_CAPACITY = 4096,
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

static const OptionButton buttons[OPTIONS_BUTTON_COUNT] = {
    {WM_OPTIONS_CONTROL_DATA, "B_DataManage_00", "DataManage", "SetUp",
     "T_Datamanage0_00", "Data Management", WM_OPTIONS_PAGE_DATA},
    {WM_OPTIONS_CONTROL_SYSTEM, "B_Setting_00", "Setting", "SetUp",
     "T_Setting_00", "Wii Settings", WM_OPTIONS_PAGE_SYSTEM_SETTINGS},
    {WM_OPTIONS_CONTROL_SAVE, "B_SaveData_00", "SaveData", "DataChannel",
     "T_SaveData_00", "Save Data", WM_OPTIONS_PAGE_SAVE},
    {WM_OPTIONS_CONTROL_CHANNELS, "B_Channel_00", "Channel", "DataChannel",
     "T_Channel_00", "Channels", WM_OPTIONS_PAGE_CHANNEL_STORAGE},
    {WM_OPTIONS_CONTROL_WII, "B_Wii_00", "Wii", "SaveData",
     "T_Wii_00", "Wii", WM_OPTIONS_PAGE_WII_STORAGE},
    {WM_OPTIONS_CONTROL_GAMECUBE, "B_Cube_00", "Cube", "SaveData",
     "T_Cube_00", "Nintendo\nGameCube", WM_OPTIONS_PAGE_GAMECUBE_STORAGE}
};

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

typedef struct OptionPose {
    WmLayoutClip clips[OPTIONS_POSE_CAPACITY];
    char names[OPTIONS_POSE_CAPACITY][96];
    char groups[OPTIONS_POSE_CAPACITY][32];
    size_t count;
} OptionPose;

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

static WmLayout *load_layout(const char *directory, const char *relative) {
    char path[OPTIONS_PATH_CAPACITY];
    int length = snprintf(path, sizeof(path), "%s/%s", directory, relative);
    if (length < 0 || length >= (int)sizeof(path)) return NULL;
    char error[160] = {0};
    WmLayout *layout = wm_layout_load_json(path, error, sizeof(error));
    if (!layout) {
        fprintf(stderr, "Could not load Wii Options layout %s: %s\n",
                relative, error);
    }
    return layout;
}

static int button_index(WmOptionsControl control) {
    for (int index = 0; index < OPTIONS_BUTTON_COUNT; index++) {
        if (buttons[index].control == control) return index;
    }
    return -1;
}

static bool page_pair(WmOptionsPage page, int *first, int *second) {
    switch (page) {
        case WM_OPTIONS_PAGE_OPTIONS: *first = 0; *second = 1; return true;
        case WM_OPTIONS_PAGE_DATA: *first = 2; *second = 3; return true;
        case WM_OPTIONS_PAGE_SAVE: *first = 4; *second = 5; return true;
        default: return false;
    }
}

static float phase_duration(WmOptionsPhase phase) {
    switch (phase) {
        case WM_OPTIONS_ENTER_BACK: return 16.0f;
        case WM_OPTIONS_ENTER_BUTTONS: return 16.0f;
        case WM_OPTIONS_SELECT_FLASH: return 40.0f;
        case WM_OPTIONS_ENTER_LEVEL: return 16.0f;
        case WM_OPTIONS_BACK_FLASH: return 19.0f;
        case WM_OPTIONS_BACK_LEVEL: return 20.0f;
        default: return 0.0f;
    }
}

static float endpoint(float frame, float frames) {
    return fminf(fmaxf(frame, 0.0f), frames - 1.0f);
}

static void start_phase(WmOptionsScene *scene, WmOptionsPhase phase) {
    scene->phase = phase;
    scene->phase_frame = 0.0f;
    scene->hover = WM_OPTIONS_CONTROL_NONE;
    memset(scene->focus, 0, sizeof(scene->focus));
}

static void store(StoredClip *clip, const char *suffix, float frame) {
    *clip = (StoredClip){.suffix = suffix, .frame = frame, .active = true};
}

static void retain_focus_pose(WmOptionsScene *scene) {
    /* menu-scenes.js commits sampled focus clips before starting an option's
     * FoucusFlash. In particular, Back may be partway through its rollout;
     * clearing that pose would snap its button scale on the click frame. */
    for (int index = 0; index < OPTIONS_BUTTON_COUNT; index++) {
        const FocusEffect *focus = &scene->focus[index];
        if (focus->active) {
            store(&scene->retained_object_focus[index],
                  focus->entering ? "FoucusIn" : "FoucusOut",
                  endpoint(focus->frame, 7.0f));
        }
    }
    const FocusEffect *back = &scene->focus[OPTIONS_FOCUS_COUNT - 1];
    if (back->active) {
        store(&scene->retained_back_focus,
              back->entering ? "BtnFoucusIn" : "BtnFoucusOut",
              endpoint(back->frame, 7.0f));
    }
}

static void add_option(OptionPose *pose, int button, const char *suffix,
                       bool extra, float frame) {
    if (button < 0 || button >= OPTIONS_BUTTON_COUNT || !suffix ||
        pose->count >= OPTIONS_POSE_CAPACITY) return;
    size_t index = pose->count++;
    snprintf(pose->names[index], sizeof(pose->names[index]),
             "it_ObjSetUp_a_%s%s", buttons[button].stem, suffix);
    snprintf(pose->groups[index], sizeof(pose->groups[index]), "G_%s_%02d",
             buttons[button].group, extra ? 1 : 0);
    pose->clips[index] = (WmLayoutClip){
        .animation = pose->names[index],
        .frame = frame,
        .group = pose->groups[index],
        .loop_override = 0
    };
}

static void add_back(OptionPose *pose, const char *suffix, const char *group,
                     float frame) {
    if (!suffix || pose->count >= OPTIONS_POSE_CAPACITY) return;
    size_t index = pose->count++;
    snprintf(pose->names[index], sizeof(pose->names[index]),
             "it_Button_a_%s", suffix);
    pose->clips[index] = (WmLayoutClip){
        .animation = pose->names[index],
        .frame = frame,
        .group = group,
        .loop_override = 0
    };
}

static void add_stored_option(OptionPose *pose, int button, int extra,
                              const StoredClip *clip) {
    if (clip->active) {
        add_option(pose, button, clip->suffix, extra != 0, clip->frame);
    }
}

static void pose_objects(WmOptionsScene *scene) {
    OptionPose pose = {0};
    for (int index = 0; index < OPTIONS_BUTTON_COUNT; index++) {
        add_stored_option(&pose, index, 0, &scene->object_base[index][0]);
        add_stored_option(&pose, index, 1, &scene->object_base[index][1]);
        add_stored_option(&pose, index, 0,
                          &scene->retained_object_focus[index]);
    }
    float frame = scene->phase_frame;
    int first, second;
    switch (scene->phase) {
        case WM_OPTIONS_ENTER_BUTTONS:
        case WM_OPTIONS_ENTER_LEVEL:
            if (page_pair(scene->page, &first, &second)) {
                add_option(&pose, first, "In", false, endpoint(frame, 16));
                add_option(&pose, second, "In", false, endpoint(frame, 16));
            }
            break;
        case WM_OPTIONS_SELECT_FLASH:
            add_option(&pose, scene->selected, "FoucusFlash", false,
                       endpoint(frame, 40));
            add_option(&pose, scene->selected, "FoucusFlash", true,
                       endpoint(frame, 40));
            add_option(&pose, scene->sibling, "Out", false,
                       endpoint(frame, 16));
            break;
        case WM_OPTIONS_BACK_LEVEL:
            if (page_pair(scene->exiting_page, &first, &second)) {
                add_option(&pose, first, "Out", false, endpoint(frame, 16));
                add_option(&pose, second, "Out", false, endpoint(frame, 16));
            }
            add_option(&pose, scene->selected, "Back", false,
                       endpoint(frame, 20));
            add_option(&pose, scene->sibling, "In", false,
                       endpoint(frame, 16));
            break;
        default: break;
    }
    for (int index = 0; index < OPTIONS_BUTTON_COUNT; index++) {
        const FocusEffect *focus = &scene->focus[index];
        if (!focus->active) continue;
        add_option(&pose, index,
                   focus->entering ? "FoucusIn" : "FoucusOut", false,
                   endpoint(focus->frame, 7));
    }
    wm_layout_pose(scene->objects, pose.clips, pose.count);
}

static void pose_back(WmOptionsScene *scene) {
    OptionPose pose = {0};
    if (scene->back_bar.active) {
        add_back(&pose, scene->back_bar.suffix, "G_BarIn",
                 scene->back_bar.frame);
    }
    if (scene->back_focus.active) {
        add_back(&pose, scene->back_focus.suffix, "G_FocusBtnA",
                 scene->back_focus.frame);
    }
    if (scene->back_wii.active) {
        add_back(&pose, scene->back_wii.suffix, "G_Wii",
                 scene->back_wii.frame);
    }
    if (scene->back_select.active) {
        add_back(&pose, scene->back_select.suffix, "G_SelectBtnA",
                 scene->back_select.frame);
    }
    if (scene->retained_back_focus.active) {
        add_back(&pose, scene->retained_back_focus.suffix,
                 "G_FocusBtnA", scene->retained_back_focus.frame);
    }
    if (scene->phase == WM_OPTIONS_ENTER_BACK) {
        add_back(&pose, "SeenIn", "G_BarIn",
                 endpoint(scene->phase_frame, 16));
    } else if (scene->phase == WM_OPTIONS_BACK_FLASH) {
        add_back(&pose, "BtnFlash", "G_SelectBtnA",
                 endpoint(scene->phase_frame, 19));
    }
    const FocusEffect *focus = &scene->focus[OPTIONS_FOCUS_COUNT - 1];
    if (focus->active) {
        add_back(&pose,
                 focus->entering ? "BtnFoucusIn" : "BtnFoucusOut",
                 "G_FocusBtnA", endpoint(focus->frame, 7));
    }
    wm_layout_pose(scene->back, pose.clips, pose.count);
}

WmOptionsScene *wm_options_scene_create(WmPlatform *platform,
                                         const char *assets_directory,
                                         WmTextureCache *textures,
                                         WmFontCache *fonts) {
    if (!platform || !assets_directory || !assets_directory[0] ||
        !textures || !fonts) return NULL;
    WmOptionsScene *scene = calloc(1, sizeof(*scene));
    if (!scene) return NULL;
    scene->platform = platform;
    scene->textures = textures;
    scene->fonts = fonts;
    scene->background = load_layout(assets_directory,
                                    "layouts/setupBg/it_BgSetUp_a.json");
    scene->back = load_layout(assets_directory,
                              "layouts/setupBtn/it_Button_a.json");
    scene->objects = load_layout(assets_directory,
                                 "layouts/setupSel/it_ObjSetUp_a.json");
    scene->settings = wm_settings_scene_create(platform, assets_directory,
                                               textures, fonts);
    wm_settings_scene_set_wide(scene->settings, true);
    if (!scene->background || !scene->back || !scene->objects) {
        wm_options_scene_destroy(scene);
        return NULL;
    }
    wm_layout_prepare_materials(platform, scene->background);
    wm_layout_prepare_materials(platform, scene->back);
    wm_layout_prepare_materials(platform, scene->objects);
    for (int index = 0; index < OPTIONS_BUTTON_COUNT; index++) {
        wm_layout_set_text(scene->objects, buttons[index].label_pane,
                           buttons[index].label);
    }
    static const struct {
        const char *pane;
        const char *value;
    } heading_labels[] = {
        {"T_DataManage_01", "Data Management"},
        {"T_SaveData_01", "Save Data"},
        {"T_Channel_01", "Channels"},
        {"T_Wii_01", "Wii"},
        {"T_Cube_01", "Nintendo\nGameCube"}
    };
    for (size_t index = 0; index < sizeof(heading_labels) /
                                  sizeof(heading_labels[0]); index++) {
        wm_layout_set_text(scene->objects, heading_labels[index].pane,
                           heading_labels[index].value);
    }
    wm_layout_set_text(scene->back, "T_Button_00", "Back");
    scene->phase = WM_OPTIONS_CLOSED;
    return scene;
}

void wm_options_scene_destroy(WmOptionsScene *scene) {
    if (!scene) return;
    wm_layout_destroy(scene->objects);
    wm_layout_destroy(scene->back);
    wm_layout_destroy(scene->background);
    wm_settings_scene_destroy(scene->settings);
    free(scene);
}

void wm_options_scene_reset(WmOptionsScene *scene) {
    if (!scene) return;
    wm_settings_scene_reset(scene->settings);
    scene->page = WM_OPTIONS_PAGE_OPTIONS;
    scene->exiting_page = WM_OPTIONS_PAGE_OPTIONS;
    scene->phase = WM_OPTIONS_CLOSED;
    scene->phase_frame = 0.0f;
    scene->action = WM_OPTIONS_ACTION_NONE;
    scene->hover = WM_OPTIONS_CONTROL_NONE;
    scene->selected = -1;
    scene->sibling = -1;
    scene->history_count = 0;
    scene->settings_category = 0;
    scene->settings_exit_fader = (WmSceneFader){0};
    scene->settings_returning = false;
    scene->direct_settings = false;
    memset(scene->object_base, 0, sizeof(scene->object_base));
    memset(scene->retained_object_focus, 0,
           sizeof(scene->retained_object_focus));
    memset(&scene->back_bar, 0, sizeof(scene->back_bar));
    memset(&scene->back_focus, 0, sizeof(scene->back_focus));
    memset(&scene->back_select, 0, sizeof(scene->back_select));
    memset(&scene->back_wii, 0, sizeof(scene->back_wii));
    memset(&scene->retained_back_focus, 0,
           sizeof(scene->retained_back_focus));
    memset(scene->focus, 0, sizeof(scene->focus));
}

bool wm_options_scene_open(WmOptionsScene *scene) {
    if (!scene) return false;
    scene->direct_settings = false;
    scene->page = WM_OPTIONS_PAGE_OPTIONS;
    scene->exiting_page = WM_OPTIONS_PAGE_OPTIONS;
    scene->action = WM_OPTIONS_ACTION_NONE;
    scene->history_count = 0;
    scene->settings_category = 0;
    scene->selected = -1;
    scene->sibling = -1;
    for (int index = 0; index < OPTIONS_BUTTON_COUNT; index++) {
        store(&scene->object_base[index][0], "In", 0.0f);
        scene->object_base[index][1].active = false;
        scene->retained_object_focus[index].active = false;
    }
    store(&scene->back_bar, "SeenIn", 0.0f);
    store(&scene->back_focus, "AlphOut", 0.0f);
    scene->back_select.active = false;
    store(&scene->back_wii, "WiiLost", 0.0f);
    scene->retained_back_focus.active = false;
    start_phase(scene, WM_OPTIONS_ENTER_BACK);
    return true;
}

bool wm_options_scene_open_internet(WmOptionsScene *scene) {
    if (!scene || !scene->settings) return false;
    wm_options_scene_reset(scene);
    if (!wm_settings_scene_open_internet(scene->settings)) return false;
    scene->page = WM_OPTIONS_PAGE_SYSTEM_SETTINGS;
    scene->exiting_page = WM_OPTIONS_PAGE_SYSTEM_SETTINGS;
    scene->phase = WM_OPTIONS_READY;
    scene->direct_settings = true;
    return true;
}

static void finish_back_level(WmOptionsScene *scene) {
    int first, second;
    if (page_pair(scene->exiting_page, &first, &second)) {
        store(&scene->object_base[first][0], "Out", 15.0f);
        store(&scene->object_base[second][0], "Out", 15.0f);
    }
    store(&scene->object_base[scene->selected][0], "Back", 19.0f);
    store(&scene->object_base[scene->sibling][0], "In", 15.0f);
    start_phase(scene, WM_OPTIONS_READY);
}

static void finish_phase(WmOptionsScene *scene) {
    int first, second;
    switch (scene->phase) {
        case WM_OPTIONS_ENTER_BACK:
            store(&scene->back_bar, "SeenIn", 15.0f);
            start_phase(scene, WM_OPTIONS_ENTER_BUTTONS);
            break;
        case WM_OPTIONS_ENTER_BUTTONS:
        case WM_OPTIONS_ENTER_LEVEL:
            if (page_pair(scene->page, &first, &second)) {
                store(&scene->object_base[first][0], "In", 15.0f);
                store(&scene->object_base[second][0], "In", 15.0f);
            }
            start_phase(scene, WM_OPTIONS_READY);
            break;
        case WM_OPTIONS_SELECT_FLASH: {
            store(&scene->object_base[scene->selected][0],
                  "FoucusFlash", 39.0f);
            store(&scene->object_base[scene->selected][1],
                  "FoucusFlash", 39.0f);
            store(&scene->object_base[scene->sibling][0], "Out", 15.0f);
            scene->retained_object_focus[scene->selected].active = false;
            scene->retained_object_focus[scene->sibling].active = false;
            scene->page = buttons[scene->selected].destination;
            if (page_pair(scene->page, &first, &second)) {
                start_phase(scene, WM_OPTIONS_ENTER_LEVEL);
            } else if (scene->page == WM_OPTIONS_PAGE_SYSTEM_SETTINGS &&
                       scene->settings &&
                       wm_settings_scene_open(scene->settings)) {
                start_phase(scene, WM_OPTIONS_READY);
            } else {
                static const WmOptionsAction leaf_actions[] = {
                    WM_OPTIONS_ACTION_NONE,
                    WM_OPTIONS_ACTION_NONE,
                    WM_OPTIONS_ACTION_NONE,
                    WM_OPTIONS_ACTION_SYSTEM_SETTINGS,
                    WM_OPTIONS_ACTION_CHANNEL_STORAGE,
                    WM_OPTIONS_ACTION_WII_STORAGE,
                    WM_OPTIONS_ACTION_GAMECUBE_STORAGE
                };
                scene->action = leaf_actions[scene->page];
                start_phase(scene, WM_OPTIONS_READY);
            }
            break;
        }
        case WM_OPTIONS_BACK_FLASH:
            store(&scene->back_select, "BtnFlash", 18.0f);
            scene->retained_back_focus.active = false;
            if (!scene->history_count) {
                scene->action = WM_OPTIONS_ACTION_EXITED;
                /* The caller's 23-update root fade still needs this completed
                 * Options pose underneath it until the black handoff. */
                start_phase(scene, WM_OPTIONS_EXIT_HOLD);
            } else {
                HistoryEntry previous = scene->history[--scene->history_count];
                scene->exiting_page = scene->page;
                scene->page = previous.page;
                scene->selected = previous.selected;
                page_pair(scene->page, &first, &second);
                scene->sibling = scene->selected == first ? second : first;
                start_phase(scene, WM_OPTIONS_BACK_LEVEL);
            }
            break;
        case WM_OPTIONS_BACK_LEVEL:
            finish_back_level(scene);
            break;
        default: break;
    }
}

static void advance_focus(WmOptionsScene *scene, float frames) {
    for (int index = 0; index < OPTIONS_FOCUS_COUNT; index++) {
        FocusEffect *effect = &scene->focus[index];
        if (!effect->active) continue;
        float before = effect->frame;
        effect->frame = fminf(7.0f, before + frames);
        if (effect->frame >= 7.0f && effect->requested != effect->entering) {
            float remaining = fmaxf(0.0f, frames - (7.0f - before));
            effect->entering = effect->requested;
            effect->frame = fminf(7.0f, remaining);
        }
    }
}

void wm_options_scene_advance(WmOptionsScene *scene, float frames) {
    if (!scene || !isfinite(frames) || frames < 0.0f) return;
    if (scene->settings_returning) {
        if (wm_scene_fader_advance(&scene->settings_exit_fader, frames)) {
            wm_settings_scene_reset(scene->settings);
            wm_options_scene_open(scene);
            return;
        }
        if (wm_scene_fader_active(&scene->settings_exit_fader) &&
            scene->settings_exit_fader.phase == WM_SCENE_FADER_OUT)
            return;
        if (!wm_scene_fader_active(&scene->settings_exit_fader))
            scene->settings_returning = false;
    }
    if (scene->page == WM_OPTIONS_PAGE_SYSTEM_SETTINGS && scene->settings &&
        scene->phase == WM_OPTIONS_READY) {
        wm_settings_scene_advance(scene->settings, frames);
        if (wm_settings_scene_take_exit(scene->settings)) {
            if (scene->direct_settings) {
                scene->action = WM_OPTIONS_ACTION_EXITED;
            } else if (wm_scene_fader_start(&scene->settings_exit_fader)) {
                scene->settings_returning = true;
                wm_scene_fader_advance(&scene->settings_exit_fader, frames);
            }
        }
        return;
    }
    advance_focus(scene, frames);
    float remaining = frames;
    for (int steps = 0; steps < 8; steps++) {
        float duration = phase_duration(scene->phase);
        if (duration <= 0.0f) break;
        float amount = fminf(remaining, duration - scene->phase_frame);
        scene->phase_frame += amount;
        remaining -= amount;
        if (scene->phase_frame < duration) break;
        finish_phase(scene);
        if (remaining <= 0.0f) break;
    }
}

static WmOptionsControl option_control_from_settings(
    WmSettingsControl control) {
    switch (control) {
        case WM_SETTINGS_CONTROL_BACK: return WM_OPTIONS_CONTROL_BACK;
        case WM_SETTINGS_CONTROL_PREVIOUS:
            return WM_OPTIONS_CONTROL_SETTINGS_PREVIOUS;
        case WM_SETTINGS_CONTROL_NEXT:
            return WM_OPTIONS_CONTROL_SETTINGS_NEXT;
        case WM_SETTINGS_CONTROL_ITEM_1:
        case WM_SETTINGS_CONTROL_ITEM_2:
        case WM_SETTINGS_CONTROL_ITEM_3:
        case WM_SETTINGS_CONTROL_ITEM_4:
        case WM_SETTINGS_CONTROL_ITEM_5:
        case WM_SETTINGS_CONTROL_ITEM_6:
            return (WmOptionsControl)(WM_OPTIONS_CONTROL_SETTINGS_ITEM_1 +
                control - WM_SETTINGS_CONTROL_ITEM_1);
        default: return WM_OPTIONS_CONTROL_NONE;
    }
}

WmOptionsSnapshot wm_options_scene_snapshot(const WmOptionsScene *scene) {
    if (!scene) return (WmOptionsSnapshot){.phase = WM_OPTIONS_CLOSED};
    bool settings_active = scene->page == WM_OPTIONS_PAGE_SYSTEM_SETTINGS &&
                           scene->settings;
    WmSettingsSnapshot settings = settings_active
        ? wm_settings_scene_snapshot(scene->settings)
        : (WmSettingsSnapshot){0};
    return (WmOptionsSnapshot){
        .page = scene->page,
        .phase = scene->phase,
        .hover = settings_active ? option_control_from_settings(settings.hover)
                                 : scene->hover,
        .phase_frame = scene->phase_frame,
        .phase_duration = phase_duration(scene->phase),
        .locked = scene->settings_returning ||
                  scene->phase != WM_OPTIONS_READY ||
                  (settings_active && settings.phase != WM_SETTINGS_READY)
    };
}

bool wm_options_scene_update_question(const WmOptionsScene *scene) {
    if (!scene || scene->page != WM_OPTIONS_PAGE_SYSTEM_SETTINGS ||
        !scene->settings) return false;
    return wm_settings_scene_update_question(scene->settings);
}

const char *wm_options_scene_click_cue(const WmOptionsScene *scene,
                                        WmOptionsControl control) {
    if (!scene || control == WM_OPTIONS_CONTROL_NONE) return NULL;
    if (scene->page == WM_OPTIONS_PAGE_SYSTEM_SETTINGS && scene->settings) {
        if (wm_settings_scene_update_question(scene->settings)) {
            if (control == WM_OPTIONS_CONTROL_BACK) return "WIPL_SE_DECIDE";
            if (control == WM_OPTIONS_CONTROL_SETTINGS_NEXT)
                return "WIPL_SE_CANCEL";
        }
        WmSettingsSnapshot settings = wm_settings_scene_snapshot(scene->settings);
        if (settings.category == 7 && settings.detail == 3) {
            /* EULA_index.html assigns se 3 to left Yes, se 4 to right No. */
            if (control == WM_OPTIONS_CONTROL_BACK) return "WIPL_SE_DECIDE";
            if (control == WM_OPTIONS_CONTROL_SETTINGS_NEXT)
                return "WIPL_SE_CANCEL";
        }
        if (settings.category == 7 && settings.detail == 11) {
            /* Common0204.html assigns se 3 to left Yes, se 4 to right No. */
            if (control == WM_OPTIONS_CONTROL_BACK) return "WIPL_SE_DECIDE";
            if (control == WM_OPTIONS_CONTROL_SETTINGS_NEXT)
                return "WIPL_SE_CANCEL";
        }
        if (settings.category == 7 &&
            (settings.detail == 1 || settings.detail == 4 ||
             settings.detail == 5 || settings.detail == 6 ||
             settings.detail == 8 || settings.detail == 9 ||
             settings.detail == 10)) {
            /* The extracted Internet pages use se 3 for a row/OK and se 4
             * for Back. Sample before activation changes the detail page. */
            if (control == WM_OPTIONS_CONTROL_BACK)
                return "WIPL_SE_CANCEL";
            if ((settings.detail == 6 || settings.detail == 8 ||
                 settings.detail == 9) &&
                control == WM_OPTIONS_CONTROL_SETTINGS_NEXT)
                return "WIPL_SE_DECIDE";
            if (control >= WM_OPTIONS_CONTROL_SETTINGS_ITEM_1 &&
                control <= WM_OPTIONS_CONTROL_SETTINGS_ITEM_6)
                return "WIPL_SE_DECIDE";
        }
        if (settings.category == 6 && settings.detail == 1) {
            if (control == WM_OPTIONS_CONTROL_BACK) return "WIPL_SE_CANCEL";
            if (control == WM_OPTIONS_CONTROL_SETTINGS_NEXT)
                return "WIPL_SE_DECIDE";
            if (control == WM_OPTIONS_CONTROL_SETTINGS_ITEM_1 ||
                control == WM_OPTIONS_CONTROL_SETTINGS_ITEM_2)
                return "WIPL_SE_CHOICE_CHG";
        }
        if (settings.category == 3 && settings.detail == 1) {
            if (control == WM_OPTIONS_CONTROL_BACK) return "WIPL_SE_CANCEL";
            if (control == WM_OPTIONS_CONTROL_SETTINGS_NEXT)
                return "WIPL_SE_DECIDE";
            if (control == WM_OPTIONS_CONTROL_SETTINGS_ITEM_1)
                return settings.selection >= 32
                    ? "WIPL_SE_CHAR_DELETE_ERROR" : "WIPL_SE_CHOICE_CHG";
            if (control == WM_OPTIONS_CONTROL_SETTINGS_ITEM_2)
                return settings.selection == 0
                    ? "WIPL_SE_CHAR_DELETE_ERROR" : "WIPL_SE_CHOICE_CHG";
        }
        if (settings.category == 3 && settings.detail >= 2 &&
            settings.detail <= 4) {
            if (control == WM_OPTIONS_CONTROL_BACK) return "WIPL_SE_CANCEL";
            if (control == WM_OPTIONS_CONTROL_SETTINGS_NEXT)
                return "WIPL_SE_DECIDE";
            if (control == WM_OPTIONS_CONTROL_SETTINGS_ITEM_1 ||
                control == WM_OPTIONS_CONTROL_SETTINGS_ITEM_2)
                return "WIPL_SE_CHOICE_CHG";
        }
        if (settings.category == 4 && settings.detail == 0) {
            if (control == WM_OPTIONS_CONTROL_BACK) return "WIPL_SE_CANCEL";
            if (control == WM_OPTIONS_CONTROL_SETTINGS_NEXT)
                return "WIPL_SE_DECIDE";
            if (control >= WM_OPTIONS_CONTROL_SETTINGS_ITEM_1 &&
                control <= WM_OPTIONS_CONTROL_SETTINGS_ITEM_3)
                return "WIPL_SE_OUTPUT_MODE_SELECT";
        }
    }
    if (control == WM_OPTIONS_CONTROL_BACK) return "WIPL_SE_CANCEL";
    if (control == WM_OPTIONS_CONTROL_SETTINGS_PREVIOUS ||
        control == WM_OPTIONS_CONTROL_SETTINGS_NEXT) return "page";
    return "confirm";
}

WmOptionsAction wm_options_scene_take_action(WmOptionsScene *scene) {
    if (!scene) return WM_OPTIONS_ACTION_NONE;
    WmOptionsAction action = scene->action;
    scene->action = WM_OPTIONS_ACTION_NONE;
    return action;
}

unsigned wm_options_scene_take_settings_category(WmOptionsScene *scene) {
    if (!scene) return 0;
    unsigned category = scene->settings_category;
    scene->settings_category = 0;
    return category;
}

bool wm_options_scene_back(WmOptionsScene *scene) {
    if (!scene || scene->phase != WM_OPTIONS_READY ||
        scene->settings_returning) return false;
    if (scene->page == WM_OPTIONS_PAGE_SYSTEM_SETTINGS && scene->settings)
        return wm_settings_scene_back(scene->settings);
    if (scene->page == WM_OPTIONS_PAGE_SYSTEM_SETTINGS ||
        scene->page == WM_OPTIONS_PAGE_CHANNEL_STORAGE ||
        scene->page == WM_OPTIONS_PAGE_WII_STORAGE ||
        scene->page == WM_OPTIONS_PAGE_GAMECUBE_STORAGE) {
        if (!scene->history_count) return false;
        int first, second;
        HistoryEntry previous = scene->history[--scene->history_count];
        scene->exiting_page = scene->page;
        scene->page = previous.page;
        scene->selected = previous.selected;
        page_pair(scene->page, &first, &second);
        scene->sibling = scene->selected == first ? second : first;
        start_phase(scene, WM_OPTIONS_BACK_LEVEL);
        return true;
    }
    start_phase(scene, WM_OPTIONS_BACK_FLASH);
    return true;
}

static bool is_control_on_page(const WmOptionsScene *scene,
                               WmOptionsControl control) {
    if (control == WM_OPTIONS_CONTROL_BACK) {
        int first, second;
        return page_pair(scene->page, &first, &second);
    }
    int first, second;
    int index = button_index(control);
    return index >= 0 && page_pair(scene->page, &first, &second) &&
           (index == first || index == second);
}

static int focus_index(WmOptionsControl control) {
    if (control == WM_OPTIONS_CONTROL_BACK) return OPTIONS_FOCUS_COUNT - 1;
    return button_index(control);
}

static void request_focus(WmOptionsScene *scene, WmOptionsControl control,
                          bool entering) {
    int index = focus_index(control);
    if (index < 0) return;
    FocusEffect *effect = &scene->focus[index];
    if (!effect->active) {
        if (entering) *effect = (FocusEffect){true, true, true, 0.0f};
        return;
    }
    effect->requested = entering;
    if (effect->frame >= 7.0f && effect->entering != entering) {
        effect->entering = entering;
        effect->frame = 0.0f;
    }
}

static WmSettingsControl settings_control(WmOptionsControl control) {
    switch (control) {
        case WM_OPTIONS_CONTROL_NONE: return WM_SETTINGS_CONTROL_NONE;
        case WM_OPTIONS_CONTROL_BACK: return WM_SETTINGS_CONTROL_BACK;
        case WM_OPTIONS_CONTROL_SETTINGS_PREVIOUS:
            return WM_SETTINGS_CONTROL_PREVIOUS;
        case WM_OPTIONS_CONTROL_SETTINGS_NEXT:
            return WM_SETTINGS_CONTROL_NEXT;
        case WM_OPTIONS_CONTROL_SETTINGS_ITEM_1:
        case WM_OPTIONS_CONTROL_SETTINGS_ITEM_2:
        case WM_OPTIONS_CONTROL_SETTINGS_ITEM_3:
        case WM_OPTIONS_CONTROL_SETTINGS_ITEM_4:
        case WM_OPTIONS_CONTROL_SETTINGS_ITEM_5:
        case WM_OPTIONS_CONTROL_SETTINGS_ITEM_6:
            return (WmSettingsControl)(WM_SETTINGS_CONTROL_ITEM_1 +
                control - WM_OPTIONS_CONTROL_SETTINGS_ITEM_1);
        default: return WM_SETTINGS_CONTROL_NONE;
    }
}

bool wm_options_scene_pointer_down(WmOptionsScene *scene,
                                    WmOptionsControl control) {
    if (!scene || scene->page != WM_OPTIONS_PAGE_SYSTEM_SETTINGS ||
        !scene->settings || scene->phase != WM_OPTIONS_READY ||
        scene->settings_returning) return false;
    return wm_settings_scene_pointer_down(scene->settings,
                                           settings_control(control));
}

void wm_options_scene_pointer_up(WmOptionsScene *scene) {
    if (scene && scene->settings)
        wm_settings_scene_pointer_up(scene->settings);
}

bool wm_options_scene_text_editing(const WmOptionsScene *scene) {
    if (!scene || scene->page != WM_OPTIONS_PAGE_SYSTEM_SETTINGS ||
        scene->phase != WM_OPTIONS_READY || scene->settings_returning ||
        !scene->settings) return false;
    return wm_settings_scene_editing_nickname(scene->settings);
}

bool wm_options_scene_type_ascii(WmOptionsScene *scene, char character) {
    return wm_options_scene_text_editing(scene) &&
           wm_settings_scene_type_ascii(scene->settings, character);
}

bool wm_options_scene_backspace(WmOptionsScene *scene) {
    return wm_options_scene_text_editing(scene) &&
           wm_settings_scene_backspace(scene->settings);
}

bool wm_options_scene_hover(WmOptionsScene *scene, WmOptionsControl control) {
    if (scene && scene->page == WM_OPTIONS_PAGE_SYSTEM_SETTINGS &&
        scene->settings && scene->phase == WM_OPTIONS_READY &&
        !scene->settings_returning) {
        WmSettingsControl mapped = settings_control(control);
        if (control != WM_OPTIONS_CONTROL_NONE &&
            mapped == WM_SETTINGS_CONTROL_NONE) return false;
        return wm_settings_scene_hover(scene->settings, mapped);
    }
    if (!scene || scene->phase != WM_OPTIONS_READY ||
        (control != WM_OPTIONS_CONTROL_NONE &&
         !is_control_on_page(scene, control)) || scene->hover == control) {
        return false;
    }
    if (scene->hover != WM_OPTIONS_CONTROL_NONE) {
        request_focus(scene, scene->hover, false);
    }
    scene->hover = control;
    if (control != WM_OPTIONS_CONTROL_NONE) request_focus(scene, control, true);
    return true;
}

bool wm_options_scene_activate(WmOptionsScene *scene,
                                WmOptionsControl control) {
    if (scene && scene->page == WM_OPTIONS_PAGE_SYSTEM_SETTINGS &&
        scene->settings && scene->phase == WM_OPTIONS_READY &&
        !scene->settings_returning) {
        WmSettingsControl mapped = settings_control(control);
        if (mapped == WM_SETTINGS_CONTROL_NONE ||
            !wm_settings_scene_activate(scene->settings, mapped))
            return false;
        unsigned category = wm_settings_scene_take_category(scene->settings);
        if (category) {
            scene->settings_category = category;
            scene->action = WM_OPTIONS_ACTION_SETTINGS_CATEGORY;
        }
        return true;
    }
    if (!scene || scene->phase != WM_OPTIONS_READY ||
        !is_control_on_page(scene, control)) return false;
    if (control == WM_OPTIONS_CONTROL_BACK) return wm_options_scene_back(scene);
    if (scene->history_count >= OPTIONS_HISTORY_CAPACITY) return false;
    int first, second;
    int selected = button_index(control);
    if (selected < 0 || !page_pair(scene->page, &first, &second)) return false;
    scene->selected = selected;
    scene->sibling = selected == first ? second : first;
    scene->history[scene->history_count++] = (HistoryEntry){
        .page = scene->page,
        .selected = selected
    };
    retain_focus_pose(scene);
    start_phase(scene, WM_OPTIONS_SELECT_FLASH);
    return true;
}

static bool contains(WmSourceRect rect, int x, int y) {
    return (float)x >= rect.x && (float)x <= rect.x + rect.width &&
           (float)y >= rect.y && (float)y <= rect.y + rect.height;
}

WmOptionsControl wm_options_scene_hit(WmOptionsScene *scene, int x, int y) {
    if (!scene || scene->phase != WM_OPTIONS_READY ||
        scene->settings_returning || x < 0 || y < 0 ||
        x >= WM_FRAME_WIDTH || y >= WM_FRAME_HEIGHT) {
        return WM_OPTIONS_CONTROL_NONE;
    }
    if (scene->page == WM_OPTIONS_PAGE_SYSTEM_SETTINGS && scene->settings) {
        return option_control_from_settings(
            wm_settings_scene_hit(scene->settings, x, y));
    }
    int first, second;
    if (!page_pair(scene->page, &first, &second)) {
        return WM_OPTIONS_CONTROL_NONE;
    }
    pose_objects(scene);
    WmSourceRect rect;
    /* Object layer follows the Back layer, and therefore takes hit priority. */
    if (wm_source_pane_rect(scene->objects, buttons[second].pane, true,
                            WM_LAYOUT_IPL, NULL, &rect) &&
        contains(rect, x, y)) return buttons[second].control;
    if (wm_source_pane_rect(scene->objects, buttons[first].pane, true,
                            WM_LAYOUT_IPL, NULL, &rect) &&
        contains(rect, x, y)) return buttons[first].control;
    pose_back(scene);
    if (wm_source_pane_rect(scene->back, "B_Button_00", true,
                            WM_LAYOUT_IPL, NULL, &rect) &&
        contains(rect, x, y)) return WM_OPTIONS_CONTROL_BACK;
    return WM_OPTIONS_CONTROL_NONE;
}

bool wm_options_scene_draw(WmOptionsScene *scene) {
    if (!scene || scene->phase == WM_OPTIONS_CLOSED) return false;
    if (scene->settings_returning)
        wm_platform_set_fade_alpha(
            scene->platform,
            wm_scene_fader_alpha(&scene->settings_exit_fader));
    if (scene->page == WM_OPTIONS_PAGE_SYSTEM_SETTINGS && scene->settings &&
        scene->phase == WM_OPTIONS_READY)
        return wm_settings_scene_draw(scene->settings);
    wm_layout_pose(scene->background, NULL, 0);
    pose_back(scene);
    pose_objects(scene);
    wm_layout_present_with_fonts(scene->platform, scene->textures, scene->fonts,
                                 scene->background, true, WM_LAYOUT_IPL, NULL);
    wm_layout_present_with_fonts(scene->platform, scene->textures, scene->fonts,
                                 scene->back, true, WM_LAYOUT_IPL, NULL);
    wm_layout_present_with_fonts(scene->platform, scene->textures, scene->fonts,
                                 scene->objects, true, WM_LAYOUT_IPL, NULL);
    return true;
}

bool wm_options_scene_draw_background(WmOptionsScene *scene) {
    if (!scene || scene->phase == WM_OPTIONS_CLOSED) return false;
    wm_layout_pose(scene->background, NULL, 0);
    wm_layout_present_with_fonts(scene->platform, scene->textures, scene->fonts,
                                 scene->background, true, WM_LAYOUT_IPL, NULL);
    return true;
}

bool wm_options_scene_draw_objects(WmOptionsScene *scene) {
    if (!scene || scene->phase == WM_OPTIONS_CLOSED) return false;
    pose_objects(scene);
    wm_layout_present_with_fonts(scene->platform, scene->textures, scene->fonts,
                                 scene->objects, true, WM_LAYOUT_IPL, NULL);
    return true;
}
