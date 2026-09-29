#include "geometry.h"

#include <assert.h>

static void assert_corner(WmDrawVertex corner, float x, float y, float u, float v,
                          WmColor color) {
    assert(corner.x == x);
    assert(corner.y == y);
    assert(corner.u == u);
    assert(corner.v == v);
    assert(corner.color.r == color.r);
    assert(corner.color.g == color.g);
    assert(corner.color.b == color.b);
    assert(corner.color.a == color.a);
}

static void test_quad_corners(void) {
    const WmQuad quad = {.x = 12.5f,
                         .y = -3.0f,
                         .width = 8.0f,
                         .height = 5.5f,
                         .u0 = 0.125f,
                         .v0 = 0.25f,
                         .u1 = 0.75f,
                         .v1 = 0.875f,
                         .color = {0.25f, 0.5f, 0.75f, 1.0f}};
    WmDrawVertex corners[WM_QUAD_CORNERS];
    wm_render_quad_corners(&quad, corners);

    assert_corner(corners[0], 12.5f, -3.0f, 0.125f, 0.25f, quad.color);
    assert_corner(corners[1], 20.5f, -3.0f, 0.75f, 0.25f, quad.color);
    assert_corner(corners[2], 12.5f, 2.5f, 0.125f, 0.875f, quad.color);
    assert_corner(corners[3], 20.5f, 2.5f, 0.75f, 0.875f, quad.color);
}

static void test_signed_dimensions_and_uvs(void) {
    /* Extent policy belongs to the caller. Preserve flipped geometry and
     * image coordinates when producing corners for a drawable quad. */
    const WmQuad quad = {.x = 20.0f,
                         .y = 15.0f,
                         .width = -8.0f,
                         .height = -5.0f,
                         .u0 = 1.0f,
                         .v0 = 1.0f,
                         .u1 = 0.0f,
                         .v1 = 0.0f,
                         .color = {1.0f, 1.0f, 1.0f, 0.5f}};
    WmDrawVertex corners[WM_QUAD_CORNERS];
    wm_render_quad_corners(&quad, corners);

    assert_corner(corners[0], 20.0f, 15.0f, 1.0f, 1.0f, quad.color);
    assert_corner(corners[1], 12.0f, 15.0f, 0.0f, 1.0f, quad.color);
    assert_corner(corners[2], 20.0f, 10.0f, 1.0f, 0.0f, quad.color);
    assert_corner(corners[3], 12.0f, 10.0f, 0.0f, 0.0f, quad.color);
}

static float triangle_area(const WmDrawVertex corners[WM_QUAD_CORNERS],
                           unsigned first) {
    const WmDrawVertex a = corners[wm_quad_triangle_order[first]];
    const WmDrawVertex b = corners[wm_quad_triangle_order[first + 1]];
    const WmDrawVertex c = corners[wm_quad_triangle_order[first + 2]];
    return ((b.x - a.x) * (c.y - a.y) - (b.y - a.y) * (c.x - a.x)) * 0.5f;
}

static void test_quad_triangulation(void) {
    const WmQuad quad = {.width = 8.0f, .height = 5.0f};
    WmDrawVertex corners[WM_QUAD_CORNERS];
    wm_render_quad_corners(&quad, corners);

    /* The top-left to bottom-right diagonal matters when vertex attributes
     * differ across corners. Both triangles must preserve positive winding. */
    assert(wm_quad_triangle_order[0] == 0);
    assert(wm_quad_triangle_order[1] == 1);
    assert(wm_quad_triangle_order[2] == 3);
    assert(wm_quad_triangle_order[3] == 0);
    assert(wm_quad_triangle_order[4] == 3);
    assert(wm_quad_triangle_order[5] == 2);
    assert(triangle_area(corners, 0) == 20.0f);
    assert(triangle_area(corners, 3) == 20.0f);
}

int main(void) {
    test_quad_corners();
    test_signed_dimensions_and_uvs();
    test_quad_triangulation();
    return 0;
}
