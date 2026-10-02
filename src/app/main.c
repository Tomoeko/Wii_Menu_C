#define _POSIX_C_SOURCE 200809L

#include "app_options.h"
#include "app_recording.h"
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
#include <signal.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

typedef struct {
    struct sigaction interrupt;
    struct sigaction terminate;
    bool installed;
} RecordingSignals;

/* Signal handlers only request the normal main-thread recording cleanup. */
static volatile sig_atomic_t recording_exit_requested;

static void request_recording_exit(int signal_number) {
    (void)signal_number;
    recording_exit_requested = 1;
}

static bool install_recording_signals(RecordingSignals *signals) {
    struct sigaction action = {.sa_handler = request_recording_exit};
    if (sigemptyset(&action.sa_mask) != 0 ||
        sigaction(SIGINT, NULL, &signals->interrupt) != 0 ||
        sigaction(SIGTERM, NULL, &signals->terminate) != 0)
        return false;
    recording_exit_requested = 0;
    if (sigaction(SIGINT, &action, NULL) != 0)
        return false;
    if (sigaction(SIGTERM, &action, NULL) != 0) {
        sigaction(SIGINT, &signals->interrupt, NULL);
        return false;
    }
    signals->installed = true;
    return true;
}

static void restore_recording_signals(const RecordingSignals *signals) {
    if (!signals->installed)
        return;
    sigaction(SIGINT, &signals->interrupt, NULL);
    sigaction(SIGTERM, &signals->terminate, NULL);
}

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
    fprintf(stderr,
            "Usage: %s [--assets DIRECTORY] [--bypass] "
            "[--preview-channel ID-OR-NAME] [--record [half]]\n",
            program);
    fprintf(stderr,
            "       %s --layout JSON --raw-root DIRECTORY [--animation NAME] "
            "[--hide-masks] [--record [half]]\n",
            program);
    fprintf(stderr, "Default assets: searches for Files/.local/native-assets.\n");
    fprintf(stderr, "--bypass skips prepared-asset integrity checks.\n");
    fprintf(stderr,
            "--record [half] saves video and audio to Movies until the window closes.\n"
            "The half option halves video width/height and preserves audio.\n");
    fprintf(stderr, "Controls: pointer, arrow keys, Enter, Escape, H for HOME.\n");
    return 0;
}

static bool select_preview_channel(WmMenu *menu, WmAppResources *resources,
                                   const char *requested) {
    int match = -1;
    for (int slot = 1; slot < WM_SLOT_COUNT; slot++) {
        const WmChannel *channel = &menu->slots[slot];
        if (!channel->occupied || (strcmp(channel->id, requested) != 0 &&
                                   strcmp(channel->title, requested) != 0))
            continue;
        if (match >= 0) {
            fprintf(stderr, "Ambiguous channel name: %s; use its ID.\n", requested);
            return false;
        }
        match = slot;
    }
    if (match < 0) {
        fprintf(stderr, "Channel is not visible: %s\n", requested);
        return false;
    }

    wm_health_scene_reset(resources->health_scene, false);
    menu->screen = WM_SCREEN_PREVIEW;
    menu->page = match / WM_CHANNELS_PER_PAGE;
    menu->selected = match;
    return true;
}

static void sync_audio_and_pointer(WmAppRuntime *app, bool health_frame) {
    WmMenu *menu = app->menu;
    WmAppResources *resources = app->resources;
    if (!resources->layout && !health_frame &&
        !wm_menu_restart_active(&app->flow->restart)) {
        wm_audio_sync(resources->audio, menu);
        /* Health may queue the badge before startup audio begins. */
        if (menu->screen == WM_SCREEN_GRID &&
            !wm_home_overlay_active(resources->home) &&
            wm_resource_scene_take_new_mail_sound(resources->resource_scene)) {
            wm_audio_play(resources->audio, "WIPL_SE_NEW_ARRIVAL");
        }
    }
    /* The source switches from P1_Def to P1_Cat only while a channel or
     * Board memo is held. Derive the pointer pose after drag controllers
     * advance so a release returns to the normal hand in this frame. */
    WmChannelDragPhase drag_phase = wm_channel_drag_state(resources->drag).phase;
    bool memo_held = menu->screen == WM_SCREEN_BOARD &&
                     wm_board_scene_dragging(resources->board_scene);
    wm_pointer_set_grabbed(resources->pointer,
                           wm_pointer_grabbed_for_state(drag_phase, memo_held));
}

