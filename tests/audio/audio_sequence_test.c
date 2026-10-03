#include "wii_menu/audio/audio_sequence.h"

#include "audio_sequence_driver_internal.h"
#include "audio_sequence_render_internal.h"

#include <assert.h>
#include <float.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static WmSequenceTimeline parse(const uint8_t *data, size_t size) {
    WmRsarSequence source = {.data = data, .size = size};
    WmSequenceTimeline timeline;
    char error[160] = {0};
    if (!wm_sequence_parse(&source, &timeline, error, sizeof(error))) {
        fprintf(stderr, "sequence parse failed: %s\n", error);
        abort();
    }
    assert(error[0] == '\0');
    return timeline;
}

static void independent_track_clocks(void) {
    const uint8_t sequence[] = {0x88, 0x01, 0x00, 0x00, 0x10, 0xe1, 0x00, 0x3c,
                                0xc7, 0x00, 0x80, 0x30, 0x3c, 0x40, 0x00, 0xff,
                                0xc7, 0x00, 0x80, 0x30, 0x3e, 0x40, 0x00, 0xff};
    WmSequenceTimeline timeline = parse(sequence, sizeof(sequence));
    assert(timeline.count == 3);
    assert(timeline.events[0].kind == WM_SEQUENCE_TEMPO);
    assert(timeline.events[0].tick == 0);
    assert(timeline.events[0].value == 60);
    assert(timeline.events[1].kind == WM_SEQUENCE_NOTE);
    assert(timeline.events[1].track == 0);
    assert(timeline.events[1].key == 60);
    assert(timeline.events[1].tick == 48);
    assert(timeline.events[2].kind == WM_SEQUENCE_NOTE);
    assert(timeline.events[2].track == 1);
    assert(timeline.events[2].key == 62);
    assert(timeline.events[2].tick == 48);
    wm_sequence_timeline_free(&timeline);
}

static void loop_and_note_wait(void) {
    const uint8_t loop[] = {0xc7, 0x00, 0x3c, 0x7f, 0x00, 0x80, 0x06,
                            0x3e, 0x7f, 0x00, 0x89, 0x00, 0x00, 0x00};
    WmSequenceTimeline timeline = parse(loop, sizeof(loop));
    assert(timeline.looping);
    assert(timeline.loop_start_tick == 0);
    assert(timeline.loop_end_tick == 6);
    assert(timeline.count == 2);
    assert(timeline.events[0].tick == 0);
    assert(timeline.events[1].tick == 6);
    wm_sequence_timeline_free(&timeline);

    const uint8_t wait[] = {0x3c, 0x5a, 0x00, 0x80, 0x01, 0xc7,
                            0x00, 0x3c, 0x78, 0x00, 0xff};
    timeline = parse(wait, sizeof(wait));
    assert(timeline.has_wait_for_end);
    assert(timeline.events[0].wait_for_end);
    assert(!timeline.events[1].wait_for_end);
    assert(timeline.events[0].tick == 0);
    assert(timeline.events[1].tick == 1);
    wm_sequence_timeline_free(&timeline);
}

static void centered_random_and_rejection(void) {
    const uint8_t random_center[] = {0xa0, 0xc4, 0x00, 0x00, 0x00,
                                     0x00, 0x3c, 0x7f, 0x01, 0xff};
    WmSequenceTimeline timeline = parse(random_center, sizeof(random_center));
    assert(timeline.count == 1);
    assert(timeline.events[0].length == 1);
    wm_sequence_timeline_free(&timeline);

    const uint8_t unsupported[] = {0xf0, 0xff};
    WmRsarSequence source = {.data = unsupported, .size = sizeof(unsupported)};
    char error[160] = {0};
    assert(!wm_sequence_parse(&source, &timeline, error, sizeof(error)));
    assert(strstr(error, "Unsupported") != NULL);
    assert(timeline.events == NULL);
}

static void variable_counted_phrase(void) {
    const uint8_t sequence[] = {0xf0, 0x80, 0x01, 0x00, 0x00, 0x30, 0x64, 0x01,
                                0xf0, 0x81, 0x01, 0x00, 0x01, 0xf0, 0x94, 0x01,
                                0x00, 0x03, 0xa2, 0x89, 0x00, 0x00, 0x05, 0xff};
    WmSequenceTimeline timeline = parse(sequence, sizeof(sequence));
    assert(timeline.count == 3);
    for (size_t index = 0; index < 3; index++) {
        assert(timeline.events[index].kind == WM_SEQUENCE_NOTE);
        assert(timeline.events[index].tick == index);
        assert(timeline.events[index].key == 0x30);
    }
    assert(!timeline.looping);
    wm_sequence_timeline_free(&timeline);
}

