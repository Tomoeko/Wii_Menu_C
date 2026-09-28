#define _POSIX_C_SOURCE 200809L

#include "frame_render.h"
#include "input_routing.h"

#include "wii_menu/animation/scene_fader.h"
#include "wii_menu/audio/audio.h"
#include "wii_menu/audio/hover_audio.h"
#include "wii_menu/board/board_scene.h"
#include "wii_menu/fonts/font_cache.h"
#include "wii_menu/input/channel_drag.h"
#include "wii_menu/input/pointer.h"
#include "wii_menu/layout/layout_runtime.h"
#include "wii_menu/menu/menu.h"
#include "wii_menu/menu/menu_restart.h"
#include "wii_menu/persistence/board_store.h"
#include "wii_menu/platform/platform.h"
#include "wii_menu/render/texture_cache.h"
#include "wii_menu/render/ui.h"
#include "wii_menu/resources/resource_bmg.h"
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
#include <stdlib.h>
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

static void home_cue(void *context, const char *symbol) {
    wm_audio_play((WmAudio *)context, symbol);
}

static void home_remote_changed(void *context,
                                const WmHomeRemoteState *state) {
    WmAudio *audio = context;
    wm_audio_set_volume(audio, state->volume);
    wm_audio_set_muted(audio, state->muted);
}

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

static bool same_hit(WmHit first, WmHit second) {
    return first.type == second.type && first.slot == second.slot;
}

static WmHit hit_menu(const WmMenu *menu, const WmResourceScene *resource_scene,
                      const WmPreviewScene *preview_scene, int x, int y) {
    if (menu->notice[0]) return wm_ui_notice_hit(menu, x, y);
    if (resource_scene && menu->screen == WM_SCREEN_GRID && !menu->home_open) {
        return wm_resource_scene_hit(resource_scene, menu, x, y);
    }
    if (preview_scene && !menu->home_open &&
        wm_preview_scene_available(preview_scene, menu)) {
        return wm_preview_scene_hit(preview_scene, menu, x, y);
    }
    return wm_ui_hit(menu, x, y);
}

static WmHit hover_menu(const WmMenu *menu,
                        const WmResourceScene *resource_scene,
                        const WmPreviewScene *preview_scene,
                        int x, int y, WmHit held) {
    if (!menu->notice[0] && menu->screen == WM_SCREEN_PREVIEW &&
        preview_scene && !menu->home_open &&
        wm_preview_scene_available(preview_scene, menu)) {
        return wm_preview_scene_hover_hit(preview_scene, menu, x, y, held);
    }
    return hit_menu(menu, resource_scene, preview_scene, x, y);
}

static WmHit drag_hover(const WmResourceScene *scene, const WmMenu *menu,
                        int x, int y) {
    WmHit hit = wm_resource_scene_hit(scene, menu, x, y);
    return hit.type == WM_HIT_PAGE_PREVIOUS || hit.type == WM_HIT_PAGE_NEXT
               ? hit : (WmHit){WM_HIT_NONE, -1};
}

static void drag_point(WmChannelDrag *drag, const WmResourceScene *scene,
                        const WmMenu *menu, int x, int y) {
    WmHit arrow = drag_hover(scene, menu, x, y);
    int edge = arrow.type == WM_HIT_PAGE_PREVIOUS ? -1 :
               arrow.type == WM_HIT_PAGE_NEXT ? 1 : 0;
    int slot = wm_resource_scene_slot_at(scene, menu, x, y);
    wm_channel_drag_point(drag, (float)x, (float)y, slot, edge);
}

static void play_drag_sound(WmAudio *audio, WmChannelDragSound sound) {
    if (sound == WM_CHANNEL_DRAG_SOUND_DROP) wm_audio_play(audio, "drop");
    if (sound == WM_CHANNEL_DRAG_SOUND_INVALID_DROP)
        wm_audio_play(audio, "invalidDrop");
}

static void play_hover_cue(WmAudio *audio, WmHit hit) {
    const char *cue = wm_hover_audio_menu_cue(hit.type);
    if (cue) wm_audio_play(audio, cue);
}

static const char *storage_click_cue(WmStorageControl control,
                                     bool channel_storage) {
    switch (control) {
        case WM_STORAGE_CONTROL_PREVIOUS:
        case WM_STORAGE_CONTROL_NEXT:
            return "WSD_SELECT";
        case WM_STORAGE_CONTROL_WII_TAB:
        case WM_STORAGE_CONTROL_SD_TAB:
            return "WIPL_SE_BT_PUSH";
        case WM_STORAGE_CONTROL_SLOT:
        case WM_STORAGE_CONTROL_MOVE:
        case WM_STORAGE_CONTROL_COPY:
        case WM_STORAGE_CONTROL_ERASE:
            return channel_storage ? "WIPL_SE_DECIDE" : "WIPL_SE_BT_PUSH";
        case WM_STORAGE_CONTROL_BACK:
        case WM_STORAGE_CONTROL_NO:
            return "WIPL_SE_CANCEL";
        case WM_STORAGE_CONTROL_YES:
            return "WIPL_SE_DECIDE";
        case WM_STORAGE_CONTROL_NONE:
            return NULL;
    }
    return NULL;
}

static bool compose_scroll_arrow(WmBoardControl control) {
    return control == WM_BOARD_CONTROL_COMPOSE_SCROLL_UP ||
           control == WM_BOARD_CONTROL_COMPOSE_SCROLL_DOWN;
}

static bool compose_keyboard_control(WmBoardControl control) {
    return control >= WM_BOARD_CONTROL_COMPOSE_KEY_FIRST &&
           control <= WM_BOARD_CONTROL_COMPOSE_KEY_LAST;
}

