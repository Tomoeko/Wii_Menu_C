#include "wii_menu/audio/audio_sequence.h"
#include "wii_menu/support/error.h"
#include "audio_sequence_bounds.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Convert bounded RSEQ commands to an ordered event timeline. Synthesis and
 * console-derived volume tables remain in audio_sequence.c. */
enum {
    WM_SEQUENCE_MAX_EVENTS = 100000,
    WM_SEQUENCE_MAX_TICK = 1000000,
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

static bool take_byte(const WmRsarSequence *sequence, uint32_t *offset,
                      uint8_t *value) {
    if (!wm_sequence_has_bytes(sequence, *offset, 1))
        return false;
    *value = sequence->data[(*offset)++];
    return true;
}

static bool take_variable(const WmRsarSequence *sequence, uint32_t *offset,
                          uint32_t *value) {
    uint64_t result = 0;
    for (unsigned index = 0; index < 5; index++) {
        uint8_t byte;
        if (!take_byte(sequence, offset, &byte))
            return false;
        result = result * 128 + (byte & 127);
        if (result > UINT32_MAX)
            return false;
        if (!(byte & 128)) {
            *value = (uint32_t)result;
            return true;
        }
    }
    return false;
}

static bool take_address(const WmRsarSequence *sequence, uint32_t *offset,
                         uint32_t *address) {
    if (!wm_sequence_has_bytes(sequence, *offset, 3))
        return false;
    const uint8_t *bytes = sequence->data + *offset;
    *address = ((uint32_t)bytes[0] << 16) | ((uint32_t)bytes[1] << 8) | bytes[2];
    *offset += 3;
    return wm_sequence_has_bytes(sequence, *address, 1);
}

static bool append_event(WmSequenceTimeline *timeline, size_t *capacity,
                         WmSequenceEvent event) {
    if (timeline->count >= WM_SEQUENCE_MAX_EVENTS)
        return false;
    if (timeline->count == *capacity) {
        size_t next = *capacity ? *capacity * 2 : 256;
        if (next > WM_SEQUENCE_MAX_EVENTS)
            next = WM_SEQUENCE_MAX_EVENTS;
        WmSequenceEvent *events = realloc(timeline->events, next * sizeof(*events));
        if (!events)
            return false;
        timeline->events = events;
        *capacity = next;
    }
    event.order = (uint32_t)timeline->count;
    timeline->events[timeline->count++] = event;
    if (event.wait_for_end)
        timeline->has_wait_for_end = true;
    return true;
}

static bool visit_tick(Visit *visits, uint32_t offset, uint32_t tick,
                       uint32_t variable_epoch, uint32_t event_order) {
    size_t index = ((size_t)offset * 2654435761u) & (WM_SEQUENCE_VISIT_SLOTS - 1);
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

static bool lookup_visit(const Visit *visits, uint32_t offset, Visit *result) {
    size_t index = ((size_t)offset * 2654435761u) & (WM_SEQUENCE_VISIT_SLOTS - 1);
    for (size_t probe = 0; probe < WM_SEQUENCE_VISIT_SLOTS; probe++) {
        const Visit *visit = &visits[index];
        if (visit->offset_plus_one == offset + 1) {
            *result = *visit;
            return true;
        }
        if (!visit->offset_plus_one)
            return false;
        index = (index + 1) & (WM_SEQUENCE_VISIT_SLOTS - 1);
    }
    return false;
}

static int compare_events(const void *left, const void *right) {
    const WmSequenceEvent *a = left;
    const WmSequenceEvent *b = right;
    if (a->tick != b->tick)
        return a->tick < b->tick ? -1 : 1;
    if (a->track != b->track)
        return a->track < b->track ? -1 : 1;
    return a->order < b->order ? -1 : a->order > b->order;
}

static bool scan_track(const WmRsarSequence *sequence, WmSequenceTimeline *timeline,
                       size_t *capacity, TrackStart start, TrackStart pending[16],
                       size_t *pending_count, bool seen_tracks[16], char *error,
                       size_t error_capacity) {
    Visit *visits = calloc(WM_SEQUENCE_VISIT_SLOTS, sizeof(*visits));
    if (!visits) {
        wm_error_set(error, error_capacity, "Out of memory scanning sequence.");
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
        if (tick > WM_SEQUENCE_MAX_TICK ||
            !wm_sequence_has_bytes(sequence, offset, 1)) {
            wm_error_set(error, error_capacity,
                         "Sequence clock or address exceeds bounds.");
            valid = false;
            break;
        }
        if (!visit_tick(visits, offset, tick, variable_epoch,
                        (uint32_t)timeline->count)) {
            wm_error_set(error, error_capacity, "Sequence command map is full.");
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
            if (key < 0)
                key = 0;
            if (key > 127)
                key = 127;
            event.kind = WM_SEQUENCE_NOTE;
            event.key = (uint8_t)key;
            event.velocity = velocity;
            event.length = length;
            event.program = program;
            event.wait_for_end = note_wait && length == 0;
            if (!append_event(timeline, capacity, event)) {
                valid = false;
                break;
            }
            if (note_wait)
                tick += length;
        } else if (command == 0x80) {
            uint32_t duration;
            if (!take_variable(sequence, &offset, &duration) ||
                duration > WM_SEQUENCE_MAX_TICK - tick) {
                valid = false;
                break;
            }
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
            if (!take_address(sequence, &offset, &address)) {
                valid = false;
                break;
            }
            if (command == 0x8A) {
                if (call_count >= 16) {
                    valid = false;
                    break;
                }
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
                         (tick == earlier.tick && !timeline->has_wait_for_end)) ||
                        (timeline->looping &&
                         (timeline->loop_start_tick != earlier.tick ||
                          timeline->loop_end_tick != tick))) {
                        valid = false;
                    } else {
                        timeline->looping = true;
                        timeline->loop_start_tick = earlier.tick;
                        timeline->loop_end_tick = tick;
                        timeline->voice_wait_loop = tick == earlier.tick;
                        timeline->loop_start_event_order = earlier.event_order;
                    }
                    terminated = true;
                    break;
                }
                offset = address;
            }
        } else if (command == 0xfd) {
            if (call_count == 0) {
                terminated = true;
                break;
            }
            offset = calls[--call_count];
        } else if (command == 0xff) {
            terminated = true;
            break;
        } else if (command == 0xfe || command == 0xe0 || command == 0xe3) {
            /* The original parser accepts these two-byte control values. */
            if (!wm_sequence_has_bytes(sequence, offset, 2)) {
                valid = false;
                break;
            }
            offset += 2;
        } else if (command == 0xc3) {
            uint8_t value;
            if (!take_byte(sequence, &offset, &value)) {
                valid = false;
                break;
            }
            transpose = (int)(int8_t)value;
        } else if (command == 0xe1) {
            uint8_t high, low;
            if (!take_byte(sequence, &offset, &high) ||
                !take_byte(sequence, &offset, &low)) {
                valid = false;
                break;
            }
            event.kind = WM_SEQUENCE_TEMPO;
            event.value = (uint16_t)(((uint16_t)high << 8) | low);
            if (!event.value || event.value > 1000 ||
                !append_event(timeline, capacity, event)) {
                valid = false;
                break;
            }
        } else if (command == 0xa0) {
            /* Use the center of the encoded randomized-pitch range for these
             * cues. The five bytes include target and bounds; random
             * modulation is intentionally omitted. */
            if (!wm_sequence_has_bytes(sequence, offset, 5) ||
                sequence->data[offset] != 0xc4) {
                valid = false;
                break;
            }
            offset += 5;
        } else if (command == 0xa1) {
            uint8_t target, variable;
            if (!take_byte(sequence, &offset, &target) ||
                !take_byte(sequence, &offset, &variable) || target != 0xc1 ||
                variables[variable] < 0 || variables[variable] > 127) {
                valid = false;
                break;
            }
            event.kind = WM_SEQUENCE_VOLUME;
            event.value = (uint16_t)variables[variable];
            if (!append_event(timeline, capacity, event)) {
                valid = false;
                break;
            }
        } else if (command == 0xa2) {
            uint8_t target;
            uint32_t address;
            if (!take_byte(sequence, &offset, &target) || target != 0x89 ||
                !take_address(sequence, &offset, &address)) {
                valid = false;
                break;
            }
            if (condition)
                offset = address;
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
            if (operation == 0x80)
                next = operand;
            else if (operation == 0x81)
                next += operand;
            else if (operation == 0x82)
                next -= operand;
            else if (operation == 0x92)
                condition = next > operand;
            else if (operation == 0x94)
                condition = next < operand;
            else {
                valid = false;
                break;
            }
            if (next < INT16_MIN || next > INT16_MAX) {
                valid = false;
                break;
            }
            variables[variable] = (int16_t)next;
            if (operation == 0x80 || operation == 0x81 || operation == 0x82)
                variable_epoch++;
        } else {
            uint8_t value;
            if (!take_byte(sequence, &offset, &value)) {
                valid = false;
                break;
            }
            event.value = value;
            switch (command) {
                case 0xb0:
                    if (value != 48)
                        valid = false;
                    break;
                case 0xc0:
                    event.kind = WM_SEQUENCE_PAN;
                    break;
                case 0xc1:
                    event.kind = WM_SEQUENCE_VOLUME;
                    break;
                case 0xc2:
                    event.kind = WM_SEQUENCE_MAIN_VOLUME;
                    break;
                case 0xc7:
                    note_wait = value != 0;
                    break;
                case 0xd0:
                    event.kind = WM_SEQUENCE_ATTACK;
                    break;
                case 0xd1:
                    event.kind = WM_SEQUENCE_DECAY;
                    break;
                case 0xd2:
                    event.kind = WM_SEQUENCE_SUSTAIN;
                    break;
                case 0xd3:
                    event.kind = WM_SEQUENCE_RELEASE;
                    break;
                case 0xd5:
                    event.kind = WM_SEQUENCE_VOLUME2;
                    break;
                case 0xd9:
                    event.kind = WM_SEQUENCE_AUX_A;
                    break;
                case 0xda:
                    event.kind = WM_SEQUENCE_AUX_B;
                    break;
                case 0xdb:
                    event.kind = WM_SEQUENCE_MAIN_SEND;
                    break;
                case 0xde:
                    event.kind = WM_SEQUENCE_AUX_C;
                    break;
                case 0xb1:
                case 0xc6:
                case 0xca:
                case 0xcb:
                case 0xcc:
                case 0xcd:
                case 0xd7:
                case 0xd8:
                    break;
                default:
                    valid = false;
                    break;
            }
            if (value > 127)
                valid = false;
            if (!valid)
                break;
            if ((command == 0xc0 || command == 0xc1 || command == 0xc2 ||
                 (command >= 0xd0 && command <= 0xd3) || command == 0xd5 ||
                 command == 0xd9 || command == 0xda || command == 0xdb ||
                 command == 0xde) &&
                !append_event(timeline, capacity, event)) {
                valid = false;
                break;
            }
        }
    }
    free(visits);
    if (valid && !terminated)
        valid = false;
    if (!valid && error && error_capacity && !error[0]) {
        snprintf(error, error_capacity,
                 "Unsupported or malformed RSEQ command near offset 0x%x.",
                 offset ? offset - 1 : 0);
    }
    return valid;
}

