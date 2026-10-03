#include "audio_sequence_render_internal.h"

#include <math.h>
#include <stdlib.h>
#include <string.h>

static void update_envelope(SequenceVoice *voice, const SequenceTables *tables) {
    const uint8_t *envelope = voice->envelope;
    if (voice->envelope_state == 0) {
        for (size_t millisecond = 0; millisecond < 3; millisecond++) {
            voice->envelope_level *= tables->attack[envelope[0]];
            if (voice->envelope_level > -1.0f / 32.0f) {
                voice->envelope_level = 0;
                voice->envelope_state = 1;
            }
        }
        /* Attack consumes the block's elapsed time. The native decay path
         * still compares sustain at that boundary without advancing decay. */
        if (voice->envelope_state == 1 &&
            voice->envelope_level <= tables->sustain[envelope[2]]) {
            voice->envelope_level = tables->sustain[envelope[2]];
            voice->envelope_state = 2;
        }
    } else if (voice->envelope_state == 1) {
        voice->envelope_level -= wm_sequence_release_rate(envelope[1]) * 3.0f;
        if (voice->envelope_level <= tables->sustain[envelope[2]]) {
            voice->envelope_level = tables->sustain[envelope[2]];
            voice->envelope_state = 2;
        }
    } else if (voice->envelope_state == 3) {
        voice->envelope_level -= wm_sequence_release_rate(envelope[3]) * 3.0f;
    }
}

static float envelope_gain(const SequenceVoice *voice, const SequenceTables *tables) {
    bool instant =
        voice->envelope_state == 0 && tables->attack[voice->envelope[0]] == 0;
    float decibels = instant ? 0 : voice->envelope_level / 10.0f;
    if (decibels < -90.4f)
        decibels = -90.4f;
    if (decibels > 6.0f)
        decibels = 6.0f;
    int index = 904 + (int)(decibels * 10.0f);
    if (index < 0)
        index = 0;
    if (index > 964)
        index = 964;
    return tables->decibels[index];
}

static int volume_coefficient(float gain) {
    if (gain < 0)
        gain = 0;
    if (gain > 1)
        gain = 1;
    return (int)truncf(gain * 32767.0f);
}

static int send_coefficient(float gain) {
    if (gain < 0)
        gain = 0;
    float scaled = truncf(gain * 32768.0f);
    if (scaled > 65535.0f)
        return 65535;
    return (int)scaled;
}

static int multiply_pcm_volume(double sample, int coefficient) {
    return (int)floor(sample * coefficient / 32768.0);
}

static int pan_table_index(float pan) {
    if (pan < -1)
        pan = -1;
    if (pan > 1)
        pan = 1;
    int index = (int)floorf((pan + 1.0f) * 128.0f + 0.5f);
    if (index < 0)
        index = 0;
    if (index > 256)
        index = 256;
    return index;
}

void wm_sequence_voice_render(SequencePlayer *player, SequenceVoice *voice) {
    if (player->elapsed_ticks > 0 && player->elapsed_ticks - 1 >= voice->release_tick) {
        voice->envelope_state = 3;
    }
    const SequenceTrack *track = &player->tracks[voice->track];
    float volume = (float)track->volume / 127.0f;
    float volume2 = (float)track->volume2 / 127.0f;
    float main_volume = (float)player->main_volume / 127.0f;
    float gain = voice->initial_gain * volume * volume * volume2 * volume2 *
                 main_volume * main_volume * player->gain;
    float initial_gain = voice->has_previous_gain ? voice->previous_gain : gain;
    int initial =
        volume_coefficient(initial_gain * envelope_gain(voice, player->tables));
    update_envelope(voice, player->tables);
    int target = volume_coefficient(gain * envelope_gain(voice, player->tables));
    int delta = (target - initial) / WM_SEQUENCE_BLOCK;
    voice->previous_gain = gain;
    voice->has_previous_gain = true;

    float pan =
        ((float)voice->instrument->pan - 64.0f + (float)track->pan - 64.0f) / 63.0f;
    float main_send = (float)track->main_send / 127.0f;
    float aux_send = (float)track->aux_a / 127.0f;
    const WmAudioPcm *wave = voice->wave;
    int main_left[2], main_right[2], aux_left[2], aux_right[2];
    size_t source_channels = wave->channels == 2 ? 2 : 1;
    for (size_t channel = 0; channel < source_channels; channel++) {
        /* Stereo sources begin at opposite pan endpoints. Apply the shared
         * pan before clamping each source so panning can crossfeed it. */
        float source_pan = pan;
        if (source_channels == 2)
            source_pan += channel == 0 ? -1.0f : 1.0f;
        int index = pan_table_index(source_pan);
        main_left[channel] = send_coefficient(player->tables->pan[index] * main_send);
        main_right[channel] =
            send_coefficient(player->tables->pan[256 - index] * main_send);
        aux_left[channel] = send_coefficient(player->tables->pan[index] * aux_send);
        aux_right[channel] =
            send_coefficient(player->tables->pan[256 - index] * aux_send);
    }
    for (size_t frame = 0; frame < WM_SEQUENCE_BLOCK; frame++) {
        if (voice->position >= wave->frame_count)
            break;
        size_t low = (size_t)voice->position;
        size_t next = low + 1 < wave->frame_count ? low + 1 : low;
        double fraction = voice->position - (double)low;
        double source_left = wave->samples[low * wave->channels] +
                             (wave->samples[next * wave->channels] -
                              wave->samples[low * wave->channels]) *
                                 fraction;
        double source_right;
        if (wave->channels == 2) {
            source_right =
                wave->samples[low * 2 + 1] +
                (wave->samples[next * 2 + 1] - wave->samples[low * 2 + 1]) * fraction;
        } else {
            source_right = source_left;
        }
        int envelope = initial + (int)frame * delta;
        int left = multiply_pcm_volume(source_left, envelope);
        int right = multiply_pcm_volume(source_right, envelope);
        int output_left = multiply_pcm_volume(left, main_left[0]);
        int output_right = multiply_pcm_volume(left, main_right[0]);
        int output_aux_left = multiply_pcm_volume(left, aux_left[0]);
        int output_aux_right = multiply_pcm_volume(left, aux_right[0]);
        if (source_channels == 2) {
            output_left += multiply_pcm_volume(right, main_left[1]);
            output_right += multiply_pcm_volume(right, main_right[1]);
            output_aux_left += multiply_pcm_volume(right, aux_left[1]);
            output_aux_right += multiply_pcm_volume(right, aux_right[1]);
        }
        player->block_left[frame] += (float)output_left / 32768.0f;
        player->block_right[frame] += (float)output_right / 32768.0f;
        player->aux_left[frame] += (float)output_aux_left / 32768.0f;
        player->aux_right[frame] += (float)output_aux_right / 32768.0f;
        voice->position += voice->speed;
    }
}

