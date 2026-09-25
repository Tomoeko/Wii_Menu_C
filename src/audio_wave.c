#include "wii_menu/audio_wave.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

enum {
    WM_WAV_MAX_BYTES = 128 * 1024 * 1024,
    WM_WAV_MAX_FRAMES = 20000000
};

static void set_error(char *error, size_t capacity, const char *message)
{
    if (error && capacity) snprintf(error, capacity, "%s", message);
}

static uint16_t le16(const uint8_t *bytes)
{
    return (uint16_t)(bytes[0] | ((uint16_t)bytes[1] << 8));
}

static uint32_t le32(const uint8_t *bytes)
{
    return (uint32_t)bytes[0] | ((uint32_t)bytes[1] << 8) |
           ((uint32_t)bytes[2] << 16) | ((uint32_t)bytes[3] << 24);
}

static void put16(uint8_t *bytes, uint16_t value)
{
    bytes[0] = (uint8_t)value;
    bytes[1] = (uint8_t)(value >> 8);
}

static void put32(uint8_t *bytes, uint32_t value)
{
    bytes[0] = (uint8_t)value;
    bytes[1] = (uint8_t)(value >> 8);
    bytes[2] = (uint8_t)(value >> 16);
    bytes[3] = (uint8_t)(value >> 24);
}

static bool write_bytes(FILE *file, const void *data, size_t size)
{
    return fwrite(data, 1, size, file) == size;
}

bool wm_audio_wav_write(const char *path, const WmAudioPcm *audio,
                        char *error, size_t error_capacity)
{
    set_error(error, error_capacity, "");
    if (!path || !audio || !audio->samples ||
        (audio->channels != 1 && audio->channels != 2) ||
        !audio->sample_rate || audio->sample_rate > 192000 ||
        !audio->frame_count || audio->frame_count > WM_WAV_MAX_FRAMES ||
        (audio->looping &&
         (audio->loop_start >= audio->loop_end ||
          audio->loop_end > audio->frame_count))) {
        set_error(error, error_capacity, "Invalid PCM data for WAV export.");
        return false;
    }
    size_t payload = (size_t)audio->frame_count * audio->channels * 2;
    size_t extra = audio->looping ? 68 : 0;
    if (payload > UINT32_MAX - 36 - extra || payload > WM_WAV_MAX_BYTES) {
        set_error(error, error_capacity, "WAV output is too large.");
        return false;
    }
    FILE *file = fopen(path, "wb");
    if (!file) {
        set_error(error, error_capacity, "Could not create WAV output.");
        return false;
    }
    uint8_t header[44] = {0};
    memcpy(header, "RIFF", 4);
    put32(header + 4, (uint32_t)(36 + payload + extra));
    memcpy(header + 8, "WAVEfmt ", 8);
    put32(header + 16, 16);
    put16(header + 20, 1);
    put16(header + 22, audio->channels);
    put32(header + 24, audio->sample_rate);
    put32(header + 28, audio->sample_rate * audio->channels * 2);
    put16(header + 32, (uint16_t)(audio->channels * 2));
    put16(header + 34, 16);
    memcpy(header + 36, "data", 4);
    put32(header + 40, (uint32_t)payload);
    bool valid = write_bytes(file, header, sizeof(header));
    uint8_t buffer[4096];
    size_t values = (size_t)audio->frame_count * audio->channels;
    for (size_t position = 0; position < values && valid;) {
        size_t count = values - position;
        if (count > sizeof(buffer) / 2) count = sizeof(buffer) / 2;
        for (size_t index = 0; index < count; index++) {
            put16(buffer + index * 2,
                  (uint16_t)audio->samples[position + index]);
        }
        valid = write_bytes(file, buffer, count * 2);
        position += count;
    }
    if (audio->looping && valid) {
        uint8_t loop[68] = {0};
        memcpy(loop, "smpl", 4);
        put32(loop + 4, 60);
        put32(loop + 36, 1); /* One forward loop. */
        put32(loop + 52, audio->loop_start);
        put32(loop + 56, audio->loop_end - 1); /* RIFF end is inclusive. */
        valid = write_bytes(file, loop, sizeof(loop));
    }
    if (fclose(file) != 0) valid = false;
    if (!valid) {
        remove(path);
        set_error(error, error_capacity, "Could not write WAV output.");
    }
    return valid;
}

