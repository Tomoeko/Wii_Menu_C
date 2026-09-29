#if defined(__linux__)
#define _GNU_SOURCE 1
#endif
#define _XOPEN_SOURCE 700
#if defined(__APPLE__)
#define _DARWIN_C_SOURCE 1
#endif

#include "preparation/prepare_fs.h"
#include "preparation/prepare_hash.h"
#include "preparation/prepare_update.h"
#include "wad/crypto.h"

#include <ctype.h>
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

static size_t find_channel(const PrepareManifest *manifest, const char *id);

static bool normalize_title_id(const char *value, char normalized[17]) {
    if (strlen(value) != 16)
        return false;
    for (size_t index = 0; index < 16; index++) {
        unsigned char character = (unsigned char)value[index];
        if (!isxdigit(character))
            return false;
        normalized[index] = (char)tolower(character);
    }
    normalized[16] = '\0';
    return true;
}

static bool source_hash(const WmJson *json, size_t source, const char *name,
                        char value[41]) {
    size_t token = wm_json_member(json, source, name);
    if (token == WM_JSON_INVALID)
        return true;
    if (!wm_json_copy(json, token, value, 41) || strlen(value) != 40)
        return false;
    for (size_t index = 0; index < 40; index++) {
        if (!((value[index] >= '0' && value[index] <= '9') ||
              (value[index] >= 'a' && value[index] <= 'f')))
            return false;
    }
    return true;
}

bool prepare_choice_contains(const PrepareChoices *choices, const char *id) {
    for (size_t index = 0; index < choices->count; index++) {
        if (strcmp(choices->ids[index], id) == 0)
            return true;
    }
    return false;
}

bool prepare_add_choice(PrepareChoices *choices, const char *value) {
    char id[17];
    if (!normalize_title_id(value, id))
        return false;
    if (prepare_choice_contains(choices, id))
        return true;
    if (choices->count >= PREPARE_MAX_CHANNELS)
        return false;
    strcpy(choices->ids[choices->count++], id);
    return true;
}

void prepare_close_manifest(PrepareManifest *manifest) {
    free(manifest->channels);
    wm_json_free(&manifest->json);
    memset(manifest, 0, sizeof(*manifest));
}

bool prepare_open_manifest(const char *directory, PrepareManifest *manifest) {
    memset(manifest, 0, sizeof(*manifest));
    char path[PREPARE_PATH_CAPACITY];
    struct stat metadata;
    if (!path_join(path, sizeof(path), directory, "channels.json") ||
        lstat(path, &metadata) != 0 || !S_ISREG(metadata.st_mode) ||
        !wm_json_load(&manifest->json, path, PREPARE_MAX_MANIFEST))
        return false;
    const WmJson *json = &manifest->json;
    int version = 0;
    size_t channels = wm_json_member(json, 0, "channels");
    manifest->default_order = wm_json_member(json, 0, "defaultOrder");
    manifest->saved_layout = wm_json_member(json, 0, "savedLayout");
    bool okay =
        wm_json_integer(json, wm_json_member(json, 0, "schemaVersion"), &version) &&
        version == 1 && channels != WM_JSON_INVALID &&
        json->tokens[channels].type == WM_JSON_ARRAY &&
        json->tokens[channels].children <= PREPARE_MAX_CHANNELS &&
        manifest->default_order != WM_JSON_INVALID &&
        json->tokens[manifest->default_order].type == WM_JSON_ARRAY &&
        wm_json_copy(json, wm_json_member(json, 0, "language"), manifest->language,
                     sizeof(manifest->language));
    if (!okay) {
        prepare_close_manifest(manifest);
        return false;
    }
    manifest->count = json->tokens[channels].children;
    manifest->channels =
        calloc(manifest->count ? manifest->count : 1, sizeof(*manifest->channels));
    if (!manifest->channels) {
        prepare_close_manifest(manifest);
        return false;
    }
    for (size_t index = 0; index < manifest->count; index++) {
        size_t entry = wm_json_index(json, channels, index);
        char original[32];
        if (entry == WM_JSON_INVALID || json->tokens[entry].type != WM_JSON_OBJECT ||
            !wm_json_copy(json, wm_json_member(json, entry, "id"), original,
                          sizeof(original)) ||
            !normalize_title_id(original, manifest->channels[index].id) ||
            !wm_json_equals(json, wm_json_member(json, entry, "id"),
                            manifest->channels[index].id)) {
            okay = false;
            break;
        }
        manifest->channels[index].token = entry;
        size_t source = wm_json_member(json, entry, "source");
        if (!source_hash(json, source, "contentSha1",
                         manifest->channels[index].content_sha1) ||
            !source_hash(json, source, "tmdSha1", manifest->channels[index].tmd_sha1)) {
            okay = false;
            break;
        }
        for (size_t previous = 0; previous < index; previous++) {
            if (strcmp(manifest->channels[previous].id, manifest->channels[index].id) ==
                0)
                okay = false;
        }
        if (!okay)
            break;
    }
    for (size_t index = 0;
         okay && index < json->tokens[manifest->default_order].children; index++) {
        char original[32];
        char id[17];
        size_t token = wm_json_index(json, manifest->default_order, index);
        okay = wm_json_copy(json, token, original, sizeof(original)) &&
               normalize_title_id(original, id) && wm_json_equals(json, token, id) &&
               find_channel(manifest, id) != SIZE_MAX;
    }
    if (!okay)
        prepare_close_manifest(manifest);
    return okay;
}

