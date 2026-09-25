#include "wii_menu/pointer.h"

#include "wii_menu/layout_present.h"
#include "wii_menu/layout_runtime.h"
#include "wii_menu/material_prepare.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>

enum { WM_POINTER_PATH_CAPACITY = 4096 };

struct WmPointer {
    WmPlatform *platform;
    WmTextureCache *textures;
    WmLayout *default_layout;
    WmLayout *grabbed_layout;
    float x;
    float y;
    bool visible;
    bool grabbed;
};

static WmLayout *load_pointer_layout(const char *assets_directory,
                                      const char *name) {
    char path[WM_POINTER_PATH_CAPACITY];
    int length = snprintf(path, sizeof(path), "%s/layouts/cursor/%s.json",
                          assets_directory, name);
    if (length < 0 || length >= (int)sizeof(path)) return NULL;
    char error[160];
    return wm_layout_load_json(path, error, sizeof(error));
}

WmPointer *wm_pointer_create(WmPlatform *platform, const char *assets_directory,
                             WmTextureCache *textures) {
    if (!platform || !assets_directory || !assets_directory[0] || !textures) {
        return NULL;
    }
    WmPointer *pointer = calloc(1, sizeof(*pointer));
    if (!pointer) return NULL;
    pointer->platform = platform;
    pointer->textures = textures;
    pointer->default_layout = load_pointer_layout(assets_directory, "P1_Def");
    pointer->grabbed_layout = load_pointer_layout(assets_directory, "P1_Cat");
    if (!pointer->default_layout || !pointer->grabbed_layout) {
        wm_pointer_destroy(pointer);
        return NULL;
    }
    wm_layout_prepare_materials(platform, pointer->default_layout);
    wm_layout_prepare_materials(platform, pointer->grabbed_layout);
    return pointer;
}

void wm_pointer_destroy(WmPointer *pointer) {
    if (!pointer) return;
    wm_layout_destroy(pointer->default_layout);
    wm_layout_destroy(pointer->grabbed_layout);
    free(pointer);
}

void wm_pointer_move(WmPointer *pointer, float x, float y) {
    if (!pointer) return;
    if (!isfinite(x) || !isfinite(y)) {
        pointer->visible = false;
        return;
    }
    /* Captured channel drags keep their source coordinates beyond the fitted
     * picture. Normal pointer departures are hidden by the input router. */
    pointer->x = x;
    pointer->y = y;
    pointer->visible = true;
}

void wm_pointer_hide(WmPointer *pointer) {
    if (pointer) pointer->visible = false;
}

void wm_pointer_set_grabbed(WmPointer *pointer, bool grabbed) {
    if (pointer) pointer->grabbed = grabbed;
}

bool wm_pointer_grabbed_for_state(WmChannelDragPhase channel_phase,
                                  bool dragging_memo) {
    return channel_phase == WM_CHANNEL_DRAG_GRAB ||
           channel_phase == WM_CHANNEL_DRAG_MOVING || dragging_memo;
}

void wm_pointer_matrix(float x, float y, float matrix[12]) {
    if (!matrix) return;
    /* The source's 16:9 projection spans 832 logical X units. The render
     * target remains 640 pixels wide, so undo that raster scale here. */
    const float source_x = x * 832.0f / WM_FRAME_WIDTH - 416.0f;
    const float source_y = 228.0f - y;
    const float result[12] = {
        1, 0, 0, source_x,
        0, 1, 0, source_y,
        0, 0, 1, 0
    };
    for (size_t index = 0; index < 12; index++) matrix[index] = result[index];
}

void wm_pointer_draw(const WmPointer *pointer) {
    if (!pointer || !pointer->visible) return;
    float matrix[12];
    wm_pointer_matrix(pointer->x, pointer->y, matrix);
    wm_layout_present(pointer->platform, pointer->textures,
                      pointer->grabbed ? pointer->grabbed_layout
                                       : pointer->default_layout,
                      true, WM_LAYOUT_IPL, matrix);
}
