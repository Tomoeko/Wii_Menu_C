#include "storage_scene_internal.h"

#include "wii_menu/render/material_prepare.h"
#include "wii_menu/input/source_hit.h"
#include "wii_menu/support/hex.h"

#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

enum { STORAGE_RECORD_LIMIT = 240 };

bool wm_storage_manageable_channel(const char *id, bool has_icon) {
    if (!has_icon || !id || !id[0] || strcmp(id, "disc") == 0)
        return false;
    if (strlen(id) != 16)
        return true;
    uint32_t high = 0;
    for (int index = 0; index < 16; index++) {
        int digit = wm_hex_digit(id[index]);
        if (digit < 0)
            return true;
        if (index < 8)
            high = (high << 4) | (uint32_t)digit;
    }
    if (high < 0x10000u || high > 0x10007u || !(0xd3u & (1u << (high - 0x10000u))))
        return false;
    unsigned first =
        ((unsigned)wm_hex_digit(id[8]) << 4) | (unsigned)wm_hex_digit(id[9]);
    return (first >= 0x41u && first <= 0x5au) || (first >= 0x30u && first <= 0x39u) ||
           first < 0x20u || first > 0x7eu;
}

static bool native_title_id(const char *id) {
    if (strlen(id) != 16)
        return false;
    for (size_t index = 0; index < 16; index++) {
        if (wm_hex_digit(id[index]) < 0)
            return false;
    }
    return true;
}

static int channel_record_order(const WmStorageRecord *first,
                                const WmStorageRecord *second) {
    bool first_native = native_title_id(first->id);
    bool second_native = native_title_id(second->id);
    if (first_native != second_native)
        return first_native ? -1 : 1;
    if (!first_native)
        return 0;
    for (size_t index = 0; index < 16; index++) {
        int difference =
            wm_hex_digit(first->id[index]) - wm_hex_digit(second->id[index]);
        if (difference)
            return difference;
    }
    return 0;
}

static void sort_wii_channels(WmStorageRecord *records, size_t count) {
    /* Sort 16-digit title IDs first while preserving the input order of
     * other local records with a stable insertion sort. */
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
    return wm_layout_load_asset(directory, relative, "Data Management");
}

static bool record_valid(const WmStorageRecord *record) {
    if (!record || !record->id[0] || !record->title[0] ||
        !memchr(record->id, 0, sizeof(record->id)) ||
        !memchr(record->title, 0, sizeof(record->title)) ||
        !memchr(record->icon_layout, 0, sizeof(record->icon_layout))) {
        return false;
    }
    const char *path = record->icon_layout;
    if (path[0] == '/' || strstr(path, "..") || strchr(path, '\\'))
        return false;
    if (strcmp(record->id, "disc") == 0 || strcmp(record->id, "__proto__") == 0 ||
        strcmp(record->id, "constructor") == 0 || strcmp(record->id, "prototype") == 0)
        return false;
    for (const char *at = record->id; *at; at++) {
        char value = *at;
        if (!((value >= 'A' && value <= 'Z') || (value >= 'a' && value <= 'z') ||
              (value >= '0' && value <= '9') || value == '_' || value == '-'))
            return false;
    }
    return true;
}

const StorageMedium *wm_storage_current_medium(const WmStorageScene *scene) {
    return &scene->media[scene->tab];
}

const WmStorageRecord *wm_storage_record_at(const WmStorageScene *scene, int slot) {
    const StorageMedium *medium = wm_storage_current_medium(scene);
    if (medium->status != WM_STORAGE_READY || slot < 0 || slot >= STORAGE_PAGE_SIZE)
        return NULL;
    size_t index = scene->page * STORAGE_PAGE_SIZE + (size_t)slot;
    return index < medium->count ? &medium->records[index] : NULL;
}

