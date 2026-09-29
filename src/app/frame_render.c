#include "frame_render.h"

#include "wii_menu/layout/layout_present.h"
#include "wii_menu/render/ui.h"

#include <string.h>

static const WmColor scene_clear = {0.92f, 0.92f, 0.92f, 1.0f};
static const WmColor black_clear = {0.0f, 0.0f, 0.0f, 1.0f};

static bool without_masks(void *context, const WmLayoutPaneView *pane) {
    (void)context;
    return strncmp(pane->name, "BaseMask", 8) != 0 && strcmp(pane->name, "ChMask") != 0;
}

static void begin_scene_frame(const WmAppRenderer *renderer, WmColor clear_color) {
    wm_texture_cache_begin_frame(renderer->scene_textures);
    wm_font_cache_begin_frame(renderer->scene_fonts);
    wm_platform_begin(renderer->platform, clear_color);
}

static void draw_home_underlay(const WmAppRenderer *renderer,
                               const WmAppRenderFrame *frame) {
    const WmMenu *menu = frame->menu;
    if (renderer->resource_scene && menu->screen == WM_SCREEN_GRID) {
        WmResourceSceneFrame scene_frame = {
            .elapsed_seconds = frame->home_underlay_elapsed,
            .preview_elapsed_seconds = frame->home_underlay_preview_elapsed,
            .hover = {WM_HIT_NONE, -1},
            .preview_scene = renderer->preview_scene,
            .board_scene = renderer->board_scene};
        wm_resource_scene_draw_layers(renderer->resource_scene, menu, &scene_frame);
        return;
    }
    if (renderer->preview_scene && menu->screen == WM_SCREEN_PREVIEW &&
        wm_preview_scene_available(renderer->preview_scene, menu)) {
        wm_texture_cache_begin_frame(renderer->scene_textures);
        wm_font_cache_begin_frame(renderer->scene_fonts);
        wm_preview_scene_draw_layers(renderer->preview_scene, menu,
                                     frame->home_underlay_preview_elapsed, NULL);
        return;
    }

    wm_texture_cache_begin_frame(renderer->scene_textures);
    wm_font_cache_begin_frame(renderer->scene_fonts);
    if (renderer->board_scene && menu->screen == WM_SCREEN_BOARD) {
        wm_board_scene_draw_body(renderer->board_scene);
        float grid_frame = 0.0f;
        if (renderer->resource_scene &&
            wm_board_scene_grid_overlay(renderer->board_scene, &grid_frame)) {
            wm_resource_scene_draw_grid_overlay(renderer->resource_scene, menu,
                                                grid_frame,
                                                frame->home_underlay_elapsed);
        }
        wm_board_scene_set_menu_elapsed_seconds(renderer->board_scene,
                                                frame->home_underlay_elapsed);
        wm_board_scene_draw_footer(renderer->board_scene);
        float sd_frame = 0.0f;
        if (renderer->resource_scene &&
            wm_board_scene_sd_button_frame(renderer->board_scene, &sd_frame)) {
            wm_resource_scene_draw_sd_button(renderer->resource_scene,
                                             frame->home_underlay_elapsed, sd_frame);
        }
    } else if (renderer->options_scene && menu->screen == WM_SCREEN_SETTINGS) {
        if (frame->active_storage) {
            wm_options_scene_draw_background(renderer->options_scene);
            wm_storage_scene_draw_back(frame->active_storage);
            wm_options_scene_draw_objects(renderer->options_scene);
            wm_storage_scene_draw_content(frame->active_storage);
        } else {
            wm_options_scene_draw(renderer->options_scene);
        }
    } else if (renderer->sd_scene && menu->screen == WM_SCREEN_SD) {
        wm_sd_scene_draw(renderer->sd_scene);
    }
}

static void draw_layout(WmAppRenderer *renderer, const WmAppRenderFrame *frame) {
    if (renderer->animation) {
        WmLayoutClip clip = {.animation = renderer->animation,
                             .frame = (float)(frame->frame_start - frame->started) /
                                      1000000000.0f * 60.0f,
                             .loop_override = -1};
        wm_layout_pose(renderer->layout, &clip, 1);
    }
    wm_texture_cache_begin_frame(renderer->layout_textures);
    wm_font_cache_begin_frame(renderer->layout_fonts);
    wm_platform_begin(renderer->platform, scene_clear);
    wm_layout_present_filtered_with_fonts(
        renderer->platform, renderer->layout_textures, renderer->layout_fonts,
        renderer->layout, true, WM_LAYOUT_IPL, NULL,
        renderer->hide_masks ? without_masks : NULL, NULL);
    wm_platform_end(renderer->platform);
}

