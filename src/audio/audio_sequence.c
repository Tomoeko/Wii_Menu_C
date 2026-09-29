#include "audio_sequence_render_internal.h"
#include "wii_menu/support/error.h"

#include <math.h>
#include <stdlib.h>
#include <string.h>

enum {
    WM_SEQUENCE_MAX_TICK = 1000000,
    WM_SEQUENCE_MAX_FRAMES = WM_SEQUENCE_RATE * 600
};

static void release_waves(SequenceWave *waves, size_t count) {
    for (size_t index = 0; index < count; index++) {
        wm_audio_pcm_free(&waves[index].pcm);
    }
}

static bool
render_original_loop_wave(const WmRsar *archive, const WmRsarSequence *sequence,
                          const WmRsarSound *sound, const WmSequenceTimeline *timeline,
                          WmAudioPcm *output, char *error, size_t error_capacity) {
    if (timeline->count != 1 || timeline->events[0].kind != WM_SEQUENCE_NOTE ||
        timeline->events[0].tick != 0 || timeline->events[0].length != 0)
        return false;
    const WmSequenceEvent *event = &timeline->events[0];
    WmRsarInstrument instrument;
    if (!wm_rsar_get_instrument(archive, sequence->bank_index, event->program,
                                event->key, event->velocity, &instrument, error,
                                error_capacity))
        return false;
    if (instrument.pitch != 1.0f || event->key != instrument.root_key)
        return false;
    WmAudioPcm wave;
    if (!wm_rsar_decode_bank_wave(archive, sequence->bank_index, instrument.wave_index,
                                  &wave, error, error_capacity))
        return false;
    if (!wave.looping) {
        wm_audio_pcm_free(&wave);
        return false;
    }
    /* The drag cue uses a looping sample. Bake archive gain into its PCM
     * so the manifest can play it at unity gain. */
    float gain = (float)sound->volume / 127.0f;
    size_t sample_count = (size_t)wave.frame_count * wave.channels;
    for (size_t index = 0; index < sample_count; index++) {
        float scaled = roundf((float)wave.samples[index] * gain);
        if (scaled < -32768)
            scaled = -32768;
        if (scaled > 32767)
            scaled = 32767;
        wave.samples[index] = (int16_t)scaled;
    }
    *output = wave;
    return true;
}

static bool prepare_notes(const WmRsar *archive, uint32_t bank_index,
                          const WmSequenceTimeline *timeline,
                          PreparedNote **prepared_output,
                          SequenceWave waves[WM_SEQUENCE_MAX_WAVES], size_t *wave_count,
                          char *error, size_t error_capacity) {
    PreparedNote *prepared = calloc(timeline->count, sizeof(*prepared));
    if (!prepared) {
        wm_error_set(error, error_capacity, "Out of memory preparing notes.");
        return false;
    }
    for (size_t index = 0; index < timeline->count; index++) {
        const WmSequenceEvent *event = &timeline->events[index];
        if (event->kind != WM_SEQUENCE_NOTE)
            continue;
        if (!wm_rsar_get_instrument(archive, bank_index, event->program, event->key,
                                    event->velocity, &prepared[index].instrument, error,
                                    error_capacity))
            goto failed;
        const WmRsarInstrument *instrument = &prepared[index].instrument;
        size_t slot = 0;
        while (slot < *wave_count && waves[slot].index != instrument->wave_index)
            slot++;
        if (slot == *wave_count) {
            if (*wave_count >= WM_SEQUENCE_MAX_WAVES ||
                !wm_rsar_decode_bank_wave(archive, bank_index, instrument->wave_index,
                                          &waves[slot].pcm, error, error_capacity))
                goto failed;
            if (waves[slot].pcm.looping) {
                wm_error_set(error, error_capacity,
                             "Looping bank waves need an audited voice path.");
                wm_audio_pcm_free(&waves[slot].pcm);
                goto failed;
            }
            waves[slot].index = instrument->wave_index;
            (*wave_count)++;
        }
        prepared[index].wave_slot = (uint16_t)slot;
    }
    *prepared_output = prepared;
    return true;

failed:
    free(prepared);
    release_waves(waves, *wave_count);
    *wave_count = 0;
    return false;
}

