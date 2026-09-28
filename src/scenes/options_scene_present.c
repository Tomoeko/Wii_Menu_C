#include "options_scene_internal.h"

#include "wii_menu/input/source_hit.h"
#include "wii_menu/layout/layout_present.h"

#include <stdio.h>
#include <string.h>

typedef struct OptionPose {
    WmLayoutClip clips[OPTIONS_POSE_CAPACITY];
    char names[OPTIONS_POSE_CAPACITY][96];
    char groups[OPTIONS_POSE_CAPACITY][32];
    size_t count;
} OptionPose;

static void add_option(OptionPose *pose, int button, const char *suffix, bool extra,
                       float frame) {
    if (button < 0 || button >= OPTIONS_BUTTON_COUNT || !suffix ||
        pose->count >= OPTIONS_POSE_CAPACITY)
        return;
    size_t index = pose->count++;
    snprintf(pose->names[index], sizeof(pose->names[index]), "it_ObjSetUp_a_%s%s",
             options_scene_buttons[button].stem, suffix);
    snprintf(pose->groups[index], sizeof(pose->groups[index]), "G_%s_%02d",
             options_scene_buttons[button].group, extra ? 1 : 0);
    pose->clips[index] = (WmLayoutClip){.animation = pose->names[index],
                                        .frame = frame,
                                        .group = pose->groups[index],
                                        .loop_override = 0};
}

static void add_back(OptionPose *pose, const char *suffix, const char *group,
                     float frame) {
    if (!suffix || pose->count >= OPTIONS_POSE_CAPACITY)
        return;
    size_t index = pose->count++;
    snprintf(pose->names[index], sizeof(pose->names[index]), "it_Button_a_%s", suffix);
    pose->clips[index] = (WmLayoutClip){.animation = pose->names[index],
                                        .frame = frame,
                                        .group = group,
                                        .loop_override = 0};
}

static void add_stored_option(OptionPose *pose, int button, int extra,
                              const StoredClip *clip) {
    if (clip->active) {
        add_option(pose, button, clip->suffix, extra != 0, clip->frame);
    }
}

void options_scene_pose_objects(WmOptionsScene *scene) {
    OptionPose pose = {0};
    for (int index = 0; index < OPTIONS_BUTTON_COUNT; index++) {
        add_stored_option(&pose, index, 0, &scene->object_base[index][0]);
        add_stored_option(&pose, index, 1, &scene->object_base[index][1]);
        add_stored_option(&pose, index, 0, &scene->retained_object_focus[index]);
    }
    float frame = scene->phase_frame;
    int first, second;
    switch (scene->phase) {
        case WM_OPTIONS_ENTER_BUTTONS:
        case WM_OPTIONS_ENTER_LEVEL:
            if (options_scene_page_pair(scene->page, &first, &second)) {
                add_option(&pose, first, "In", false,
                           options_scene_endpoint(frame, 16));
                add_option(&pose, second, "In", false,
                           options_scene_endpoint(frame, 16));
            }
            break;
        case WM_OPTIONS_SELECT_FLASH:
            add_option(&pose, scene->selected, "FoucusFlash", false,
                       options_scene_endpoint(frame, 40));
            add_option(&pose, scene->selected, "FoucusFlash", true,
                       options_scene_endpoint(frame, 40));
            add_option(&pose, scene->sibling, "Out", false,
                       options_scene_endpoint(frame, 16));
            break;
        case WM_OPTIONS_BACK_LEVEL:
            if (options_scene_page_pair(scene->exiting_page, &first, &second)) {
                add_option(&pose, first, "Out", false,
                           options_scene_endpoint(frame, 16));
                add_option(&pose, second, "Out", false,
                           options_scene_endpoint(frame, 16));
            }
            add_option(&pose, scene->selected, "Back", false,
                       options_scene_endpoint(frame, 20));
            add_option(&pose, scene->sibling, "In", false,
                       options_scene_endpoint(frame, 16));
            break;
        default:
            break;
    }
    for (int index = 0; index < OPTIONS_BUTTON_COUNT; index++) {
        const FocusEffect *focus = &scene->focus[index];
        if (!focus->active)
            continue;
        add_option(&pose, index, focus->entering ? "FoucusIn" : "FoucusOut", false,
                   options_scene_endpoint(focus->frame, 7));
    }
    wm_layout_pose(scene->objects, pose.clips, pose.count);
}

