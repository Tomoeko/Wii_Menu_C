#define _POSIX_C_SOURCE 200809L

#include "resource_scene_internal.h"

#include "wii_menu/animation/channel_animation.h"
#include "wii_menu/animation/menu_transition.h"
#include "wii_menu/input/channel_drag.h"
#include "wii_menu/input/pointer.h"
#include "wii_menu/layout/layout_present.h"
#include "wii_menu/scenes/preview_scene.h"

#include <math.h>
#include <stdio.h>
#include <string.h>
#include <time.h>

typedef struct GridTraversal {
    WmGridTile tiles[5][WM_CHANNELS_PER_PAGE];
    bool has_tile[5][WM_CHANNELS_PER_PAGE];
    float clock_anchors[3][12];
    bool has_clock_anchor[3];
    int page;
    bool masks_only;
    float zoom_scale_x;
    float zoom_scale_y;
} GridTraversal;

static bool collect_clock_anchor(void *context, const WmLayoutPaneView *pane) {
    GridTraversal *traversal = context;
    const char *name = pane->name;
    if (strncmp(name, "N_Clock", 7) == 0 && name[7] >= '0' && name[7] <= '2' &&
        name[8] == '\0') {
        int index = name[7] - '0';
        memcpy(traversal->clock_anchors[index], pane->matrix,
               sizeof(traversal->clock_anchors[index]));
        traversal->has_clock_anchor[index] = true;
    }
    return true;
}

static bool grid_pane(void *context, const WmLayoutPaneView *pane) {
    GridTraversal *traversal = context;
    const char *name = pane->name;
    if (strncmp(name, "N_Ch_", 5) == 0 && name[5] >= 'a' && name[5] <= 'e' &&
        name[6] >= '0' && name[6] <= '9' && name[7] >= '0' && name[7] <= '9' &&
        name[8] == '\0') {
        int group = name[5] - 'a';
        int slot = (name[6] - '0') * 10 + (name[7] - '0') - 1;
        if (slot >= 0 && slot < WM_CHANNELS_PER_PAGE) {
            /* The source clips icons to 170 x 96 logical pixels at 16:9,
             * centered on the anchor; that is narrower than pane bounds. */
            const float scale = (float)WM_FRAME_WIDTH / 832.0f;
            const float width = 170.0f * scale * traversal->zoom_scale_x;
            const float center_x = WM_FRAME_WIDTH * 0.5f + pane->matrix[3] * scale;
            const float center_y = WM_FRAME_HEIGHT * 0.5f - pane->matrix[7];
            WmGridTile *tile = &traversal->tiles[group][slot];
            tile->page_offset = group - 2;
            memcpy(tile->matrix, pane->matrix, sizeof(tile->matrix));
            tile->clip = (WmClipRect){center_x - width * 0.5f,
                                      center_y - 48.0f * traversal->zoom_scale_y, width,
                                      96.0f * traversal->zoom_scale_y};
            traversal->has_tile[group][slot] = true;
        }
    }
    if (strcmp(pane->type, "pic1") != 0)
        return true;
    bool base_mask = strncmp(name, "BaseMask", 8) == 0;
    if (traversal->masks_only)
        return base_mask;
    if (base_mask || strcmp(name, "ChMask") == 0)
        return false;
    if (strncmp(name, "Edge", 4) == 0 && name[4] >= '0' && name[4] <= '4' &&
        name[5] == '\0') {
        int relative = (name[4] - '0') - 2;
        if (traversal->page + relative < 0 ||
            traversal->page + relative >= WM_PAGE_COUNT)
            return false;
    }
    return true;
}

static size_t visible_grid_tiles(const GridTraversal *traversal, WmGridTile *tiles,
                                 size_t capacity) {
    size_t count = 0;
    for (int group = 0; group < 5; group++) {
        int page = traversal->page + group - 2;
        if (page < 0 || page >= WM_PAGE_COUNT)
            continue;
        for (int index = 0; index < WM_CHANNELS_PER_PAGE; index++) {
            if (!traversal->has_tile[group][index])
                continue;
            WmGridTile tile = traversal->tiles[group][index];
            if (tile.clip.x + tile.clip.width <= 0.0f ||
                tile.clip.x >= WM_FRAME_WIDTH ||
                tile.clip.y + tile.clip.height <= 0.0f ||
                tile.clip.y >= WM_FRAME_HEIGHT)
                continue;
            tile.slot = page * WM_CHANNELS_PER_PAGE + index;
            if (count < capacity && tiles)
                tiles[count] = tile;
            count++;
        }
    }
    return count;
}