static void subroutine_clocks(void) {
    /* Nested calls share the track clock and return to the instruction after each call.
     */
    const uint8_t sequence[] = {0x8a, 0x00, 0x00, 0x0c, 0x3c, 0x64, 0x01,
                                0xff, 0xff, 0xff, 0xff, 0xff, 0x3e, 0x64,
                                0x02, 0x8a, 0x00, 0x00, 0x18, 0x40, 0x64,
                                0x03, 0xfd, 0xff, 0x41, 0x64, 0x04, 0xfd};
    const uint8_t keys[] = {62, 65, 64, 60};
    const uint32_t ticks[] = {0, 2, 6, 9};
    WmSequenceTimeline timeline = parse(sequence, sizeof(sequence));
    assert(timeline.count == 4);
    assert(!timeline.looping);
    for (size_t index = 0; index < timeline.count; index++) {
        assert(timeline.events[index].kind == WM_SEQUENCE_NOTE);
        assert(timeline.events[index].key == keys[index]);
        assert(timeline.events[index].tick == ticks[index]);
    }
    wm_sequence_timeline_free(&timeline);
}

static void reject_control_flow(const uint8_t *data, size_t size) {
    WmRsarSequence source = {.data = data, .size = size};
    WmSequenceTimeline timeline = {0};
    char error[160] = {0};
    assert(!wm_sequence_parse(&source, &timeline, error, sizeof(error)));
    assert(error[0] != '\0');
    assert(timeline.events == NULL);
    assert(timeline.count == 0);
}

static void bounded_control_flow(void) {
    const uint8_t recursion[] = {0x8a, 0x00, 0x00, 0x00};
    const uint8_t short_call[] = {0x8a, 0x00, 0x00};
    const uint8_t duplicate_track[] = {0x88, 0x00, 0x00, 0x00, 0x05, 0xff};
    const uint8_t stalled_loop[] = {0xc7, 0x00, 0x3c, 0x7f, 0x01,
                                    0x89, 0x00, 0x00, 0x00};
    reject_control_flow(recursion, sizeof(recursion));
    reject_control_flow(short_call, sizeof(short_call));
    reject_control_flow(duplicate_track, sizeof(duplicate_track));
    reject_control_flow(stalled_loop, sizeof(stalled_loop));
}

static void synthetic_voice_pcm(void) {
    /* Interpolation and two fixed-point gain stages preserve the source's
     * floor-toward-negative-infinity behavior for negative samples. */
    int16_t samples[] = {0, 32767, -32768, 1234};
    WmAudioPcm wave = {.samples = samples,
                       .sample_rate = WM_SEQUENCE_RATE,
                       .frame_count = 4,
                       .channels = 1};
    WmRsarInstrument instrument = {.pan = 64};
    SequenceTables tables = {0};
    tables.attack[0] = 0.0f;
    tables.decibels[904] = 1.0f;
    tables.pan[128] = 1.0f;
    SequencePlayer player = {.tables = &tables, .main_volume = 127, .gain = 1.0f};
    player.tracks[0] = (SequenceTrack){
        .volume = 127, .volume2 = 127, .pan = 64, .main_send = 127, .aux_a = 127};
    SequenceVoice voice = {.instrument = &instrument,
                           .wave = &wave,
                           .release_tick = UINT32_MAX,
                           .speed = 0.5,
                           .initial_gain = 1.0f,
                           .envelope_level = -904.0f};
    const int expected[] = {0, 16383, 32766, -1, -32767, -15767, 1233, 1233};
    wm_sequence_voice_render(&player, &voice);
    assert(voice.position == 4.0);
    assert(voice.envelope_state == 2);
    for (size_t frame = 0; frame < sizeof(expected) / sizeof(expected[0]); frame++) {
        float sample = (float)expected[frame] / 32768.0f;
        assert(player.block_left[frame] == sample);
        assert(player.block_right[frame] == sample);
        assert(player.aux_left[frame] == sample);
        assert(player.aux_right[frame] == sample);
    }
    assert(player.block_left[8] == 0.0f);

    /* Instant attack reaches sustain in this block only when sustain is
     * already at zero. A lower level begins decay in the following block. */
    tables.sustain[0] = -100;
    voice.position = 0.0;
    voice.envelope_level = -904.0f;
    voice.envelope_state = 0;
    voice.has_previous_gain = false;
    wm_sequence_voice_render(&player, &voice);
    assert(voice.envelope_state == 1 && voice.envelope_level == 0.0f);
    wm_sequence_voice_render(&player, &voice);
    assert(voice.envelope_state == 1);
    assert(voice.envelope_level < 0.0f && voice.envelope_level > -100.0f);
}