void options_scene_pose_back(WmOptionsScene *scene) {
    OptionPose pose = {0};
    if (scene->back_bar.active) {
        add_back(&pose, scene->back_bar.suffix, "G_BarIn", scene->back_bar.frame);
    }
    if (scene->back_focus.active) {
        add_back(&pose, scene->back_focus.suffix, "G_FocusBtnA",
                 scene->back_focus.frame);
    }
    if (scene->back_wii.active) {
        add_back(&pose, scene->back_wii.suffix, "G_Wii", scene->back_wii.frame);
    }
    if (scene->back_select.active) {
        add_back(&pose, scene->back_select.suffix, "G_SelectBtnA",
                 scene->back_select.frame);
    }
    if (scene->retained_back_focus.active) {
        add_back(&pose, scene->retained_back_focus.suffix, "G_FocusBtnA",
                 scene->retained_back_focus.frame);
    }
    if (scene->phase == WM_OPTIONS_ENTER_BACK) {
        add_back(&pose, "SeenIn", "G_BarIn",
                 options_scene_endpoint(scene->phase_frame, 16));
    } else if (scene->phase == WM_OPTIONS_BACK_FLASH) {
        add_back(&pose, "BtnFlash", "G_SelectBtnA",
                 options_scene_endpoint(scene->phase_frame, 19));
    }
    const FocusEffect *focus = &scene->focus[OPTIONS_FOCUS_COUNT - 1];
    if (focus->active) {
        add_back(&pose, focus->entering ? "BtnFoucusIn" : "BtnFoucusOut", "G_FocusBtnA",
                 options_scene_endpoint(focus->frame, 7));
    }
    wm_layout_pose(scene->back, pose.clips, pose.count);
}

static bool contains(WmSourceRect rect, int x, int y) {
    return (float)x >= rect.x && (float)x <= rect.x + rect.width &&
           (float)y >= rect.y && (float)y <= rect.y + rect.height;
}

WmOptionsControl wm_options_scene_hit(WmOptionsScene *scene, int x, int y) {
    if (!scene || scene->phase != WM_OPTIONS_READY || scene->settings_returning ||
        x < 0 || y < 0 || x >= WM_FRAME_WIDTH || y >= WM_FRAME_HEIGHT) {
        return WM_OPTIONS_CONTROL_NONE;
    }
    if (scene->page == WM_OPTIONS_PAGE_SYSTEM_SETTINGS && scene->settings) {
        return options_scene_control_from_settings(
            wm_settings_scene_hit(scene->settings, x, y));
    }
    int first, second;
    if (!options_scene_page_pair(scene->page, &first, &second)) {
        return WM_OPTIONS_CONTROL_NONE;
    }
    options_scene_pose_objects(scene);
    WmSourceRect rect;
    /* Object layer follows the Back layer, and therefore takes hit priority. */
    if (wm_source_pane_rect(scene->objects, options_scene_buttons[second].pane, true,
                            WM_LAYOUT_IPL, NULL, &rect) &&
        contains(rect, x, y))
        return options_scene_buttons[second].control;
    if (wm_source_pane_rect(scene->objects, options_scene_buttons[first].pane, true,
                            WM_LAYOUT_IPL, NULL, &rect) &&
        contains(rect, x, y))
        return options_scene_buttons[first].control;
    options_scene_pose_back(scene);
    if (wm_source_pane_rect(scene->back, "B_Button_00", true, WM_LAYOUT_IPL, NULL,
                            &rect) &&
        contains(rect, x, y))
        return WM_OPTIONS_CONTROL_BACK;
    return WM_OPTIONS_CONTROL_NONE;
}

bool wm_options_scene_draw(WmOptionsScene *scene) {
    if (!scene || scene->phase == WM_OPTIONS_CLOSED)
        return false;
    if (scene->settings_returning)
        wm_platform_set_fade_alpha(scene->platform,
                                   wm_scene_fader_alpha(&scene->settings_exit_fader));
    if (scene->page == WM_OPTIONS_PAGE_SYSTEM_SETTINGS && scene->settings &&
        scene->phase == WM_OPTIONS_READY)
        return wm_settings_scene_draw(scene->settings);
    wm_layout_pose(scene->background, NULL, 0);
    options_scene_pose_back(scene);
    options_scene_pose_objects(scene);
    wm_layout_present_with_fonts(scene->platform, scene->textures, scene->fonts,
                                 scene->background, true, WM_LAYOUT_IPL, NULL);
    wm_layout_present_with_fonts(scene->platform, scene->textures, scene->fonts,
                                 scene->back, true, WM_LAYOUT_IPL, NULL);
    wm_layout_present_with_fonts(scene->platform, scene->textures, scene->fonts,
                                 scene->objects, true, WM_LAYOUT_IPL, NULL);
    return true;
}

bool wm_options_scene_draw_background(WmOptionsScene *scene) {
    if (!scene || scene->phase == WM_OPTIONS_CLOSED)
        return false;
    wm_layout_pose(scene->background, NULL, 0);
    wm_layout_present_with_fonts(scene->platform, scene->textures, scene->fonts,
                                 scene->background, true, WM_LAYOUT_IPL, NULL);
    return true;
}

bool wm_options_scene_draw_objects(WmOptionsScene *scene) {
    if (!scene || scene->phase == WM_OPTIONS_CLOSED)
        return false;
    options_scene_pose_objects(scene);
    wm_layout_present_with_fonts(scene->platform, scene->textures, scene->fonts,
                                 scene->objects, true, WM_LAYOUT_IPL, NULL);
    return true;
}