static void advance_scenes_after_events(WmAppRuntime *app, float elapsed,
                                        bool health_frame, uint64_t frame_start) {
    WmMenu *menu = app->menu;
    WmAppResources *resources = app->resources;
    WmAppInputState *input = app->input;
    WmAppFlowState *flow = app->flow;
    WmAppBoardUpdate board_update = {.menu = menu,
                                     .board = resources->board_scene,
                                     .resource_scene = resources->resource_scene,
                                     .audio = resources->audio,
                                     .fade = &flow->fade,
                                     .input = &input->board,
                                     .menu_hover = &input->menu_pointer.hovered,
                                     .settings_request = &flow->board_settings_request,
                                     .state_path = resources->board_state_path,
                                     .entry_hover_pending =
                                         &flow->board_entry_hover_pending,
                                     .visited = &flow->board_visited,
                                     .pointer_inside = input->pointer_inside,
                                     .pointer_x = input->pointer_x,
                                     .pointer_y = input->pointer_y};
    bool scene_updates_active = !resources->layout && !health_frame &&
                                !wm_home_overlay_active(resources->home) &&
                                !wm_menu_restart_active(&flow->restart);
    wm_app_board_advance(&board_update, elapsed * 60.0f, scene_updates_active,
                         !resources->layout, wm_home_overlay_active(resources->home));
    if (scene_updates_active && menu->screen == WM_SCREEN_SETTINGS &&
        (resources->options_scene || flow->active_storage)) {
        WmAppSettingsUpdate settings_update = {
            .menu = menu,
            .options = resources->options_scene,
            .storage_scenes = resources->storage_scenes,
            .active_storage = &flow->active_storage,
            .audio = resources->audio,
            .fade = &flow->fade,
            .board_settings_request = &flow->board_settings_request,
            .hovered = &input->scene.options_hovered,
            .pointer_inside = input->pointer_inside,
            .pointer_x = input->pointer_x,
            .pointer_y = input->pointer_y};
        wm_app_settings_advance(&settings_update, elapsed * 60.0f);
    }
    if (scene_updates_active && resources->sd_scene && menu->screen == WM_SCREEN_SD) {
        WmAppSdUpdate sd_update = {.scene = resources->sd_scene,
                                   .audio = resources->audio,
                                   .fade = &flow->fade,
                                   .page = &flow->sd_page,
                                   .help_seen = &flow->sd_help_seen};
        wm_app_sd_advance(&sd_update, elapsed * 60.0f);
    }
    if (scene_updates_active && resources->drag) {
        wm_app_menu_drag_advance(&input->menu_pointer, menu, resources->resource_scene,
                                 resources->preview_scene, resources->drag,
                                 resources->audio, elapsed * 60.0f);
    }
    sync_audio_and_pointer(app, health_frame);
    wm_app_update_preview_clock(app, frame_start);
}

static WmAppRenderFrame make_render_frame(const WmAppRuntime *app, uint64_t frame_start,
                                          bool health_frame) {
    const WmAppFlowState *flow = app->flow;
    const WmAppInputState *input = app->input;
    WmAppRenderFrame frame = {0};
    frame.menu = app->menu;
    frame.fade = &flow->fade;
    frame.restart = &flow->restart;
    frame.active_storage = flow->active_storage;
    frame.hover = input->menu_pointer.hovered;
    frame.board_hovered = input->board.hovered;
    frame.health_frame = health_frame;
    frame.keyboard_focus = input->keyboard_focus;
    frame.focused_slot = input->focused_slot;
    frame.frame_start = frame_start;
    frame.started = flow->started;
    frame.preview_started = flow->preview_started;
    frame.home_underlay_elapsed = flow->home_underlay_elapsed;
    frame.home_underlay_preview_elapsed = flow->home_underlay_preview_elapsed;
    return frame;
}

typedef struct {
    uint64_t frame_count;
    uint64_t frame_total;
    uint64_t draw_total;
    uint64_t draw_max;
} AppFrameProfile;