static void draw_restart(WmAppRenderer *renderer, const WmAppRenderFrame *frame) {
    if (frame->restart->phase == WM_MENU_RESTART_GRID && renderer->resource_scene) {
        const WmResourceSceneFrame scene_frame = {
            .elapsed_seconds =
                (float)(frame->frame_start - frame->started) / 1000000000.0f,
            .hover = {WM_HIT_NONE, -1},
            .pointer = renderer->pointer,
            .preview_scene = renderer->preview_scene,
            .board_scene = renderer->board_scene};
        wm_resource_scene_draw(renderer->resource_scene, frame->menu, &scene_frame);
        return;
    }
    begin_scene_frame(renderer, black_clear);
    wm_menu_restart_scene_draw(renderer->restart_scene, frame->restart);
    wm_platform_end(renderer->platform);
}

static void draw_home(WmAppRenderer *renderer, const WmAppRenderFrame *frame) {
    /* Freeze the underlying scene for this HOME opening. Reusing the target
     * preserves its animation pose and avoids resubmitting its draw list. */
    if (!renderer->home_underlay_texture) {
        renderer->home_underlay_texture =
            wm_platform_create_render_texture(renderer->platform);
    }
    if (!renderer->home_underlay_valid && renderer->home_underlay_texture &&
        wm_platform_begin_target(renderer->platform, renderer->home_underlay_texture,
                                 scene_clear)) {
        draw_home_underlay(renderer, frame);
        wm_platform_end(renderer->platform);
        renderer->home_underlay_valid = true;
    }

    begin_scene_frame(renderer, scene_clear);
    if (renderer->home_underlay_valid) {
        const WmQuad retained_scene = {.x = 0.0f,
                                       .y = 0.0f,
                                       .width = WM_FRAME_WIDTH,
                                       .height = WM_FRAME_HEIGHT,
                                       .u1 = 1.0f,
                                       .v1 = 1.0f,
                                       .color = {1.0f, 1.0f, 1.0f, 1.0f},
                                       .texture = renderer->home_underlay_texture};
        wm_platform_draw_quad(renderer->platform, &retained_scene);
    } else {
        draw_home_underlay(renderer, frame);
    }
    wm_home_overlay_draw(renderer->home);
    wm_pointer_draw(renderer->pointer);
    wm_home_overlay_draw_fade(renderer->home);
    wm_platform_end(renderer->platform);
}

static void draw_grid(WmAppRenderer *renderer, const WmAppRenderFrame *frame,
                      WmHit visual_focus) {
    const WmResourceSceneFrame scene_frame = {
        .elapsed_seconds = (float)(frame->frame_start - frame->started) / 1000000000.0f,
        .preview_elapsed_seconds =
            (float)(frame->frame_start - frame->preview_started) / 1000000000.0f,
        .hover = visual_focus,
        .suppress_balloons = wm_scene_fader_active(&frame->fade->clock),
        .pointer = renderer->pointer,
        .preview_scene = renderer->preview_scene,
        .board_scene = renderer->board_scene,
        .drag = renderer->drag};
    wm_resource_scene_draw(renderer->resource_scene, frame->menu, &scene_frame);
    if (wm_resource_scene_take_balloon_sound(renderer->resource_scene)) {
        wm_audio_play(renderer->audio, "balloon");
    }
}

static void draw_board(WmAppRenderer *renderer, const WmAppRenderFrame *frame) {
    begin_scene_frame(renderer, scene_clear);
    wm_board_scene_draw_body(renderer->board_scene);
    float grid_frame = 0.0f;
    float scene_seconds = (float)(frame->frame_start - frame->started) / 1000000000.0f;
    if (renderer->resource_scene &&
        wm_board_scene_grid_overlay(renderer->board_scene, &grid_frame)) {
        wm_resource_scene_draw_grid_overlay(renderer->resource_scene, frame->menu,
                                            grid_frame, scene_seconds);
    }
    wm_board_scene_set_menu_elapsed_seconds(renderer->board_scene, scene_seconds);
    wm_board_scene_draw_footer(renderer->board_scene);
    float sd_frame = 0.0f;
    if (renderer->resource_scene &&
        wm_board_scene_sd_button_frame(renderer->board_scene, &sd_frame)) {
        wm_resource_scene_draw_sd_button(renderer->resource_scene, scene_seconds,
                                         sd_frame);
    }
    if (renderer->resource_scene) {
        wm_resource_scene_draw_board_balloons(renderer->resource_scene,
                                              renderer->board_scene,
                                              frame->board_hovered, scene_seconds);
        if (wm_resource_scene_take_balloon_sound(renderer->resource_scene)) {
            wm_audio_play(renderer->audio, "balloon");
        }
    }
    wm_pointer_draw(renderer->pointer);
    wm_platform_end(renderer->platform);
}