size_t wm_resource_scene_collect_tiles(const WmLayout *grid, int page,
                                       WmGridTile *tiles, size_t capacity) {
    if (!grid || page < 0 || page >= WM_PAGE_COUNT)
        return 0;
    GridTraversal traversal = {
        .page = page, .masks_only = true, .zoom_scale_x = 1.0f, .zoom_scale_y = 1.0f};
    WmLayoutDrawOptions options = {.wide = true,
                                   .mode = WM_LAYOUT_IPL,
                                   .alpha = 1.0f,
                                   .on_pane = grid_pane,
                                   .context = &traversal};
    wm_layout_draw(grid, &options);
    return visible_grid_tiles(&traversal, tiles, capacity);
}

static void pose_layout(WmLayout *layout, const char *animation, float frame) {
    WmLayoutClip clip = {.animation = animation, .frame = frame, .loop_override = -1};
    wm_layout_pose(layout, &clip, 1);
}

static bool sd_button_pane(void *context, const WmLayoutPaneView *pane) {
    (void)context;
    return strcmp(pane->name, "N_Btn_Off") != 0;
}

/* The 16:9 IPL projection places the SD button at X=-245. The 4:3
 * position is X=-152; applying it here moves the button toward the center. */
const float wm_resource_scene_sd_position[12] = {1, 0,    0, -245, 0, 1,
                                                 0, -172, 0, 0,    1, 0};

static bool clock_pane(void *context, const WmLayoutPaneView *pane) {
    int hour = *(const int *)context;
    if (strncmp(pane->name, "Num", 3) == 0 || strcmp(pane->name, "AM") == 0 ||
        strcmp(pane->name, "PM") == 0 || strcmp(pane->name, "AM_PM") == 0)
        return false;
    if (strcmp(pane->name, "Clock3") == 0 && hour < 10)
        return false;
    return true;
}

static void update_clock_textures(WmResourceScene *scene, int hour, int minute,
                                  bool afternoon) {
    if (scene->clock_hour == hour && scene->clock_minute == minute)
        return;
    static const char *const target[] = {"Clock0", "Clock1", "Clock2", "Clock3"};
    const int digits[] = {minute % 10, minute / 10, hour % 10, hour / 10};
    char donor[16];
    for (size_t index = 0; index < 4; index++) {
        snprintf(donor, sizeof(donor), "Num%d", digits[index]);
        wm_layout_copy_texture_map(scene->clock, donor, target[index], 0);
    }
    wm_layout_copy_texture_map(scene->clock, afternoon ? "PM" : "AM", "AM_PM_R", 0);
    scene->clock_hour = hour;
    scene->clock_minute = minute;
}

static void update_background_date(WmResourceScene *scene, const struct tm *date) {
    if (scene->date_year == date->tm_year && scene->date_month == date->tm_mon &&
        scene->date_day == date->tm_mday)
        return;
    static const char *const weekdays[] = {"Sun", "Mon", "Tue", "Wed",
                                           "Thu", "Fri", "Sat"};
    if (date->tm_wday < 0 || date->tm_wday >= 7)
        return;
    char label[32];
    snprintf(label, sizeof(label), "%s %d/%d", weekdays[date->tm_wday],
             date->tm_mon + 1, date->tm_mday);
    wm_layout_set_text(scene->background, "T_Day_a", label);
    wm_layout_set_text(scene->background, "T_Day_b", label);
    wm_layout_set_text(scene->background, "T_Day_c", label);
    scene->date_year = date->tm_year;
    scene->date_month = date->tm_mon;
    scene->date_day = date->tm_mday;
}

static bool channel_mask_pane(void *context, const WmLayoutPaneView *pane) {
    (void)context;
    return strcmp(pane->name, "RootPane") == 0 || strcmp(pane->name, "ChMask") == 0;
}

static bool footer_without_arrows(void *context, const WmLayoutPaneView *pane) {
    (void)context;
    return strcmp(pane->name, "N_ArwL") != 0 && strcmp(pane->name, "N_ArwR") != 0;
}

static bool footer_arrows_only(void *context, const WmLayoutPaneView *pane) {
    (void)context;
    const char *name = pane->name;
    return strcmp(name, "RootPane") == 0 || strcmp(name, "N_TopBtn") == 0 ||
           strcmp(name, "N_BtnL") == 0 || strcmp(name, "N_BtnR") == 0 ||
           strncmp(name, "N_Arw", 5) == 0 || strncmp(name, "Arw", 3) == 0 ||
           strncmp(name, "Taba", 4) == 0 || strncmp(name, "B_Arw", 5) == 0;
}

