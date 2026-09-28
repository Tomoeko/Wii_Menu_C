#define _POSIX_C_SOURCE 200809L

#include "export_internal.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

static void wm_json_string(FILE *stream, const char *value) {
    fputc('"', stream);
    for (const unsigned char *current = (const unsigned char *)value; *current;
         ++current) {
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

static void wm_json_nullable(FILE *stream, const char *value) {
    if (value[0])
        wm_json_string(stream, value);
    else
        fputs("null", stream);
}

static void wm_write_resource_json(FILE *stream, const char *kind,
                                   const WmChannelExport *channel, unsigned type) {
    fputs("      \"", stream);
    fputs(kind, stream);
    fputs("\": {\"layout\": ", stream);
    wm_json_nullable(stream, type == 0 ? channel->icon_layout : channel->banner_layout);
    fputs(", \"layouts\": {", stream);
    for (size_t index = 0; index < channel->layout_count[type]; ++index) {
        if (index)
            fputs(", ", stream);
        wm_json_string(stream, channel->layouts[type][index].name);
        fputs(": ", stream);
        wm_json_string(stream, channel->layouts[type][index].path);
    }
    fprintf(stream, "}, \"textureCount\": %u, \"animationCount\": %u}",
            type == 0 ? channel->icon_textures : channel->banner_textures,
            type == 0 ? channel->icon_animations : channel->banner_animations);
}

static void wm_write_channel_json(FILE *stream, const WmChannelExport *channel) {
    fputs("    {\"id\": ", stream);
    wm_json_string(stream, channel->id);
    fputs(", \"shortId\": ", stream);
    wm_json_string(stream, channel->short_id);
    fputs(", \"title\": ", stream);
    wm_json_string(stream, channel->title);
    fputs(", \"titles\": {", stream);
    for (unsigned index = 0; index < 10; ++index) {
        if (index)
            fputs(", ", stream);
        wm_json_string(stream, wm_languages[index]);
        fputs(": ", stream);
        wm_json_string(stream, channel->titles[index]);
    }
    fprintf(stream,
            "}, \"imetVersion\": %u, \"behavior\": "
            "{\"flags\": \"0x%08x\", \"iconModule\": %u, "
            "\"bannerModule\": %u, \"iconScript\": %u, "
            "\"bannerScript\": %u},\n",
            channel->version, channel->flags, channel->flags >> 28 & 15u,
            channel->flags >> 24 & 15u, channel->flags >> 20 & 15u,
            channel->flags >> 16 & 15u);
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
          "\"preferred\": ",
          stream);
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
                channel->audio_frames, channel->audio_looping ? "true" : "false",
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
        if (has_warning)
            fputs(", ", stream);
        wm_json_string(stream, "Missing banner.brlyt");
        has_warning = true;
    }
    if ((channel->flags & UINT32_C(0xffff0000)) != 0) {
        if (has_warning)
            fputs(", ", stream);
        wm_json_string(
            stream,
            "Native module/channel-script behavior is not executed by layout export.");
    }
    fputs("]}", stream);
}

static int wm_priority(const char *short_id) {
    static const char *const ids[] = {"HACA", "HAYA", "HABA", "HAFE", "HAGE"};
    for (unsigned index = 0; index < 5; ++index) {
        if (strcmp(short_id, ids[index]) == 0)
            return (int)index;
    }
    return 5;
}

bool wm_write_manifest(const char *output, const char *language,
                       WmChannelList *channels, const WmSavedLayout *saved_layout) {
    char path[WM_PATH_CAP];
    int length = snprintf(path, sizeof(path), "%s/.channels.json-XXXXXX", output);
    if (length < 0 || length >= (int)sizeof(path) || !wm_output_parent(path))
        return false;
    /* Exclusive creation avoids following a stale temporary symlink. */
    int descriptor = mkstemp(path);
    if (descriptor < 0)
        return false;
    FILE *stream = fdopen(descriptor, "wb");
    if (!stream) {
        close(descriptor);
        unlink(path);
        return false;
    }
    fprintf(stream, "{\n  \"schemaVersion\": 1,\n"
                    "  \"source\": {\"kind\": \"local-decrypted-channel-content\"},\n"
                    "  \"language\": ");
    wm_json_string(stream, language);
    fputs(",\n  \"channels\": [\n", stream);
    for (size_t index = 0; index < channels->count; ++index) {
        if (index)
            fputs(",\n", stream);
        wm_write_channel_json(stream, &channels->items[index]);
    }
    fputs("\n  ],\n  \"defaultOrder\": [", stream);
    bool first = true;
    if (saved_layout) {
        for (unsigned slot = 0; slot < WM_SAVED_CHANNEL_SLOTS; ++slot) {
            const char *id = saved_layout->slots[slot].id;
            for (size_t index = 0; id[0] && index < channels->count; ++index) {
                if (strcmp(channels->items[index].id, id) != 0)
                    continue;
                if (!first)
                    fputs(", ", stream);
                wm_json_string(stream, id);
                first = false;
                break;
            }
        }
    } else {
        for (int priority = 0; priority <= 5; ++priority) {
            for (size_t index = 0; index < channels->count; ++index) {
                WmChannelExport *channel = &channels->items[index];
                if (!channel->preferred || wm_priority(channel->short_id) != priority)
                    continue;
                if (!first)
                    fputs(", ", stream);
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
          "\n",
          stream);
    bool valid = !ferror(stream);
    if (fclose(stream) != 0)
        valid = false;
    if (!valid) {
        remove(path);
        return false;
    }
    char final[WM_PATH_CAP];
    length = snprintf(final, sizeof(final), "%s/channels.json", output);
    if (length < 0 || length >= (int)sizeof(final) || rename(path, final) != 0) {
        remove(path);
        return false;
    }
    return true;
}
