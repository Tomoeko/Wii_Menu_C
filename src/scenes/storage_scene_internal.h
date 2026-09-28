#ifndef WII_MENU_STORAGE_SCENE_INTERNAL_H
#define WII_MENU_STORAGE_SCENE_INTERNAL_H

#include "wii_menu/scenes/storage_scene.h"

#include "wii_menu/layout/layout_runtime.h"
#include "scene_assets.h"

enum {
    STORAGE_PAGE_SIZE = 15,
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
    char assets_directory[WM_SCENE_ASSET_PATH_CAPACITY];
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
    bool dialog_cue;
    char capacity_text[96];
};

/* These read-only accessors keep medium/page bounds in one place. */
const StorageMedium *wm_storage_current_medium(const WmStorageScene *scene);
const WmStorageRecord *wm_storage_record_at(const WmStorageScene *scene,
                                             int slot);

/* Hit testing poses the same authored layout as drawing. These helpers do
 * not draw; the presentation module owns their pose and anchor details. */
void wm_storage_prepare_visible_icons(WmStorageScene *scene);
void wm_storage_pose_base(WmStorageScene *scene);
void wm_storage_pose_box(WmStorageScene *scene, int slot);
void wm_storage_pose_detail(WmStorageScene *scene);
void wm_storage_pose_dialog(WmStorageScene *scene);
void wm_storage_pose_back(WmStorageScene *scene);
StorageAnchors wm_storage_base_anchors(WmStorageScene *scene);

#endif
