#ifndef WII_MENU_SD_SCENE_H
#define WII_MENU_SD_SCENE_H

#include "wii_menu/font_cache.h"
#include "wii_menu/platform.h"
#include "wii_menu/texture_cache.h"

#include <stdbool.h>
#include <stddef.h>

enum { WM_SD_PAGE_COUNT = 20, WM_SD_SLOTS_PER_PAGE = 12,
       WM_SD_SLOT_COUNT = WM_SD_PAGE_COUNT * WM_SD_SLOTS_PER_PAGE };

typedef struct WmSdScene WmSdScene;

/* SD membership is an explicit local mapping. Its artwork must have been
 * prepared from a user-supplied title; this scene never reads a physical SD
 * device or infers membership from the installed NAND title list. */
typedef struct WmSdChannel {
    unsigned slot;
    const char *title_id; /* Sixteen hexadecimal characters. */
} WmSdChannel;

typedef enum WmSdMediaStatus {
    WM_SD_MEDIA_READY,
    WM_SD_MEDIA_ABSENT,
    WM_SD_MEDIA_READ_ERROR,
    WM_SD_MEDIA_UNSUPPORTED
} WmSdMediaStatus;

typedef enum WmSdPhase {
    WM_SD_CLOSED,
    WM_SD_ACTIVE,
    WM_SD_SCROLL,
    WM_SD_LEAVING
} WmSdPhase;

typedef enum WmSdControl {
    WM_SD_CONTROL_NONE,
    WM_SD_CONTROL_BACK,
    WM_SD_CONTROL_HELP,
    WM_SD_CONTROL_PREVIOUS,
    WM_SD_CONTROL_NEXT,
    WM_SD_CONTROL_CHANNEL,
    WM_SD_CONTROL_HELP_BACK,
    WM_SD_CONTROL_HELP_NEXT
} WmSdControl;

typedef struct WmSdHit {
    WmSdControl control;
    unsigned slot; /* Absolute 0–239 slot for WM_SD_CONTROL_CHANNEL. */
} WmSdHit;

typedef enum WmSdEventType {
    WM_SD_EVENT_NONE,
    WM_SD_EVENT_EXIT,
    WM_SD_EVENT_CHANNEL_SELECTED,
    WM_SD_EVENT_HELP_OPEN,
    WM_SD_EVENT_HELP_CLOSE,
    WM_SD_EVENT_PAGE_CHANGED,
    WM_SD_EVENT_HOVER_SOUND,
    WM_SD_EVENT_CONFIRM_SOUND,
    WM_SD_EVENT_CANCEL_SOUND,
    WM_SD_EVENT_PAGE_SOUND,
    WM_SD_EVENT_INFO_SOUND,
    WM_SD_EVENT_BALLOON_SOUND
} WmSdEventType;

typedef struct WmSdEvent {
    WmSdEventType type;
    unsigned slot;
} WmSdEvent;

typedef const char *(*WmSdMessageProvider)(void *context, unsigned message_id);

/* The caller owns the graphics platform and caches. The scene owns parsed
 * layouts and all remembered channel mappings. */
WmSdScene *wm_sd_scene_create(WmPlatform *platform,
                               const char *assets_directory,
                               WmTextureCache *textures,
                               WmFontCache *fonts);
void wm_sd_scene_destroy(WmSdScene *scene);
bool wm_sd_scene_set_channels(WmSdScene *scene,
                               const WmSdChannel *channels, size_t count);
const char *wm_sd_scene_channel_id(const WmSdScene *scene, unsigned slot);
void wm_sd_scene_set_messages(WmSdScene *scene, WmSdMessageProvider provider,
                               void *context);
void wm_sd_scene_set_card_ready(WmSdScene *scene, bool ready);

/* Open at the shared scene fader's black handoff. `revealing` in advance
 * stays true until that fader has finished opening; the first-visit guide
 * begins after it. The caller persists page and help_seen when they change. */
bool wm_sd_scene_open(WmSdScene *scene, unsigned page, bool help_seen,
                       WmSdMediaStatus media_status);
/* Close transient SD animations/dialogs/events on menu restart while keeping
 * channel mappings and the last page/help preference for a later reopen. */
void wm_sd_scene_reset(WmSdScene *scene);
void wm_sd_scene_advance(WmSdScene *scene, float frames, bool revealing);
WmSdPhase wm_sd_scene_phase(const WmSdScene *scene);
unsigned wm_sd_scene_page(const WmSdScene *scene);
bool wm_sd_scene_help_seen(const WmSdScene *scene);
bool wm_sd_scene_is_locked(const WmSdScene *scene);
bool wm_sd_scene_help_open(const WmSdScene *scene);
unsigned wm_sd_scene_help_page(const WmSdScene *scene);
bool wm_sd_scene_take_event(WmSdScene *scene, WmSdEvent *event);

/* Draw between the caller's platform begin/end. The scene renders grid
 * background, tiles, trim, page labels, footer, then modal dialogs. */
void wm_sd_scene_draw(WmSdScene *scene);
WmSdHit wm_sd_scene_hit(WmSdScene *scene, int x, int y);
void wm_sd_scene_hover(WmSdScene *scene, WmSdHit hit);
bool wm_sd_scene_activate(WmSdScene *scene, WmSdHit hit);
bool wm_sd_scene_back(WmSdScene *scene);

/* Pure page helpers are also used by the fixture and persistence adapters. */
bool wm_sd_page_can_move(unsigned page, int direction);
float wm_sd_scroll_animation_frame(int direction, float elapsed_frames);

#endif
