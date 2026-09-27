#include "wii_menu/audio/audio_held.h"

#include <math.h>
#include <stddef.h>
#include <string.h>

enum {
    HELD_ATTACK,
    HELD_DECAY,
    HELD_SUSTAIN,
    HELD_RELEASE
};

void wm_audio_held_start(WmAudioHeldState *state)
{
    if (!state) return;
    *state = (WmAudioHeldState){
        .envelope_level = -904.0f,
        .active = true
    };
}

void wm_audio_held_release(WmAudioHeldState *state)
{
    if (state && state->active) state->stage = HELD_RELEASE;
}

bool wm_audio_held_tables_valid(const WmAudioHeldTables *tables)
{
    if (!tables) return false;
    for (size_t index = 0; index < WM_AUDIO_HELD_DECIBELS; index++) {
        if (!isfinite(tables->decibels[index]) ||
            tables->decibels[index] < 0.0f || tables->decibels[index] > 2.0f)
            return false;
    }
    for (size_t index = 0; index < WM_AUDIO_HELD_PAN; index++) {
        if (!isfinite(tables->pan[index]) || tables->pan[index] < 0.0f ||
            tables->pan[index] > 2.0f) return false;
    }
    return true;
}

static bool valid_profile(const WmAudioHeldProfile *profile)
{
    return profile && isfinite(profile->attack_multiplier) &&
           profile->attack_multiplier >= 0.0f &&
           profile->attack_multiplier < 1.0f &&
           isfinite(profile->decay_rate) && profile->decay_rate >= 0.0f &&
           isfinite(profile->sustain_level) &&
           profile->sustain_level >= -904.0f && profile->sustain_level <= 0.0f &&
           isfinite(profile->release_rate) && profile->release_rate > 0.0f &&
           isfinite(profile->volume) && profile->volume >= 0.0f &&
           profile->volume <= 1.0f && isfinite(profile->pan) &&
           profile->pan >= -1.0f && profile->pan <= 1.0f;
}

static float envelope_gain(const WmAudioHeldState *state,
                            const WmAudioHeldProfile *profile,
                            const WmAudioHeldTables *tables)
{
    float level = state->stage == HELD_ATTACK &&
                  profile->attack_multiplier == 0.0f
                      ? 0.0f : state->envelope_level / 10.0f;
    level = fminf(fmaxf(level, -90.4f), 6.0f);
    int index = 904 + (int)(level * 10.0f);
    if (index < 0) index = 0;
    if (index >= WM_AUDIO_HELD_DECIBELS) index = WM_AUDIO_HELD_DECIBELS - 1;
    return tables->decibels[index];
}

static void update_envelope(WmAudioHeldState *state,
                            const WmAudioHeldProfile *profile)
{
    if (state->stage == HELD_ATTACK) {
        for (unsigned millisecond = 0; millisecond < 3; millisecond++) {
            state->envelope_level *= profile->attack_multiplier;
            if (state->envelope_level > -1.0f / 32.0f) {
                state->envelope_level = 0.0f;
                state->stage = HELD_DECAY;
            }
        }
        /* Native attack consumes all three milliseconds, including the step
         * that reaches decay. The decay comparison still runs with zero time. */
        if (state->stage == HELD_DECAY &&
            state->envelope_level <= profile->sustain_level) {
            state->envelope_level = profile->sustain_level;
            state->stage = HELD_SUSTAIN;
        }
    } else if (state->stage == HELD_DECAY) {
        state->envelope_level -= profile->decay_rate * 3.0f;
        if (state->envelope_level <= profile->sustain_level) {
            state->envelope_level = profile->sustain_level;
            state->stage = HELD_SUSTAIN;
        }
    } else if (state->stage == HELD_RELEASE) {
        state->envelope_level -= profile->release_rate * 3.0f;
    }
}

static int volume_coefficient(float gain)
{
    return (int)truncf(fminf(fmaxf(gain, 0.0f), 1.0f) * 32767.0f);
}

static int pan_coefficient(float gain)
{
    return (int)truncf(fminf(fmaxf(gain * 32768.0f, 0.0f), 65535.0f));
}

