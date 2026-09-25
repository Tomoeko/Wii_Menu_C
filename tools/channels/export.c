#define _POSIX_C_SOURCE 200809L

#include "wii_menu/image.h"
#include "wii_menu/audio_wave.h"
#include "wii_menu/resource_audio.h"
#include "wii_menu/resource_ash.h"
#include "wii_menu/resource_layout.h"
#include "wii_menu/resource_tpl.h"
#include "wii_menu/resource_u8.h"
#include "wii_menu/saved_layout.h"

#include "md5.h"
#include "../wad/crypto.h"

#include <dirent.h>
#include <ctype.h>
#include <errno.h>
#include <inttypes.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

enum {
    WM_PATH_CAP = 4096,
    WM_MAX_CONTENT = 64 * 1024 * 1024,
    WM_MAX_CHANNELS = 2048,
    WM_MAX_TMD_CONTENTS = 4096
};

typedef struct WmChannelExport {
    char id[17];
    char short_id[5];
    char title[512];
    char titles[10][512];
    char content_id[9];
    char source_file[128];
    char content_sha1[41];
    char tmd_sha1[41];
    char icon_layout[WM_PATH_CAP];
    char banner_layout[WM_PATH_CAP];
    struct WmLayoutPath *layouts[2];
    size_t layout_count[2];
    unsigned version;
    unsigned tmd_version;
    uint32_t flags;
    unsigned icon_textures;
    unsigned banner_textures;
    unsigned icon_animations;
    unsigned banner_animations;
    bool preferred;
    bool has_audio;
    uint32_t audio_rate;
    uint32_t audio_frames;
    uint32_t audio_loop_start;
    uint32_t audio_loop_end;
    uint8_t audio_channels;
    bool audio_looping;
} WmChannelExport;

typedef struct WmLayoutPath {
    char name[128];
    char path[WM_PATH_CAP];
} WmLayoutPath;

typedef struct WmChannelList {
    WmChannelExport *items;
    size_t count;
    size_t capacity;
} WmChannelList;

typedef struct WmTmdContent {
    uint32_t id;
    uint16_t type;
    uint64_t size;
    uint8_t sha1[20];
} WmTmdContent;

typedef struct WmMetadata {
    char titles[10][512];
    char title[512];
    size_t archive_offset;
    uint32_t flags;
    uint32_t version;
} WmMetadata;

static const char *const wm_languages[10] = {
    "JPN", "ENG", "GER", "FRA", "SPA", "ITA", "NED", "CHN", "CHT", "KOR"
};

static uint16_t wm_be16(const uint8_t *bytes)
{
    return (uint16_t)(((uint16_t)bytes[0] << 8) | bytes[1]);
}

static uint32_t wm_be32(const uint8_t *bytes)
{
    return ((uint32_t)bytes[0] << 24) | ((uint32_t)bytes[1] << 16) |
           ((uint32_t)bytes[2] << 8) | bytes[3];
}

static uint64_t wm_be64(const uint8_t *bytes)
{
    return ((uint64_t)wm_be32(bytes) << 32) | wm_be32(bytes + 4);
}

static void wm_format_sha1(const uint8_t digest[20], char result[41])
{
    for (size_t index = 0; index < 20; ++index) {
        snprintf(result + index * 2, 3, "%02x", digest[index]);
    }
    result[40] = '\0';
}

static bool wm_fits(size_t size, size_t offset, size_t length)
{
    return offset <= size && length <= size - offset;
}

static bool wm_hex8(const char *value)
{
    if (strlen(value) != 8) return false;
    for (unsigned index = 0; index < 8; ++index) {
        char c = value[index];
        if (!((c >= '0' && c <= '9') || (c >= 'a' && c <= 'f') ||
              (c >= 'A' && c <= 'F'))) return false;
    }
    return true;
}

static bool wm_regular_file(const char *path)
{
    struct stat info;
    return lstat(path, &info) == 0 && S_ISREG(info.st_mode);
}

static bool wm_directory(const char *path)
{
    struct stat info;
    return lstat(path, &info) == 0 && S_ISDIR(info.st_mode);
}

static bool wm_make_directories(const char *path)
{
    size_t length = strlen(path);
    if (length == 0 || length >= WM_PATH_CAP) return false;
    char buffer[WM_PATH_CAP];
    memcpy(buffer, path, length + 1);
    for (size_t index = 1; index <= length; ++index) {
        if (buffer[index] != '/' && buffer[index] != '\0') continue;
        char saved = buffer[index];
        buffer[index] = '\0';
        if (mkdir(buffer, 0755) != 0 && errno != EEXIST) return false;
        if (!wm_directory(buffer)) return false;
        buffer[index] = saved;
    }
    return true;
}

static bool wm_output_parent(const char *path)
{
    char parent[WM_PATH_CAP];
    size_t size = strlen(path);
    if (size == 0 || size >= sizeof(parent)) return false;
    memcpy(parent, path, size + 1);
    char *slash = strrchr(parent, '/');
    if (!slash) return true;
    *slash = '\0';
    return wm_make_directories(parent);
}

static bool wm_output_target_safe(const char *path)
{
    struct stat info;
    if (lstat(path, &info) == 0) return S_ISREG(info.st_mode);
    return errno == ENOENT;
}

static uint8_t *wm_read_file(const char *path, size_t maximum, size_t *size)
{
    *size = 0;
    if (!wm_regular_file(path)) return NULL;
    FILE *stream = fopen(path, "rb");
    if (!stream) return NULL;
    if (fseek(stream, 0, SEEK_END) != 0) {
        fclose(stream);
        return NULL;
    }
    long length = ftell(stream);
    if (length < 0 || (uint64_t)length > maximum ||
        fseek(stream, 0, SEEK_SET) != 0) {
        fclose(stream);
        return NULL;
    }
    uint8_t *data = malloc(length != 0 ? (size_t)length : 1);
    if (!data || fread(data, 1, (size_t)length, stream) != (size_t)length ||
        fgetc(stream) != EOF) {
        free(data);
        fclose(stream);
        return NULL;
    }
    fclose(stream);
    *size = (size_t)length;
    return data;
}

