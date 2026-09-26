#ifndef WII_MENU_VIEWPORT_H
#define WII_MENU_VIEWPORT_H

#include <stdbool.h>

/* Top-left output rectangle for the menu's 16:9 presentation. The source
 * 640 x 456 raster is anamorphic and fills this rectangle. */
typedef struct WmViewport {
    int x;
    int y;
    int width;
    int height;
} WmViewport;

WmViewport wm_viewport_fit(int output_width, int output_height);
bool wm_viewport_map_pointer(WmViewport viewport, int x, int y,
                              int *menu_x, int *menu_y);
/* Writes logical coordinates even in the letterbox or outside the window.
 * Returns true only inside the fitted picture. Invalid viewports and output
 * pointers return false without writing coordinates. */
bool wm_viewport_map_pointer_unbounded(WmViewport viewport, int x, int y,
                                        int *menu_x, int *menu_y);

#endif
