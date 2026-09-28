#define _POSIX_C_SOURCE 200809L

#include "app_resources.h"
#include "board_input.h"
#include "board_update.h"
#include "frame_render.h"
#include "input_routing.h"
#include "menu_pointer.h"
#include "scene_input.h"
#include "scene_updates.h"

#include "wii_menu/animation/scene_fader.h"
#include "wii_menu/audio/audio.h"
#include "wii_menu/board/board_scene.h"
#include "wii_menu/fonts/font_cache.h"
#include "wii_menu/input/channel_drag.h"
#include "wii_menu/input/pointer.h"
#include "wii_menu/layout/layout_runtime.h"
#include "wii_menu/menu/menu.h"
#include "wii_menu/menu/menu_restart.h"
#include "wii_menu/platform/platform.h"
#include "wii_menu/render/texture_cache.h"
#include "wii_menu/scenes/health_scene.h"
#include "wii_menu/scenes/home_overlay.h"
#include "wii_menu/scenes/options_scene.h"
#include "wii_menu/scenes/preview_scene.h"
#include "wii_menu/scenes/resource_scene.h"
#include "wii_menu/scenes/sd_scene.h"
#include "wii_menu/scenes/storage_scene.h"

#include <errno.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <time.h>

typedef struct WmHomeEntryContext {
    WmHomeOverlay *home;
    WmMenu *menu;
    WmResourceScene *resource_scene;
    const WmAppSceneFade *fade;
    WmChannelDrag *drag;
    WmBoardScene *board_scene;
    WmAudio *audio;
    WmOptionsScene *options_scene;
    bool *options_held_arrow;
    uint64_t *opened_at;
    float *underlay_elapsed;
    float *underlay_preview_elapsed;
    WmAppRenderer *renderer;
} WmHomeEntryContext;

static bool open_home(WmHomeOverlay *home, WmMenu *menu,
                      WmResourceScene *resource_scene,
                      const WmAppSceneFade *fade, WmChannelDrag *drag,
                      uint64_t now, uint64_t *opened_at) {
    if (!home || !menu || !opened_at || menu->notice[0] ||
        menu->transition != WM_TRANSITION_NONE ||
        wm_scene_fader_active(&fade->clock) ||
        wm_channel_drag_state(drag).phase != WM_CHANNEL_DRAG_NONE ||
        !wm_home_overlay_open(home)) {
        return false;
    }
    /* The GLES2 backend permits one capture target. The zoom capture is no
     * longer needed after the grid has settled. */
    wm_resource_scene_release_preview_capture(resource_scene);
    menu->home_open = true;
    *opened_at = now;
    return true;
}

static bool try_enter_home(const WmHomeEntryContext *entry, uint64_t now,
                           uint64_t started, uint64_t preview_started) {
    if (entry->menu->screen == WM_SCREEN_SETTINGS && entry->options_scene &&
        wm_options_scene_snapshot(entry->options_scene).locked) {
        return false;
    }
    if (!open_home(entry->home, entry->menu, entry->resource_scene,
                   entry->fade, entry->drag, now, entry->opened_at)) {
        return false;
    }

    wm_board_scene_cancel_pointer(entry->board_scene);
    wm_audio_stop_loop(entry->audio, "WIPL_SE_BOARD_DRAG");
    wm_audio_stop_loop(entry->audio, "WIPL_SE_MESSAGE_SCROLL");
    if (*entry->options_held_arrow) {
        wm_options_scene_pointer_up(entry->options_scene);
        *entry->options_held_arrow = false;
    }
    *entry->underlay_elapsed = (float)(now - started) / 1000000000.0f;
    *entry->underlay_preview_elapsed =
        (float)(now - preview_started) / 1000000000.0f;
    wm_app_renderer_invalidate_home_underlay(entry->renderer);
    return true;
}

static uint64_t monotonic_nanoseconds(void) {
    struct timespec time;
    clock_gettime(CLOCK_MONOTONIC, &time);
    return (uint64_t)time.tv_sec * 1000000000ULL + (uint64_t)time.tv_nsec;
}

static void sleep_nanoseconds(uint64_t duration) {
    struct timespec delay = {
        .tv_sec = (time_t)(duration / 1000000000ULL),
        .tv_nsec = (long)(duration % 1000000000ULL)
    };
    struct timespec remaining;
    while (nanosleep(&delay, &remaining) != 0 && errno == EINTR) {
        delay = remaining;
    }
}

