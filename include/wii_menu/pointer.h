#ifndef WII_MENU_POINTER_H
#define WII_MENU_POINTER_H

#include "wii_menu/platform.h"
#include "wii_menu/texture_cache.h"
#include "wii_menu/channel_drag.h"

#include <stdbool.h>

typedef struct WmPointer WmPointer;

/* Load the source P1 cursor layouts from a local resource export. The texture
 * cache and platform remain caller-owned. A hidden pointer is created so a
 * window does not show the hand until a real pointer event arrives. */
WmPointer *wm_pointer_create(WmPlatform *platform, const char *assets_directory,
                             WmTextureCache *textures);
void wm_pointer_destroy(WmPointer *pointer);

/* Input and output use the shared 640 x 456 framebuffer. Source P1 geometry
 * supplies the hand and shadow offsets from the hotspot. There is no extra
 * hand-tip correction or motion interpolation in the HTML reference. */
void wm_pointer_move(WmPointer *pointer, float x, float y);
void wm_pointer_hide(WmPointer *pointer);
void wm_pointer_set_grabbed(WmPointer *pointer, bool grabbed);
/* The HTML drawPointer contract uses Cat only in its channel grab/drag
 * phases or while the Board is dragging a memo. Drop/cancel use Def. */
bool wm_pointer_grabbed_for_state(WmChannelDragPhase channel_phase,
                                  bool dragging_memo);

/* Draw after scene content, before the platform's end-frame call. The grabbed
 * resource is reserved for actual channel/memo drag, not ordinary presses. */
void wm_pointer_draw(const WmPointer *pointer);

/* Convert a framebuffer position to the parent transform of the source P1
 * layout. Exposed for hit geometry and aspect-ratio regression checks. */
void wm_pointer_matrix(float x, float y, float matrix[12]);

#endif