static void draw_options(WmAppRenderer *renderer, const WmAppRenderFrame *frame) {
    begin_scene_frame(renderer, scene_clear);
    if (frame->active_storage) {
        wm_options_scene_draw_background(renderer->options_scene);
        wm_storage_scene_draw_back(frame->active_storage);
        wm_options_scene_draw_objects(renderer->options_scene);
        wm_storage_scene_draw_content(frame->active_storage);
    } else {
        wm_options_scene_draw(renderer->options_scene);
    }
    wm_pointer_draw(renderer->pointer);
    wm_platform_end(renderer->platform);
}

void wm_app_renderer_invalidate_home_underlay(WmAppRenderer *renderer) {
    if (renderer) {
        renderer->home_underlay_valid = false;
    }
}

void wm_app_renderer_release_home_underlay(WmAppRenderer *renderer) {
    if (!renderer) {
        return;
    }
    if (renderer->home_underlay_texture) {
        wm_platform_destroy_texture(renderer->platform,
                                    renderer->home_underlay_texture);
        renderer->home_underlay_texture = 0;
    }
    renderer->home_underlay_valid = false;
}

void wm_app_renderer_draw(WmAppRenderer *renderer, const WmAppRenderFrame *frame) {
    WmHit visual_focus = frame->hover;
    if (frame->keyboard_focus && frame->menu->screen == WM_SCREEN_GRID &&
        !frame->menu->home_open) {
        visual_focus =
            (WmHit){WM_HIT_CHANNEL,
                    frame->menu->page * WM_CHANNELS_PER_PAGE + frame->focused_slot};
    }
    bool grid_transition = frame->menu->transition == WM_TRANSITION_SELECT ||
                           frame->menu->transition == WM_TRANSITION_BACK;
    if (renderer->layout) {
        draw_layout(renderer, frame);
    } else if (frame->health_frame) {
        begin_scene_frame(renderer, black_clear);
        wm_health_scene_draw(renderer->health_scene);
        wm_platform_end(renderer->platform);
    } else if (wm_menu_restart_active(frame->restart)) {
        draw_restart(renderer, frame);
    } else if (wm_home_overlay_active(renderer->home)) {
        draw_home(renderer, frame);
    } else if (renderer->resource_scene &&
               (frame->menu->screen == WM_SCREEN_GRID || grid_transition) &&
               !frame->menu->home_open) {
        draw_grid(renderer, frame, visual_focus);
    } else if (renderer->preview_scene && frame->menu->screen == WM_SCREEN_PREVIEW &&
               !frame->menu->home_open &&
               wm_preview_scene_draw(
                   renderer->preview_scene, frame->menu,
                   (float)(frame->frame_start - frame->preview_started) / 1000000000.0f,
                   visual_focus, renderer->pointer)) {
        /* The WAD-backed preview owns this frame. */
    } else if (renderer->board_scene && frame->menu->screen == WM_SCREEN_BOARD &&
               !frame->menu->home_open) {
        draw_board(renderer, frame);
    } else if (renderer->options_scene && frame->menu->screen == WM_SCREEN_SETTINGS &&
               !frame->menu->home_open) {
        draw_options(renderer, frame);
    } else if (renderer->sd_scene && frame->menu->screen == WM_SCREEN_SD &&
               !frame->menu->home_open) {
        begin_scene_frame(renderer, scene_clear);
        wm_sd_scene_draw(renderer->sd_scene);
        wm_pointer_draw(renderer->pointer);
        wm_platform_end(renderer->platform);
    } else {
        wm_ui_draw(renderer->platform, frame->menu, visual_focus, renderer->pointer);
    }
}
