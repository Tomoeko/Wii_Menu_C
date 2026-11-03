#define _POSIX_C_SOURCE 200809L

#include "wii_menu/persistence/channel_layout_store.h"

#include "../support/atomic_file.h"
#include "wii_menu/support/error.h"
#include "wii_menu/support/json.h"
#include "wii_menu/support/regular_file.h"

#include <stdint.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

enum { CHANNEL_LAYOUT_MAX_BYTES = 64 * 1024 };

typedef struct ChannelPositions {
    char ids[WM_SLOT_COUNT][sizeof(((WmChannel *)0)->id)];
} ChannelPositions;

struct WmChannelLayoutStore {
    char *path;
    char *file_contents;
    size_t file_length;
    ChannelPositions baseline;
};

static bool capture_positions(const WmMenu *menu, ChannelPositions *positions) {
    memset(positions, 0, sizeof(*positions));
    for (int slot = 0; slot < WM_SLOT_COUNT; slot++) {
        const WmChannel *channel = &menu->slots[slot];
        if (!channel->occupied)
            continue;
        if (!channel->id[0] || !memchr(channel->id, '\0', sizeof(channel->id)))
            return false;
        for (int previous = 0; previous < slot; previous++) {
            if (strcmp(positions->ids[previous], channel->id) == 0)
                return false;
        }
        strcpy(positions->ids[slot], channel->id);
    }
    return strcmp(positions->ids[0], "disc") == 0;
}

static int channel_slot(const WmMenu *menu, const char *id) {
    for (int slot = 0; slot < WM_SLOT_COUNT; slot++) {
        if (menu->slots[slot].occupied && strcmp(menu->slots[slot].id, id) == 0)
            return slot;
    }
    return -1;
}

static bool apply_positions(const WmJson *json, WmMenu *menu, char *error,
                            size_t error_capacity) {
    int version;
    size_t slots = wm_json_member(json, 0, "slots");
    if (json->tokens[0].type != WM_JSON_OBJECT || json->tokens[0].children != 2 ||
        !wm_json_integer(json, wm_json_member(json, 0, "schemaVersion"), &version) ||
        version != 1 || slots == WM_JSON_INVALID ||
        json->tokens[slots].type != WM_JSON_ARRAY ||
        json->tokens[slots].children > WM_SLOT_COUNT) {
        wm_error_set(error, error_capacity, "Invalid channel layout schema");
        return false;
    }

    int destinations[WM_SLOT_COUNT];
    bool assigned[WM_SLOT_COUNT] = {false};
    for (int slot = 0; slot < WM_SLOT_COUNT; slot++)
        destinations[slot] = slot;
    for (size_t index = 0; index < json->tokens[slots].children; index++) {
        size_t entry = wm_json_index(json, slots, index);
        char id[sizeof(menu->slots[0].id)];
        int page, position;
        if (json->tokens[entry].type != WM_JSON_OBJECT ||
            json->tokens[entry].children != 3 ||
            !wm_json_copy_text(json, wm_json_member(json, entry, "id"), id,
                               sizeof(id)) ||
            !id[0] ||
            !wm_json_integer(json, wm_json_member(json, entry, "page"), &page) ||
            !wm_json_integer(json, wm_json_member(json, entry, "index"), &position) ||
            page < 0 || page >= WM_PAGE_COUNT || position < 0 ||
            position >= WM_CHANNELS_PER_PAGE) {
            wm_error_set(error, error_capacity, "Invalid channel layout entry");
            return false;
        }
        int source = channel_slot(menu, id);
        if (source < 0) {
            wm_error_set(error, error_capacity, "Unknown channel ID in local config");
            return false;
        }
        if (assigned[source]) {
            wm_error_set(error, error_capacity, "Repeated channel ID in local config");
            return false;
        }
        int target = page * WM_CHANNELS_PER_PAGE + position;
        if ((source == 0) != (target == 0)) {
            wm_error_set(error, error_capacity,
                         "Overlapped or moved Disc Channel position in local config");
            return false;
        }
        assigned[source] = true;
        destinations[source] = target;
    }

    WmChannel result[WM_SLOT_COUNT] = {0};
    for (int source = 0; source < WM_SLOT_COUNT; source++) {
        if (!menu->slots[source].occupied)
            continue;
        int target = destinations[source];
        if (result[target].occupied) {
            wm_error_set(error, error_capacity,
                         "Overlapped channel position in local config");
            return false;
        }
        result[target] = menu->slots[source];
    }
    memcpy(menu->slots, result, sizeof(result));
    return true;
}

void wm_channel_layout_store_destroy(WmChannelLayoutStore *store) {
    if (!store)
        return;
    free(store->path);
    free(store->file_contents);
    free(store);
}

