#include "storage_scene_internal.h"

#include "wii_menu/layout/layout_present.h"
#include "wii_menu/render/material_prepare.h"

#include <math.h>
#include <stdio.h>
#include <string.h>

enum { STORAGE_POSE_CAPACITY = 18 };

typedef struct StoragePose {
    WmLayoutClip clips[STORAGE_POSE_CAPACITY];
    /* Clip names and groups must remain valid until wm_layout_pose returns. */
    char names[STORAGE_POSE_CAPACITY][96];
    char groups[STORAGE_POSE_CAPACITY][32];
    size_t count;
} StoragePose;

static WmLayout *slot_icon(WmStorageScene *scene, int slot);

void wm_storage_prepare_visible_icons(WmStorageScene *scene) {
    if (scene->kind != WM_STORAGE_CHANNELS ||
        wm_storage_current_medium(scene)->status != WM_STORAGE_READY)
        return;
    for (int slot = 0; slot < STORAGE_PAGE_SIZE; slot++) {
        slot_icon(scene, slot);
    }
}

static float endpoint(float frame, float frames) {
    return fminf(fmaxf(frame, 0.0f), frames - 1.0f);
}

static void add_clip(StoragePose *pose, const char *stem, const char *suffix,
                     const char *group, float frame) {
    if (!stem || !suffix || pose->count >= STORAGE_POSE_CAPACITY)
        return;
    if (group && strlen(group) >= sizeof(pose->groups[0]))
        return;
    size_t index = pose->count++;
    snprintf(pose->names[index], sizeof(pose->names[index]), "%s_%s", stem, suffix);
    if (group)
        strcpy(pose->groups[index], group);
    pose->clips[index] = (WmLayoutClip){.animation = pose->names[index],
                                        .frame = frame,
                                        .group = group ? pose->groups[index] : NULL,
                                        .loop_override = 0};
}

static void add_focus(StoragePose *pose, const char *stem, const StorageFocus *focus,
                      const char *in_suffix, const char *out_suffix,
                      const char *group) {
    if (!focus->active)
        return;
    add_clip(pose, stem, focus->entering ? in_suffix : out_suffix, group,
             endpoint(focus->frame, 7.0f));
}

static void add_tab_focus(StoragePose *pose, const char *stem,
                          const StorageFocus *focus, const char *in_suffix,
                          const char *out_suffix, const char *group) {
    if (!focus->active)
        return;
    /* Both tab clips grow from scale 1.0 to 1.1. Reverse the exit clip so
     * the focused tab shrinks back to its resting scale. */
    float frame = focus->entering ? endpoint(focus->frame, 7.0f)
                                  : endpoint(6.0f - focus->frame, 7.0f);
    add_clip(pose, stem, focus->entering ? in_suffix : out_suffix, group, frame);
}

