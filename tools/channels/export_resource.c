#include "export_internal.h"
#include "md5.h"

#include "wii_menu/audio/audio_wave.h"
#include "wii_menu/render/image.h"
#include "wii_menu/resources/resource_ash.h"
#include "wii_menu/resources/resource_audio.h"
#include "wii_menu/resources/resource_layout.h"
#include "wii_menu/resources/resource_tpl.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static bool wm_ends_with(const char *path, const char *extension) {
    size_t path_size = strlen(path);
    size_t extension_size = strlen(extension);
    return path_size >= extension_size &&
           strcmp(path + path_size - extension_size, extension) == 0;
}

static bool wm_stem(const char *path, const char *extension, char stem[128],
                    char basename[128]) {
    if (!wm_ends_with(path, extension))
        return false;
    const char *name = strrchr(path, '/');
    name = name ? name + 1 : path;
    size_t length = strlen(name);
    size_t suffix = strlen(extension);
    if (length <= suffix || length >= 128)
        return false;
    for (size_t index = 0; index < length - suffix; ++index) {
        char c = name[index];
        if (!((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
              (c >= '0' && c <= '9') || c == '_' || c == '-' || c == '.' || c == '+')) {
            return false;
        }
    }
    memcpy(stem, name, length - suffix);
    stem[length - suffix] = '\0';
    memcpy(basename, name, length + 1);
    return true;
}

static char *wm_copy(const char *source) {
    size_t length = strlen(source) + 1;
    char *copy = malloc(length);
    if (copy)
        memcpy(copy, source, length);
    return copy;
}

static bool wm_lz77_decode(const uint8_t *data, size_t size, uint8_t **output,
                           size_t *output_size) {
    *output = NULL;
    *output_size = 0;
    if (size < 4 || data[0] != 0x10)
        return false;
    size_t length = (size_t)data[1] | ((size_t)data[2] << 8) | ((size_t)data[3] << 16);
    if (length > WM_MAX_CONTENT)
        return false;
    if (length == 0) {
        for (size_t index = 4; index < size; ++index) {
            if (data[index] != 0)
                return false;
        }
    }
    uint8_t *decoded = malloc(length ? length : 1);
    if (!decoded)
        return false;
    size_t source = 4;
    size_t produced = 0;
    while (produced < length) {
        if (source >= size)
            break;
        uint8_t flags = data[source++];
        for (int bit = 7; bit >= 0 && produced < length; --bit) {
            if ((flags & (1u << bit)) == 0) {
                if (source >= size)
                    goto fail;
                decoded[produced++] = data[source++];
            } else {
                if (!wm_fits(size, source, 2))
                    goto fail;
                uint16_t word = wm_be16(data + source);
                source += 2;
                size_t count = (word >> 12) + 3;
                size_t distance = (word & 0x0fff) + 1;
                if (distance > produced)
                    goto fail;
                for (size_t index = 0; index < count && produced < length; ++index) {
                    decoded[produced] = decoded[produced - distance];
                    produced++;
                }
            }
        }
    }
    if (produced != length)
        goto fail;
    *output = decoded;
    *output_size = length;
    return true;
fail:
    free(decoded);
    return false;
}

static bool wm_unwrap_resource(const uint8_t *data, size_t size, uint8_t **output,
                               size_t *output_size) {
    uint8_t *current = malloc(size ? size : 1);
    if (!current)
        return false;
    memcpy(current, data, size);
    for (unsigned layer = 0; layer < 4; ++layer) {
        if (size >= 4 && memcmp(current, "IMD5", 4) == 0) {
            if (size < 32)
                break;
            size_t length = wm_be32(current + 4);
            if (!wm_fits(size, 32, length))
                break;
            WmMd5 md5;
            uint8_t digest[16];
            wm_md5_init(&md5);
            wm_md5_update(&md5, current + 32, length);
            wm_md5_final(&md5, digest);
            if (memcmp(digest, current + 16, 16) != 0)
                break;
            memmove(current, current + 32, length);
            size = length;
        } else if (size >= 4 && memcmp(current, "LZ77", 4) == 0) {
            uint8_t *decoded = NULL;
            size_t decoded_size = 0;
            if (!wm_lz77_decode(current + 4, size - 4, &decoded, &decoded_size))
                break;
            free(current);
            current = decoded;
            size = decoded_size;
        } else if (size >= 4 && memcmp(current, "ASH0", 4) == 0) {
            uint8_t *decoded = NULL;
            size_t decoded_size = 0;
            char error[160];
            if (!wm_ash_decode(current, size, &decoded, &decoded_size, error,
                               sizeof(error)))
                break;
            free(current);
            current = decoded;
            size = decoded_size;
        } else if (size > 0 && current[0] == 0x10) {
            uint8_t *decoded = NULL;
            size_t decoded_size = 0;
            if (!wm_lz77_decode(current, size, &decoded, &decoded_size))
                break;
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

bool wm_export_channel_audio(const WmU8Entry *entry, const char *output,
                             WmChannelExport *channel) {
    if (!entry)
        return true;
    uint8_t *decoded = NULL;
    size_t decoded_size = 0;
    WmAudioPcm pcm = {0};
    char error[160] = {0};
    if (!wm_unwrap_resource(entry->data, entry->size, &decoded, &decoded_size)) {
        fprintf(stderr, "Could not unwrap channel sound resource.\n");
        return false;
    }
    bool valid = wm_bns_decode(decoded, decoded_size, &pcm, error, sizeof(error));
    if (!valid) {
        fprintf(stderr, "Channel sound decode: %s\n", error);
        free(decoded);
        return false;
    }
    char directory[WM_PATH_CAP], destination[WM_PATH_CAP];
    int first = snprintf(directory, sizeof(directory), "%s/channel-audio", output);
    int second =
        snprintf(destination, sizeof(destination), "%s/%s.wav", directory, channel->id);
    valid = first > 0 && first < (int)sizeof(directory) && second > 0 &&
            second < (int)sizeof(destination) && wm_output_parent(destination) &&
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

static int wm_compare_layouts(const void *left, const void *right) {
    const WmLayoutPath *a = left;
    const WmLayoutPath *b = right;
    return strcmp(a->name, b->name);
}

typedef struct ChannelResourceExport {
    const char *output;
    const char *channel_id;
    const char *kind;
    const char *source_file;
    char *default_layout;
    unsigned *texture_count;
    WmResourceTexture *textures;
    WmResourceAnimation *animations;
    WmLayoutPath *layouts;
    size_t textures_found;
    size_t animations_found;
    size_t layouts_found;
} ChannelResourceExport;

/* The export owns metadata strings; resource bytes stay borrowed from the archive. */
static void release_resource_export(ChannelResourceExport *export) {
    for (size_t index = 0; index < export->textures_found; index++) {
        free((char *)export->textures[index].name);
        free((char *)export->textures[index].url);
        free((char *)export->textures[index].source);
    }
    for (size_t index = 0; index < export->animations_found; index++)
        free((char *)export->animations[index].name);
    free(export->textures);
    free(export->animations);
    free(export->layouts);
}

static bool append_animation(ChannelResourceExport *export, const WmU8Entry *item,
                             const char *stem) {
    char *name = wm_copy(stem);
    if (!name)
        return false;
    export->animations[export->animations_found++] =
        (WmResourceAnimation){name, item->data, item->size};
    return true;
}

static bool export_texture_images(const ChannelResourceExport *export, const WmTpl *tpl,
                                  const char *stem) {
    for (size_t image = 0; image < tpl->count; image++) {
        char relative[WM_PATH_CAP];
        char destination[WM_PATH_CAP];
        int length = snprintf(relative, sizeof(relative),
                              image == 0 ? "channel-layouts/%s/%s/textures/%s.wmra"
                                         : "channel-layouts/%s/%s/textures/%s-%zu.wmra",
                              export->channel_id, export->kind, stem, image);
        int full = snprintf(destination, sizeof(destination), "%s/%s", export->output,
                            relative);
        WmImage converted = {tpl->images[image].width, tpl->images[image].height,
                             tpl->images[image].rgba};
        if (length < 0 || length >= (int)sizeof(relative) || full < 0 ||
            full >= (int)sizeof(destination) || !wm_output_parent(destination) ||
            !wm_output_target_safe(destination) ||
            !wm_image_write(destination, &converted))
            return false;
    }
    return true;
}

static bool append_texture(ChannelResourceExport *export, const WmU8Entry *item,
                           const WmTpl *tpl, const char *stem, const char *basename) {
    char relative[WM_PATH_CAP];
    char source[WM_PATH_CAP];
    int url_size =
        snprintf(relative, sizeof(relative), "channel-layouts/%s/%s/textures/%s.png",
                 export->channel_id, export->kind, stem);
    int source_size = snprintf(source, sizeof(source), "%s/meta/%s.bin/%s",
                               export->source_file, export->kind, item->path);
    if (url_size < 0 || url_size >= (int)sizeof(relative) || source_size < 0 ||
        source_size >= (int)sizeof(source))
        return false;

    char *name = wm_copy(basename);
    char *url = wm_copy(relative);
    char *origin = wm_copy(source);
    if (!name || !url || !origin) {
        free(name);
        free(url);
        free(origin);
        return false;
    }
    export->textures[export->textures_found++] =
        (WmResourceTexture){.name = name,
                            .url = url,
                            .width = tpl->images[0].width,
                            .height = tpl->images[0].height,
                            .format = tpl->images[0].format,
                            .source = origin};
    (*export->texture_count)++;
    return true;
}

static bool export_texture(ChannelResourceExport *export, const WmU8Entry *item,
                           const char *stem, const char *basename) {
    for (size_t previous = 0; previous < export->textures_found; previous++) {
        if (strcmp(export->textures[previous].name, basename) == 0) {
            fprintf(stderr, "Ambiguous channel texture basename.\n");
            return false;
        }
    }
    WmTpl tpl = {0};
    char error[160] = {0};
    if (!wm_tpl_decode(item->data, item->size, &tpl, error, sizeof(error))) {
        fprintf(stderr, "Channel TPL decode: %s\n", error);
        return false;
    }
    bool valid = export_texture_images(export, &tpl, stem) &&
                 (tpl.count == 0 || append_texture(export, item, &tpl, stem, basename));
    wm_tpl_free(&tpl);
    return valid;
}

static bool collect_resources(ChannelResourceExport *export,
                              const WmU8Archive *archive) {
    for (size_t index = 0; index < archive->count; index++) {
        const WmU8Entry *item = &archive->entries[index];
        char stem[128];
        char basename[128];
        if (wm_stem(item->path, ".brlan", stem, basename)) {
            if (!append_animation(export, item, stem))
                return false;
        } else if (wm_stem(item->path, ".tpl", stem, basename)) {
            if (!export_texture(export, item, stem, basename))
                return false;
        }
    }
    return true;
}

static bool export_layout(ChannelResourceExport *export, const WmU8Entry *item,
                          const char *stem) {
    for (size_t previous = 0; previous < export->layouts_found; previous++) {
        if (strcmp(export->layouts[previous].name, stem) == 0) {
            fprintf(stderr, "Ambiguous channel layout basename.\n");
            return false;
        }
    }
    char package[128];
    char source[WM_PATH_CAP];
    int package_size = snprintf(package, sizeof(package), "channel-%s-%s",
                                export->channel_id, export->kind);
    int source_size = snprintf(source, sizeof(source), "%s/meta/%s.bin/%s",
                               export->source_file, export->kind, item->path);
    char *json = NULL;
    size_t json_size = 0;
    char error[160] = {0};
    if (package_size < 0 || package_size >= (int)sizeof(package) || source_size < 0 ||
        source_size >= (int)sizeof(source) ||
        !wm_brlyt_to_json_with_source(item->data, item->size, stem, package, source,
                                      export->textures, export->textures_found,
                                      export->animations, export->animations_found,
                                      &json, &json_size, error, sizeof(error))) {
        fprintf(stderr, "Channel BRLYT export: %s\n", error);
        free(json);
        return false;
    }
    char relative[WM_PATH_CAP];
    char destination[WM_PATH_CAP];
    int path_size =
        snprintf(relative, sizeof(relative), "channel-layouts/%s/%s/%s.json",
                 export->channel_id, export->kind, stem);
    int full =
        snprintf(destination, sizeof(destination), "%s/%s", export->output, relative);
    bool valid = path_size >= 0 && path_size < (int)sizeof(relative) && full >= 0 &&
                 full < (int)sizeof(destination) &&
                 wm_output_write_file(destination, json, json_size);
    free(json);
    if (!valid)
        return false;
    WmLayoutPath *layout = &export->layouts[export->layouts_found++];
    strcpy(layout->name, stem);
    strcpy(layout->path, relative);
    if (strcmp(stem, export->kind) == 0)
        strcpy(export->default_layout, relative);
    return true;
}

static bool export_layouts(ChannelResourceExport *export, const WmU8Archive *archive) {
    for (size_t index = 0; index < archive->count; index++) {
        const WmU8Entry *item = &archive->entries[index];
        char stem[128];
        char basename[128];
        if (wm_stem(item->path, ".brlyt", stem, basename) &&
            !export_layout(export, item, stem))
            return false;
    }
    return true;
}

bool wm_export_resource(const WmU8Entry *entry, const char *output,
                        const char *channel_id, const char *kind,
                        const char *source_file, WmLayoutPath **layout_paths,
                        size_t *layout_count, char default_layout[WM_PATH_CAP],
                        unsigned *texture_count, unsigned *animation_count) {
    uint8_t *decoded = NULL;
    size_t decoded_size = 0;
    if (!wm_unwrap_resource(entry->data, entry->size, &decoded, &decoded_size)) {
        fprintf(stderr, "Could not validate a channel resource envelope.\n");
        return false;
    }
    WmU8Archive archive = {0};
    ChannelResourceExport export = {.output = output,
                                    .channel_id = channel_id,
                                    .kind = kind,
                                    .source_file = source_file,
                                    .default_layout = default_layout,
                                    .texture_count = texture_count};
    bool valid = false;
    char error[160] = {0};
    if (!wm_u8_parse(decoded, decoded_size, &archive, error, sizeof(error))) {
        fprintf(stderr, "Channel resource archive: %s\n", error);
        goto release_export;
    }
    export.textures = calloc(archive.count + 1, sizeof(*export.textures));
    export.animations = calloc(archive.count + 1, sizeof(*export.animations));
    export.layouts = calloc(archive.count + 1, sizeof(*export.layouts));
    if (!export.textures || !export.animations || !export.layouts)
        goto release_export;
    if (!collect_resources(&export, &archive) || !export_layouts(&export, &archive))
        goto release_export;

    qsort(export.layouts, export.layouts_found, sizeof(*export.layouts),
          wm_compare_layouts);
    *layout_paths = export.layouts; /* Ownership passes to the channel record. */
    export.layouts = NULL;
    *layout_count = export.layouts_found;
    *animation_count = (unsigned)export.animations_found;
    valid = true;

release_export:
    release_resource_export(&export);
    wm_u8_free(&archive);
    free(decoded);
    return valid;
}