static void initialize_player(SequencePlayer *player,
                              const WmSequenceTimeline *timeline,
                              const PreparedNote *prepared, const SequenceWave *waves,
                              const SequenceTables *tables, const WmRsarSound *sound) {
    memset(player, 0, sizeof(*player));
    player->timeline = timeline;
    player->prepared = prepared;
    player->waves = waves;
    player->tables = tables;
    player->tempo = 120;
    player->tempo_counter = 416;
    player->main_volume = 127;
    player->gain = (float)sound->volume / 127.0f;
    for (size_t track = 0; track < 16; track++) {
        SequenceTrack *state = &player->tracks[track];
        state->volume = 127;
        state->volume2 = 127;
        state->pan = 64;
        state->main_send = 127;
        for (size_t envelope = 0; envelope < 4; envelope++) {
            state->envelope[envelope] = -1;
        }
    }
    for (size_t index = 0; index < timeline->count; index++) {
        if (timeline->events[index].tick >= timeline->loop_start_tick) {
            player->loop_event_index = index;
            break;
        }
    }
    if (timeline->voice_wait_loop) {
        for (size_t index = 0; index < timeline->count; index++) {
            if (timeline->events[index].order == timeline->loop_start_event_order) {
                player->voice_loop_event_index = index;
                break;
            }
        }
    }
}

static double note_step(const WmAudioPcm *wave, const WmRsarInstrument *instrument,
                        uint8_t key) {
    float semitones = (float)((int)key - (int)instrument->root_key) / 12.0f;
    float pitch = instrument->pitch * powf(2.0f, semitones);
    float ratio = (pitch * (float)wave->sample_rate) / (float)WM_SEQUENCE_RATE;
    if (!isfinite(ratio) || ratio <= 0)
        return 0;
    float fixed = truncf(ratio * 65536.0f);
    if (fixed > 4294967295.0f)
        fixed = 4294967295.0f;
    return (double)fixed / 65536.0;
}

static float note_gain(uint8_t velocity, uint8_t instrument_volume) {
    float velocity_ratio = (float)velocity / 127.0f;
    float instrument_ratio = (float)instrument_volume / 127.0f;
    return (velocity_ratio * velocity_ratio) * instrument_ratio;
}

static bool dispatch_event(SequencePlayer *player, size_t index) {
    const WmSequenceEvent *event = &player->timeline->events[index];
    SequenceTrack *track = &player->tracks[event->track];
    uint8_t value = (uint8_t)event->value;
    switch ((WmSequenceEventKind)event->kind) {
        case WM_SEQUENCE_TEMPO:
            player->tempo = event->value;
            break;
        case WM_SEQUENCE_VOLUME:
            track->volume = value;
            break;
        case WM_SEQUENCE_VOLUME2:
            track->volume2 = value;
            break;
        case WM_SEQUENCE_MAIN_VOLUME:
            player->main_volume = value;
            break;
        case WM_SEQUENCE_PAN:
            track->pan = value;
            break;
        case WM_SEQUENCE_MAIN_SEND:
            track->main_send = value;
            break;
        case WM_SEQUENCE_AUX_A:
            track->aux_a = value;
            break;
        case WM_SEQUENCE_AUX_B:
            track->aux_b = value;
            break;
        case WM_SEQUENCE_AUX_C:
            track->aux_c = value;
            break;
        case WM_SEQUENCE_ATTACK:
            track->envelope[0] = value;
            break;
        case WM_SEQUENCE_DECAY:
            track->envelope[1] = value;
            break;
        case WM_SEQUENCE_SUSTAIN:
            track->envelope[2] = value;
            break;
        case WM_SEQUENCE_RELEASE:
            track->envelope[3] = value;
            break;
        case WM_SEQUENCE_NOTE: {
            if (player->voice_count >= WM_SEQUENCE_MAX_VOICES)
                return false;
            if (player->timeline->voice_wait_loop &&
                index == player->voice_loop_event_index) {
                player->voice_loop_start_frame = player->block_position;
                player->voice_loop_started = true;
            }
            const PreparedNote *note = &player->prepared[index];
            SequenceVoice *voice = &player->voices[player->voice_count++];
            memset(voice, 0, sizeof(*voice));
            voice->instrument = &note->instrument;
            voice->wave = &player->waves[note->wave_slot].pcm;
            voice->track = event->track;
            voice->speed = note_step(voice->wave, voice->instrument, event->key);
            if (voice->speed <= 0)
                return false;
            voice->initial_gain = note_gain(event->velocity, voice->instrument->volume);
            voice->release_tick =
                event->length ? player->elapsed_ticks + event->length : UINT32_MAX;
            voice->envelope_level = -904;
            for (size_t envelope = 0; envelope < 4; envelope++) {
                voice->envelope[envelope] = track->envelope[envelope] >= 0
                                                ? (uint8_t)track->envelope[envelope]
                                                : voice->instrument->envelope[envelope];
            }
            break;
        }
        default:
            return false;
    }
    return true;
}

