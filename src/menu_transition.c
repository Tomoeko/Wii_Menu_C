#include "wii_menu/menu_transition.h"

#include <math.h>
#include <string.h>

enum { CHANNEL_ZOOM_FRAMES = 28 };

static float mix(float start, float end, float amount) {
    return start + (end - start) * amount;
}

static float clamp(float value, float low, float high) {
    return fminf(high, fmaxf(low, value));
}

static void set_affine(float matrix[12], float scale_x, float scale_y,
                       float translate_x, float translate_y) {
    memset(matrix, 0, sizeof(float) * 12);
    matrix[0] = scale_x;
    matrix[3] = translate_x;
    matrix[5] = scale_y;
    matrix[7] = translate_y;
    matrix[10] = 1.0f;
}

static void add_outside(WmChannelZoom *zoom, float x, float y,
                        float width, float height) {
    if (width <= 0.0f || height <= 0.0f) return;
    zoom->outside[zoom->outside_count++] = (WmTransitionRect){
        x, y, width, height
    };
}

bool wm_channel_zoom(float frame, bool reverse, float center_x,
                     float center_y, bool wide, WmChannelZoom *zoom) {
    if (!zoom || !isfinite(frame) || !isfinite(center_x) ||
        !isfinite(center_y)) return false;
    memset(zoom, 0, sizeof(*zoom));

    const float half_width = wide ? 416.0f : 304.0f;
    const float half_thumbnail_width = wide ? 85.0f : 64.0f;
    const float half_thumbnail_height = 48.0f;
    const float width = half_width * 2.0f;
    const float height = 456.0f;
    const float elapsed = clamp(frame, 0.0f, CHANNEL_ZOOM_FRAMES);
    const float normalized = (reverse ? CHANNEL_ZOOM_FRAMES - elapsed
                                      : elapsed) / CHANNEL_ZOOM_FRAMES;
    const float amount = normalized * normalized * (3.0f - 2.0f * normalized);

    const float thumbnail_left = center_x - half_thumbnail_width;
    const float thumbnail_right = center_x + half_thumbnail_width;
    const float thumbnail_top = center_y + half_thumbnail_height;
    const float thumbnail_bottom = center_y - half_thumbnail_height;
    const float camera_left = mix(-half_width, thumbnail_left, amount);
    const float camera_right = mix(half_width, thumbnail_right, amount);
    const float camera_top = mix(228.0f, thumbnail_top, amount);
    const float camera_bottom = mix(-228.0f, thumbnail_bottom, amount);
    const float scale_x = width / (camera_right - camera_left);
    const float scale_y = height / (camera_top - camera_bottom);
    const float translate_x = -(camera_left + camera_right) * 0.5f * scale_x;
    const float translate_y = -(camera_top + camera_bottom) * 0.5f * scale_y;
    set_affine(zoom->camera_matrix, scale_x, scale_y,
               translate_x, translate_y);

    const float thumbnail_scale_x = half_thumbnail_width * 2.0f / width;
    const float thumbnail_scale_y = half_thumbnail_height * 2.0f / height;
    set_affine(zoom->preview_matrix,
               scale_x * thumbnail_scale_x,
               scale_y * thumbnail_scale_y,
               scale_x * center_x + translate_x,
               scale_y * center_y + translate_y);

    const float left = thumbnail_left * scale_x + translate_x + half_width;
    const float top = 228.0f - (thumbnail_top * scale_y + translate_y);
    const float rect_width = half_thumbnail_width * 2.0f * scale_x;
    const float rect_height = half_thumbnail_height * 2.0f * scale_y;
    zoom->preview_rect = (WmTransitionRect){
        left, top, rect_width, rect_height
    };
    const float clipped_left = clamp(left, 0.0f, width);
    const float clipped_right = clamp(left + rect_width, 0.0f, width);
    const float clipped_top = clamp(top, 0.0f, height);
    const float clipped_bottom = clamp(top + rect_height, 0.0f, height);
    add_outside(zoom, 0.0f, 0.0f, width, clipped_top);
    add_outside(zoom, 0.0f, clipped_bottom, width,
                height - clipped_bottom);
    add_outside(zoom, 0.0f, clipped_top, clipped_left,
                clipped_bottom - clipped_top);
    add_outside(zoom, clipped_right, clipped_top, width - clipped_right,
                clipped_bottom - clipped_top);

    zoom->alpha = floorf(255.0f * amount) / 255.0f;
    zoom->amount = amount;
    zoom->layout_frame = 200.0f + (reverse ? CHANNEL_ZOOM_FRAMES - elapsed
                                            : elapsed);
    zoom->banner_starts = !reverse && elapsed >= CHANNEL_ZOOM_FRAMES;
    return true;
}
