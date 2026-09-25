#include "wii_menu/audio_sequence.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

enum {
    WM_SEQUENCE_RATE = 32000,
    WM_SEQUENCE_BLOCK = 96,
    WM_SEQUENCE_MAX_EVENTS = 100000,
    WM_SEQUENCE_MAX_TICK = 1000000,
    WM_SEQUENCE_MAX_FRAMES = WM_SEQUENCE_RATE * 600,
    WM_SEQUENCE_MAX_VOICES = 128,
    WM_SEQUENCE_MAX_WAVES = 256,
    WM_SEQUENCE_VISIT_SLOTS = 262144
};

typedef struct TrackStart {
    uint32_t offset;
    uint32_t tick;
    uint8_t track;
} TrackStart;

typedef struct Visit {
    uint32_t offset_plus_one;
    uint32_t tick;
    uint32_t variable_epoch;
    uint32_t event_order;
} Visit;

static void set_error(char *error, size_t capacity, const char *message)
{
    if (error && capacity) snprintf(error, capacity, "%s", message);
}

static bool has_bytes(const WmRsarSequence *sequence, size_t offset,
                      size_t length)
{
    return sequence && sequence->data && offset <= sequence->size &&
           length <= sequence->size - offset;
}

static bool take_byte(const WmRsarSequence *sequence, uint32_t *offset,
                      uint8_t *value)
{
    if (!has_bytes(sequence, *offset, 1)) return false;
    *value = sequence->data[(*offset)++];
    return true;
}

static bool take_variable(const WmRsarSequence *sequence, uint32_t *offset,
                          uint32_t *value)
{
    uint64_t result = 0;
    for (unsigned index = 0; index < 5; index++) {
        uint8_t byte;
        if (!take_byte(sequence, offset, &byte)) return false;
        result = result * 128 + (byte & 127);
        if (result > UINT32_MAX) return false;
        if (!(byte & 128)) {
            *value = (uint32_t)result;
            return true;
        }
    }
    return false;
}

static bool take_address(const WmRsarSequence *sequence, uint32_t *offset,
                         uint32_t *address)
{
    if (!has_bytes(sequence, *offset, 3)) return false;
    const uint8_t *bytes = sequence->data + *offset;
    *address = ((uint32_t)bytes[0] << 16) |
               ((uint32_t)bytes[1] << 8) | bytes[2];
    *offset += 3;
    return has_bytes(sequence, *address, 1);
}

static bool append_event(WmSequenceTimeline *timeline, size_t *capacity,
                         WmSequenceEvent event)
{
    if (timeline->count >= WM_SEQUENCE_MAX_EVENTS) return false;
    if (timeline->count == *capacity) {
        size_t next = *capacity ? *capacity * 2 : 256;
        if (next > WM_SEQUENCE_MAX_EVENTS) next = WM_SEQUENCE_MAX_EVENTS;
        WmSequenceEvent *events = realloc(timeline->events,
                                          next * sizeof(*events));
        if (!events) return false;
        timeline->events = events;
        *capacity = next;
    }
    event.order = (uint32_t)timeline->count;
    timeline->events[timeline->count++] = event;
    if (event.wait_for_end) timeline->has_wait_for_end = true;
    return true;
}

static bool visit_tick(Visit *visits, uint32_t offset, uint32_t tick,
                       uint32_t variable_epoch, uint32_t event_order)
{
    size_t index = ((size_t)offset * 2654435761u) &
                   (WM_SEQUENCE_VISIT_SLOTS - 1);
    for (size_t probe = 0; probe < WM_SEQUENCE_VISIT_SLOTS; probe++) {
        Visit *visit = &visits[index];
        if (visit->offset_plus_one == offset + 1) {
            return true;
        }
        if (!visit->offset_plus_one) {
            visit->offset_plus_one = offset + 1;
            visit->tick = tick;
            visit->variable_epoch = variable_epoch;
            visit->event_order = event_order;
            return true;
        }
        index = (index + 1) & (WM_SEQUENCE_VISIT_SLOTS - 1);
    }
    return false;
}

static bool lookup_visit(const Visit *visits, uint32_t offset,
                         Visit *result)
{
    size_t index = ((size_t)offset * 2654435761u) &
                   (WM_SEQUENCE_VISIT_SLOTS - 1);
    for (size_t probe = 0; probe < WM_SEQUENCE_VISIT_SLOTS; probe++) {
        const Visit *visit = &visits[index];
        if (visit->offset_plus_one == offset + 1) {
            *result = *visit;
            return true;
        }
        if (!visit->offset_plus_one) return false;
        index = (index + 1) & (WM_SEQUENCE_VISIT_SLOTS - 1);
    }
    return false;
}

static int compare_events(const void *left, const void *right)
{
    const WmSequenceEvent *a = left;
    const WmSequenceEvent *b = right;
    if (a->tick != b->tick) return a->tick < b->tick ? -1 : 1;
    if (a->track != b->track) return a->track < b->track ? -1 : 1;
    return a->order < b->order ? -1 : a->order > b->order;
}