static void stereo_source_pan(void) {
    /* Each stereo source has its own pan lookup and integer send stage.
     * The synthetic midpoint makes crossfeed and negative rounding explicit. */
    int16_t samples[] = {32767, -32768, -3, 7};
    WmAudioPcm wave = {.samples = samples,
                       .sample_rate = WM_SEQUENCE_RATE,
                       .frame_count = 2,
                       .channels = 2};
    SequenceTables tables = {0};
    tables.decibels[904] = 1.0f;
    tables.pan[0] = 1.0f;
    tables.pan[128] = 0.5f;
    const struct {
        uint8_t instrument_pan;
        uint8_t track_pan;
        bool main_enabled;
        bool aux_enabled;
        int expected[4];
    } cases[] = {{64, 64, true, true, {32766, -32767, -3, 6}},
                 {64, 127, true, true, {16383, -16384, -2, 4}},
                 {64, 1, true, true, {16382, -16384, 0, 3}},
                 {127, 127, true, true, {0, -1, 0, 3}},
                 {1, 1, true, true, {-1, 0, 3, 0}},
                 {64, 127, false, true, {16383, -16384, -2, 4}},
                 {64, 127, true, false, {16383, -16384, -2, 4}}};
    for (size_t index = 0; index < sizeof(cases) / sizeof(cases[0]); index++) {
        WmRsarInstrument instrument = {.pan = cases[index].instrument_pan};
        SequencePlayer player = {.tables = &tables, .main_volume = 127, .gain = 1.0f};
        player.tracks[0] =
            (SequenceTrack){.volume = 127,
                            .volume2 = 127,
                            .pan = cases[index].track_pan,
                            .main_send = cases[index].main_enabled ? 127 : 0,
                            .aux_a = cases[index].aux_enabled ? 127 : 0};
        SequenceVoice voice = {.instrument = &instrument,
                               .wave = &wave,
                               .release_tick = UINT32_MAX,
                               .speed = 1.0,
                               .initial_gain = 1.0f,
                               .envelope_level = -904.0f};
        wm_sequence_voice_render(&player, &voice);
        assert(voice.position == 2.0);
        for (size_t frame = 0; frame < 2; frame++) {
            float left = (float)cases[index].expected[frame * 2] / 32768.0f;
            float right = (float)cases[index].expected[frame * 2 + 1] / 32768.0f;
            assert(player.block_left[frame] == (cases[index].main_enabled ? left : 0));
            assert(player.block_right[frame] ==
                   (cases[index].main_enabled ? right : 0));
            assert(player.aux_left[frame] == (cases[index].aux_enabled ? left : 0));
            assert(player.aux_right[frame] == (cases[index].aux_enabled ? right : 0));
        }
        assert(player.block_left[2] == 0.0f && player.block_right[2] == 0.0f);
    }
}

static void synthetic_reverb_impulse(void) {
    SequenceTables tables = {0};
    for (size_t index = 0; index < 8; index++) {
        tables.reverb_frames[index] = 1;
    }
    tables.reverb_preset[1] = 1.0f;
    tables.reverb_preset[3] = 1.0f;
    tables.reverb_preset[5] = 1.0f;
    SequenceReverb reverb;
    if (!wm_sequence_reverb_initialize(&reverb, &tables, true)) {
        fputs("synthetic reverb initialization failed\n", stderr);
        abort();
    }
    SequencePlayer player = {0};
    player.aux_left[0] = 1.0f;
    wm_sequence_reverb_apply(&reverb, &player);
    memset(player.aux_left, 0, sizeof(player.aux_left));
    wm_sequence_reverb_apply(&reverb, &player);
    wm_sequence_reverb_apply(&reverb, &player);
    assert(player.block_left[4] == 58982.0f / 32768.0f);
    assert(player.block_right[4] == 0.0f);
    wm_sequence_reverb_free(&reverb);
}

static void put_big_u32(uint8_t *bytes, uint32_t value) {
    bytes[0] = (uint8_t)(value >> 24);
    bytes[1] = (uint8_t)(value >> 16);
    bytes[2] = (uint8_t)(value >> 8);
    bytes[3] = (uint8_t)value;
}

static void put_big_float(uint8_t *bytes, float value) {
    uint32_t bits;
    memcpy(&bits, &value, sizeof(bits));
    put_big_u32(bytes, bits);
}

