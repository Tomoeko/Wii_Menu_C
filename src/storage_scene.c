#include "wii_menu/storage_scene.h"

#include "wii_menu/layout_present.h"
#include "wii_menu/layout_runtime.h"
#include "wii_menu/material_prepare.h"
#include "wii_menu/source_hit.h"

#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

enum {
    STORAGE_PATH_CAPACITY = 4096,
    STORAGE_RECORD_LIMIT = 240,
    STORAGE_PAGE_SIZE = 15,
    STORAGE_POSE_CAPACITY = 18,
    STORAGE_FOCUS_COUNT = 25
};

typedef struct StorageMedium {
    WmStorageMediumStatus status;
    WmStorageRecord *records;
    size_t count;
    unsigned free_blocks;
} StorageMedium;

typedef struct StorageFocus {
    bool active;
    bool entering;
    bool requested;
    float frame;
} StorageFocus;

typedef struct StoragePose {
    WmLayoutClip clips[STORAGE_POSE_CAPACITY];
    /* Clip names and groups must remain valid until wm_layout_pose returns. */
    char names[STORAGE_POSE_CAPACITY][96];
    char groups[STORAGE_POSE_CAPACITY][32];
    size_t count;
} StoragePose;

typedef struct StorageAnchors {
    float matrices[STORAGE_PAGE_SIZE][12];
    bool found[STORAGE_PAGE_SIZE];
} StorageAnchors;

struct WmStorageScene {
    WmPlatform *platform;
    WmTextureCache *textures;
    WmFontCache *fonts;
    WmLayout *base;
    WmLayout *boxes[STORAGE_PAGE_SIZE];
    WmLayout *detail;
    WmLayout *dialog;
    WmLayout *back;
    WmLayout *balloon;
    WmLayout *icons[STORAGE_PAGE_SIZE];
    char icon_paths[STORAGE_PAGE_SIZE][256];
    char assets_directory[STORAGE_PATH_CAPACITY];
    char base_stem[40];
    char box_stem[40];
    char detail_stem[48];
    WmStorageKind kind;
    StorageMedium media[2];
    WmStorageTab tab;
    WmStorageTab target_tab;
    WmStorageView view;
    WmStoragePhase phase;
    WmStorageAction action;
    WmStorageOperation operation;
    WmStorageHit hover;
    StorageFocus focus[STORAGE_FOCUS_COUNT];
    float phase_frame;
    float age;
    float detail_age;
    float base_data_frame;
    float base_select_frame;
    float box_frame;
    size_t page;
    int page_direction;
    int selected_slot;
    float detail_origin_x;
    float detail_origin_y;
    bool answered_yes;
    bool boxes_visible;
    float balloon_wait;
    float balloon_frame;
    int balloon_slot;
    bool balloon_cue;
    char capacity_text[96];
};

static const StorageMedium *current_medium(const WmStorageScene *scene);
static WmLayout *slot_icon(WmStorageScene *scene, int slot);
static StorageAnchors base_anchors(WmStorageScene *scene);

static void prepare_visible_icons(WmStorageScene *scene) {
    if (scene->kind != WM_STORAGE_CHANNELS ||
        current_medium(scene)->status != WM_STORAGE_READY) return;
    for (int slot = 0; slot < STORAGE_PAGE_SIZE; slot++) {
        slot_icon(scene, slot);
    }
}

static int hex_digit(char value) {
    if (value >= '0' && value <= '9') return value - '0';
    if (value >= 'a' && value <= 'f') return value - 'a' + 10;
    if (value >= 'A' && value <= 'F') return value - 'A' + 10;
    return -1;
}

bool wm_storage_manageable_channel(const char *id, bool has_icon) {
    if (!has_icon || !id || !id[0] || strcmp(id, "disc") == 0) return false;
    if (strlen(id) != 16) return true;
    uint32_t high = 0;
    for (int index = 0; index < 16; index++) {
        int digit = hex_digit(id[index]);
        if (digit < 0) return true;
        if (index < 8) high = (high << 4) | (uint32_t)digit;
    }
    if (high < 0x10000u || high > 0x10007u ||
        !(0xd3u & (1u << (high - 0x10000u)))) return false;
    unsigned first = ((unsigned)hex_digit(id[8]) << 4) |
                     (unsigned)hex_digit(id[9]);
    return (first >= 0x41u && first <= 0x5au) ||
           (first >= 0x30u && first <= 0x39u) ||
           first < 0x20u || first > 0x7eu;
}

static bool native_title_id(const char *id) {
    if (strlen(id) != 16) return false;
    for (size_t index = 0; index < 16; index++) {
        if (hex_digit(id[index]) < 0) return false;
    }
    return true;
}

static int channel_record_order(const WmStorageRecord *first,
                                const WmStorageRecord *second) {
    bool first_native = native_title_id(first->id);
    bool second_native = native_title_id(second->id);
    if (first_native != second_native) return first_native ? -1 : 1;
    if (!first_native) return 0;
    for (size_t index = 0; index < 16; index++) {
        int difference = hex_digit(first->id[index]) -
                         hex_digit(second->id[index]);
        if (difference) return difference;
    }
    return 0;
}

static void sort_wii_channels(WmStorageRecord *records, size_t count) {
    /* Native ES enumeration sorts title IDs. An insertion sort retains the
     * input order of authored local records, as the HTML stable sort does. */
    for (size_t index = 1; index < count; index++) {
        WmStorageRecord record = records[index];
        size_t position = index;
        while (position > 0 &&
               channel_record_order(&records[position - 1], &record) > 0) {
            records[position] = records[position - 1];
            position--;
        }
        records[position] = record;
    }
}

static WmLayout *load_layout(const char *directory, const char *relative) {
    char path[STORAGE_PATH_CAPACITY];
    int length = snprintf(path, sizeof(path), "%s/%s", directory, relative);
    if (length < 0 || length >= (int)sizeof(path)) return NULL;
    char error[160] = {0};
    WmLayout *layout = wm_layout_load_json(path, error, sizeof(error));
    if (!layout) {
        fprintf(stderr, "Could not load Data Management layout %s: %s\n",
                relative, error);
    }
    return layout;
}

static bool record_valid(const WmStorageRecord *record) {
    if (!record || !record->id[0] || !record->title[0] ||
        !memchr(record->id, 0, sizeof(record->id)) ||
        !memchr(record->title, 0, sizeof(record->title)) ||
        !memchr(record->icon_layout, 0, sizeof(record->icon_layout))) {
        return false;
    }
    const char *path = record->icon_layout;
    if (path[0] == '/' || strstr(path, "..") || strchr(path, '\\')) return false;
    if (strcmp(record->id, "disc") == 0 ||
        strcmp(record->id, "__proto__") == 0 ||
        strcmp(record->id, "constructor") == 0 ||
        strcmp(record->id, "prototype") == 0) return false;
    for (const char *at = record->id; *at; at++) {
        char value = *at;
        if (!((value >= 'A' && value <= 'Z') ||
              (value >= 'a' && value <= 'z') ||
              (value >= '0' && value <= '9') ||
              value == '_' || value == '-')) return false;
    }
    return true;
}

static const StorageMedium *current_medium(const WmStorageScene *scene) {
    return &scene->media[scene->tab];
}

static const WmStorageRecord *record_at(const WmStorageScene *scene,
                                         int slot) {
    const StorageMedium *medium = current_medium(scene);
    if (medium->status != WM_STORAGE_READY || slot < 0 ||
        slot >= STORAGE_PAGE_SIZE) return NULL;
    size_t index = scene->page * STORAGE_PAGE_SIZE + (size_t)slot;
    return index < medium->count ? &medium->records[index] : NULL;
}

