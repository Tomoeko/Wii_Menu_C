#define _POSIX_C_SOURCE 200809L

#include "app_resources.h"
#include "app_runtime.h"
#include "asset_path.h"
#include "board_update.h"
#include "corruption_screen.h"
#include "event_dispatch.h"
#include "frame_transitions.h"
#include "scene_updates.h"
#include "wii_menu/support/asset_manifest.h"

#include <errno.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

static uint64_t monotonic_nanoseconds(void) {
    struct timespec time;
    clock_gettime(CLOCK_MONOTONIC, &time);
    return (uint64_t)time.tv_sec * 1000000000ULL + (uint64_t)time.tv_nsec;
}

static void sleep_nanoseconds(uint64_t duration) {
    struct timespec delay = {.tv_sec = (time_t)(duration / 1000000000ULL),
                             .tv_nsec = (long)(duration % 1000000000ULL)};
    struct timespec remaining;
    while (nanosleep(&delay, &remaining) != 0 && errno == EINTR) {
        delay = remaining;
    }
}

static int print_usage(const char *program) {
    fprintf(stderr, "Usage: %s [--assets DIRECTORY] [--bypass]\n", program);
    fprintf(stderr,
            "       %s --layout JSON --raw-root DIRECTORY [--animation NAME] "
            "[--hide-masks]\n",
            program);
    fprintf(stderr, "Default assets: searches for .local/native-assets.\n");
    fprintf(stderr, "--bypass skips prepared-asset integrity checks.\n");
    fprintf(stderr, "Controls: pointer, arrow keys, Enter, Escape, H for HOME.\n");
    return 0;
}