static bool scan_track(const WmRsarSequence *sequence,
                       WmSequenceTimeline *timeline, size_t *capacity,
                       TrackStart start, TrackStart pending[16],
                       size_t *pending_count, bool seen_tracks[16],
                       char *error, size_t error_capacity)
{
    Visit *visits = calloc(WM_SEQUENCE_VISIT_SLOTS, sizeof(*visits));
    if (!visits) {
        set_error(error, error_capacity, "Out of memory scanning sequence.");
        return false;
    }
    uint32_t calls[16];
    unsigned call_count = 0;
    uint32_t offset = start.offset;
    uint32_t tick = start.tick;
    uint32_t program = 0;
    int transpose = 0;
    bool note_wait = true;
    uint32_t variable_epoch = 0;
    bool condition = false;
    int16_t variables[256] = {0};
    bool valid = true;
    bool terminated = false;
    for (size_t command_count = 0; command_count < WM_SEQUENCE_MAX_EVENTS;
         command_count++) {
        if (tick > WM_SEQUENCE_MAX_TICK || !has_bytes(sequence, offset, 1)) {
            set_error(error, error_capacity, "Sequence clock or address exceeds bounds.");
            valid = false;
            break;
        }
        if (!visit_tick(visits, offset, tick, variable_epoch,
                        (uint32_t)timeline->count)) {
            set_error(error, error_capacity, "Sequence command map is full.");
            valid = false;
            break;
        }
        uint8_t command;
        take_byte(sequence, &offset, &command);
        WmSequenceEvent event = {.tick = tick, .track = start.track};
        if (command < 0x80) {
            uint8_t velocity;
            uint32_t length;
            if (!take_byte(sequence, &offset, &velocity) ||
                !take_variable(sequence, &offset, &length) ||
                length > WM_SEQUENCE_MAX_TICK || velocity > 127) {
                valid = false;
                break;
            }
            int key = (int)command + transpose;
            if (key < 0) key = 0;
            if (key > 127) key = 127;
            event.kind = WM_SEQUENCE_NOTE;
            event.key = (uint8_t)key;
            event.velocity = velocity;
            event.length = length;
            event.program = program;
            event.wait_for_end = note_wait && length == 0;
            if (!append_event(timeline, capacity, event)) { valid = false; break; }
            if (note_wait) tick += length;
        } else if (command == 0x80) {
            uint32_t duration;
            if (!take_variable(sequence, &offset, &duration) ||
                duration > WM_SEQUENCE_MAX_TICK - tick) { valid = false; break; }
            tick += duration;
        } else if (command == 0x81) {
            if (!take_variable(sequence, &offset, &program) || program > 1024) {
                valid = false;
                break;
            }
        } else if (command == 0x88) {
            uint8_t track;
            uint32_t address;
            if (!take_byte(sequence, &offset, &track) ||
                !take_address(sequence, &offset, &address) || track >= 16 ||
                seen_tracks[track] || *pending_count >= 16) {
                valid = false;
                break;
            }
            seen_tracks[track] = true;
            pending[(*pending_count)++] = (TrackStart){address, tick, track};
        } else if (command == 0x89 || command == 0x8A) {
            uint32_t address;
            if (!take_address(sequence, &offset, &address)) { valid = false; break; }
            if (command == 0x8A) {
                if (call_count >= 16) { valid = false; break; }
                calls[call_count++] = offset;
                offset = address;
            } else {
                Visit earlier;
                if (lookup_visit(visits, address, &earlier)) {
                    if (variable_epoch != earlier.variable_epoch) {
                        offset = address;
                        continue;
                    }
                    if ((tick < earlier.tick ||
                         (tick == earlier.tick &&
                          !timeline->has_wait_for_end)) ||
                        (timeline->looping &&
                         (timeline->loop_start_tick != earlier.tick ||
                          timeline->loop_end_tick != tick))) {
                        valid = false;
                    } else {
                        timeline->looping = true;
                        timeline->loop_start_tick = earlier.tick;
                        timeline->loop_end_tick = tick;
                        timeline->voice_wait_loop = tick == earlier.tick;
                        timeline->loop_start_event_order =
                            earlier.event_order;
                    }
                    terminated = true;
                    break;
                }
                offset = address;
            }
        } else if (command == 0xfd) {
            if (call_count == 0) { terminated = true; break; }
            offset = calls[--call_count];
        } else if (command == 0xff) {
            terminated = true;
            break;
        } else if (command == 0xfe || command == 0xe0 || command == 0xe3) {
            /* The original parser accepts these two-byte control values. */
            if (!has_bytes(sequence, offset, 2)) { valid = false; break; }
            offset += 2;
        } else if (command == 0xc3) {
            uint8_t value;
            if (!take_byte(sequence, &offset, &value)) { valid = false; break; }
            transpose = (int)(int8_t)value;
        } else if (command == 0xe1) {
            uint8_t high, low;
            if (!take_byte(sequence, &offset, &high) ||
                !take_byte(sequence, &offset, &low)) { valid = false; break; }
            event.kind = WM_SEQUENCE_TEMPO;
            event.value = (uint16_t)(((uint16_t)high << 8) | low);
            if (!event.value || event.value > 1000 ||
                !append_event(timeline, capacity, event)) { valid = false; break; }
        } else if (command == 0xa0) {
            /* The HTML export uses the center of native randomized pitch
             * range for these cues. The five bytes include target and bounds;
             * random modulation is intentionally left centered here too. */
            if (!has_bytes(sequence, offset, 5) ||
                sequence->data[offset] != 0xc4) {
                valid = false;
                break;
            }
            offset += 5;
        } else if (command == 0xa1) {
            uint8_t target, variable;
            if (!take_byte(sequence, &offset, &target) ||
                !take_byte(sequence, &offset, &variable) ||
                target != 0xc1 || variables[variable] < 0 ||
                variables[variable] > 127) {
                valid = false;
                break;
            }
            event.kind = WM_SEQUENCE_VOLUME;
            event.value = (uint16_t)variables[variable];
            if (!append_event(timeline, capacity, event)) { valid = false; break; }
        } else if (command == 0xa2) {
            uint8_t target;
            uint32_t address;
            if (!take_byte(sequence, &offset, &target) || target != 0x89 ||
                !take_address(sequence, &offset, &address)) {
                valid = false;
                break;
            }
            if (condition) offset = address;
        } else if (command == 0xf0) {
            uint8_t operation, variable, high, low;
            if (!take_byte(sequence, &offset, &operation) ||
                !take_byte(sequence, &offset, &variable) ||
                !take_byte(sequence, &offset, &high) ||
                !take_byte(sequence, &offset, &low)) {
                valid = false;
                break;
            }
            int16_t operand = (int16_t)(((uint16_t)high << 8) | low);
            int32_t next = variables[variable];
            if (operation == 0x80) next = operand;
            else if (operation == 0x81) next += operand;
            else if (operation == 0x82) next -= operand;
            else if (operation == 0x92) condition = next > operand;
            else if (operation == 0x94) condition = next < operand;
            else {
                valid = false;
                break;
            }
            if (next < INT16_MIN || next > INT16_MAX) {
                valid = false;
                break;
            }
            variables[variable] = (int16_t)next;
            if (operation == 0x80 || operation == 0x81 ||
                operation == 0x82) variable_epoch++;
        } else {
            uint8_t value;
            if (!take_byte(sequence, &offset, &value)) { valid = false; break; }
            event.value = value;
            switch (command) {
                case 0xb0:
                    if (value != 48) valid = false;
                    break;
                case 0xc0: event.kind = WM_SEQUENCE_PAN; break;
                case 0xc1: event.kind = WM_SEQUENCE_VOLUME; break;
                case 0xc2: event.kind = WM_SEQUENCE_MAIN_VOLUME; break;
                case 0xc7: note_wait = value != 0; break;
                case 0xd0: event.kind = WM_SEQUENCE_ATTACK; break;
                case 0xd1: event.kind = WM_SEQUENCE_DECAY; break;
                case 0xd2: event.kind = WM_SEQUENCE_SUSTAIN; break;
                case 0xd3: event.kind = WM_SEQUENCE_RELEASE; break;
                case 0xd5: event.kind = WM_SEQUENCE_VOLUME2; break;
                case 0xd9: event.kind = WM_SEQUENCE_AUX_A; break;
                case 0xda: event.kind = WM_SEQUENCE_AUX_B; break;
                case 0xdb: event.kind = WM_SEQUENCE_MAIN_SEND; break;
                case 0xde: event.kind = WM_SEQUENCE_AUX_C; break;
                case 0xb1: case 0xc6: case 0xca: case 0xcb: case 0xcc:
                case 0xcd: case 0xd7: case 0xd8:
                    break;
                default:
                    valid = false;
                    break;
            }
            if (value > 127) valid = false;
            if (!valid) break;
            if ((command == 0xc0 || command == 0xc1 || command == 0xc2 ||
                 (command >= 0xd0 && command <= 0xd3) || command == 0xd5 ||
                 command == 0xd9 || command == 0xda || command == 0xdb ||
                 command == 0xde) &&
                !append_event(timeline, capacity, event)) { valid = false; break; }
        }
    }
    free(visits);
    if (valid && !terminated) valid = false;
    if (!valid && error && error_capacity && !error[0]) {
        snprintf(error, error_capacity,
                 "Unsupported or malformed RSEQ command near offset 0x%x.",
                 offset ? offset - 1 : 0);
    }
    return valid;
}

