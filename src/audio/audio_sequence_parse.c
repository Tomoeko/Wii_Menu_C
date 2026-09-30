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
    WM_SEQUENCE_VISIT_SLOTS = 262144,
    WM_SEQUENCE_MAX_TRACKS = 16,
    WM_SEQUENCE_MAX_CALLS = 16,
    WM_SEQUENCE_VARIABLE_COUNT = 256
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

static bool decode_control(uint8_t command, uint8_t value, bool *note_wait,
                           WmSequenceEvent *event, bool *emits_event) {
    if (value > 127)
        return false;
    event->value = value;
    *emits_event = true;
    switch (command) {
        case 0xc0:
            event->kind = WM_SEQUENCE_PAN;
            break;
        case 0xc1:
            event->kind = WM_SEQUENCE_VOLUME;
            break;
        case 0xc2:
            event->kind = WM_SEQUENCE_MAIN_VOLUME;
            break;
        case 0xd0:
            event->kind = WM_SEQUENCE_ATTACK;
            break;
        case 0xd1:
            event->kind = WM_SEQUENCE_DECAY;
            break;
        case 0xd2:
            event->kind = WM_SEQUENCE_SUSTAIN;
            break;
        case 0xd3:
            event->kind = WM_SEQUENCE_RELEASE;
            break;
        case 0xd5:
            event->kind = WM_SEQUENCE_VOLUME2;
            break;
        case 0xd9:
            event->kind = WM_SEQUENCE_AUX_A;
            break;
        case 0xda:
            event->kind = WM_SEQUENCE_AUX_B;
            break;
        case 0xdb:
            event->kind = WM_SEQUENCE_MAIN_SEND;
            break;
        case 0xde:
            event->kind = WM_SEQUENCE_AUX_C;
            break;
        case 0xb0:
            *emits_event = false;
            return value == 48;
        case 0xc7:
            *note_wait = value != 0;
            *emits_event = false;
            break;
        case 0xb1:
        case 0xc6:
        case 0xca:
        case 0xcb:
        case 0xcc:
        case 0xcd:
        case 0xd7:
        case 0xd8:
            *emits_event = false;
            break;
        default:
            return false;
    }
    return true;
}

static bool apply_variable_command(const WmRsarSequence *sequence, uint32_t *offset,
                                   int16_t variables[WM_SEQUENCE_VARIABLE_COUNT],
                                   bool *condition, uint32_t *variable_epoch) {
    uint8_t operation, variable, high, low;
    if (!take_byte(sequence, offset, &operation) ||
        !take_byte(sequence, offset, &variable) ||
        !take_byte(sequence, offset, &high) || !take_byte(sequence, offset, &low))
        return false;

    int16_t operand = (int16_t)(((uint16_t)high << 8) | low);
    int32_t next = variables[variable];
    switch (operation) {
        case 0x80:
            next = operand;
            break;
        case 0x81:
            next += operand;
            break;
        case 0x82:
            next -= operand;
            break;
        case 0x92:
            *condition = next > operand;
            break;
        case 0x94:
            *condition = next < operand;
            break;
        default:
            return false;
    }
    if (next < INT16_MIN || next > INT16_MAX)
        return false;
    variables[variable] = (int16_t)next;
    if (operation == 0x80 || operation == 0x81 || operation == 0x82)
        (*variable_epoch)++;
    return true;
}

typedef struct TrackScan {
    const WmRsarSequence *sequence;
    WmSequenceTimeline *timeline;
    size_t *event_capacity;
    TrackStart *pending;
    size_t *pending_count;
    bool *seen_tracks;
    Visit *visits;
    uint32_t offset;
    uint32_t tick;
    uint8_t track;
    uint32_t program;
    uint32_t calls[WM_SEQUENCE_MAX_CALLS];
    unsigned call_count;
    int transpose;
    bool note_wait;
    uint32_t variable_epoch;
    bool condition;
    int16_t variables[WM_SEQUENCE_VARIABLE_COUNT];
    bool terminated;
} TrackScan;

static WmSequenceEvent track_event(const TrackScan *scan) {
    return (WmSequenceEvent){.tick = scan->tick, .track = scan->track};
}