static size_t find_channel(const PrepareManifest *manifest, const char *id) {
    for (size_t index = 0; index < manifest->count; index++) {
        if (strcmp(manifest->channels[index].id, id) == 0)
            return index;
    }
    return SIZE_MAX;
}

static bool write_json_token(FILE *stream, const WmJson *json, size_t token) {
    if (token == WM_JSON_INVALID || token >= json->count)
        return false;
    const WmJsonToken *item = &json->tokens[token];
    size_t start = item->start;
    size_t end = item->end;
    if (item->type == WM_JSON_STRING) {
        if (start == 0 || end >= json->length)
            return false;
        start--;
        end++;
    }
    return end >= start && end <= json->length &&
           fwrite(json->source + start, 1, end - start, stream) == end - start;
}

static bool copy_selected_channel(const char *incoming_assets,
                                  const char *staged_assets, const char *id) {
    char source[PREPARE_PATH_CAPACITY];
    char destination[PREPARE_PATH_CAPACITY];
    char relative[96];
    int length = snprintf(relative, sizeof(relative), "channel-layouts/%s", id);
    if (length < 0 || (size_t)length >= sizeof(relative) ||
        !path_join(source, sizeof(source), incoming_assets, relative) ||
        !path_join(destination, sizeof(destination), staged_assets, relative) ||
        !remove_tree(destination))
        return false;
    struct stat metadata;
    if (lstat(source, &metadata) == 0) {
        if (!S_ISDIR(metadata.st_mode) || !make_file_parent(destination) ||
            !copy_tree(source, destination))
            return false;
    } else if (errno != ENOENT) {
        return false;
    }
    length = snprintf(relative, sizeof(relative), "channel-audio/%s.wav", id);
    if (length < 0 || (size_t)length >= sizeof(relative) ||
        !path_join(source, sizeof(source), incoming_assets, relative) ||
        !path_join(destination, sizeof(destination), staged_assets, relative) ||
        !remove_tree(destination))
        return false;
    if (lstat(source, &metadata) == 0) {
        if (!S_ISREG(metadata.st_mode) || !make_file_parent(destination) ||
            !copy_file(source, destination))
            return false;
    } else if (errno != ENOENT) {
        return false;
    }
    return true;
}

static void hash_name(WmSha1 *sha1, char kind, const char *relative) {
    wm_sha1_update(sha1, (const uint8_t *)&kind, 1);
    wm_sha1_update(sha1, (const uint8_t *)relative, strlen(relative) + 1);
}

bool prepare_hash_regular_file(const char *path, char hexadecimal[41]) {
    int file = open(path, O_RDONLY | O_NOFOLLOW);
    if (file < 0)
        return false;
    struct stat metadata;
    bool okay = fstat(file, &metadata) == 0 && S_ISREG(metadata.st_mode);
    WmSha1 sha1;
    wm_sha1_init(&sha1);
    uint8_t bytes[65536];
    while (okay) {
        ssize_t count = read(file, bytes, sizeof(bytes));
        if (count < 0 && errno == EINTR)
            continue;
        if (count < 0)
            okay = false;
        if (count <= 0)
            break;
        wm_sha1_update(&sha1, bytes, (size_t)count);
    }
    if (close(file) != 0)
        okay = false;
    if (!okay)
        return false;
    uint8_t digest[20];
    wm_sha1_final(&sha1, digest);
    prepare_format_sha1(digest, hexadecimal);
    return true;
}

