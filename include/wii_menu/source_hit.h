#ifndef WII_MENU_SOURCE_HIT_H
#define WII_MENU_SOURCE_HIT_H

#include "wii_menu/layout_runtime.h"
#include "wii_menu/ui.h"

/* Pane bounds in the fixed 640 x 456 logical framebuffer. These are the
 * axis-aligned bounds of the four transformed source pane corners, matching
 * the HTML renderer's control geometry. */
typedef struct WmSourceRect {
    float x;
    float y;
    float width;
    float height;
} WmSourceRect;

/* Finds the last visible pane with this name, following source draw order.
 * Parent transforms, source origins, rotation, and widescreen scale are
 * handled by the layout runtime. A hidden or degenerate pane has no bounds. */
bool wm_source_pane_rect(const WmLayout *layout, const char *pane_name,
                         bool wide, WmLayoutMode mode,
                         const float parent_matrix[12],
                         WmSourceRect *rect);

/* ChannelSelect uses N_Ch_c01 through N_Ch_c12 as its button rectangles.
 * Other controls continue through the shared UI hit test until their source
 * layouts are part of the scene. `grid` must contain the current posed layout. */
WmHit wm_source_menu_hit(const WmLayout *grid, const WmMenu *menu,
                         int x, int y);

/* Return the absolute slot beneath a pointer, including empty channels.
 * The same authored N_Ch_c pane bounds drive selection and drag targets. */
int wm_source_menu_slot_at(const WmLayout *grid, const WmMenu *menu,
                           int x, int y);

#endif