static float duration(WmStoragePhase phase) {
    switch (phase) {
        case WM_STORAGE_DATA_IN:
        case WM_STORAGE_BOXES_IN:
        case WM_STORAGE_DIALOG_IN:
        case WM_STORAGE_DIALOG_OUT:
        case WM_STORAGE_DATA_OUT: return 26.0f;
        case WM_STORAGE_TABS_IN: return 16.0f;
        case WM_STORAGE_ERROR_IN: return 16.0f;
        case WM_STORAGE_TAB_OUT: return 22.0f;
        case WM_STORAGE_PAGE_OUT: return 21.0f * 20.0f / 47.0f;
        case WM_STORAGE_PAGE_IN: return 26.0f * 20.0f / 47.0f;
        case WM_STORAGE_DETAIL_OUT: return 11.0f;
        case WM_STORAGE_DETAIL_IN: return 36.0f;
        case WM_STORAGE_BACK_PRESS:
        case WM_STORAGE_OPERATION_FLASH: return 19.0f;
        case WM_STORAGE_DETAIL_BUTTONS_OUT:
        case WM_STORAGE_DETAIL_BUTTONS_IN: return 46.0f;
        case WM_STORAGE_DIALOG_PRESS: return 21.0f;
        default: return 0.0f;
    }
}

static float scene_duration(const WmStorageScene *scene) {
    if (scene->phase == WM_STORAGE_TABS_IN &&
        scene->kind == WM_STORAGE_GAMECUBE_SAVES &&
        current_medium(scene)->status == WM_STORAGE_READY) return 26.0f;
    return duration(scene->phase);
}

static float endpoint(float frame, float frames) {
    return fminf(fmaxf(frame, 0.0f), frames - 1.0f);
}

static void phase_start(WmStorageScene *scene, WmStoragePhase phase) {
    scene->phase = phase;
    scene->phase_frame = 0.0f;
    scene->hover = (WmStorageHit){WM_STORAGE_CONTROL_NONE, -1};
    memset(scene->focus, 0, sizeof(scene->focus));
}

static void add_clip(StoragePose *pose, const char *stem, const char *suffix,
                     const char *group, float frame) {
    if (!stem || !suffix || pose->count >= STORAGE_POSE_CAPACITY) return;
    if (group && strlen(group) >= sizeof(pose->groups[0])) return;
    size_t index = pose->count++;
    snprintf(pose->names[index], sizeof(pose->names[index]),
             "%s_%s", stem, suffix);
    if (group) strcpy(pose->groups[index], group);
    pose->clips[index] = (WmLayoutClip){
        .animation = pose->names[index],
        .frame = frame,
        .group = group ? pose->groups[index] : NULL,
        .loop_override = 0
    };
}

static int focus_index(WmStorageHit hit) {
    switch (hit.control) {
        case WM_STORAGE_CONTROL_WII_TAB: return 0;
        case WM_STORAGE_CONTROL_SD_TAB: return 1;
        case WM_STORAGE_CONTROL_SLOT:
            return hit.slot >= 0 && hit.slot < STORAGE_PAGE_SIZE
                ? hit.slot + 2 : -1;
        case WM_STORAGE_CONTROL_PREVIOUS: return 17;
        case WM_STORAGE_CONTROL_NEXT: return 18;
        case WM_STORAGE_CONTROL_BACK: return 19;
        case WM_STORAGE_CONTROL_MOVE: return 20;
        case WM_STORAGE_CONTROL_COPY: return 21;
        case WM_STORAGE_CONTROL_ERASE: return 22;
        case WM_STORAGE_CONTROL_NO: return 23;
        case WM_STORAGE_CONTROL_YES: return 24;
        default: return -1;
    }
}

static void add_focus(StoragePose *pose, const char *stem,
                      const StorageFocus *focus, const char *in_suffix,
                      const char *out_suffix, const char *group) {
    if (!focus->active) return;
    add_clip(pose, stem, focus->entering ? in_suffix : out_suffix,
             group, endpoint(focus->frame, 7.0f));
}

static void add_tab_focus(StoragePose *pose, const char *stem,
                          const StorageFocus *focus, const char *in_suffix,
                          const char *out_suffix, const char *group) {
    if (!focus->active) return;
    /* Both authored tab clips grow from scale 1.0 to 1.1. The native Out
     * controller plays its resource backwards, as the HTML port records. */
    float frame = focus->entering
        ? endpoint(focus->frame, 7.0f)
        : endpoint(6.0f - focus->frame, 7.0f);
    add_clip(pose, stem, focus->entering ? in_suffix : out_suffix,
             group, frame);
}

static void pose_base(WmStorageScene *scene) {
    StoragePose pose = {0};
    add_clip(&pose, scene->base_stem, "DataIn", "G_DataAll",
             scene->base_data_frame);
    add_clip(&pose, scene->base_stem, "SelectIn", "G_Select",
             scene->base_select_frame);
    if (scene->kind != WM_STORAGE_CHANNELS) {
        add_clip(&pose, scene->base_stem,
                 scene->kind == WM_STORAGE_GAMECUBE_SAVES
                     ? "CubeSwitch" : "WiiSwitch", "G_Switch", 1.0f);
    }
    /* Error text owns a separate sixteen-update fade. Keep it hidden while
     * Data/Tabs are entering; ERROR_IN supplies the live frame after the
     * selected medium has settled. During Back's button press it remains
     * visible until DataOut starts the shared exit. */
    bool media_error = current_medium(scene)->status != WM_STORAGE_READY;
    bool entering_error = scene->phase == WM_STORAGE_DATA_IN ||
                          scene->phase == WM_STORAGE_TABS_IN;
    add_clip(&pose, scene->base_stem,
             media_error && !entering_error ? "ErrorTxtIn" : "ErrorTxtOut",
             "G_ErrorTxt", 15.0f);
    if (scene->tab == WM_STORAGE_SD) {
        add_clip(&pose, scene->base_stem, "SelectWiiFlash",
                 "G_Select", 21.0f);
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
                     scene->target_tab == WM_STORAGE_WII
                         ? "SelectSdFlash" : "SelectWiiFlash",
                     "G_Select", endpoint(frame, 22));
            if (media_error) {
                add_clip(&pose, scene->base_stem, "ErrorTxtOut",
                         "G_ErrorTxt", endpoint(frame, 16));
            }
            break;
        case WM_STORAGE_DATA_OUT:
            add_clip(&pose, scene->base_stem, "DataOut", "G_DataAll",
                     endpoint(frame, 26));
            if (media_error) {
                add_clip(&pose, scene->base_stem, "ErrorTxtOut",
                         "G_ErrorTxt", endpoint(frame, 16));
            }
            if (scene->kind == WM_STORAGE_CHANNELS) {
                add_clip(&pose, scene->base_stem, "Lost", "G_ArwL_End",
                         endpoint(frame, 11));
                add_clip(&pose, scene->base_stem, "Lost", "G_ArwR_End",
                         endpoint(frame, 11));
            }
            break;
        default: break;
    }
    add_tab_focus(&pose, scene->base_stem, &scene->focus[0],
                   "SelectWiiFoucusIn", "SelectWiiFoucusOut", "G_SelectWii");
    add_tab_focus(&pose, scene->base_stem, &scene->focus[1],
                   "SelectSdIn", "SelectSdOut", "G_SelectSd");
    add_focus(&pose, scene->base_stem, &scene->focus[17],
              "FocusOn", "FocusOff", "G_ArwL_Focus");
    add_focus(&pose, scene->base_stem, &scene->focus[18],
              "FocusOn", "FocusOff", "G_ArwR_Focus");
    wm_layout_pose(scene->base, pose.clips, pose.count);

    const char *first_tab = scene->kind == WM_STORAGE_GAMECUBE_SAVES
        ? "Slot A" : "Wii";
    const char *second_tab = scene->kind == WM_STORAGE_GAMECUBE_SAVES
        ? "Slot B" : "SD Card";
    wm_layout_set_pose_text(scene->base, "T_SelectWii_00", first_tab);
    wm_layout_set_pose_text(scene->base, "T_SelectWii_01", first_tab);
    wm_layout_set_pose_text(scene->base, "T_SelectSd_00", second_tab);
    wm_layout_set_pose_text(scene->base, "T_SelectSd_01", second_tab);
    snprintf(scene->capacity_text, sizeof(scene->capacity_text),
             "Blocks Open: %u", current_medium(scene)->free_blocks);
    wm_layout_set_pose_text(scene->base, "T_Capa_00", scene->capacity_text);
    const char *error = "";
    if (current_medium(scene)->status == WM_STORAGE_ABSENT) {
        error = scene->kind == WM_STORAGE_GAMECUBE_SAVES
            ? (scene->tab == WM_STORAGE_WII
                   ? "Nothing is inserted in Slot A."
                   : "Nothing is inserted in Slot B.")
            : "Nothing is inserted in the SD Card Slot.";
    } else if (current_medium(scene)->status == WM_STORAGE_READ_ERROR) {
        error = scene->kind == WM_STORAGE_GAMECUBE_SAVES
            ? "The Memory Card could not be read." : "An SD Card process failed.";
    } else if (current_medium(scene)->status == WM_STORAGE_UNSUPPORTED) {
        error = "The inserted device cannot be used.";
    }
    wm_layout_set_pose_text(scene->base, "T_Error_00", error);
    bool capacity = scene->boxes_visible &&
                    current_medium(scene)->status == WM_STORAGE_READY &&
                    scene->kind != WM_STORAGE_GAMECUBE_SAVES;
    wm_layout_set_pane_visible(scene->base, "N_Capa_00", capacity);
    wm_layout_set_pane_visible(scene->base, "T_Capa_00", capacity);
    size_t count = current_medium(scene)->count;
    bool grid = scene->view == WM_STORAGE_VIEW_GRID;
    wm_layout_set_pane_visible(scene->base, "N_ArwL", grid && scene->page > 0);
    wm_layout_set_pane_visible(scene->base, "N_ArwR",
                                grid && (scene->page + 1) * STORAGE_PAGE_SIZE < count);
}