void wm_storage_pose_base(WmStorageScene *scene) {
    StoragePose pose = {0};
    add_clip(&pose, scene->base_stem, "DataIn", "G_DataAll", scene->base_data_frame);
    add_clip(&pose, scene->base_stem, "SelectIn", "G_Select", scene->base_select_frame);
    if (scene->kind != WM_STORAGE_CHANNELS) {
        add_clip(&pose, scene->base_stem,
                 scene->kind == WM_STORAGE_GAMECUBE_SAVES ? "CubeSwitch" : "WiiSwitch",
                 "G_Switch", 1.0f);
    }
    /* Error text owns a separate sixteen-update fade. Keep it hidden while
     * Data/Tabs are entering; ERROR_IN supplies the live frame after the
     * selected medium has settled. During Back's button press it remains
     * visible until DataOut starts the shared exit. */
    bool media_error = wm_storage_current_medium(scene)->status != WM_STORAGE_READY;
    bool entering_error =
        scene->phase == WM_STORAGE_DATA_IN || scene->phase == WM_STORAGE_TABS_IN;
    add_clip(&pose, scene->base_stem,
             media_error && !entering_error ? "ErrorTxtIn" : "ErrorTxtOut",
             "G_ErrorTxt", 15.0f);
    if (scene->tab == WM_STORAGE_SD) {
        add_clip(&pose, scene->base_stem, "SelectWiiFlash", "G_Select", 21.0f);
    }
    float frame = scene->phase_frame;
    switch (scene->phase) {
        case WM_STORAGE_DATA_IN:
            add_clip(&pose, scene->base_stem, "DataIn", "G_DataAll",
                     endpoint(frame, 26));
            break;
        case WM_STORAGE_TABS_IN:
            add_clip(&pose, scene->base_stem, "SelectIn", "G_Select",
                     endpoint(frame, 16));
            break;
        case WM_STORAGE_ERROR_IN:
            add_clip(&pose, scene->base_stem, "ErrorTxtIn", "G_ErrorTxt",
                     endpoint(frame, 16));
            break;
        case WM_STORAGE_TAB_OUT:
            add_clip(&pose, scene->base_stem,
                     scene->target_tab == WM_STORAGE_WII ? "SelectSdFlash"
                                                         : "SelectWiiFlash",
                     "G_Select", endpoint(frame, 22));
            if (media_error) {
                add_clip(&pose, scene->base_stem, "ErrorTxtOut", "G_ErrorTxt",
                         endpoint(frame, 16));
            }
            break;
        case WM_STORAGE_DATA_OUT:
            add_clip(&pose, scene->base_stem, "DataOut", "G_DataAll",
                     endpoint(frame, 26));
            if (media_error) {
                add_clip(&pose, scene->base_stem, "ErrorTxtOut", "G_ErrorTxt",
                         endpoint(frame, 16));
            }
            if (scene->kind == WM_STORAGE_CHANNELS) {
                add_clip(&pose, scene->base_stem, "Lost", "G_ArwL_End",
                         endpoint(frame, 11));
                add_clip(&pose, scene->base_stem, "Lost", "G_ArwR_End",
                         endpoint(frame, 11));
            }
            break;
        default:
            break;
    }
    add_tab_focus(&pose, scene->base_stem, &scene->focus[0], "SelectWiiFoucusIn",
                  "SelectWiiFoucusOut", "G_SelectWii");
    add_tab_focus(&pose, scene->base_stem, &scene->focus[1], "SelectSdIn",
                  "SelectSdOut", "G_SelectSd");
    add_focus(&pose, scene->base_stem, &scene->focus[17], "FocusOn", "FocusOff",
              "G_ArwL_Focus");
    add_focus(&pose, scene->base_stem, &scene->focus[18], "FocusOn", "FocusOff",
              "G_ArwR_Focus");
    wm_layout_pose(scene->base, pose.clips, pose.count);

    const char *first_tab = scene->kind == WM_STORAGE_GAMECUBE_SAVES ? "Slot A" : "Wii";
    const char *second_tab =
        scene->kind == WM_STORAGE_GAMECUBE_SAVES ? "Slot B" : "SD Card";
    wm_layout_set_pose_text(scene->base, "T_SelectWii_00", first_tab);
    wm_layout_set_pose_text(scene->base, "T_SelectWii_01", first_tab);
    wm_layout_set_pose_text(scene->base, "T_SelectSd_00", second_tab);
    wm_layout_set_pose_text(scene->base, "T_SelectSd_01", second_tab);
    snprintf(scene->capacity_text, sizeof(scene->capacity_text), "Blocks Open: %u",
             wm_storage_current_medium(scene)->free_blocks);
    wm_layout_set_pose_text(scene->base, "T_Capa_00", scene->capacity_text);
    const char *error = "";
    if (wm_storage_current_medium(scene)->status == WM_STORAGE_ABSENT) {
        error = scene->kind == WM_STORAGE_GAMECUBE_SAVES
                    ? (scene->tab == WM_STORAGE_WII ? "Nothing is inserted in Slot A."
                                                    : "Nothing is inserted in Slot B.")
                    : "Nothing is inserted in the SD Card Slot.";
    } else if (wm_storage_current_medium(scene)->status == WM_STORAGE_READ_ERROR) {
        error = scene->kind == WM_STORAGE_GAMECUBE_SAVES
                    ? "The Memory Card could not be read."
                    : "An SD Card process failed.";
    } else if (wm_storage_current_medium(scene)->status == WM_STORAGE_UNSUPPORTED) {
        error = "The inserted device cannot be used.";
    }
    wm_layout_set_pose_text(scene->base, "T_Error_00", error);
    bool capacity = scene->boxes_visible &&
                    wm_storage_current_medium(scene)->status == WM_STORAGE_READY &&
                    scene->kind != WM_STORAGE_GAMECUBE_SAVES;
    wm_layout_set_pane_visible(scene->base, "N_Capa_00", capacity);
    wm_layout_set_pane_visible(scene->base, "T_Capa_00", capacity);
    size_t count = wm_storage_current_medium(scene)->count;
    bool grid = scene->view == WM_STORAGE_VIEW_GRID;
    wm_layout_set_pane_visible(scene->base, "N_ArwL", grid && scene->page > 0);
    wm_layout_set_pane_visible(scene->base, "N_ArwR",
                               grid && (scene->page + 1) * STORAGE_PAGE_SIZE < count);
}

