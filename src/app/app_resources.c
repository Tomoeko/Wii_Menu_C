#include "app_resources.h"

#include "wii_menu/persistence/board_store.h"

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void home_cue(void *context, const char *symbol) {
    wm_audio_play((WmAudio *)context, symbol);
}

static void home_remote_changed(void *context, const WmHomeRemoteState *state) {
    WmAudio *audio = context;
    wm_audio_set_volume(audio, state->volume);
    wm_audio_set_muted(audio, state->muted);
}

static void load_board_state(WmAppResources *resources, const char *assets) {
    static const char memo_suffix[] = "/.board-memos.json";
    static const char contacts_suffix[] = "/.board-contacts.json";
    size_t length = strlen(assets);

    if (length <= SIZE_MAX - sizeof(memo_suffix)) {
        resources->board_state_path = malloc(length + sizeof(memo_suffix));
        if (resources->board_state_path) {
            memcpy(resources->board_state_path, assets, length);
            memcpy(resources->board_state_path + length, memo_suffix,
                   sizeof(memo_suffix));
            char error[160] = {0};
            if (wm_board_store_load(resources->board_state_path, resources->board_scene,
                                    error, sizeof(error)) == WM_BOARD_STORE_ERROR) {
                fprintf(stderr, "Could not load Message Board memos: %s\n", error);
            }
        }
    }
    if (length <= SIZE_MAX - sizeof(contacts_suffix)) {
        char *contacts_path = malloc(length + sizeof(contacts_suffix));
        if (contacts_path) {
            memcpy(contacts_path, assets, length);
            memcpy(contacts_path + length, contacts_suffix, sizeof(contacts_suffix));
            char error[160] = {0};
            if (wm_board_scene_load_contacts(resources->board_scene, contacts_path,
                                             error, sizeof(error)) ==
                WM_BOARD_CONTACT_STORE_ERROR) {
                fprintf(stderr, "Could not load Address Book contacts: %s\n", error);
            }
            free(contacts_path);
        }
    }
}

static void populate_channel_storage(WmAppResources *resources, const WmMenu *menu) {
    WmStorageScene *storage = resources->storage_scenes[WM_STORAGE_CHANNELS];
    if (!storage)
        return;

    WmStorageRecord records[WM_SLOT_COUNT];
    size_t count = 0;
    for (int slot = 1; slot < WM_SLOT_COUNT; slot++) {
        const WmChannel *channel = &menu->slots[slot];
        if (!channel->occupied || !channel->icon_layout[0])
            continue;
        WmStorageRecord *record = &records[count++];
        memset(record, 0, sizeof(*record));
        snprintf(record->id, sizeof(record->id), "%s", channel->id);
        snprintf(record->title, sizeof(record->title), "%s", channel->title);
        snprintf(record->icon_layout, sizeof(record->icon_layout), "%s",
                 channel->icon_layout);
        record->blocks = 1;
    }
    if (!wm_storage_scene_set_medium(storage, WM_STORAGE_WII, WM_STORAGE_READY, records,
                                     count, 0)) {
        fprintf(stderr, "Could not populate Channels storage.\n");
    }
}