static bool hash_asset_tree(WmSha1 *sha1, const char *root, const char *relative) {
    char path[PREPARE_PATH_CAPACITY];
    if (!path_join(path, sizeof(path), root, relative))
        return false;
    struct stat metadata;
    if (lstat(path, &metadata) != 0) {
        if (errno != ENOENT)
            return false;
        hash_name(sha1, 'M', relative);
        return true;
    }
    if (S_ISREG(metadata.st_mode)) {
        hash_name(sha1, 'F', relative);
        int file = open(path, O_RDONLY | O_NOFOLLOW);
        if (file < 0 || fstat(file, &metadata) != 0 || !S_ISREG(metadata.st_mode)) {
            if (file >= 0)
                close(file);
            return false;
        }
        char bytes[65536];
        bool okay = true;
        while (okay) {
            ssize_t count = read(file, bytes, sizeof(bytes));
            if (count < 0 && errno == EINTR)
                continue;
            if (count < 0)
                okay = false;
            if (count <= 0)
                break;
            wm_sha1_update(sha1, (const uint8_t *)bytes, (size_t)count);
        }
        if (close(file) != 0)
            okay = false;
        return okay;
    }
    if (!S_ISDIR(metadata.st_mode))
        return false;
    hash_name(sha1, 'D', relative);
    struct dirent **entries = NULL;
    int count = scandir(path, &entries, NULL, alphasort);
    if (count < 0)
        return false;
    bool okay = true;
    for (int index = 0; index < count; index++) {
        const char *name = entries[index]->d_name;
        if (strcmp(name, ".") != 0 && strcmp(name, "..") != 0 && okay) {
            char child[PREPARE_PATH_CAPACITY];
            okay = path_join(child, sizeof(child), relative, name) &&
                   hash_asset_tree(sha1, root, child);
        }
        free(entries[index]);
    }
    free(entries);
    return okay;
}

static bool channel_export_hash(const PrepareManifest *manifest, size_t index,
                                const char *assets, char hexadecimal[41]) {
    const PrepareChannel *channel = &manifest->channels[index];
    WmSha1 sha1;
    wm_sha1_init(&sha1);
    const WmJsonToken *record = &manifest->json.tokens[channel->token];
    wm_sha1_update(&sha1, (const uint8_t *)manifest->json.source + record->start,
                   record->end - record->start);
    char relative[96];
    int length =
        snprintf(relative, sizeof(relative), "channel-layouts/%s", channel->id);
    if (length < 0 || (size_t)length >= sizeof(relative) ||
        !hash_asset_tree(&sha1, assets, relative))
        return false;
    length = snprintf(relative, sizeof(relative), "channel-audio/%s.wav", channel->id);
    if (length < 0 || (size_t)length >= sizeof(relative) ||
        !hash_asset_tree(&sha1, assets, relative))
        return false;
    uint8_t digest[20];
    wm_sha1_final(&sha1, digest);
    prepare_format_sha1(digest, hexadecimal);
    return true;
}

static bool print_channel_version(FILE *stream, const PrepareManifest *manifest,
                                  size_t index) {
    if (index == SIZE_MAX)
        return fputs("null", stream) != EOF;
    const WmJson *json = &manifest->json;
    size_t source = wm_json_member(json, manifest->channels[index].token, "source");
    int version = 0;
    if (!wm_json_integer(json, wm_json_member(json, source, "tmdVersion"), &version))
        return fputs("null", stream) != EOF;
    return fprintf(stream, "%d", version) >= 0;
}

static bool print_source_hash(FILE *stream, const char value[41]) {
    if (!value[0])
        return fputs("null", stream) != EOF;
    return fprintf(stream, "\"%s\"", value) >= 0;
}

static bool choices_present(const PrepareManifest *incoming,
                            const PrepareChoices *replace_ids,
                            const PrepareChoices *keep_ids) {
    for (size_t index = 0; index < replace_ids->count; index++) {
        if (find_channel(incoming, replace_ids->ids[index]) == SIZE_MAX)
            return false;
    }
    for (size_t index = 0; index < keep_ids->count; index++) {
        if (find_channel(incoming, keep_ids->ids[index]) == SIZE_MAX ||
            prepare_choice_contains(replace_ids, keep_ids->ids[index]))
            return false;
    }
    return true;
}