void wm_storage_pose_box(WmStorageScene *scene, int slot) {
    StoragePose pose = {0};
    add_clip(&pose, scene->box_stem, "SaveDataIn", "G_Data", scene->box_frame);
    float frame = scene->phase_frame;
    switch (scene->phase) {
        case WM_STORAGE_TABS_IN:
            if (scene->kind == WM_STORAGE_GAMECUBE_SAVES &&
                wm_storage_current_medium(scene)->status == WM_STORAGE_READY) {
                add_clip(&pose, scene->box_stem, "SaveDataIn", "G_Data",
                         endpoint(frame, 26));
            }
            break;
        case WM_STORAGE_BOXES_IN:
            add_clip(&pose, scene->box_stem, "SaveDataIn", "G_Data",
                     endpoint(frame, 26));
            break;
        case WM_STORAGE_TAB_OUT:
        case WM_STORAGE_DATA_OUT:
            add_clip(&pose, scene->box_stem, "SaveDataOut", "G_Data",
                     endpoint(frame, 21));
            break;
        case WM_STORAGE_PAGE_OUT:
            add_clip(&pose, scene->box_stem, "SaveDataOut", "G_Data",
                     endpoint(frame * 47.0f / 20.0f, 21));
            break;
        case WM_STORAGE_PAGE_IN:
            add_clip(&pose, scene->box_stem, "SaveDataIn", "G_Data",
                     endpoint(frame * 47.0f / 20.0f, 26));
            break;
        default:
            break;
    }
    add_focus(&pose, scene->box_stem, &scene->focus[slot + 2], "SaveDataFoucusIn",
              "SaveDataFoucusOut", "G_Data");
    wm_layout_pose(scene->boxes[slot], pose.clips, pose.count);
    if (scene->kind == WM_STORAGE_CHANNELS) {
        wm_layout_set_pane_visible(scene->boxes[slot], "N_Data16x9", true);
        wm_layout_set_pane_visible(scene->boxes[slot], "N_Data4x3", false);
        wm_layout_set_pane_visible(scene->boxes[slot], "DataBaseCover_00", false);
        wm_layout_set_pane_visible(scene->boxes[slot], "DataBaseCover_01", false);
    } else {
        wm_layout_set_pane_visible(scene->boxes[slot], "N_Data_00", true);
        wm_layout_set_pane_visible(scene->boxes[slot], "DataBanner_00",
                                   wm_storage_record_at(scene, slot) != NULL);
    }
}

static const char *operation_stem(WmStorageOperation operation) {
    switch (operation) {
        case WM_STORAGE_OPERATION_MOVE:
            return "Move";
        case WM_STORAGE_OPERATION_COPY:
            return "Copy";
        case WM_STORAGE_OPERATION_ERASE:
            return "Del";
        default:
            return NULL;
    }
}