bool wm_sequence_parse(const WmRsarSequence *sequence,
                       WmSequenceTimeline *timeline,
                       char *error, size_t error_capacity)
{
    set_error(error, error_capacity, "");
    if (!timeline) return false;
    *timeline = (WmSequenceTimeline){0};
    if (!sequence || !has_bytes(sequence, sequence->start_offset, 1) ||
        sequence->size > 128u * 1024u * 1024u) {
        set_error(error, error_capacity, "Invalid RSEQ source span.");
        return false;
    }
    TrackStart pending[16] = {{sequence->start_offset, 0, 0}};
    bool seen_tracks[16] = {true};
    size_t pending_count = 1;
    size_t capacity = 0;
    for (size_t next = 0; next < pending_count; next++) {
        if (!scan_track(sequence, timeline, &capacity, pending[next],
                        pending, &pending_count, seen_tracks,
                        error, error_capacity)) {
            wm_sequence_timeline_free(timeline);
            return false;
        }
    }
    if (timeline->count == 0) {
        set_error(error, error_capacity, "Sequence has no playable events.");
        wm_sequence_timeline_free(timeline);
        return false;
    }
    qsort(timeline->events, timeline->count, sizeof(*timeline->events),
          compare_events);
    return true;
}

void wm_sequence_timeline_free(WmSequenceTimeline *timeline)
{
    if (!timeline) return;
    free(timeline->events);
    *timeline = (WmSequenceTimeline){0};
}

typedef struct SequenceTables {
    float attack[128];
    int16_t sustain[128];
    float decibels[965];
    float pan[257];
    uint32_t reverb_frames[8];
    float reverb_preset[6];
} SequenceTables;

typedef struct SequenceWave {
    uint32_t index;
    WmAudioPcm pcm;
} SequenceWave;

typedef struct PreparedNote {
    WmRsarInstrument instrument;
    uint16_t wave_slot;
} PreparedNote;

typedef struct SequenceTrack {
    uint8_t volume;
    uint8_t volume2;
    uint8_t pan;
    uint8_t main_send;
    uint8_t aux_a;
    uint8_t aux_b;
    uint8_t aux_c;
    int16_t envelope[4];
    size_t event_index;
    uint32_t tick;
    bool waiting_for_end;
} SequenceTrack;

typedef struct SequenceVoice {
    const WmRsarInstrument *instrument;
    const WmAudioPcm *wave;
    uint32_t release_tick;
    double position;
    double speed;
    float initial_gain;
    float previous_gain;
    float envelope_level;
    uint8_t envelope[4];
    uint8_t track;
    uint8_t envelope_state;
    bool has_previous_gain;
} SequenceVoice;

typedef struct SequencePlayer {
    const WmSequenceTimeline *timeline;
    const PreparedNote *prepared;
    const SequenceWave *waves;
    const SequenceTables *tables;
    SequenceTrack tracks[16];
    SequenceVoice voices[WM_SEQUENCE_MAX_VOICES];
    size_t voice_count;
    uint32_t pending_until[16];
    uint32_t song_tick;
    uint32_t elapsed_ticks;
    uint32_t tempo_counter;
    uint32_t tempo;
    uint32_t block_position;
    uint8_t main_volume;
    size_t event_index;
    size_t loop_event_index;
    size_t voice_loop_event_index;
    uint32_t voice_loop_start_frame;
    bool voice_loop_started;
    float gain;
    float block_left[WM_SEQUENCE_BLOCK];
    float block_right[WM_SEQUENCE_BLOCK];
    float aux_left[WM_SEQUENCE_BLOCK];
    float aux_right[WM_SEQUENCE_BLOCK];
} SequencePlayer;

static uint16_t big_u16(const uint8_t *data)
{
    return (uint16_t)(((uint16_t)data[0] << 8) | data[1]);
}

static uint32_t big_u32(const uint8_t *data)
{
    return ((uint32_t)data[0] << 24) | ((uint32_t)data[1] << 16) |
           ((uint32_t)data[2] << 8) | data[3];
}

static bool dol_range(const uint8_t *dol, size_t dol_size, uint32_t address,
                      size_t count, const uint8_t **bytes)
{
    if (!dol || dol_size < 0x100 || count > UINT32_MAX ||
        address > UINT32_MAX - count) return false;
    for (size_t section = 0; section < 18; section++) {
        uint32_t file_offset = big_u32(dol + section * 4);
        uint32_t start = big_u32(dol + 0x48 + section * 4);
        uint32_t size = big_u32(dol + 0x90 + section * 4);
        if (!size || file_offset < 0x100 ||
            file_offset > dol_size || size > dol_size - file_offset) continue;
        if (address >= start && address - start <= size &&
            count <= size - (address - start)) {
            *bytes = dol + file_offset + (address - start);
            return true;
        }
    }
    return false;
}

