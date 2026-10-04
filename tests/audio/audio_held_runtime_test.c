#define _POSIX_C_SOURCE 200809L
#define _XOPEN_SOURCE 700

#include "wii_menu/audio/audio.h"
#include "wii_menu/audio/audio_wave.h"

#include "audio_internal.h"
#include "audio_platform.h"

#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "../support/test_directory.h"

enum { TEST_WAVE_FRAMES = 256, TEST_RENDER_FRAMES = 8192 };

struct WmAudioDevice {
    WmAudioRender render;
    void *context;
};

typedef struct TestAssets {
    char root[256];
    char audio[256];
    char constant_wave[256];
    char ramp_wave[256];
    char sequence_manifest[256];
    char held_manifest[256];
} TestAssets;

static WmAudioDevice *device;

WmAudioDevice *wm_audio_device_open(WmAudioRender render, void *context) {
    assert(!device);
    device = calloc(1, sizeof(*device));
    assert(device);
    device->render = render;
    device->context = context;
    return device;
}

bool wm_audio_device_start(WmAudioDevice *opened) {
    return opened != NULL;
}

bool wm_audio_device_stop(WmAudioDevice *opened) {
    return opened != NULL;
}

void wm_audio_device_close(WmAudioDevice *opened) {
    assert(opened == device);
    free(opened);
    device = NULL;
}

static void asset_path(char destination[256], const char *root, const char *name) {
    int length = snprintf(destination, 256, "%s/%s", root, name);
    assert(length > 0 && length < 256);
}

static void write_text(const char *path, const char *text) {
    FILE *file = fopen(path, "wb");
    assert(file);
    size_t size = strlen(text);
    assert(fwrite(text, 1, size, file) == size);
    assert(fclose(file) == 0);
}

static void write_wave(const char *path, bool ramp, bool looping) {
    int16_t samples[TEST_WAVE_FRAMES];
    for (size_t frame = 0; frame < TEST_WAVE_FRAMES; frame++) {
        samples[frame] = ramp ? (int16_t)(4096 + frame * 32) : 16384;
    }
    WmAudioPcm pcm = {.samples = samples,
                      .sample_rate = 32000,
                      .frame_count = TEST_WAVE_FRAMES,
                      .loop_start = 0,
                      .loop_end = TEST_WAVE_FRAMES,
                      .channels = 1,
                      .looping = looping};
    char error[160] = {0};
    assert(wm_audio_wav_write(path, &pcm, error, sizeof(error)));
}

static TestAssets create_assets(void) {
    TestAssets assets = {0};
    strcpy(assets.root, "wm-audio-held-runtime-XXXXXX");
    assert(wm_test_directory(assets.root, sizeof(assets.root)));
    asset_path(assets.audio, assets.root, "audio");
    assert(cc_directory_create(assets.audio));
    asset_path(assets.constant_wave, assets.audio, "drag.wav");
    asset_path(assets.ramp_wave, assets.audio, "WIPL_SE_BOARD_DRAG.wav");
    asset_path(assets.sequence_manifest, assets.root, "audio-sequence.json");
    asset_path(assets.held_manifest, assets.root, "audio-held.json");
    write_wave(assets.constant_wave, false, true);
    write_wave(assets.ramp_wave, true, true);
    write_text(assets.sequence_manifest,
               "{\n"
               "    \"drag\": {\"sourceSymbol\": \"WIPL_SE_CH_DRAG\", \"gain\": 1},\n"
               "    \"WIPL_SE_BOARD_DRAG\": {\"sourceSymbol\": \"WIPL_SE_BOARD_DRAG\", "
               "\"gain\": 1}\n"
               "}\n");
    return assets;
}