void wm_storage_pose_detail(WmStorageScene *scene) {
    StoragePose pose = {0};
    add_clip(&pose, scene->detail_stem, "SeenIn", "G_Mask", 35.0f);
    float frame = scene->phase_frame;
    const char *stem = operation_stem(scene->operation);
    switch (scene->phase) {
        case WM_STORAGE_DETAIL_IN:
            add_clip(&pose, scene->detail_stem, "SeenIn", "G_Mask",
                     endpoint(frame, 36));
            break;
        case WM_STORAGE_DETAIL_OUT:
            add_clip(&pose, scene->detail_stem, "SeenOut", "G_Mask",
                     endpoint(frame, 11));
            break;
        case WM_STORAGE_OPERATION_FLASH:
            if (stem) {
                char suffix[32];
                char group[32];
                snprintf(suffix, sizeof(suffix), "%sFlash", stem);
                snprintf(group, sizeof(group), "G_%sFlash", stem);
                add_clip(&pose, scene->detail_stem, suffix, group, endpoint(frame, 19));
            }
            break;
        case WM_STORAGE_DETAIL_BUTTONS_OUT:
            add_clip(&pose, scene->detail_stem, "SelectOut", "G_Select",
                     endpoint(frame, 46));
            break;
        case WM_STORAGE_DETAIL_BUTTONS_IN:
            add_clip(&pose, scene->detail_stem, "SelectOut", "G_Select",
                     endpoint(45.0f - frame, 46));
            break;
        case WM_STORAGE_DIALOG_IN:
        case WM_STORAGE_DIALOG_PRESS:
        case WM_STORAGE_DIALOG_OUT:
            add_clip(&pose, scene->detail_stem, "SelectOut", "G_Select", 45.0f);
            break;
        case WM_STORAGE_READY_PHASE:
            /* Layout posing starts from base panes each draw. Keep the
             * SelectOut endpoint while the dialog is idle, or the three
             * operation buttons reappear over its message. */
            if (scene->view == WM_STORAGE_VIEW_DIALOG)
                add_clip(&pose, scene->detail_stem, "SelectOut", "G_Select", 45.0f);
            break;
        default:
            break;
    }
    add_focus(&pose, scene->detail_stem, &scene->focus[20], "MoveFoucusIn",
              "MoveFoucusOut", "G_Move");
    add_focus(&pose, scene->detail_stem, &scene->focus[21], "CopyFoucusIn",
              "CopyFoucusOut", "G_Copy");
    add_focus(&pose, scene->detail_stem, &scene->focus[22], "DelFoucusIn",
              "DelFoucusOut", "G_Del");
    wm_layout_pose(scene->detail, pose.clips, pose.count);
    if (scene->kind == WM_STORAGE_CHANNELS && scene->phase == WM_STORAGE_DETAIL_IN) {
        /* Keep the window invisible while the mask starts scaling, then
         * translate it from the selected box over the next 12 frames. */
        float motion = fminf(1.0f, fmaxf(0.0f, scene->phase_frame - 2.0f) / 12.0f);
        wm_layout_set_pane_translation(scene->detail, "N_Window",
                                       scene->detail_origin_x * (1.0f - motion),
                                       scene->detail_origin_y * (1.0f - motion), 0.0f);
        if (scene->phase_frame == 0.0f)
            wm_layout_set_pane_alpha(scene->detail, "N_Window", 0.0f);
    }
    static const char *const hidden[] = {
        "N_Wait",       "T_Block_01",      "T_Block_03", "Banner_01",
        "BaseMove_off", "BaseMove_off_00", "T_Move_off", "T_Move_off_00"};
    for (size_t index = 0; index < sizeof(hidden) / sizeof(hidden[0]); index++) {
        wm_layout_set_pane_visible(scene->detail, hidden[index], false);
    }
    if (scene->kind == WM_STORAGE_CHANNELS) {
        wm_layout_set_pane_visible(scene->detail, "N_Mask4x3", false);
        wm_layout_set_pane_visible(scene->detail, "N_Mask16x9", true);
        wm_layout_set_pane_visible(scene->detail, "BlockLine", false);
        wm_layout_set_pane_visible(scene->detail, "BlockLine01", true);
        wm_layout_set_pane_visible(scene->detail, "Cover_4x3_del", false);
        wm_layout_set_pane_visible(scene->detail, "Cover_16x9_del", false);
    }
    const WmStorageRecord *record = wm_storage_record_at(scene, scene->selected_slot);
    const char *title = record ? record->title : "";
    char block_text[20];
    snprintf(block_text, sizeof(block_text), "%u", record ? record->blocks : 0);
    wm_layout_set_pose_text(scene->detail, "T_Title_00", title);
    wm_layout_set_pose_text(scene->detail, "T_Title_02", title);
    wm_layout_set_pose_text(scene->detail, "T_Title_01",
                            scene->kind == WM_STORAGE_CHANNELS ? ""
                                                               : "Local save fixture");
    wm_layout_set_pose_text(scene->detail, "T_Title_03", "");
    wm_layout_set_pose_text(scene->detail, "T_Block_00", block_text);
    wm_layout_set_pose_text(scene->detail, "T_Block_02", block_text);
    wm_layout_set_pose_text(scene->detail, "T_Move_00", "Move");
    wm_layout_set_pose_text(scene->detail, "T_Copy_00", "Copy");
    wm_layout_set_pose_text(scene->detail, "T_Del_00", "Erase");
    char prompt[160] = {0};
    if (stem) {
        snprintf(prompt, sizeof(prompt), "%s this %s?",
                 scene->operation == WM_STORAGE_OPERATION_ERASE  ? "Erase"
                 : scene->operation == WM_STORAGE_OPERATION_COPY ? "Copy"
                                                                 : "Move",
                 scene->kind == WM_STORAGE_CHANNELS ? "channel" : "save data");
    }
    wm_layout_set_pose_text(scene->detail, "T_Message_00", prompt);
}

