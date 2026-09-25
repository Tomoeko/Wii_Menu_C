#include "wii_menu/resource_audio.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

enum {
    WM_AUDIO_MAX_RESOURCE_BYTES = 128 * 1024 * 1024,
    WM_AUDIO_MAX_FRAMES = 20000000
};

static void audio_error(char *error, size_t capacity, const char *message) {
    if (error && capacity) snprintf(error, capacity, "%s", message);
}

static bool range_fits(size_t size, size_t offset, size_t length) {
    return offset <= size && length <= size - offset;
}

static uint16_t be16(const uint8_t *bytes) {
    return (uint16_t)(((uint16_t)bytes[0] << 8) | bytes[1]);
}

static uint32_t be32(const uint8_t *bytes) {
    return ((uint32_t)bytes[0] << 24) | ((uint32_t)bytes[1] << 16) |
           ((uint32_t)bytes[2] << 8) | bytes[3];
}

static int16_t signed_be16(const uint8_t *bytes) {
    uint16_t bits = be16(bytes);
    int32_t value = bits < 0x8000u ? bits : (int32_t)bits - 0x10000;
    return (int16_t)value;
}

static int64_t floor_div_2048(int64_t value) {
    return value >= 0 ? value / 2048 : -((-value + 2047) / 2048);
}

static bool dsp_decode_channel(const uint8_t *data, size_t size,
                               uint32_t sample_count,
                               const int16_t coefficients[16],
                               int16_t history1, int16_t history2,
                               int16_t *output, size_t stride,
                               char *error, size_t error_capacity) {
    if (!data || !coefficients || !output || !sample_count ||
        sample_count > WM_AUDIO_MAX_FRAMES) {
        audio_error(error, error_capacity, "Invalid DSP ADPCM input.");
        return false;
    }
    size_t blocks = ((size_t)sample_count + 13) / 14;
    if (blocks > SIZE_MAX / 8 || !range_fits(size, 0, blocks * 8)) {
        audio_error(error, error_capacity, "Truncated DSP ADPCM frames.");
        return false;
    }
    int32_t older = history2, previous = history1;
    uint32_t produced = 0;
    for (size_t block = 0; block < blocks; block++) {
        const uint8_t *frame = data + block * 8;
        unsigned predictor = frame[0] >> 4;
        unsigned scale = 1u << (frame[0] & 15u);
        if (predictor > 7) {
            audio_error(error, error_capacity, "Invalid DSP ADPCM predictor.");
            return false;
        }
        int32_t first = coefficients[predictor * 2];
        int32_t second = coefficients[predictor * 2 + 1];
        for (size_t byte = 1; byte < 8 && produced < sample_count; byte++) {
            for (int half = 0; half < 2 && produced < sample_count; half++) {
                unsigned nibble = half == 0 ? frame[byte] >> 4 : frame[byte] & 15u;
                int32_t signed_nibble = nibble < 8 ? (int32_t)nibble :
                                                      (int32_t)nibble - 16;
                int64_t predicted = (int64_t)signed_nibble * scale * 2048 +
                                    (int64_t)first * previous +
                                    (int64_t)second * older + 1024;
                int64_t sample = floor_div_2048(predicted);
                if (sample < -32768) sample = -32768;
                if (sample > 32767) sample = 32767;
                output[(size_t)produced * stride] = (int16_t)sample;
                older = previous;
                previous = (int32_t)sample;
                produced++;
            }
        }
    }
    return true;
}

bool wm_dsp_decode(const uint8_t *data, size_t size, uint32_t sample_count,
                   const int16_t coefficients[16], int16_t history1,
                   int16_t history2, int16_t *output,
                   char *error, size_t error_capacity) {
    audio_error(error, error_capacity, "");
    return dsp_decode_channel(data, size, sample_count, coefficients,
                              history1, history2, output, 1,
                              error, error_capacity);
}

void wm_audio_pcm_free(WmAudioPcm *audio) {
    if (!audio) return;
    free(audio->samples);
    *audio = (WmAudioPcm){0};
}

