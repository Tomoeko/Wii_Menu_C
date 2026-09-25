#ifndef WII_MENU_MENU_TRANSITION_H
#define WII_MENU_MENU_TRANSITION_H

#include <stdbool.h>
#include <stddef.h>

typedef struct WmTransitionRect {
    float x;
    float y;
    float width;
    float height;
} WmTransitionRect;

typedef struct WmChannelZoom {
    /* Layout-space affine matrices: x' = m[0]x + m[1]y + m[3],
     * y' = m[4]x + m[5]y + m[7]. */
    float camera_matrix[12];
    float preview_matrix[12];
    WmTransitionRect preview_rect;
    WmTransitionRect outside[4];
    size_t outside_count;
    float alpha;
    float amount;
    float layout_frame;
    bool banner_starts;
} WmChannelZoom;

/* Reproduce ChannelSelect's 28-frame Hermite zoom in the WAD's logical
 * projection. Rectangles use top-left screen coordinates in projection units.
 * `reverse` is the preview-to-grid path, not a second curve. */
bool wm_channel_zoom(float frame, bool reverse, float center_x,
                     float center_y, bool wide, WmChannelZoom *zoom);

#endif
