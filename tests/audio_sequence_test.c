#include "wii_menu/audio_sequence.h"

#include <assert.h>
#include <stdio.h>
#include <string.h>

static WmSequenceTimeline parse(const uint8_t *data, size_t size)
{
    WmRsarSequence source = {.data = data, .size = size};
    WmSequenceTimeline timeline;
    char error[160] = {0};
    assert(wm_sequence_parse(&source, &timeline, error, sizeof(error)));
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

int main(void)
{
    independent_track_clocks();
    loop_and_note_wait();
    centered_random_and_rejection();
    variable_counted_phrase();
    puts("Sequence parsing fixtures passed.");
    return 0;
}
