#ifndef WM_SD_SCENE_INTERNAL_H
#define WM_SD_SCENE_INTERNAL_H

#include "wii_menu/scenes/sd_scene.h"
#include "wii_menu/input/arrow_interaction.h"
#include "wii_menu/layout/layout_runtime.h"
#include "wii_menu/render/texture_cache.h"
#include "wii_menu/fonts/font_cache.h"
#include "wii_menu/layout/layout_assets.h"

#include <stdbool.h>
#include <stddef.h>

enum {
    SD_VISIBLE_TILES = 5 * WM_SD_SLOTS_PER_PAGE,
    SD_CLIP_CAPACITY = 18,
    SD_EVENT_CAPACITY = 16
};

typedef struct SdChannel {
    char id[17];
    WmLayout *icon;
} SdChannel;

typedef struct SdFocus {
    bool active;
    bool entering;
    bool pending_leave;
    float frame;
} SdFocus;

typedef struct SdTile {
    bool valid;
    unsigned absolute_slot;
    float matrix[12];
    WmClipRect clip;
} SdTile;

typedef enum SdBalloonPhase {
    SD_BALLOON_CLOSED,
    SD_BALLOON_WAIT,
    SD_BALLOON_ENTER,
    SD_BALLOON_LEAVE
} SdBalloonPhase;

typedef struct SdBalloon {
    SdBalloonPhase phase;
    float wait;
    float frame;
} SdBalloon;

typedef enum SdDialogPhase {
    SD_DIALOG_CLOSED,
    SD_DIALOG_ENTER,
    SD_DIALOG_IDLE,
    SD_DIALOG_SELECT,
    SD_DIALOG_TEXT_OUT,
    SD_DIALOG_TEXT_IN,
    SD_DIALOG_EXIT
} SdDialogPhase;

typedef enum SdLoaderPhase {
    SD_LOADER_CLOSED,
    SD_LOADER_ENTER,
    SD_LOADER_WAIT,
    SD_LOADER_EXIT
} SdLoaderPhase;

struct WmSdScene {
    char assets_directory[WM_LAYOUT_ASSET_PATH_CAPACITY];
    WmPlatform *platform;
    WmTextureCache *textures;
    WmFontCache *fonts;
    WmLayout *grid;
    WmLayout *footer;
    WmLayout *empty_tile;
    WmLayout *page_labels[3];
    WmLayout *focus_layout;
    WmLayout *balloon;
    WmLayout *dialog;
    WmLayout *wait_icon;
    WmLayout *help_button;
    WmLayout *loading_panel;
    WmLayout *error_panel;
    SdChannel channels[WM_SD_SLOT_COUNT];
    unsigned populated_count;
    unsigned page;
    WmSdPhase phase;
    WmSdMediaStatus media_status;
    bool card_ready;
    float age;
    float scroll_frame;
    int scroll_direction;
    bool help_seen;
    bool welcome_pending;
    bool welcome_active;
    SdDialogPhase dialog_phase;
    float dialog_frame;
    unsigned dialog_page;
    unsigned dialog_previous_page;
    int dialog_destination;
    WmSdControl dialog_selected;
    float icon_age;
    SdLoaderPhase loader_phase;
    float loader_frame;
    SdFocus button_focus[WM_SD_CONTROL_HELP_NEXT + 1];
    SdFocus tile_focus[WM_SD_SLOT_COUNT];
    WmSdHit hover;
    WmArrowInteraction arrows;
    float help_press;
    WmSdControl balloon_hover;
    SdBalloon balloons[2];
    WmSdMessageProvider message_provider;
    void *message_context;
    WmSdEvent events[SD_EVENT_CAPACITY];
    unsigned event_first;
    unsigned event_count;
};

float wm_sd_frame_clamp(float value, float end);
void wm_sd_pose_footer(WmSdScene *scene);

#endif