static void draw_captured_preview(WmResourceScene *scene, const WmChannelZoom *zoom) {
    const float *matrix = zoom->preview_matrix;
    const float half_width = 416.0f;
    const float half_height = 228.0f;
    const float scale_x = (float)WM_FRAME_WIDTH / 832.0f;
    const float positions[4][2] = {{-half_width, half_height},
                                   {half_width, half_height},
                                   {-half_width, -half_height},
                                   {half_width, -half_height}};
    const float uv[4][2] = {{0, 0}, {1, 0}, {0, 1}, {1, 1}};
    WmDrawVertex vertices[4];
    for (size_t index = 0; index < 4; index++) {
        float world_x = matrix[0] * positions[index][0] + matrix[3];
        float world_y = matrix[5] * positions[index][1] + matrix[7];
        vertices[index] = (WmDrawVertex){.x = WM_FRAME_WIDTH * 0.5f + world_x * scale_x,
                                         .y = WM_FRAME_HEIGHT * 0.5f - world_y,
                                         .u = uv[index][0],
                                         .v = uv[index][1],
                                         .color = {1, 1, 1, zoom->alpha}};
    }
    wm_platform_draw_vertices(scene->platform, vertices, scene->preview_capture);
    for (size_t index = 0; index < zoom->outside_count; index++) {
        const WmTransitionRect *rect = &zoom->outside[index];
        WmQuad shade = {.x = rect->x * scale_x,
                        .y = rect->y,
                        .width = rect->width * scale_x,
                        .height = rect->height,
                        .u0 = 0,
                        .v0 = 0,
                        .u1 = 1,
                        .v1 = 1,
                        .color = {0, 0, 0, zoom->alpha},
                        .texture = 0};
        wm_platform_draw_quad(scene->platform, &shade);
    }
}

static void draw_grid_thumbnails(WmResourceScene *scene, const WmMenu *menu,
                                 const GridTraversal *traversal, float elapsed_seconds,
                                 WmChannelDrag *drag) {
    WmGridTile visible[5 * WM_CHANNELS_PER_PAGE];
    size_t tile_count =
        visible_grid_tiles(traversal, visible, sizeof(visible) / sizeof(visible[0]));
    WmChannelDragState drag_state = wm_channel_drag_state(drag);
    bool dragging = drag_state.phase != WM_CHANNEL_DRAG_NONE;
    for (size_t tile_index = 0; tile_index < tile_count; tile_index++) {
        const WmGridTile *tile = &visible[tile_index];
        int absolute = tile->slot;
        int index = absolute % WM_CHANNELS_PER_PAGE;
        WmLayout *thumbnail = scene->empty_channel;
        const char *animation = "my_IplTop_b";
        bool moving_origin = dragging && absolute == drag_state.source &&
                             (drag_state.phase == WM_CHANNEL_DRAG_GRAB ||
                              drag_state.phase == WM_CHANNEL_DRAG_MOVING ||
                              drag_state.phase == WM_CHANNEL_DRAG_DROP_IN);
        if (!moving_origin && absolute == 0 && menu->slots[0].occupied) {
            thumbnail = scene->disc_channel;
            animation = "my_DiskCh_b";
        } else if (!moving_origin && absolute > 0 && scene->channel_icons[absolute]) {
            thumbnail = scene->channel_icons[absolute];
            animation = scene->channel_animations[absolute];
        }
        float icon_frame = elapsed_seconds * 60.0f +
                           (thumbnail == scene->empty_channel ? index * 83.0f : 0.0f);
        if (thumbnail == scene->channel_icons[absolute] && absolute > 0) {
            const WmChannelAnimationOptions icon_options = {
                .language = "ENG",
                .measure_text = wm_font_cache_measure_text,
                .measure_context = scene->fonts};
            wm_channel_animation_pose(thumbnail, menu->slots[absolute].id,
                                      WM_CHANNEL_ICON, icon_frame, &icon_options);
        } else {
            pose_layout(thumbnail, animation, icon_frame);
        }
        wm_platform_set_clip(scene->platform, &tile->clip);
        wm_layout_present_with_fonts(scene->platform, scene->textures, scene->fonts,
                                     thumbnail, true, WM_LAYOUT_EMBEDDED, tile->matrix);
        if (dragging && menu->slots[absolute].occupied &&
            absolute != drag_state.source) {
            wm_channel_drag_draw_mask(drag, tile->matrix[3], tile->matrix[7]);
        }
        if (dragging && drag_state.target == absolute) {
            wm_channel_drag_draw_drop(drag, tile->matrix[3], tile->matrix[7]);
        }
        wm_platform_set_clip(scene->platform, NULL);
    }
}