bool prepare_print_update_plan(FILE *stream, const char *base_assets,
                               const char *incoming_assets,
                               const char input_nand_sha1[41],
                               const PrepareChoices *replace_ids,
                               const PrepareChoices *keep_ids, bool replace_all) {
    PrepareManifest base;
    PrepareManifest incoming;
    if (!prepare_open_manifest(base_assets, &base))
        return false;
    if (!prepare_open_manifest(incoming_assets, &incoming)) {
        prepare_close_manifest(&base);
        return false;
    }
    bool okay = strcmp(base.language, incoming.language) == 0 &&
                choices_present(&incoming, replace_ids, keep_ids);
    typedef struct PreparePlanRow {
        size_t existing;
        char existing_hash[41];
        char incoming_hash[41];
        bool keep;
    } PreparePlanRow;
    PreparePlanRow *rows = calloc(incoming.count ? incoming.count : 1, sizeof(*rows));
    if (!rows)
        okay = false;
    for (size_t index = 0; index < incoming.count && okay; index++) {
        const char *id = incoming.channels[index].id;
        rows[index].existing = find_channel(&base, id);
        okay = incoming.channels[index].content_sha1[0] &&
               incoming.channels[index].tmd_sha1[0] &&
               channel_export_hash(&incoming, index, incoming_assets,
                                   rows[index].incoming_hash) &&
               (rows[index].existing == SIZE_MAX ||
                channel_export_hash(&base, rows[index].existing, base_assets,
                                    rows[index].existing_hash));
        rows[index].keep = prepare_choice_contains(keep_ids, id) ||
                           (rows[index].existing != SIZE_MAX && !replace_all &&
                            !prepare_choice_contains(replace_ids, id));
    }
    if (okay)
        okay = fprintf(stream,
                       "{\n  \"schemaVersion\": 3,\n"
                       "  \"inputNandSha1\": \"%s\",\n"
                       "  \"hashKind\": \"exported-channel-SHA1\",\n"
                       "  \"sourceHashKind\": "
                       "\"TMD-bytes-and-TMD-validated-active-content-SHA1\",\n"
                       "  \"rows\": [\n",
                       input_nand_sha1) >= 0;
    for (size_t index = 0; index < incoming.count && okay; index++) {
        const char *id = incoming.channels[index].id;
        const PreparePlanRow *row = &rows[index];
        const char *change = row->existing == SIZE_MAX ? "new"
                             : strcmp(row->existing_hash, row->incoming_hash) == 0
                                 ? "unchanged"
                                 : "different";
        if (index && fputs(",\n", stream) == EOF)
            okay = false;
        if (okay && fprintf(stream,
                            "    {\n"
                            "      \"id\": \"%s\",\n"
                            "      \"change\": \"%s\",\n"
                            "      \"action\": \"%s\",\n"
                            "      \"existingVersion\": ",
                            id, change,
                            row->keep                   ? "keep"
                            : row->existing == SIZE_MAX ? "add"
                                                        : "replace") < 0)
            okay = false;
        if (okay)
            okay = print_channel_version(stream, &base, row->existing);
        if (okay)
            okay = fputs(",\n      \"incomingVersion\": ", stream) != EOF;
        if (okay)
            okay = print_channel_version(stream, &incoming, index);
        if (okay && row->existing != SIZE_MAX) {
            okay = fprintf(stream, ",\n      \"existingExportSha1\": \"%s\"",
                           row->existing_hash) >= 0;
        } else if (okay) {
            okay = fputs(",\n      \"existingExportSha1\": null", stream) != EOF;
        }
        if (okay)
            okay = fprintf(stream,
                           ",\n      \"incomingExportSha1\": \"%s\",\n"
                           "      \"existingContentSha1\": ",
                           row->incoming_hash) >= 0;
        if (okay)
            okay = print_source_hash(stream,
                                     row->existing == SIZE_MAX
                                         ? ""
                                         : base.channels[row->existing].content_sha1);
        if (okay)
            okay = fputs(",\n      \"incomingContentSha1\": ", stream) != EOF;
        if (okay)
            okay = print_source_hash(stream, incoming.channels[index].content_sha1);
        if (okay)
            okay = fputs(",\n      \"existingTmdSha1\": ", stream) != EOF;
        if (okay)
            okay = print_source_hash(
                stream,
                row->existing == SIZE_MAX ? "" : base.channels[row->existing].tmd_sha1);
        if (okay)
            okay = fputs(",\n      \"incomingTmdSha1\": ", stream) != EOF;
        if (okay)
            okay = print_source_hash(stream, incoming.channels[index].tmd_sha1);
        if (okay)
            okay = fputs("\n    }", stream) != EOF;
    }
    if (okay)
        okay = fputs("\n  ]\n}\n", stream) != EOF && fflush(stream) == 0;
    free(rows);
    prepare_close_manifest(&incoming);
    prepare_close_manifest(&base);
    return okay;
}