static float big_float(const uint8_t *data)
{
    uint32_t bits = big_u32(data);
    float number;
    memcpy(&number, &bits, sizeof(number));
    return number;
}

static bool load_tables(const uint8_t *dol, size_t dol_size,
                        SequenceTables *tables)
{
    const uint8_t *bytes;
    if (!dol_range(dol, dol_size, 0x8161e3f0u, sizeof(tables->attack),
                   &bytes)) return false;
    for (size_t index = 0; index < 128; index++) {
        tables->attack[index] = big_float(bytes + index * 4);
        if (!isfinite(tables->attack[index])) return false;
    }
    if (!dol_range(dol, dol_size, 0x8161e2f0u,
                   sizeof(tables->sustain), &bytes)) return false;
    for (size_t index = 0; index < 128; index++) {
        tables->sustain[index] = (int16_t)big_u16(bytes + index * 2);
    }
    if (!dol_range(dol, dol_size, 0x8161ead8u,
                   sizeof(tables->decibels), &bytes)) return false;
    for (size_t index = 0; index < 965; index++) {
        tables->decibels[index] = big_float(bytes + index * 4);
        if (!isfinite(tables->decibels[index])) return false;
    }
    if (!dol_range(dol, dol_size, 0x8161f9ecu,
                   sizeof(tables->pan), &bytes)) return false;
    for (size_t index = 0; index < 257; index++) {
        tables->pan[index] = big_float(bytes + index * 4);
        if (!isfinite(tables->pan[index])) return false;
    }
    if (!dol_range(dol, dol_size, 0x81685da0u,
                   sizeof(tables->reverb_frames), &bytes)) return false;
    for (size_t index = 0; index < 8; index++) {
        tables->reverb_frames[index] = big_u32(bytes + index * 4);
        if (!tables->reverb_frames[index] ||
            tables->reverb_frames[index] > WM_SEQUENCE_RATE) return false;
    }
    if (!dol_range(dol, dol_size, 0x8160f048u,
                   sizeof(tables->reverb_preset), &bytes)) return false;
    for (size_t index = 0; index < 6; index++) {
        tables->reverb_preset[index] = big_float(bytes + index * 4);
        if (!isfinite(tables->reverb_preset[index])) return false;
    }
    return tables->reverb_preset[0] == 0 &&
           tables->reverb_preset[1] > 0 &&
           tables->reverb_preset[1] <= 10 &&
           tables->reverb_preset[4] == 0;
}

static void release_waves(SequenceWave *waves, size_t count)
{
    for (size_t index = 0; index < count; index++) {
        wm_audio_pcm_free(&waves[index].pcm);
    }
}

static bool render_original_loop_wave(const WmRsar *archive,
                                      const WmRsarSequence *sequence,
                                      const WmRsarSound *sound,
                                      const WmSequenceTimeline *timeline,
                                      WmAudioPcm *output, char *error,
                                      size_t error_capacity)
{
    if (timeline->count != 1 ||
        timeline->events[0].kind != WM_SEQUENCE_NOTE ||
        timeline->events[0].tick != 0 ||
        timeline->events[0].length != 0) return false;
    const WmSequenceEvent *event = &timeline->events[0];
    WmRsarInstrument instrument;
    if (!wm_rsar_get_instrument(archive, sequence->bank_index,
                                event->program, event->key, event->velocity,
                                &instrument, error, error_capacity)) return false;
    if (instrument.pitch != 1.0f ||
        event->key != instrument.root_key) return false;
    WmAudioPcm wave;
    if (!wm_rsar_decode_bank_wave(archive, sequence->bank_index,
                                  instrument.wave_index, &wave, error,
                                  error_capacity)) return false;
    if (!wave.looping) {
        wm_audio_pcm_free(&wave);
        return false;
    }
    /* The HTML drag reader uses the original looping sample and applies
     * archive gain at playback. Bake that same gain for a unity-gain manifest. */
    float gain = (float)sound->volume / 127.0f;
    size_t sample_count = (size_t)wave.frame_count * wave.channels;
    for (size_t index = 0; index < sample_count; index++) {
        float scaled = roundf((float)wave.samples[index] * gain);
        if (scaled < -32768) scaled = -32768;
        if (scaled > 32767) scaled = 32767;
        wave.samples[index] = (int16_t)scaled;
    }
    *output = wave;
    return true;
}

