#include "wii_menu/menu/local_catalog.h"

#include "../support/atomic_file.h"
#include "wii_menu/support/regular_file.h"
#include "wii_menu/support/json.h"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static bool catalog_path(char path[4096], const char *assets) {
    if (!assets || !*assets)
        return false;
    int length = snprintf(path, 4096, "%s/channels.local.json", assets);
    return length > 0 && length < 4096;
}

bool wm_local_channel_id_valid(const char *id) {
    if (!id || strncmp(id, "custom-", 7) != 0)
        return false;
    size_t length = strlen(id);
    if (length < 8 || length > 63 ||
        !((id[7] >= 'a' && id[7] <= 'z') || (id[7] >= '0' && id[7] <= '9')))
        return false;
    for (size_t index = 8; index < length; index++) {
        unsigned char character = (unsigned char)id[index];
        if (!(character >= 'a' && character <= 'z') &&
            !(character >= '0' && character <= '9') && character != '_' &&
            character != '-')
            return false;
    }
    return true;
}

bool wm_local_native_id_valid(const char *id) {
    if (!id || strlen(id) != 16)
        return false;
    for (size_t index = 0; index < 16; index++) {
        if (!isxdigit((unsigned char)id[index]))
            return false;
    }
    return true;
}

static bool local_id_valid(const WmLocalChannel *channel) {
    return channel->imported ? wm_local_native_id_valid(channel->id)
                             : wm_local_channel_id_valid(channel->id);
}

bool wm_local_catalog_load(const char *assets, WmLocalCatalog *catalog) {
    if (!catalog)
        return false;
    memset(catalog, 0, sizeof(*catalog));
    char path[4096];
    if (!catalog_path(path, assets))
        return false;
    char *source = NULL;
    size_t length = 0;
    WmRegularFileStatus read =
        wm_regular_file_read(path, 1024 * 1024, &source, &length);
    if (read == WM_REGULAR_FILE_MISSING)
        return true;
    if (read != WM_REGULAR_FILE_OK)
        return false;

    WmJson json;
    bool valid = wm_json_parse(&json, source, length);
    free(source);
    if (!valid)
        return false;
    size_t version = wm_json_member(&json, 0, "schemaVersion");
    size_t channels = wm_json_member(&json, 0, "channels");
    size_t hidden = wm_json_member(&json, 0, "hidden");
    int schema;
    valid = wm_json_integer(&json, version, &schema) && schema == 1 &&
            channels < json.count && json.tokens[channels].type == WM_JSON_ARRAY &&
            json.tokens[channels].children <= WM_LOCAL_CHANNEL_LIMIT &&
            hidden < json.count && json.tokens[hidden].type == WM_JSON_ARRAY &&
            json.tokens[hidden].children <= WM_LOCAL_HIDDEN_LIMIT;
    for (size_t index = 0; valid && index < json.tokens[channels].children; index++) {
        size_t entry = wm_json_index(&json, channels, index);
        WmLocalChannel *channel = &catalog->channels[catalog->channel_count];
        size_t removed = wm_json_member(&json, entry, "removed");
        size_t kind = wm_json_member(&json, entry, "kind");
        channel->imported = wm_json_equals(&json, kind, "imported");
        valid = wm_json_copy(&json, wm_json_member(&json, entry, "id"), channel->id,
                             sizeof(channel->id)) &&
                wm_json_copy_text(&json, wm_json_member(&json, entry, "title"),
                                  channel->title, sizeof(channel->title)) &&
                channel->title[0] && local_id_valid(channel) &&
                (channel->imported || wm_json_equals(&json, kind, "custom")) &&
                removed < json.count && json.tokens[removed].type == WM_JSON_BOOLEAN;
        if (!valid)
            break;
        channel->removed = json.source[json.tokens[removed].start] == 't';
        for (size_t prior = 0; prior < catalog->channel_count; prior++) {
            if (strcmp(catalog->channels[prior].id, channel->id) == 0)
                valid = false;
        }
        catalog->channel_count++;
    }
    for (size_t index = 0; valid && index < json.tokens[hidden].children; index++) {
        size_t token = wm_json_index(&json, hidden, index);
        char *id = catalog->hidden[catalog->hidden_count];
        valid = wm_json_copy(&json, token, id, 65) && wm_local_native_id_valid(id);
        if (!valid)
            break;
        for (size_t prior = 0; prior < catalog->hidden_count; prior++) {
            if (strcmp(catalog->hidden[prior], id) == 0)
                valid = false;
        }
        catalog->hidden_count++;
    }
    wm_json_free(&json);
    if (!valid)
        memset(catalog, 0, sizeof(*catalog));
    return valid;
}

static bool write_string(FILE *stream, const char *value) {
    if (fputc('"', stream) == EOF)
        return false;
    for (const unsigned char *cursor = (const unsigned char *)value; *cursor;
         cursor++) {
        unsigned char character = *cursor;
        if (character == '"' || character == '\\') {
            if (fputc('\\', stream) == EOF)
                return false;
        } else if (character < 0x20) {
            if (fprintf(stream, "\\u%04x", character) < 0)
                return false;
            continue;
        }
        if (fputc(character, stream) == EOF)
            return false;
    }
    return fputc('"', stream) != EOF;
}

bool wm_local_catalog_save(const char *assets, const WmLocalCatalog *catalog) {
    char path[4096];
    if (!catalog || !catalog_path(path, assets) ||
        catalog->channel_count > WM_LOCAL_CHANNEL_LIMIT ||
        catalog->hidden_count > WM_LOCAL_HIDDEN_LIMIT)
        return false;
    WmAtomicFile file;
    if (wm_atomic_file_open(&file, path) != WM_ATOMIC_FILE_OK)
        return false;
    FILE *stream = file.stream;
    bool okay =
        fputs("{\n    \"schemaVersion\": 1,\n    \"channels\": [\n", stream) >= 0;
    for (size_t index = 0; okay && index < catalog->channel_count; index++) {
        const WmLocalChannel *channel = &catalog->channels[index];
        okay =
            local_id_valid(channel) && channel->title[0] &&
            fputs(index ? ",\n        {\"id\": " : "        {\"id\": ", stream) >= 0 &&
            write_string(stream, channel->id) && fputs(", \"title\": ", stream) >= 0 &&
            write_string(stream, channel->title) &&
            fprintf(stream, ", \"kind\": \"%s\", \"removed\": %s}",
                    channel->imported ? "imported" : "custom",
                    channel->removed ? "true" : "false") >= 0;
    }
    okay = okay && fputs("\n    ],\n    \"hidden\": [", stream) >= 0;
    for (size_t index = 0; okay && index < catalog->hidden_count; index++) {
        okay = wm_local_native_id_valid(catalog->hidden[index]) &&
               fputs(index ? ", " : "", stream) >= 0 &&
               write_string(stream, catalog->hidden[index]);
    }
    okay = okay && fputs("]\n}\n", stream) >= 0;
    if (!okay) {
        wm_atomic_file_discard(&file);
        return false;
    }
    return wm_atomic_file_commit(&file, path);
}