bool wm_app_resources_create(WmAppResources *resources, WmMenu *menu,
                             const char *assets, const char *layout_path,
                             const char *raw_root) {
    if (!resources || !menu)
        return false;
    memset(resources, 0, sizeof(*resources));

    bool catalog_loaded = !layout_path && assets && wm_catalog_load(menu, assets);
    if (layout_path) {
        char error[160];
        resources->layout = wm_layout_load_json(layout_path, error, sizeof(error));
        if (!resources->layout) {
            fprintf(stderr, "Could not load layout: %s\n", error);
            return false;
        }
    }

    resources->platform = wm_platform_create("Wii Menu in C", 960, 540);
    if (!resources->platform) {
        fprintf(stderr, "Could not initialize the graphics backend.\n");
        wm_app_resources_destroy(resources);
        return false;
    }
    if (resources->layout) {
        resources->layout_textures = wm_texture_cache_create(
            resources->platform, raw_root, 128u * 1024u * 1024u);
        resources->layout_fonts =
            wm_font_cache_create(resources->platform, raw_root, 16u * 1024u * 1024u);
        if (!resources->layout_textures || !resources->layout_fonts) {
            fprintf(stderr, "Could not open the layout asset directory.\n");
            wm_app_resources_destroy(resources);
            return false;
        }
    }

    if (resources->layout || !assets)
        return true;

    resources->scene_textures =
        wm_texture_cache_create(resources->platform, assets, 128u * 1024u * 1024u);
    resources->scene_fonts =
        wm_font_cache_create(resources->platform, assets, 16u * 1024u * 1024u);
    resources->resource_scene =
        wm_resource_scene_create(resources->platform, assets, menu,
                                 resources->scene_textures, resources->scene_fonts);
    resources->preview_scene =
        wm_preview_scene_create(resources->platform, assets, menu,
                                resources->scene_textures, resources->scene_fonts);
    if (resources->scene_textures && resources->scene_fonts) {
        resources->board_scene =
            wm_board_scene_create(resources->platform, assets,
                                  resources->scene_textures, resources->scene_fonts);
        resources->restart_scene = wm_menu_restart_scene_create(
            resources->platform, assets, resources->scene_textures,
            resources->scene_fonts);
        resources->health_scene = wm_health_scene_create(
            resources->platform, assets, resources->scene_textures,
            resources->scene_fonts, true, NULL);
    }
    if (resources->board_scene)
        load_board_state(resources, assets);

    if (resources->scene_textures && resources->scene_fonts) {
        resources->options_scene =
            wm_options_scene_create(resources->platform, assets,
                                    resources->scene_textures, resources->scene_fonts);
        resources->sd_scene =
            wm_sd_scene_create(resources->platform, assets, resources->scene_textures,
                               resources->scene_fonts);
    }
    char message_error[160] = {0};
    resources->messages =
        wm_bmg_load_assets(assets, "eng", message_error, sizeof(message_error));
    if (resources->sd_scene && resources->messages) {
        wm_sd_scene_set_messages(resources->sd_scene, wm_bmg_message,
                                 resources->messages);
    }
    if (resources->scene_textures && resources->scene_fonts) {
        for (int kind = 0; kind < 3; kind++) {
            resources->storage_scenes[kind] = wm_storage_scene_create(
                resources->platform, assets, resources->scene_textures,
                resources->scene_fonts, (WmStorageKind)kind);
        }
        populate_channel_storage(resources, menu);
    }
    if (resources->scene_textures) {
        resources->pointer =
            wm_pointer_create(resources->platform, assets, resources->scene_textures);
    }
    resources->audio = wm_audio_create(assets);
    if (resources->scene_textures && resources->scene_fonts) {
        resources->home = wm_home_overlay_create(
            resources->platform, assets, resources->scene_textures,
            resources->scene_fonts, home_cue, home_remote_changed, resources->audio);
        resources->drag = wm_channel_drag_create(resources->platform, assets,
                                                 resources->scene_textures,
                                                 resources->scene_fonts, true);
    }
    if (!resources->pointer) {
        fprintf(stderr, "Could not load the source Wii hand pointer.\n");
    }
    if (!catalog_loaded && !resources->resource_scene) {
        fprintf(
            stderr,
            "Could not load a valid prepared channel catalog; showing Disc only.\n");
    }
    return true;
}

void wm_app_resources_destroy(WmAppResources *resources) {
    if (!resources)
        return;

    wm_texture_cache_destroy(resources->layout_textures);
    wm_font_cache_destroy(resources->layout_fonts);
    wm_pointer_destroy(resources->pointer);
    wm_channel_drag_destroy(resources->drag);
    wm_home_overlay_destroy(resources->home);
    wm_audio_destroy(resources->audio);
    wm_resource_scene_destroy(resources->resource_scene);
    wm_preview_scene_destroy(resources->preview_scene);
    wm_board_scene_destroy(resources->board_scene);
    free(resources->board_state_path);
    wm_health_scene_destroy(resources->health_scene);
    wm_menu_restart_scene_destroy(resources->restart_scene);
    wm_options_scene_destroy(resources->options_scene);
    wm_sd_scene_destroy(resources->sd_scene);
    wm_bmg_destroy(resources->messages);
    for (int kind = 0; kind < 3; kind++) {
        wm_storage_scene_destroy(resources->storage_scenes[kind]);
    }
    wm_texture_cache_destroy(resources->scene_textures);
    wm_font_cache_destroy(resources->scene_fonts);
    wm_layout_destroy(resources->layout);
    wm_platform_destroy(resources->platform);
    memset(resources, 0, sizeof(*resources));
}