static bool has_track_voice(const SequencePlayer *player, uint8_t track) {
    for (size_t index = 0; index < player->voice_count; index++) {
        if (player->voices[index].track == track)
            return true;
    }
    /* SeqTrack checks completions before the sound thread retires the AX
     * notification for this block. Equality still means one more wait. */
    return player->pending_until[track] != 0 &&
           player->pending_until[track] >= player->block_position;
}

static bool dispatch_tick(SequencePlayer *player) {
    const WmSequenceTimeline *timeline = player->timeline;
    if (timeline->has_wait_for_end) {
        for (uint8_t track = 0; track < 16; track++) {
            SequenceTrack *state = &player->tracks[track];
            if (state->waiting_for_end) {
                if (has_track_voice(player, track))
                    continue;
                state->waiting_for_end = false;
            }
            while (state->event_index < timeline->count) {
                size_t index = state->event_index;
                const WmSequenceEvent *event = &timeline->events[index];
                state->event_index++;
                if (event->track != track)
                    continue;
                if (event->tick != state->tick) {
                    state->event_index = index;
                    break;
                }
                if (!dispatch_event(player, index))
                    return false;
                if (event->wait_for_end) {
                    state->waiting_for_end = true;
                    break;
                }
            }
            if (!state->waiting_for_end)
                state->tick++;
        }
    } else {
        while (player->event_index < timeline->count &&
               timeline->events[player->event_index].tick == player->song_tick) {
            if (!dispatch_event(player, player->event_index++))
                return false;
        }
        if (timeline->looping && player->song_tick == timeline->loop_end_tick) {
            player->song_tick = timeline->loop_start_tick;
            player->event_index = player->loop_event_index;
            while (player->event_index < timeline->count &&
                   timeline->events[player->event_index].tick == player->song_tick) {
                if (!dispatch_event(player, player->event_index++))
                    return false;
            }
        }
        player->song_tick++;
    }
    player->elapsed_ticks++;
    return true;
}

static bool render_block(SequencePlayer *player) {
    uint32_t due = player->tempo_counter / 416;
    player->tempo_counter = player->tempo_counter % 416 + player->tempo;
    for (uint32_t tick = 0; tick < due; tick++) {
        if (!dispatch_tick(player))
            return false;
    }
    memset(player->block_left, 0, sizeof(player->block_left));
    memset(player->block_right, 0, sizeof(player->block_right));
    memset(player->aux_left, 0, sizeof(player->aux_left));
    memset(player->aux_right, 0, sizeof(player->aux_right));
    for (size_t index = 0; index < player->voice_count; index++) {
        wm_sequence_voice_render(player, &player->voices[index]);
    }
    size_t retained = 0;
    for (size_t index = 0; index < player->voice_count; index++) {
        SequenceVoice *voice = &player->voices[index];
        if (voice->position < voice->wave->frame_count &&
            voice->envelope_level > -904) {
            player->voices[retained++] = *voice;
        } else if (player->timeline->has_wait_for_end && voice->envelope_level > -904) {
            player->pending_until[voice->track] =
                player->block_position + WM_SEQUENCE_BLOCK * 2;
        }
    }
    player->voice_count = retained;
    player->block_position += WM_SEQUENCE_BLOCK;
    return true;
}

static bool tick_positions(const WmSequenceTimeline *timeline, uint32_t loop_end_tick,
                           uint32_t final_tick, uint32_t *loop_frame,
                           uint32_t *final_frame) {
    uint32_t counter = 416;
    uint32_t tempo = 120;
    uint32_t tick = 0;
    size_t event = 0;
    for (uint32_t block = 0; block < WM_SEQUENCE_MAX_FRAMES / WM_SEQUENCE_BLOCK;
         block++) {
        uint32_t due = counter / 416;
        counter = counter % 416 + tempo;
        while (due--) {
            if (tick == loop_end_tick)
                *loop_frame = block * WM_SEQUENCE_BLOCK;
            if (tick == final_tick) {
                *final_frame = block * WM_SEQUENCE_BLOCK;
                return true;
            }
            while (event < timeline->count && timeline->events[event].tick == tick) {
                if (timeline->events[event].kind == WM_SEQUENCE_TEMPO) {
                    tempo = timeline->events[event].value;
                }
                event++;
            }
            tick++;
        }
    }
    return false;
}