static void write_held_manifest(const TestAssets *assets) {
    FILE *file = fopen(assets->held_manifest, "wb");
    assert(file);
    assert(fputs("{\n"
                 "    \"schema\": 1,\n"
                 "    \"sampleRate\": 32000,\n"
                 "    \"blockFrames\": 96,\n"
                 "    \"tables\": {\n"
                 "        \"decibels\": [\n",
                 file) >= 0);
    /* Synthetic tables exercise metadata ownership without retaining any
     * extracted driver data. The levels follow a conventional decibel curve. */
    for (int index = 0; index < 965; index++) {
        float gain = powf(10.0f, (float)(index - 904) / 200.0f);
        assert(fprintf(file, "            %.9g%s\n", (double)gain,
                       index == 964 ? "" : ",") > 0);
    }
    assert(fputs("        ],\n        \"pan\": [\n", file) >= 0);
    for (int index = 0; index < 257; index++) {
        float gain = 1.0f - (float)index / 256.0f;
        assert(fprintf(file, "            %.9g%s\n", (double)gain,
                       index == 256 ? "" : ",") > 0);
    }
    assert(fputs("        ]\n    },\n    \"profiles\": {\n", file) >= 0);
    const char *symbols[] = {"WIPL_SE_CH_DRAG", "WIPL_SE_BOARD_DRAG"};
    for (size_t index = 0; index < 2; index++) {
        assert(fprintf(file,
                       "        \"%s\": {\n"
                       "            \"attackMultiplier\": 0.5,\n"
                       "            \"decayRate\": 65535,\n"
                       "            \"sustainLevel\": 0,\n"
                       "            \"releaseRate\": 12,\n"
                       "            \"volume\": 1,\n"
                       "            \"pan\": 0\n"
                       "        }%s\n",
                       symbols[index], index == 1 ? "" : ",") > 0);
    }
    assert(fputs("    }\n}\n", file) >= 0);
    assert(fclose(file) == 0);
}

static void destroy_assets(const TestAssets *assets) {
    assert(remove(assets->constant_wave) == 0);
    assert(remove(assets->ramp_wave) == 0);
    assert(remove(assets->sequence_manifest) == 0);
    if (cc_path_information(assets->held_manifest, false, NULL) == CC_PATH_FILE)
        assert(remove(assets->held_manifest) == 0);
    assert(wm_test_remove_directory(assets->audio) == 0);
    assert(wm_test_remove_directory(assets->root) == 0);
}

static void render(float *samples, size_t frames) {
    assert(device);
    device->render(device->context, samples, frames);
    for (size_t index = 0; index < frames * 2; index++)
        assert(isfinite(samples[index]));
}

static void discard_frames(size_t frames) {
    float samples[514];
    while (frames) {
        size_t chunk = frames < 257 ? frames : 257;
        render(samples, chunk);
        frames -= chunk;
    }
}

static bool silent(const float *samples, size_t frames) {
    for (size_t index = 0; index < frames * 2; index++) {
        if (samples[index] != 0.0f)
            return false;
    }
    return true;
}

static void test_attack_controls_and_release(const TestAssets *assets) {
    WmAudio *audio = wm_audio_create(assets->root);
    assert(audio && device);
    assert(!wm_audio_hold_loop(audio, "drag", NAN, 0.0f, 1.0f));
    assert(!wm_audio_hold_loop(audio, "drag", 0.5f, NAN, 1.0f));
    assert(!wm_audio_hold_loop(audio, "drag", 0.5f, 0.0f, NAN));
    assert(wm_audio_hold_loop(audio, "drag", 0.5f, 0.0f, 1.0f));
    float samples[TEST_RENDER_FRAMES * 2];
    render(samples, 1);
    assert(fabsf(samples[0]) < 0.01f);
    assert(fabsf(samples[1]) < 0.01f);
    discard_frames(TEST_RENDER_FRAMES);
    render(samples, 256);
    const float steady = samples[0];
    assert(steady > 0.10f && steady < 0.15f);
    assert(fabsf(samples[1] - steady) < 0.00001f);

    assert(wm_audio_hold_loop(audio, "drag", 0.0f, 0.0f, 1.0f));
    discard_frames(512);
    render(samples, 256);
    assert(silent(samples, 256));
    assert(wm_audio_hold_loop(audio, "drag", 0.5f, -1.0f, 1.0f));
    discard_frames(512);
    render(samples, 256);
    assert(samples[0] > steady * 1.9f);
    assert(samples[1] == 0.0f);
    assert(wm_audio_hold_loop(audio, "drag", 0.5f, 1.0f, 1.0f));
    discard_frames(512);
    render(samples, 256);
    assert(samples[0] == 0.0f);
    assert(samples[1] > steady * 1.9f);

    wm_audio_stop_loop(audio, "drag");
    render(samples, 512);
    assert(!silent(samples, 512));
    discard_frames(TEST_RENDER_FRAMES);
    render(samples, 256);
    assert(silent(samples, 256));
    wm_audio_destroy(audio);
}

