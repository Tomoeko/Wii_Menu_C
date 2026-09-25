#include "wii_menu/menu.h"
#include "wii_menu/json.h"
#include "wii_menu/saved_layout.h"

#include <stdio.h>
#include <string.h>

static bool channel_from_id(const WmJson *json, size_t channels, const char *id,
                            WmChannel *channel);
static bool already_placed(const WmChannel *slots, const char *id);

static bool safe_asset_path(const char *path) {
    if (!path[0] || path[0] == '/' || strchr(path, '\\')) return false;
    for (const char *part = path; *part;) {
        const char *end = strchr(part, '/');
        size_t length = end ? (size_t)(end - part) : strlen(part);
        if (length == 0 || (length == 1 && part[0] == '.') ||
            (length == 2 && part[0] == '.' && part[1] == '.')) return false;
        if (!end) break;
        part = end + 1;
    }
    return true;
}

static void copy_layout_path(const WmJson *json, size_t entry,
                             const char *member, char path[256]) {
    char candidate[256];
    if (wm_json_copy(json, wm_json_member(json, entry, member), candidate,
                     sizeof(candidate)) && safe_asset_path(candidate)) {
        strcpy(path, candidate);
    }
}

static bool load_saved_slots(const char *assets_directory, const WmJson *json,
                             size_t channels, WmChannel slots[WM_SLOT_COUNT]) {
    char path[4096];
    int length = snprintf(path, sizeof(path), "%s/iplsave.bin", assets_directory);
    if (length < 0 || (size_t)length >= sizeof(path)) return false;
    FILE *file = fopen(path, "rb");
    if (!file) return false;
    uint8_t bytes[WM_SAVED_LAYOUT_BYTES];
    bool exact = fread(bytes, 1, sizeof(bytes), file) == sizeof(bytes) &&
                 fgetc(file) == EOF;
    fclose(file);
    WmSavedLayout saved;
    if (!exact || !wm_saved_layout_parse(bytes, sizeof(bytes), &saved, NULL, 0)) {
        return false;
    }
    for (int index = 1; index < WM_SLOT_COUNT; index++) {
        const char *id = saved.slots[index].id;
        if (id[0] && !already_placed(slots, id)) {
            channel_from_id(json, channels, id, &slots[index]);
        }
    }
    return true;
}

static bool channel_from_id(const WmJson *json, size_t channels, const char *id,
                            WmChannel *channel) {
    if (channels == WM_JSON_INVALID || json->tokens[channels].type != WM_JSON_ARRAY ||
        json->tokens[channels].children > 2048) return false;
    for (size_t index = 0; index < json->tokens[channels].children; index++) {
        size_t entry = wm_json_index(json, channels, index);
        size_t id_token = wm_json_member(json, entry, "id");
        if (!wm_json_equals(json, id_token, id)) continue;
        size_t title = wm_json_member(json, entry, "title");
        if (!wm_json_copy(json, title, channel->title, sizeof(channel->title))) return false;
        if (strlen(id) >= sizeof(channel->id)) return false;
        strcpy(channel->id, id);
        copy_layout_path(json, entry, "iconLayout", channel->icon_layout);
        copy_layout_path(json, entry, "bannerLayout", channel->banner_layout);
        channel->occupied = true;
        return true;
    }
    return false;
}

static bool already_placed(const WmChannel *slots, const char *id) {
    for (int slot = 1; slot < WM_SLOT_COUNT; slot++) {
        if (slots[slot].occupied && strcmp(slots[slot].id, id) == 0) return true;
    }
    return false;
}

bool wm_catalog_load(WmMenu *menu, const char *assets_directory) {
    if (!assets_directory || !*assets_directory) return false;
    char path[4096];
    int length = snprintf(path, sizeof(path), "%s/channels.json", assets_directory);
    if (length < 0 || (size_t)length >= sizeof(path)) return false;

    WmJson json;
    if (!wm_json_load(&json, path, 8 * 1024 * 1024)) return false;
    bool success = false;
    do {
        size_t version = wm_json_member(&json, 0, "schemaVersion");
        size_t channels = wm_json_member(&json, 0, "channels");
        size_t defaults = wm_json_member(&json, 0, "defaultOrder");
        int schema_version;
        if (!wm_json_integer(&json, version, &schema_version) || schema_version != 1 ||
            channels == WM_JSON_INVALID || defaults == WM_JSON_INVALID ||
            json.tokens[channels].type != WM_JSON_ARRAY ||
            json.tokens[defaults].type != WM_JSON_ARRAY) break;

        WmChannel slots[WM_SLOT_COUNT] = {0};
        slots[0] = menu->slots[0];
        bool has_native_save = load_saved_slots(assets_directory, &json,
                                                channels, slots);
        size_t saved = wm_json_member(&json, 0, "savedLayout");
        size_t saved_slots = wm_json_member(&json, saved, "slots");
        if (!has_native_save && saved_slots != WM_JSON_INVALID &&
            json.tokens[saved_slots].type == WM_JSON_ARRAY) {
            for (size_t entry_index = 0;
                 entry_index < json.tokens[saved_slots].children; entry_index++) {
                size_t entry = wm_json_index(&json, saved_slots, entry_index);
                int page, index;
                if (!wm_json_integer(&json, wm_json_member(&json, entry, "page"), &page) ||
                    !wm_json_integer(&json, wm_json_member(&json, entry, "index"), &index) ||
                    page < 0 || page >= WM_PAGE_COUNT || index < 0 ||
                    index >= WM_CHANNELS_PER_PAGE) continue;
                int slot = page * WM_CHANNELS_PER_PAGE + index;
                if (slot == 0) continue;
                char id[65];
                if (!wm_json_copy(&json, wm_json_member(&json, entry, "id"), id,
                                  sizeof(id)) || already_placed(slots, id)) continue;
                channel_from_id(&json, channels, id, &slots[slot]);
            }
        }

        int next_free = 1;
        for (size_t index = 0; index < json.tokens[defaults].children; index++) {
            size_t token = wm_json_index(&json, defaults, index);
            char id[65];
            if (!wm_json_copy(&json, token, id, sizeof(id)) || already_placed(slots, id)) {
                continue;
            }
            while (next_free < WM_SLOT_COUNT && slots[next_free].occupied) next_free++;
            if (next_free == WM_SLOT_COUNT) break;
            channel_from_id(&json, channels, id, &slots[next_free]);
        }
        memcpy(menu->slots, slots, sizeof(slots));
        success = true;
    } while (false);
    wm_json_free(&json);
    return success;
}