/* ChannelSelect owns the clock on the grid and during the Board handoff.
 * Both paths use the same state so the intro and colon phase cannot restart
 * when draw ownership moves between scenes. */
static void draw_grid_clock(WmResourceScene *scene, const GridTraversal *traversal,
                            float elapsed_seconds, const float *camera,
                            const struct timespec *wall_time, const struct tm *date) {
    if (!wall_time || !date ||
        !(traversal->has_clock_anchor[0] || traversal->has_clock_anchor[1] ||
          traversal->has_clock_anchor[2]))
        return;

    int hour = date->tm_hour % 12;
    if (hour == 0)
        hour = 12;
    update_clock_textures(scene, hour, date->tm_min, date->tm_hour >= 12);
    if (scene->clock_change_start < 0.0f && elapsed_seconds >= 3.0f &&
        (date->tm_sec % 2) != 0) {
        scene->clock_change_start = elapsed_seconds;
    }
    float change_frame = scene->clock_change_start < 0.0f
                             ? 0.0f
                             : (elapsed_seconds - scene->clock_change_start) * 60.0f;
    if (change_frame > scene->clock_change_frames) {
        change_frame = scene->clock_change_frames;
    }
    WmLayoutClip clock_clips[3] = {{.animation = "my_Clock_a_Change",
                                    .frame = change_frame,
                                    .loop_override = 0,
                                    .target_name = "T_WiiMenu"},
                                   {.animation = "my_Clock_a_Change",
                                    .frame = change_frame,
                                    .loop_override = 0,
                                    .target_name = "N_Clock"}};
    size_t clock_clip_count = 2;
    if (scene->clock_change_start >= 0.0f) {
        int millisecond = (int)(wall_time->tv_nsec / 1000000L);
        float blink_frame =
            ((float)(date->tm_sec % 2) + (float)millisecond / 1000.0f) * 60.0f;
        if (blink_frame > scene->clock_blink_frames) {
            blink_frame = scene->clock_blink_frames;
        }
        clock_clips[clock_clip_count++] = (WmLayoutClip){.animation = "my_Clock_a_Min",
                                                         .frame = blink_frame,
                                                         .loop_override = 0,
                                                         .target_name = "ClockTen"};
    }
    wm_layout_pose(scene->clock, clock_clips, clock_clip_count);
    for (size_t index = 0; index < 3; index++) {
        if (!traversal->has_clock_anchor[index])
            continue;
        float clock_position[12] = {camera ? camera[0] : 1.0f,
                                    0,
                                    0,
                                    traversal->clock_anchors[index][3],
                                    0,
                                    camera ? camera[5] : 1.0f,
                                    0,
                                    traversal->clock_anchors[index][7],
                                    0,
                                    0,
                                    1,
                                    0};
        wm_layout_present_filtered_with_fonts(
            scene->platform, scene->textures, scene->fonts, scene->clock, true,
            WM_LAYOUT_IPL, clock_position, clock_pane, &hour);
    }
}

void wm_resource_scene_draw_grid_overlay(WmResourceScene *scene, const WmMenu *menu,
                                         float layout_frame, float elapsed_seconds) {
    if (!scene || !menu || menu->page < 0 || menu->page >= WM_PAGE_COUNT)
        return;
    pose_layout(scene->grid, "my_IplTop_a", layout_frame);
    GridTraversal traversal = {.page = menu->page,
                               .masks_only = true,
                               .zoom_scale_x = 1.0f,
                               .zoom_scale_y = 1.0f};
    wm_layout_visit_all_transforms(scene->grid, true, WM_LAYOUT_IPL, NULL,
                                   collect_clock_anchor, &traversal);
    wm_layout_present_filtered_with_fonts(scene->platform, scene->textures,
                                          scene->fonts, scene->grid, true,
                                          WM_LAYOUT_IPL, NULL, grid_pane, &traversal);
    draw_grid_thumbnails(scene, menu, &traversal, elapsed_seconds, NULL);
    traversal.masks_only = false;
    wm_layout_present_filtered_with_fonts(scene->platform, scene->textures,
                                          scene->fonts, scene->grid, true,
                                          WM_LAYOUT_IPL, NULL, grid_pane, &traversal);
    struct timespec wall_time = {0};
    struct tm date;
    if (clock_gettime(CLOCK_REALTIME, &wall_time) == 0 &&
        localtime_r(&wall_time.tv_sec, &date) != NULL) {
        draw_grid_clock(scene, &traversal, elapsed_seconds, NULL, &wall_time, &date);
    }
}