int main(int argc, char **argv) {
    const char *assets = NULL;
    const char *layout_path = NULL;
    const char *raw_root = NULL;
    const char *animation = NULL;
    bool hide_masks = false;
    bool bypass = false;
    for (int index = 1; index < argc; index++) {
        if (strcmp(argv[index], "--help") == 0 || strcmp(argv[index], "-h") == 0) {
            return print_usage(argv[0]);
        }
        if (strcmp(argv[index], "--assets") == 0 && index + 1 < argc) {
            assets = argv[++index];
            continue;
        }
        if (strcmp(argv[index], "--layout") == 0 && index + 1 < argc) {
            layout_path = argv[++index];
            continue;
        }
        if (strcmp(argv[index], "--raw-root") == 0 && index + 1 < argc) {
            raw_root = argv[++index];
            continue;
        }
        if (strcmp(argv[index], "--animation") == 0 && index + 1 < argc) {
            animation = argv[++index];
            continue;
        }
        if (strcmp(argv[index], "--hide-masks") == 0) {
            hide_masks = true;
            continue;
        }
        if (strcmp(argv[index], "--bypass") == 0) {
            bypass = true;
            continue;
        }
        print_usage(argv[0]);
        return 2;
    }
    if ((layout_path && !raw_root) || ((animation || hide_masks) && !layout_path)) {
        print_usage(argv[0]);
        return 2;
    }

    char default_assets[WM_APP_ASSET_PATH_CAPACITY];
    if (!assets && !layout_path) {
        if (wm_app_find_default_assets(argv[0], default_assets,
                                        sizeof(default_assets))) {
            assets = default_assets;
        } else {
            fprintf(stderr, "Could not find .local/native-assets; "
                            "use --assets DIRECTORY to select prepared assets.\n");
        }
    }

    if (!layout_path && !bypass) {
        const char *root = assets ? assets : ".local/native-assets";
        unsigned issues = 0;
        if (!wm_asset_manifest_verify(root, stderr, &issues)) {
            fprintf(stderr, "Prepared assets at %s have %u integrity issue(s).\n"
                            "Use --bypass only if these files were intentionally edited.\n",
                    root, issues);
            return wm_app_show_corruption_screen(assets);
        }
    }

    WmMenu menu;
    wm_menu_init(&menu);
    WmAppResources resources;
    if (!wm_app_resources_create(&resources, &menu, assets, layout_path, raw_root)) {
        return 1;
    }
    WmLayout *layout = resources.layout;
    WmPlatform *platform = resources.platform;
    WmTextureCache *textures = resources.layout_textures;
    WmFontCache *fonts = resources.layout_fonts;
    WmTextureCache *scene_textures = resources.scene_textures;
    WmFontCache *scene_fonts = resources.scene_fonts;
    WmResourceScene *resource_scene = resources.resource_scene;
    WmPreviewScene *preview_scene = resources.preview_scene;
    WmBoardScene *board_scene = resources.board_scene;
    WmMenuRestartScene *restart_scene = resources.restart_scene;
    WmHealthScene *health_scene = resources.health_scene;
    WmOptionsScene *options_scene = resources.options_scene;
    WmSdScene *sd_scene = resources.sd_scene;
    WmStorageScene **storage_scenes = resources.storage_scenes;
    WmPointer *pointer = resources.pointer;
    WmAudio *audio = resources.audio;
    WmHomeOverlay *home = resources.home;
    WmChannelDrag *drag = resources.drag;
    char *board_state_path = resources.board_state_path;

    bool running = true;
    uint64_t previous = monotonic_nanoseconds();
    WmAppInputState input = {
        .menu_pointer = {.hovered = {WM_HIT_NONE, -1}, .pressed = {WM_HIT_NONE, -1}},
        .scene = {.storage_pressed = {WM_STORAGE_CONTROL_NONE, -1}}};
    WmAppFlowState flow = {.board_settings_request = WM_BOARD_ACTION_NONE,
                           .preview_running_slot = -1,
                           .started = previous,
                           .preview_started = previous};
    WmAppRenderer renderer = {.platform = platform,
                              .layout_textures = textures,
                              .layout_fonts = fonts,
                              .scene_textures = scene_textures,
                              .scene_fonts = scene_fonts,
                              .layout = layout,
                              .animation = animation,
                              .hide_masks = hide_masks,
                              .health_scene = health_scene,
                              .restart_scene = restart_scene,
                              .home = home,
                              .resource_scene = resource_scene,
                              .preview_scene = preview_scene,
                              .board_scene = board_scene,
                              .options_scene = options_scene,
                              .sd_scene = sd_scene,
                              .pointer = pointer,
                              .drag = drag,
                              .audio = audio};
    WmAppRuntime app = {.menu = &menu,
                        .resources = &resources,
                        .renderer = &renderer,
                        .input = &input,
                        .flow = &flow};
    const uint64_t frame_period = 1000000000ULL / 60ULL;
    uint64_t next_frame_deadline = previous + frame_period;
    const bool profile_frames = getenv("WM_PROFILE_FRAMES") != NULL;
    uint64_t profile_frame_count = 0;
    uint64_t profile_frame_total = 0;
    uint64_t profile_draw_total = 0;
    uint64_t profile_draw_max = 0;

    while (running) {
        uint64_t frame_start = monotonic_nanoseconds();
        float elapsed = (float)(frame_start - previous) / 1000000000.0f;
        if (elapsed > 0.1f)
            elapsed = 0.1f;
        previous = frame_start;
        bool health_frame = wm_app_advance_before_events(&app, frame_start, elapsed);
        if (board_scene && menu.screen == WM_SCREEN_BOARD && !menu.home_open) {
            wm_board_scene_set_menu_elapsed_seconds(
                board_scene, (float)(frame_start - flow.started) / 1000000000.0f);
        }
        wm_app_poll_events(&app, frame_start, health_frame, &running);
        if (!running)
            break;
        WmAppBoardUpdate board_update = {
            .menu = &menu,
            .board = board_scene,
            .resource_scene = resource_scene,
            .audio = audio,
            .fade = &flow.fade,
            .input = &input.board,
            .menu_hover = &input.menu_pointer.hovered,
            .settings_request = &flow.board_settings_request,
            .state_path = board_state_path,
            .entry_hover_pending = &flow.board_entry_hover_pending,
            .visited = &flow.board_visited,
            .pointer_inside = input.pointer_inside,
            .pointer_x = input.pointer_x,
            .pointer_y = input.pointer_y};
        bool scene_updates_active = !layout && !health_frame &&
                                    !wm_home_overlay_active(home) &&
                                    !wm_menu_restart_active(&flow.restart);
        wm_app_board_advance(&board_update, elapsed * 60.0f, scene_updates_active,
                             !layout, wm_home_overlay_active(home));
        if (scene_updates_active && menu.screen == WM_SCREEN_SETTINGS &&
            (options_scene || flow.active_storage)) {
            WmAppSettingsUpdate settings_update = {
                .menu = &menu,
                .options = options_scene,
                .storage_scenes = storage_scenes,
                .active_storage = &flow.active_storage,
                .audio = audio,
                .fade = &flow.fade,
                .board_settings_request = &flow.board_settings_request,
                .hovered = &input.scene.options_hovered,
                .pointer_inside = input.pointer_inside,
                .pointer_x = input.pointer_x,
                .pointer_y = input.pointer_y};
            wm_app_settings_advance(&settings_update, elapsed * 60.0f);
        }
        if (scene_updates_active && sd_scene && menu.screen == WM_SCREEN_SD) {
            WmAppSdUpdate sd_update = {.scene = sd_scene,
                                       .audio = audio,
                                       .fade = &flow.fade,
                                       .page = &flow.sd_page,
                                       .help_seen = &flow.sd_help_seen};
            wm_app_sd_advance(&sd_update, elapsed * 60.0f);
        }
        if (scene_updates_active && drag) {
            wm_app_menu_drag_advance(&input.menu_pointer, &menu, resource_scene,
                                     preview_scene, drag, audio, elapsed * 60.0f);
        }
        if (!layout && !health_frame && !wm_menu_restart_active(&flow.restart)) {
            wm_audio_sync(audio, &menu);
            /* Health may queue the badge before startup audio begins. */
            if (menu.screen == WM_SCREEN_GRID && !wm_home_overlay_active(home) &&
                wm_resource_scene_take_new_mail_sound(resource_scene)) {
                wm_audio_play(audio, "WIPL_SE_NEW_ARRIVAL");
            }
        }
        /* The source switches from P1_Def to P1_Cat only while a channel or
         * Board memo is held. Derive the pointer pose after drag controllers
         * advance so a release returns to the normal hand in this frame. */
        WmChannelDragPhase drag_phase = wm_channel_drag_state(drag).phase;
        bool memo_held =
            menu.screen == WM_SCREEN_BOARD && wm_board_scene_dragging(board_scene);
        wm_pointer_set_grabbed(pointer,
                               wm_pointer_grabbed_for_state(drag_phase, memo_held));
        wm_app_update_preview_clock(&app, frame_start);
        WmAppRenderFrame render_frame = {
            .menu = &menu,
            .fade = &flow.fade,
            .restart = &flow.restart,
            .active_storage = flow.active_storage,
            .hover = input.menu_pointer.hovered,
            .board_hovered = input.board.hovered,
            .health_frame = health_frame,
            .keyboard_focus = input.keyboard_focus,
            .focused_slot = input.focused_slot,
            .frame_start = frame_start,
            .started = flow.started,
            .preview_started = flow.preview_started,
            .home_underlay_elapsed = flow.home_underlay_elapsed,
            .home_underlay_preview_elapsed = flow.home_underlay_preview_elapsed};
        uint64_t draw_start = profile_frames ? monotonic_nanoseconds() : 0;
        wm_app_renderer_draw(&renderer, &render_frame);

        /* Follow a fixed deadline so sleep overshoot does not accumulate and
         * slow every 28-frame zoom by roughly one millisecond per frame. */
        uint64_t frame_end = monotonic_nanoseconds();
        if (profile_frames) {
            uint64_t draw_time = frame_end - draw_start;
            profile_frame_count++;
            profile_frame_total += frame_end - frame_start;
            profile_draw_total += draw_time;
            if (draw_time > profile_draw_max) profile_draw_max = draw_time;
            if (profile_frame_count == 120) {
                WmTextureCacheStats textures =
                    wm_texture_cache_stats(scene_textures);
                fprintf(stderr,
                        "Frame timing: screen=%d, frame=%.2f ms, draw=%.2f ms, "
                        "max draw=%.2f ms, textures=%zu/%zu MiB, "
                        "evictions=%llu (120 frames)\n",
                        (int)menu.screen,
                        (double)profile_frame_total / 120000000.0,
                        (double)profile_draw_total / 120000000.0,
                        (double)profile_draw_max / 1000000.0,
                        textures.resident_bytes / (1024u * 1024u),
                        textures.budget_bytes / (1024u * 1024u),
                        (unsigned long long)textures.evictions);
                profile_frame_count = 0;
                profile_frame_total = 0;
                profile_draw_total = 0;
                profile_draw_max = 0;
            }
        }
        if (frame_end < next_frame_deadline)
            sleep_nanoseconds(next_frame_deadline - frame_end);
        uint64_t wake_time = monotonic_nanoseconds();
        if (wake_time > next_frame_deadline + frame_period / 2)
            next_frame_deadline = wake_time + frame_period;
        else
            next_frame_deadline += frame_period;
    }
    wm_app_renderer_release_home_underlay(&renderer);
    wm_app_resources_destroy(&resources);
    return 0;
}
