#ifndef WII_MENU_RENDER_GEOMETRY_H
#define WII_MENU_RENDER_GEOMETRY_H

#include "wii_menu/platform/platform.h"

enum { WM_QUAD_CORNERS = 4, WM_VERTICES_PER_QUAD = 6 };

/* Both backends expand corners in this order to preserve the same diagonal
 * and winding for basic and material quads. */
static const unsigned wm_quad_triangle_order[WM_VERTICES_PER_QUAD] = {0, 1, 3, 0, 3, 2};

/* Build left-top, right-top, left-bottom, right-bottom corners. The caller
 * decides whether the quad's dimensions are drawable for its backend. */
static inline void wm_render_quad_corners(const WmQuad *quad,
                                          WmDrawVertex corners[WM_QUAD_CORNERS]) {
    corners[0] = (WmDrawVertex){quad->x, quad->y, quad->u0, quad->v0, quad->color};
    corners[1] =
        (WmDrawVertex){quad->x + quad->width, quad->y, quad->u1, quad->v0, quad->color};
    corners[2] = (WmDrawVertex){quad->x, quad->y + quad->height, quad->u0, quad->v1,
                                quad->color};
    corners[3] = (WmDrawVertex){quad->x + quad->width, quad->y + quad->height, quad->u1,
                                quad->v1, quad->color};
}

#endif
