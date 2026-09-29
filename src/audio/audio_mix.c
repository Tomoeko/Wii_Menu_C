#include "audio_internal.h"

#include <math.h>
#include <string.h>

static float sample_at(const WmAudioClip *clip, uint32_t frame, uint8_t channel) {
    if (clip->pcm.channels == 1)
        channel = 0;
    return (float)clip->pcm.samples[(size_t)frame * clip->pcm.channels + channel] /
           32768.0f;
}

static bool held_native_sample(WmAudio *audio, WmAudioVoice *voice,
                               const WmAudioClip *clip, float sample[2]) {
    if (voice->held_cursor == WM_AUDIO_HELD_BLOCK) {
        if (!voice->held_state.active ||
            !wm_audio_held_render(&voice->held_state, clip->held_profile,
                                  &audio->held_tables, &clip->pcm, voice->gain,
                                  voice->pan, voice->pitch, voice->held_block))
            return false;
        voice->held_cursor = 0;
    }
    sample[0] = voice->held_block[voice->held_cursor * 2];
    sample[1] = voice->held_block[voice->held_cursor * 2 + 1];
    voice->held_cursor++;
    return true;
}

static void mix_held_voice(WmAudio *audio, WmAudioVoice *voice, const WmAudioClip *clip,
                           float *output, size_t frames) {
    /* The held voice runs at native 32 kHz. Retain its 2:3 output phase and
     * lookahead across arbitrary host callback sizes, without allocation. */
    if (!voice->held_primed) {
        if (!held_native_sample(audio, voice, clip, voice->held_current)) {
            voice->active = false;
            return;
        }
        voice->held_has_next = held_native_sample(audio, voice, clip, voice->held_next);
        voice->held_primed = true;
    }
    for (size_t frame = 0; frame < frames; frame++) {
        float fraction = (float)voice->held_phase / 3.0f;
        for (size_t channel = 0; channel < 2; channel++) {
            output[frame * 2 + channel] +=
                voice->held_current[channel] +
                (voice->held_next[channel] - voice->held_current[channel]) * fraction;
        }
        voice->held_phase += 2;
        if (voice->held_phase >= 3) {
            voice->held_phase -= 3;
            if (!voice->held_has_next) {
                voice->active = false;
                break;
            }
            memcpy(voice->held_current, voice->held_next, sizeof(voice->held_current));
            voice->held_has_next =
                held_native_sample(audio, voice, clip, voice->held_next);
            if (!voice->held_has_next)
                memset(voice->held_next, 0, sizeof(voice->held_next));
        }
    }
}

void wm_audio_mix(void *context, float *interleaved, size_t frames) {
    WmAudio *audio = context;
    if (!audio || !interleaved || frames > SIZE_MAX / (2 * sizeof(float)))
        return;
    memset(interleaved, 0, frames * 2 * sizeof(float));
    wm_audio_refresh_controls(audio);
    bool finished = false;
    for (size_t index = 0; index < WM_AUDIO_MAX_VOICES; index++) {
        WmAudioVoice *voice = &audio->voices[index];
        if (!voice->active || voice->paused)
            continue;
        const WmAudioClip *clip = &audio->clips[voice->clip_index];
        if (clip->held_profile) {
            mix_held_voice(audio, voice, clip, interleaved, frames);
            finished |= !voice->active;
            continue;
        }
        uint32_t end = clip->pcm.looping ? clip->pcm.loop_end : clip->pcm.frame_count;
        for (size_t output = 0; output < frames; output++) {
            if (voice->frame >= end) {
                if (!clip->pcm.looping) {
                    voice->active = false;
                    break;
                }
                double span = end - clip->pcm.loop_start;
                voice->frame = clip->pcm.loop_start +
                               fmod(voice->frame - clip->pcm.loop_start, span);
            }
            uint32_t first = (uint32_t)voice->frame;
            uint32_t next = first + 1;
            if (next >= end)
                next = clip->pcm.looping ? clip->pcm.loop_start : first;
            float fraction = (float)(voice->frame - first);
            for (uint8_t channel = 0; channel < 2; channel++) {
                float first_sample = sample_at(clip, first, channel);
                float next_sample = sample_at(clip, next, channel);
                interleaved[output * 2 + channel] +=
                    (first_sample + (next_sample - first_sample) * fraction) *
                    voice->gain * (channel == 0 ? voice->pan_left : voice->pan_right);
            }
            voice->frame += voice->step;
            if (voice->fade_frames) {
                voice->gain += voice->fade_step;
                voice->fade_frames--;
                if (!voice->fade_frames || voice->gain <= 0.0f) {
                    voice->active = false;
                    break;
                }
            }
        }
        finished |= !voice->active;
    }
    for (size_t frame = 0; frame < frames * 2; frame++) {
        interleaved[frame] *= audio->mixer_muted ? 0.0f : audio->mixer_volume;
        if (interleaved[frame] > 1.0f)
            interleaved[frame] = 1.0f;
        if (interleaved[frame] < -1.0f)
            interleaved[frame] = -1.0f;
    }
    if (finished)
        wm_audio_retire_finished(audio);
}