static void pose_box(WmStorageScene *scene, int slot) {
    StoragePose pose = {0};
    add_clip(&pose, scene->box_stem, "SaveDataIn", "G_Data",
             scene->box_frame);
    float frame = scene->phase_frame;
    switch (scene->phase) {
        case WM_STORAGE_TABS_IN:
            if (scene->kind == WM_STORAGE_GAMECUBE_SAVES &&
                current_medium(scene)->status == WM_STORAGE_READY) {
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
        default: break;
    }
    add_focus(&pose, scene->box_stem, &scene->focus[slot + 2],
              "SaveDataFoucusIn", "SaveDataFoucusOut", "G_Data");
    wm_layout_pose(scene->boxes[slot], pose.clips, pose.count);
    if (scene->kind == WM_STORAGE_CHANNELS) {
        wm_layout_set_pane_visible(scene->boxes[slot], "N_Data16x9", true);
        wm_layout_set_pane_visible(scene->boxes[slot], "N_Data4x3", false);
        wm_layout_set_pane_visible(scene->boxes[slot], "DataBaseCover_00", false);
        wm_layout_set_pane_visible(scene->boxes[slot], "DataBaseCover_01", false);
    } else {
        wm_layout_set_pane_visible(scene->boxes[slot], "N_Data_00", true);
        wm_layout_set_pane_visible(scene->boxes[slot], "DataBanner_00",
                                    record_at(scene, slot) != NULL);
    }
}

static const char *operation_stem(WmStorageOperation operation) {
    switch (operation) {
        case WM_STORAGE_OPERATION_MOVE: return "Move";
        case WM_STORAGE_OPERATION_COPY: return "Copy";
        case WM_STORAGE_OPERATION_ERASE: return "Del";
        default: return NULL;
    }
}

static void pose_detail(WmStorageScene *scene) {
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
                add_clip(&pose, scene->detail_stem, suffix, group,
                         endpoint(frame, 19));
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
                add_clip(&pose, scene->detail_stem, "SelectOut",
                         "G_Select", 45.0f);
            break;
        default: break;
    }
    add_focus(&pose, scene->detail_stem, &scene->focus[20],
              "MoveFoucusIn", "MoveFoucusOut", "G_Move");
    add_focus(&pose, scene->detail_stem, &scene->focus[21],
              "CopyFoucusIn", "CopyFoucusOut", "G_Copy");
    add_focus(&pose, scene->detail_stem, &scene->focus[22],
              "DelFoucusIn", "DelFoucusOut", "G_Del");
    wm_layout_pose(scene->detail, pose.clips, pose.count);
    if (scene->kind == WM_STORAGE_CHANNELS &&
        scene->phase == WM_STORAGE_DETAIL_IN) {
        /* ChanAppEdit moves its window from the selected box after the mask
         * scale starts. The source's first layout calculation still draws the
         * previous invisible window, then translation catches up by update 14. */
        float motion = fminf(1.0f,
                             fmaxf(0.0f, scene->phase_frame - 2.0f) / 12.0f);
        wm_layout_set_pane_translation(
            scene->detail, "N_Window",
            scene->detail_origin_x * (1.0f - motion),
            scene->detail_origin_y * (1.0f - motion), 0.0f);
        if (scene->phase_frame == 0.0f)
            wm_layout_set_pane_alpha(scene->detail, "N_Window", 0.0f);
    }
    static const char *const hidden[] = {
        "N_Wait", "T_Block_01", "T_Block_03", "Banner_01",
        "BaseMove_off", "BaseMove_off_00", "T_Move_off", "T_Move_off_00"
    };
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
    const WmStorageRecord *record = record_at(scene, scene->selected_slot);
    const char *title = record ? record->title : "";
    char block_text[20];
    snprintf(block_text, sizeof(block_text), "%u", record ? record->blocks : 0);
    wm_layout_set_pose_text(scene->detail, "T_Title_00", title);
    wm_layout_set_pose_text(scene->detail, "T_Title_02", title);
    wm_layout_set_pose_text(scene->detail, "T_Title_01",
                             scene->kind == WM_STORAGE_CHANNELS ? "" : "Local save fixture");
    wm_layout_set_pose_text(scene->detail, "T_Title_03", "");
    wm_layout_set_pose_text(scene->detail, "T_Block_00", block_text);
    wm_layout_set_pose_text(scene->detail, "T_Block_02", block_text);
    wm_layout_set_pose_text(scene->detail, "T_Move_00", "Move");
    wm_layout_set_pose_text(scene->detail, "T_Copy_00", "Copy");
    wm_layout_set_pose_text(scene->detail, "T_Del_00", "Erase");
    char prompt[160] = {0};
    if (stem) {
        snprintf(prompt, sizeof(prompt), "%s this %s?",
                 scene->operation == WM_STORAGE_OPERATION_ERASE ? "Erase" :
                 scene->operation == WM_STORAGE_OPERATION_COPY ? "Copy" : "Move",
                 scene->kind == WM_STORAGE_CHANNELS ? "channel" : "save data");
    }
    wm_layout_set_pose_text(scene->detail, "T_Message_00", prompt);
}

static void pose_dialog(WmStorageScene *scene) {
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
    add_focus(&pose, "my_DialogWindow_b", &scene->focus[23],
              "FocusBtn_on", "FocusBtn_off", "G_FocusBtnB");
    add_focus(&pose, "my_DialogWindow_b", &scene->focus[24],
              "FocusBtn_on", "FocusBtn_off", "G_FocusBtnA");
    wm_layout_pose(scene->dialog, pose.clips, pose.count);
    wm_layout_set_pane_visible(scene->dialog, "N_Top", false);
    wm_layout_set_pose_text(scene->dialog, "T_BtnA", "Yes");
    wm_layout_set_pose_text(scene->dialog, "T_BtnB", "No");
}

