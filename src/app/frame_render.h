#ifndef WM_APP_FRAME_RENDER_H
#define WM_APP_FRAME_RENDER_H

#include "input_routing.h"

#include "wii_menu/fonts/font_cache.h"
#include "wii_menu/input/channel_drag.h"
#include "wii_menu/input/pointer.h"
#include "wii_menu/layout/layout_runtime.h"
#include "wii_menu/menu/menu_restart.h"
#include "wii_menu/render/texture_cache.h"
#include "wii_menu/scenes/health_scene.h"
#include "wii_menu/scenes/home_overlay.h"
#include "wii_menu/scenes/preview_scene.h"
#include "wii_menu/scenes/sd_scene.h"
#include "wii_menu/scenes/storage_scene.h"

#include <stdbool.h>
#include <stdint.h>

/* Scene pointers are borrowed. The retained HOME render target belongs to this
 * renderer and must be released before destroying the platform. */
typedef struct WmAppRenderer {
    WmPlatform *platform;
    WmTextureCache *layout_textures;
    WmFontCache *layout_fonts;
    WmTextureCache *scene_textures;
    WmFontCache *scene_fonts;
    WmLayout *layout;
    const char *animation;
    bool hide_masks;
    WmHealthScene *health_scene;
    WmMenuRestartScene *restart_scene;
    WmHomeOverlay *home;
    WmResourceScene *resource_scene;
    WmPreviewScene *preview_scene;
    WmBoardScene *board_scene;
    WmOptionsScene *options_scene;
    WmSdScene *sd_scene;
    WmPointer *pointer;
    WmChannelDrag *drag;
    WmAudio *audio;
    uint32_t home_underlay_texture;
    bool home_underlay_valid;
} WmAppRenderer;

typedef struct WmAppRenderFrame {
    WmMenu *menu;
    const WmAppSceneFade *fade;
    const WmMenuRestartClock *restart;
    WmStorageScene *active_storage;
    WmHit hover;
    WmBoardHit board_hovered;
    bool health_frame;
    bool keyboard_focus;
    int focused_slot;
    uint64_t frame_start;
    uint64_t started;
    uint64_t preview_started;
    float home_underlay_elapsed;
    float home_underlay_preview_elapsed;
} WmAppRenderFrame;

void wm_app_renderer_invalidate_home_underlay(WmAppRenderer *renderer);
void wm_app_renderer_release_home_underlay(WmAppRenderer *renderer);
void wm_app_renderer_draw(WmAppRenderer *renderer, const WmAppRenderFrame *frame);

#endif