static int print_usage(const char *program) {
    fprintf(stderr, "Usage: %s [--assets DIRECTORY]\n", program);
    fprintf(stderr, "       %s --layout JSON --raw-root DIRECTORY [--animation NAME] [--hide-masks]\n",
            program);
    fprintf(stderr, "Controls: pointer, arrow keys, Enter, Escape, H for HOME.\n");
    return 0;
}

int main(int argc, char **argv) {
    const char *assets = NULL;
    const char *layout_path = NULL;
    const char *raw_root = NULL;
    const char *animation = NULL;
    bool hide_masks = false;
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
        print_usage(argv[0]);
        return 2;
    }
    if ((layout_path && !raw_root) ||
        ((animation || hide_masks) && !layout_path)) {
        print_usage(argv[0]);
        return 2;
    }

    WmMenu menu;
    wm_menu_init(&menu);
    WmAppResources resources;
    if (!wm_app_resources_create(&resources, &menu, assets, layout_path,
                                 raw_root)) {
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
    bool keyboard_focus = false;
    WmAppMenuPointer menu_pointer = {
        .hovered = {WM_HIT_NONE, -1},
        .pressed = {WM_HIT_NONE, -1}
    };
    WmAppBoardInput board_input = {0};
    int pointer_x = 0;
    int pointer_y = 0;
    bool pointer_inside = false;
    bool board_entry_hover_pending = false;
    bool board_visited = false;
    WmAppSceneInput scene_input = {
        .storage_pressed = {WM_STORAGE_CONTROL_NONE, -1}
    };
    WmHomeControl home_hovered = WM_HOME_CONTROL_NONE;
    WmHomeControl home_pressed = WM_HOME_CONTROL_NONE;
    WmStorageScene *active_storage = NULL;
    WmAppSceneFade fade = {0};
    WmBoardAction board_settings_request = WM_BOARD_ACTION_NONE;
    WmMenuRestartClock restart = {0};
    unsigned sd_page = 0;
    bool sd_help_seen = false;
    int focused_slot = 0;
    uint64_t previous = monotonic_nanoseconds();
    uint64_t started = previous;
    uint64_t preview_started = previous;
    uint64_t home_opened_at = 0;
    bool entrance_active = false;
    float entrance_frames = 0.0f;
    float home_underlay_elapsed = 0.0f;
    float home_underlay_preview_elapsed = 0.0f;
    int preview_running_slot = -1;
    WmAppRenderer renderer = {
        .platform = platform,
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
        .audio = audio
    };
    const WmHomeEntryContext home_entry = {
        .home = home,
        .menu = &menu,
        .resource_scene = resource_scene,
        .fade = &fade,
        .drag = drag,
        .board_scene = board_scene,
        .audio = audio,
        .options_scene = options_scene,
        .options_held_arrow = &scene_input.options_held_arrow,
        .opened_at = &home_opened_at,
        .underlay_elapsed = &home_underlay_elapsed,
        .underlay_preview_elapsed = &home_underlay_preview_elapsed,
        .renderer = &renderer
    };
    const uint64_t frame_period = 1000000000ULL / 60ULL;
    uint64_t next_frame_deadline = previous + frame_period;

    while (running) {
        uint64_t frame_start = monotonic_nanoseconds();
        float elapsed = (float)(frame_start - previous) / 1000000000.0f;
        if (elapsed > 0.1f) elapsed = 0.1f;
        previous = frame_start;
        bool restart_started_this_frame = false;
        bool health_frame = wm_health_scene_active(health_scene);
        if (health_frame) {
            wm_health_scene_advance(health_scene, elapsed * 60.0f);
            if (!wm_health_scene_active(health_scene)) {
                started = frame_start;
                preview_started = frame_start;
                wm_resource_scene_restart(resource_scene);
                entrance_active = true;
                entrance_frames = 0.0f;
            }
        }
        if (!health_frame && entrance_active) {
            entrance_frames += elapsed * 60.0f;
            if (wm_menu_entrance_complete(entrance_frames))
                entrance_active = false;
        }
        if (!layout && !health_frame && !wm_home_overlay_active(home) &&
            !wm_menu_restart_active(&restart)) wm_menu_tick(&menu, elapsed);
        if (!layout && !health_frame && !wm_home_overlay_active(home) &&
            !wm_menu_restart_active(&restart) &&
            wm_scene_fader_advance(&fade.clock, elapsed * 60.0f)) {
            if (wm_menu_switch_screen_at_black(&menu, fade.destination)) {
                if (fade.destination == WM_SCREEN_SETTINGS) {
                    if (board_settings_request ==
                            WM_BOARD_ACTION_OPEN_CONNECT24_SETTINGS)
                        wm_options_scene_open_connect24(options_scene);
                    else if (board_settings_request ==
                                 WM_BOARD_ACTION_OPEN_SETTINGS)
                        wm_options_scene_open_internet(options_scene);
                    else
                        wm_options_scene_open(options_scene);
                } else if (fade.destination == WM_SCREEN_SD) {
                    wm_sd_scene_open(sd_scene, sd_page, sd_help_seen,
                                     WM_SD_MEDIA_READY);
                } else if (fade.destination == WM_SCREEN_BOARD) {
                    wm_options_scene_reset(options_scene);
                    board_entry_hover_pending = true;
                }
            }
            menu_pointer.hovered = (WmHit){WM_HIT_NONE, -1};
            menu_pointer.pressed = menu_pointer.hovered;
        }
        wm_platform_set_fade_alpha(
            platform, entrance_active
                ? wm_menu_entrance_alpha(entrance_frames)
                : wm_scene_fader_alpha(&fade.clock));
        if (wm_home_overlay_active(home)) {
            wm_home_overlay_advance(home, elapsed * 60.0f);
            WmHomeOutcome outcome = wm_home_overlay_take_outcome(home);
            if (outcome != WM_HOME_OUTCOME_NONE) {
                uint64_t paused = frame_start - home_opened_at;
                started += paused;
                preview_started += paused;
                wm_app_renderer_release_home_underlay(&renderer);
                home_hovered = WM_HOME_CONTROL_NONE;
                home_pressed = WM_HOME_CONTROL_NONE;
                menu_pointer.hovered = (WmHit){WM_HIT_NONE, -1};
                menu_pointer.pressed = menu_pointer.hovered;
                keyboard_focus = false;
                if (outcome == WM_HOME_OUTCOME_RETURN_MENU) {
                    wm_channel_drag_cancel(drag);
                    menu_pointer.drag_button = 0;
                    menu_pointer.drag_has_previous = false;
                    if (restart_scene && wm_menu_restart_start(&restart)) {
                        restart_started_this_frame = true;
                        wm_audio_reset_all(audio);
                        active_storage = NULL;
                    } else {
                        wm_menu_return_to_menu(&menu);
                        wm_resource_scene_restart(resource_scene);
                        wm_board_scene_reset(board_scene);
                        wm_options_scene_reset(options_scene);
                        wm_sd_scene_reset(sd_scene);
                        started = frame_start;
                        preview_started = frame_start;
                        preview_running_slot = -1;
                        active_storage = NULL;
                    }
                } else {
                    menu.home_open = false;
                }
            }
        }
        if (!layout && !restart_started_this_frame &&
            wm_menu_restart_active(&restart) &&
            wm_menu_restart_advance(&restart, elapsed * 60.0f)) {
            wm_menu_return_to_menu(&menu);
            wm_resource_scene_restart(resource_scene);
            wm_board_scene_reset(board_scene);
            wm_options_scene_reset(options_scene);
            wm_sd_scene_reset(sd_scene);
            started = frame_start;
            preview_started = frame_start;
            preview_running_slot = -1;
            active_storage = NULL;
            wm_audio_sync(audio, &menu);
        }
        if (!layout && wm_menu_restart_active(&restart))
            wm_platform_set_fade_alpha(platform,
                wm_menu_restart_alpha(&restart));

        WmEvent event;
        while (wm_platform_poll(platform, &event)) {
            bool memo_release_outside = event.outside_viewport &&
                event.type == WM_EVENT_POINTER_UP &&
                menu.screen == WM_SCREEN_BOARD &&
                wm_board_scene_dragging(board_scene);
            if (event.outside_viewport && menu_pointer.drag_button == 0 &&
                !memo_release_outside &&
                (event.type == WM_EVENT_POINTER_DOWN ||
                 event.type == WM_EVENT_POINTER_MOVE ||
                 event.type == WM_EVENT_POINTER_UP)) {
                event.type = WM_EVENT_POINTER_LEAVE;
            }
            if (event.type == WM_EVENT_POINTER_LEAVE &&
                menu_pointer.drag_button != 0 && !event.cancel_capture) {
                continue;
            }
            if (event.type == WM_EVENT_POINTER_LEAVE) {
                pointer_inside = false;
            } else if (event.type == WM_EVENT_POINTER_MOVE ||
                       event.type == WM_EVENT_POINTER_DOWN ||
                       event.type == WM_EVENT_POINTER_UP) {
                pointer_x = event.x;
                pointer_y = event.y;
                pointer_inside = true;
            }
            if (event.type == WM_EVENT_QUIT) {
                running = false;
            } else if (layout) {
                if (event.type == WM_EVENT_KEY_DOWN &&
                    (event.key == WM_KEY_ESCAPE || event.key == 'q')) running = false;
            } else if (health_frame) {
                if (wm_health_scene_ready(health_scene) &&
                    ((event.type == WM_EVENT_POINTER_DOWN &&
                      event.button == WM_POINTER_LEFT) ||
                     (event.type == WM_EVENT_KEY_DOWN &&
                      (event.key == WM_KEY_ENTER ||
                       event.key == WM_KEY_ESCAPE || event.key == ' ' ||
                       event.key == 'a' || event.key == 'A')))) {
                    if (wm_health_scene_accept(health_scene))
                        wm_audio_play(audio, "click");
                }
            } else if (entrance_active) {
                if (event.type == WM_EVENT_POINTER_MOVE)
                    wm_pointer_move(pointer, (float)event.x, (float)event.y);
                if (event.type == WM_EVENT_POINTER_LEAVE)
                    wm_pointer_hide(pointer);
            } else if (wm_scene_fader_active(&fade.clock)) {
                if (event.type == WM_EVENT_POINTER_MOVE)
                    wm_pointer_move(pointer, (float)event.x, (float)event.y);
                if (event.type == WM_EVENT_POINTER_LEAVE)
                    wm_pointer_hide(pointer);
            } else if (wm_menu_restart_active(&restart)) {
                if (event.type == WM_EVENT_POINTER_MOVE)
                    wm_pointer_move(pointer, (float)event.x, (float)event.y);
                if (event.type == WM_EVENT_POINTER_LEAVE)
                    wm_pointer_hide(pointer);
            } else if (wm_home_overlay_active(home)) {
                if (event.type == WM_EVENT_POINTER_MOVE) {
                    wm_pointer_move(pointer, (float)event.x, (float)event.y);
                    home_hovered = wm_home_overlay_hit(home, event.x, event.y);
                    wm_home_overlay_hover(home, home_hovered);
                } else if (event.type == WM_EVENT_POINTER_DOWN) {
                    wm_pointer_move(pointer, (float)event.x, (float)event.y);
                    home_pressed = event.button == WM_POINTER_LEFT
                        ? wm_home_overlay_hit(home, event.x, event.y)
                        : WM_HOME_CONTROL_NONE;
                } else if (event.type == WM_EVENT_POINTER_UP) {
                    wm_pointer_move(pointer, (float)event.x, (float)event.y);
                    home_hovered = wm_home_overlay_hit(home, event.x, event.y);
                    wm_home_overlay_hover(home, home_hovered);
                    if (event.button == WM_POINTER_LEFT &&
                        home_pressed != WM_HOME_CONTROL_NONE &&
                        home_pressed == home_hovered)
                        wm_home_overlay_activate(home, home_hovered);
                    home_pressed = WM_HOME_CONTROL_NONE;
                } else if (event.type == WM_EVENT_POINTER_LEAVE) {
                    wm_pointer_hide(pointer);
                    home_hovered = WM_HOME_CONTROL_NONE;
                    home_pressed = WM_HOME_CONTROL_NONE;
                    wm_home_overlay_hover(home, WM_HOME_CONTROL_NONE);
                } else if (event.type == WM_EVENT_KEY_DOWN) {
                    if (event.key == WM_KEY_HOME || event.key == 'h' ||
                        event.key == 'H' || event.key == WM_KEY_ESCAPE ||
                        event.key == WM_KEY_BACKSPACE) {
                        wm_home_overlay_back(home);
                    } else if (event.key == WM_KEY_ENTER &&
                               home_hovered != WM_HOME_CONTROL_NONE) {
                        wm_home_overlay_activate(home, home_hovered);
                    } else if (event.key == '1' || event.key == '2') {
                        wm_home_overlay_reconnect_key(home, (char)event.key,
                                                      true);
                    }
                }
            } else if (event.type == WM_EVENT_POINTER_MOVE) {
                wm_pointer_move(pointer, (float)event.x, (float)event.y);
                if (menu.screen == WM_SCREEN_GRID && resource_scene)
                    wm_resource_scene_pointer_moved(resource_scene);
                keyboard_focus = false;
                if (menu.screen == WM_SCREEN_SD && sd_scene &&
                    !menu.home_open && !menu.notice[0]) {
                    wm_app_sd_pointer_event(&scene_input, sd_scene, &event);
                    continue;
                }
                if (active_storage && menu.screen == WM_SCREEN_SETTINGS &&
                    !menu.home_open && !menu.notice[0]) {
                    wm_app_storage_pointer_event(
                        &scene_input, active_storage,
                        storage_scenes[WM_STORAGE_CHANNELS], audio, &event);
                    continue;
                }
                if (menu.screen == WM_SCREEN_BOARD && board_scene &&
                    !menu.home_open && !menu.notice[0]) {
                    wm_app_board_pointer_move(&board_input, board_scene,
                                              audio, event.x, event.y);
                    continue;
                }
                if (menu.screen == WM_SCREEN_SETTINGS && options_scene &&
                    !menu.home_open && !menu.notice[0]) {
                    wm_app_options_pointer_event(&scene_input, options_scene,
                                                 audio, &event);
                    continue;
                }
                wm_app_menu_pointer_move(&menu_pointer, &menu,
                                         resource_scene, preview_scene, drag,
                                         audio, event.x, event.y);
            } else if (event.type == WM_EVENT_POINTER_DOWN) {
                wm_pointer_move(pointer, (float)event.x, (float)event.y);
                keyboard_focus = false;
                if (menu.screen == WM_SCREEN_SD && sd_scene &&
                    !menu.home_open && !menu.notice[0]) {
                    wm_app_sd_pointer_event(&scene_input, sd_scene, &event);
                    continue;
                }
                if (active_storage && menu.screen == WM_SCREEN_SETTINGS &&
                    !menu.home_open && !menu.notice[0]) {
                    wm_app_storage_pointer_event(
                        &scene_input, active_storage,
                        storage_scenes[WM_STORAGE_CHANNELS], audio, &event);
                    continue;
                }
                if (menu.screen == WM_SCREEN_BOARD && board_scene &&
                    !menu.home_open && !menu.notice[0]) {
                    wm_app_board_pointer_down(&board_input, board_scene,
                                              audio, event.button,
                                              event.x, event.y);
                    continue;
                }
                if (menu.screen == WM_SCREEN_SETTINGS && options_scene &&
                    !menu.home_open && !menu.notice[0]) {
                    wm_app_options_pointer_event(&scene_input, options_scene,
                                                 audio, &event);
                    continue;
                }
                wm_app_menu_pointer_down(&menu_pointer, &menu,
                                         resource_scene, preview_scene, drag,
                                         audio, event.button, event.x, event.y);
            } else if (event.type == WM_EVENT_POINTER_UP) {
                wm_pointer_move(pointer, (float)event.x, (float)event.y);
                if (menu.screen == WM_SCREEN_SD && sd_scene &&
                    !menu.home_open && !menu.notice[0]) {
                    wm_app_sd_pointer_event(&scene_input, sd_scene, &event);
                    continue;
                }
                if (active_storage && menu.screen == WM_SCREEN_SETTINGS &&
                    !menu.home_open && !menu.notice[0]) {
                    wm_app_storage_pointer_event(
                        &scene_input, active_storage,
                        storage_scenes[WM_STORAGE_CHANNELS], audio, &event);
                    continue;
                }
                if (menu.screen == WM_SCREEN_BOARD && board_scene &&
                    !menu.home_open && !menu.notice[0]) {
                    wm_app_board_pointer_up(&board_input, board_scene,
                                            resource_scene, pointer, audio,
                                            event.button, event.x, event.y,
                                            event.outside_viewport);
                    continue;
                }
                if (menu.screen == WM_SCREEN_SETTINGS && options_scene &&
                    !menu.home_open && !menu.notice[0]) {
                    wm_app_options_pointer_event(&scene_input, options_scene,
                                                 audio, &event);
                    continue;
                }
                WmAppMenuPointerRelease release = wm_app_menu_pointer_up(
                    &menu_pointer, &menu, resource_scene, preview_scene,
                    drag, pointer, audio, &event);
                if (release.released_drag) continue;
                if (release.activated.type == WM_HIT_HOME) {
                    try_enter_home(&home_entry, frame_start, started,
                                   preview_started);
                } else if (release.activated.type != WM_HIT_NONE) {
                    wm_app_activate_hit(&menu, audio, resource_scene,
                                        board_scene, options_scene, &fade,
                                        release.activated);
                    if (release.activated.type == WM_HIT_BOARD &&
                        menu.screen == WM_SCREEN_BOARD)
                        board_entry_hover_pending = true;
                }
            } else if (event.type == WM_EVENT_POINTER_LEAVE) {
                wm_app_menu_pointer_leave(&menu_pointer, drag, audio);
                wm_pointer_hide(pointer);
                wm_sd_scene_hover(sd_scene,
                    (WmSdHit){WM_SD_CONTROL_NONE, 0});
                if (active_storage)
                    wm_storage_scene_hover(active_storage,
                        (WmStorageHit){WM_STORAGE_CONTROL_NONE, -1});
                wm_app_board_pointer_leave(&board_input, board_scene,
                                           event.cancel_capture);
                wm_app_options_pointer_leave(&scene_input, options_scene);
            } else if (event.type == WM_EVENT_KEY_DOWN) {
                keyboard_focus = true;
                bool composing = menu.screen == WM_SCREEN_BOARD &&
                    board_scene &&
                    wm_board_scene_child(board_scene) == WM_BOARD_CHILD_COMPOSE;
                bool editing_nickname = menu.screen == WM_SCREEN_SETTINGS &&
                    wm_options_scene_text_editing(options_scene);
                if (event.key == WM_KEY_HOME ||
                    (!composing && !editing_nickname &&
                     (event.key == 'h' || event.key == 'H'))) {
                    if (try_enter_home(&home_entry, frame_start, started,
                                       preview_started)) {
                        menu_pointer.hovered = (WmHit){WM_HIT_NONE, -1};
                        menu_pointer.pressed = menu_pointer.hovered;
                        keyboard_focus = false;
                    }
                    continue;
                }
                if (menu.screen == WM_SCREEN_SD && sd_scene &&
                    (event.key == WM_KEY_ESCAPE ||
                     event.key == WM_KEY_BACKSPACE)) {
                    wm_sd_scene_back(sd_scene);
                    continue;
                }
                if (active_storage &&
                    (event.key == WM_KEY_ESCAPE ||
                     event.key == WM_KEY_BACKSPACE)) {
                    if (wm_storage_scene_back(active_storage))
                        wm_audio_play(audio, "WIPL_SE_CANCEL");
                    continue;
                }
                if (menu.screen == WM_SCREEN_BOARD && board_scene &&
                    wm_board_scene_child(board_scene) == WM_BOARD_CHILD_COMPOSE &&
                    wm_app_board_compose_key(board_scene, audio, event.key)) {
                    continue;
                }
                if (editing_nickname) {
                    if (event.key == WM_KEY_BACKSPACE) {
                        wm_options_scene_backspace(options_scene);
                        continue;
                    }
                    if (event.key == WM_KEY_ENTER) {
                        wm_options_scene_activate(
                            options_scene, WM_OPTIONS_CONTROL_SETTINGS_NEXT);
                        continue;
                    }
                    if (event.key >= 32 && event.key <= 126) {
                        wm_options_scene_type_ascii(options_scene,
                                                     (char)event.key);
                        continue;
                    }
                }
                wm_app_handle_key(&menu, audio, resource_scene, board_scene,
                           options_scene, &fade, event.key,
                           &focused_slot);
            }
        }
        if (!running) break;
        WmAppBoardUpdate board_update = {
            .menu = &menu,
            .board = board_scene,
            .resource_scene = resource_scene,
            .audio = audio,
            .fade = &fade,
            .input = &board_input,
            .menu_hover = &menu_pointer.hovered,
            .settings_request = &board_settings_request,
            .state_path = board_state_path,
            .entry_hover_pending = &board_entry_hover_pending,
            .visited = &board_visited,
            .pointer_inside = pointer_inside,
            .pointer_x = pointer_x,
            .pointer_y = pointer_y
        };
        bool scene_updates_active = !layout && !health_frame &&
            !wm_home_overlay_active(home) && !wm_menu_restart_active(&restart);
        wm_app_board_advance(&board_update, elapsed * 60.0f,
                             scene_updates_active, !layout,
                             wm_home_overlay_active(home));
        if (scene_updates_active && menu.screen == WM_SCREEN_SETTINGS &&
            (options_scene || active_storage)) {
            WmAppSettingsUpdate settings_update = {
                .menu = &menu,
                .options = options_scene,
                .storage_scenes = storage_scenes,
                .active_storage = &active_storage,
                .audio = audio,
                .fade = &fade,
                .board_settings_request = &board_settings_request,
                .hovered = &scene_input.options_hovered,
                .pointer_inside = pointer_inside,
                .pointer_x = pointer_x,
                .pointer_y = pointer_y
            };
            wm_app_settings_advance(&settings_update, elapsed * 60.0f);
        }
        if (scene_updates_active && sd_scene && menu.screen == WM_SCREEN_SD) {
            WmAppSdUpdate sd_update = {
                .scene = sd_scene,
                .audio = audio,
                .fade = &fade,
                .page = &sd_page,
                .help_seen = &sd_help_seen
            };
            wm_app_sd_advance(&sd_update, elapsed * 60.0f);
        }
        if (scene_updates_active && drag) {
            wm_app_menu_drag_advance(&menu_pointer, &menu, resource_scene,
                                     preview_scene, drag, audio,
                                     elapsed * 60.0f);
        }
        if (!layout && !health_frame && !wm_menu_restart_active(&restart)) {
            wm_audio_sync(audio, &menu);
            /* Health may queue the badge before startup audio begins. */
            if (menu.screen == WM_SCREEN_GRID &&
                !wm_home_overlay_active(home) &&
                wm_resource_scene_take_new_mail_sound(resource_scene)) {
                wm_audio_play(audio, "WIPL_SE_NEW_ARRIVAL");
            }
        }
        /* The source switches from P1_Def to P1_Cat only while a channel or
         * Board memo is held. Derive the pointer pose after drag controllers
         * advance so a release returns to the normal hand in this frame. */
        WmChannelDragPhase drag_phase = wm_channel_drag_state(drag).phase;
        bool memo_held = menu.screen == WM_SCREEN_BOARD &&
                         wm_board_scene_dragging(board_scene);
        wm_pointer_set_grabbed(pointer,
            wm_pointer_grabbed_for_state(drag_phase, memo_held));
        if (menu.screen != WM_SCREEN_PREVIEW) {
            preview_running_slot = -1;
        } else if (menu.transition == WM_TRANSITION_SELECT) {
            /* The native banner clock starts after the 28-frame grid zoom.
             * The zoom capture itself samples the banner's initial pose. */
            preview_started = frame_start;
            preview_running_slot = -1;
        } else if (menu.transition == WM_TRANSITION_NONE &&
                   preview_running_slot != menu.selected) {
            bool navigated = preview_running_slot >= 0;
            preview_started = frame_start;
            preview_running_slot = menu.selected;
            wm_preview_scene_set_module_lead(preview_scene,
                                               navigated ? 10.0f : 0.0f);
        }
        WmAppRenderFrame render_frame = {
            .menu = &menu,
            .fade = &fade,
            .restart = &restart,
            .active_storage = active_storage,
            .hover = menu_pointer.hovered,
            .board_hovered = board_input.hovered,
            .health_frame = health_frame,
            .keyboard_focus = keyboard_focus,
            .focused_slot = focused_slot,
            .frame_start = frame_start,
            .started = started,
            .preview_started = preview_started,
            .home_underlay_elapsed = home_underlay_elapsed,
            .home_underlay_preview_elapsed = home_underlay_preview_elapsed
        };
        wm_app_renderer_draw(&renderer, &render_frame);

        /* Follow a fixed deadline so sleep overshoot does not accumulate and
         * slow every 28-frame zoom by roughly one millisecond per frame. */
        uint64_t frame_end = monotonic_nanoseconds();
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