static bool allocate_delay(DelayLine *line, uint32_t length) {
    line->samples = calloc(length, sizeof(*line->samples));
    line->length = length;
    return line->samples != NULL;
}

void wm_sequence_reverb_free(SequenceReverb *reverb) {
    for (size_t channel = 0; channel < 2; channel++) {
        ReverbChannel *state = &reverb->channels[channel];
        for (size_t index = 0; index < 3; index++) {
            free(state->comb[index].samples);
        }
        for (size_t index = 0; index < 2; index++) {
            free(state->all_pass[index].samples);
        }
        free(state->final.samples);
    }
    memset(reverb, 0, sizeof(*reverb));
}

bool wm_sequence_reverb_initialize(SequenceReverb *reverb, const SequenceTables *tables,
                                   bool enabled) {
    memset(reverb, 0, sizeof(*reverb));
    reverb->enabled = enabled;
    if (!enabled)
        return true;
    const float *preset = tables->reverb_preset;
    reverb->coloration = preset[2];
    reverb->low_pass = fminf(0.95f, 1.0f - preset[3]);
    reverb->output_gain = 0.6f * preset[5];
    float denominator = preset[1] * WM_SEQUENCE_RATE;
    for (size_t index = 0; index < 3; index++) {
        reverb->comb_gain[index] =
            powf(10.0f, (float)tables->reverb_frames[index] * -3.0f / denominator);
    }
    for (size_t channel = 0; channel < 2; channel++) {
        ReverbChannel *state = &reverb->channels[channel];
        for (size_t index = 0; index < 3; index++) {
            if (!allocate_delay(&state->comb[index], tables->reverb_frames[index]))
                goto failed;
        }
        for (size_t index = 0; index < 2; index++) {
            if (!allocate_delay(&state->all_pass[index],
                                tables->reverb_frames[3 + index]))
                goto failed;
        }
        if (!allocate_delay(&state->final, tables->reverb_frames[5 + channel]))
            goto failed;
    }
    return true;

failed:
    wm_sequence_reverb_free(reverb);
    return false;
}

static float all_pass(DelayLine *line, float input, float coefficient) {
    float delayed = line->samples[line->position];
    float value = input + delayed * coefficient;
    line->samples[line->position] = value;
    line->position = (line->position + 1) % line->length;
    return delayed - value * coefficient;
}

static float process_reverb(SequenceReverb *reverb, uint8_t channel, float input) {
    ReverbChannel *state = &reverb->channels[channel];
    float value = 0;
    for (size_t index = 0; index < 3; index++) {
        DelayLine *line = &state->comb[index];
        float delayed = line->samples[line->position];
        value += delayed;
        line->samples[line->position] = input + delayed * reverb->comb_gain[index];
        line->position = (line->position + 1) % line->length;
    }
    for (size_t index = 0; index < 2; index++) {
        value = all_pass(&state->all_pass[index], value, reverb->coloration);
    }
    value = (1.0f - reverb->low_pass) * value + reverb->low_pass * state->last;
    state->last = value;
    value = all_pass(&state->final, value, reverb->coloration);
    return truncf(value * reverb->output_gain * 32768.0f) / 32768.0f;
}

void wm_sequence_reverb_apply(SequenceReverb *reverb, SequencePlayer *player) {
    if (!reverb->enabled)
        return;
    for (size_t frame = 0; frame < WM_SEQUENCE_BLOCK; frame++) {
        size_t position = reverb->aux_return_position;
        player->block_left[frame] += reverb->aux_return[0][position];
        player->block_right[frame] += reverb->aux_return[1][position];
        reverb->aux_return[0][position] =
            process_reverb(reverb, 0, player->aux_left[frame]);
        reverb->aux_return[1][position] =
            process_reverb(reverb, 1, player->aux_right[frame]);
        reverb->aux_return_position = (position + 1) % (WM_SEQUENCE_BLOCK * 2);
    }
}
