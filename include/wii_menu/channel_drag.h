#ifndef WII_MENU_CHANNEL_DRAG_H
#define WII_MENU_CHANNEL_DRAG_H

#include "wii_menu/font_cache.h"
#include "wii_menu/menu.h"
#include "wii_menu/platform.h"
#include "wii_menu/texture_cache.h"

#include <stdbool.h>

typedef struct WmChannelDrag WmChannelDrag;

typedef enum WmChannelDragPhase {
    WM_CHANNEL_DRAG_NONE,
    WM_CHANNEL_DRAG_GRAB,
    WM_CHANNEL_DRAG_MOVING,
    WM_CHANNEL_DRAG_DROP_IN,
    WM_CHANNEL_DRAG_DROP_OUT,
    WM_CHANNEL_DRAG_CANCEL
} WmChannelDragPhase;

typedef enum WmChannelDragLayer {
    WM_CHANNEL_DRAG_MASK,
    WM_CHANNEL_DRAG_SHADE,
    WM_CHANNEL_DRAG_DROP
} WmChannelDragLayer;

typedef enum WmChannelDragSound {
    WM_CHANNEL_DRAG_SOUND_NONE,
    WM_CHANNEL_DRAG_SOUND_DROP,
    WM_CHANNEL_DRAG_SOUND_INVALID_DROP
} WmChannelDragSound;

/* Each length is the last playable frame, or animation.frames - 1. The
 * resource-backed constructor reads these from the exported WAD layouts. */
typedef struct WmChannelDragLengths {
    float mask_appear;
    float mask_lost;
    float shade_appear;
    float shade_lost;
    float drop_appear;
    float drop_lost;
} WmChannelDragLengths;

typedef struct WmChannelDragState {
    WmChannelDragPhase phase;
    int source;
    int target; /* -1 when the pointer is outside channel tiles. */
    float pointer_x; /* 640 x 456 framebuffer coordinates. */
    float pointer_y;
    int edge; /* -1, 0, or +1. */
    float edge_frames;
    float frame;
    float appearance_frame;
    float release_frame;
    bool release_started;
    bool pending_release;
    bool valid_drop;
} WmChannelDragState;

typedef struct WmChannelDragEvents {
    WmChannelDragSound sound;
    int page; /* -1, 0, or +1; caller starts the page transition. */
    bool move;
    int move_source;
    int move_target; /* Caller applies wm_menu_move_channel once. */
} WmChannelDragEvents;

typedef struct WmChannelDragPose {
    bool visible;
    const char *animation; /* Static source animation name. */
    float frame; /* Effective source frame, held at the authored endpoint. */
} WmChannelDragPose;

typedef struct WmChannelDragAudioParameters {
    float gain;
    float pan;
    float pitch;
    bool changes_pitch;
} WmChannelDragAudioParameters;

/* Loads the three authored chanSel layouts and prepares their materials.
 * Platform, texture cache, and font cache remain caller-owned. */
WmChannelDrag *wm_channel_drag_create(WmPlatform *platform,
                                       const char *assets_directory,
                                       WmTextureCache *textures,
                                       WmFontCache *fonts, bool wide);

/* Headless controller for deterministic replay and tests. It has no drawable
 * resources; source-backed presentation uses wm_channel_drag_create. */
WmChannelDrag *wm_channel_drag_create_controller(
    const WmChannelDragLengths *lengths);
void wm_channel_drag_destroy(WmChannelDrag *drag);

WmChannelDragState wm_channel_drag_state(const WmChannelDrag *drag);
bool wm_channel_drag_active(const WmChannelDrag *drag);
bool wm_channel_drag_start(WmChannelDrag *drag, int index, const WmMenu *menu,
                           float pointer_x, float pointer_y);
void wm_channel_drag_point(WmChannelDrag *drag, float pointer_x,
                           float pointer_y, int target, int edge);
WmChannelDragSound wm_channel_drag_release(WmChannelDrag *drag,
                                            const WmMenu *menu, bool scrolling);
void wm_channel_drag_cancel(WmChannelDrag *drag);
WmChannelDragEvents wm_channel_drag_advance(WmChannelDrag *drag, float frames,
                                              const WmMenu *menu,
                                              bool scrolling);

WmChannelDragPose wm_channel_drag_pose(const WmChannelDrag *drag,
                                       WmChannelDragLayer layer);

/* Mask and drop are drawn under the caller's channel tile clip, using the
 * centered source anchor. Shade is drawn after the grid, at the pointer. */
void wm_channel_drag_draw_mask(WmChannelDrag *drag, float anchor_x,
                                float anchor_y);
void wm_channel_drag_draw_drop(WmChannelDrag *drag, float anchor_x,
                                float anchor_y);
void wm_channel_drag_draw_shade(WmChannelDrag *drag);

/* System::holdSEwithPosDis from the USA 4.3 reference. Coordinates are
 * centered source projection units (x spans -416..416 at 16:9), with positive
 * y upward. Movement per frame drives gain and x drives stereo pan. */
WmChannelDragAudioParameters wm_channel_drag_audio_parameters(
    float pointer_x, float pointer_y, bool has_previous,
    float previous_x, float previous_y, float frames);

/* Adapter for the common 640 x 456 framebuffer pointer coordinates. */
WmChannelDragAudioParameters wm_channel_drag_audio_for_framebuffer(
    bool wide, float pointer_x, float pointer_y, bool has_previous,
    float previous_x, float previous_y, float frames);

#endif