static int multiply_volume(double sample, int coefficient)
{
    return (int)floor(sample * coefficient / 32768.0);
}

bool wm_audio_held_render(WmAudioHeldState *state,
                          const WmAudioHeldProfile *profile,
                          const WmAudioHeldTables *tables,
                          const WmAudioPcm *pcm, float gain, float pan,
                          float pitch,
                          float stereo[WM_AUDIO_HELD_BLOCK * 2])
{
    if (!stereo) return false;
    memset(stereo, 0, WM_AUDIO_HELD_BLOCK * 2 * sizeof(*stereo));
    if (!state || !valid_profile(profile) || !tables || !pcm ||
        !pcm->samples || pcm->sample_rate != WM_AUDIO_HELD_RATE ||
        pcm->channels != 1 || !pcm->looping ||
        pcm->loop_start >= pcm->loop_end || pcm->loop_end > pcm->frame_count ||
        !isfinite(gain) || gain < 0.0f || gain > 2.0f ||
        !isfinite(pan) || !isfinite(pitch) || pitch < 0.25f || pitch > 4.0f ||
        !isfinite(state->position) || state->position < 0.0 ||
        !isfinite(state->envelope_level) || state->stage > HELD_RELEASE ||
        (state->has_previous_gain &&
         (!isfinite(state->previous_gain) || state->previous_gain < 0.0f ||
          state->previous_gain > 2.0f))) {
        return false;
    }
    if (!state->active) return true;
    if (state->stage == HELD_RELEASE && state->envelope_level <= -904.0f) {
        state->active = false;
        return true;
    }

    gain *= profile->volume;
    float prior_gain = state->has_previous_gain ? state->previous_gain : gain;
    float initial_envelope = envelope_gain(state, profile, tables);
    if (!isfinite(initial_envelope) || initial_envelope < 0.0f ||
        initial_envelope > 2.0f) return false;
    int initial = volume_coefficient(prior_gain * initial_envelope);
    update_envelope(state, profile);
    float target_envelope = envelope_gain(state, profile, tables);
    if (!isfinite(target_envelope) || target_envelope < 0.0f ||
        target_envelope > 2.0f) return false;
    int target = volume_coefficient(gain * target_envelope);
    int delta = (target - initial) / WM_AUDIO_HELD_BLOCK;
    state->previous_gain = gain;
    state->has_previous_gain = true;

    pan = fminf(fmaxf(pan + profile->pan, -1.0f), 1.0f);
    int pan_index = (int)floorf((pan + 1.0f) * 128.0f + 0.5f);
    if (!isfinite(tables->pan[pan_index]) ||
        !isfinite(tables->pan[256 - pan_index]) ||
        tables->pan[pan_index] < 0.0f || tables->pan[pan_index] > 2.0f ||
        tables->pan[256 - pan_index] < 0.0f ||
        tables->pan[256 - pan_index] > 2.0f) return false;
    int left_send = pan_coefficient(tables->pan[pan_index]);
    int right_send = pan_coefficient(tables->pan[256 - pan_index]);
    double step = (double)truncf(pitch * 65536.0f) / 65536.0;
    for (size_t frame = 0; frame < WM_AUDIO_HELD_BLOCK; frame++) {
        if (state->position >= pcm->loop_end) {
            double span = pcm->loop_end - pcm->loop_start;
            state->position = pcm->loop_start +
                              fmod(state->position - pcm->loop_start, span);
        }
        uint32_t first = (uint32_t)state->position;
        uint32_t next = first + 1;
        if (next >= pcm->loop_end) next = pcm->loop_start;
        double fraction = state->position - first;
        double source = pcm->samples[first] +
                        (pcm->samples[next] - pcm->samples[first]) * fraction;
        int volume = initial + (int)frame * delta;
        int sample = multiply_volume(source, volume);
        stereo[frame * 2] =
            (float)multiply_volume(sample, left_send) / 32768.0f;
        stereo[frame * 2 + 1] =
            (float)multiply_volume(sample, right_send) / 32768.0f;
        state->position += step;
    }
    return true;
}