static void pose_back(WmStorageScene *scene) {
    StoragePose pose = {0};
    add_clip(&pose, "it_Button_a", "SeenIn", "G_BarIn", 15.0f);
    add_clip(&pose, "it_Button_a", "WiiLost", "G_Wii", 15.0f);
    add_clip(&pose, "it_Button_a", "AlphOut", "G_FocusBtnA", 0.0f);
    if (scene->kind == WM_STORAGE_CHANNELS) {
        /* Channels keeps the independent Back button alive while its dialog
         * enters and exits. The authored alpha clips run alongside those
         * 26- and 46-frame transitions, then hold their endpoint. */
        const char *fade = NULL;
        float fade_frame = 0.0f;
        if (scene->phase == WM_STORAGE_DIALOG_IN) {
            fade = "AlphOut";
            fade_frame = scene->phase_frame;
        } else if (scene->phase == WM_STORAGE_DETAIL_BUTTONS_IN) {
            fade = "AlphIn";
            fade_frame = scene->phase_frame;
        } else if (scene->view == WM_STORAGE_VIEW_DIALOG) {
            fade = "AlphOut";
            fade_frame = 10.0f;
        }
        if (fade) {
            add_clip(&pose, "it_Button_a", fade, "G_FocusBtnA",
                     endpoint(fade_frame, 11));
        }
    }
    if (scene->phase == WM_STORAGE_BACK_PRESS) {
        add_clip(&pose, "it_Button_a", "BtnFlash", "G_SelectBtnA",
                 endpoint(scene->phase_frame, 19));
    }
    add_focus(&pose, "it_Button_a", &scene->focus[19],
              "BtnFoucusIn", "BtnFoucusOut", "G_FocusBtnA");
    wm_layout_pose(scene->back, pose.clips, pose.count);
    wm_layout_set_pose_text(scene->back, "T_Button_00",
                             scene->view == WM_STORAGE_VIEW_GRID ? "Back" : "Back");
    if (scene->view == WM_STORAGE_VIEW_DIALOG &&
        scene->kind != WM_STORAGE_CHANNELS) {
        wm_layout_set_pane_visible(scene->back, "N_Button", false);
    }
}

WmStorageScene *wm_storage_scene_create(WmPlatform *platform,
                                         const char *assets_directory,
                                         WmTextureCache *textures,
                                         WmFontCache *fonts,
                                         WmStorageKind kind) {
    if (!platform || !assets_directory || !assets_directory[0] ||
        !textures || !fonts || kind > WM_STORAGE_GAMECUBE_SAVES) return NULL;
    const char *base_path = kind == WM_STORAGE_CHANNELS
        ? "layouts/chanEdit/it_ObjChannelEdit_a.json"
        : kind == WM_STORAGE_GAMECUBE_SAVES
            ? "layouts/gcMem/it_ObjCubeEdit_a.json"
            : "layouts/wiiMem/it_ObjCubeEdit_a.json";
    const char *box_path = kind == WM_STORAGE_CHANNELS
        ? "layouts/chanEdit/it_ObjChannelEdit_b.json"
        : kind == WM_STORAGE_GAMECUBE_SAVES
            ? "layouts/gcMem/it_ObjCubeEdit_b.json"
            : "layouts/wiiMem/it_ObjDataEdit_b.json";
    const char *detail_path = kind == WM_STORAGE_CHANNELS
        ? "layouts/chanEdit/mn_ChannelDetail_a.json"
        : "layouts/wiiMem/081210_sys4_mn_DataDetail_a.json";
    WmStorageScene *scene = calloc(1, sizeof(*scene));
    if (!scene) return NULL;
    scene->platform = platform;
    scene->textures = textures;
    scene->fonts = fonts;
    scene->kind = kind;
    if (strlen(assets_directory) >= sizeof(scene->assets_directory)) {
        wm_storage_scene_destroy(scene);
        return NULL;
    }
    strcpy(scene->assets_directory, assets_directory);
    snprintf(scene->base_stem, sizeof(scene->base_stem), "%s",
             kind == WM_STORAGE_CHANNELS
                 ? "it_ObjChannelEdit_a" : "it_ObjCubeEdit_a");
    /* Wii Memory binds CubeEdit BRLAN clips to the DataEdit BRLYT. The
     * export also contains DataEdit clips, but its focus keys differ. */
    snprintf(scene->box_stem, sizeof(scene->box_stem), "%s",
             kind == WM_STORAGE_CHANNELS
                 ? "it_ObjChannelEdit_b" : "it_ObjCubeEdit_b");
    snprintf(scene->detail_stem, sizeof(scene->detail_stem), "%s",
             kind == WM_STORAGE_CHANNELS
                 ? "mn_ChannelDetail_a" : "081210_sys4_mn_DataDetail_a");
    scene->base = load_layout(assets_directory, base_path);
    for (int index = 0; index < STORAGE_PAGE_SIZE; index++) {
        scene->boxes[index] = load_layout(assets_directory, box_path);
    }
    scene->detail = load_layout(assets_directory, detail_path);
    scene->dialog = load_layout(assets_directory,
                                 "layouts/dlgWdw/my_DialogWindow_b.json");
    scene->back = load_layout(assets_directory,
                               "layouts/setupBtn/it_Button_a.json");
    scene->balloon = load_layout(assets_directory,
                                  "layouts/balloon/my_IplTopBalloon_a.json");
    if (!scene->base || !scene->detail || !scene->dialog || !scene->back ||
        !scene->balloon) {
        wm_storage_scene_destroy(scene);
        return NULL;
    }
    for (int index = 0; index < STORAGE_PAGE_SIZE; index++) {
        if (!scene->boxes[index]) {
            wm_storage_scene_destroy(scene);
            return NULL;
        }
    }
    wm_layout_prepare_materials(platform, scene->base);
    for (int index = 0; index < STORAGE_PAGE_SIZE; index++) {
        wm_layout_prepare_materials(platform, scene->boxes[index]);
    }
    wm_layout_prepare_materials(platform, scene->detail);
    wm_layout_prepare_materials(platform, scene->dialog);
    wm_layout_prepare_materials(platform, scene->back);
    wm_layout_prepare_materials(platform, scene->balloon);
    scene->media[WM_STORAGE_WII].status = WM_STORAGE_READY;
    scene->media[WM_STORAGE_SD].status =
        kind == WM_STORAGE_GAMECUBE_SAVES ? WM_STORAGE_ABSENT : WM_STORAGE_READY;
    scene->media[WM_STORAGE_WII].free_blocks = 905;
    scene->media[WM_STORAGE_SD].free_blocks = 905;
    if (kind == WM_STORAGE_WII_SAVES) {
        WmStorageRecord sample = {
            .id = "dummy-save", .title = "Dummy Save", .blocks = 1
        };
        if (!wm_storage_scene_set_medium(scene, WM_STORAGE_WII,
                                          WM_STORAGE_READY, &sample, 1, 905)) {
            wm_storage_scene_destroy(scene);
            return NULL;
        }
    }
    scene->phase = WM_STORAGE_CLOSED;
    scene->selected_slot = -1;
    return scene;
}

void wm_storage_scene_destroy(WmStorageScene *scene) {
    if (!scene) return;
    for (int tab = 0; tab < 2; tab++) free(scene->media[tab].records);
    for (int index = 0; index < STORAGE_PAGE_SIZE; index++) {
        wm_layout_destroy(scene->icons[index]);
    }
    wm_layout_destroy(scene->back);
    wm_layout_destroy(scene->balloon);
    wm_layout_destroy(scene->dialog);
    wm_layout_destroy(scene->detail);
    for (int index = 0; index < STORAGE_PAGE_SIZE; index++) {
        wm_layout_destroy(scene->boxes[index]);
    }
    wm_layout_destroy(scene->base);
    free(scene);
}

bool wm_storage_scene_set_medium(WmStorageScene *scene, WmStorageTab tab,
                                  WmStorageMediumStatus status,
                                  const WmStorageRecord *records,
                                  size_t count, unsigned free_blocks) {
    if (!scene || tab > WM_STORAGE_SD || status > WM_STORAGE_UNSUPPORTED ||
        count > STORAGE_RECORD_LIMIT || (count && !records) ||
        free_blocks > (tab == WM_STORAGE_WII ? 9999u : 999999u)) return false;
    WmStorageRecord *copy = NULL;
    size_t included = 0;
    if (count) {
        copy = malloc(count * sizeof(*copy));
        if (!copy) return false;
        for (size_t index = 0; index < count; index++) {
            if (!memchr(records[index].id, 0, sizeof(records[index].id)) ||
                !memchr(records[index].icon_layout, 0,
                        sizeof(records[index].icon_layout))) {
                free(copy);
                return false;
            }
            if (scene->kind == WM_STORAGE_CHANNELS &&
                !wm_storage_manageable_channel(records[index].id,
                                               records[index].icon_layout[0] != 0)) {
                continue;
            }
            if (!record_valid(&records[index])) {
                free(copy);
                return false;
            }
            for (size_t earlier = 0; earlier < included; earlier++) {
                if (strcmp(records[index].id, copy[earlier].id) == 0) {
                    free(copy);
                    return false;
                }
            }
            copy[included++] = records[index];
        }
    }
    if (scene->kind == WM_STORAGE_CHANNELS && tab == WM_STORAGE_WII) {
        sort_wii_channels(copy, included);
    }
    if (!included) {
        free(copy);
        copy = NULL;
    }
    free(scene->media[tab].records);
    scene->media[tab] = (StorageMedium){status, copy, included, free_blocks};
    if (scene->tab == tab &&
        scene->page * STORAGE_PAGE_SIZE >= included) scene->page = 0;
    if (scene->tab == tab && scene->phase != WM_STORAGE_CLOSED) {
        prepare_visible_icons(scene);
    }
    return true;
}