/* Match the exact plan emitted by this build. The plan contains both input
 * export hashes and the selected actions, so a stale or differently selected
 * review cannot silently publish an update. */
bool prepare_verify_expected_plan(const char *expected_path, const char *base_assets,
                                  const char *incoming_assets,
                                  const char input_nand_sha1[41],
                                  const PrepareChoices *replace_ids,
                                  const PrepareChoices *keep_ids, bool replace_all) {
    int descriptor = open(expected_path, O_RDONLY | O_NOFOLLOW);
    if (descriptor < 0)
        return false;
    struct stat metadata;
    bool okay = fstat(descriptor, &metadata) == 0 && S_ISREG(metadata.st_mode) &&
                metadata.st_size <= PREPARE_MAX_MANIFEST;
    FILE *expected = okay ? fdopen(descriptor, "rb") : NULL;
    if (!expected) {
        close(descriptor);
        return false;
    }
    FILE *actual = tmpfile();
    if (!actual)
        okay = false;
    if (okay)
        okay = prepare_print_update_plan(actual, base_assets, incoming_assets,
                                         input_nand_sha1, replace_ids, keep_ids,
                                         replace_all) &&
               fseek(actual, 0, SEEK_SET) == 0;
    while (okay) {
        int reviewed = fgetc(expected);
        int current = fgetc(actual);
        if (reviewed != current)
            okay = false;
        if (reviewed == EOF || current == EOF) {
            if (ferror(expected) || ferror(actual))
                okay = false;
            break;
        }
    }
    if (actual && fclose(actual) != 0)
        okay = false;
    if (fclose(expected) != 0)
        okay = false;
    return okay;
}

static size_t merged_index(const PrepareChannel *channels, size_t count,
                           const char *id) {
    for (size_t index = 0; index < count; index++) {
        if (strcmp(channels[index].id, id) == 0)
            return index;
    }
    return SIZE_MAX;
}

static bool append_default_order(FILE *stream, const PrepareManifest *manifest,
                                 const PrepareManifest *base,
                                 const PrepareChannel *merged, size_t count,
                                 bool *emitted, bool incoming, bool *first) {
    const WmJson *json = &manifest->json;
    size_t order = manifest->default_order;
    for (size_t index = 0; index < json->tokens[order].children; index++) {
        size_t token = wm_json_index(json, order, index);
        char original[32];
        char id[17];
        if (!wm_json_copy(json, token, original, sizeof(original)) ||
            !normalize_title_id(original, id))
            return false;
        size_t target = merged_index(merged, count, id);
        if (target == SIZE_MAX || emitted[target])
            continue;
        if (incoming &&
            (find_channel(base, id) != SIZE_MAX || !merged[target].incoming))
            continue;
        if (!*first && fputs(", ", stream) == EOF)
            return false;
        if (fprintf(stream, "\"%s\"", id) < 0)
            return false;
        emitted[target] = true;
        *first = false;
    }
    return true;
}