static void test_callback_partitioning(const TestAssets *assets) {
    float contiguous[TEST_RENDER_FRAMES * 2];
    float partitioned[TEST_RENDER_FRAMES * 2];
    WmAudio *audio = wm_audio_create(assets->root);
    assert(wm_audio_hold_loop(audio, "WIPL_SE_BOARD_DRAG", 0.75f, 0.25f, 2.0f));
    render(contiguous, TEST_RENDER_FRAMES);
    wm_audio_destroy(audio);

    audio = wm_audio_create(assets->root);
    assert(wm_audio_hold_loop(audio, "WIPL_SE_BOARD_DRAG", 0.75f, 0.25f, 2.0f));
    const size_t chunks[] = {1, 17, 95, 144, 251, 13, 509};
    size_t offset = 0;
    size_t chunk_index = 0;
    while (offset < TEST_RENDER_FRAMES) {
        size_t chunk = chunks[chunk_index++ % (sizeof(chunks) / sizeof(chunks[0]))];
        if (chunk > TEST_RENDER_FRAMES - offset)
            chunk = TEST_RENDER_FRAMES - offset;
        render(partitioned + offset * 2, chunk);
        offset += chunk;
    }
    assert(memcmp(contiguous, partitioned, sizeof(contiguous)) == 0);
    wm_audio_destroy(audio);
}

static void test_applied_pitch(const TestAssets *assets) {
    float normal[768 * 2];
    float faster[768 * 2];
    WmAudio *audio = wm_audio_create(assets->root);
    assert(wm_audio_hold_loop(audio, "WIPL_SE_BOARD_DRAG", 1.0f, 0.0f, 1.0f));
    discard_frames(TEST_RENDER_FRAMES);
    render(normal, 768);
    assert(wm_audio_hold_loop(audio, "WIPL_SE_BOARD_DRAG", 1.0f, 0.0f, 2.0f));
    discard_frames(512);
    render(faster, 768);
    double normal_half_period_error = 0.0;
    for (size_t frame = 0; frame < 384; frame++) {
        assert(fabsf(normal[frame * 2] - normal[(frame + 384) * 2]) < 0.0001f);
        assert(fabsf(faster[frame * 2] - faster[(frame + 192) * 2]) < 0.0001f);
        normal_half_period_error +=
            fabsf(normal[frame * 2] - normal[(frame + 192) * 2]);
    }
    assert(normal_half_period_error / 384.0 > 0.03);
    wm_audio_destroy(audio);
}

static void test_regrab_during_release(const TestAssets *assets) {
    WmAudio *audio = wm_audio_create(assets->root);
    assert(wm_audio_hold_loop(audio, "drag", 0.5f, 1.0f, 1.0f));
    discard_frames(TEST_RENDER_FRAMES);
    wm_audio_stop_loop(audio, "drag");
    assert(wm_audio_hold_loop(audio, "drag", 0.5f, -1.0f, 1.0f));
    float samples[TEST_RENDER_FRAMES * 2];
    render(samples, 512);
    bool left_audible = false;
    bool right_audible = false;
    for (size_t frame = 0; frame < 512; frame++) {
        left_audible |= samples[frame * 2] > 0.01f;
        right_audible |= samples[frame * 2 + 1] > 0.01f;
    }
    assert(left_audible && right_audible);
    discard_frames(TEST_RENDER_FRAMES);
    render(samples, 256);
    assert(samples[0] > 0.2f);
    assert(samples[1] == 0.0f);
    wm_audio_stop_loop(audio, "drag");
    discard_frames(TEST_RENDER_FRAMES);
    render(samples, 256);
    assert(silent(samples, 256));
    wm_audio_destroy(audio);
}