static void play_board_hover_cue(WmAudio *audio,
                                 WmBoardControl control) {
    const char *cue = wm_hover_audio_board_cue(control);
    if (cue) wm_audio_play(audio, cue);
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
    bool catalog_loaded = !layout_path && assets && wm_catalog_load(&menu, assets);

    WmLayout *layout = NULL;
    if (layout_path) {
        char error[160];
        layout = wm_layout_load_json(layout_path, error, sizeof(error));
        if (!layout) {
            fprintf(stderr, "Could not load layout: %s\n", error);
            return 1;
        }
    }

    WmPlatform *platform = wm_platform_create("Wii Menu in C", 960, 540);
    if (!platform) {
        fprintf(stderr, "Could not initialize the graphics backend.\n");
        wm_layout_destroy(layout);
        return 1;
    }
    WmTextureCache *textures = NULL;
    WmFontCache *fonts = NULL;
    if (layout) {
        textures = wm_texture_cache_create(platform, raw_root, 128u * 1024u * 1024u);
        fonts = wm_font_cache_create(platform, raw_root, 16u * 1024u * 1024u);
        if (!textures || !fonts) {
            fprintf(stderr, "Could not open the layout asset directory.\n");
            wm_texture_cache_destroy(textures);
            wm_font_cache_destroy(fonts);
            wm_layout_destroy(layout);
            wm_platform_destroy(platform);
            return 1;
        }
    }
    WmTextureCache *scene_textures = !layout && assets
                                        ? wm_texture_cache_create(platform, assets,
                                                                   128u * 1024u * 1024u)
                                        : NULL;
    WmFontCache *scene_fonts = !layout && assets
                                    ? wm_font_cache_create(platform, assets,
                                                            16u * 1024u * 1024u)
                                    : NULL;
    WmResourceScene *resource_scene = !layout && assets
                                          ? wm_resource_scene_create(platform, assets,
                                                                     &menu, scene_textures,
                                                                     scene_fonts)
                                          : NULL;
    WmPreviewScene *preview_scene = !layout && assets
                                        ? wm_preview_scene_create(platform, assets, &menu,
                                                                  scene_textures,
                                                                  scene_fonts)
                                        : NULL;
    WmBoardScene *board_scene = !layout && assets && scene_textures && scene_fonts
        ? wm_board_scene_create(platform, assets, scene_textures, scene_fonts)
        : NULL;
    WmMenuRestartScene *restart_scene = !layout && assets &&
        scene_textures && scene_fonts
        ? wm_menu_restart_scene_create(platform, assets, scene_textures,
                                       scene_fonts)
        : NULL;
    WmHealthScene *health_scene = !layout && assets && scene_textures &&
        scene_fonts ? wm_health_scene_create(platform, assets, scene_textures,
                                            scene_fonts, true, NULL) : NULL;
    char *board_state_path = NULL;
    if (board_scene && assets) {
        static const char suffix[] = "/.board-memos.json";
        static const char contacts_suffix[] = "/.board-contacts.json";
        size_t length = strlen(assets);
        if (length <= SIZE_MAX - sizeof(suffix)) {
            board_state_path = malloc(length + sizeof(suffix));
            if (board_state_path) {
                memcpy(board_state_path, assets, length);
                memcpy(board_state_path + length, suffix, sizeof(suffix));
                char error[160] = {0};
                if (wm_board_store_load(board_state_path, board_scene,
                                        error, sizeof(error)) ==
                    WM_BOARD_STORE_ERROR) {
                    fprintf(stderr, "Could not load Message Board memos: %s\n",
                            error);
                }
            }
        }
        if (length <= SIZE_MAX - sizeof(contacts_suffix)) {
            char *contacts_path = malloc(length + sizeof(contacts_suffix));
            if (contacts_path) {
                memcpy(contacts_path, assets, length);
                memcpy(contacts_path + length, contacts_suffix,
                       sizeof(contacts_suffix));
                char error[160] = {0};
                if (wm_board_scene_load_contacts(board_scene, contacts_path,
                                                 error, sizeof(error)) ==
                    WM_BOARD_CONTACT_STORE_ERROR) {
                    fprintf(stderr, "Could not load Address Book contacts: %s\n",
                            error);
                }
                free(contacts_path);
            }
        }
    }
    WmOptionsScene *options_scene = !layout && assets && scene_textures && scene_fonts
        ? wm_options_scene_create(platform, assets, scene_textures, scene_fonts)
        : NULL;
    WmSdScene *sd_scene = !layout && assets && scene_textures && scene_fonts
        ? wm_sd_scene_create(platform, assets, scene_textures, scene_fonts)
        : NULL;
    char message_error[160] = {0};
    WmBmg *messages = !layout && assets
        ? wm_bmg_load_assets(assets, "eng", message_error,
                             sizeof(message_error)) : NULL;
    if (sd_scene && messages)
        wm_sd_scene_set_messages(sd_scene, wm_bmg_message, messages);
    WmStorageScene *storage_scenes[3] = {0};
    if (!layout && assets && scene_textures && scene_fonts) {
        for (int kind = 0; kind < 3; kind++) {
            storage_scenes[kind] = wm_storage_scene_create(
                platform, assets, scene_textures, scene_fonts,
                (WmStorageKind)kind);
        }
        if (storage_scenes[WM_STORAGE_CHANNELS]) {
            WmStorageRecord records[WM_SLOT_COUNT];
            size_t count = 0;
            for (int slot = 1; slot < WM_SLOT_COUNT; slot++) {
                const WmChannel *channel = &menu.slots[slot];
                if (!channel->occupied || !channel->icon_layout[0]) continue;
                WmStorageRecord *record = &records[count++];
                memset(record, 0, sizeof(*record));
                snprintf(record->id, sizeof(record->id), "%s", channel->id);
                snprintf(record->title, sizeof(record->title), "%s",
                         channel->title);
                snprintf(record->icon_layout, sizeof(record->icon_layout),
                         "%s", channel->icon_layout);
                record->blocks = 1;
            }
            if (!wm_storage_scene_set_medium(
                    storage_scenes[WM_STORAGE_CHANNELS], WM_STORAGE_WII,
                    WM_STORAGE_READY, records, count, 0)) {
                fprintf(stderr, "Could not populate Channels storage.\n");
            }
        }
    }
    WmPointer *pointer = !layout && scene_textures
                             ? wm_pointer_create(platform, assets, scene_textures)
                             : NULL;
    WmAudio *audio = !layout && assets ? wm_audio_create(assets) : NULL;
    WmHomeOverlay *home = !layout && assets && scene_textures && scene_fonts
        ? wm_home_overlay_create(platform, assets, scene_textures, scene_fonts,
                                 home_cue, home_remote_changed, audio)
        : NULL;
    WmChannelDrag *drag = !layout && assets && scene_textures && scene_fonts
        ? wm_channel_drag_create(platform, assets, scene_textures,
                                  scene_fonts, true) : NULL;
    if (assets && !layout && !pointer) {
        fprintf(stderr, "Could not load the source Wii hand pointer.\n");
    }
    if (assets && !layout && !catalog_loaded && !resource_scene) {
        fprintf(stderr, "Could not load a valid prepared channel catalog; showing Disc only.\n");
    }

    bool running = true;
    bool keyboard_focus = false;
    WmHit hover = {WM_HIT_NONE, -1};
    WmHit pressed = {WM_HIT_NONE, -1};
    WmBoardHit board_hovered = {WM_BOARD_CONTROL_NONE, 0};
    WmBoardHit board_pressed = {WM_BOARD_CONTROL_NONE, 0};
    bool board_held_keyboard = false;
    int board_press_x = 0;
    int board_press_y = 0;
    int pointer_x = 0;
    int pointer_y = 0;
    bool pointer_inside = false;
    bool board_entry_hover_pending = false;
    bool board_visited = false;
    WmOptionsControl options_hovered = WM_OPTIONS_CONTROL_NONE;
    WmOptionsControl options_pressed = WM_OPTIONS_CONTROL_NONE;
    bool options_held_arrow = false;
    WmSdHit sd_pressed = {WM_SD_CONTROL_NONE, 0};
    WmHomeControl home_hovered = WM_HOME_CONTROL_NONE;
    WmHomeControl home_pressed = WM_HOME_CONTROL_NONE;
    WmStorageScene *active_storage = NULL;
    WmStorageHit storage_pressed = {WM_STORAGE_CONTROL_NONE, -1};
    WmAppSceneFade fade = {0};
    WmBoardAction board_settings_request = WM_BOARD_ACTION_NONE;
    WmMenuRestartClock restart = {0};
    unsigned sd_page = 0;
    bool sd_help_seen = false;
    WmPointerButton drag_button = 0;
    float drag_previous_x = 0.0f, drag_previous_y = 0.0f;
    float drag_pitch = 1.0f;
    bool drag_has_previous = false;
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
        .options_held_arrow = &options_held_arrow,
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
            hover = (WmHit){WM_HIT_NONE, -1};
            pressed = hover;
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
                hover = (WmHit){WM_HIT_NONE, -1};
                pressed = hover;
                keyboard_focus = false;
                if (outcome == WM_HOME_OUTCOME_RETURN_MENU) {
                    wm_channel_drag_cancel(drag);
                    drag_button = 0;
                    drag_has_previous = false;
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
            if (event.outside_viewport && drag_button == 0 &&
                !memo_release_outside &&
                (event.type == WM_EVENT_POINTER_DOWN ||
                 event.type == WM_EVENT_POINTER_MOVE ||
                 event.type == WM_EVENT_POINTER_UP)) {
                event.type = WM_EVENT_POINTER_LEAVE;
            }
            if (event.type == WM_EVENT_POINTER_LEAVE &&
                drag_button != 0 && !event.cancel_capture) {
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
                    wm_sd_scene_hover(sd_scene,
                        wm_sd_scene_hit(sd_scene, event.x, event.y));
                    continue;
                }
                if (active_storage && menu.screen == WM_SCREEN_SETTINGS &&
                    !menu.home_open && !menu.notice[0]) {
                    WmStorageHit next = wm_storage_scene_hit(
                        active_storage, event.x, event.y);
                    WmStorageSnapshot snapshot =
                        wm_storage_scene_snapshot(active_storage);
                    bool selected_tab =
                        (next.control == WM_STORAGE_CONTROL_WII_TAB &&
                         snapshot.tab == WM_STORAGE_WII) ||
                        (next.control == WM_STORAGE_CONTROL_SD_TAB &&
                         snapshot.tab == WM_STORAGE_SD);
                    if (wm_storage_scene_hover(active_storage, next) &&
                        next.control != WM_STORAGE_CONTROL_NONE &&
                        !selected_tab) {
                        wm_audio_play(audio,
                            wm_hover_audio_storage_cue(next.control));
                    }
                    continue;
                }
                if (menu.screen == WM_SCREEN_BOARD && board_scene &&
                    !menu.home_open && !menu.notice[0]) {
                    if (board_pressed.control == WM_BOARD_CONTROL_MEMO &&
                        !wm_board_scene_dragging(board_scene)) {
                        int dx = event.x - board_press_x;
                        int dy = event.y - board_press_y;
                        if (dx * dx + dy * dy > 9 &&
                            wm_board_scene_pointer_down(board_scene,
                                board_pressed, board_press_x, board_press_y))
                            wm_board_scene_pointer_move(board_scene,
                                                        event.x, event.y);
                    } else if (wm_board_scene_dragging(board_scene)) {
                        wm_board_scene_pointer_move(board_scene,
                                                    event.x, event.y);
                    }
                    if (wm_board_scene_dragging(board_scene)) continue;
                    WmBoardHit next = wm_board_scene_hit(board_scene,
                                                          event.x, event.y);
                    if (next.control != board_hovered.control ||
                        next.memo_index != board_hovered.memo_index) {
                        play_board_hover_cue(audio, next.control);
                    }
                    board_hovered = next;
                    wm_board_scene_hover(board_scene, next);
                    continue;
                }
                if (menu.screen == WM_SCREEN_SETTINGS && options_scene &&
                    !menu.home_open && !menu.notice[0]) {
                    WmOptionsControl next = wm_options_scene_hit(options_scene,
                                                                   event.x, event.y);
                    if (next != options_hovered && next != WM_OPTIONS_CONTROL_NONE)
                        wm_audio_play(audio,
                            wm_options_scene_hover_cue(options_scene, next));
                    options_hovered = next;
                    wm_options_scene_hover(options_scene, next);
                    continue;
                }
                if (drag_button != 0)
                    drag_point(drag, resource_scene, &menu, event.x, event.y);
                WmHit next = drag_button != 0
                    ? drag_hover(resource_scene, &menu, event.x, event.y)
                    : hover_menu(&menu, resource_scene, preview_scene,
                                 event.x, event.y, hover);
                if (!same_hit(hover, next)) play_hover_cue(audio, next);
                hover = next;
            } else if (event.type == WM_EVENT_POINTER_DOWN) {
                wm_pointer_move(pointer, (float)event.x, (float)event.y);
                keyboard_focus = false;
                if (menu.screen == WM_SCREEN_SD && sd_scene &&
                    !menu.home_open && !menu.notice[0]) {
                    sd_pressed = event.button == WM_POINTER_LEFT
                        ? wm_sd_scene_hit(sd_scene, event.x, event.y)
                        : (WmSdHit){WM_SD_CONTROL_NONE, 0};
                    continue;
                }
                if (active_storage && menu.screen == WM_SCREEN_SETTINGS &&
                    !menu.home_open && !menu.notice[0]) {
                    storage_pressed = event.button == WM_POINTER_LEFT
                        ? wm_storage_scene_hit(active_storage, event.x, event.y)
                        : (WmStorageHit){WM_STORAGE_CONTROL_NONE, -1};
                    continue;
                }
                if (menu.screen == WM_SCREEN_BOARD && board_scene &&
                    !menu.home_open && !menu.notice[0]) {
                    WmBoardHit board_hit = wm_board_scene_hit(
                        board_scene, event.x, event.y);
                    WmBoardPhase press_phase = wm_board_scene_phase(board_scene);
                    if (press_phase == WM_BOARD_DATE_SCROLL ||
                        press_phase == WM_BOARD_MEMO_PAGE) {
                        /* Hover remains live during a turn, but a press begun
                         * while locked must not activate after it settles. */
                        board_pressed =
                            (WmBoardHit){WM_BOARD_CONTROL_NONE, SIZE_MAX};
                        continue;
                    }
                    if (event.button == WM_POINTER_RIGHT) {
                        wm_board_scene_hover(board_scene, board_hit);
                        if (wm_board_scene_activate_secondary(board_scene,
                                                               board_hit)) {
                            board_pressed = (WmBoardHit){WM_BOARD_CONTROL_NONE, 0};
                            wm_app_play_board_compose_cues(audio, board_scene);
                            continue;
                        }
                    }
                    if ((event.button == WM_POINTER_MIDDLE ||
                         event.button == WM_POINTER_RIGHT) &&
                        board_hit.control == WM_BOARD_CONTROL_MEMO &&
                        wm_board_scene_pointer_down(board_scene, board_hit,
                                                     event.x, event.y)) {
                        board_pressed = (WmBoardHit){WM_BOARD_CONTROL_NONE, 0};
                        continue;
                    }
                    board_pressed = event.button == WM_POINTER_LEFT
                        ? board_hit
                        : (WmBoardHit){WM_BOARD_CONTROL_NONE, 0};
                    board_press_x = event.x;
                    board_press_y = event.y;
                    board_held_keyboard = event.button == WM_POINTER_LEFT &&
                        compose_keyboard_control(board_pressed.control) &&
                        wm_board_scene_hold_compose_control(
                            board_scene, board_pressed.control);
                    if (board_held_keyboard) {
                        board_pressed = (WmBoardHit){WM_BOARD_CONTROL_NONE, 0};
                        wm_app_play_board_compose_cues(audio, board_scene);
                    }
                    continue;
                }
                if (menu.screen == WM_SCREEN_SETTINGS && options_scene &&
                    !menu.home_open && !menu.notice[0]) {
                    options_pressed = event.button == WM_POINTER_LEFT
                        ? wm_options_scene_hit(options_scene, event.x, event.y)
                        : WM_OPTIONS_CONTROL_NONE;
                    options_held_arrow = event.button == WM_POINTER_LEFT &&
                        wm_options_scene_pointer_down(options_scene,
                                                        options_pressed);
                    if (options_held_arrow) wm_audio_play(audio, "click");
                    continue;
                }
                if ((event.button == WM_POINTER_MIDDLE ||
                     event.button == WM_POINTER_RIGHT) && drag && resource_scene &&
                    menu.screen == WM_SCREEN_GRID &&
                    menu.transition == WM_TRANSITION_NONE &&
                    !menu.home_open && !menu.notice[0]) {
                    int slot = wm_resource_scene_slot_at(resource_scene, &menu,
                                                          event.x, event.y);
                    if (wm_channel_drag_start(drag, slot, &menu,
                                               (float)event.x, (float)event.y)) {
                        drag_button = event.button;
                        drag_has_previous = false;
                        drag_pitch = 1.0f;
                        drag_point(drag, resource_scene, &menu,
                                    event.x, event.y);
                        wm_audio_play(audio, "grab");
                        wm_audio_hold_loop(audio, "drag", 0.0f, 0.0f, 1.0f);
                        hover = (WmHit){WM_HIT_NONE, -1};
                        pressed = hover;
                        continue;
                    }
                }
                WmHit exact = hit_menu(&menu, resource_scene, preview_scene,
                                       event.x, event.y);
                WmHit next = hover_menu(&menu, resource_scene, preview_scene,
                                        event.x, event.y, hover);
                if (!same_hit(hover, next)) play_hover_cue(audio, next);
                hover = next;
                pressed = event.button == WM_POINTER_LEFT
                              ? exact : (WmHit){WM_HIT_NONE, -1};
            } else if (event.type == WM_EVENT_POINTER_UP) {
                wm_pointer_move(pointer, (float)event.x, (float)event.y);
                if (menu.screen == WM_SCREEN_SD && sd_scene &&
                    !menu.home_open && !menu.notice[0]) {
                    WmSdHit next = wm_sd_scene_hit(sd_scene, event.x, event.y);
                    if (event.button == WM_POINTER_LEFT &&
                        sd_pressed.control != WM_SD_CONTROL_NONE &&
                        next.control == sd_pressed.control &&
                        next.slot == sd_pressed.slot)
                        wm_sd_scene_activate(sd_scene, next);
                    sd_pressed = (WmSdHit){WM_SD_CONTROL_NONE, 0};
                    continue;
                }
                if (active_storage && menu.screen == WM_SCREEN_SETTINGS &&
                    !menu.home_open && !menu.notice[0]) {
                    WmStorageHit next = wm_storage_scene_hit(
                        active_storage, event.x, event.y);
                    if (event.button == WM_POINTER_LEFT &&
                        storage_pressed.control != WM_STORAGE_CONTROL_NONE &&
                        next.control == storage_pressed.control &&
                        next.slot == storage_pressed.slot &&
                        wm_storage_scene_activate(active_storage, next)) {
                        bool channels = active_storage ==
                                        storage_scenes[WM_STORAGE_CHANNELS];
                        const char *cue = storage_click_cue(next.control,
                                                             channels);
                        if (cue) wm_audio_play(audio, cue);
                    }
                    storage_pressed = (WmStorageHit){WM_STORAGE_CONTROL_NONE, -1};
                    continue;
                }
                if (menu.screen == WM_SCREEN_BOARD && board_scene &&
                    !menu.home_open && !menu.notice[0]) {
                    if (board_held_keyboard) {
                        wm_board_scene_release_compose_control(board_scene);
                        board_held_keyboard = false;
                        board_pressed = (WmBoardHit){WM_BOARD_CONTROL_NONE, 0};
                        continue;
                    }
                    if (wm_board_scene_dragging(board_scene)) {
                        wm_board_scene_pointer_up(board_scene,
                                                  event.x, event.y);
                        if (event.outside_viewport) wm_pointer_hide(pointer);
                        board_pressed = (WmBoardHit){WM_BOARD_CONTROL_NONE, 0};
                        continue;
                    }
                    WmBoardHit next = wm_board_scene_hit(board_scene,
                                                          event.x, event.y);
                    bool confirmed_board_click =
                        event.button == WM_POINTER_LEFT &&
                        board_pressed.control != WM_BOARD_CONTROL_NONE &&
                        next.control == board_pressed.control &&
                        next.memo_index == board_pressed.memo_index;
                    if (confirmed_board_click &&
                        (next.control == WM_BOARD_CONTROL_BACK ||
                         next.control == WM_BOARD_CONTROL_CALENDAR ||
                         next.control == WM_BOARD_CONTROL_CREATE)) {
                        wm_resource_scene_dismiss_balloon(resource_scene);
                    }
                    if (confirmed_board_click &&
                        wm_board_scene_activate(board_scene, next)) {
                        const char *cue = "confirm";
                        if (next.control == WM_BOARD_CONTROL_MEMO)
                            cue = "WIPL_SE_BOARD_SELECT";
                        else if (next.control == WM_BOARD_CONTROL_MEMO_BACK)
                            cue = "WIPL_SE_BOARD_UNSELECT";
                        else if (next.control == WM_BOARD_CONTROL_MEMO_TRASH)
                            cue = "WIPL_SE_BT_PUSH";
                        else if (next.control == WM_BOARD_CONTROL_CALENDAR_DAY)
                            /* Calendar queues its date-select cue itself. */
                            cue = NULL;
                        else if (next.control == WM_BOARD_CONTROL_BACK)
                            cue = "confirm";
                        else if (next.control == WM_BOARD_CONTROL_PREVIOUS ||
                                 next.control == WM_BOARD_CONTROL_NEXT)
                            cue = NULL;
                        else if (compose_scroll_arrow(next.control))
                            cue = "WIPL_SE_LINE_SCROLL";
                        else if (next.control ==
                                     WM_BOARD_CONTROL_COMPOSE_ADDRESS_PREVIOUS ||
                                 next.control ==
                                     WM_BOARD_CONTROL_COMPOSE_ADDRESS_NEXT)
                            cue = NULL;
                        else if (next.control ==
                                     WM_BOARD_CONTROL_COMPOSE_ADDRESS ||
                                 next.control ==
                                     WM_BOARD_CONTROL_COMPOSE_ADDRESS_WII ||
                                 next.control ==
                                     WM_BOARD_CONTROL_COMPOSE_ADDRESS_OTHERS)
                            cue = "WIPL_SE_DECIDE";
                        else if (next.control == WM_BOARD_CONTROL_MEMO_SCROLL_UP ||
                                 next.control == WM_BOARD_CONTROL_MEMO_SCROLL_DOWN)
                            cue = NULL;
                        else if (next.control == WM_BOARD_CONTROL_COMPOSE_EDIT ||
                                 next.control ==
                                     WM_BOARD_CONTROL_COMPOSE_ADDRESS_EDIT ||
                                 next.control ==
                                     WM_BOARD_CONTROL_COMPOSE_ADDRESS_DIALOG_OK ||
                                 next.control ==
                                     WM_BOARD_CONTROL_COMPOSE_ADDRESS_MII ||
                                 next.control ==
                                     WM_BOARD_CONTROL_COMPOSE_ADDRESS_INFO ||
                                 next.control ==
                                     WM_BOARD_CONTROL_COMPOSE_ADDRESS_CHANGE_NICKNAME ||
                                 next.control ==
                                     WM_BOARD_CONTROL_COMPOSE_ADDRESS_ERASE ||
                                 next.control ==
                                     WM_BOARD_CONTROL_COMPOSE_ADDRESS_DIALOG_YES ||
                                 next.control ==
                                     WM_BOARD_CONTROL_COMPOSE_ADDRESS_DIALOG_NO ||
                                 compose_keyboard_control(next.control))
                            cue = NULL;
                        if (!wm_app_play_board_compose_cues(audio, board_scene) && cue)
                            wm_audio_play(audio, cue);
                        wm_app_play_board_sound_events(audio, board_scene);
                    }
                    board_pressed = (WmBoardHit){WM_BOARD_CONTROL_NONE, 0};
                    continue;
                }
                if (menu.screen == WM_SCREEN_SETTINGS && options_scene &&
                    !menu.home_open && !menu.notice[0]) {
                    if (options_held_arrow) {
                        wm_options_scene_pointer_up(options_scene);
                        options_held_arrow = false;
                        options_pressed = WM_OPTIONS_CONTROL_NONE;
                        continue;
                    }
                    WmOptionsControl next = wm_options_scene_hit(options_scene,
                                                                   event.x, event.y);
                    const char *click_cue =
                        wm_options_scene_click_cue(options_scene, next);
                    if (event.button == WM_POINTER_LEFT &&
                        options_pressed != WM_OPTIONS_CONTROL_NONE &&
                        options_pressed == next &&
                        wm_options_scene_activate(options_scene, next)) {
                        if (click_cue) wm_audio_play(audio, click_cue);
                    }
                    options_pressed = WM_OPTIONS_CONTROL_NONE;
                    continue;
                }
                if (drag_button != 0 && event.button == drag_button) {
                    drag_point(drag, resource_scene, &menu, event.x, event.y);
                    play_drag_sound(audio, wm_channel_drag_release(
                        drag, &menu, menu.transition == WM_TRANSITION_PAGE));
                    wm_audio_stop_loop(audio, "drag");
                    drag_button = 0;
                    drag_has_previous = false;
                    if (event.outside_viewport) {
                        wm_pointer_hide(pointer);
                        hover = (WmHit){WM_HIT_NONE, -1};
                    } else {
                        hover = hit_menu(&menu, resource_scene, preview_scene,
                                         event.x, event.y);
                    }
                    pressed = (WmHit){WM_HIT_NONE, -1};
                    continue;
                }
                WmHit exact = hit_menu(&menu, resource_scene, preview_scene,
                                       event.x, event.y);
                hover = hover_menu(&menu, resource_scene, preview_scene,
                                   event.x, event.y, hover);
                if (event.button == WM_POINTER_LEFT &&
                    pressed.type != WM_HIT_NONE && same_hit(pressed, exact)) {
                    if (exact.type == WM_HIT_HOME) {
                        try_enter_home(&home_entry, frame_start, started,
                                       preview_started);
                    } else {
                        wm_app_activate_hit(&menu, audio, resource_scene, board_scene,
                                     options_scene, &fade, exact);
                        if (exact.type == WM_HIT_BOARD &&
                            menu.screen == WM_SCREEN_BOARD)
                            board_entry_hover_pending = true;
                    }
                }
                pressed = (WmHit){WM_HIT_NONE, -1};
            } else if (event.type == WM_EVENT_POINTER_LEAVE) {
                if (drag_button != 0) {
                    wm_channel_drag_cancel(drag);
                    wm_audio_stop_loop(audio, "drag");
                    drag_button = 0;
                    drag_has_previous = false;
                }
                wm_pointer_hide(pointer);
                wm_sd_scene_hover(sd_scene, (WmSdHit){WM_SD_CONTROL_NONE, 0});
                if (active_storage) wm_storage_scene_hover(active_storage,
                    (WmStorageHit){WM_STORAGE_CONTROL_NONE, -1});
                wm_board_scene_hover(board_scene,
                    (WmBoardHit){WM_BOARD_CONTROL_NONE, 0});
                wm_board_scene_release_compose_control(board_scene);
                if (event.cancel_capture)
                    wm_board_scene_cancel_pointer(board_scene);
                else
                    wm_board_scene_pointer_finish(board_scene);
                board_pressed = (WmBoardHit){WM_BOARD_CONTROL_NONE, 0};
                board_held_keyboard = false;
                wm_options_scene_hover(options_scene, WM_OPTIONS_CONTROL_NONE);
                if (options_held_arrow) {
                    wm_options_scene_pointer_up(options_scene);
                    options_held_arrow = false;
                    options_pressed = WM_OPTIONS_CONTROL_NONE;
                }
                board_hovered = (WmBoardHit){WM_BOARD_CONTROL_NONE, 0};
                options_hovered = WM_OPTIONS_CONTROL_NONE;
                hover = (WmHit){WM_HIT_NONE, -1};
                pressed = (WmHit){WM_HIT_NONE, -1};
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
                        hover = (WmHit){WM_HIT_NONE, -1};
                        pressed = hover;
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
                    wm_board_scene_child(board_scene) == WM_BOARD_CHILD_COMPOSE) {
                    bool editor_active =
                        wm_board_scene_compose_editor_active(board_scene);
                    if (event.key == WM_KEY_BACKSPACE &&
                        wm_board_scene_backspace(board_scene)) {
                        wm_audio_play(audio, "WIPL_SE_CHAR_DELETE");
                        continue;
                    }
                    if (event.key == WM_KEY_BACKSPACE && editor_active) {
                        wm_audio_play(audio, "WIPL_SE_CHAR_DELETE_ERROR");
                        continue;
                    }
                    if (event.key == WM_KEY_ENTER &&
                        wm_board_scene_address_editor_active(board_scene)) {
                        if (wm_board_scene_finish_edit(board_scene))
                            wm_app_play_board_compose_cues(audio, board_scene);
                        continue;
                    }
                    if (event.key == WM_KEY_ENTER &&
                        wm_board_scene_insert_text(board_scene, "\n")) {
                        wm_app_play_board_compose_cues(audio, board_scene);
                        wm_audio_play(audio, "WIPL_SE_CHAR_DECIDE");
                        continue;
                    }
                    if (event.key == WM_KEY_ENTER && editor_active) {
                        wm_audio_play(audio, "WIPL_SE_CHAR_DELETE_ERROR");
                        continue;
                    }
                    if (event.key >= 32 && event.key <= 126) {
                        char character[2] = {(char)event.key, '\0'};
                        if (wm_board_scene_insert_text(board_scene, character)) {
                            bool non_scroll_played;
                            wm_app_play_board_compose_cues_with_non_scroll(
                                audio, board_scene, &non_scroll_played);
                            if (!editor_active || !non_scroll_played)
                                wm_audio_play(audio, event.key == ' '
                                    ? "WIPL_SE_CHAR_DECIDE" :
                                      "WIPL_SE_CHAR_INPUT");
                            continue;
                        }
                        if (editor_active) {
                            wm_audio_play(audio, "WIPL_SE_CHAR_DELETE_ERROR");
                            continue;
                        }
                    }
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
        if (menu.screen != WM_SCREEN_BOARD)
            board_entry_hover_pending = false;
        if (menu.screen != WM_SCREEN_BOARD || wm_home_overlay_active(home))
            board_hovered = (WmBoardHit){WM_BOARD_CONTROL_NONE, 0};
        if (!layout && !health_frame && !wm_home_overlay_active(home) &&
            !wm_menu_restart_active(&restart) &&
            menu.screen == WM_SCREEN_BOARD && board_scene) {
            wm_board_scene_advance(board_scene, elapsed * 60.0f);
            wm_app_play_board_compose_cues(audio, board_scene);
            wm_app_play_board_sound_events(audio, board_scene);
            const char *reader_cue;
            while ((reader_cue =
                    wm_board_scene_take_reader_cue(board_scene)) != NULL)
                wm_audio_play(audio, reader_cue);
            if (board_entry_hover_pending &&
                wm_board_scene_phase(board_scene) == WM_BOARD_READY) {
                WmBoardHit initial = pointer_inside
                    ? wm_board_scene_hit(board_scene, pointer_x, pointer_y)
                    : (WmBoardHit){WM_BOARD_CONTROL_NONE, SIZE_MAX};
                play_board_hover_cue(audio, initial.control);
                board_hovered = initial;
                wm_board_scene_hover(board_scene, initial);
                board_entry_hover_pending = false;
            }
            WmBoardPhase board_phase = wm_board_scene_phase(board_scene);
            /* The common arrows keep one hover owner throughout a page
             * turn. Reconcile their source bounds without requiring a new
             * pointer event when the page settles or an arrow disappears. */
            if (pointer_inside && !menu.notice[0] &&
                !wm_board_scene_dragging(board_scene) &&
                wm_board_scene_child(board_scene) == WM_BOARD_CHILD_NONE &&
                (board_phase == WM_BOARD_READY ||
                 board_phase == WM_BOARD_DATE_SCROLL ||
                 board_phase == WM_BOARD_MEMO_PAGE)) {
                WmBoardHit next = wm_board_scene_hit(board_scene,
                                                      pointer_x, pointer_y);
                if (next.control != board_hovered.control ||
                    next.memo_index != board_hovered.memo_index) {
                    play_board_hover_cue(audio, next.control);
                    board_hovered = next;
                    wm_board_scene_hover(board_scene, next);
                }
            }
            if (board_phase != WM_BOARD_READY &&
                board_phase != WM_BOARD_MEMO_READ &&
                board_phase != WM_BOARD_DATE_SCROLL &&
                board_phase != WM_BOARD_MEMO_PAGE)
                board_hovered = (WmBoardHit){WM_BOARD_CONTROL_NONE, 0};
            float departing_grid_frame = 0.0f;
            if (!wm_board_scene_grid_overlay(board_scene,
                                             &departing_grid_frame))
                board_visited = true;
            WmBoardAction action = wm_board_scene_take_action(board_scene, NULL);
            if (action == WM_BOARD_ACTION_EXITED) {
                wm_menu_switch_screen_at_black(&menu, WM_SCREEN_GRID);
                hover = pointer_inside
                    ? wm_resource_scene_hit(resource_scene, &menu,
                                            pointer_x, pointer_y)
                    : (WmHit){WM_HIT_NONE, -1};
                wm_audio_stop_loop(audio, "WIPL_SE_BOARD_DRAG");
                wm_audio_stop_loop(audio, "WIPL_SE_MESSAGE_SCROLL");
            } else if ((action == WM_BOARD_ACTION_OPEN_SETTINGS ||
                        action == WM_BOARD_ACTION_OPEN_CONNECT24_SETTINGS) &&
                       wm_scene_fader_start(&fade.clock)) {
                board_settings_request = action;
                fade.destination = WM_SCREEN_SETTINGS;
            } else if ((action == WM_BOARD_ACTION_MEMO_POSTED ||
                        action == WM_BOARD_ACTION_ERASE_MEMO ||
                        action == WM_BOARD_ACTION_MEMO_READ ||
                        action == WM_BOARD_ACTION_MEMO_MOVED) &&
                       board_state_path) {
                char error[160] = {0};
                if (!wm_board_store_save(board_state_path, board_scene,
                                         error, sizeof(error))) {
                    fprintf(stderr, "Could not save Message Board memos: %s\n",
                            error);
                }
            }
            float cue_pan = 0.0f;
            WmBoardDragCue cue;
            while ((cue = wm_board_scene_take_drag_cue(board_scene,
                                                        &cue_pan)) !=
                   WM_BOARD_DRAG_CUE_NONE) {
                wm_audio_play_panned(audio, cue == WM_BOARD_DRAG_CUE_HOLD
                                              ? "WIPL_SE_BOARD_HOLD"
                                              : "WIPL_SE_BOARD_RELEASE",
                                     cue_pan);
            }
            float drag_gain = 0.0f;
            float drag_pan = 0.0f;
            float memo_drag_pitch = 1.0f;
            if (wm_board_scene_drag_mix(board_scene, &drag_gain, &drag_pan,
                                          &memo_drag_pitch)) {
                wm_audio_hold_loop(audio, "WIPL_SE_BOARD_DRAG",
                                  drag_gain, drag_pan, memo_drag_pitch);
            } else {
                wm_audio_stop_loop(audio, "WIPL_SE_BOARD_DRAG");
            }
            if (wm_board_scene_reader_scroll_sound_active(board_scene)) {
                wm_audio_start_loop(audio, "WIPL_SE_MESSAGE_SCROLL");
            } else {
                wm_audio_stop_loop(audio, "WIPL_SE_MESSAGE_SCROLL");
            }
        } else if (!layout && !health_frame &&
                   !wm_home_overlay_active(home) &&
                   !wm_menu_restart_active(&restart) &&
                   menu.screen == WM_SCREEN_GRID && board_scene) {
            /* A return to today's date can leave ordered arrivals underway
             * behind ChannelSelect. Continue their shared clock and cues
             * once per update, including after the Board has closed. */
            wm_board_scene_advance_parked(board_scene, elapsed * 60.0f);
            wm_app_play_board_sound_events(audio, board_scene);
        }
        if (!layout && resource_scene && board_scene) {
            if (menu.screen == WM_SCREEN_GRID &&
                !wm_home_overlay_active(home))
                wm_board_scene_refresh_today(board_scene, wm_app_today_date());
            wm_resource_scene_set_message_badge(
                resource_scene, wm_board_scene_today_count(board_scene),
                !board_visited &&
                    wm_board_scene_today_unread_count(board_scene) > 0);
        }
        if (!layout && !health_frame && !wm_home_overlay_active(home) &&
            !wm_menu_restart_active(&restart) &&
            menu.screen == WM_SCREEN_SETTINGS && options_scene) {
            if (!wm_scene_fader_active(&fade.clock))
                wm_options_scene_advance(options_scene, elapsed * 60.0f);
            WmOptionsAction action = wm_options_scene_take_action(options_scene);
            if (action == WM_OPTIONS_ACTION_EXITED &&
                wm_scene_fader_start(&fade.clock)) {
                fade.destination = board_settings_request != WM_BOARD_ACTION_NONE
                    ? WM_SCREEN_BOARD : WM_SCREEN_GRID;
                board_settings_request = WM_BOARD_ACTION_NONE;
            }
            else if (action == WM_OPTIONS_ACTION_CHANNEL_STORAGE ||
                     action == WM_OPTIONS_ACTION_WII_STORAGE ||
                     action == WM_OPTIONS_ACTION_GAMECUBE_STORAGE) {
                WmStorageKind kind = action == WM_OPTIONS_ACTION_CHANNEL_STORAGE
                    ? WM_STORAGE_CHANNELS : action == WM_OPTIONS_ACTION_WII_STORAGE
                    ? WM_STORAGE_WII_SAVES : WM_STORAGE_GAMECUBE_SAVES;
                active_storage = storage_scenes[kind];
                if (active_storage)
                    wm_storage_scene_open(active_storage, WM_STORAGE_WII);
            }
            /* A Settings page turn can finish without another pointer event.
             * Re-hit the settled page so an arrow under a stationary hand
             * enters its hover state immediately. */
            if (!active_storage && !menu.notice[0] && pointer_inside &&
                !wm_scene_fader_active(&fade.clock)) {
                WmOptionsControl next = wm_options_scene_hit(
                    options_scene, pointer_x, pointer_y);
                if (next != options_hovered) {
                    if (next != WM_OPTIONS_CONTROL_NONE)
                        wm_audio_play(audio,
                            wm_options_scene_hover_cue(options_scene, next));
                    options_hovered = next;
                    wm_options_scene_hover(options_scene, next);
                }
            }
        }
        if (!layout && !health_frame && !wm_home_overlay_active(home) &&
            !wm_menu_restart_active(&restart) && active_storage &&
            menu.screen == WM_SCREEN_SETTINGS) {
            wm_storage_scene_advance(active_storage, elapsed * 60.0f);
            WmStorageAction action = wm_storage_scene_take_action(
                active_storage, NULL, NULL);
            if (action == WM_STORAGE_ACTION_EXITED) {
                active_storage = NULL;
                wm_options_scene_back(options_scene);
            }
            if (wm_storage_scene_take_balloon_cue(active_storage))
                wm_audio_play(audio, "balloon");
        }
        if (!layout && !health_frame && !wm_home_overlay_active(home) &&
            !wm_menu_restart_active(&restart) && sd_scene &&
            menu.screen == WM_SCREEN_SD) {
            if (fade.clock.phase != WM_SCENE_FADER_OUT)
                wm_sd_scene_advance(sd_scene, elapsed * 60.0f,
                                     fade.clock.phase == WM_SCENE_FADER_IN);
            WmSdEvent sd_event;
            while (wm_sd_scene_take_event(sd_scene, &sd_event)) {
                switch (sd_event.type) {
                    case WM_SD_EVENT_EXIT:
                        if (wm_scene_fader_start(&fade.clock))
                            fade.destination = WM_SCREEN_GRID;
                        break;
                    case WM_SD_EVENT_PAGE_CHANGED:
                        sd_page = wm_sd_scene_page(sd_scene);
                        break;
                    case WM_SD_EVENT_HELP_CLOSE:
                        sd_help_seen = wm_sd_scene_help_seen(sd_scene);
                        break;
                    case WM_SD_EVENT_HOVER_SOUND:
                        wm_audio_play(audio,
                            wm_hover_audio_sd_cue(sd_event.control));
                        break;
                    case WM_SD_EVENT_CONFIRM_SOUND:
                        wm_audio_play(audio, "confirm");
                        break;
                    case WM_SD_EVENT_CANCEL_SOUND:
                        wm_audio_play(audio, "WIPL_SE_CANCEL");
                        break;
                    case WM_SD_EVENT_PAGE_SOUND:
                        wm_audio_play(audio, "page");
                        break;
                    case WM_SD_EVENT_INFO_SOUND:
                        wm_audio_play(audio, "infoWindow");
                        break;
                    case WM_SD_EVENT_BALLOON_SOUND:
                        wm_audio_play(audio, "balloon");
                        break;
                    case WM_SD_EVENT_CHANNEL_SELECTED:
                    case WM_SD_EVENT_HELP_OPEN:
                    case WM_SD_EVENT_NONE:
                        break;
                }
            }
        }
        if (!layout && !health_frame && !wm_home_overlay_active(home) &&
            !wm_menu_restart_active(&restart) && drag) {
            WmChannelDragEvents drag_events = wm_channel_drag_advance(
                drag, elapsed * 60.0f, &menu,
                menu.transition == WM_TRANSITION_PAGE);
            play_drag_sound(audio, drag_events.sound);
            if (drag_events.page && wm_menu_change_page(&menu, drag_events.page)) {
                wm_resource_scene_press_arrow(resource_scene,
                                               drag_events.page);
                wm_audio_play(audio, "page");
            }
            if (drag_events.move &&
                wm_menu_move_channel(&menu, drag_events.move_source,
                                      drag_events.move_target)) {
                wm_resource_scene_move_channel(resource_scene,
                    drag_events.move_source, drag_events.move_target);
                wm_preview_scene_move_channel(preview_scene,
                    drag_events.move_source, drag_events.move_target);
            }
            if (drag_button != 0) {
                WmChannelDragState state = wm_channel_drag_state(drag);
                WmChannelDragAudioParameters parameters =
                    wm_channel_drag_audio_for_framebuffer(
                        true, state.pointer_x, state.pointer_y,
                        drag_has_previous, drag_previous_x,
                        drag_previous_y, elapsed * 60.0f);
                if (parameters.changes_pitch) drag_pitch = parameters.pitch;
                wm_audio_hold_loop(audio, "drag", parameters.gain,
                                   parameters.pan, drag_pitch);
                drag_previous_x = state.pointer_x;
                drag_previous_y = state.pointer_y;
                drag_has_previous = true;
            }
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
            .hover = hover,
            .board_hovered = board_hovered,
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
    wm_texture_cache_destroy(textures);
    wm_font_cache_destroy(fonts);
    wm_pointer_destroy(pointer);
    wm_channel_drag_destroy(drag);
    wm_home_overlay_destroy(home);
    wm_app_renderer_release_home_underlay(&renderer);
    wm_audio_destroy(audio);
    wm_resource_scene_destroy(resource_scene);
    wm_preview_scene_destroy(preview_scene);
    wm_board_scene_destroy(board_scene);
    free(board_state_path);
    wm_health_scene_destroy(health_scene);
    wm_menu_restart_scene_destroy(restart_scene);
    wm_options_scene_destroy(options_scene);
    wm_sd_scene_destroy(sd_scene);
    wm_bmg_destroy(messages);
    for (int kind = 0; kind < 3; kind++)
        wm_storage_scene_destroy(storage_scenes[kind]);
    wm_texture_cache_destroy(scene_textures);
    wm_font_cache_destroy(scene_fonts);
    wm_layout_destroy(layout);
    wm_platform_destroy(platform);
    return 0;
}