static bool write_merged_manifest(const char *staged_assets,
                                  const PrepareManifest *base,
                                  const PrepareManifest *incoming,
                                  const PrepareChannel *merged, size_t count) {
    char temporary[PREPARE_PATH_CAPACITY];
    char destination[PREPARE_PATH_CAPACITY];
    if (!path_join(temporary, sizeof(temporary), staged_assets, "channels.json.tmp") ||
        !path_join(destination, sizeof(destination), staged_assets, "channels.json"))
        return false;
    FILE *stream = fopen(temporary, "wb");
    if (!stream)
        return false;
    bool okay = fputs("{\n  \"schemaVersion\": 1,\n"
                      "  \"source\": {\"kind\": \"local-prepared-channel-update\"},\n"
                      "  \"language\": ",
                      stream) != EOF &&
                write_json_token(stream, &base->json,
                                 wm_json_member(&base->json, 0, "language")) &&
                fputs(",\n  \"channels\": [\n", stream) != EOF;
    for (size_t index = 0; okay && index < count; index++) {
        const PrepareManifest *source = merged[index].incoming ? incoming : base;
        if (index && fputs(",\n", stream) == EOF)
            okay = false;
        if (okay)
            okay = write_json_token(stream, &source->json, merged[index].token);
    }
    if (okay)
        okay = fputs("\n  ],\n  \"defaultOrder\": [", stream) != EOF;
    bool *emitted = calloc(count ? count : 1, sizeof(*emitted));
    if (!emitted)
        okay = false;
    bool first = true;
    if (okay)
        okay = append_default_order(stream, base, base, merged, count, emitted, false,
                                    &first);
    if (okay)
        okay = append_default_order(stream, incoming, base, merged, count, emitted,
                                    true, &first);
    free(emitted);
    if (okay)
        okay = fputs("],\n  \"savedLayout\": ", stream) != EOF;
    if (okay && base->saved_layout != WM_JSON_INVALID) {
        okay = write_json_token(stream, &base->json, base->saved_layout);
    } else if (okay) {
        okay = fputs("null", stream) != EOF;
    }
    if (okay)
        okay = fputs(",\n  \"notes\": ["
                     "\"Installed title resources are local exports; "
                     "native modules and scripts are not executed.\"]\n}\n",
                     stream) != EOF;
    if (fclose(stream) != 0)
        okay = false;
    if (!okay) {
        unlink(temporary);
        return false;
    }
    /* The existing catalog is still in place at this point. Replace it only
     * after validating the newly written document itself. */
    if (okay) {
        WmJson parsed;
        okay = wm_json_load(&parsed, temporary, PREPARE_MAX_MANIFEST);
        if (okay)
            wm_json_free(&parsed);
    }
    if (okay)
        okay = rename(temporary, destination) == 0;
    if (!okay)
        unlink(temporary);
    return okay;
}

bool prepare_update_channels(const char *base_assets, const char *incoming_assets,
                             const char *staged_assets,
                             const PrepareChoices *replace_ids,
                             const PrepareChoices *keep_ids, bool replace_all) {
    PrepareManifest base;
    PrepareManifest incoming;
    if (!prepare_open_manifest(base_assets, &base)) {
        fputs("The existing channel catalog is invalid.\n", stderr);
        return false;
    }
    if (!prepare_open_manifest(incoming_assets, &incoming)) {
        fputs("The incoming channel catalog is invalid.\n", stderr);
        prepare_close_manifest(&base);
        return false;
    }
    bool okay = strcmp(base.language, incoming.language) == 0;
    if (!okay)
        fputs("Channel catalog languages do not match.\n", stderr);
    if (okay)
        okay = choices_present(&incoming, replace_ids, keep_ids);
    if (!okay)
        fputs("A selected title is absent or both kept and replaced.\n", stderr);
    PrepareChannel *merged = calloc(PREPARE_MAX_CHANNELS, sizeof(*merged));
    if (!merged)
        okay = false;
    size_t count = 0;
    if (okay) {
        for (size_t index = 0; index < base.count; index++) {
            merged[count++] = base.channels[index];
        }
        for (size_t index = 0; index < incoming.count && okay; index++) {
            const PrepareChannel *candidate = &incoming.channels[index];
            size_t existing = merged_index(merged, count, candidate->id);
            bool selected = !prepare_choice_contains(keep_ids, candidate->id) &&
                            (existing == SIZE_MAX || replace_all ||
                             prepare_choice_contains(replace_ids, candidate->id));
            if (!selected)
                continue;
            if (existing == SIZE_MAX && count >= PREPARE_MAX_CHANNELS) {
                okay = false;
                break;
            }
            PrepareChannel chosen = *candidate;
            chosen.incoming = true;
            if (existing == SIZE_MAX)
                merged[count++] = chosen;
            else
                merged[existing] = chosen;
            okay = copy_selected_channel(incoming_assets, staged_assets, candidate->id);
        }
        if (okay)
            okay =
                write_merged_manifest(staged_assets, &base, &incoming, merged, count);
    }
    free(merged);
    prepare_close_manifest(&incoming);
    prepare_close_manifest(&base);
    return okay;
}