bool wm_audio_wav_read(const char *path, WmAudioPcm *audio,
                       char *error, size_t error_capacity)
{
    set_error(error, error_capacity, "");
    if (!path || !audio) return false;
    *audio = (WmAudioPcm){0};
    FILE *file = fopen(path, "rb");
    if (!file) {
        set_error(error, error_capacity, "Could not open WAV input.");
        return false;
    }
    if (fseek(file, 0, SEEK_END) != 0) { fclose(file); return false; }
    long length = ftell(file);
    if (length < 44 || (unsigned long)length > WM_WAV_MAX_BYTES ||
        fseek(file, 0, SEEK_SET) != 0) { fclose(file); return false; }
    size_t size = (size_t)length;
    uint8_t *bytes = malloc(size);
    if (!bytes) { fclose(file); return false; }
    bool valid = fread(bytes, 1, size, file) == size;
    fclose(file);
    if (!valid || memcmp(bytes, "RIFF", 4) != 0 ||
        memcmp(bytes + 8, "WAVE", 4) != 0 ||
        le32(bytes + 4) > size - 8 || le32(bytes + 4) < 36) {
        free(bytes);
        set_error(error, error_capacity, "Invalid RIFF/WAVE header.");
        return false;
    }
    size_t end = 8 + le32(bytes + 4);
    const uint8_t *format = NULL, *pcm_data = NULL, *loop = NULL;
    size_t format_size = 0, pcm_size = 0, loop_size = 0;
    for (size_t offset = 12; offset + 8 <= end;) {
        size_t part_size = le32(bytes + offset + 4);
        size_t payload = offset + 8;
        if (part_size > end - payload) { valid = false; break; }
        if (memcmp(bytes + offset, "fmt ", 4) == 0 && !format) {
            format = bytes + payload; format_size = part_size;
        } else if (memcmp(bytes + offset, "data", 4) == 0 && !pcm_data) {
            pcm_data = bytes + payload; pcm_size = part_size;
        } else if (memcmp(bytes + offset, "smpl", 4) == 0 && !loop) {
            loop = bytes + payload; loop_size = part_size;
        }
        offset = payload + part_size + (part_size & 1);
        if (offset > end) { valid = false; break; }
    }
    if (!valid || !format || format_size < 16 || !pcm_data ||
        le16(format) != 1 || (le16(format + 2) != 1 && le16(format + 2) != 2) ||
        le16(format + 14) != 16 || le32(format + 4) == 0 ||
        le32(format + 4) > 192000 ||
        le16(format + 12) != le16(format + 2) * 2 ||
        le32(format + 8) != le32(format + 4) * le16(format + 12) ||
        pcm_size % le16(format + 12) != 0) {
        free(bytes);
        set_error(error, error_capacity, "Unsupported or invalid PCM16 WAV.");
        return false;
    }
    uint16_t channels = le16(format + 2);
    size_t frames = pcm_size / (channels * 2);
    if (!frames || frames > WM_WAV_MAX_FRAMES) {
        free(bytes);
        set_error(error, error_capacity, "Invalid WAV frame count.");
        return false;
    }
    int16_t *samples = malloc(pcm_size);
    if (!samples) { free(bytes); return false; }
    for (size_t index = 0; index < frames * channels; index++) {
        samples[index] = (int16_t)le16(pcm_data + index * 2);
    }
    audio->samples = samples;
    audio->channels = (uint8_t)channels;
    audio->sample_rate = le32(format + 4);
    audio->frame_count = (uint32_t)frames;
    if (loop && loop_size >= 60 && le32(loop + 28) > 0 &&
        le32(loop + 40) == 0) {
        uint32_t start = le32(loop + 44);
        uint32_t last = le32(loop + 48);
        if (start < frames && last >= start && last < frames) {
            audio->looping = true;
            audio->loop_start = start;
            audio->loop_end = last + 1;
        }
    }
    free(bytes);
    return true;
}