bool wm_storage_scene_open(WmStorageScene *scene, WmStorageTab initial_tab) {
    if (!scene || initial_tab > WM_STORAGE_SD) return false;
    scene->tab = initial_tab;
    scene->target_tab = initial_tab;
    scene->view = WM_STORAGE_VIEW_GRID;
    scene->page = 0;
    scene->age = 0;
    scene->detail_age = 0;
    scene->action = WM_STORAGE_ACTION_NONE;
    scene->operation = WM_STORAGE_OPERATION_NONE;
    scene->selected_slot = -1;
    scene->boxes_visible = false;
    scene->balloon_wait = 0.0f;
    scene->balloon_frame = 0.0f;
    scene->balloon_slot = -1;
    scene->balloon_cue = false;
    scene->base_data_frame = 0.0f;
    scene->base_select_frame = 0.0f;
    scene->box_frame = 0.0f;
    phase_start(scene, WM_STORAGE_DATA_IN);
    prepare_visible_icons(scene);
    return true;
}

static void phase_finish(WmStorageScene *scene) {
    switch (scene->phase) {
        case WM_STORAGE_DATA_IN:
            scene->base_data_frame = 25.0f;
            if (scene->kind == WM_STORAGE_GAMECUBE_SAVES &&
                current_medium(scene)->status == WM_STORAGE_READY) {
                scene->boxes_visible = true;
            }
            phase_start(scene, WM_STORAGE_TABS_IN);
            break;
        case WM_STORAGE_TABS_IN:
            scene->base_select_frame = 15.0f;
            if (scene->kind == WM_STORAGE_GAMECUBE_SAVES &&
                current_medium(scene)->status == WM_STORAGE_READY) {
                scene->box_frame = 25.0f;
                phase_start(scene, WM_STORAGE_READY_PHASE);
            } else if (current_medium(scene)->status == WM_STORAGE_READY) {
                scene->boxes_visible = true;
                phase_start(scene, WM_STORAGE_BOXES_IN);
            } else phase_start(scene, WM_STORAGE_ERROR_IN);
            break;
        case WM_STORAGE_ERROR_IN:
            phase_start(scene, WM_STORAGE_READY_PHASE);
            break;
        case WM_STORAGE_BOXES_IN:
            scene->box_frame = 25.0f;
            phase_start(scene, WM_STORAGE_READY_PHASE);
            break;
        case WM_STORAGE_PAGE_IN:
        case WM_STORAGE_DETAIL_IN:
        case WM_STORAGE_DETAIL_BUTTONS_IN:
            phase_start(scene, WM_STORAGE_READY_PHASE);
            break;
        case WM_STORAGE_TAB_OUT:
            scene->tab = scene->target_tab;
            scene->page = 0;
            prepare_visible_icons(scene);
            scene->boxes_visible = current_medium(scene)->status == WM_STORAGE_READY;
            phase_start(scene, scene->boxes_visible
                        ? WM_STORAGE_BOXES_IN : WM_STORAGE_ERROR_IN);
            break;
        case WM_STORAGE_PAGE_OUT:
            if (scene->page_direction > 0) scene->page++;
            else scene->page--;
            prepare_visible_icons(scene);
            phase_start(scene, WM_STORAGE_PAGE_IN);
            break;
        case WM_STORAGE_BACK_PRESS:
            phase_start(scene, scene->view == WM_STORAGE_VIEW_GRID
                        ? WM_STORAGE_DATA_OUT : WM_STORAGE_DETAIL_OUT);
            break;
        case WM_STORAGE_DETAIL_OUT:
            scene->view = WM_STORAGE_VIEW_GRID;
            scene->selected_slot = -1;
            scene->operation = WM_STORAGE_OPERATION_NONE;
            phase_start(scene, WM_STORAGE_READY_PHASE);
            break;
        case WM_STORAGE_DATA_OUT:
            phase_start(scene, WM_STORAGE_CLOSED);
            scene->action = WM_STORAGE_ACTION_EXITED;
            break;
        case WM_STORAGE_OPERATION_FLASH:
            phase_start(scene, WM_STORAGE_DETAIL_BUTTONS_OUT);
            break;
        case WM_STORAGE_DETAIL_BUTTONS_OUT:
            scene->view = WM_STORAGE_VIEW_DIALOG;
            phase_start(scene, WM_STORAGE_DIALOG_IN);
            break;
        case WM_STORAGE_DIALOG_IN:
            phase_start(scene, WM_STORAGE_READY_PHASE);
            break;
        case WM_STORAGE_DIALOG_PRESS:
            phase_start(scene, WM_STORAGE_DIALOG_OUT);
            break;
        case WM_STORAGE_DIALOG_OUT:
            scene->view = WM_STORAGE_VIEW_DETAIL;
            if (scene->answered_yes) scene->action = WM_STORAGE_ACTION_CONFIRMED;
            phase_start(scene, WM_STORAGE_DETAIL_BUTTONS_IN);
            break;
        default: break;
    }
}

static void advance_focus(WmStorageScene *scene, float frames) {
    for (int index = 0; index < STORAGE_FOCUS_COUNT; index++) {
        StorageFocus *focus = &scene->focus[index];
        if (!focus->active) continue;
        float before = focus->frame;
        focus->frame = fminf(7.0f, before + frames);
        if (focus->frame >= 7.0f && focus->requested != focus->entering) {
            float remaining = fmaxf(0.0f, frames - (7.0f - before));
            focus->entering = focus->requested;
            focus->frame = fminf(7.0f, remaining);
        }
    }
}

void wm_storage_scene_advance(WmStorageScene *scene, float frames) {
    if (!scene || !isfinite(frames) || frames < 0.0f) return;
    scene->age += frames;
    if (scene->view == WM_STORAGE_VIEW_DETAIL ||
        scene->view == WM_STORAGE_VIEW_DIALOG) {
        scene->detail_age += frames;
    }
    advance_focus(scene, frames);
    if (scene->phase == WM_STORAGE_READY_PHASE &&
        scene->view == WM_STORAGE_VIEW_GRID &&
        scene->hover.control == WM_STORAGE_CONTROL_SLOT &&
        record_at(scene, scene->hover.slot)) {
        float previous_wait = scene->balloon_wait;
        scene->balloon_wait += frames;
        if (previous_wait < 17.0f && scene->balloon_wait >= 17.0f) {
            scene->balloon_cue = true;
        }
        scene->balloon_frame = fminf(6.0f,
                                    fmaxf(0.0f, scene->balloon_wait - 17.0f));
    } else {
        scene->balloon_frame = fmaxf(0.0f, scene->balloon_frame - frames);
    }
    float remaining = frames;
    for (int steps = 0; steps < 12; steps++) {
        float length = scene_duration(scene);
        if (length <= 0.0f) break;
        float amount = fminf(remaining, length - scene->phase_frame);
        scene->phase_frame += amount;
        remaining -= amount;
        if (scene->phase_frame < length) break;
        phase_finish(scene);
        if (remaining <= 0.0f) break;
    }
}

WmStorageSnapshot wm_storage_scene_snapshot(const WmStorageScene *scene) {
    if (!scene) return (WmStorageSnapshot){.phase = WM_STORAGE_CLOSED};
    return (WmStorageSnapshot){
        .kind = scene->kind,
        .tab = scene->tab,
        .view = scene->view,
        .phase = scene->phase,
        .medium_status = current_medium(scene)->status,
        .page = scene->page,
        .record_count = current_medium(scene)->count,
        .selected_slot = scene->selected_slot,
        .operation = scene->operation,
        .phase_frame = scene->phase_frame,
        .phase_duration = scene_duration(scene),
        .locked = scene->phase != WM_STORAGE_READY_PHASE
    };
}