bool wm_bns_decode(const uint8_t *data, size_t size, WmAudioPcm *output,
                   char *error, size_t error_capacity) {
    audio_error(error, error_capacity, "");
    if (!output) {
        audio_error(error, error_capacity, "Missing audio output.");
        return false;
    }
    *output = (WmAudioPcm){0};
    static const uint8_t signature[8] = {'B', 'N', 'S', ' ', 0xfe, 0xff, 1, 0};
    if (!data || size < 32 || size > WM_AUDIO_MAX_RESOURCE_BYTES ||
        memcmp(data, signature, sizeof(signature)) != 0) {
        audio_error(error, error_capacity, "Expected bounded BNS 1.0 resource.");
        return false;
    }
    if (be32(data + 8) != size || be16(data + 12) != 32 ||
        be16(data + 14) != 2) {
        audio_error(error, error_capacity, "Invalid BNS header.");
        return false;
    }
    size_t info_offset = be32(data + 16), info_length = be32(data + 20);
    size_t data_offset = be32(data + 24), data_length = be32(data + 28);
    if (info_offset < 32 || data_offset < 32 || info_length < 8 || data_length < 8 ||
        !range_fits(size, info_offset, info_length) ||
        !range_fits(size, data_offset, data_length) ||
        info_offset > data_offset || info_length > data_offset - info_offset ||
        memcmp(data + info_offset, "INFO", 4) != 0 ||
        memcmp(data + data_offset, "DATA", 4) != 0 ||
        be32(data + info_offset + 4) != info_length ||
        be32(data + data_offset + 4) != data_length) {
        audio_error(error, error_capacity, "Invalid BNS INFO/DATA blocks.");
        return false;
    }
    const uint8_t *info = data + info_offset + 8;
    size_t info_size = info_length - 8;
    const uint8_t *encoded = data + data_offset + 8;
    size_t encoded_size = data_length - 8;
    if (info_size < 24 || info[0] != 0 || info[1] > 1 ||
        (info[2] != 1 && info[2] != 2)) {
        audio_error(error, error_capacity, "Unsupported BNS stream metadata.");
        return false;
    }
    uint8_t channels = info[2];
    uint32_t rate = be16(info + 4);
    uint32_t loop_start = be32(info + 8);
    uint32_t frames = be32(info + 12);
    size_t table_offset = be32(info + 16);
    if (!rate || frames == 0 || frames > WM_AUDIO_MAX_FRAMES ||
        (info[1] && loop_start >= frames) ||
        !range_fits(info_size, table_offset, (size_t)channels * 4)) {
        audio_error(error, error_capacity, "Invalid BNS sample count, rate or loop.");
        return false;
    }
    size_t encoded_bytes = (((size_t)frames + 13) / 14) * 8;
    size_t sample_values = (size_t)frames * channels;
    if (sample_values > SIZE_MAX / sizeof(int16_t)) {
        audio_error(error, error_capacity, "BNS PCM output is too large.");
        return false;
    }
    int16_t *pcm = malloc(sample_values * sizeof(*pcm));
    if (!pcm) {
        audio_error(error, error_capacity, "Out of memory decoding BNS.");
        return false;
    }
    for (size_t channel = 0; channel < channels; channel++) {
        size_t entry_offset = be32(info + table_offset + channel * 4);
        if (!range_fits(info_size, entry_offset, 12)) {
            audio_error(error, error_capacity, "Invalid BNS channel entry.");
            free(pcm);
            return false;
        }
        size_t sample_offset = be32(info + entry_offset);
        size_t context_offset = be32(info + entry_offset + 4);
        if (!range_fits(info_size, context_offset, 48) ||
            !range_fits(encoded_size, sample_offset, encoded_bytes)) {
            audio_error(error, error_capacity, "Invalid BNS DSP context or sample range.");
            free(pcm);
            return false;
        }
        const uint8_t *context = info + context_offset;
        int16_t coefficients[16];
        for (size_t index = 0; index < 16; index++) {
            coefficients[index] = signed_be16(context + index * 2);
        }
        int16_t history1 = signed_be16(context + 36);
        int16_t history2 = signed_be16(context + 38);
        if (!dsp_decode_channel(encoded + sample_offset, encoded_bytes, frames,
                                coefficients, history1, history2,
                                pcm + channel, channels,
                                error, error_capacity)) {
            free(pcm);
            return false;
        }
    }
    output->samples = pcm;
    output->sample_rate = rate;
    output->frame_count = frames;
    output->channels = channels;
    output->looping = info[1] != 0;
    if (output->looping) {
        output->loop_start = loop_start;
        output->loop_end = frames;
    }
    return true;
}