static bool finished(const SequencePlayer *player) {
    if (player->voice_count)
        return false;
    if (!player->timeline->has_wait_for_end) {
        return player->event_index >= player->timeline->count;
    }
    for (uint8_t track = 0; track < 16; track++) {
        if (player->tracks[track].waiting_for_end ||
            player->pending_until[track] > player->block_position)
            return false;
        for (size_t index = player->tracks[track].event_index;
             index < player->timeline->count; index++) {
            if (player->timeline->events[index].track == track)
                return false;
        }
    }
    return true;
}

static int16_t quantize_sample(float value) {
    double scaled = round((double)value * 32768.0);
    if (scaled < -32768)
        scaled = -32768;
    if (scaled > 32767)
        scaled = 32767;
    return (int16_t)scaled;
}

static bool reserve_pcm(int16_t **samples, size_t *capacity, size_t count) {
    if (count <= *capacity)
        return true;
    size_t next = *capacity ? *capacity : 8192;
    while (next < count) {
        if (next >= WM_SEQUENCE_MAX_FRAMES / 2) {
            next = WM_SEQUENCE_MAX_FRAMES;
            break;
        }
        next *= 2;
    }
    if (next < count || next > WM_SEQUENCE_MAX_FRAMES ||
        next > SIZE_MAX / (2 * sizeof(**samples)))
        return false;
    int16_t *grown = realloc(*samples, next * 2 * sizeof(**samples));
    if (!grown)
        return false;
    *samples = grown;
    *capacity = next;
    return true;
}