static void test_legacy_fallback(const TestAssets *assets) {
    WmAudio *audio = wm_audio_create(assets->root);
    assert(wm_audio_hold_loop(audio, "drag", 0.5f, 0.0f, 1.0f));
    float samples[64];
    render(samples, 32);
    assert(fabsf(samples[0] - 0.25f) < 0.00001f);
    assert(fabsf(samples[1] - 0.25f) < 0.00001f);
    wm_audio_stop_loop(audio, "drag");
    render(samples, 32);
    assert(silent(samples, 32));
    wm_audio_destroy(audio);
}

static void test_ordinary_loop_pitch_transition(const TestAssets *assets) {
    WmAudio *audio = wm_audio_create(assets->root);
    assert(wm_audio_hold_loop(audio, "WIPL_SE_BOARD_DRAG", 0.5f, 0.0f, 1.0f));
    float samples[137 * 2];
    render(samples, 137);
    WmAudioVoice *voice = &audio->voices[0];
    assert(!audio->clips[voice->clip_index].held_profile);
    double position = voice->frame;
    assert(fabs(position - 137.0 * 32000 / 48000) < 1e-10);
    wm_audio_set_loop(audio, "WIPL_SE_BOARD_DRAG", 0.5f, 0.0f, 1.25f);
    render(samples, 137);
    for (size_t frame = 0; frame < 137; ++frame) {
        uint32_t first = (uint32_t)position;
        uint32_t next = (first + 1) % TEST_WAVE_FRAMES;
        float fraction = (float)(position - first);
        float first_sample = (float)(4096 + first * 32) / 32768;
        float next_sample = (float)(4096 + next * 32) / 32768;
        float expected =
            (first_sample + (next_sample - first_sample) * fraction) * 0.5f;
        assert(samples[frame * 2] == expected && samples[frame * 2 + 1] == expected);
        position += 32000.0 * 1.25 / 48000;
        if (position >= TEST_WAVE_FRAMES)
            position = fmod(position, TEST_WAVE_FRAMES);
    }
    assert(voice->frame == position);
    wm_audio_destroy(audio);
}

static void test_held_capture_and_restart(const TestAssets *assets) {
    WmAudio *audio = wm_audio_create(assets->root);
    assert(wm_audio_output_stop(audio));
    assert(wm_audio_capture_begin(audio, 1024));
    assert(wm_audio_output_start(audio));
    assert(wm_audio_hold_loop(audio, "WIPL_SE_BOARD_DRAG", 0.75f, 0.25f, 1.0f));
    float first[1024 * 2], replay[1024 * 2], captured[1024 * 2];
    render(first, 1024);
    assert(wm_audio_capture_read(audio, captured, 1024) == 1024);
    assert(!memcmp(first, captured, sizeof(first)));
    assert(fabsf(first[0]) < 0.00001f && !silent(first, 1024));
    wm_audio_reset_all(audio);
    assert(wm_audio_hold_loop(audio, "WIPL_SE_BOARD_DRAG", 0.75f, 0.25f, 1.0f));
    render(replay, 1024);
    assert(!memcmp(first, replay, sizeof(first)));
    assert(wm_audio_capture_read(audio, captured, 1024) == 1024);
    assert(!memcmp(replay, captured, sizeof(replay)));
    wm_audio_destroy(audio);
}

