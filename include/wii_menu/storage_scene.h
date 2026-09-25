#ifndef WII_MENU_STORAGE_SCENE_H
#define WII_MENU_STORAGE_SCENE_H

#include "wii_menu/font_cache.h"
#include "wii_menu/platform.h"
#include "wii_menu/texture_cache.h"

#include <stdbool.h>
#include <stddef.h>

typedef struct WmStorageScene WmStorageScene;

typedef enum WmStorageKind {
    WM_STORAGE_CHANNELS,
    WM_STORAGE_WII_SAVES,
    WM_STORAGE_GAMECUBE_SAVES
} WmStorageKind;

typedef enum WmStorageTab {
    WM_STORAGE_WII,
    WM_STORAGE_SD
} WmStorageTab;

typedef enum WmStorageMediumStatus {
    WM_STORAGE_READY,
    WM_STORAGE_ABSENT,
    WM_STORAGE_READ_ERROR,
    WM_STORAGE_UNSUPPORTED
} WmStorageMediumStatus;

typedef struct WmStorageRecord {
    char id[65];
    char title[128];
    char icon_layout[256]; /* Relative path from a local WAD export, optional. */
    unsigned blocks;
} WmStorageRecord;

typedef enum WmStorageView {
    WM_STORAGE_VIEW_GRID,
    WM_STORAGE_VIEW_DETAIL,
    WM_STORAGE_VIEW_DIALOG
} WmStorageView;

typedef enum WmStoragePhase {
    WM_STORAGE_CLOSED,
    WM_STORAGE_DATA_IN,
    WM_STORAGE_TABS_IN,
    WM_STORAGE_ERROR_IN,
    WM_STORAGE_BOXES_IN,
    WM_STORAGE_READY_PHASE,
    WM_STORAGE_TAB_OUT,
    WM_STORAGE_PAGE_OUT,
    WM_STORAGE_PAGE_IN,
    WM_STORAGE_DETAIL_IN,
    WM_STORAGE_BACK_PRESS,
    WM_STORAGE_DETAIL_OUT,
    WM_STORAGE_OPERATION_FLASH,
    WM_STORAGE_DETAIL_BUTTONS_OUT,
    WM_STORAGE_DIALOG_IN,
    WM_STORAGE_DIALOG_PRESS,
    WM_STORAGE_DIALOG_OUT,
    WM_STORAGE_DETAIL_BUTTONS_IN,
    WM_STORAGE_DATA_OUT
} WmStoragePhase;

typedef enum WmStorageControl {
    WM_STORAGE_CONTROL_NONE,
    WM_STORAGE_CONTROL_WII_TAB,
    WM_STORAGE_CONTROL_SD_TAB,
    WM_STORAGE_CONTROL_SLOT,
    WM_STORAGE_CONTROL_PREVIOUS,
    WM_STORAGE_CONTROL_NEXT,
    WM_STORAGE_CONTROL_BACK,
    WM_STORAGE_CONTROL_MOVE,
    WM_STORAGE_CONTROL_COPY,
    WM_STORAGE_CONTROL_ERASE,
    WM_STORAGE_CONTROL_NO,
    WM_STORAGE_CONTROL_YES
} WmStorageControl;

typedef struct WmStorageHit {
    WmStorageControl control;
    int slot; /* Zero through fourteen only for SLOT. */
} WmStorageHit;

typedef enum WmStorageOperation {
    WM_STORAGE_OPERATION_NONE,
    WM_STORAGE_OPERATION_MOVE,
    WM_STORAGE_OPERATION_COPY,
    WM_STORAGE_OPERATION_ERASE
} WmStorageOperation;

typedef enum WmStorageAction {
    WM_STORAGE_ACTION_NONE,
    WM_STORAGE_ACTION_EXITED,
    WM_STORAGE_ACTION_CONFIRMED
} WmStorageAction;

typedef struct WmStorageSnapshot {
    WmStorageKind kind;
    WmStorageTab tab;
    WmStorageView view;
    WmStoragePhase phase;
    WmStorageMediumStatus medium_status;
    size_t page;
    size_t record_count;
    int selected_slot;
    WmStorageOperation operation;
    float phase_frame;
    float phase_duration;
    bool locked;
} WmStorageSnapshot;

/* Source category test for Channel Management. Non-native local IDs are
 * allowed as in the HTML demonstration fixture; the Disc Channel is excluded. */
bool wm_storage_manageable_channel(const char *id, bool has_icon);

WmStorageScene *wm_storage_scene_create(WmPlatform *platform,
                                         const char *assets_directory,
                                         WmTextureCache *textures,
                                         WmFontCache *fonts,
                                         WmStorageKind kind);
void wm_storage_scene_destroy(WmStorageScene *scene);

/* Input records are copied. Channel media filter non-manageable system titles
 * and sort native Wii title IDs as the HTML controller does. The 240-record
 * bound mirrors its local fixture. A ready Wii Save Data medium defaults to
 * its synthetic dummy save; GameCube Slot B defaults to absent. */
bool wm_storage_scene_set_medium(WmStorageScene *scene, WmStorageTab tab,
                                  WmStorageMediumStatus status,
                                  const WmStorageRecord *records,
                                  size_t count, unsigned free_blocks);
bool wm_storage_scene_open(WmStorageScene *scene, WmStorageTab initial_tab);
void wm_storage_scene_advance(WmStorageScene *scene, float frames);
WmStorageSnapshot wm_storage_scene_snapshot(const WmStorageScene *scene);

WmStorageHit wm_storage_scene_hit(WmStorageScene *scene, int x, int y);
bool wm_storage_scene_hover(WmStorageScene *scene, WmStorageHit hit);
bool wm_storage_scene_activate(WmStorageScene *scene, WmStorageHit hit);
bool wm_storage_scene_back(WmStorageScene *scene);

/* A confirmed action reports its record and requested operation. The scene
 * makes no NAND, SD, or save-file changes; the HTML fixture also leaves its
 * record list intact. The caller handles any explicit local persistence. */
WmStorageAction wm_storage_scene_take_action(WmStorageScene *scene,
                                              WmStorageOperation *operation,
                                              WmStorageRecord *record);
/* True once when a held populated slot opens its native title balloon. */
bool wm_storage_scene_take_balloon_cue(WmStorageScene *scene);

/* While Storage is open, source order is Options background, Storage Back,
 * retained Options objects/headings, then Storage content. */
bool wm_storage_scene_draw_back(WmStorageScene *scene);
bool wm_storage_scene_draw_content(WmStorageScene *scene);
/* Complete draw for callers that do not need to interleave the heading. */
bool wm_storage_scene_draw(WmStorageScene *scene);

#endif