bool wm_sequence_parse(const WmRsarSequence *sequence, WmSequenceTimeline *timeline,
                       char *error, size_t error_capacity) {
    wm_error_set(error, error_capacity, "");
    if (!timeline)
        return false;
    *timeline = (WmSequenceTimeline){0};
    if (!sequence || !wm_sequence_has_bytes(sequence, sequence->start_offset, 1) ||
        sequence->size > 128u * 1024u * 1024u) {
        wm_error_set(error, error_capacity, "Invalid RSEQ source span.");
        return false;
    }
    TrackStart pending[16] = {{sequence->start_offset, 0, 0}};
    bool seen_tracks[16] = {true};
    size_t pending_count = 1;
    size_t capacity = 0;
    for (size_t next = 0; next < pending_count; next++) {
        if (!scan_track(sequence, timeline, &capacity, pending[next], pending,
                        &pending_count, seen_tracks, error, error_capacity)) {
            wm_sequence_timeline_free(timeline);
            return false;
        }
    }
    if (timeline->count == 0) {
        wm_error_set(error, error_capacity, "Sequence has no playable events.");
        wm_sequence_timeline_free(timeline);
        return false;
    }
    qsort(timeline->events, timeline->count, sizeof(*timeline->events), compare_events);
    return true;
}

void wm_sequence_timeline_free(WmSequenceTimeline *timeline) {
    if (!timeline)
        return;
    free(timeline->events);
    *timeline = (WmSequenceTimeline){0};
}
