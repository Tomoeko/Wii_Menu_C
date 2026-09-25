#include "wii_menu/viewport.h"

#include <assert.h>

int main(void) {
    WmViewport wide = wm_viewport_fit(1200, 540);
    assert(wide.x == 120 && wide.y == 0);
    assert(wide.width == 960 && wide.height == 540);
    int x = -1, y = -1;
    assert(wm_viewport_map_pointer(wide, 120, 0, &x, &y));
    assert(x == 0 && y == 0);
    assert(wm_viewport_map_pointer(wide, 600, 270, &x, &y));
    assert(x == 320 && y == 228);
    assert(!wm_viewport_map_pointer(wide, 119, 270, &x, &y));
    assert(!wm_viewport_map_pointer(wide, 1080, 270, &x, &y));
    assert(!wm_viewport_map_pointer_unbounded(wide, 119, 270, &x, &y));
    assert(x == -1 && y == 228);
    assert(!wm_viewport_map_pointer_unbounded(wide, 1080, 270, &x, &y));
    assert(x == 640 && y == 228);
    assert(!wm_viewport_map_pointer_unbounded(wide, 600, -1, &x, &y));
    assert(x == 320 && y == -1);
    assert(!wm_viewport_map_pointer_unbounded(wide, 600, 540, &x, &y));
    assert(x == 320 && y == 456);
    WmViewport tall = wm_viewport_fit(960, 800);
    assert(tall.x == 0 && tall.y == 130);
    assert(tall.width == 960 && tall.height == 540);
    assert(!wm_viewport_map_pointer(tall, 10, 129, &x, &y));
    assert(!wm_viewport_map_pointer_unbounded(tall, 10, 129, &x, &y));
    assert(x == 6 && y == -1);
    assert(wm_viewport_map_pointer(tall, 480, 400, &x, &y));
    assert(x == 320 && y == 228);
    WmViewport native = wm_viewport_fit(960, 540);
    assert(native.x == 0 && native.y == 0);
    assert(native.width == 960 && native.height == 540);
    x = 17;
    y = 19;
    assert(!wm_viewport_map_pointer_unbounded((WmViewport){0}, 100, 100,
                                               &x, &y));
    assert(x == 17 && y == 19);
    return 0;
}