static void draw_sd_button(WmResourceScene *scene, const WmMenu *menu,
                           float elapsed_seconds, const float *camera,
                           float visibility_frame) {
    bool hovered = menu && scene->sd_hovered;
    float hover_frame =
        fminf(6.0f, fmaxf(0.0f, elapsed_seconds * 60.0f - scene->sd_hover_changed_at));
    WmLayoutClip clips[4] = {{.animation = "mn_Sdcard_Btn_On_Roop",
                              .frame = elapsed_seconds * 60.0f,
                              .group = "On_Roop",
                              .loop_override = 1},
                             {.animation = "mn_Sdcard_Btn_BtnL_Out",
                              .frame = visibility_frame,
                              .group = "Btn_L_InOut",
                              .loop_override = 0},
                             {.animation = hovered ? "mn_Sdcard_Btn_BtnL_RollOver"
                                                   : "mn_Sdcard_Btn_BtnL_RollOut",
                              .frame = hover_frame,
                              .group = "Btn_L_Roll",
                              .loop_override = 0}};
    size_t clip_count = 3;
    if (menu && menu->transition == WM_TRANSITION_SETTINGS &&
        menu->screen == WM_SCREEN_SD) {
        clips[clip_count++] =
            (WmLayoutClip){.animation = "mn_Sdcard_Btn_BtnL_On",
                           .frame = fminf(20.0f, menu->transition_elapsed * 60.0f),
                           .group = "Btn_L_On",
                           .loop_override = 0};
    }
    wm_layout_pose(scene->sd_button, clips, clip_count);
    float zoom_position[12];
    const float *position = wm_resource_scene_sd_position;
    if (camera) {
        memcpy(zoom_position, camera, sizeof(zoom_position));
        zoom_position[3] = camera[0] * wm_resource_scene_sd_position[3] + camera[3];
        zoom_position[7] = camera[5] * wm_resource_scene_sd_position[7] + camera[7];
        position = zoom_position;
    }
    wm_layout_present_filtered_with_fonts(
        scene->platform, scene->textures, scene->fonts, scene->sd_button, true,
        WM_LAYOUT_IPL, position, sd_button_pane, NULL);
}

void wm_resource_scene_draw_sd_button(WmResourceScene *scene, float elapsed_seconds) {
    if (!scene)
        return;
    if (scene->board_sd_reveal_started_at < 0.0f) {
        scene->board_sd_reveal_started_at = elapsed_seconds;
    }
    float visibility_frame = fmaxf(
        0.0f, 15.0f - (elapsed_seconds - scene->board_sd_reveal_started_at) * 60.0f);
    draw_sd_button(scene, NULL, elapsed_seconds, NULL, visibility_frame);
}

static bool capture_matches(const WmResourceScene *scene, const WmMenu *menu, int slot,
                            int date) {
    if (!scene->capture_valid || scene->capture_slot != slot ||
        scene->capture_date != date)
        return false;
    const WmChannel *current = &menu->slots[slot];
    const WmChannel *cached = &scene->capture_channel;
    return current->occupied == cached->occupied &&
           strcmp(current->id, cached->id) == 0 &&
           strcmp(current->title, cached->title) == 0 &&
           strcmp(current->banner_layout, cached->banner_layout) == 0;
}

static bool capture_preview(WmResourceScene *scene, const WmMenu *menu,
                            WmPreviewScene *preview, int slot, int date,
                            float preview_seconds) {
    if (!preview || slot < 0 || slot >= WM_SLOT_COUNT)
        return false;
    if (!scene->preview_capture)
        scene->preview_capture = wm_platform_create_render_texture(scene->platform);
    if (!scene->preview_capture ||
        !wm_platform_begin_target(scene->platform, scene->preview_capture,
                                  (WmColor){0.92f, 0.92f, 0.92f, 1.0f})) {
        scene->capture_valid = false;
        return false;
    }
    scene->capture_valid =
        wm_preview_scene_draw_capture(preview, menu, preview_seconds);
    wm_platform_end(scene->platform);
    if (scene->capture_valid) {
        scene->capture_slot = slot;
        scene->capture_date = date;
        scene->capture_channel = menu->slots[slot];
    }
    return scene->capture_valid;
}

