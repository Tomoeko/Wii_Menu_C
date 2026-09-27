#include "wii_menu/audio/audio_held.h"

#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <string.h>

static WmAudioHeldTables authored_tables(void)
{
    WmAudioHeldTables tables;
    for (size_t index = 0; index < WM_AUDIO_HELD_DECIBELS; index++) {
        tables.decibels[index] = index == 0
                                     ? 0.0f
                                     : powf(10.0f, ((float)index - 904.0f) / 200.0f);
    }
    for (size_t index = 0; index < WM_AUDIO_HELD_PAN; index++) {
        tables.pan[index] = (256.0f - (float)index) / 256.0f;
    }
    return tables;
}

static WmAudioHeldProfile instant_profile(void)
{
    return (WmAudioHeldProfile){
        .attack_multiplier = 0.0f,
        .decay_rate = 65535.0f,
        .sustain_level = 0.0f,
        .release_rate = 65535.0f,
        .volume = 1.0f
    };
}

static WmAudioPcm looping_wave(int16_t *samples, uint32_t count)
{
    return (WmAudioPcm){
        .samples = samples,
        .sample_rate = WM_AUDIO_HELD_RATE,
        .frame_count = count,
        .loop_end = count,
        .channels = 1,
        .looping = true
    };
}

static float expected_sample(double sample, int pan_coefficient)
{
    int envelope_sample = (int)floor(sample * 32767.0 / 32768.0);
    return (float)floor(envelope_sample * pan_coefficient / 32768.0) / 32768.0f;
}

static void integer_gain_and_pan(void)
{
    WmAudioHeldTables tables = authored_tables();
    WmAudioHeldProfile profile = instant_profile();
    int16_t samples[] = {16384, -16384};
    WmAudioPcm wave = looping_wave(samples, 2);
    WmAudioHeldState state;
    float output[WM_AUDIO_HELD_BLOCK * 2];
    wm_audio_held_start(&state);
    assert(wm_audio_held_render(&state, &profile, &tables, &wave,
                                1.0f, 0.0f, 1.0f, output));
    for (size_t frame = 0; frame < WM_AUDIO_HELD_BLOCK; frame++) {
        float expected = expected_sample(samples[frame % 2], 16384);
        assert(output[frame * 2] == expected);
        assert(output[frame * 2 + 1] == expected);
    }
    wm_audio_held_start(&state);
    assert(wm_audio_held_render(&state, &profile, &tables, &wave,
                                1.0f, -1.0f, 1.0f, output));
    assert(output[0] == expected_sample(samples[0], 32768));
    assert(output[1] == 0.0f);
    wm_audio_held_start(&state);
    assert(wm_audio_held_render(&state, &profile, &tables, &wave,
                                0.5f, 1.0f, 1.0f, output));
    assert(output[0] == 0.0f);
    assert(output[1] == 8191.0f / 32768.0f);
}

static void source_loop_and_fractional_pitch(void)
{
    WmAudioHeldTables tables = authored_tables();
    WmAudioHeldProfile profile = instant_profile();
    int16_t samples[] = {1000, 2000, 3000, 4000};
    WmAudioPcm wave = looping_wave(samples, 4);
    wave.loop_start = 1;
    WmAudioHeldState state;
    float output[WM_AUDIO_HELD_BLOCK * 2];
    wm_audio_held_start(&state);
    assert(wm_audio_held_render(&state, &profile, &tables, &wave,
                                1.0f, -1.0f, 0.5f, output));
    const double first_samples[] = {
        1000, 1500, 2000, 2500, 3000, 3500, 4000, 3000, 2000
    };
    for (size_t frame = 0; frame < sizeof(first_samples) / sizeof(first_samples[0]);
         frame++) {
        assert(output[frame * 2] == expected_sample(first_samples[frame], 32768));
    }
    assert(state.position == 3.0);
    assert(wm_audio_held_render(&state, &profile, &tables, &wave,
                                1.0f, -1.0f, 0.5f, output));
    assert(output[0] == expected_sample(4000, 32768));
    assert(state.position == 3.0);

    wm_audio_held_start(&state);
    assert(wm_audio_held_render(&state, &profile, &tables, &wave,
                                1.0f, -1.0f, 0.3333333f, output));
    double total = WM_AUDIO_HELD_BLOCK *
                   ((double)truncf(0.3333333f * 65536.0f) / 65536.0);
    double expected = 1.0 + fmod(total - 1.0, 3.0);
    assert(state.position == expected);
}