static float duration(WmStoragePhase phase) {
    switch (phase) {
        case WM_STORAGE_DATA_IN:
        case WM_STORAGE_BOXES_IN:
        case WM_STORAGE_DIALOG_IN:
        case WM_STORAGE_DIALOG_OUT:
        case WM_STORAGE_DATA_OUT:
            return 26.0f;
        case WM_STORAGE_TABS_IN:
            return 16.0f;
        case WM_STORAGE_ERROR_IN:
            return 16.0f;
        case WM_STORAGE_TAB_OUT:
            return 22.0f;
        case WM_STORAGE_PAGE_OUT:
            return 21.0f * 20.0f / 47.0f;
        case WM_STORAGE_PAGE_IN:
            return 26.0f * 20.0f / 47.0f;
        case WM_STORAGE_DETAIL_OUT:
            return 11.0f;
        case WM_STORAGE_DETAIL_IN:
            return 36.0f;
        case WM_STORAGE_BACK_PRESS:
        case WM_STORAGE_OPERATION_FLASH:
            return 19.0f;
        case WM_STORAGE_DETAIL_BUTTONS_OUT:
        case WM_STORAGE_DETAIL_BUTTONS_IN:
            return 46.0f;
        case WM_STORAGE_DIALOG_PRESS:
            return 21.0f;
        default:
            return 0.0f;
    }
}

static float scene_duration(const WmStorageScene *scene) {
    if (scene->phase == WM_STORAGE_TABS_IN &&
        scene->kind == WM_STORAGE_GAMECUBE_SAVES &&
        wm_storage_current_medium(scene)->status == WM_STORAGE_READY)
        return 26.0f;
    return duration(scene->phase);
}

static void phase_start(WmStorageScene *scene, WmStoragePhase phase) {
    scene->phase = phase;
    scene->phase_frame = 0.0f;
    scene->hover = (WmStorageHit){WM_STORAGE_CONTROL_NONE, -1};
    memset(scene->focus, 0, sizeof(scene->focus));
}

static int focus_index(WmStorageHit hit) {
    switch (hit.control) {
        case WM_STORAGE_CONTROL_WII_TAB:
            return 0;
        case WM_STORAGE_CONTROL_SD_TAB:
            return 1;
        case WM_STORAGE_CONTROL_SLOT:
            return hit.slot >= 0 && hit.slot < STORAGE_PAGE_SIZE ? hit.slot + 2 : -1;
        case WM_STORAGE_CONTROL_PREVIOUS:
            return 17;
        case WM_STORAGE_CONTROL_NEXT:
            return 18;
        case WM_STORAGE_CONTROL_BACK:
            return 19;
        case WM_STORAGE_CONTROL_MOVE:
            return 20;
        case WM_STORAGE_CONTROL_COPY:
            return 21;
        case WM_STORAGE_CONTROL_ERASE:
            return 22;
        case WM_STORAGE_CONTROL_NO:
            return 23;
        case WM_STORAGE_CONTROL_YES:
            return 24;
        default:
            return -1;
    }
}

