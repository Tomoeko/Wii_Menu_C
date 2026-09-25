#include "wii_menu/viewport.h"
#include "wii_menu/platform.h"

#include <limits.h>
#include <stdint.h>

WmViewport wm_viewport_fit(int output_width, int output_height) {
    if (output_width <= 0 || output_height <= 0) return (WmViewport){0};
    int width = output_width;
    int height = output_height;
    if ((int64_t)output_width * 9 > (int64_t)output_height * 16) {
        width = (int)((int64_t)output_height * 16 / 9);
    } else {
        height = (int)((int64_t)output_width * 9 / 16);
    }
    if (width < 1) width = 1;
    if (height < 1) height = 1;
    return (WmViewport){(output_width - width) / 2,
                        (output_height - height) / 2, width, height};
}

bool wm_viewport_map_pointer(WmViewport viewport, int x, int y,
                              int *menu_x, int *menu_y) {
    if (!menu_x || !menu_y || viewport.width <= 0 || viewport.height <= 0 ||
        x < viewport.x || y < viewport.y ||
        (int64_t)x >= (int64_t)viewport.x + viewport.width ||
        (int64_t)y >= (int64_t)viewport.y + viewport.height) return false;
    return wm_viewport_map_pointer_unbounded(viewport, x, y,
                                              menu_x, menu_y);
}

static int floor_scaled_coordinate(int64_t output, int origin,
                                   int source_size, int output_size) {
    int64_t numerator = (output - origin) * source_size;
    int64_t value = numerator / output_size;
    if (numerator < 0 && numerator % output_size != 0) value--;
    if (value < INT_MIN) return INT_MIN;
    if (value > INT_MAX) return INT_MAX;
    return (int)value;
}

bool wm_viewport_map_pointer_unbounded(WmViewport viewport, int x, int y,
                                        int *menu_x, int *menu_y) {
    if (!menu_x || !menu_y || viewport.width <= 0 || viewport.height <= 0) {
        return false;
    }
    *menu_x = floor_scaled_coordinate(x, viewport.x, WM_FRAME_WIDTH,
                                      viewport.width);
    *menu_y = floor_scaled_coordinate(y, viewport.y, WM_FRAME_HEIGHT,
                                      viewport.height);
    return x >= viewport.x && y >= viewport.y &&
           (int64_t)x < (int64_t)viewport.x + viewport.width &&
           (int64_t)y < (int64_t)viewport.y + viewport.height;
}