static void draw_resource_scene(WmResourceScene *scene, const WmMenu *menu,
                                const WmResourceSceneFrame *frame, bool owns_frame) {
    if (!scene || !menu || !frame)
        return;
    float elapsed_seconds = frame->elapsed_seconds;
    scene->board_sd_reveal_started_at = -1.0f;
    WmHit hover = frame->hover;
    wm_resource_scene_advance_interactions(scene, menu, elapsed_seconds, hover,
                                           frame->suppress_balloons);
    WmGridPresentation presentation = wm_menu_grid_presentation(menu);
    WmChannelZoom zoom = {0};
    const float *camera = NULL;
    if (presentation.zooming && presentation.zoom_slot >= 0 &&
        presentation.zoom_slot < WM_SLOT_COUNT) {
        pose_layout(scene->grid, "my_IplTop_a", 0.0f);
        GridTraversal anchor = {.page = presentation.page,
                                .masks_only = true,
                                .zoom_scale_x = 1.0f,
                                .zoom_scale_y = 1.0f};
        WmLayoutDrawOptions options = {.wide = true,
                                       .mode = WM_LAYOUT_IPL,
                                       .alpha = 1.0f,
                                       .on_pane = grid_pane,
                                       .context = &anchor};
        wm_layout_draw(scene->grid, &options);
        int index = presentation.zoom_slot % WM_CHANNELS_PER_PAGE;
        if (anchor.has_tile[2][index] &&
            wm_channel_zoom(wm_menu_transition_frame(menu), presentation.zoom_out,
                            anchor.tiles[2][index].matrix[3],
                            anchor.tiles[2][index].matrix[7], true, &zoom)) {
            camera = zoom.camera_matrix;
        }
    }
    wm_texture_cache_begin_frame(scene->textures);
    wm_font_cache_begin_frame(scene->fonts);

    struct timespec wall_time = {0};
    struct tm date;
    bool has_date = clock_gettime(CLOCK_REALTIME, &wall_time) == 0 &&
                    localtime_r(&wall_time.tv_sec, &date) != NULL;
    int capture_date = has_date ? date.tm_year * 1000 + date.tm_yday : -1;
    bool can_prepare =
        !presentation.zooming && menu->screen == WM_SCREEN_GRID &&
        menu->transition == WM_TRANSITION_NONE && !menu->home_open &&
        !menu->notice[0] && !frame->suppress_balloons && has_date &&
        frame->preview_scene &&
        wm_channel_drag_state(frame->drag).phase == WM_CHANNEL_DRAG_NONE &&
        frame->hover.type == WM_HIT_CHANNEL && frame->hover.slot >= 0 &&
        frame->hover.slot < WM_SLOT_COUNT && menu->slots[frame->hover.slot].occupied;
    if (can_prepare) {
        int slot = frame->hover.slot;
        if (scene->capture_hover_slot != slot) {
            scene->capture_hover_slot = slot;
            scene->capture_hover_started = elapsed_seconds;
        }
        if (!capture_matches(scene, menu, slot, capture_date)) {
            scene->capture_valid = false;
            /* One stable pointer update moves the offscreen work out of the
             * first SELECT frame without capturing every tile crossed. */
            if (elapsed_seconds - scene->capture_hover_started >= 1.0f / 60.0f) {
                WmMenu preview_menu = *menu;
                preview_menu.screen = WM_SCREEN_PREVIEW;
                preview_menu.selected = slot;
                preview_menu.transition = WM_TRANSITION_SELECT;
                if (wm_preview_scene_available(frame->preview_scene, &preview_menu)) {
                    capture_preview(scene, &preview_menu, frame->preview_scene, slot,
                                    capture_date, 0.0f);
                }
            }
        }
    } else {
        scene->capture_hover_slot = -1;
        if (!presentation.zooming)
            scene->capture_valid = false;
    }
    bool captured = false;
    if (camera && frame->preview_scene) {
        if (presentation.zoom_out ||
            !capture_matches(scene, menu, presentation.zoom_slot, capture_date))
            capture_preview(
                scene, menu, frame->preview_scene, presentation.zoom_slot, capture_date,
                presentation.zoom_out ? frame->preview_elapsed_seconds : 0.0f);
        captured = scene->capture_valid;
        /* BACK keeps its advancing capture for this draw, but that late pose
         * must not become the cached frame for the next SELECT zoom. */
        if (presentation.zoom_out)
            scene->capture_valid = false;
    }
    if (owns_frame)
        wm_platform_begin(scene->platform, (WmColor){0.92f, 0.92f, 0.92f, 1.0f});

    if (has_date)
        update_background_date(scene, &date);

    pose_layout(scene->background, "my_IplTop_c", 0);
    wm_layout_present_with_fonts(scene->platform, scene->textures, scene->fonts,
                                 scene->background, true, WM_LAYOUT_IPL, camera);

    if (frame->board_scene && has_date) {
        WmBoardDate today = {
            .year = date.tm_year + 1900, .month = date.tm_mon + 1, .day = date.tm_mday};
        wm_board_scene_draw_parked_memos(frame->board_scene, today, camera);
    }

    pose_layout(scene->grid, "my_IplTop_a", presentation.layout_frame);
    GridTraversal traversal = {.page = presentation.page,
                               .masks_only = true,
                               .zoom_scale_x = camera ? camera[0] : 1.0f,
                               .zoom_scale_y = camera ? camera[5] : 1.0f};
    /* Native clock::draw reads all authored anchor matrices, even when a
     * page animation hides their parent from the visible grid traversal. */
    wm_layout_visit_all_transforms(scene->grid, true, WM_LAYOUT_IPL, camera,
                                   collect_clock_anchor, &traversal);
    wm_layout_present_filtered_with_fonts(scene->platform, scene->textures,
                                          scene->fonts, scene->grid, true,
                                          WM_LAYOUT_IPL, camera, grid_pane, &traversal);

    WmChannelDragState drag_state = wm_channel_drag_state(frame->drag);
    bool dragging = drag_state.phase != WM_CHANNEL_DRAG_NONE;
    draw_grid_thumbnails(scene, menu, &traversal, elapsed_seconds, frame->drag);

    traversal.masks_only = false;
    wm_layout_present_filtered_with_fonts(scene->platform, scene->textures,
                                          scene->fonts, scene->grid, true,
                                          WM_LAYOUT_IPL, camera, grid_pane, &traversal);

    if (has_date) {
        draw_grid_clock(scene, &traversal, elapsed_seconds, camera, &wall_time, &date);
    }

    if (menu->transition == WM_TRANSITION_SELECT && menu->selected >= 0 &&
        menu->selected < WM_SLOT_COUNT) {
        int index = menu->selected % WM_CHANNELS_PER_PAGE;
        if (traversal.has_tile[2][index]) {
            float frame = menu->transition_elapsed * 60.0f;
            if (frame < 16.0f) {
                WmLayoutClip select_clip = {.animation = "my_IplTop_d_Select",
                                            .frame = frame,
                                            .loop_override = 0};
                wm_layout_pose(scene->focus, &select_clip, 1);
                wm_layout_present_with_fonts(
                    scene->platform, scene->textures, scene->fonts, scene->focus, true,
                    WM_LAYOUT_EMBEDDED, traversal.tiles[2][index].matrix);
            }
        }
    } else {
        for (int index = 0; index < WM_CHANNELS_PER_PAGE; index++) {
            int absolute = presentation.page * WM_CHANNELS_PER_PAGE + index;
            FocusAnimation *focus = &scene->focus_states[absolute];
            if (!focus->active || !traversal.has_tile[2][index])
                continue;
            WmLayoutClip focus_clip = {.animation = focus->off_phase
                                                        ? "my_IplTop_d_FocusOff"
                                                        : "my_IplTop_d_FocusOn",
                                       .frame = focus->frame,
                                       .loop_override = 0};
            wm_layout_pose(scene->focus, &focus_clip, 1);
            wm_layout_present_with_fonts(scene->platform, scene->textures, scene->fonts,
                                         scene->focus, true, WM_LAYOUT_EMBEDDED,
                                         traversal.tiles[2][index].matrix);
        }
    }

    /* Keep the carried channel beneath the common footer arrows. The grab
     * hand itself is the final overlay, like the ordinary pointer. */
    if (dragging) {
        wm_channel_drag_draw_shade(frame->drag);
    }

    WmLayoutClip footer_clips[12] = {
        {.animation = "my_IplTop_e",
         .frame = 0,
         .group = "G_SeenChange",
         .loop_override = 0},
        {.animation = "my_IplTop_e",
         .frame = 10000.0f + (float)((int)(elapsed_seconds * 60.0f) % 55),
         .group = "G_ArwRoop",
         .loop_override = 0},
        {.animation = "my_IplTop_e",
         .frame = (scene->arrow_visible[0] ? 10150.0f : 10100.0f) +
                  fminf(scene->arrow_age[0], 10.0f),
         .group = "G_ArwL_End",
         .loop_override = 0},
        {.animation = "my_IplTop_e",
         .frame = (scene->arrow_visible[1] ? 10150.0f : 10100.0f) +
                  fminf(scene->arrow_age[1], 10.0f),
         .group = "G_ArwR_End",
         .loop_override = 0}};
    size_t footer_clip_count = 4;
    static const char *const footer_groups[] = {"G_Set", "G_Bbs", "G_ArwL_Focus",
                                                "G_ArwR_Focus"};
    for (size_t index = 0; index < 4; index++) {
        HoverAnimation *state = &scene->footer_states[index];
        if (!state->active && index < 2)
            continue;
        float origin;
        float length;
        if (index == 0) {
            origin = state->entering ? 6900.0f : 6930.0f;
            length = state->entering ? 6.0f : 8.0f;
        } else if (index == 1) {
            origin = state->entering ? 900.0f : 930.0f;
            length = state->entering ? 6.0f : 8.0f;
        } else {
            origin = state->active && !state->entering ? 10800.0f : 10600.0f;
            length = 15.0f;
        }
        footer_clips[footer_clip_count++] =
            (WmLayoutClip){.animation = "my_IplTop_e",
                           .frame = origin + fminf(state->frame, length),
                           .group = footer_groups[index],
                           .loop_override = 0};
    }
    for (size_t index = 0; index < 2; index++) {
        if (scene->arrow_press_age[index] < 0.0f)
            continue;
        footer_clips[footer_clip_count++] =
            (WmLayoutClip){.animation = "my_IplTop_e",
                           .frame = 10700.0f + scene->arrow_press_age[index],
                           .group = index == 0 ? "G_ArwL_Ac" : "G_ArwR_Ac",
                           .loop_override = 0};
    }
    /* G_Bbs hover also keys Picture_00. Apply the mail-number group after
     * hover so its frame-zero pose keeps the spare envelope hidden when the
     * Board has no messages. */
    footer_clips[footer_clip_count++] =
        (WmLayoutClip){.animation = "my_IplTop_e",
                       .frame = scene->message_badge_count
                                    ? 1.0f + fmodf(elapsed_seconds * 60.0f, 399.0f)
                                    : 0.0f,
                       .group = "G_BbsSignal",
                       .loop_override = 0};
    footer_clips[footer_clip_count++] = (WmLayoutClip){
        .animation = "my_IplTop_e",
        .frame =
            scene->new_mail_active ? 1.0f + fminf(scene->new_mail_age, 159.0f) : 0.0f,
        .group = "G_BbsSignal_new",
        .loop_override = 0};
    wm_layout_pose(scene->footer, footer_clips, footer_clip_count);
    if (camera) {
        wm_layout_present_filtered_with_fonts(
            scene->platform, scene->textures, scene->fonts, scene->footer, true,
            WM_LAYOUT_IPL, camera, footer_without_arrows, NULL);
    } else {
        wm_layout_present_with_fonts(scene->platform, scene->textures, scene->fonts,
                                     scene->footer, true, WM_LAYOUT_IPL, NULL);
    }

    draw_sd_button(scene, menu, elapsed_seconds, camera, 0.0f);
    if (camera && captured) {
        wm_layout_present_filtered_with_fonts(
            scene->platform, scene->textures, scene->fonts, scene->grid, true,
            WM_LAYOUT_IPL, camera, channel_mask_pane, NULL);
        draw_captured_preview(scene, &zoom);
    }
    if (camera) {
        /* The common arrows leave outside the channel camera, retaining
         * their source size while the selected channel zooms beneath them. */
        wm_platform_set_clip(scene->platform, NULL);
        wm_layout_present_filtered_with_fonts(
            scene->platform, scene->textures, scene->fonts, scene->footer, true,
            WM_LAYOUT_IPL, NULL, footer_arrows_only, NULL);
    }
    if (menu->transition == WM_TRANSITION_BACK && frame->preview_scene &&
        wm_menu_transition_frame(menu) <= 10.0f) {
        /* ChannelTitle's black outside rectangles have already been drawn.
         * Common arrows leave in the full-screen source projection, outside
         * the zoom camera and with no inherited channel clip. */
        wm_platform_set_clip(scene->platform, NULL);
        wm_preview_scene_draw_return_arrows(frame->preview_scene, menu, elapsed_seconds,
                                            true);
    }
    if (!presentation.zooming && !dragging && !menu->home_open &&
        !frame->suppress_balloons)
        wm_resource_scene_draw_balloons(scene, menu, NULL);
    if (owns_frame) {
        wm_pointer_draw(frame->pointer);
        wm_platform_end(scene->platform);
    }
}

void wm_resource_scene_draw_layers(WmResourceScene *scene, const WmMenu *menu,
                                   const WmResourceSceneFrame *frame) {
    draw_resource_scene(scene, menu, frame, false);
}

void wm_resource_scene_draw(WmResourceScene *scene, const WmMenu *menu,
                            const WmResourceSceneFrame *frame) {
    draw_resource_scene(scene, menu, frame, true);
}