static bool prepare_notes(const WmRsar *archive, uint32_t bank_index,
                          const WmSequenceTimeline *timeline,
                          PreparedNote **prepared_output,
                          SequenceWave waves[WM_SEQUENCE_MAX_WAVES],
                          size_t *wave_count, char *error,
                          size_t error_capacity)
{
    PreparedNote *prepared = calloc(timeline->count, sizeof(*prepared));
    if (!prepared) {
        set_error(error, error_capacity, "Out of memory preparing notes.");
        return false;
    }
    for (size_t index = 0; index < timeline->count; index++) {
        const WmSequenceEvent *event = &timeline->events[index];
        if (event->kind != WM_SEQUENCE_NOTE) continue;
        if (!wm_rsar_get_instrument(archive, bank_index, event->program,
                                    event->key, event->velocity,
                                    &prepared[index].instrument, error,
                                    error_capacity)) goto failed;
        const WmRsarInstrument *instrument = &prepared[index].instrument;
        size_t slot = 0;
        while (slot < *wave_count &&
               waves[slot].index != instrument->wave_index) slot++;
        if (slot == *wave_count) {
            if (*wave_count >= WM_SEQUENCE_MAX_WAVES ||
                !wm_rsar_decode_bank_wave(archive, bank_index,
                                          instrument->wave_index,
                                          &waves[slot].pcm, error,
                                          error_capacity)) goto failed;
            if (waves[slot].pcm.looping) {
                set_error(error, error_capacity,
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
                              const PreparedNote *prepared,
                              const SequenceWave *waves,
                              const SequenceTables *tables,
                              const WmRsarSound *sound)
{
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
            if (timeline->events[index].order ==
                timeline->loop_start_event_order) {
                player->voice_loop_event_index = index;
                break;
            }
        }
    }
}

static double note_step(const WmAudioPcm *wave,
                        const WmRsarInstrument *instrument,
                        uint8_t key)
{
    float semitones = (float)((int)key - (int)instrument->root_key) / 12.0f;
    float pitch = instrument->pitch * powf(2.0f, semitones);
    float ratio = (pitch * (float)wave->sample_rate) /
                  (float)WM_SEQUENCE_RATE;
    if (!isfinite(ratio) || ratio <= 0) return 0;
    float fixed = truncf(ratio * 65536.0f);
    if (fixed > 4294967295.0f) fixed = 4294967295.0f;
    return (double)fixed / 65536.0;
}

static float note_gain(uint8_t velocity, uint8_t instrument_volume)
{
    float velocity_ratio = (float)velocity / 127.0f;
    float instrument_ratio = (float)instrument_volume / 127.0f;
    return (velocity_ratio * velocity_ratio) * instrument_ratio;
}

static bool dispatch_event(SequencePlayer *player, size_t index)
{
    const WmSequenceEvent *event = &player->timeline->events[index];
    SequenceTrack *track = &player->tracks[event->track];
    uint8_t value = (uint8_t)event->value;
    switch ((WmSequenceEventKind)event->kind) {
        case WM_SEQUENCE_TEMPO: player->tempo = event->value; break;
        case WM_SEQUENCE_VOLUME: track->volume = value; break;
        case WM_SEQUENCE_VOLUME2: track->volume2 = value; break;
        case WM_SEQUENCE_MAIN_VOLUME: player->main_volume = value; break;
        case WM_SEQUENCE_PAN: track->pan = value; break;
        case WM_SEQUENCE_MAIN_SEND: track->main_send = value; break;
        case WM_SEQUENCE_AUX_A: track->aux_a = value; break;
        case WM_SEQUENCE_AUX_B: track->aux_b = value; break;
        case WM_SEQUENCE_AUX_C: track->aux_c = value; break;
        case WM_SEQUENCE_ATTACK: track->envelope[0] = value; break;
        case WM_SEQUENCE_DECAY: track->envelope[1] = value; break;
        case WM_SEQUENCE_SUSTAIN: track->envelope[2] = value; break;
        case WM_SEQUENCE_RELEASE: track->envelope[3] = value; break;
        case WM_SEQUENCE_NOTE: {
            if (player->voice_count >= WM_SEQUENCE_MAX_VOICES) return false;
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
            voice->speed = note_step(voice->wave, voice->instrument,
                                     event->key);
            if (voice->speed <= 0) return false;
            voice->initial_gain = note_gain(event->velocity,
                                            voice->instrument->volume);
            voice->release_tick = event->length
                                      ? player->elapsed_ticks + event->length
                                      : UINT32_MAX;
            voice->envelope_level = -904;
            for (size_t envelope = 0; envelope < 4; envelope++) {
                voice->envelope[envelope] =
                    track->envelope[envelope] >= 0
                        ? (uint8_t)track->envelope[envelope]
                        : voice->instrument->envelope[envelope];
            }
            break;
        }
        default: return false;
    }
    return true;
}

static bool has_track_voice(const SequencePlayer *player, uint8_t track)
{
    for (size_t index = 0; index < player->voice_count; index++) {
        if (player->voices[index].track == track) return true;
    }
    /* SeqTrack checks completions before the sound thread retires the AX
     * notification for this block. Equality still means one more wait. */
    return player->pending_until[track] != 0 &&
           player->pending_until[track] >= player->block_position;
}

static bool dispatch_tick(SequencePlayer *player)
{
    const WmSequenceTimeline *timeline = player->timeline;
    if (timeline->has_wait_for_end) {
        for (uint8_t track = 0; track < 16; track++) {
            SequenceTrack *state = &player->tracks[track];
            if (state->waiting_for_end) {
                if (has_track_voice(player, track)) continue;
                state->waiting_for_end = false;
            }
            while (state->event_index < timeline->count) {
                size_t index = state->event_index;
                const WmSequenceEvent *event = &timeline->events[index];
                state->event_index++;
                if (event->track != track) continue;
                if (event->tick != state->tick) {
                    state->event_index = index;
                    break;
                }
                if (!dispatch_event(player, index)) return false;
                if (event->wait_for_end) {
                    state->waiting_for_end = true;
                    break;
                }
            }
            if (!state->waiting_for_end) state->tick++;
        }
    } else {
        while (player->event_index < timeline->count &&
               timeline->events[player->event_index].tick ==
                   player->song_tick) {
            if (!dispatch_event(player, player->event_index++)) return false;
        }
        if (timeline->looping &&
            player->song_tick == timeline->loop_end_tick) {
            player->song_tick = timeline->loop_start_tick;
            player->event_index = player->loop_event_index;
            while (player->event_index < timeline->count &&
                   timeline->events[player->event_index].tick ==
                       player->song_tick) {
                if (!dispatch_event(player, player->event_index++)) return false;
            }
        }
        player->song_tick++;
    }
    player->elapsed_ticks++;
    return true;
}

static float release_rate(uint8_t value)
{
    if (value == 127) return 65535;
    if (value == 126) return 24;
    if (value < 50) return ((float)(value * 2 + 1) / 128.0f) / 5.0f;
    return (60.0f / (126.0f - (float)value)) / 5.0f;
}

static void update_envelope(SequenceVoice *voice,
                            const SequenceTables *tables)
{
    const uint8_t *envelope = voice->envelope;
    if (voice->envelope_state == 0) {
        for (size_t millisecond = 0; millisecond < 3; millisecond++) {
            voice->envelope_level *= tables->attack[envelope[0]];
            if (voice->envelope_level > -1.0f / 32.0f) {
                voice->envelope_level = 0;
                voice->envelope_state = 1;
            }
        }
    } else if (voice->envelope_state == 1) {
        voice->envelope_level -= release_rate(envelope[1]) * 3.0f;
        if (voice->envelope_level <= tables->sustain[envelope[2]]) {
            voice->envelope_level = tables->sustain[envelope[2]];
            voice->envelope_state = 2;
        }
    } else if (voice->envelope_state == 3) {
        voice->envelope_level -= release_rate(envelope[3]) * 3.0f;
    }
}

static float envelope_gain(const SequenceVoice *voice,
                           const SequenceTables *tables)
{
    bool instant = voice->envelope_state == 0 &&
                   tables->attack[voice->envelope[0]] == 0;
    float decibels = instant ? 0 : voice->envelope_level / 10.0f;
    if (decibels < -90.4f) decibels = -90.4f;
    if (decibels > 6.0f) decibels = 6.0f;
    int index = 904 + (int)(decibels * 10.0f);
    if (index < 0) index = 0;
    if (index > 964) index = 964;
    return tables->decibels[index];
}

static int volume_coefficient(float gain)
{
    if (gain < 0) gain = 0;
    if (gain > 1) gain = 1;
    return (int)truncf(gain * 32767.0f);
}

static int send_coefficient(float gain)
{
    if (gain < 0) gain = 0;
    float scaled = truncf(gain * 32768.0f);
    if (scaled > 65535.0f) return 65535;
    return (int)scaled;
}

static int multiply_pcm_volume(double sample, int coefficient)
{
    return (int)floor(sample * coefficient / 32768.0);
}

static void render_voice(SequencePlayer *player, SequenceVoice *voice)
{
    if (player->elapsed_ticks > 0 &&
        player->elapsed_ticks - 1 >= voice->release_tick) {
        voice->envelope_state = 3;
    }
    const SequenceTrack *track = &player->tracks[voice->track];
    float volume = (float)track->volume / 127.0f;
    float volume2 = (float)track->volume2 / 127.0f;
    float main_volume = (float)player->main_volume / 127.0f;
    float gain = voice->initial_gain * volume * volume *
                 volume2 * volume2 * main_volume * main_volume *
                 player->gain;
    float initial_gain = voice->has_previous_gain
                             ? voice->previous_gain : gain;
    int initial = volume_coefficient(initial_gain *
                                     envelope_gain(voice, player->tables));
    update_envelope(voice, player->tables);
    int target = volume_coefficient(gain *
                                    envelope_gain(voice, player->tables));
    int delta = (target - initial) / WM_SEQUENCE_BLOCK;
    voice->previous_gain = gain;
    voice->has_previous_gain = true;

    float pan = ((float)voice->instrument->pan - 64.0f +
                 (float)track->pan - 64.0f) / 63.0f;
    if (pan < -1) pan = -1;
    if (pan > 1) pan = 1;
    int pan_index = (int)floorf((pan + 1.0f) * 128.0f + 0.5f);
    if (pan_index < 0) pan_index = 0;
    if (pan_index > 256) pan_index = 256;
    float main_send = (float)track->main_send / 127.0f;
    float aux_send = (float)track->aux_a / 127.0f;
    int main_left = send_coefficient(player->tables->pan[pan_index] *
                                     main_send);
    int main_right = send_coefficient(player->tables->pan[256 - pan_index] *
                                      main_send);
    int aux_left = send_coefficient(player->tables->pan[pan_index] * aux_send);
    int aux_right = send_coefficient(player->tables->pan[256 - pan_index] *
                                     aux_send);
    const WmAudioPcm *wave = voice->wave;
    for (size_t frame = 0; frame < WM_SEQUENCE_BLOCK; frame++) {
        if (voice->position >= wave->frame_count) break;
        size_t low = (size_t)voice->position;
        size_t next = low + 1 < wave->frame_count ? low + 1 : low;
        double fraction = voice->position - (double)low;
        double source_left = wave->samples[low * wave->channels] +
            (wave->samples[next * wave->channels] -
             wave->samples[low * wave->channels]) * fraction;
        double source_right;
        if (wave->channels == 2) {
            source_right = wave->samples[low * 2 + 1] +
                (wave->samples[next * 2 + 1] -
                 wave->samples[low * 2 + 1]) * fraction;
        } else {
            source_right = source_left;
        }
        int envelope = initial + (int)frame * delta;
        int left = multiply_pcm_volume(source_left, envelope);
        int right = multiply_pcm_volume(source_right, envelope);
        player->block_left[frame] +=
            (float)multiply_pcm_volume(left, main_left) / 32768.0f;
        player->block_right[frame] +=
            (float)multiply_pcm_volume(right, main_right) / 32768.0f;
        player->aux_left[frame] +=
            (float)multiply_pcm_volume(left, aux_left) / 32768.0f;
        player->aux_right[frame] +=
            (float)multiply_pcm_volume(right, aux_right) / 32768.0f;
        voice->position += voice->speed;
    }
}

static bool render_block(SequencePlayer *player)
{
    uint32_t due = player->tempo_counter / 416;
    player->tempo_counter = player->tempo_counter % 416 + player->tempo;
    for (uint32_t tick = 0; tick < due; tick++) {
        if (!dispatch_tick(player)) return false;
    }
    memset(player->block_left, 0, sizeof(player->block_left));
    memset(player->block_right, 0, sizeof(player->block_right));
    memset(player->aux_left, 0, sizeof(player->aux_left));
    memset(player->aux_right, 0, sizeof(player->aux_right));
    for (size_t index = 0; index < player->voice_count; index++) {
        render_voice(player, &player->voices[index]);
    }
    size_t retained = 0;
    for (size_t index = 0; index < player->voice_count; index++) {
        SequenceVoice *voice = &player->voices[index];
        if (voice->position < voice->wave->frame_count &&
            voice->envelope_level > -904) {
            player->voices[retained++] = *voice;
        } else if (player->timeline->has_wait_for_end &&
                   voice->envelope_level > -904) {
            player->pending_until[voice->track] =
                player->block_position + WM_SEQUENCE_BLOCK * 2;
        }
    }
    player->voice_count = retained;
    player->block_position += WM_SEQUENCE_BLOCK;
    return true;
}

static bool tick_positions(const WmSequenceTimeline *timeline,
                           uint32_t loop_end_tick, uint32_t final_tick,
                           uint32_t *loop_frame, uint32_t *final_frame)
{
    uint32_t counter = 416;
    uint32_t tempo = 120;
    uint32_t tick = 0;
    size_t event = 0;
    for (uint32_t block = 0; block < WM_SEQUENCE_MAX_FRAMES /
                                     WM_SEQUENCE_BLOCK; block++) {
        uint32_t due = counter / 416;
        counter = counter % 416 + tempo;
        while (due--) {
            if (tick == loop_end_tick) *loop_frame = block * WM_SEQUENCE_BLOCK;
            if (tick == final_tick) {
                *final_frame = block * WM_SEQUENCE_BLOCK;
                return true;
            }
            while (event < timeline->count &&
                   timeline->events[event].tick == tick) {
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

static bool finished(const SequencePlayer *player)
{
    if (player->voice_count) return false;
    if (!player->timeline->has_wait_for_end) {
        return player->event_index >= player->timeline->count;
    }
    for (uint8_t track = 0; track < 16; track++) {
        if (player->tracks[track].waiting_for_end ||
            player->pending_until[track] > player->block_position) return false;
        for (size_t index = player->tracks[track].event_index;
             index < player->timeline->count; index++) {
            if (player->timeline->events[index].track == track) return false;
        }
    }
    return true;
}

static int16_t quantize_sample(float value)
{
    double scaled = round((double)value * 32768.0);
    if (scaled < -32768) scaled = -32768;
    if (scaled > 32767) scaled = 32767;
    return (int16_t)scaled;
}

typedef struct DelayLine {
    float *samples;
    uint32_t length;
    uint32_t position;
} DelayLine;

typedef struct ReverbChannel {
    DelayLine comb[3];
    DelayLine all_pass[2];
    DelayLine final;
    float last;
} ReverbChannel;

typedef struct SequenceReverb {
    ReverbChannel channels[2];
    float comb_gain[3];
    float coloration;
    float low_pass;
    float output_gain;
    float aux_return[2][WM_SEQUENCE_BLOCK * 2];
    size_t aux_return_position;
    bool enabled;
} SequenceReverb;

static bool allocate_delay(DelayLine *line, uint32_t length)
{
    line->samples = calloc(length, sizeof(*line->samples));
    line->length = length;
    return line->samples != NULL;
}

static void free_reverb(SequenceReverb *reverb)
{
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

static bool initialize_reverb(SequenceReverb *reverb,
                              const SequenceTables *tables,
                              bool enabled)
{
    memset(reverb, 0, sizeof(*reverb));
    reverb->enabled = enabled;
    if (!enabled) return true;
    const float *preset = tables->reverb_preset;
    reverb->coloration = preset[2];
    reverb->low_pass = fminf(0.95f, 1.0f - preset[3]);
    reverb->output_gain = 0.6f * preset[5];
    float denominator = preset[1] * WM_SEQUENCE_RATE;
    for (size_t index = 0; index < 3; index++) {
        reverb->comb_gain[index] = powf(10.0f,
            (float)tables->reverb_frames[index] * -3.0f / denominator);
    }
    for (size_t channel = 0; channel < 2; channel++) {
        ReverbChannel *state = &reverb->channels[channel];
        for (size_t index = 0; index < 3; index++) {
            if (!allocate_delay(&state->comb[index],
                                tables->reverb_frames[index])) goto failed;
        }
        for (size_t index = 0; index < 2; index++) {
            if (!allocate_delay(&state->all_pass[index],
                                tables->reverb_frames[3 + index])) goto failed;
        }
        if (!allocate_delay(&state->final,
                            tables->reverb_frames[5 + channel])) goto failed;
    }
    return true;

failed:
    free_reverb(reverb);
    return false;
}

static float all_pass(DelayLine *line, float input, float coefficient)
{
    float delayed = line->samples[line->position];
    float value = input + delayed * coefficient;
    line->samples[line->position] = value;
    line->position = (line->position + 1) % line->length;
    return delayed - value * coefficient;
}

static float process_reverb(SequenceReverb *reverb, uint8_t channel,
                            float input)
{
    ReverbChannel *state = &reverb->channels[channel];
    float value = 0;
    for (size_t index = 0; index < 3; index++) {
        DelayLine *line = &state->comb[index];
        float delayed = line->samples[line->position];
        value += delayed;
        line->samples[line->position] = input + delayed *
                                        reverb->comb_gain[index];
        line->position = (line->position + 1) % line->length;
    }
    for (size_t index = 0; index < 2; index++) {
        value = all_pass(&state->all_pass[index], value,
                         reverb->coloration);
    }
    value = (1.0f - reverb->low_pass) * value +
            reverb->low_pass * state->last;
    state->last = value;
    value = all_pass(&state->final, value, reverb->coloration);
    return truncf(value * reverb->output_gain * 32768.0f) / 32768.0f;
}

static void apply_reverb(SequenceReverb *reverb, SequencePlayer *player)
{
    if (!reverb->enabled) return;
    for (size_t frame = 0; frame < WM_SEQUENCE_BLOCK; frame++) {
        size_t position = reverb->aux_return_position;
        player->block_left[frame] += reverb->aux_return[0][position];
        player->block_right[frame] += reverb->aux_return[1][position];
        reverb->aux_return[0][position] =
            process_reverb(reverb, 0, player->aux_left[frame]);
        reverb->aux_return[1][position] =
            process_reverb(reverb, 1, player->aux_right[frame]);
        reverb->aux_return_position =
            (position + 1) % (WM_SEQUENCE_BLOCK * 2);
    }
}

static bool reserve_pcm(int16_t **samples, size_t *capacity, size_t count)
{
    if (count <= *capacity) return true;
    size_t next = *capacity ? *capacity : 8192;
    while (next < count) {
        if (next >= WM_SEQUENCE_MAX_FRAMES / 2) {
            next = WM_SEQUENCE_MAX_FRAMES;
            break;
        }
        next *= 2;
    }
    if (next < count || next > WM_SEQUENCE_MAX_FRAMES ||
        next > SIZE_MAX / (2 * sizeof(**samples))) return false;
    int16_t *grown = realloc(*samples, next * 2 * sizeof(**samples));
    if (!grown) return false;
    *samples = grown;
    *capacity = next;
    return true;
}

bool wm_sequence_render(const WmRsar *archive, const WmRsarSound *sound,
                        const uint8_t *system_menu_dol, size_t dol_size,
                        WmAudioPcm *output, char *error,
                        size_t error_capacity)
{
    set_error(error, error_capacity, "");
    if (!output) return false;
    *output = (WmAudioPcm){0};
    WmRsarSequence sequence;
    WmSequenceTimeline timeline;
    SequenceTables tables;
    if (!wm_rsar_get_sequence(archive, sound, &sequence, error,
                              error_capacity)) return false;
    if (!wm_sequence_parse(&sequence, &timeline, error,
                            error_capacity)) return false;
    if (render_original_loop_wave(archive, &sequence, sound, &timeline,
                                  output, error, error_capacity)) {
        wm_sequence_timeline_free(&timeline);
        return true;
    }
    set_error(error, error_capacity, "");
    if (!load_tables(system_menu_dol, dol_size, &tables)) {
        wm_sequence_timeline_free(&timeline);
        set_error(error, error_capacity,
                  "Matching USA 4.3 System Menu audio tables are unavailable.");
        return false;
    }
    if (timeline.looping && timeline.has_wait_for_end &&
        !timeline.voice_wait_loop) {
        wm_sequence_timeline_free(&timeline);
        set_error(error, error_capacity,
                  "Looping sequence with voice-finish waits is unsupported.");
        return false;
    }
    SequenceWave *waves = calloc(WM_SEQUENCE_MAX_WAVES, sizeof(*waves));
    PreparedNote *prepared = NULL;
    size_t wave_count = 0;
    if (!waves || !prepare_notes(archive, sequence.bank_index, &timeline,
                                 &prepared, waves, &wave_count, error,
                                 error_capacity)) {
        free(waves);
        wm_sequence_timeline_free(&timeline);
        return false;
    }
    bool has_aux = false;
    bool has_note = false;
    for (size_t index = 0; index < timeline.count; index++) {
        const WmSequenceEvent *event = &timeline.events[index];
        if (event->kind == WM_SEQUENCE_NOTE) has_note = true;
        if (event->kind == WM_SEQUENCE_AUX_A && event->value) has_aux = true;
    }
    if (!has_note) {
        set_error(error, error_capacity, "Sequence contains no notes.");
        goto failed;
    }
    SequenceReverb reverb;
    if (!initialize_reverb(&reverb, &tables, has_aux)) {
        set_error(error, error_capacity,
                  "Out of memory initializing sequence reverb.");
        goto failed;
    }
    SequencePlayer *player = malloc(sizeof(*player));
    if (!player) {
        free_reverb(&reverb);
        set_error(error, error_capacity, "Out of memory creating sequence player.");
        goto failed;
    }
    initialize_player(player, &timeline, prepared, waves, &tables, sound);

    uint32_t loop_frame = 0;
    uint32_t final_frame = 0;
    if (timeline.looping && !timeline.voice_wait_loop) {
        uint32_t span = timeline.loop_end_tick - timeline.loop_start_tick;
        uint32_t target = timeline.loop_end_tick + span;
        if (timeline.loop_start_tick == 0 && span == 24 &&
            player->tempo == 120) {
            /* Five repeats restore the 416-threshold clock phase for the
             * original dry reader movement cue. */
            target = 120;
        }
        if (target > WM_SEQUENCE_MAX_TICK ||
            !tick_positions(&timeline, timeline.loop_end_tick, target,
                            &loop_frame, &final_frame) || !final_frame) {
            free(player);
            free_reverb(&reverb);
            set_error(error, error_capacity,
                      "Sequence loop exceeds the ten-minute render budget.");
            goto failed;
        }
        if (target == 120 && timeline.loop_start_tick == 0) loop_frame = 0;
    }

    int16_t *samples = NULL;
    size_t capacity = 0;
    size_t frame_count = 0;
    size_t last_audible = 0;
    size_t tail_remaining = 0;
    bool valid = true;
    for (size_t block = 0; block < WM_SEQUENCE_MAX_FRAMES /
                                WM_SEQUENCE_BLOCK; block++) {
        if (timeline.looping && !timeline.voice_wait_loop &&
            frame_count >= final_frame) break;
        if ((!timeline.looping || timeline.voice_wait_loop) &&
            finished(player)) {
            if (!has_aux) break;
            if (!tail_remaining) {
                tail_remaining = (size_t)ceilf(
                    tables.reverb_preset[1] * 3.0f * WM_SEQUENCE_RATE) +
                    WM_SEQUENCE_BLOCK * 2;
            }
            if (tail_remaining == 0) break;
        }
        if (!reserve_pcm(&samples, &capacity,
                          frame_count + WM_SEQUENCE_BLOCK) ||
            !render_block(player)) {
            valid = false;
            break;
        }
        apply_reverb(&reverb, player);
        for (size_t frame = 0; frame < WM_SEQUENCE_BLOCK; frame++) {
            int16_t left = quantize_sample(player->block_left[frame]);
            int16_t right = quantize_sample(player->block_right[frame]);
            samples[(frame_count + frame) * 2] = left;
            samples[(frame_count + frame) * 2 + 1] = right;
            if (left || right) last_audible = frame_count + frame + 1;
        }
        frame_count += WM_SEQUENCE_BLOCK;
        if (tail_remaining) {
            tail_remaining = tail_remaining > WM_SEQUENCE_BLOCK
                                 ? tail_remaining - WM_SEQUENCE_BLOCK : 0;
            if (!tail_remaining) break;
        }
    }
    bool voice_loop_started = player->voice_loop_started;
    uint32_t voice_loop_start_frame = player->voice_loop_start_frame;
    free(player);
    free_reverb(&reverb);
    if (!valid || (timeline.looping && !timeline.voice_wait_loop &&
                   frame_count != final_frame) ||
        (!timeline.looping && frame_count >= WM_SEQUENCE_MAX_FRAMES)) {
        free(samples);
        set_error(error, error_capacity,
                  "Sequence voice or PCM allocation budget exceeded.");
        goto failed;
    }
    if (!timeline.looping) frame_count = last_audible ? last_audible : 1;
    if (timeline.voice_wait_loop) {
        if (!voice_loop_started || voice_loop_start_frame >= frame_count) {
            free(samples);
            set_error(error, error_capacity,
                      "Sequence voice-finish loop has no repeatable region.");
            goto failed;
        }
        loop_frame = voice_loop_start_frame;
    }
    *output = (WmAudioPcm){
        .samples = samples,
        .sample_rate = WM_SEQUENCE_RATE,
        .frame_count = (uint32_t)frame_count,
        .loop_start = loop_frame,
        .loop_end = (uint32_t)frame_count,
        .channels = 2,
        .looping = timeline.looping
    };
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
