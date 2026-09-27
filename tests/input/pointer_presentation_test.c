#include "wii_menu/input/pointer.h"
#include "wii_menu/layout/layout_runtime.h"
#include "wii_menu/layout/layout_present.h"
#include "wii_menu/render/material_prepare.h"

#include <assert.h>
#include <math.h>
#include <stdlib.h>

static unsigned draw_count;
static float drawn_x;
static float drawn_y;

WmLayout *wm_layout_load_json(const char *path, char *error,
                              size_t error_capacity) {
    (void)path;
    (void)error;
    (void)error_capacity;
    return calloc(1, 1); /* Opaque layout token; the mock presenter never reads it. */
}

void wm_layout_destroy(WmLayout *layout) {
    free(layout);
}

void wm_layout_prepare_materials(WmPlatform *platform, const WmLayout *layout) {
    (void)platform;
    assert(layout);
}

void wm_layout_present(WmPlatform *platform, WmTextureCache *textures,
                        const WmLayout *layout, bool wide,
                        WmLayoutMode projection,
                        const float parent_matrix[12]) {
    (void)platform;
    (void)textures;
    (void)wide;
    assert(layout && projection == WM_LAYOUT_IPL);
    draw_count++;
    drawn_x = parent_matrix[3];
    drawn_y = parent_matrix[7];
}

int main(void) {
    WmPointer *pointer = wm_pointer_create((WmPlatform *)1, "synthetic-assets",
                                           (WmTextureCache *)1);
    assert(pointer);
    wm_pointer_draw(pointer);
    assert(draw_count == 0);
    /* Health/controller gates still retain the first active position. A
     * stationary stream need not produce another MOVE after acceptance. */
    WmEvent event = {.type = WM_EVENT_POINTER_MOVE, .x = 320, .y = 228};
    wm_pointer_apply_event(pointer, &event);
    wm_pointer_draw(pointer);
    assert(draw_count == 1 && fabsf(drawn_x) < 0.001f &&
           fabsf(drawn_y) < 0.001f);
    wm_pointer_apply_event(pointer, NULL);
    event.type = WM_EVENT_KEY_DOWN;
    wm_pointer_apply_event(pointer, &event);
    wm_pointer_draw(pointer);
    assert(draw_count == 2);
    event.type = WM_EVENT_POINTER_LEAVE;
    wm_pointer_apply_event(pointer, &event);
    wm_pointer_draw(pointer);
    assert(draw_count == 2);
    /* A click snapshot carries its own position even without a preceding
     * motion event, and disconnection/leave immediately hides it again. */
    event = (WmEvent){.type = WM_EVENT_POINTER_DOWN, .x = 640, .y = 456};
    wm_pointer_apply_event(pointer, &event);
    wm_pointer_draw(pointer);
    assert(draw_count == 3 && fabsf(drawn_x - 416.0f) < 0.001f &&
           fabsf(drawn_y + 228.0f) < 0.001f);
    event = (WmEvent){.type = WM_EVENT_POINTER_UP, .x = 320, .y = 228};
    wm_pointer_apply_event(pointer, &event);
    wm_pointer_draw(pointer);
    assert(draw_count == 4 && fabsf(drawn_x) < 0.001f &&
           fabsf(drawn_y) < 0.001f);
    wm_pointer_destroy(pointer);
    return 0;
}