static bool same_hit(WmStorageHit first, WmStorageHit second) {
    return first.control == second.control &&
           (first.control != WM_STORAGE_CONTROL_SLOT || first.slot == second.slot);
}

static bool valid_hit(const WmStorageScene *scene, WmStorageHit hit) {
    const StorageMedium *medium = current_medium(scene);
    if (scene->view == WM_STORAGE_VIEW_DIALOG) {
        return hit.control == WM_STORAGE_CONTROL_NO ||
               hit.control == WM_STORAGE_CONTROL_YES;
    }
    if (scene->view == WM_STORAGE_VIEW_DETAIL) {
        return hit.control == WM_STORAGE_CONTROL_BACK ||
               hit.control == WM_STORAGE_CONTROL_MOVE ||
               hit.control == WM_STORAGE_CONTROL_COPY ||
               hit.control == WM_STORAGE_CONTROL_ERASE;
    }
    switch (hit.control) {
        case WM_STORAGE_CONTROL_WII_TAB:
        case WM_STORAGE_CONTROL_SD_TAB:
        case WM_STORAGE_CONTROL_BACK: return true;
        case WM_STORAGE_CONTROL_SLOT:
            return scene->boxes_visible && hit.slot >= 0 &&
                   hit.slot < STORAGE_PAGE_SIZE;
        case WM_STORAGE_CONTROL_PREVIOUS: return scene->page > 0;
        case WM_STORAGE_CONTROL_NEXT:
            return (scene->page + 1) * STORAGE_PAGE_SIZE < medium->count;
        default: return false;
    }
}

static void request_focus(WmStorageScene *scene, WmStorageHit hit,
                          bool entering) {
    int index = focus_index(hit);
    if (index < 0) return;
    StorageFocus *focus = &scene->focus[index];
    if (!focus->active) {
        if (entering) *focus = (StorageFocus){true, true, true, 0.0f};
        return;
    }
    focus->requested = entering;
    if (focus->frame >= 7.0f && focus->entering != entering) {
        focus->entering = entering;
        focus->frame = 0.0f;
    }
}

bool wm_storage_scene_hover(WmStorageScene *scene, WmStorageHit hit) {
    if (!scene || scene->phase != WM_STORAGE_READY_PHASE ||
        (hit.control != WM_STORAGE_CONTROL_NONE && !valid_hit(scene, hit))) {
        return false;
    }
    if ((hit.control == WM_STORAGE_CONTROL_WII_TAB &&
         scene->tab == WM_STORAGE_WII) ||
        (hit.control == WM_STORAGE_CONTROL_SD_TAB &&
         scene->tab == WM_STORAGE_SD)) {
        hit = (WmStorageHit){WM_STORAGE_CONTROL_NONE, -1};
    }
    if (same_hit(scene->hover, hit)) return false;
    if (scene->hover.control != WM_STORAGE_CONTROL_NONE) {
        request_focus(scene, scene->hover, false);
    }
    scene->hover = hit;
    if (hit.control == WM_STORAGE_CONTROL_SLOT && record_at(scene, hit.slot)) {
        scene->balloon_slot = hit.slot;
        scene->balloon_wait = 0.0f;
    }
    if (hit.control != WM_STORAGE_CONTROL_NONE) request_focus(scene, hit, true);
    return true;
}

bool wm_storage_scene_back(WmStorageScene *scene) {
    if (!scene || scene->phase != WM_STORAGE_READY_PHASE) return false;
    if (scene->view == WM_STORAGE_VIEW_DIALOG) {
        scene->answered_yes = false;
        phase_start(scene, WM_STORAGE_DIALOG_PRESS);
    } else {
        phase_start(scene, WM_STORAGE_BACK_PRESS);
    }
    return true;
}

bool wm_storage_scene_activate(WmStorageScene *scene, WmStorageHit hit) {
    if (!scene || scene->phase != WM_STORAGE_READY_PHASE ||
        !valid_hit(scene, hit)) return false;
    if (hit.control == WM_STORAGE_CONTROL_BACK) {
        return wm_storage_scene_back(scene);
    }
    if (scene->view == WM_STORAGE_VIEW_DIALOG) {
        scene->answered_yes = hit.control == WM_STORAGE_CONTROL_YES;
        phase_start(scene, WM_STORAGE_DIALOG_PRESS);
        return true;
    }
    if (scene->view == WM_STORAGE_VIEW_DETAIL) {
        switch (hit.control) {
            case WM_STORAGE_CONTROL_MOVE:
                scene->operation = WM_STORAGE_OPERATION_MOVE; break;
            case WM_STORAGE_CONTROL_COPY:
                scene->operation = WM_STORAGE_OPERATION_COPY; break;
            case WM_STORAGE_CONTROL_ERASE:
                scene->operation = WM_STORAGE_OPERATION_ERASE; break;
            default: return false;
        }
        phase_start(scene, WM_STORAGE_OPERATION_FLASH);
        return true;
    }
    if (hit.control == WM_STORAGE_CONTROL_WII_TAB ||
        hit.control == WM_STORAGE_CONTROL_SD_TAB) {
        scene->target_tab = hit.control == WM_STORAGE_CONTROL_WII_TAB
            ? WM_STORAGE_WII : WM_STORAGE_SD;
        if (scene->target_tab == scene->tab) return false;
        phase_start(scene, WM_STORAGE_TAB_OUT);
        return true;
    }
    if (hit.control == WM_STORAGE_CONTROL_PREVIOUS ||
        hit.control == WM_STORAGE_CONTROL_NEXT) {
        scene->page_direction = hit.control == WM_STORAGE_CONTROL_NEXT ? 1 : -1;
        phase_start(scene, WM_STORAGE_PAGE_OUT);
        return true;
    }
    if (hit.control == WM_STORAGE_CONTROL_SLOT && record_at(scene, hit.slot)) {
        pose_base(scene);
        StorageAnchors anchors = base_anchors(scene);
        /* N_Window owns its own IPL root scale. The box anchor above has
         * already been converted for an embedded child layout. Store the
         * original local X so the detail window receives that scale once. */
        scene->detail_origin_x = anchors.found[hit.slot]
            ? anchors.matrices[hit.slot][3] * (608.0f / 832.0f) : 0.0f;
        scene->detail_origin_y = anchors.found[hit.slot]
            ? anchors.matrices[hit.slot][7] : 0.0f;
        scene->selected_slot = hit.slot;
        scene->operation = WM_STORAGE_OPERATION_NONE;
        scene->detail_age = 0.0f;
        scene->view = WM_STORAGE_VIEW_DETAIL;
        phase_start(scene, WM_STORAGE_DETAIL_IN);
        return true;
    }
    return false;
}

WmStorageAction wm_storage_scene_take_action(WmStorageScene *scene,
                                              WmStorageOperation *operation,
                                              WmStorageRecord *record) {
    if (!scene) return WM_STORAGE_ACTION_NONE;
    WmStorageAction action = scene->action;
    if (action == WM_STORAGE_ACTION_CONFIRMED) {
        if (operation) *operation = scene->operation;
        const WmStorageRecord *selected = record_at(scene, scene->selected_slot);
        if (record && selected) *record = *selected;
    }
    scene->action = WM_STORAGE_ACTION_NONE;
    return action;
}

bool wm_storage_scene_take_balloon_cue(WmStorageScene *scene) {
    if (!scene) return false;
    bool cue = scene->balloon_cue;
    scene->balloon_cue = false;
    return cue;
}

