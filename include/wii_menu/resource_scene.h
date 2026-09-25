#ifndef WII_MENU_RESOURCE_SCENE_H
#define WII_MENU_RESOURCE_SCENE_H

#include "wii_menu/menu.h"
#include "wii_menu/platform.h"
#include "wii_menu/font_cache.h"
#include "wii_menu/texture_cache.h"
#include "wii_menu/ui.h"
#include "wii_menu/layout_runtime.h"
#include "wii_menu/board_scene.h"

#include <stddef.h>

typedef struct WmResourceScene WmResourceScene;
typedef struct WmPointer WmPointer;
typedef struct WmPreviewScene WmPreviewScene;
typedef struct WmChannelDrag WmChannelDrag;

typedef struct WmResourceSceneFrame {
    float elapsed_seconds;
    float preview_elapsed_seconds;
    WmHit hover;
    bool suppress_balloons;
    const WmPointer *pointer;
    WmPreviewScene *preview_scene;
    WmBoardScene *board_scene;
    WmChannelDrag *drag;
} WmResourceSceneFrame;

typedef struct WmGridTile {
    int slot;
    int page_offset;
    float matrix[12];
    WmClipRect clip;
} WmGridTile;

/* Collect only source-anchored tiles that intersect the 640 x 456 raster.
 * Neighboring pages contribute visible edge strips but remain noninteractive.
 * The return value is the total count, even when capacity is smaller. */
size_t wm_resource_scene_collect_tiles(const WmLayout *grid, int page,
                                       WmGridTile *tiles, size_t capacity);

/* Load the local System Menu layout export and GPU texture cache. Returns
 * NULL when the directory does not contain the required grid resources. */
WmResourceScene *wm_resource_scene_create(WmPlatform *platform,
                                           const char *assets_directory,
                                           const WmMenu *menu,
                                           WmTextureCache *textures,
                                           WmFontCache *fonts);
void wm_resource_scene_destroy(WmResourceScene *scene);

/* Render the original background, channel masks, thumbnails and frames. The
 * caller retains menu state and input handling. */
void wm_resource_scene_draw(WmResourceScene *scene, const WmMenu *menu,
                            const WmResourceSceneFrame *frame);

/* Draw the grid into a caller-owned frame without drawing the pointer. HOME
 * uses this to retain the current scene in a GPU render target. */
void wm_resource_scene_draw_layers(WmResourceScene *scene,
                                   const WmMenu *menu,
                                   const WmResourceSceneFrame *frame);

/* Free the zoom preview's GPU capture once a stable scene is reached, so a
 * separate retained HOME underlay can use the GLES2 capture target. */
void wm_resource_scene_release_preview_capture(WmResourceScene *scene);

/* Draw the animated ChannelSelect grid over the Message Board during its
 * authored entry and exit. elapsed_seconds is the shared menu clock for icon
 * animation. The caller owns the frame and draws Board body/footer/pointer. */
void wm_resource_scene_draw_grid_overlay(WmResourceScene *scene,
                                         const WmMenu *menu,
                                         float layout_frame,
                                         float elapsed_seconds);

/* Draw only the grid's SD button inside an already active Board frame. */
void wm_resource_scene_draw_sd_button(WmResourceScene *scene,
                                      float elapsed_seconds);

/* The Board uses the grid's same source TextBalloon controller, timing, and
 * sound. Draw after the Board footer so its button anchors are posed. */
void wm_resource_scene_draw_board_balloons(WmResourceScene *scene,
                                            const WmBoardScene *board,
                                            WmBoardHit hover,
                                            float elapsed_seconds);

/* Hit testing follows the same source pane geometry as the rendered grid. */
WmHit wm_resource_scene_hit(const WmResourceScene *scene, const WmMenu *menu,
                            int x, int y);

/* The authored channel pane beneath the pointer, including empty slots. */
int wm_resource_scene_slot_at(const WmResourceScene *scene,
                              const WmMenu *menu, int x, int y);

/* Starts the authored pressed-arrow bubble for a page action. */
void wm_resource_scene_press_arrow(WmResourceScene *scene, int direction);
void wm_resource_scene_move_channel(WmResourceScene *scene, int from, int to);
bool wm_resource_scene_take_balloon_sound(WmResourceScene *scene);

/* Reverse the current text bubble on activation and keep it dismissed until
 * the pointer leaves that control. */
void wm_resource_scene_dismiss_balloon(WmResourceScene *scene);

/* Retire the Home footer focus when another scene takes ownership. The
 * pointer can start a fresh authored hover if it still covers that button
 * when Home returns. */
void wm_resource_scene_retire_footer_focus(WmResourceScene *scene);

/* A fresh pointer movement may reacquire a control after a global fade
 * cleared its bubble. Click dismissal still requires leaving the control. */
void wm_resource_scene_pointer_moved(WmResourceScene *scene);

/* Today's local Board count controls the second envelope and number under
 * it. new_mail enables the authored arrival cue until the Board is visited. */
void wm_resource_scene_set_message_badge(WmResourceScene *scene,
                                         unsigned today_count,
                                         bool new_mail);
bool wm_resource_scene_take_new_mail_sound(WmResourceScene *scene);

/* Returning from HOME starts a fresh ChannelSelect animation lifetime. */
void wm_resource_scene_restart(WmResourceScene *scene);

#endif