static void test_repeated_voice_retirement(const TestAssets *assets) {
    WmAudio *audio = wm_audio_create(assets->root);
    float samples[64];
    /* Exceed the mixer's finite voice capacity. Every completed release must
     * retire its resampler lookahead so the next hold can reuse the slot. */
    for (size_t iteration = 0; iteration < 64; iteration++) {
        float pan = iteration % 2 ? -1.0f : 1.0f;
        assert(wm_audio_hold_loop(audio, "drag", 0.5f, pan, 1.0f));
        discard_frames(2048);
        render(samples, 32);
        assert(!silent(samples, 32));
        wm_audio_stop_loop(audio, "drag");
        discard_frames(TEST_RENDER_FRAMES);
        render(samples, 32);
        assert(silent(samples, 32));
    }
    wm_audio_destroy(audio);
}

static void test_manifest_loop_metadata(const TestAssets *assets) {
    /* Older WAVs may obtain loop points from the manifest instead of smpl.
     * Profile admission must examine that final interpreted loop state. */
    write_wave(assets->constant_wave, false, false);
    write_text(assets->sequence_manifest,
               "{\n"
               "    \"drag\": {\n"
               "        \"sourceSymbol\": \"WIPL_SE_CH_DRAG\",\n"
               "        \"gain\": 1,\n"
               "        \"loop\": true,\n"
               "        \"loopStart\": 0,\n"
               "        \"loopEnd\": 0.008\n"
               "    }\n"
               "}\n");
    WmAudio *audio = wm_audio_create(assets->root);
    assert(wm_audio_hold_loop(audio, "drag", 0.5f, 0.0f, 1.0f));
    float samples[2];
    render(samples, 1);
    assert(fabsf(samples[0]) < 0.01f);
    assert(fabsf(samples[1]) < 0.01f);
    wm_audio_destroy(audio);
}

static void test_untrusted_manifest_numbers(const TestAssets *assets) {
    int16_t samples[1024] = {0};
    WmAudioPcm pcm = {
        .samples = samples, .sample_rate = 32000, .frame_count = 1024, .channels = 1};
    char error[160] = {0};
    assert(wm_audio_wav_write(assets->constant_wave, &pcm, error, sizeof(error)));

    /* In float arithmetic, these seconds become 2^32 and 2^32 + 512
     * frames. Narrowing first would create a false 0..512 loop. */
    write_text(assets->sequence_manifest,
               "{\"drag\": {\"gain\": 1, \"loop\": true, "
               "\"loopStart\": 134217.734375, \"loopEnd\": 134217.75}}");
    WmAudio *audio = wm_audio_create(assets->root);
    assert(audio);
    WmAudioClip *clip = wm_audio_load_clip(audio, "drag", "audio");
    assert(clip && !clip->pcm.looping);
    wm_audio_destroy(audio);

    write_text(assets->sequence_manifest, "{\"drag\": {\"gain\": 3e38, \"loop\": true, "
                                          "\"loopStart\": 1e30, \"loopEnd\": 2e30}}");
    audio = wm_audio_create(assets->root);
    assert(audio);
    clip = wm_audio_load_clip(audio, "drag", "audio");
    assert(clip && !clip->pcm.looping && clip->gain == 1.0f);
    wm_audio_destroy(audio);
}

int main(void) {
    TestAssets assets = create_assets();
    test_legacy_fallback(&assets);
    test_ordinary_loop_pitch_transition(&assets);
    write_text(assets.held_manifest,
               "{\n    \"schema\": 1,\n    \"tables\": {\"pan\": [1]}\n}\n");
    test_legacy_fallback(&assets);
    write_held_manifest(&assets);
    test_attack_controls_and_release(&assets);
    test_callback_partitioning(&assets);
    test_held_capture_and_restart(&assets);
    test_applied_pitch(&assets);
    test_regrab_during_release(&assets);
    test_repeated_voice_retirement(&assets);
    test_manifest_loop_metadata(&assets);
    test_untrusted_manifest_numbers(&assets);
    destroy_assets(&assets);
    puts("held audio runtime tests passed");
    return 0;
}