static bool collect_anchor(void *context, const WmLayoutPaneView *pane) {
    StorageAnchors *anchors = context;
    if (strncmp(pane->name, "N_Data_b_", 9) != 0 ||
        strlen(pane->name) != 11) return true;
    char first = pane->name[9], second = pane->name[10];
    if (first < '0' || first > '9' || second < '0' || second > '9') return true;
    int index = (first - '0') * 10 + second - '0';
    if (index < 0 || index >= STORAGE_PAGE_SIZE) return true;
    static const float identity[12] = {
        1.0f, 0.0f, 0.0f, 0.0f,
        0.0f, 1.0f, 0.0f, 0.0f,
        0.0f, 0.0f, 1.0f, 0.0f
    };
    memcpy(anchors->matrices[index], identity,
           sizeof(anchors->matrices[index]));
    /* The source controller copies only anchor XY into N_All, then the box
     * layout's own IPL root scale projects that local X. Passing the entire
     * anchor matrix directly as a parent compresses the 16:9 grid. */
    anchors->matrices[index][3] = pane->matrix[3] * (832.0f / 608.0f);
    anchors->matrices[index][7] = pane->matrix[7];
    anchors->found[index] = true;
    return true;
}

static StorageAnchors base_anchors(WmStorageScene *scene) {
    StorageAnchors anchors = {0};
    WmLayoutDrawOptions options = {
        .wide = true,
        .mode = WM_LAYOUT_IPL,
        .alpha = 1.0f,
        .on_pane = collect_anchor,
        .context = &anchors
    };
    wm_layout_draw(scene->base, &options);
    return anchors;
}

static WmLayout *slot_icon(WmStorageScene *scene, int slot) {
    const WmStorageRecord *record = record_at(scene, slot);
    const char *path = record ? record->icon_layout : "";
    if (strcmp(scene->icon_paths[slot], path) != 0) {
        wm_layout_destroy(scene->icons[slot]);
        scene->icons[slot] = NULL;
        snprintf(scene->icon_paths[slot], sizeof(scene->icon_paths[slot]),
                 "%s", path);
        if (path[0]) {
            scene->icons[slot] = load_layout(scene->assets_directory, path);
            if (scene->icons[slot]) {
                wm_layout_prepare_materials(scene->platform,
                                            scene->icons[slot]);
            }
        }
    }
    return scene->icons[slot];
}

static void pose_slot_icon(WmLayout *icon, float age) {
    WmLayoutAnimationInfo animation;
    const char *name = NULL;
    if (wm_layout_animation_info(icon, "icon", &animation)) name = "icon";
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
        WmLayoutClip clip = {
            .animation = name,
            .frame = frame,
            .loop_override = 0
        };
        wm_layout_pose(icon, &clip, 1);
    } else {
        wm_layout_pose(icon, NULL, 0);
    }
    wm_layout_mask_language_groups(icon, "ENG");
}

static bool only_channel_cover(void *context, const WmLayoutPaneView *pane) {
    (void)context;
    if (strcmp(pane->name, "DataBaseCover_01") == 0) return true;
    return strcmp(pane->type, "pic1") != 0 &&
           strcmp(pane->type, "wnd1") != 0 &&
           strcmp(pane->type, "txt1") != 0;
}

static bool only_channel_arrows(void *context,
                                const WmLayoutPaneView *pane) {
    (void)context;
    if (strcmp(pane->type, "pic1") == 0) {
        return strcmp(pane->name, "ArwL") == 0 ||
               strcmp(pane->name, "ArwR") == 0 ||
               strcmp(pane->name, "ArwBtnL") == 0 ||
               strcmp(pane->name, "ArwBtnR") == 0 ||
               strcmp(pane->name, "ArwBtnL_Ac") == 0 ||
               strcmp(pane->name, "ArwBtnR_Ac") == 0;
    }
    return strcmp(pane->type, "txt1") != 0 &&
           strcmp(pane->type, "wnd1") != 0;
}

static void draw_channel_icon(WmStorageScene *scene, int slot,
                              const float anchor[12]) {
    if (scene->kind != WM_STORAGE_CHANNELS ||
        scene->phase == WM_STORAGE_BOXES_IN ||
        scene->phase == WM_STORAGE_TAB_OUT ||
        scene->phase == WM_STORAGE_PAGE_OUT ||
        scene->phase == WM_STORAGE_PAGE_IN ||
        scene->phase == WM_STORAGE_DATA_OUT) return;
    WmLayout *icon = slot_icon(scene, slot);
    if (!icon) return;
    pose_slot_icon(icon, scene->age);
    WmLayoutPaneState hit_pane;
    WmLayoutPaneState data_pane;
    if (!wm_layout_pane_state(scene->boxes[slot], "N_Atari16x9", &hit_pane) ||
        !wm_layout_pane_state(scene->boxes[slot], "N_Data_01", &data_pane)) {
        return;
    }
    float icon_scale = hit_pane.scale[0] * data_pane.scale[0];
    float icon_matrix[12] = {
        icon_scale, 0.0f, 0.0f, anchor[3],
        0.0f, icon_scale, 0.0f, anchor[7],
        0.0f, 0.0f, 1.0f, 0.0f
    };
    /* The independent thumbnail grows with the box's Focus clip. Its draw
     * window stays at the authored cell bounds, so hover never enlarges the
     * clipped region or exposes artwork outside the rounded border. */
    const float screen_scale = (float)WM_FRAME_WIDTH / 832.0f;
    const float center_x = WM_FRAME_WIDTH * 0.5f + anchor[3] * screen_scale;
    const float center_y = WM_FRAME_HEIGHT * 0.5f - anchor[7];
    WmClipRect clip = {
        center_x - 51.0f * screen_scale,
        center_y - 28.8f,
        102.0f * screen_scale,
        57.6f
    };
    wm_platform_set_clip(scene->platform, &clip);
    wm_layout_present_with_fonts(scene->platform, scene->textures, scene->fonts,
                                 icon, true, WM_LAYOUT_IPL, icon_matrix);
    wm_platform_set_clip(scene->platform, NULL);
    wm_layout_set_pane_visible(scene->boxes[slot], "DataBaseCover_01", true);
    wm_layout_present_filtered_with_fonts(
        scene->platform, scene->textures, scene->fonts, scene->boxes[slot],
        true, WM_LAYOUT_IPL, anchor, only_channel_cover, NULL);
}