void wm_storage_pose_dialog(WmStorageScene *scene) {
    StoragePose pose = {0};
    add_clip(&pose, "my_DialogWindow_b", "DialogIn", "G_InOut", 25.0f);
    float frame = scene->phase_frame;
    if (scene->phase == WM_STORAGE_DIALOG_IN) {
        add_clip(&pose, "my_DialogWindow_b", "DialogIn", "G_InOut",
                 endpoint(frame, 26));
    } else if (scene->phase == WM_STORAGE_DIALOG_PRESS) {
        add_clip(&pose, "my_DialogWindow_b", "SelectBtn_Ac",
                 scene->answered_yes ? "G_SelectBtnA" : "G_SelectBtnB",
                 endpoint(frame, 21));
    } else if (scene->phase == WM_STORAGE_DIALOG_OUT) {
        add_clip(&pose, "my_DialogWindow_b", "DialogOut", "G_InOut",
                 endpoint(frame, 26));
    }
    add_focus(&pose, "my_DialogWindow_b", &scene->focus[23], "FocusBtn_on",
              "FocusBtn_off", "G_FocusBtnB");
    add_focus(&pose, "my_DialogWindow_b", &scene->focus[24], "FocusBtn_on",
              "FocusBtn_off", "G_FocusBtnA");
    wm_layout_pose(scene->dialog, pose.clips, pose.count);
    wm_layout_set_pane_visible(scene->dialog, "N_Top", false);
    wm_layout_set_pose_text(scene->dialog, "T_BtnA", "Yes");
    wm_layout_set_pose_text(scene->dialog, "T_BtnB", "No");
}

void wm_storage_pose_back(WmStorageScene *scene) {
    StoragePose pose = {0};
    add_clip(&pose, "it_Button_a", "SeenIn", "G_BarIn", 15.0f);
    add_clip(&pose, "it_Button_a", "WiiLost", "G_Wii", 15.0f);
    add_clip(&pose, "it_Button_a", "AlphOut", "G_FocusBtnA", 0.0f);
    /* Fade Back as soon as the operation is pressed. The confirmation
     * window rises only after the button has finished disappearing. */
    const char *fade = NULL;
    float fade_frame = 0.0f;
    if (scene->phase == WM_STORAGE_OPERATION_FLASH) {
        fade = "AlphOut";
        fade_frame = scene->phase_frame;
    } else if (scene->phase == WM_STORAGE_DETAIL_BUTTONS_IN) {
        fade = "AlphIn";
        fade_frame = scene->phase_frame;
    } else if (scene->phase == WM_STORAGE_DETAIL_BUTTONS_OUT ||
               scene->view == WM_STORAGE_VIEW_DIALOG) {
        fade = "AlphOut";
        fade_frame = 10.0f;
    }
    if (fade) {
        add_clip(&pose, "it_Button_a", fade, "G_FocusBtnA", endpoint(fade_frame, 11));
    }
    if (scene->phase == WM_STORAGE_BACK_PRESS) {
        add_clip(&pose, "it_Button_a", "BtnFlash", "G_SelectBtnA",
                 endpoint(scene->phase_frame, 19));
    }
    add_focus(&pose, "it_Button_a", &scene->focus[19], "BtnFoucusIn", "BtnFoucusOut",
              "G_FocusBtnA");
    wm_layout_pose(scene->back, pose.clips, pose.count);
    wm_layout_set_pose_text(scene->back, "T_Button_00",
                            scene->view == WM_STORAGE_VIEW_GRID ? "Back" : "Back");
    if (scene->view == WM_STORAGE_VIEW_DIALOG && scene->kind != WM_STORAGE_CHANNELS) {
        wm_layout_set_pane_visible(scene->back, "N_Button", false);
    }
}

