#ifndef WII_MENU_PREVIEW_SCENE_H
#define WII_MENU_PREVIEW_SCENE_H

#include "wii_menu/menu.h"
#include "wii_menu/platform.h"
#include "wii_menu/ui.h"
#include "wii_menu/font_cache.h"
#include "wii_menu/layout_runtime.h"
#include "wii_menu/texture_cache.h"
#include "wii_menu/pointer.h"

#include <stdbool.h>

typedef struct WmPreviewScene WmPreviewScene;

/* Load the System Menu's Disc Channel banner, ChannelTitle, background, and
 * common arrow layouts from a local WAD resource export. The scene owns its
 * layouts and texture cache; the caller retains the platform and directory. */
WmPreviewScene *wm_preview_scene_create(WmPlatform *platform,
                                         const char *assets_directory,
                                         const WmMenu *menu,
                                         WmTextureCache *textures,
                                         WmFontCache *fonts);
void wm_preview_scene_destroy(WmPreviewScene *scene);
void wm_preview_scene_move_channel(WmPreviewScene *scene, int from, int to);
void wm_preview_scene_set_module_lead(WmPreviewScene *scene, float frames);
bool wm_preview_scene_available(const WmPreviewScene *scene,
                                const WmMenu *menu);

/* Draw one complete preview frame. Time is measured from preview entry.
 * Returns false without touching the framebuffer when its banner is absent. */
bool wm_preview_scene_draw(WmPreviewScene *scene, const WmMenu *menu,
                            float preview_elapsed_seconds,
                            WmHit hover,
                            const WmPointer *pointer);

/* Render the complete preview inside a caller-owned frame. Parent matrix is a
 * source-space transform; NULL uses the full-screen projection. */
bool wm_preview_scene_draw_layers(WmPreviewScene *scene, const WmMenu *menu,
                                   float preview_elapsed_seconds,
                                   const float parent_matrix[12]);

/* The ChannelTitle zoom capture contains the preview body only. Common arrow
 * panes stay in the full-screen projection and draw after its black border. */
bool wm_preview_scene_draw_capture(WmPreviewScene *scene, const WmMenu *menu,
                                   float preview_elapsed_seconds);

/* Pose the first-party common arrow resource at a source Enter/End frame.
 * The caller chooses 4:3 or 16:9 when presenting this same layout. */
bool wm_preview_scene_pose_arrows(WmLayout *arrows, float frame,
                                  float loop_frame, bool exiting);

/* Draw the first ten frames of Back's outward arrows in a full-screen source
 * projection. Returns false when no return arrow is due. */
bool wm_preview_scene_draw_return_arrows(WmPreviewScene *scene,
                                         const WmMenu *menu,
                                         float loop_elapsed_seconds,
                                         bool wide);

/* Source pane hit regions for the WAD-backed Disc preview. */
WmHit wm_preview_scene_hit(const WmPreviewScene *scene, const WmMenu *menu,
                            int x, int y);
/* Retain an already focused arrow through the source's four-unit exit
 * tolerance. Click activation continues to use the exact hit region. */
WmHit wm_preview_scene_hover_hit(const WmPreviewScene *scene,
                                  const WmMenu *menu, int x, int y,
                                  WmHit held);

#endif