WmStorageScene *wm_storage_scene_create(WmPlatform *platform,
                                        const char *assets_directory,
                                        WmTextureCache *textures, WmFontCache *fonts,
                                        WmStorageKind kind) {
    if (!platform || !assets_directory || !assets_directory[0] || !textures || !fonts ||
        kind > WM_STORAGE_GAMECUBE_SAVES)
        return NULL;
    const char *base_path =
        kind == WM_STORAGE_CHANNELS ? "layouts/chanEdit/it_ObjChannelEdit_a.json"
        : kind == WM_STORAGE_GAMECUBE_SAVES ? "layouts/gcMem/it_ObjCubeEdit_a.json"
                                            : "layouts/wiiMem/it_ObjCubeEdit_a.json";
    const char *box_path =
        kind == WM_STORAGE_CHANNELS ? "layouts/chanEdit/it_ObjChannelEdit_b.json"
        : kind == WM_STORAGE_GAMECUBE_SAVES ? "layouts/gcMem/it_ObjCubeEdit_b.json"
                                            : "layouts/wiiMem/it_ObjDataEdit_b.json";
    const char *detail_path = kind == WM_STORAGE_CHANNELS
                                  ? "layouts/chanEdit/mn_ChannelDetail_a.json"
                                  : "layouts/wiiMem/081210_sys4_mn_DataDetail_a.json";
    WmStorageScene *scene = calloc(1, sizeof(*scene));
    if (!scene)
        return NULL;
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
             kind == WM_STORAGE_CHANNELS ? "it_ObjChannelEdit_a" : "it_ObjCubeEdit_a");
    /* Wii Memory binds CubeEdit BRLAN clips to the DataEdit BRLYT. The
     * export also contains DataEdit clips, but its focus keys differ. */
    snprintf(scene->box_stem, sizeof(scene->box_stem), "%s",
             kind == WM_STORAGE_CHANNELS ? "it_ObjChannelEdit_b" : "it_ObjCubeEdit_b");
    snprintf(scene->detail_stem, sizeof(scene->detail_stem), "%s",
             kind == WM_STORAGE_CHANNELS ? "mn_ChannelDetail_a"
                                         : "081210_sys4_mn_DataDetail_a");
    scene->base = load_layout(assets_directory, base_path);
    for (int index = 0; index < STORAGE_PAGE_SIZE; index++) {
        scene->boxes[index] = load_layout(assets_directory, box_path);
    }
    scene->detail = load_layout(assets_directory, detail_path);
    scene->dialog =
        load_layout(assets_directory, "layouts/dlgWdw/my_DialogWindow_b.json");
    scene->back = load_layout(assets_directory, "layouts/setupBtn/it_Button_a.json");
    scene->balloon =
        load_layout(assets_directory, "layouts/balloon/my_IplTopBalloon_a.json");
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
            .id = "dummy-save", .title = "Dummy Save", .blocks = 1};
        if (!wm_storage_scene_set_medium(scene, WM_STORAGE_WII, WM_STORAGE_READY,
                                         &sample, 1, 905)) {
            wm_storage_scene_destroy(scene);
            return NULL;
        }
    }
    scene->phase = WM_STORAGE_CLOSED;
    scene->selected_slot = -1;
    return scene;
}