static bool collect_anchor(void *context, const WmLayoutPaneView *pane) {
    StorageAnchors *anchors = context;
    if (strncmp(pane->name, "N_Data_b_", 9) != 0 || strlen(pane->name) != 11)
        return true;
    char first = pane->name[9], second = pane->name[10];
    if (first < '0' || first > '9' || second < '0' || second > '9')
        return true;
    int index = (first - '0') * 10 + second - '0';
    if (index < 0 || index >= STORAGE_PAGE_SIZE)
        return true;
    static const float identity[12] = {1.0f, 0.0f, 0.0f, 0.0f, 0.0f, 1.0f,
                                       0.0f, 0.0f, 0.0f, 0.0f, 1.0f, 0.0f};
    memcpy(anchors->matrices[index], identity, sizeof(anchors->matrices[index]));
    /* Copy only anchor XY into N_All. The box layout's IPL root scale
     * projects X; using the entire parent matrix compresses the 16:9 grid. */
    anchors->matrices[index][3] = pane->matrix[3] * (832.0f / 608.0f);
    anchors->matrices[index][7] = pane->matrix[7];
    anchors->found[index] = true;
    return true;
}

StorageAnchors wm_storage_base_anchors(WmStorageScene *scene) {
    StorageAnchors anchors = {0};
    WmLayoutDrawOptions options = {.wide = true,
                                   .mode = WM_LAYOUT_IPL,
                                   .alpha = 1.0f,
                                   .on_pane = collect_anchor,
                                   .context = &anchors};
    wm_layout_draw(scene->base, &options);
    return anchors;
}

static WmLayout *slot_icon(WmStorageScene *scene, int slot) {
    const WmStorageRecord *record = wm_storage_record_at(scene, slot);
    const char *path = record ? record->icon_layout : "";
    if (strcmp(scene->icon_paths[slot], path) != 0) {
        wm_layout_destroy(scene->icons[slot]);
        scene->icons[slot] = NULL;
        snprintf(scene->icon_paths[slot], sizeof(scene->icon_paths[slot]), "%s", path);
        if (path[0]) {
            scene->icons[slot] =
                wm_layout_load_asset(scene->assets_directory, path, "Data Management");
            if (scene->icons[slot]) {
                wm_layout_prepare_materials(scene->platform, scene->icons[slot]);
            }
        }
    }
    return scene->icons[slot];
}

static void pose_slot_icon(WmLayout *icon, float age) {
    WmLayoutAnimationInfo animation;
    const char *name = NULL;
    if (wm_layout_animation_info(icon, "icon", &animation))
        name = "icon";
    else if (wm_layout_animation_info(icon, "icon_Whole", &animation)) {
        name = "icon_Whole";
    }
    if (name) {
        float frame = age;
        if (animation.loop && animation.frames > 0.0f) {
            frame = fmodf(frame, animation.frames);
        } else {
            frame = endpoint(frame, animation.frames);
        }
        WmLayoutClip clip = {.animation = name, .frame = frame, .loop_override = 0};
        wm_layout_pose(icon, &clip, 1);
    } else {
        wm_layout_pose(icon, NULL, 0);
    }
    wm_layout_mask_language_groups(icon, "ENG");
}

static bool only_channel_cover(void *context, const WmLayoutPaneView *pane) {
    (void)context;
    if (strcmp(pane->name, "DataBaseCover_01") == 0)
        return true;
    return strcmp(pane->type, "pic1") != 0 && strcmp(pane->type, "wnd1") != 0 &&
           strcmp(pane->type, "txt1") != 0;
}

static bool only_channel_arrows(void *context, const WmLayoutPaneView *pane) {
    (void)context;
    if (strcmp(pane->type, "pic1") == 0) {
        return strcmp(pane->name, "ArwL") == 0 || strcmp(pane->name, "ArwR") == 0 ||
               strcmp(pane->name, "ArwBtnL") == 0 ||
               strcmp(pane->name, "ArwBtnR") == 0 ||
               strcmp(pane->name, "ArwBtnL_Ac") == 0 ||
               strcmp(pane->name, "ArwBtnR_Ac") == 0;
    }
    return strcmp(pane->type, "txt1") != 0 && strcmp(pane->type, "wnd1") != 0;
}