static bool wm_write_file(const char *path, const void *data, size_t size)
{
    if (!wm_output_parent(path) || !wm_output_target_safe(path)) return false;
    FILE *stream = fopen(path, "wb");
    if (!stream) return false;
    bool valid = fwrite(data, 1, size, stream) == size;
    if (fclose(stream) != 0) valid = false;
    if (!valid) remove(path);
    return valid;
}

static bool wm_ends_with(const char *path, const char *extension)
{
    size_t path_size = strlen(path);
    size_t extension_size = strlen(extension);
    return path_size >= extension_size &&
           strcmp(path + path_size - extension_size, extension) == 0;
}

static bool wm_stem(const char *path, const char *extension,
                    char stem[128], char basename[128])
{
    if (!wm_ends_with(path, extension)) return false;
    const char *name = strrchr(path, '/');
    name = name ? name + 1 : path;
    size_t length = strlen(name);
    size_t suffix = strlen(extension);
    if (length <= suffix || length >= 128) return false;
    for (size_t index = 0; index < length - suffix; ++index) {
        char c = name[index];
        if (!((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
              (c >= '0' && c <= '9') || c == '_' || c == '-' || c == '.' ||
              c == '+')) {
            return false;
        }
    }
    memcpy(stem, name, length - suffix);
    stem[length - suffix] = '\0';
    memcpy(basename, name, length + 1);
    return true;
}

static char *wm_copy(const char *source)
{
    size_t length = strlen(source) + 1;
    char *copy = malloc(length);
    if (copy) memcpy(copy, source, length);
    return copy;
}

static bool wm_append_channel(WmChannelList *list,
                              const WmChannelExport *channel)
{
    if (list->count >= WM_MAX_CHANNELS) return false;
    if (list->count == list->capacity) {
        size_t capacity = list->capacity ? list->capacity * 2 : 16;
        WmChannelExport *items = realloc(list->items,
                                          capacity * sizeof(*items));
        if (!items) return false;
        list->items = items;
        list->capacity = capacity;
    }
    list->items[list->count++] = *channel;
    return true;
}

static bool wm_utf16_line(const uint8_t *bytes, char output[512])
{
    size_t position = 0;
    for (size_t index = 0; index < 21; ++index) {
        uint32_t codepoint = wm_be16(bytes + index * 2);
        if (codepoint == 0) break;
        if (codepoint >= 0xd800 && codepoint <= 0xdbff) {
            if (index + 1 == 21) return false;
            uint32_t low = wm_be16(bytes + (++index) * 2);
            if (low < 0xdc00 || low > 0xdfff) return false;
            codepoint = 0x10000 + ((codepoint - 0xd800) << 10) +
                        (low - 0xdc00);
        } else if (codepoint >= 0xdc00 && codepoint <= 0xdfff) {
            return false;
        }
        size_t bytes_needed = codepoint < 0x80 ? 1 :
                              codepoint < 0x800 ? 2 :
                              codepoint < 0x10000 ? 3 : 4;
        if (position + bytes_needed >= 512) return false;
        if (bytes_needed == 1) {
            output[position++] = (char)codepoint;
        } else {
            for (size_t part = 0; part < bytes_needed; ++part) {
                unsigned shift = (unsigned)((bytes_needed - part - 1) * 6);
                uint8_t prefix = part == 0 ?
                    (uint8_t)(0xffu << (8 - bytes_needed)) : 0x80u;
                uint8_t payload = part == 0 ?
                    (uint8_t)((1u << (7 - bytes_needed)) - 1) : 0x3fu;
                output[position++] = (char)(prefix |
                    ((codepoint >> shift) & payload));
            }
        }
    }
    output[position] = '\0';
    return true;
}

static bool wm_metadata(const uint8_t *data, size_t size, unsigned language,
                         WmMetadata *metadata)
{
    size_t position = 0;
    while (position + 4 <= size && position < 0xa3 &&
           memcmp(data + position, "IMET", 4) != 0) position++;
    if (position >= 0xa3 || !wm_fits(size, position, 28 + 10 * 84)) return false;
    uint32_t header_size = wm_be32(data + position + 4);
    size_t archive_offset = position + (size_t)header_size;
    if (archive_offset < 64) return false;
    archive_offset -= 64;
    if (!wm_fits(size, archive_offset, 4) ||
        memcmp(data + archive_offset, "\x55\xaa\x38\x2d", 4) != 0) return false;
    memset(metadata, 0, sizeof(*metadata));
    metadata->archive_offset = archive_offset;
    metadata->version = wm_be32(data + position + 8);
    metadata->flags = wm_be32(data + position + 24);
    for (unsigned index = 0; index < 10; ++index) {
        char first[512];
        char second[512];
        const uint8_t *name = data + position + 28 + index * 84;
        if (!wm_utf16_line(name, first) ||
            !wm_utf16_line(name + 42, second)) return false;
        int length = snprintf(metadata->titles[index], 512, "%s%s%s", first,
                              first[0] && second[0] ? " " : "", second);
        if (length < 0 || length >= 512) return false;
    }
    const char *title = metadata->titles[language][0]
        ? metadata->titles[language] : metadata->titles[1];
    if (!title[0]) {
        for (unsigned index = 0; index < 10; ++index) {
            if (metadata->titles[index][0]) {
                title = metadata->titles[index];
                break;
            }
        }
    }
    memcpy(metadata->title, title, strlen(title) + 1);
    return true;
}

static bool wm_tmd_contents(const uint8_t *data, size_t size,
                             const char *expected_title,
                             WmTmdContent **contents, size_t *count,
                             unsigned *version)
{
    *contents = NULL;
    *count = 0;
    if (size < 4) return false;
    uint32_t signature = wm_be32(data);
    size_t body = signature == 0x10000 ? 0x240 :
                  signature == 0x10001 ? 0x140 :
                  signature == 0x10002 ? 0x80 : 0;
    if (body == 0 || !wm_fits(size, body, 0xa4)) return false;
    char title[17];
    for (unsigned index = 0; index < 8; ++index) {
        snprintf(title + index * 2, 3, "%02x", data[body + 0x4c + index]);
    }
    if (strcmp(title, expected_title) != 0) return false;
    *version = wm_be16(data + body + 0x9c);
    size_t item_count = wm_be16(data + body + 0x9e);
    if (item_count == 0 || item_count > WM_MAX_TMD_CONTENTS ||
        !wm_fits(size, body + 0xa4, item_count * 36)) return false;
    WmTmdContent *items = calloc(item_count, sizeof(*items));
    if (!items) return false;
    for (size_t index = 0; index < item_count; ++index) {
        const uint8_t *record = data + body + 0xa4 + index * 36;
        items[index].id = wm_be32(record);
        items[index].type = wm_be16(record + 6);
        items[index].size = wm_be64(record + 8);
        memcpy(items[index].sha1, record + 16, 20);
        for (size_t previous = 0; previous < index; ++previous) {
            if (items[previous].id == items[index].id) {
                free(items);
                return false;
            }
        }
    }
    *contents = items;
    *count = item_count;
    return true;
}

static bool wm_has_imet(const char *path)
{
    if (!wm_regular_file(path)) return false;
    FILE *stream = fopen(path, "rb");
    if (!stream) return false;
    uint8_t header[0xa3];
    size_t size = fread(header, 1, sizeof(header), stream);
    fclose(stream);
    for (size_t index = 0; index + 4 <= size; ++index) {
        if (memcmp(header + index, "IMET", 4) == 0) return true;
    }
    return false;
}

static bool wm_validated_content(const char *path, const WmTmdContent *record,
                                  uint8_t **data, size_t *size)
{
    if (record->size > WM_MAX_CONTENT) return false;
    size_t file_size = 0;
    uint8_t *bytes = wm_read_file(path, WM_MAX_CONTENT + 64, &file_size);
    if (!bytes || file_size < record->size) {
        free(bytes);
        return false;
    }
    for (size_t index = (size_t)record->size; index < file_size; ++index) {
        if (bytes[index] != 0) {
            free(bytes);
            return false;
        }
    }
    WmSha1 sha1;
    uint8_t digest[20];
    wm_sha1_init(&sha1);
    wm_sha1_update(&sha1, bytes, (size_t)record->size);
    wm_sha1_final(&sha1, digest);
    if (memcmp(digest, record->sha1, sizeof(digest)) != 0) {
        free(bytes);
        return false;
    }
    *data = bytes;
    *size = (size_t)record->size;
    return true;
}

static bool wm_lz77_decode(const uint8_t *data, size_t size,
                            uint8_t **output, size_t *output_size)
{
    *output = NULL;
    *output_size = 0;
    if (size < 4 || data[0] != 0x10) return false;
    size_t length = (size_t)data[1] | ((size_t)data[2] << 8) |
                    ((size_t)data[3] << 16);
    if (length > WM_MAX_CONTENT) return false;
    if (length == 0) {
        for (size_t index = 4; index < size; ++index) {
            if (data[index] != 0) return false;
        }
    }
    uint8_t *decoded = malloc(length ? length : 1);
    if (!decoded) return false;
    size_t source = 4;
    size_t produced = 0;
    while (produced < length) {
        if (source >= size) break;
        uint8_t flags = data[source++];
        for (int bit = 7; bit >= 0 && produced < length; --bit) {
            if ((flags & (1u << bit)) == 0) {
                if (source >= size) goto fail;
                decoded[produced++] = data[source++];
            } else {
                if (!wm_fits(size, source, 2)) goto fail;
                uint16_t word = wm_be16(data + source);
                source += 2;
                size_t count = (word >> 12) + 3;
                size_t distance = (word & 0x0fff) + 1;
                if (distance > produced) goto fail;
                for (size_t index = 0; index < count && produced < length;
                     ++index) {
                    decoded[produced] = decoded[produced - distance];
                    produced++;
                }
            }
        }
    }
    if (produced != length) goto fail;
    *output = decoded;
    *output_size = length;
    return true;
fail:
    free(decoded);
    return false;
}

static bool wm_unwrap_resource(const uint8_t *data, size_t size,
                               uint8_t **output, size_t *output_size)
{
    uint8_t *current = malloc(size ? size : 1);
    if (!current) return false;
    memcpy(current, data, size);
    for (unsigned layer = 0; layer < 4; ++layer) {
        if (size >= 4 && memcmp(current, "IMD5", 4) == 0) {
            if (size < 32) break;
            size_t length = wm_be32(current + 4);
            if (!wm_fits(size, 32, length)) break;
            WmMd5 md5;
            uint8_t digest[16];
            wm_md5_init(&md5);
            wm_md5_update(&md5, current + 32, length);
            wm_md5_final(&md5, digest);
            if (memcmp(digest, current + 16, 16) != 0) break;
            memmove(current, current + 32, length);
            size = length;
        } else if (size >= 4 && memcmp(current, "LZ77", 4) == 0) {
            uint8_t *decoded = NULL;
            size_t decoded_size = 0;
            if (!wm_lz77_decode(current + 4, size - 4,
                                &decoded, &decoded_size)) break;
            free(current);
            current = decoded;
            size = decoded_size;
        } else if (size >= 4 && memcmp(current, "ASH0", 4) == 0) {
            uint8_t *decoded = NULL;
            size_t decoded_size = 0;
            char error[160];
            if (!wm_ash_decode(current, size, &decoded, &decoded_size,
                                error, sizeof(error))) break;
            free(current);
            current = decoded;
            size = decoded_size;
        } else if (size > 0 && current[0] == 0x10) {
            uint8_t *decoded = NULL;
            size_t decoded_size = 0;
            if (!wm_lz77_decode(current, size, &decoded, &decoded_size)) break;
            free(current);
            current = decoded;
            size = decoded_size;
        } else {
            *output = current;
            *output_size = size;
            return true;
        }
    }
    free(current);
    return false;
}

static bool wm_export_channel_audio(const WmU8Entry *entry,
                                    const char *output,
                                    WmChannelExport *channel)
{
    if (!entry) return true;
    uint8_t *decoded = NULL;
    size_t decoded_size = 0;
    WmAudioPcm pcm = {0};
    char error[160] = {0};
    if (!wm_unwrap_resource(entry->data, entry->size,
                            &decoded, &decoded_size)) {
        fprintf(stderr, "Could not unwrap channel sound resource.\n");
        return false;
    }
    bool valid = wm_bns_decode(decoded, decoded_size, &pcm,
                               error, sizeof(error));
    if (!valid) {
        fprintf(stderr, "Channel sound decode: %s\n", error);
        free(decoded);
        return false;
    }
    char directory[WM_PATH_CAP], destination[WM_PATH_CAP];
    int first = snprintf(directory, sizeof(directory),
                         "%s/channel-audio", output);
    int second = snprintf(destination, sizeof(destination),
                          "%s/%s.wav", directory, channel->id);
    valid = first > 0 && first < (int)sizeof(directory) &&
            second > 0 && second < (int)sizeof(destination) &&
            wm_make_directories(directory) &&
            wm_audio_wav_write(destination, &pcm, error, sizeof(error));
    if (!valid) {
        fprintf(stderr, "Could not export channel sound: %s\n", error);
    } else {
        channel->has_audio = true;
        channel->audio_rate = pcm.sample_rate;
        channel->audio_frames = pcm.frame_count;
        channel->audio_channels = pcm.channels;
        channel->audio_looping = pcm.looping;
        channel->audio_loop_start = pcm.loop_start;
        channel->audio_loop_end = pcm.loop_end;
    }
    wm_audio_pcm_free(&pcm);
    free(decoded);
    return valid;
}

static int wm_compare_layouts(const void *left, const void *right)
{
    const WmLayoutPath *a = left;
    const WmLayoutPath *b = right;
    return strcmp(a->name, b->name);
}

static bool wm_export_resource(const WmU8Entry *entry, const char *output,
                               const char *channel_id, const char *kind,
                               const char *source_file,
                               WmLayoutPath **layout_paths,
                               size_t *layout_count,
                               char default_layout[WM_PATH_CAP],
                               unsigned *texture_count,
                               unsigned *animation_count)
{
    uint8_t *decoded = NULL;
    size_t decoded_size = 0;
    if (!wm_unwrap_resource(entry->data, entry->size,
                            &decoded, &decoded_size)) {
        fprintf(stderr, "Could not validate a channel resource envelope.\n");
        return false;
    }
    WmU8Archive archive = {0};
    char error[160] = {0};
    if (!wm_u8_parse(decoded, decoded_size, &archive, error, sizeof(error))) {
        fprintf(stderr, "Channel resource archive: %s\n", error);
        free(decoded);
        return false;
    }
    WmResourceTexture *textures = calloc(archive.count + 1, sizeof(*textures));
    WmResourceAnimation *animations = calloc(archive.count + 1,
                                              sizeof(*animations));
    WmLayoutPath *layouts = calloc(archive.count + 1, sizeof(*layouts));
    if (!textures || !animations || !layouts) {
        free(textures);
        free(animations);
        free(layouts);
        wm_u8_free(&archive);
        free(decoded);
        return false;
    }
    size_t textures_found = 0;
    size_t animations_found = 0;
    size_t layouts_found = 0;
    bool valid = true;
    char relative[WM_PATH_CAP];
    char destination[WM_PATH_CAP];
    char source[WM_PATH_CAP];
    for (size_t index = 0; index < archive.count && valid; ++index) {
        const WmU8Entry *item = &archive.entries[index];
        char stem[128];
        char basename[128];
        if (wm_stem(item->path, ".brlan", stem, basename)) {
            animations[animations_found].name = wm_copy(stem);
            if (!animations[animations_found].name) {
                valid = false;
                break;
            }
            animations[animations_found].data = item->data;
            animations[animations_found].size = item->size;
            animations_found++;
        } else if (wm_stem(item->path, ".tpl", stem, basename)) {
            for (size_t previous = 0; previous < textures_found; ++previous) {
                if (strcmp(textures[previous].name, basename) == 0) {
                    fprintf(stderr, "Ambiguous channel texture basename.\n");
                    valid = false;
                }
            }
            if (!valid) break;
            WmTpl tpl = {0};
            if (!wm_tpl_decode(item->data, item->size, &tpl,
                                error, sizeof(error))) {
                fprintf(stderr, "Channel TPL decode: %s\n", error);
                valid = false;
                break;
            }
            for (size_t image = 0; image < tpl.count && valid; ++image) {
                int length = snprintf(relative, sizeof(relative),
                    image == 0
                        ? "channel-layouts/%s/%s/textures/%s.wmra"
                        : "channel-layouts/%s/%s/textures/%s-%zu.wmra",
                    channel_id, kind, stem, image);
                int full = snprintf(destination, sizeof(destination),
                                    "%s/%s", output, relative);
                WmImage converted = {
                    tpl.images[image].width,
                    tpl.images[image].height,
                    tpl.images[image].rgba
                };
                if (length < 0 || length >= (int)sizeof(relative) ||
                    full < 0 || full >= (int)sizeof(destination) ||
                    !wm_output_parent(destination) ||
                    !wm_output_target_safe(destination) ||
                    !wm_image_write(destination, &converted)) {
                    valid = false;
                }
            }
            if (valid && tpl.count > 0) {
                int url_size = snprintf(relative, sizeof(relative),
                    "channel-layouts/%s/%s/textures/%s.png",
                    channel_id, kind, stem);
                int source_size = snprintf(source, sizeof(source),
                    "%s/meta/%s.bin/%s", source_file, kind, item->path);
                if (url_size < 0 || url_size >= (int)sizeof(relative) ||
                    source_size < 0 || source_size >= (int)sizeof(source)) {
                    valid = false;
                } else {
                    char *name = wm_copy(basename);
                    char *url = wm_copy(relative);
                    char *origin = wm_copy(source);
                    if (!name || !url || !origin) {
                        free(name);
                        free(url);
                        free(origin);
                        valid = false;
                    } else {
                        textures[textures_found++] = (WmResourceTexture){
                            name, url, tpl.images[0].width,
                            tpl.images[0].height, tpl.images[0].format, origin
                        };
                        (*texture_count)++;
                    }
                }
            }
            wm_tpl_free(&tpl);
        }
    }
    for (size_t index = 0; index < archive.count && valid; ++index) {
        const WmU8Entry *item = &archive.entries[index];
        char stem[128];
        char basename[128];
        if (!wm_stem(item->path, ".brlyt", stem, basename)) continue;
        for (size_t previous = 0; previous < layouts_found; ++previous) {
            if (strcmp(layouts[previous].name, stem) == 0) {
                fprintf(stderr, "Ambiguous channel layout basename.\n");
                valid = false;
            }
        }
        if (!valid) break;
        char package[128];
        int package_size = snprintf(package, sizeof(package),
                                    "channel-%s-%s", channel_id, kind);
        int source_size = snprintf(source, sizeof(source),
            "%s/meta/%s.bin/%s", source_file, kind, item->path);
        char *json = NULL;
        size_t json_size = 0;
        if (package_size < 0 || package_size >= (int)sizeof(package) ||
            source_size < 0 || source_size >= (int)sizeof(source) ||
            !wm_brlyt_to_json_with_source(item->data, item->size,
                stem, package, source, textures, textures_found,
                animations, animations_found, &json, &json_size,
                error, sizeof(error))) {
            fprintf(stderr, "Channel BRLYT export: %s\n", error);
            valid = false;
            free(json);
            break;
        }
        int path_size = snprintf(relative, sizeof(relative),
                                  "channel-layouts/%s/%s/%s.json",
                                  channel_id, kind, stem);
        int full = snprintf(destination, sizeof(destination),
                            "%s/%s", output, relative);
        if (path_size < 0 || path_size >= (int)sizeof(relative) ||
            full < 0 || full >= (int)sizeof(destination) ||
            !wm_write_file(destination, json, json_size)) {
            valid = false;
        }
        free(json);
        if (!valid) break;
        strcpy(layouts[layouts_found].name, stem);
        strcpy(layouts[layouts_found].path, relative);
        layouts_found++;
        if (strcmp(stem, kind) == 0) strcpy(default_layout, relative);
    }
    if (valid) {
        qsort(layouts, layouts_found, sizeof(*layouts), wm_compare_layouts);
        *layout_paths = layouts;
        *layout_count = layouts_found;
        *animation_count = (unsigned)animations_found;
    } else {
        free(layouts);
    }
    for (size_t index = 0; index < textures_found; ++index) {
        free((char *)textures[index].name);
        free((char *)textures[index].url);
        free((char *)textures[index].source);
    }
    for (size_t index = 0; index < animations_found; ++index) {
        free((char *)animations[index].name);
    }
    free(textures);
    free(animations);
    wm_u8_free(&archive);
    free(decoded);
    return valid;
}

static void wm_free_channels(WmChannelList *channels)
{
    for (size_t index = 0; index < channels->count; ++index) {
        free(channels->items[index].layouts[0]);
        free(channels->items[index].layouts[1]);
    }
    free(channels->items);
    memset(channels, 0, sizeof(*channels));
}

static void wm_lower_title_id(char id[17], const char *high, const char *low)
{
    for (unsigned index = 0; index < 8; ++index) {
        id[index] = (char)tolower((unsigned char)high[index]);
        id[index + 8] = (char)tolower((unsigned char)low[index]);
    }
    id[16] = '\0';
}

static void wm_short_title_id(char short_id[5], const char *low)
{
    for (unsigned index = 0; index < 4; ++index) {
        char digits[3] = {low[index * 2], low[index * 2 + 1], '\0'};
        unsigned long value = strtoul(digits, NULL, 16);
        short_id[index] = value >= 32 && value <= 126 ? (char)value : '?';
    }
    short_id[4] = '\0';
}

static bool wm_export_title(const char *root, const char *output,
                            const char *high, const char *low,
                            unsigned language, WmChannelList *channels)
{
    char id[17];
    wm_lower_title_id(id, high, low);
    if (strcmp(id, "0000000100000002") == 0) return true;

    char tmd_path[WM_PATH_CAP];
    int length = snprintf(tmd_path, sizeof(tmd_path),
                          "%s/title/%s/%s/content/title.tmd", root, high, low);
    if (length < 0 || length >= (int)sizeof(tmd_path)) return false;
    if (!wm_regular_file(tmd_path)) return true;
    size_t tmd_size = 0;
    uint8_t *tmd = wm_read_file(tmd_path, 1024 * 1024, &tmd_size);
    WmTmdContent *records = NULL;
    size_t record_count = 0;
    unsigned version = 0;
    bool valid = tmd && wm_tmd_contents(tmd, tmd_size, id,
                                        &records, &record_count, &version);
    uint8_t tmd_digest[20] = {0};
    if (valid) {
        WmSha1 sha1;
        wm_sha1_init(&sha1);
        wm_sha1_update(&sha1, tmd, tmd_size);
        wm_sha1_final(&sha1, tmd_digest);
    }
    free(tmd);
    if (!valid) {
        fprintf(stderr, "Invalid TMD for title %s.\n", id);
        return false;
    }

    const WmTmdContent *selected = NULL;
    char selected_path[WM_PATH_CAP] = {0};
    for (size_t index = 0; index < record_count; ++index) {
        const WmTmdContent *record = &records[index];
        if ((record->type & 0x8000u) != 0) continue;
        char path[WM_PATH_CAP];
        length = snprintf(path, sizeof(path),
                          "%s/title/%s/%s/content/%08x.app",
                          root, high, low, record->id);
        if (length < 0 || length >= (int)sizeof(path)) {
            valid = false;
            break;
        }
        if (!wm_has_imet(path)) continue;
        if (selected) {
            fprintf(stderr, "Multiple active IMET contents for title %s.\n", id);
            valid = false;
            break;
        }
        selected = record;
        memcpy(selected_path, path, (size_t)length + 1);
    }
    if (!valid || !selected) {
        free(records);
        return valid;
    }

    uint8_t *content = NULL;
    size_t content_size = 0;
    if (!wm_validated_content(selected_path, selected,
                               &content, &content_size)) {
        fprintf(stderr, "TMD SHA-1 or content size mismatch for title %s.\n", id);
        free(records);
        return false;
    }
    WmMetadata metadata;
    WmU8Archive archive = {0};
    char error[160] = {0};
    valid = wm_metadata(content, content_size, language, &metadata) &&
            wm_u8_parse(content + metadata.archive_offset,
                        content_size - metadata.archive_offset,
                        &archive, error, sizeof(error));
    if (!valid) {
        fprintf(stderr, "Invalid IMET archive for title %s: %s\n", id, error);
        free(content);
        free(records);
        return false;
    }
    const WmU8Entry *icon = wm_u8_find(&archive, "meta/icon.bin");
    const WmU8Entry *banner = wm_u8_find(&archive, "meta/banner.bin");
    const WmU8Entry *sound = wm_u8_find(&archive, "meta/sound.bin");
    if (!icon && !banner) {
        wm_u8_free(&archive);
        free(content);
        free(records);
        return true;
    }

    WmChannelExport channel = {0};
    memcpy(channel.id, id, sizeof(channel.id));
    wm_short_title_id(channel.short_id, low);
    memcpy(channel.title, metadata.title, sizeof(channel.title));
    memcpy(channel.titles, metadata.titles, sizeof(channel.titles));
    channel.version = metadata.version;
    channel.tmd_version = version;
    wm_format_sha1(selected->sha1, channel.content_sha1);
    wm_format_sha1(tmd_digest, channel.tmd_sha1);
    channel.flags = metadata.flags;
    snprintf(channel.content_id, sizeof(channel.content_id), "%08x", selected->id);
    channel.preferred = true;
    if (strcmp(channel.short_id, "HAFA") == 0 ||
        strcmp(channel.short_id, "HAGA") == 0) {
        char counterpart[WM_PATH_CAP];
        length = snprintf(counterpart, sizeof(counterpart),
                          "%s/title/%s/%.6s45", root, high, low);
        if (length < 0 || length >= (int)sizeof(counterpart)) valid = false;
        else if (wm_directory(counterpart)) channel.preferred = false;
    }
    char source_file[WM_PATH_CAP];
    length = snprintf(source_file, sizeof(source_file),
                      "title/%s/%s/content/%08x.app",
                      high, low, selected->id);
    if (length < 0 || length >= (int)sizeof(source_file)) valid = false;
    if (valid) memcpy(channel.source_file, source_file, (size_t)length + 1);
    if (valid && icon) valid = wm_export_resource(
        icon, output, id, "icon", source_file,
        &channel.layouts[0], &channel.layout_count[0], channel.icon_layout,
        &channel.icon_textures, &channel.icon_animations);
    if (valid && banner) valid = wm_export_resource(
        banner, output, id, "banner", source_file,
        &channel.layouts[1], &channel.layout_count[1], channel.banner_layout,
        &channel.banner_textures, &channel.banner_animations);
    if (valid) valid = wm_export_channel_audio(sound, output, &channel);
    if (valid && channel.title[0] == '\0') {
        memcpy(channel.title, channel.short_id, sizeof(channel.short_id));
    }
    if (valid) valid = wm_append_channel(channels, &channel);
    if (!valid) {
        fprintf(stderr, "Could not export channel %s.\n", id);
        free(channel.layouts[0]);
        free(channel.layouts[1]);
    }
    wm_u8_free(&archive);
    free(content);
    free(records);
    return valid;
}

static bool wm_scan_titles(const char *root, const char *output,
                           unsigned language, WmChannelList *channels)
{
    char high_root[WM_PATH_CAP];
    int length = snprintf(high_root, sizeof(high_root), "%s/title", root);
    if (length < 0 || length >= (int)sizeof(high_root)) return false;
    DIR *highs = opendir(high_root);
    if (!highs) return false;
    bool valid = true;
    struct dirent *high;
    while (valid && (high = readdir(highs)) != NULL) {
        if (!wm_hex8(high->d_name)) continue;
        char low_root[WM_PATH_CAP];
        length = snprintf(low_root, sizeof(low_root),
                          "%s/%s", high_root, high->d_name);
        if (length < 0 || length >= (int)sizeof(low_root)) {
            valid = false;
            break;
        }
        if (!wm_directory(low_root)) continue;
        DIR *lows = opendir(low_root);
        if (!lows) {
            valid = false;
            break;
        }
        struct dirent *low;
        while (valid && (low = readdir(lows)) != NULL) {
            if (!wm_hex8(low->d_name)) continue;
            char title_root[WM_PATH_CAP];
            length = snprintf(title_root, sizeof(title_root),
                              "%s/%s", low_root, low->d_name);
            if (length < 0 || length >= (int)sizeof(title_root)) {
                valid = false;
                break;
            }
            if (!wm_directory(title_root)) continue;
            valid = wm_export_title(root, output, high->d_name,
                                    low->d_name, language, channels);
        }
        closedir(lows);
    }
    closedir(highs);
    return valid;
}

static int wm_compare_channels(const void *left, const void *right)
{
    const WmChannelExport *a = left;
    const WmChannelExport *b = right;
    return strcmp(a->id, b->id);
}

static void wm_json_string(FILE *stream, const char *value)
{
    fputc('"', stream);
    for (const unsigned char *current = (const unsigned char *)value;
         *current; ++current) {
        if (*current == '"' || *current == '\\') {
            fputc('\\', stream);
            fputc(*current, stream);
        } else if (*current < 0x20) {
            fprintf(stream, "\\u%04x", (unsigned)*current);
        } else {
            fputc(*current, stream);
        }
    }
    fputc('"', stream);
}

static void wm_json_nullable(FILE *stream, const char *value)
{
    if (value[0]) wm_json_string(stream, value);
    else fputs("null", stream);
}

static void wm_write_resource_json(FILE *stream, const char *kind,
                                   const WmChannelExport *channel,
                                   unsigned type)
{
    fputs("      \"", stream);
    fputs(kind, stream);
    fputs("\": {\"layout\": ", stream);
    wm_json_nullable(stream, type == 0 ? channel->icon_layout :
                                      channel->banner_layout);
    fputs(", \"layouts\": {", stream);
    for (size_t index = 0; index < channel->layout_count[type]; ++index) {
        if (index) fputs(", ", stream);
        wm_json_string(stream, channel->layouts[type][index].name);
        fputs(": ", stream);
        wm_json_string(stream, channel->layouts[type][index].path);
    }
    fprintf(stream, "}, \"textureCount\": %u, \"animationCount\": %u}",
            type == 0 ? channel->icon_textures : channel->banner_textures,
            type == 0 ? channel->icon_animations : channel->banner_animations);
}

static void wm_write_channel_json(FILE *stream, const WmChannelExport *channel)
{
    fputs("    {\"id\": ", stream);
    wm_json_string(stream, channel->id);
    fputs(", \"shortId\": ", stream);
    wm_json_string(stream, channel->short_id);
    fputs(", \"title\": ", stream);
    wm_json_string(stream, channel->title);
    fputs(", \"titles\": {", stream);
    for (unsigned index = 0; index < 10; ++index) {
        if (index) fputs(", ", stream);
        wm_json_string(stream, wm_languages[index]);
        fputs(": ", stream);
        wm_json_string(stream, channel->titles[index]);
    }
    fprintf(stream, "}, \"imetVersion\": %u, \"behavior\": "
            "{\"flags\": \"0x%08x\", \"iconModule\": %u, "
            "\"bannerModule\": %u, \"iconScript\": %u, "
            "\"bannerScript\": %u},\n",
            channel->version, channel->flags,
            channel->flags >> 28 & 15u, channel->flags >> 24 & 15u,
            channel->flags >> 20 & 15u, channel->flags >> 16 & 15u);
    fputs("      \"source\": {\"file\": ", stream);
    wm_json_string(stream, channel->source_file);
    fputs(", \"tmdVersion\": ", stream);
    fprintf(stream, "%u", channel->tmd_version);
    fputs(", \"contentId\": ", stream);
    wm_json_string(stream, channel->content_id);
    fputs(", \"contentSha1\": ", stream);
    wm_json_string(stream, channel->content_sha1);
    fputs(", \"tmdSha1\": ", stream);
    wm_json_string(stream, channel->tmd_sha1);
    fputs(", \"integrity\": \"TMD SHA-1 validated\"}, "
          "\"preferred\": ", stream);
    fputs(channel->preferred ? "true" : "false", stream);
    fputs(", \"iconLayout\": ", stream);
    wm_json_nullable(stream, channel->icon_layout);
    fputs(", \"bannerLayout\": ", stream);
    wm_json_nullable(stream, channel->banner_layout);
    if (channel->has_audio) {
        fprintf(stream,
                ", \"audio\": {\"src\": \"channel-audio/%s.wav\", "
                "\"sourceFormat\": \"BNS 1.0 DSP ADPCM\", "
                "\"sampleRate\": %u, \"channels\": %u, "
                "\"samples\": %u, \"loop\": %s, "
                "\"loopStart\": %.9g, \"loopEnd\": %.9g}",
                channel->id, channel->audio_rate, channel->audio_channels,
                channel->audio_frames,
                channel->audio_looping ? "true" : "false",
                (double)channel->audio_loop_start / channel->audio_rate,
                (double)channel->audio_loop_end / channel->audio_rate);
    }
    fputs(", \"resources\": {\n", stream);
    wm_write_resource_json(stream, "icon", channel, 0);
    fputs(",\n", stream);
    wm_write_resource_json(stream, "banner", channel, 1);
    fputs("}, \"warnings\": [", stream);
    bool has_warning = false;
    if (!channel->icon_layout[0]) {
        wm_json_string(stream, "Missing icon.brlyt");
        has_warning = true;
    }
    if (!channel->banner_layout[0]) {
        if (has_warning) fputs(", ", stream);
        wm_json_string(stream, "Missing banner.brlyt");
        has_warning = true;
    }
    if ((channel->flags & UINT32_C(0xffff0000)) != 0) {
        if (has_warning) fputs(", ", stream);
        wm_json_string(stream,
            "Native module/channel-script behavior is not executed by layout export.");
    }
    fputs("]}", stream);
}

static int wm_priority(const char *short_id)
{
    static const char *const ids[] = {"HACA", "HAYA", "HABA", "HAFE", "HAGE"};
    for (unsigned index = 0; index < 5; ++index) {
        if (strcmp(short_id, ids[index]) == 0) return (int)index;
    }
    return 5;
}

static bool wm_write_manifest(const char *output, const char *language,
                              WmChannelList *channels,
                              const WmSavedLayout *saved_layout)
{
    char path[WM_PATH_CAP];
    int length = snprintf(path, sizeof(path), "%s/channels.json.tmp", output);
    if (length < 0 || length >= (int)sizeof(path) ||
        !wm_output_parent(path) || !wm_output_target_safe(path)) return false;
    FILE *stream = fopen(path, "wb");
    if (!stream) return false;
    fprintf(stream, "{\n  \"schemaVersion\": 1,\n"
            "  \"source\": {\"kind\": \"local-decrypted-channel-content\"},\n"
            "  \"language\": ");
    wm_json_string(stream, language);
    fputs(",\n  \"channels\": [\n", stream);
    for (size_t index = 0; index < channels->count; ++index) {
        if (index) fputs(",\n", stream);
        wm_write_channel_json(stream, &channels->items[index]);
    }
    fputs("\n  ],\n  \"defaultOrder\": [", stream);
    bool first = true;
    if (saved_layout) {
        for (unsigned slot = 0; slot < WM_SAVED_CHANNEL_SLOTS; ++slot) {
            const char *id = saved_layout->slots[slot].id;
            for (size_t index = 0; id[0] && index < channels->count; ++index) {
                if (strcmp(channels->items[index].id, id) != 0) continue;
                if (!first) fputs(", ", stream);
                wm_json_string(stream, id);
                first = false;
                break;
            }
        }
    } else {
        for (int priority = 0; priority <= 5; ++priority) {
            for (size_t index = 0; index < channels->count; ++index) {
                WmChannelExport *channel = &channels->items[index];
                if (!channel->preferred ||
                    wm_priority(channel->short_id) != priority) continue;
                if (!first) fputs(", ", stream);
                wm_json_string(stream, channel->id);
                first = false;
            }
        }
    }
    fputs("],\n  \"savedLayout\": null,\n"
          "  \"notes\": ["
          "\"Default order places core channels first; a checksum-validated "
          "iplsave.bin supplies installed slot placement when present.\", "
          "\"Native module and channel-script programs are not executed.\"]\n}"
          "\n", stream);
    bool valid = !ferror(stream);
    if (fclose(stream) != 0) valid = false;
    if (!valid) {
        remove(path);
        return false;
    }
    char final[WM_PATH_CAP];
    length = snprintf(final, sizeof(final), "%s/channels.json", output);
    if (length < 0 || length >= (int)sizeof(final) ||
        rename(path, final) != 0) {
        remove(path);
        return false;
    }
    return true;
}

static bool wm_copy_saved_layout(const char *root, const char *output,
                                 WmSavedLayout *parsed, bool *present)
{
    *present = false;
    char source[WM_PATH_CAP];
    char destination[WM_PATH_CAP];
    int one = snprintf(source, sizeof(source),
        "%s/title/00000001/00000002/data/iplsave.bin", root);
    int two = snprintf(destination, sizeof(destination),
        "%s/iplsave.bin", output);
    if (one < 0 || one >= (int)sizeof(source) ||
        two < 0 || two >= (int)sizeof(destination)) return false;
    if (!wm_regular_file(source)) {
        remove(destination);
        return true;
    }
    size_t size = 0;
    uint8_t *data = wm_read_file(source, WM_SAVED_LAYOUT_BYTES, &size);
    char error[160] = {0};
    if (!data || !wm_saved_layout_parse(data, size, parsed,
                                         error, sizeof(error))) {
        fprintf(stderr, "Ignoring invalid saved channel placement: %s\n", error);
        free(data);
        remove(destination);
        return true;
    }
    bool valid = wm_write_file(destination, data, size);
    if (valid) *present = true;
    free(data);
    return valid;
}

int main(int argc, char **argv)
{
    if (argc < 3 || argc > 4) {
        fprintf(stderr,
            "Usage: wm-channel-export EXTRACTED_ROOT OUTPUT_ROOT [LANGUAGE]\n"
            "LANGUAGE: JPN, ENG, GER, FRA, SPA, ITA, NED, CHN, CHT, KOR.\n");
        return 2;
    }
    unsigned language = 1;
    if (argc == 4) {
        for (unsigned index = 0; index < 10; ++index) {
            if (strcmp(argv[3], wm_languages[index]) == 0) language = index;
        }
        if (strcmp(argv[3], wm_languages[language]) != 0) {
            fprintf(stderr, "Unsupported channel language.\n");
            return 2;
        }
    }
    if (!wm_make_directories(argv[2])) {
        fprintf(stderr, "Could not create channel output directory.\n");
        return 1;
    }
    WmChannelList channels = {0};
    bool valid = wm_scan_titles(argv[1], argv[2], language, &channels);
    if (valid) {
        qsort(channels.items, channels.count,
              sizeof(*channels.items), wm_compare_channels);
        for (size_t index = 1; index < channels.count; ++index) {
            if (strcmp(channels.items[index - 1].id,
                       channels.items[index].id) == 0) valid = false;
        }
    }
    if (valid && channels.count == 0) {
        fprintf(stderr, "No installed channel metadata archives found.\n");
        valid = false;
    }
    WmSavedLayout saved_layout = {0};
    bool has_saved_layout = false;
    if (valid) valid = wm_copy_saved_layout(argv[1], argv[2],
                                             &saved_layout, &has_saved_layout);
    if (valid) valid = wm_write_manifest(argv[2], wm_languages[language],
                                         &channels,
                                         has_saved_layout ? &saved_layout : NULL);
    if (valid) fprintf(stdout, "Exported %zu installed channel catalogs.\n",
                       channels.count);
    else fprintf(stderr, "Channel export failed.\n");
    wm_free_channels(&channels);
    return valid ? 0 : 1;
}