static void driver_table_loading(void) {
    enum {
        DOL_HEADER_SIZE = 0x100,
        ATTACK_SIZE = 128 * 4,
        SUSTAIN_SIZE = 128 * 2,
        DECIBEL_SIZE = 965 * 4,
        PAN_SIZE = 257 * 4,
        REVERB_SIZE = 8 * 4,
        PRESET_SIZE = 6 * 4
    };
    const uint32_t addresses[] = {0x8161e3f0u, 0x8161e2f0u, 0x8161ead8u,
                                  0x8161f9ecu, 0x81685da0u, 0x8160f048u};
    const size_t lengths[] = {ATTACK_SIZE, SUSTAIN_SIZE, DECIBEL_SIZE,
                              PAN_SIZE,    REVERB_SIZE,  PRESET_SIZE};
    uint8_t dol[DOL_HEADER_SIZE + ATTACK_SIZE + SUSTAIN_SIZE + DECIBEL_SIZE + PAN_SIZE +
                REVERB_SIZE + PRESET_SIZE] = {0};
    size_t offsets[6];
    size_t next = DOL_HEADER_SIZE;
    for (size_t section = 0; section < 6; section++) {
        offsets[section] = next;
        put_big_u32(dol + section * 4, (uint32_t)next);
        put_big_u32(dol + 0x48 + section * 4, addresses[section]);
        put_big_u32(dol + 0x90 + section * 4, (uint32_t)lengths[section]);
        next += lengths[section];
    }
    assert(next == sizeof(dol));
    for (size_t index = 0; index < 128; index++) {
        put_big_float(dol + offsets[0] + index * 4, 0.5f);
        dol[offsets[1] + index * 2] = 0xfc;
        dol[offsets[1] + index * 2 + 1] = 0x78;
    }
    for (size_t index = 0; index < 965; index++) {
        put_big_float(dol + offsets[2] + index * 4, 0.75f);
    }
    for (size_t index = 0; index < 257; index++) {
        put_big_float(dol + offsets[3] + index * 4, 0.25f);
    }
    for (size_t index = 0; index < 8; index++) {
        put_big_u32(dol + offsets[4] + index * 4, 96);
    }
    put_big_float(dol + offsets[5] + 4, 2.0f);

    SequenceTables tables = {0};
    assert(wm_sequence_driver_load_tables(dol, sizeof(dol), &tables));
    assert(tables.attack[104] == 0.5f);
    assert(tables.sustain[127] == -904);
    assert(tables.decibels[904] == 0.75f);
    assert(tables.pan[128] == 0.25f);
    assert(tables.reverb_frames[7] == 96);
    assert(tables.reverb_preset[1] == 2.0f);

    /* Finite values alone cannot keep the delay network bounded. The native
     * coloration, damping, and output gain controls admit only 0..1. */
    const size_t bounded_parameters[] = {2, 3, 5};
    const float invalid_parameters[] = {-0.1f, 1.1f, FLT_MAX};
    for (size_t parameter = 0;
         parameter < sizeof(bounded_parameters) / sizeof(bounded_parameters[0]);
         parameter++) {
        size_t offset = offsets[5] + bounded_parameters[parameter] * 4;
        for (size_t value = 0;
             value < sizeof(invalid_parameters) / sizeof(invalid_parameters[0]);
             value++) {
            put_big_float(dol + offset, invalid_parameters[value]);
            assert(!wm_sequence_driver_load_tables(dol, sizeof(dol), &tables));
        }
        put_big_float(dol + offset, 1.0f);
        assert(wm_sequence_driver_load_tables(dol, sizeof(dol), &tables));
        put_big_float(dol + offset, 0.0f);
    }
    assert(wm_sequence_release_rate(127) == 65535.0f);
    assert(wm_sequence_release_rate(126) == 24.0f);

    assert(!wm_sequence_driver_load_tables(dol, sizeof(dol) - 1, &tables));
    put_big_u32(dol + offsets[0], 0x7f800000u);
    assert(!wm_sequence_driver_load_tables(dol, sizeof(dol), &tables));
    put_big_float(dol + offsets[0], 0.5f);
    put_big_u32(dol + 0x90 + 4 * 4, 0);
    assert(!wm_sequence_driver_load_tables(dol, sizeof(dol), &tables));
}

int main(void) {
    independent_track_clocks();
    loop_and_note_wait();
    centered_random_and_rejection();
    variable_counted_phrase();
    subroutine_clocks();
    bounded_control_flow();
    synthetic_voice_pcm();
    stereo_source_pan();
    synthetic_reverb_impulse();
    driver_table_loading();
    puts("Sequence parsing, driver tables, and synthetic PCM fixtures passed.");
    return 0;
}