static void envelope_and_release_lifetime(void)
{
    WmAudioHeldTables tables = authored_tables();
    WmAudioHeldProfile profile = instant_profile();
    profile.attack_multiplier = 0.5f;
    profile.sustain_level = -20.0f;
    profile.decay_rate = 10.0f;
    profile.release_rate = 100.0f;
    int16_t sample = 16384;
    WmAudioPcm wave = looping_wave(&sample, 1);
    WmAudioHeldState state;
    float output[WM_AUDIO_HELD_BLOCK * 2];
    wm_audio_held_start(&state);
    assert(wm_audio_held_render(&state, &profile, &tables, &wave,
                                1.0f, 0.0f, 1.0f, output));
    assert(state.envelope_level == -113.0f);
    assert(output[0] == 0.0f);
    assert(output[190] > output[0]);
    assert(wm_audio_held_render(&state, &profile, &tables, &wave,
                                0.0f, 0.0f, 1.0f, output));
    assert(state.envelope_level == -14.125f);
    assert(state.active);
    for (unsigned block = 0; block < 6; block++) {
        assert(wm_audio_held_render(&state, &profile, &tables, &wave,
                                    0.0f, 0.0f, 1.0f, output));
    }
    assert(state.envelope_level == -20.0f);
    assert(state.active);
    assert(wm_audio_held_render(&state, &profile, &tables, &wave,
                                1.0f, 0.0f, 1.0f, output));
    assert(state.envelope_level == -20.0f);
    wm_audio_held_release(&state);
    for (unsigned block = 0; block < 3; block++) {
        assert(wm_audio_held_render(&state, &profile, &tables, &wave,
                                    1.0f, 0.0f, 1.0f, output));
    }
    assert(state.envelope_level == -920.0f);
    assert(wm_audio_held_render(&state, &profile, &tables, &wave,
                                1.0f, 0.0f, 1.0f, output));
    assert(!state.active);
    for (size_t sample_index = 0; sample_index < WM_AUDIO_HELD_BLOCK * 2;
         sample_index++) assert(output[sample_index] == 0.0f);
    wm_audio_held_start(&state);
    assert(state.position == 0.0);
    assert(state.envelope_level == -904.0f);
    assert(state.active);
}

static void malformed_inputs(void)
{
    WmAudioHeldTables tables = authored_tables();
    assert(wm_audio_held_tables_valid(&tables));
    WmAudioHeldProfile profile = instant_profile();
    int16_t sample = 16384;
    WmAudioPcm wave = looping_wave(&sample, 1);
    WmAudioHeldState state;
    float output[WM_AUDIO_HELD_BLOCK * 2];
    wm_audio_held_start(&state);
    tables.pan[128] = NAN;
    assert(!wm_audio_held_tables_valid(&tables));
    assert(!wm_audio_held_render(&state, &profile, &tables, &wave,
                                 1.0f, 0.0f, 1.0f, output));
    tables = authored_tables();
    wave.loop_end = 0;
    assert(!wm_audio_held_render(&state, &profile, &tables, &wave,
                                 1.0f, 0.0f, 1.0f, output));
    wave.loop_end = 1;
    profile.attack_multiplier = 1.0f;
    assert(!wm_audio_held_render(&state, &profile, &tables, &wave,
                                 1.0f, 0.0f, 1.0f, output));
}

int main(void)
{
    integer_gain_and_pan();
    source_loop_and_fractional_pitch();
    envelope_and_release_lifetime();
    malformed_inputs();
    puts("Held-wave envelope, integer mixing, source loop, and lifetime passed.");
    return 0;
}