static bool scan_note(TrackScan *scan, uint8_t command) {
    uint8_t velocity;
    uint32_t length;
    if (!take_byte(scan->sequence, &scan->offset, &velocity) ||
        !take_variable(scan->sequence, &scan->offset, &length) ||
        length > WM_SEQUENCE_MAX_TICK || velocity > 127)
        return false;
    int key = (int)command + scan->transpose;
    if (key < 0)
        key = 0;
    if (key > 127)
        key = 127;
    WmSequenceEvent event = track_event(scan);
    event.kind = WM_SEQUENCE_NOTE;
    event.key = (uint8_t)key;
    event.velocity = velocity;
    event.length = length;
    event.program = scan->program;
    event.wait_for_end = scan->note_wait && length == 0;
    if (!append_event(scan->timeline, scan->event_capacity, event))
        return false;
    if (scan->note_wait)
        scan->tick += length;
    return true;
}

static bool scan_open_track(TrackScan *scan) {
    uint8_t track;
    uint32_t address;
    if (!take_byte(scan->sequence, &scan->offset, &track) ||
        !take_address(scan->sequence, &scan->offset, &address) ||
        track >= WM_SEQUENCE_MAX_TRACKS || scan->seen_tracks[track] ||
        *scan->pending_count >= WM_SEQUENCE_MAX_TRACKS)
        return false;
    scan->seen_tracks[track] = true;
    scan->pending[(*scan->pending_count)++] = (TrackStart){address, scan->tick, track};
    return true;
}

static bool scan_jump(TrackScan *scan, bool call) {
    uint32_t address;
    if (!take_address(scan->sequence, &scan->offset, &address))
        return false;
    if (call) {
        if (scan->call_count >= WM_SEQUENCE_MAX_CALLS)
            return false;
        scan->calls[scan->call_count++] = scan->offset;
        scan->offset = address;
        return true;
    }
    Visit earlier;
    if (!lookup_visit(scan->visits, address, &earlier) ||
        scan->variable_epoch != earlier.variable_epoch) {
        scan->offset = address;
        return true;
    }
    scan->terminated = true;
    WmSequenceTimeline *timeline = scan->timeline;
    if (scan->tick < earlier.tick ||
        (scan->tick == earlier.tick && !timeline->has_wait_for_end) ||
        (timeline->looping && (timeline->loop_start_tick != earlier.tick ||
                               timeline->loop_end_tick != scan->tick)))
        return false;
    timeline->looping = true;
    timeline->loop_start_tick = earlier.tick;
    timeline->loop_end_tick = scan->tick;
    timeline->voice_wait_loop = scan->tick == earlier.tick;
    timeline->loop_start_event_order = earlier.event_order;
    return true;
}

static bool scan_tempo(TrackScan *scan) {
    uint8_t high, low;
    if (!take_byte(scan->sequence, &scan->offset, &high) ||
        !take_byte(scan->sequence, &scan->offset, &low))
        return false;
    WmSequenceEvent event = track_event(scan);
    event.kind = WM_SEQUENCE_TEMPO;
    event.value = (uint16_t)(((uint16_t)high << 8) | low);
    return event.value && event.value <= 1000 &&
           append_event(scan->timeline, scan->event_capacity, event);
}

static bool scan_variable_volume(TrackScan *scan) {
    uint8_t target, variable;
    if (!take_byte(scan->sequence, &scan->offset, &target) ||
        !take_byte(scan->sequence, &scan->offset, &variable) || target != 0xc1 ||
        scan->variables[variable] < 0 || scan->variables[variable] > 127)
        return false;
    WmSequenceEvent event = track_event(scan);
    event.kind = WM_SEQUENCE_VOLUME;
    event.value = (uint16_t)scan->variables[variable];
    return append_event(scan->timeline, scan->event_capacity, event);
}

static bool scan_control(TrackScan *scan, uint8_t command) {
    uint8_t value;
    bool emits_event;
    WmSequenceEvent event = track_event(scan);
    return take_byte(scan->sequence, &scan->offset, &value) &&
           decode_control(command, value, &scan->note_wait, &event, &emits_event) &&
           (!emits_event || append_event(scan->timeline, scan->event_capacity, event));
}