static void draw_channel_icon(WmStorageScene *scene, int slot, const float anchor[12]) {
    if (scene->kind != WM_STORAGE_CHANNELS || scene->phase == WM_STORAGE_BOXES_IN ||
        scene->phase == WM_STORAGE_TAB_OUT || scene->phase == WM_STORAGE_PAGE_OUT ||
        scene->phase == WM_STORAGE_PAGE_IN || scene->phase == WM_STORAGE_DATA_OUT)
        return;
    WmLayout *icon = slot_icon(scene, slot);
    if (!icon)
        return;
    pose_slot_icon(icon, scene->age);
    WmLayoutPaneState hit_pane;
    WmLayoutPaneState data_pane;
    if (!wm_layout_pane_state(scene->boxes[slot], "N_Atari16x9", &hit_pane) ||
        !wm_layout_pane_state(scene->boxes[slot], "N_Data_01", &data_pane)) {
        return;
    }
    float icon_scale = hit_pane.scale[0] * data_pane.scale[0];
    float icon_matrix[12] = {icon_scale, 0.0f,      0.0f, anchor[3], 0.0f, icon_scale,
                             0.0f,       anchor[7], 0.0f, 0.0f,      1.0f, 0.0f};
    /* The independent thumbnail grows with box focus. Keep clipping at the
     * cell bounds so hover cannot expose art outside the rounded border. */
    const float screen_scale = (float)WM_FRAME_WIDTH / 832.0f;
    const float center_x = WM_FRAME_WIDTH * 0.5f + anchor[3] * screen_scale;
    const float center_y = WM_FRAME_HEIGHT * 0.5f - anchor[7];
    WmClipRect clip = {center_x - 51.0f * screen_scale, center_y - 28.8f,
                       102.0f * screen_scale, 57.6f};
    wm_platform_set_clip(scene->platform, &clip);
    wm_layout_present_with_fonts(scene->platform, scene->textures, scene->fonts, icon,
                                 true, WM_LAYOUT_IPL, icon_matrix);
    wm_platform_set_clip(scene->platform, NULL);
    wm_layout_set_pane_visible(scene->boxes[slot], "DataBaseCover_01", true);
    wm_layout_present_filtered_with_fonts(
        scene->platform, scene->textures, scene->fonts, scene->boxes[slot], true,
        WM_LAYOUT_IPL, anchor, only_channel_cover, NULL);
}

static bool only_detail_mask(void *context, const WmLayoutPaneView *pane) {
    (void)context;
    if (strcmp(pane->type, "pan1") == 0)
        return true;
    static const char *const names[] = {
        "BannerMask_16x9", "BannerMask_4x_01", "Cover_16x9",
        "Cover_16x9_del",  "BlockLine01",      "T_Block_02",
        "T_Block_03",      "T_Title_02",       "T_Title_03"};
    for (size_t index = 0; index < sizeof(names) / sizeof(names[0]); index++) {
        if (strcmp(pane->name, names[index]) == 0)
            return true;
    }
    return false;
}

static void draw_detail_channel_icon(WmStorageScene *scene) {
    if (scene->kind != WM_STORAGE_CHANNELS || scene->selected_slot < 0 ||
        scene->phase == WM_STORAGE_DETAIL_OUT ||
        (scene->phase == WM_STORAGE_DETAIL_IN && scene->phase_frame <= 15.0f)) {
        return;
    }
    WmLayout *icon = slot_icon(scene, scene->selected_slot);
    if (!icon)
        return;
    WmLayoutPaneState hit_pane;
    if (!wm_layout_pane_state(scene->detail, "N_Atari16x9", &hit_pane))
        return;
    pose_slot_icon(icon, scene->detail_age);
    wm_layout_set_pane_translation(icon, "RootPane", 0.0f, 0.0f, 0.0f);
    float x = hit_pane.translation[0];
    float y = hit_pane.translation[1];
    float icon_matrix[12] = {1.0f, 0.0f, 0.0f, x,    0.0f, 1.0f,
                             0.0f, y,    0.0f, 0.0f, 1.0f, 0.0f};
    float screen_scale = (float)WM_FRAME_WIDTH / 832.0f;
    WmClipRect clip = {WM_FRAME_WIDTH * 0.5f + (x - 85.0f) * screen_scale,
                       WM_FRAME_HEIGHT * 0.5f - y - 48.0f, 170.0f * screen_scale,
                       96.0f};
    wm_platform_set_clip(scene->platform, &clip);
    wm_layout_present_with_fonts(scene->platform, scene->textures, scene->fonts, icon,
                                 true, WM_LAYOUT_IPL, icon_matrix);
    wm_platform_set_clip(scene->platform, NULL);
    wm_layout_present_filtered_with_fonts(scene->platform, scene->textures,
                                          scene->fonts, scene->detail, true,
                                          WM_LAYOUT_IPL, NULL, only_detail_mask, NULL);
}