void wm_storage_scene_destroy(WmStorageScene *scene) {
    if (!scene)
        return;
    for (int tab = 0; tab < 2; tab++)
        free(scene->media[tab].records);
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
                                 const WmStorageRecord *records, size_t count,
                                 unsigned free_blocks) {
    if (!scene || tab > WM_STORAGE_SD || status > WM_STORAGE_UNSUPPORTED ||
        count > STORAGE_RECORD_LIMIT || (count && !records) ||
        free_blocks > (tab == WM_STORAGE_WII ? 9999u : 999999u))
        return false;
    WmStorageRecord *copy = NULL;
    size_t included = 0;
    if (count) {
        copy = malloc(count * sizeof(*copy));
        if (!copy)
            return false;
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
    if (scene->tab == tab && scene->page * STORAGE_PAGE_SIZE >= included)
        scene->page = 0;
    if (scene->tab == tab && scene->phase != WM_STORAGE_CLOSED) {
        wm_storage_prepare_visible_icons(scene);
    }
    return true;
}

bool wm_storage_scene_open(WmStorageScene *scene, WmStorageTab initial_tab) {
    if (!scene || initial_tab > WM_STORAGE_SD)
        return false;
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
    scene->dialog_cue = false;
    scene->base_data_frame = 0.0f;
    scene->base_select_frame = 0.0f;
    scene->box_frame = 0.0f;
    phase_start(scene, WM_STORAGE_DATA_IN);
    wm_storage_prepare_visible_icons(scene);
    return true;
}

static void phase_finish(WmStorageScene *scene) {
    switch (scene->phase) {
        case WM_STORAGE_DATA_IN:
            scene->base_data_frame = 25.0f;
            if (scene->kind == WM_STORAGE_GAMECUBE_SAVES &&
                wm_storage_current_medium(scene)->status == WM_STORAGE_READY) {
                scene->boxes_visible = true;
            }
            phase_start(scene, WM_STORAGE_TABS_IN);
            break;
        case WM_STORAGE_TABS_IN:
            scene->base_select_frame = 15.0f;
            if (scene->kind == WM_STORAGE_GAMECUBE_SAVES &&
                wm_storage_current_medium(scene)->status == WM_STORAGE_READY) {
                scene->box_frame = 25.0f;
                phase_start(scene, WM_STORAGE_READY_PHASE);
            } else if (wm_storage_current_medium(scene)->status == WM_STORAGE_READY) {
                scene->boxes_visible = true;
                phase_start(scene, WM_STORAGE_BOXES_IN);
            } else
                phase_start(scene, WM_STORAGE_ERROR_IN);
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
            wm_storage_prepare_visible_icons(scene);
            scene->boxes_visible =
                wm_storage_current_medium(scene)->status == WM_STORAGE_READY;
            phase_start(scene, scene->boxes_visible ? WM_STORAGE_BOXES_IN
                                                    : WM_STORAGE_ERROR_IN);
            break;
        case WM_STORAGE_PAGE_OUT:
            if (scene->page_direction > 0)
                scene->page++;
            else
                scene->page--;
            wm_storage_prepare_visible_icons(scene);
            phase_start(scene, WM_STORAGE_PAGE_IN);
            break;
        case WM_STORAGE_BACK_PRESS:
            phase_start(scene, scene->view == WM_STORAGE_VIEW_GRID
                                   ? WM_STORAGE_DATA_OUT
                                   : WM_STORAGE_DETAIL_OUT);
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
            scene->dialog_cue = true;
            break;
        case WM_STORAGE_DIALOG_IN:
            phase_start(scene, WM_STORAGE_READY_PHASE);
            break;
        case WM_STORAGE_DIALOG_PRESS:
            phase_start(scene, WM_STORAGE_DIALOG_OUT);
            break;
        case WM_STORAGE_DIALOG_OUT:
            scene->view = WM_STORAGE_VIEW_DETAIL;
            if (scene->answered_yes)
                scene->action = WM_STORAGE_ACTION_CONFIRMED;
            phase_start(scene, WM_STORAGE_DETAIL_BUTTONS_IN);
            break;
        default:
            break;
    }
}

static void advance_focus(WmStorageScene *scene, float frames) {
    for (int index = 0; index < STORAGE_FOCUS_COUNT; index++) {
        StorageFocus *focus = &scene->focus[index];
        if (!focus->active)
            continue;
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
    if (!scene || !isfinite(frames) || frames < 0.0f)
        return;
    scene->age += frames;
    if (scene->view == WM_STORAGE_VIEW_DETAIL ||
        scene->view == WM_STORAGE_VIEW_DIALOG) {
        scene->detail_age += frames;
    }
    advance_focus(scene, frames);
    if (scene->phase == WM_STORAGE_READY_PHASE && scene->view == WM_STORAGE_VIEW_GRID &&
        scene->hover.control == WM_STORAGE_CONTROL_SLOT &&
        wm_storage_record_at(scene, scene->hover.slot)) {
        float previous_wait = scene->balloon_wait;
        scene->balloon_wait += frames;
        if (previous_wait < 17.0f && scene->balloon_wait >= 17.0f) {
            scene->balloon_cue = true;
        }
        scene->balloon_frame = fminf(6.0f, fmaxf(0.0f, scene->balloon_wait - 17.0f));
    } else {
        scene->balloon_frame = fmaxf(0.0f, scene->balloon_frame - frames);
    }
    float remaining = frames;
    for (int steps = 0; steps < 12; steps++) {
        float length = scene_duration(scene);
        if (length <= 0.0f)
            break;
        float amount = fminf(remaining, length - scene->phase_frame);
        scene->phase_frame += amount;
        remaining -= amount;
        if (scene->phase_frame < length)
            break;
        phase_finish(scene);
        if (remaining <= 0.0f)
            break;
    }
}

WmStorageSnapshot wm_storage_scene_snapshot(const WmStorageScene *scene) {
    if (!scene)
        return (WmStorageSnapshot){.phase = WM_STORAGE_CLOSED};
    return (WmStorageSnapshot){.kind = scene->kind,
                               .tab = scene->tab,
                               .view = scene->view,
                               .phase = scene->phase,
                               .medium_status =
                                   wm_storage_current_medium(scene)->status,
                               .page = scene->page,
                               .record_count = wm_storage_current_medium(scene)->count,
                               .selected_slot = scene->selected_slot,
                               .operation = scene->operation,
                               .phase_frame = scene->phase_frame,
                               .phase_duration = scene_duration(scene),
                               .locked = scene->phase != WM_STORAGE_READY_PHASE};
}

static bool same_hit(WmStorageHit first, WmStorageHit second) {
    return first.control == second.control &&
           (first.control != WM_STORAGE_CONTROL_SLOT || first.slot == second.slot);
}

static bool valid_hit(const WmStorageScene *scene, WmStorageHit hit) {
    const StorageMedium *medium = wm_storage_current_medium(scene);
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
        case WM_STORAGE_CONTROL_BACK:
            return true;
        case WM_STORAGE_CONTROL_SLOT:
            return scene->boxes_visible && hit.slot >= 0 &&
                   hit.slot < STORAGE_PAGE_SIZE;
        case WM_STORAGE_CONTROL_PREVIOUS:
            return scene->page > 0;
        case WM_STORAGE_CONTROL_NEXT:
            return (scene->page + 1) * STORAGE_PAGE_SIZE < medium->count;
        default:
            return false;
    }
}

static void request_focus(WmStorageScene *scene, WmStorageHit hit, bool entering) {
    int index = focus_index(hit);
    if (index < 0)
        return;
    StorageFocus *focus = &scene->focus[index];
    if (!focus->active) {
        if (entering)
            *focus = (StorageFocus){true, true, true, 0.0f};
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
    if ((hit.control == WM_STORAGE_CONTROL_WII_TAB && scene->tab == WM_STORAGE_WII) ||
        (hit.control == WM_STORAGE_CONTROL_SD_TAB && scene->tab == WM_STORAGE_SD)) {
        hit = (WmStorageHit){WM_STORAGE_CONTROL_NONE, -1};
    }
    if (same_hit(scene->hover, hit))
        return false;
    if (scene->hover.control != WM_STORAGE_CONTROL_NONE) {
        request_focus(scene, scene->hover, false);
    }
    scene->hover = hit;
    if (hit.control == WM_STORAGE_CONTROL_SLOT &&
        wm_storage_record_at(scene, hit.slot)) {
        scene->balloon_slot = hit.slot;
        scene->balloon_wait = 0.0f;
    }
    if (hit.control != WM_STORAGE_CONTROL_NONE)
        request_focus(scene, hit, true);
    return true;
}

bool wm_storage_scene_back(WmStorageScene *scene) {
    if (!scene || scene->phase != WM_STORAGE_READY_PHASE)
        return false;
    if (scene->view == WM_STORAGE_VIEW_DIALOG) {
        scene->answered_yes = false;
        phase_start(scene, WM_STORAGE_DIALOG_PRESS);
    } else {
        phase_start(scene, WM_STORAGE_BACK_PRESS);
    }
    return true;
}

bool wm_storage_scene_activate(WmStorageScene *scene, WmStorageHit hit) {
    if (!scene || scene->phase != WM_STORAGE_READY_PHASE || !valid_hit(scene, hit))
        return false;
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
                scene->operation = WM_STORAGE_OPERATION_MOVE;
                break;
            case WM_STORAGE_CONTROL_COPY:
                scene->operation = WM_STORAGE_OPERATION_COPY;
                break;
            case WM_STORAGE_CONTROL_ERASE:
                scene->operation = WM_STORAGE_OPERATION_ERASE;
                break;
            default:
                return false;
        }
        phase_start(scene, WM_STORAGE_OPERATION_FLASH);
        return true;
    }
    if (hit.control == WM_STORAGE_CONTROL_WII_TAB ||
        hit.control == WM_STORAGE_CONTROL_SD_TAB) {
        scene->target_tab =
            hit.control == WM_STORAGE_CONTROL_WII_TAB ? WM_STORAGE_WII : WM_STORAGE_SD;
        if (scene->target_tab == scene->tab)
            return false;
        phase_start(scene, WM_STORAGE_TAB_OUT);
        return true;
    }
    if (hit.control == WM_STORAGE_CONTROL_PREVIOUS ||
        hit.control == WM_STORAGE_CONTROL_NEXT) {
        scene->page_direction = hit.control == WM_STORAGE_CONTROL_NEXT ? 1 : -1;
        phase_start(scene, WM_STORAGE_PAGE_OUT);
        return true;
    }
    if (hit.control == WM_STORAGE_CONTROL_SLOT &&
        wm_storage_record_at(scene, hit.slot)) {
        wm_storage_pose_base(scene);
        StorageAnchors anchors = wm_storage_base_anchors(scene);
        /* N_Window owns its own IPL root scale. The box anchor above has
         * already been converted for an embedded child layout. Store the
         * original local X so the detail window receives that scale once. */
        scene->detail_origin_x = anchors.found[hit.slot]
                                     ? anchors.matrices[hit.slot][3] * (608.0f / 832.0f)
                                     : 0.0f;
        scene->detail_origin_y =
            anchors.found[hit.slot] ? anchors.matrices[hit.slot][7] : 0.0f;
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
    if (!scene)
        return WM_STORAGE_ACTION_NONE;
    WmStorageAction action = scene->action;
    if (action == WM_STORAGE_ACTION_CONFIRMED) {
        if (operation)
            *operation = scene->operation;
        const WmStorageRecord *selected =
            wm_storage_record_at(scene, scene->selected_slot);
        if (record && selected)
            *record = *selected;
    }
    scene->action = WM_STORAGE_ACTION_NONE;
    return action;
}

bool wm_storage_scene_take_balloon_cue(WmStorageScene *scene) {
    if (!scene)
        return false;
    bool cue = scene->balloon_cue;
    scene->balloon_cue = false;
    return cue;
}

bool wm_storage_scene_take_dialog_cue(WmStorageScene *scene) {
    if (!scene)
        return false;
    bool cue = scene->dialog_cue;
    scene->dialog_cue = false;
    return cue;
}

static bool hit_rect(const WmLayout *layout, const char *name, const float parent[12],
                     int x, int y) {
    WmSourceRect rect;
    return wm_source_pane_rect(layout, name, true, WM_LAYOUT_IPL, parent, &rect) &&
           (float)x >= rect.x && (float)x < rect.x + rect.width && (float)y >= rect.y &&
           (float)y < rect.y + rect.height;
}

static WmStorageHit dialog_hit(WmStorageScene *scene, int x, int y) {
    wm_storage_pose_dialog(scene);
    if (hit_rect(scene->dialog, "B_BtnA", NULL, x, y))
        return (WmStorageHit){WM_STORAGE_CONTROL_YES, -1};
    if (hit_rect(scene->dialog, "B_BtnB", NULL, x, y))
        return (WmStorageHit){WM_STORAGE_CONTROL_NO, -1};
    return (WmStorageHit){WM_STORAGE_CONTROL_NONE, -1};
}

static WmStorageHit detail_hit(WmStorageScene *scene, int x, int y) {
    wm_storage_pose_detail(scene);
    static const struct {
        const char *pane;
        WmStorageControl control;
    } operations[] = {{"B_Move_00", WM_STORAGE_CONTROL_MOVE},
                      {"B_Copy_00", WM_STORAGE_CONTROL_COPY},
                      {"B_Del_00", WM_STORAGE_CONTROL_ERASE}};
    for (size_t index = 0; index < sizeof(operations) / sizeof(operations[0]);
         index++) {
        if (hit_rect(scene->detail, operations[index].pane, NULL, x, y))
            return (WmStorageHit){operations[index].control, -1};
    }
    return (WmStorageHit){WM_STORAGE_CONTROL_NONE, -1};
}

static WmStorageHit slot_hit(WmStorageScene *scene, int x, int y) {
    if (!scene->boxes_visible)
        return (WmStorageHit){WM_STORAGE_CONTROL_NONE, -1};
    StorageAnchors anchors = wm_storage_base_anchors(scene);
    for (int slot = STORAGE_PAGE_SIZE - 1; slot >= 0; slot--) {
        if (!anchors.found[slot])
            continue;
        wm_storage_pose_box(scene, slot);
        const char *pane =
            scene->kind == WM_STORAGE_CHANNELS ? "B_Data_01" : "B_Data_00";
        if (hit_rect(scene->boxes[slot], pane, anchors.matrices[slot], x, y))
            return (WmStorageHit){WM_STORAGE_CONTROL_SLOT, slot};
    }
    return (WmStorageHit){WM_STORAGE_CONTROL_NONE, -1};
}

static WmStorageHit base_hit(WmStorageScene *scene, int x, int y) {
    wm_storage_pose_base(scene);
    WmStorageHit hit = slot_hit(scene, x, y);
    if (hit.control != WM_STORAGE_CONTROL_NONE)
        return hit;
    if (scene->page > 0 && hit_rect(scene->base, "B_ArwL", NULL, x, y))
        return (WmStorageHit){WM_STORAGE_CONTROL_PREVIOUS, -1};
    if ((scene->page + 1) * STORAGE_PAGE_SIZE <
            wm_storage_current_medium(scene)->count &&
        hit_rect(scene->base, "B_ArwR", NULL, x, y))
        return (WmStorageHit){WM_STORAGE_CONTROL_NEXT, -1};
    if (hit_rect(scene->base, "B_SelectWii_00", NULL, x, y))
        return (WmStorageHit){WM_STORAGE_CONTROL_WII_TAB, -1};
    if (hit_rect(scene->base, "B_SelectSd_00", NULL, x, y))
        return (WmStorageHit){WM_STORAGE_CONTROL_SD_TAB, -1};
    return (WmStorageHit){WM_STORAGE_CONTROL_NONE, -1};
}

WmStorageHit wm_storage_scene_hit(WmStorageScene *scene, int x, int y) {
    WmStorageHit none = {WM_STORAGE_CONTROL_NONE, -1};
    if (!scene || scene->phase != WM_STORAGE_READY_PHASE || x < 0 || y < 0 ||
        x >= WM_FRAME_WIDTH || y >= WM_FRAME_HEIGHT)
        return none;
    if (scene->view == WM_STORAGE_VIEW_DIALOG)
        return dialog_hit(scene, x, y);
    wm_storage_pose_back(scene);
    WmStorageHit hit = scene->view == WM_STORAGE_VIEW_DETAIL ? detail_hit(scene, x, y)
                                                             : base_hit(scene, x, y);
    if (hit.control != WM_STORAGE_CONTROL_NONE)
        return hit;
    if (hit_rect(scene->back, "B_Button_00", NULL, x, y))
        return (WmStorageHit){WM_STORAGE_CONTROL_BACK, -1};
    return none;
}