static bool scan_command(TrackScan *scan, uint8_t command) {
    if (command < 0x80)
        return scan_note(scan, command);
    switch (command) {
        case 0x80: {
            uint32_t duration;
            if (!take_variable(scan->sequence, &scan->offset, &duration) ||
                duration > WM_SEQUENCE_MAX_TICK - scan->tick)
                return false;
            scan->tick += duration;
            return true;
        }
        case 0x81:
            return take_variable(scan->sequence, &scan->offset, &scan->program) &&
                   scan->program <= 1024;
        case 0x88:
            return scan_open_track(scan);
        case 0x89:
        case 0x8a:
            return scan_jump(scan, command == 0x8a);
        case 0xfd:
            if (scan->call_count == 0)
                scan->terminated = true;
            else
                scan->offset = scan->calls[--scan->call_count];
            return true;
        case 0xff:
            scan->terminated = true;
            return true;
        case 0xfe:
        case 0xe0:
        case 0xe3:
            /* These control values carry two bytes but do not emit events. */
            if (!wm_sequence_has_bytes(scan->sequence, scan->offset, 2))
                return false;
            scan->offset += 2;
            return true;
        case 0xc3: {
            uint8_t value;
            if (!take_byte(scan->sequence, &scan->offset, &value))
                return false;
            scan->transpose = (int)(int8_t)value;
            return true;
        }
        case 0xe1:
            return scan_tempo(scan);
        case 0xa0:
            /* Use the center of the encoded randomized-pitch range for these
             * cues. The five bytes include target and bounds; random
             * modulation is intentionally omitted. */
            if (!wm_sequence_has_bytes(scan->sequence, scan->offset, 5) ||
                scan->sequence->data[scan->offset] != 0xc4)
                return false;
            scan->offset += 5;
            return true;
        case 0xa1:
            return scan_variable_volume(scan);
        case 0xa2: {
            uint8_t target;
            uint32_t address;
            if (!take_byte(scan->sequence, &scan->offset, &target) || target != 0x89 ||
                !take_address(scan->sequence, &scan->offset, &address))
                return false;
            if (scan->condition)
                scan->offset = address;
            return true;
        }
        case 0xf0:
            return apply_variable_command(scan->sequence, &scan->offset,
                                          scan->variables, &scan->condition,
                                          &scan->variable_epoch);
        default:
            return scan_control(scan, command);
    }
}

static bool scan_track(const WmRsarSequence *sequence, WmSequenceTimeline *timeline,
                       size_t *capacity, TrackStart start,
                       TrackStart pending[WM_SEQUENCE_MAX_TRACKS],
                       size_t *pending_count, bool seen_tracks[WM_SEQUENCE_MAX_TRACKS],
                       char *error, size_t error_capacity) {
    TrackScan scan = {.sequence = sequence,
                      .timeline = timeline,
                      .event_capacity = capacity,
                      .pending = pending,
                      .pending_count = pending_count,
                      .seen_tracks = seen_tracks,
                      .offset = start.offset,
                      .tick = start.tick,
                      .track = start.track,
                      .note_wait = true};
    scan.visits = calloc(WM_SEQUENCE_VISIT_SLOTS, sizeof(*scan.visits));
    if (!scan.visits) {
        wm_error_set(error, error_capacity, "Out of memory scanning sequence.");
        return false;
    }
    bool valid = true;
    for (size_t command_count = 0;
         command_count < WM_SEQUENCE_MAX_EVENTS && !scan.terminated; command_count++) {
        if (scan.tick > WM_SEQUENCE_MAX_TICK ||
            !wm_sequence_has_bytes(sequence, scan.offset, 1)) {
            wm_error_set(error, error_capacity,
                         "Sequence clock or address exceeds bounds.");
            valid = false;
            break;
        }
        if (!visit_tick(scan.visits, scan.offset, scan.tick, scan.variable_epoch,
                        (uint32_t)timeline->count)) {
            wm_error_set(error, error_capacity, "Sequence command map is full.");
            valid = false;
            break;
        }
        uint8_t command;
        take_byte(sequence, &scan.offset, &command);
        if (!scan_command(&scan, command)) {
            valid = false;
            break;
        }
    }
    free(scan.visits);
    valid = valid && scan.terminated;
    if (!valid && error && error_capacity && !error[0]) {
        snprintf(error, error_capacity,
                 "Unsupported or malformed RSEQ command near offset 0x%x.",
                 scan.offset ? scan.offset - 1 : 0);
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
    TrackStart pending[WM_SEQUENCE_MAX_TRACKS] = {{sequence->start_offset, 0, 0}};
    bool seen_tracks[WM_SEQUENCE_MAX_TRACKS] = {true};
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