static void draw_balloon(WmStorageScene *scene, const StorageAnchors *anchors) {
    if (scene->view != WM_STORAGE_VIEW_GRID || scene->balloon_frame <= 0.0f ||
        scene->balloon_slot < 0 || scene->balloon_slot >= STORAGE_PAGE_SIZE ||
        !anchors->found[scene->balloon_slot])
        return;
    const WmStorageRecord *record = wm_storage_record_at(scene, scene->balloon_slot);
    if (!record)
        return;
    WmLayoutPaneState text_pane;
    float text_width = 0.0f;
    if (wm_layout_pane_state(scene->balloon, "T_Balloon", &text_pane)) {
        text_width =
            wm_font_cache_measure_text(scene->fonts, scene->balloon, &text_pane,
                                       record->title, strlen(record->title));
    }
    float width = fmaxf(160.0f * (832.0f / 608.0f), text_width + 40.0f);
    float x = fmaxf(-416.0f + 90.0f + width * 0.5f,
                    fminf(416.0f - 90.0f - width * 0.5f,
                          anchors->matrices[scene->balloon_slot][3]));
    float y = anchors->matrices[scene->balloon_slot][7] - 55.0f;
    float matrix[12] = {1.0f, 0.0f, 0.0f, x,    0.0f, 1.0f,
                        0.0f, y,    0.0f, 0.0f, 1.0f, 0.0f};
    WmLayoutClip clip = {.animation = "my_IplTopBalloon_a_BalloonInOut",
                         .frame = scene->balloon_frame,
                         .loop_override = 0};
    wm_layout_pose(scene->balloon, &clip, 1);
    wm_layout_set_pane_size(scene->balloon, "W_Base", width, 48.0f);
    wm_layout_set_pane_size(scene->balloon, "W_Shade", width, 48.0f);
    wm_layout_set_pose_text(scene->balloon, "T_Balloon", record->title);
    wm_layout_present_with_fonts(scene->platform, scene->textures, scene->fonts,
                                 scene->balloon, true, WM_LAYOUT_IPL, matrix);
}

bool wm_storage_scene_draw_back(WmStorageScene *scene) {
    if (!scene || scene->phase == WM_STORAGE_CLOSED)
        return false;
    wm_storage_pose_back(scene);
    wm_layout_present_with_fonts(scene->platform, scene->textures, scene->fonts,
                                 scene->back, true, WM_LAYOUT_IPL, NULL);
    return true;
}

bool wm_storage_scene_draw_content(WmStorageScene *scene) {
    if (!scene || scene->phase == WM_STORAGE_CLOSED)
        return false;
    wm_storage_pose_base(scene);
    wm_layout_present_with_fonts(scene->platform, scene->textures, scene->fonts,
                                 scene->base, true, WM_LAYOUT_IPL, NULL);
    if (scene->boxes_visible) {
        StorageAnchors anchors = wm_storage_base_anchors(scene);
        for (int slot = 0; slot < STORAGE_PAGE_SIZE; slot++) {
            if (!anchors.found[slot])
                continue;
            wm_storage_pose_box(scene, slot);
            wm_layout_present_with_fonts(scene->platform, scene->textures, scene->fonts,
                                         scene->boxes[slot], true, WM_LAYOUT_IPL,
                                         anchors.matrices[slot]);
            draw_channel_icon(scene, slot, anchors.matrices[slot]);
        }
        draw_balloon(scene, &anchors);
    }
    if (scene->kind == WM_STORAGE_CHANNELS) {
        /* ChannelEdit draws its arrow subtrees again after the channel boxes
         * and title balloon, so neither layer can cover the exit motion. */
        wm_layout_present_filtered_with_fonts(
            scene->platform, scene->textures, scene->fonts, scene->base, true,
            WM_LAYOUT_IPL, NULL, only_channel_arrows, NULL);
    }
    if (scene->view == WM_STORAGE_VIEW_DETAIL ||
        scene->view == WM_STORAGE_VIEW_DIALOG) {
        wm_storage_pose_detail(scene);
        wm_layout_present_with_fonts(scene->platform, scene->textures, scene->fonts,
                                     scene->detail, true, WM_LAYOUT_IPL, NULL);
        draw_detail_channel_icon(scene);
    }
    if (scene->view == WM_STORAGE_VIEW_DIALOG) {
        wm_storage_pose_dialog(scene);
        wm_layout_present_with_fonts(scene->platform, scene->textures, scene->fonts,
                                     scene->dialog, true, WM_LAYOUT_IPL, NULL);
    }
    return true;
}

bool wm_storage_scene_draw(WmStorageScene *scene) {
    if (!wm_storage_scene_draw_back(scene))
        return false;
    return wm_storage_scene_draw_content(scene);
}
