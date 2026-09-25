#include "wii_menu/resource_audio.h"

#include <assert.h>
#include <stdint.h>
#include <string.h>

enum {
    BNS_SIZE = 216,
    INFO_OFFSET = 32,
    INFO_LENGTH = 160,
    DATA_OFFSET = 192,
    DATA_LENGTH = 24
};

static void write_be16(uint8_t *target, uint16_t value) {
    target[0] = (uint8_t)(value >> 8);
    target[1] = (uint8_t)value;
}

static void write_be32(uint8_t *target, uint32_t value) {
    target[0] = (uint8_t)(value >> 24);
    target[1] = (uint8_t)(value >> 16);
    target[2] = (uint8_t)(value >> 8);
    target[3] = (uint8_t)value;
}

static void make_bns(uint8_t bytes[BNS_SIZE]) {
    memset(bytes, 0, BNS_SIZE);
    static const uint8_t signature[8] = {'B', 'N', 'S', ' ', 0xfe, 0xff, 1, 0};
    memcpy(bytes, signature, sizeof(signature));
    write_be32(bytes + 8, BNS_SIZE);
    write_be16(bytes + 12, 32);
    write_be16(bytes + 14, 2);
    write_be32(bytes + 16, INFO_OFFSET);
    write_be32(bytes + 20, INFO_LENGTH);
    write_be32(bytes + 24, DATA_OFFSET);
    write_be32(bytes + 28, DATA_LENGTH);

    memcpy(bytes + INFO_OFFSET, "INFO", 4);
    write_be32(bytes + INFO_OFFSET + 4, INFO_LENGTH);
    uint8_t *info = bytes + INFO_OFFSET + 8;
    info[1] = 1;
    info[2] = 2;
    write_be16(info + 4, 32000);
    write_be32(info + 8, 2);
    write_be32(info + 12, 6);
    write_be32(info + 16, 24);
    write_be32(info + 24, 32);
    write_be32(info + 28, 44);
    write_be32(info + 32, 0);
    write_be32(info + 36, 56);
    write_be32(info + 44, 8);
    write_be32(info + 48, 104);
    write_be16(info + 56, 2048);
    write_be16(info + 104, 2048);
    write_be16(info + 56 + 36, 100);
    write_be16(info + 104 + 36, 100);

    memcpy(bytes + DATA_OFFSET, "DATA", 4);
    write_be32(bytes + DATA_OFFSET + 4, DATA_LENGTH);
    const uint8_t encoded[16] = {
        0x00, 0x17, 0x8f, 0, 0, 0, 0, 0,
        0x00, 0xff, 0xf1, 0, 0, 0, 0, 0
    };
    memcpy(bytes + DATA_OFFSET + 8, encoded, sizeof(encoded));
}

static void make_mono_bns(uint8_t bytes[144]) {
    uint8_t stereo[BNS_SIZE];
    make_bns(stereo);
    memset(bytes, 0, 144);
    memcpy(bytes, stereo, 32);
    write_be32(bytes + 8, 144);
    write_be32(bytes + 20, 96);
    write_be32(bytes + 24, 128);
    write_be32(bytes + 28, 16);
    memcpy(bytes + 32, "INFO", 4);
    write_be32(bytes + 36, 96);
    uint8_t *info = bytes + 40;
    info[2] = 1;
    write_be16(info + 4, 32000);
    write_be32(info + 12, 6);
    write_be32(info + 16, 24);
    write_be32(info + 24, 28);
    write_be32(info + 28, 0);
    write_be32(info + 32, 40);
    write_be16(info + 40, 2048);
    write_be16(info + 40 + 36, 100);
    memcpy(bytes + 128, "DATA", 4);
    write_be32(bytes + 132, 16);
    const uint8_t encoded[8] = {0, 0x17, 0x8f, 0, 0, 0, 0, 0};
    memcpy(bytes + 136, encoded, sizeof(encoded));
}

int main(void) {
    char error[128];
    const uint8_t frame[8] = {0, 0x17, 0x8f, 0, 0xff, 0, 0, 0};
    const int16_t zero_coefficients[16] = {0};
    int16_t samples[6];
    assert(wm_dsp_decode(frame, sizeof(frame), 6, zero_coefficients,
                         0, 0, samples, error, sizeof(error)));
    const int16_t expected_dsp[6] = {1, 7, -8, -1, 0, 0};
    assert(memcmp(samples, expected_dsp, sizeof(samples)) == 0);

    int16_t history_coefficients[16] = {2048};
    const uint8_t saturation_frame[8] = {0, 0x11, 0, 0, 0, 0, 0, 0};
    assert(wm_dsp_decode(saturation_frame, sizeof(saturation_frame), 3,
                         history_coefficients, 32766, 0,
                         samples, error, sizeof(error)));
    assert(samples[0] == 32767 && samples[1] == 32767 && samples[2] == 32767);
    assert(!wm_dsp_decode(frame, 1, 1, zero_coefficients,
                          0, 0, samples, error, sizeof(error)));
    uint8_t invalid_predictor[8] = {0x80};
    assert(!wm_dsp_decode(invalid_predictor, sizeof(invalid_predictor), 1,
                          zero_coefficients, 0, 0, samples, error, sizeof(error)));

    uint8_t bns[BNS_SIZE];
    make_bns(bns);
    WmAudioPcm audio;
    assert(wm_bns_decode(bns, sizeof(bns), &audio, error, sizeof(error)));
    assert(audio.channels == 2 && audio.sample_rate == 32000 && audio.frame_count == 6);
    assert(audio.looping && audio.loop_start == 2 && audio.loop_end == 6);
    const int16_t expected_pcm[12] = {
        101, 99, 108, 98, 100, 97, 99, 98, 99, 98, 99, 98
    };
    assert(memcmp(audio.samples, expected_pcm, sizeof(expected_pcm)) == 0);
    wm_audio_pcm_free(&audio);
    assert(!audio.samples && !audio.frame_count);

    uint8_t mono[144];
    make_mono_bns(mono);
    assert(wm_bns_decode(mono, sizeof(mono), &audio, error, sizeof(error)));
    assert(audio.channels == 1 && !audio.looping && audio.loop_end == 0);
    const int16_t expected_mono[6] = {101, 108, 100, 99, 99, 99};
    assert(memcmp(audio.samples, expected_mono, sizeof(expected_mono)) == 0);
    wm_audio_pcm_free(&audio);

    assert(!wm_bns_decode(bns, sizeof(bns) - 1, &audio, error, sizeof(error)));
    make_bns(bns);
    bns[INFO_OFFSET + 8 + 1] = 2;
    assert(!wm_bns_decode(bns, sizeof(bns), &audio, error, sizeof(error)));
    make_bns(bns);
    write_be32(bns + INFO_OFFSET + 8 + 8, 6);
    assert(!wm_bns_decode(bns, sizeof(bns), &audio, error, sizeof(error)));
    make_bns(bns);
    write_be32(bns + INFO_OFFSET + 8 + 24, UINT32_MAX);
    assert(!wm_bns_decode(bns, sizeof(bns), &audio, error, sizeof(error)));
    return 0;
}