static void record_frame_profile(AppFrameProfile *profile,
                                 const WmAppResources *resources, const WmMenu *menu,
                                 uint64_t frame_start, uint64_t draw_start,
                                 uint64_t frame_end) {
    uint64_t draw_time = frame_end - draw_start;
    profile->frame_count++;
    profile->frame_total += frame_end - frame_start;
    profile->draw_total += draw_time;
    if (draw_time > profile->draw_max)
        profile->draw_max = draw_time;
    if (profile->frame_count < 120)
        return;

    WmTextureCacheStats textures = wm_texture_cache_stats(resources->scene_textures);
    fprintf(stderr,
            "Frame timing: screen=%d, frame=%.2f ms, draw=%.2f ms, "
            "max draw=%.2f ms, textures=%zu/%zu MiB, "
            "evictions=%llu (120 frames)\n",
            (int)menu->screen, (double)profile->frame_total / 120000000.0,
            (double)profile->draw_total / 120000000.0,
            (double)profile->draw_max / 1000000.0,
            textures.resident_bytes / (1024u * 1024u),
            textures.budget_bytes / (1024u * 1024u),
            (unsigned long long)textures.evictions);
    *profile = (AppFrameProfile){0};
}

static void wait_for_next_frame(uint64_t *deadline, uint64_t period,
                                uint64_t frame_end) {
    /* A fixed deadline prevents sleep overshoot from accumulating across an
     * animation. A late frame starts a fresh interval instead. */
    if (frame_end < *deadline)
        sleep_nanoseconds(*deadline - frame_end);
    uint64_t wake_time = monotonic_nanoseconds();
    if (wake_time > *deadline + period / 2)
        *deadline = wake_time + period;
    else
        *deadline += period;
}