bool wm_sequence_render(const WmRsar *archive, const WmRsarSound *sound,
                        const uint8_t *system_menu_dol, size_t dol_size,
                        WmAudioPcm *output, char *error, size_t error_capacity) {
    wm_error_set(error, error_capacity, "");
    if (!output)
        return false;
    *output = (WmAudioPcm){0};
    WmRsarSequence sequence;
    WmSequenceTimeline timeline;
    SequenceTables tables;
    if (!wm_rsar_get_sequence(archive, sound, &sequence, error, error_capacity))
        return false;
    if (!wm_sequence_parse(&sequence, &timeline, error, error_capacity))
        return false;
    if (render_original_loop_wave(archive, &sequence, sound, &timeline, output, error,
                                  error_capacity)) {
        wm_sequence_timeline_free(&timeline);
        return true;
    }
    wm_error_set(error, error_capacity, "");
    if (!wm_sequence_driver_load_tables(system_menu_dol, dol_size, &tables)) {
        wm_sequence_timeline_free(&timeline);
        wm_error_set(error, error_capacity,
                     "Matching USA 4.3 System Menu audio tables are unavailable.");
        return false;
    }
    if (timeline.looping && timeline.has_wait_for_end && !timeline.voice_wait_loop) {
        wm_sequence_timeline_free(&timeline);
        wm_error_set(error, error_capacity,
                     "Looping sequence with voice-finish waits is unsupported.");
        return false;
    }
    SequenceWave *waves = calloc(WM_SEQUENCE_MAX_WAVES, sizeof(*waves));
    PreparedNote *prepared = NULL;
    size_t wave_count = 0;
    if (!waves || !prepare_notes(archive, sequence.bank_index, &timeline, &prepared,
                                 waves, &wave_count, error, error_capacity)) {
        free(waves);
        wm_sequence_timeline_free(&timeline);
        return false;
    }
    bool has_aux = false;
    bool has_note = false;
    for (size_t index = 0; index < timeline.count; index++) {
        const WmSequenceEvent *event = &timeline.events[index];
        if (event->kind == WM_SEQUENCE_NOTE)
            has_note = true;
        if (event->kind == WM_SEQUENCE_AUX_A && event->value)
            has_aux = true;
    }
    if (!has_note) {
        wm_error_set(error, error_capacity, "Sequence contains no notes.");
        goto failed;
    }
    SequenceReverb reverb;
    if (!wm_sequence_reverb_initialize(&reverb, &tables, has_aux)) {
        wm_error_set(error, error_capacity,
                     "Out of memory initializing sequence reverb.");
        goto failed;
    }
    SequencePlayer *player = malloc(sizeof(*player));
    if (!player) {
        wm_sequence_reverb_free(&reverb);
        wm_error_set(error, error_capacity, "Out of memory creating sequence player.");
        goto failed;
    }
    initialize_player(player, &timeline, prepared, waves, &tables, sound);

    uint32_t loop_frame = 0;
    uint32_t final_frame = 0;
    if (timeline.looping && !timeline.voice_wait_loop) {
        uint32_t span = timeline.loop_end_tick - timeline.loop_start_tick;
        uint32_t target = timeline.loop_end_tick + span;
        if (timeline.loop_start_tick == 0 && span == 24 && player->tempo == 120) {
            /* Five repeats restore the 416-threshold clock phase for the
             * original dry reader movement cue. */
            target = 120;
        }
        if (target > WM_SEQUENCE_MAX_TICK ||
            !tick_positions(&timeline, timeline.loop_end_tick, target, &loop_frame,
                            &final_frame) ||
            !final_frame) {
            free(player);
            wm_sequence_reverb_free(&reverb);
            wm_error_set(error, error_capacity,
                         "Sequence loop exceeds the ten-minute render budget.");
            goto failed;
        }
        if (target == 120 && timeline.loop_start_tick == 0)
            loop_frame = 0;
    }

    int16_t *samples = NULL;
    size_t capacity = 0;
    size_t frame_count = 0;
    size_t last_audible = 0;
    size_t tail_remaining = 0;
    bool valid = true;
    for (size_t block = 0; block < WM_SEQUENCE_MAX_FRAMES / WM_SEQUENCE_BLOCK;
         block++) {
        if (timeline.looping && !timeline.voice_wait_loop && frame_count >= final_frame)
            break;
        if ((!timeline.looping || timeline.voice_wait_loop) && finished(player)) {
            if (!has_aux)
                break;
            if (!tail_remaining) {
                tail_remaining =
                    (size_t)ceilf(tables.reverb_preset[1] * 3.0f * WM_SEQUENCE_RATE) +
                    WM_SEQUENCE_BLOCK * 2;
            }
            if (tail_remaining == 0)
                break;
        }
        if (!reserve_pcm(&samples, &capacity, frame_count + WM_SEQUENCE_BLOCK) ||
            !render_block(player)) {
            valid = false;
            break;
        }
        wm_sequence_reverb_apply(&reverb, player);
        for (size_t frame = 0; frame < WM_SEQUENCE_BLOCK; frame++) {
            int16_t left = quantize_sample(player->block_left[frame]);
            int16_t right = quantize_sample(player->block_right[frame]);
            samples[(frame_count + frame) * 2] = left;
            samples[(frame_count + frame) * 2 + 1] = right;
            if (left || right)
                last_audible = frame_count + frame + 1;
        }
        frame_count += WM_SEQUENCE_BLOCK;
        if (tail_remaining) {
            tail_remaining = tail_remaining > WM_SEQUENCE_BLOCK
                                 ? tail_remaining - WM_SEQUENCE_BLOCK
                                 : 0;
            if (!tail_remaining)
                break;
        }
    }
    bool voice_loop_started = player->voice_loop_started;
    uint32_t voice_loop_start_frame = player->voice_loop_start_frame;
    free(player);
    wm_sequence_reverb_free(&reverb);
    if (!valid ||
        (timeline.looping && !timeline.voice_wait_loop && frame_count != final_frame) ||
        (!timeline.looping && frame_count >= WM_SEQUENCE_MAX_FRAMES)) {
        free(samples);
        wm_error_set(error, error_capacity,
                     "Sequence voice or PCM allocation budget exceeded.");
        goto failed;
    }
    if (!timeline.looping)
        frame_count = last_audible ? last_audible : 1;
    if (timeline.voice_wait_loop) {
        if (!voice_loop_started || voice_loop_start_frame >= frame_count) {
            free(samples);
            wm_error_set(error, error_capacity,
                         "Sequence voice-finish loop has no repeatable region.");
            goto failed;
        }
        loop_frame = voice_loop_start_frame;
    }
    *output = (WmAudioPcm){.samples = samples,
                           .sample_rate = WM_SEQUENCE_RATE,
                           .frame_count = (uint32_t)frame_count,
                           .loop_start = loop_frame,
                           .loop_end = (uint32_t)frame_count,
                           .channels = 2,
                           .looping = timeline.looping};
    release_waves(waves, wave_count);
    free(waves);
    free(prepared);
    wm_sequence_timeline_free(&timeline);
    return true;

failed:
    release_waves(waves, wave_count);
    free(waves);
    free(prepared);
    wm_sequence_timeline_free(&timeline);
    return false;
}
