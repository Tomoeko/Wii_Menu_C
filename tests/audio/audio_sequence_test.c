#include "wii_menu/audio/audio_sequence.h"

#include "audio_sequence_driver_internal.h"
#include "audio_sequence_render_internal.h"

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static WmSequenceTimeline parse(const uint8_t *data, size_t size)
{
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

static void independent_track_clocks(void)
{
    const uint8_t sequence[] = {
        0x88, 0x01, 0x00, 0x00, 0x10,
        0xe1, 0x00, 0x3c,
        0xc7, 0x00,
        0x80, 0x30,
        0x3c, 0x40, 0x00,
        0xff,
        0xc7, 0x00,
        0x80, 0x30,
        0x3e, 0x40, 0x00,
        0xff
    };
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

static void loop_and_note_wait(void)
{
    const uint8_t loop[] = {
        0xc7, 0x00,
        0x3c, 0x7f, 0x00,
        0x80, 0x06,
        0x3e, 0x7f, 0x00,
        0x89, 0x00, 0x00, 0x00
    };
    WmSequenceTimeline timeline = parse(loop, sizeof(loop));
    assert(timeline.looping);
    assert(timeline.loop_start_tick == 0);
    assert(timeline.loop_end_tick == 6);
    assert(timeline.count == 2);
    assert(timeline.events[0].tick == 0);
    assert(timeline.events[1].tick == 6);
    wm_sequence_timeline_free(&timeline);

    const uint8_t wait[] = {
        0x3c, 0x5a, 0x00,
        0x80, 0x01,
        0xc7, 0x00,
        0x3c, 0x78, 0x00,
        0xff
    };
    timeline = parse(wait, sizeof(wait));
    assert(timeline.has_wait_for_end);
    assert(timeline.events[0].wait_for_end);
    assert(!timeline.events[1].wait_for_end);
    assert(timeline.events[0].tick == 0);
    assert(timeline.events[1].tick == 1);
    wm_sequence_timeline_free(&timeline);
}

static void centered_random_and_rejection(void)
{
    const uint8_t random_center[] = {
        0xa0, 0xc4, 0x00, 0x00, 0x00, 0x00,
        0x3c, 0x7f, 0x01,
        0xff
    };
    WmSequenceTimeline timeline = parse(random_center,
                                         sizeof(random_center));
    assert(timeline.count == 1);
    assert(timeline.events[0].length == 1);
    wm_sequence_timeline_free(&timeline);

    const uint8_t unsupported[] = {0xf0, 0xff};
    WmRsarSequence source = {.data = unsupported,
                             .size = sizeof(unsupported)};
    char error[160] = {0};
    assert(!wm_sequence_parse(&source, &timeline, error, sizeof(error)));
    assert(strstr(error, "Unsupported") != NULL);
    assert(timeline.events == NULL);
}

static void variable_counted_phrase(void)
{
    const uint8_t sequence[] = {
        0xf0, 0x80, 0x01, 0x00, 0x00,
        0x30, 0x64, 0x01,
        0xf0, 0x81, 0x01, 0x00, 0x01,
        0xf0, 0x94, 0x01, 0x00, 0x03,
        0xa2, 0x89, 0x00, 0x00, 0x05,
        0xff
    };
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

static void synthetic_voice_pcm(void)
{
    /* Interpolation and two fixed-point gain stages preserve the source's
     * floor-toward-negative-infinity behavior for negative samples. */
    int16_t samples[] = {0, 32767, -32768, 1234};
    WmAudioPcm wave = {
        .samples = samples,
        .sample_rate = WM_SEQUENCE_RATE,
        .frame_count = 4,
        .channels = 1
    };
    WmRsarInstrument instrument = {.pan = 64};
    SequenceTables tables = {0};
    tables.attack[0] = 0.0f;
    tables.decibels[904] = 1.0f;
    tables.pan[128] = 1.0f;
    SequencePlayer player = {
        .tables = &tables,
        .main_volume = 127,
        .gain = 1.0f
    };
    player.tracks[0] = (SequenceTrack){
        .volume = 127,
        .volume2 = 127,
        .pan = 64,
        .main_send = 127,
        .aux_a = 127
    };
    SequenceVoice voice = {
        .instrument = &instrument,
        .wave = &wave,
        .release_tick = UINT32_MAX,
        .speed = 0.5,
        .initial_gain = 1.0f,
        .envelope_level = -904.0f
    };
    const int expected[] = {
        0, 16383, 32766, -1, -32767, -15767, 1233, 1233
    };
    wm_sequence_voice_render(&player, &voice);
    assert(voice.position == 4.0);
    assert(voice.envelope_state == 1);
    for (size_t frame = 0; frame < sizeof(expected) / sizeof(expected[0]);
         frame++) {
        float sample = (float)expected[frame] / 32768.0f;
        assert(player.block_left[frame] == sample);
        assert(player.block_right[frame] == sample);
        assert(player.aux_left[frame] == sample);
        assert(player.aux_right[frame] == sample);
    }
    assert(player.block_left[8] == 0.0f);
}

static void synthetic_reverb_impulse(void)
{
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

static void put_big_u32(uint8_t *bytes, uint32_t value)
{
    bytes[0] = (uint8_t)(value >> 24);
    bytes[1] = (uint8_t)(value >> 16);
    bytes[2] = (uint8_t)(value >> 8);
    bytes[3] = (uint8_t)value;
}

static void put_big_float(uint8_t *bytes, float value)
{
    uint32_t bits;
    memcpy(&bits, &value, sizeof(bits));
    put_big_u32(bytes, bits);
}

static void driver_table_loading(void)
{
    enum {
        DOL_HEADER_SIZE = 0x100,
        ATTACK_SIZE = 128 * 4,
        SUSTAIN_SIZE = 128 * 2,
        DECIBEL_SIZE = 965 * 4,
        PAN_SIZE = 257 * 4,
        REVERB_SIZE = 8 * 4,
        PRESET_SIZE = 6 * 4
    };
    const uint32_t addresses[] = {
        0x8161e3f0u,
        0x8161e2f0u,
        0x8161ead8u,
        0x8161f9ecu,
        0x81685da0u,
        0x8160f048u
    };
    const size_t lengths[] = {
        ATTACK_SIZE, SUSTAIN_SIZE, DECIBEL_SIZE, PAN_SIZE,
        REVERB_SIZE, PRESET_SIZE
    };
    uint8_t dol[DOL_HEADER_SIZE + ATTACK_SIZE + SUSTAIN_SIZE +
                DECIBEL_SIZE + PAN_SIZE + REVERB_SIZE + PRESET_SIZE] = {0};
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
    assert(wm_sequence_release_rate(127) == 65535.0f);
    assert(wm_sequence_release_rate(126) == 24.0f);

    assert(!wm_sequence_driver_load_tables(dol, sizeof(dol) - 1, &tables));
    put_big_u32(dol + offsets[0], 0x7f800000u);
    assert(!wm_sequence_driver_load_tables(dol, sizeof(dol), &tables));
    put_big_float(dol + offsets[0], 0.5f);
    put_big_u32(dol + 0x90 + 4 * 4, 0);
    assert(!wm_sequence_driver_load_tables(dol, sizeof(dol), &tables));
}

int main(void)
{
    independent_track_clocks();
    loop_and_note_wait();
    centered_random_and_rejection();
    variable_counted_phrase();
    synthetic_voice_pcm();
    synthetic_reverb_impulse();
    driver_table_loading();
    puts("Sequence parsing, driver tables, and synthetic PCM fixtures passed.");
    return 0;
}