static bool only_detail_mask(void *context, const WmLayoutPaneView *pane) {
    (void)context;
    if (strcmp(pane->type, "pan1") == 0) return true;
    static const char *const names[] = {
        "BannerMask_16x9", "BannerMask_4x_01", "Cover_16x9",
        "Cover_16x9_del", "BlockLine01", "T_Block_02",
        "T_Block_03", "T_Title_02", "T_Title_03"
    };
    for (size_t index = 0; index < sizeof(names) / sizeof(names[0]); index++) {
        if (strcmp(pane->name, names[index]) == 0) return true;
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
    if (!icon) return;
    WmLayoutPaneState hit_pane;
    if (!wm_layout_pane_state(scene->detail, "N_Atari16x9", &hit_pane)) return;
    pose_slot_icon(icon, scene->detail_age);
    wm_layout_set_pane_translation(icon, "RootPane", 0.0f, 0.0f, 0.0f);
    float x = hit_pane.translation[0];
    float y = hit_pane.translation[1];
    float icon_matrix[12] = {
        1.0f, 0.0f, 0.0f, x,
        0.0f, 1.0f, 0.0f, y,
        0.0f, 0.0f, 1.0f, 0.0f
    };
    float screen_scale = (float)WM_FRAME_WIDTH / 832.0f;
    WmClipRect clip = {
        WM_FRAME_WIDTH * 0.5f + (x - 85.0f) * screen_scale,
        WM_FRAME_HEIGHT * 0.5f - y - 48.0f,
        170.0f * screen_scale,
        96.0f
    };
    wm_platform_set_clip(scene->platform, &clip);
    wm_layout_present_with_fonts(scene->platform, scene->textures, scene->fonts,
                                 icon, true, WM_LAYOUT_IPL, icon_matrix);
    wm_platform_set_clip(scene->platform, NULL);
    wm_layout_present_filtered_with_fonts(
        scene->platform, scene->textures, scene->fonts, scene->detail,
        true, WM_LAYOUT_IPL, NULL, only_detail_mask, NULL);
}

static void draw_balloon(WmStorageScene *scene,
                          const StorageAnchors *anchors) {
    if (scene->view != WM_STORAGE_VIEW_GRID ||
        scene->balloon_frame <= 0.0f || scene->balloon_slot < 0 ||
        scene->balloon_slot >= STORAGE_PAGE_SIZE ||
        !anchors->found[scene->balloon_slot]) return;
    const WmStorageRecord *record = record_at(scene, scene->balloon_slot);
    if (!record) return;
    WmLayoutPaneState text_pane;
    float text_width = 0.0f;
    if (wm_layout_pane_state(scene->balloon, "T_Balloon", &text_pane)) {
        text_width = wm_font_cache_measure_text(scene->fonts, scene->balloon,
                                                &text_pane, record->title,
                                                strlen(record->title));
    }
    float width = fmaxf(160.0f * (832.0f / 608.0f), text_width + 40.0f);
    float x = fmaxf(-416.0f + 90.0f + width * 0.5f,
                    fminf(416.0f - 90.0f - width * 0.5f,
                          anchors->matrices[scene->balloon_slot][3]));
    float y = anchors->matrices[scene->balloon_slot][7] - 55.0f;
    float matrix[12] = {
        1.0f, 0.0f, 0.0f, x,
        0.0f, 1.0f, 0.0f, y,
        0.0f, 0.0f, 1.0f, 0.0f
    };
    WmLayoutClip clip = {
        .animation = "my_IplTopBalloon_a_BalloonInOut",
        .frame = scene->balloon_frame,
        .loop_override = 0
    };
    wm_layout_pose(scene->balloon, &clip, 1);
    wm_layout_set_pane_size(scene->balloon, "W_Base", width, 48.0f);
    wm_layout_set_pane_size(scene->balloon, "W_Shade", width, 48.0f);
    wm_layout_set_pose_text(scene->balloon, "T_Balloon", record->title);
    wm_layout_present_with_fonts(scene->platform, scene->textures, scene->fonts,
                                 scene->balloon, true, WM_LAYOUT_IPL, matrix);
}

static bool hit_rect(const WmLayout *layout, const char *name,
                     const float parent[12], int x, int y) {
    WmSourceRect rect;
    return wm_source_pane_rect(layout, name, true, WM_LAYOUT_IPL,
                                parent, &rect) &&
           (float)x >= rect.x && (float)x < rect.x + rect.width &&
           (float)y >= rect.y && (float)y < rect.y + rect.height;
}

WmStorageHit wm_storage_scene_hit(WmStorageScene *scene, int x, int y) {
    WmStorageHit none = {WM_STORAGE_CONTROL_NONE, -1};
    if (!scene || scene->phase != WM_STORAGE_READY_PHASE ||
        x < 0 || y < 0 || x >= WM_FRAME_WIDTH || y >= WM_FRAME_HEIGHT) return none;
    if (scene->view == WM_STORAGE_VIEW_DIALOG) {
        pose_dialog(scene);
        if (hit_rect(scene->dialog, "B_BtnA", NULL, x, y)) {
            return (WmStorageHit){WM_STORAGE_CONTROL_YES, -1};
        }
        if (hit_rect(scene->dialog, "B_BtnB", NULL, x, y)) {
            return (WmStorageHit){WM_STORAGE_CONTROL_NO, -1};
        }
        return none;
    }
    pose_back(scene);
    if (scene->view == WM_STORAGE_VIEW_DETAIL) {
        pose_detail(scene);
        static const struct {
            const char *pane;
            WmStorageControl control;
        } operations[] = {
            {"B_Move_00", WM_STORAGE_CONTROL_MOVE},
            {"B_Copy_00", WM_STORAGE_CONTROL_COPY},
            {"B_Del_00", WM_STORAGE_CONTROL_ERASE}
        };
        for (size_t index = 0; index < sizeof(operations) /
                                      sizeof(operations[0]); index++) {
            if (hit_rect(scene->detail, operations[index].pane, NULL, x, y)) {
                return (WmStorageHit){operations[index].control, -1};
            }
        }
    } else {
        pose_base(scene);
        if (scene->boxes_visible) {
            StorageAnchors anchors = base_anchors(scene);
            for (int slot = STORAGE_PAGE_SIZE - 1; slot >= 0; slot--) {
                if (!anchors.found[slot]) continue;
                pose_box(scene, slot);
                const char *pane = scene->kind == WM_STORAGE_CHANNELS
                    ? "B_Data_01" : "B_Data_00";
                if (hit_rect(scene->boxes[slot], pane,
                             anchors.matrices[slot], x, y)) {
                    return (WmStorageHit){WM_STORAGE_CONTROL_SLOT, slot};
                }
            }
        }
        if (scene->page > 0 &&
            hit_rect(scene->base, "B_ArwL", NULL, x, y)) {
            return (WmStorageHit){WM_STORAGE_CONTROL_PREVIOUS, -1};
        }
        if ((scene->page + 1) * STORAGE_PAGE_SIZE < current_medium(scene)->count &&
            hit_rect(scene->base, "B_ArwR", NULL, x, y)) {
            return (WmStorageHit){WM_STORAGE_CONTROL_NEXT, -1};
        }
        if (hit_rect(scene->base, "B_SelectWii_00", NULL, x, y)) {
            return (WmStorageHit){WM_STORAGE_CONTROL_WII_TAB, -1};
        }
        if (hit_rect(scene->base, "B_SelectSd_00", NULL, x, y)) {
            return (WmStorageHit){WM_STORAGE_CONTROL_SD_TAB, -1};
        }
    }
    if (hit_rect(scene->back, "B_Button_00", NULL, x, y)) {
        return (WmStorageHit){WM_STORAGE_CONTROL_BACK, -1};
    }
    return none;
}

bool wm_storage_scene_draw_back(WmStorageScene *scene) {
    if (!scene || scene->phase == WM_STORAGE_CLOSED) return false;
    pose_back(scene);
    wm_layout_present_with_fonts(scene->platform, scene->textures, scene->fonts,
                                 scene->back, true, WM_LAYOUT_IPL, NULL);
    return true;
}

bool wm_storage_scene_draw_content(WmStorageScene *scene) {
    if (!scene || scene->phase == WM_STORAGE_CLOSED) return false;
    pose_base(scene);
    wm_layout_present_with_fonts(scene->platform, scene->textures, scene->fonts,
                                 scene->base, true, WM_LAYOUT_IPL, NULL);
    if (scene->boxes_visible) {
        StorageAnchors anchors = base_anchors(scene);
        for (int slot = 0; slot < STORAGE_PAGE_SIZE; slot++) {
            if (!anchors.found[slot]) continue;
            pose_box(scene, slot);
            wm_layout_present_with_fonts(scene->platform, scene->textures,
                                         scene->fonts, scene->boxes[slot],
                                         true, WM_LAYOUT_IPL,
                                         anchors.matrices[slot]);
            draw_channel_icon(scene, slot, anchors.matrices[slot]);
        }
        draw_balloon(scene, &anchors);
    }
    if (scene->kind == WM_STORAGE_CHANNELS) {
        /* ChannelEdit draws its arrow subtrees again after the channel boxes
         * and title balloon, so neither layer can cover the exit motion. */
        wm_layout_present_filtered_with_fonts(
            scene->platform, scene->textures, scene->fonts, scene->base,
            true, WM_LAYOUT_IPL, NULL, only_channel_arrows, NULL);
    }
    if (scene->view == WM_STORAGE_VIEW_DETAIL ||
        scene->view == WM_STORAGE_VIEW_DIALOG) {
        pose_detail(scene);
        wm_layout_present_with_fonts(scene->platform, scene->textures,
                                     scene->fonts, scene->detail,
                                     true, WM_LAYOUT_IPL, NULL);
        draw_detail_channel_icon(scene);
    }
    if (scene->view == WM_STORAGE_VIEW_DIALOG) {
        pose_dialog(scene);
        wm_layout_present_with_fonts(scene->platform, scene->textures,
                                     scene->fonts, scene->dialog,
                                     true, WM_LAYOUT_IPL, NULL);
    }
    return true;
}

bool wm_storage_scene_draw(WmStorageScene *scene) {
    if (!wm_storage_scene_draw_back(scene)) return false;
    return wm_storage_scene_draw_content(scene);
}