WmChannelLayoutStore *wm_channel_layout_store_open(const char *path, WmMenu *menu,
                                                   WmChannelLayoutStatus *status,
                                                   char *error, size_t error_capacity) {
    if (status)
        *status = WM_CHANNEL_LAYOUT_ERROR;
    wm_error_set(error, error_capacity, "Could not open local channel layout config");
    if (!path || !path[0] || strlen(path) >= 4096 || !menu)
        return NULL;
    WmChannelLayoutStore *store = calloc(1, sizeof(*store));
    if (!store)
        return NULL;
    store->path = strdup(path);
    if (!store->path || !capture_positions(menu, &store->baseline)) {
        wm_channel_layout_store_destroy(store);
        return NULL;
    }
    uint8_t *contents;
    WmRegularFileStatus read = wm_regular_file_read_bytes(
        path, 0, CHANNEL_LAYOUT_MAX_BYTES, &contents, &store->file_length);
    if (read == WM_REGULAR_FILE_ERROR) {
        wm_channel_layout_store_destroy(store);
        return NULL;
    }
    if (read == WM_REGULAR_FILE_MISSING) {
        if (status)
            *status = WM_CHANNEL_LAYOUT_MISSING;
        wm_error_set(error, error_capacity, "");
        return store;
    }
    store->file_contents = (char *)contents;
    WmJson json;
    bool valid = wm_json_parse(&json, store->file_contents, store->file_length);
    if (valid) {
        valid = apply_positions(&json, menu, error, error_capacity);
        wm_json_free(&json);
    } else {
        wm_error_set(error, error_capacity, "Malformed channel layout JSON");
    }
    if (status)
        *status = valid ? WM_CHANNEL_LAYOUT_LOADED : WM_CHANNEL_LAYOUT_INVALID;
    if (valid) {
        capture_positions(menu, &store->baseline);
        wm_error_set(error, error_capacity, "");
    }
    return store;
}

static bool file_unchanged(const WmChannelLayoutStore *store) {
    uint8_t *contents;
    size_t length;
    WmRegularFileStatus read = wm_regular_file_read_bytes(
        store->path, 0, CHANNEL_LAYOUT_MAX_BYTES, &contents, &length);
    if (read == WM_REGULAR_FILE_MISSING)
        return !store->file_contents;
    if (read != WM_REGULAR_FILE_OK)
        return false;
    bool equal = store->file_contents && store->file_length == length &&
                 memcmp(store->file_contents, contents, length) == 0;
    free(contents);
    return equal;
}

typedef struct LayoutJson {
    char *bytes;
    size_t used;
    size_t capacity;
} LayoutJson;

static bool append_json(LayoutJson *output, const char *format, ...) {
    va_list arguments;
    va_start(arguments, format);
    int length = vsnprintf(output->bytes + output->used,
                           output->capacity - output->used, format, arguments);
    va_end(arguments);
    if (length < 0 || (size_t)length >= output->capacity - output->used)
        return false;
    output->used += (size_t)length;
    return true;
}

static bool append_id(LayoutJson *output, const char *text) {
    for (const unsigned char *id = (const unsigned char *)text; *id; id++) {
        if (*id < 0x20) {
            if (!append_json(output, "\\u%04x", (unsigned)*id))
                return false;
        } else {
            if ((*id == '"' || *id == '\\') && !append_json(output, "\\"))
                return false;
            if (!append_json(output, "%c", *id))
                return false;
        }
    }
    return true;
}

static bool append_positions(LayoutJson *output, const ChannelPositions *positions) {
    if (!append_json(output, "{\n    \"schemaVersion\": 1,\n    \"slots\": [\n"))
        return false;
    bool first = true;
    for (int slot = 0; slot < WM_SLOT_COUNT; slot++) {
        if (!positions->ids[slot][0])
            continue;
        if (!append_json(output, "%s        {\n            \"id\": \"",
                         first ? "" : ",\n") ||
            !append_id(output, positions->ids[slot]) ||
            !append_json(output,
                         "\",\n            \"page\": %d,\n"
                         "            \"index\": %d\n        }",
                         slot / WM_CHANNELS_PER_PAGE, slot % WM_CHANNELS_PER_PAGE))
            return false;
        first = false;
    }
    return append_json(output, "\n    ]\n}\n");
}

bool wm_channel_layout_store_save(WmChannelLayoutStore *store, const WmMenu *menu,
                                  char *error, size_t error_capacity) {
    ChannelPositions current;
    if (!store || !menu || !capture_positions(menu, &current)) {
        wm_error_set(error, error_capacity, "Invalid channel positions to save");
        return false;
    }
    wm_error_set(error, error_capacity, "");
    if (memcmp(&current, &store->baseline, sizeof(current)) == 0)
        return true;
    if (!file_unchanged(store)) {
        wm_error_set(error, error_capacity,
                     "Channel layout config changed externally; reload before saving");
        return false;
    }
    /* IDs need at most six bytes per input byte; 128 extra bytes per slot
     * cover indentation and the bounded numeric fields. */
    LayoutJson output = {.capacity =
                             WM_SLOT_COUNT * (6 * sizeof(current.ids[0]) + 128) + 128};
    output.bytes = malloc(output.capacity);
    if (!output.bytes || !append_positions(&output, &current) ||
        !wm_atomic_file_replace(store->path, output.bytes, output.used)) {
        free(output.bytes);
        wm_error_set(error, error_capacity, "Could not save channel layout config");
        return false;
    }
    free(store->file_contents);
    store->file_contents = output.bytes;
    store->file_length = output.used;
    store->baseline = current;
    return true;
}