static int run_app(const WmAppOptions *options, const char *program) {
    const char *assets = options->assets;

    char default_assets[WM_APP_ASSET_PATH_CAPACITY];
    if (!assets && !options->layout_path) {
        if (wm_app_find_default_assets(program, default_assets,
                                       sizeof(default_assets))) {
            assets = default_assets;
        } else {
            fprintf(stderr, "Could not find Files/.local/native-assets; "
                            "use --assets DIRECTORY to select prepared assets.\n");
        }
    }

    if (!options->layout_path && !options->bypass) {
        const char *root = assets ? assets : "Files/.local/native-assets";
        unsigned issues = 0;
        if (!wm_asset_manifest_verify(root, stderr, &issues)) {
            fprintf(stderr,
                    "Prepared assets at %s have %u integrity issue(s).\n"
                    "Use --bypass only if these files were intentionally edited.\n",
                    root, issues);
            return wm_app_show_corruption_screen(assets, options->record,
                                                 options->record_half,
                                                 &recording_exit_requested);
        }
    }

    WmMenu menu;
    wm_menu_init(&menu);
    WmAppResources resources;
    if (!wm_app_resources_create(&resources, &menu, assets, options->layout_path,
                                 options->raw_root)) {
        return 1;
    }
    if (options->preview_channel &&
        !select_preview_channel(&menu, &resources, options->preview_channel)) {
        wm_app_resources_destroy(&resources);
        return 2;
    }

    CcRecording *recording = NULL;
    if (options->record) {
        recording = wm_app_recording_open(resources.platform, resources.audio,
                                          options->record_half);
        if (!recording) {
            fprintf(stderr, "Could not start recording in Movies.\n");
            wm_app_resources_destroy(&resources);
            return 1;
        }
        fprintf(stderr, "Recording: %s\n", cc_recording_path(recording));
    }
    bool recording_started = false;
    int result = 0;
    bool running = true;
    uint64_t previous = monotonic_nanoseconds();
    WmAppInputState input = {
        .menu_pointer = {.hovered = {WM_HIT_NONE, -1}, .pressed = {WM_HIT_NONE, -1}},
        .scene = {.storage_pressed = {WM_STORAGE_CONTROL_NONE, -1}}};
    WmAppFlowState flow = {.board_settings_request = WM_BOARD_ACTION_NONE,
                           .preview_running_slot = -1,
                           .started = previous,
                           .preview_started = previous};
    WmAppRenderer renderer = {.platform = resources.platform,
                              .layout_textures = resources.layout_textures,
                              .layout_fonts = resources.layout_fonts,
                              .scene_textures = resources.scene_textures,
                              .scene_fonts = resources.scene_fonts,
                              .layout = resources.layout,
                              .animation = options->animation,
                              .hide_masks = options->hide_masks,
                              .health_scene = resources.health_scene,
                              .restart_scene = resources.restart_scene,
                              .home = resources.home,
                              .resource_scene = resources.resource_scene,
                              .preview_scene = resources.preview_scene,
                              .board_scene = resources.board_scene,
                              .options_scene = resources.options_scene,
                              .sd_scene = resources.sd_scene,
                              .pointer = resources.pointer,
                              .drag = resources.drag,
                              .audio = resources.audio};
    WmAppRuntime app = {.menu = &menu,
                        .resources = &resources,
                        .renderer = &renderer,
                        .input = &input,
                        .flow = &flow};
    const uint64_t frame_period = 1000000000ULL / 60ULL;
    uint64_t next_frame_deadline = previous + frame_period;
    const bool profile_frames = getenv("WM_PROFILE_FRAMES") != NULL;
    AppFrameProfile profile = {0};

    while (running && !recording_exit_requested) {
        uint64_t frame_start = monotonic_nanoseconds();
        float elapsed = (float)(frame_start - previous) / 1000000000.0f;
        if (elapsed > 0.1f)
            elapsed = 0.1f;
        previous = frame_start;
        bool health_frame = wm_app_advance_before_events(&app, frame_start, elapsed);
        if (resources.board_scene && menu.screen == WM_SCREEN_BOARD &&
            !menu.home_open) {
            wm_board_scene_set_menu_elapsed_seconds(
                resources.board_scene,
                (float)(frame_start - flow.started) / 1000000000.0f);
        }
        wm_app_poll_events(&app, frame_start, health_frame, &running);
        if (!running)
            break;
        advance_scenes_after_events(&app, elapsed, health_frame, frame_start);
        WmAppRenderFrame render_frame =
            make_render_frame(&app, frame_start, health_frame);
        uint64_t draw_start = profile_frames ? monotonic_nanoseconds() : 0;
        wm_app_renderer_draw(&renderer, &render_frame);

        uint64_t frame_end = monotonic_nanoseconds();
        if (recording) {
            bool captured = cc_recording_frame(recording, (double)frame_end / 1e9);
            if (captured && !recording_started) {
                captured = wm_app_recording_start(
                    recording, resources.audio, (double)monotonic_nanoseconds() / 1e9);
                recording_started = captured;
            }
            if (captured)
                captured =
                    cc_recording_pump(recording, (double)monotonic_nanoseconds() / 1e9);
            if (!captured) {
                fprintf(stderr, "Recording failed: %s\n",
                        cc_recording_error(recording));
                result = 1;
                break;
            }
            frame_end = monotonic_nanoseconds();
        }
        if (profile_frames)
            record_frame_profile(&profile, &resources, &menu, frame_start, draw_start,
                                 frame_end);
        wait_for_next_frame(&next_frame_deadline, frame_period, frame_end);
    }
    if (!wm_app_recording_close(recording, resources.audio,
                                (double)monotonic_nanoseconds() / 1e9)) {
        fprintf(stderr, "Recording could not be finalized completely.\n");
        result = 1;
    }
    wm_app_renderer_release_home_underlay(&renderer);
    wm_app_resources_destroy(&resources);
    return result;
}

int main(int argc, char **argv) {
    WmAppOptions options;
    WmAppOptionsResult parsed = wm_app_parse_options(argc, argv, &options);
    if (parsed != WM_APP_OPTIONS_READY) {
        print_usage(argv[0]);
        return parsed == WM_APP_OPTIONS_HELP ? 0 : 2;
    }
    RecordingSignals signals = {0};
    if (options.record && !install_recording_signals(&signals)) {
        fprintf(stderr, "Could not prepare recording exit handlers.\n");
        return 1;
    }
    int result = run_app(&options, argv[0]);
    restore_recording_signals(&signals);
    return result;
}
