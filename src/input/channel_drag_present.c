#include "channel_drag_internal.h"

#include "wii_menu/layout/layout_present.h"
#include "wii_menu/layout/layout_runtime.h"
#include "wii_menu/render/material_prepare.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>

/* All visual resources live here; a headless controller leaves this NULL. */
struct WmChannelDragPresentation {
    WmPlatform *platform;
    WmTextureCache *textures;
    WmFontCache *fonts;
    WmLayout *mask;
    WmLayout *shade;
    WmLayout *drop;
    uint64_t posed_revision[3];
    bool pose_valid[3];
    bool wide;
};

enum { CHANNEL_DRAG_PATH_CAPACITY = 4096 };

static WmLayout *load_layout(const char *assets_directory, const char *name) {
    char path[CHANNEL_DRAG_PATH_CAPACITY];
    int count = snprintf(path, sizeof(path), "%s/layouts/chanSel/%s.json",
                         assets_directory, name);
    if (count < 0 || count >= (int)sizeof(path))
        return NULL;
    char error[160];
    return wm_layout_load_json(path, error, sizeof(error));
}

static bool animation_length(const WmLayout *layout, const char *name, float *length) {
    WmLayoutAnimationInfo info;
    if (!wm_layout_animation_info(layout, name, &info) || !isfinite(info.frames) ||
        info.frames < 1.0f)
        return false;
    *length = info.frames - 1.0f;
    return true;
}

WmChannelDragPresentation *
wm_channel_drag_presentation_create(WmPlatform *platform, const char *assets_directory,
                                    WmTextureCache *textures, WmFontCache *fonts,
                                    bool wide, WmChannelDragLengths *lengths) {
    if (!platform || !assets_directory || !assets_directory[0] || !textures ||
        !lengths) {
        return NULL;
    }
    WmChannelDragPresentation *presentation = calloc(1, sizeof(*presentation));
    if (!presentation)
        return NULL;
    presentation->platform = platform;
    presentation->textures = textures;
    presentation->fonts = fonts;
    presentation->wide = wide;
    presentation->mask = load_layout(assets_directory, "my_TVMask_a");
    presentation->shade = load_layout(assets_directory, "my_TVShade_a");
    presentation->drop = load_layout(assets_directory, "my_TVApear_a");
    WmChannelDragLengths authored_lengths = {0};
    if (!presentation->mask || !presentation->shade || !presentation->drop ||
        !animation_length(presentation->mask, "my_TVMask_a_Apear",
                          &authored_lengths.mask_appear) ||
        !animation_length(presentation->mask, "my_TVMask_a_Lost",
                          &authored_lengths.mask_lost) ||
        !animation_length(presentation->shade, "my_TVShade_a_Apear",
                          &authored_lengths.shade_appear) ||
        !animation_length(presentation->shade, "my_TVShade_a_Lost",
                          &authored_lengths.shade_lost) ||
        !animation_length(presentation->drop, "my_TVApear_a_Apear",
                          &authored_lengths.drop_appear) ||
        !animation_length(presentation->drop, "my_TVApear_a_Lost",
                          &authored_lengths.drop_lost) ||
        (wide &&
         (!wm_layout_copy_texture_map(presentation->shade, "16x9", "4x3", 0) ||
          !wm_layout_copy_texture_map(presentation->shade, "16x9", "4x3_dummy", 0)))) {
        wm_channel_drag_presentation_destroy(presentation);
        return NULL;
    }
    wm_layout_prepare_materials(platform, presentation->mask);
    wm_layout_prepare_materials(platform, presentation->shade);
    wm_layout_prepare_materials(platform, presentation->drop);
    *lengths = authored_lengths;
    return presentation;
}

void wm_channel_drag_presentation_destroy(WmChannelDragPresentation *presentation) {
    if (!presentation)
        return;
    wm_layout_destroy(presentation->mask);
    wm_layout_destroy(presentation->shade);
    wm_layout_destroy(presentation->drop);
    free(presentation);
}

WmChannelDrag *wm_channel_drag_create(WmPlatform *platform,
                                      const char *assets_directory,
                                      WmTextureCache *textures, WmFontCache *fonts,
                                      bool wide) {
    if (!platform || !assets_directory || !assets_directory[0] || !textures) {
        return NULL;
    }
    const WmChannelDragLengths zero = {0};
    WmChannelDrag *drag = wm_channel_drag_create_controller(&zero);
    if (!drag)
        return NULL;
    drag->presentation = wm_channel_drag_presentation_create(
        platform, assets_directory, textures, fonts, wide, &drag->lengths);
    if (!drag->presentation) {
        wm_channel_drag_destroy(drag);
        return NULL;
    }
    return drag;
}

static WmLayout *layer_layout(WmChannelDragPresentation *presentation,
                              WmChannelDragLayer layer) {
    switch (layer) {
        case WM_CHANNEL_DRAG_MASK:
            return presentation->mask;
        case WM_CHANNEL_DRAG_SHADE:
            return presentation->shade;
        case WM_CHANNEL_DRAG_DROP:
            return presentation->drop;
        default:
            return NULL;
    }
}

static void draw_layer(WmChannelDrag *drag, WmChannelDragLayer layer, float anchor_x,
                       float anchor_y) {
    if (!drag || !drag->presentation || !isfinite(anchor_x) || !isfinite(anchor_y))
        return;
    WmChannelDragPresentation *presentation = drag->presentation;
    WmLayout *layout = layer_layout(presentation, layer);
    WmChannelDragPose pose = wm_channel_drag_pose(drag, layer);
    if (!layout || !pose.visible || layer > WM_CHANNEL_DRAG_DROP)
        return;
    if (!presentation->pose_valid[layer] ||
        presentation->posed_revision[layer] != drag->revision) {
        const WmLayoutClip clip = {
            .animation = pose.animation, .frame = pose.frame, .loop_override = 0};
        if (!wm_layout_pose(layout, &clip, 1))
            return;
        presentation->pose_valid[layer] = true;
        presentation->posed_revision[layer] = drag->revision;
    }
    const float translation[12] = {1, 0, 0, anchor_x, 0, 1, 0, anchor_y, 0, 0, 1, 0};
    wm_layout_present_with_fonts(presentation->platform, presentation->textures,
                                 presentation->fonts, layout, presentation->wide,
                                 WM_LAYOUT_IPL, translation);
}

void wm_channel_drag_draw_mask(WmChannelDrag *drag, float anchor_x, float anchor_y) {
    draw_layer(drag, WM_CHANNEL_DRAG_MASK, anchor_x, anchor_y);
}

void wm_channel_drag_draw_drop(WmChannelDrag *drag, float anchor_x, float anchor_y) {
    draw_layer(drag, WM_CHANNEL_DRAG_DROP, anchor_x, anchor_y);
}

void wm_channel_drag_draw_shade(WmChannelDrag *drag) {
    if (!wm_channel_drag_active(drag) || !drag->presentation)
        return;
    float source_width = drag->presentation->wide ? 832.0f : 608.0f;
    draw_layer(drag, WM_CHANNEL_DRAG_SHADE,
               (drag->state.pointer_x / WM_FRAME_WIDTH - 0.5f) * source_width,
               WM_FRAME_HEIGHT * 0.5f - drag->state.pointer_y);
}
