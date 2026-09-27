#include "../../src/platform/psvr2/audio_codec_path.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

typedef struct CodecFixture {
    uint16_t registers[128];
    uint8_t updated[8];
    unsigned count;
    unsigned fail_at;
} CodecFixture;

static int update(void *context, uint8_t reg, uint16_t mask, uint16_t value)
{
    CodecFixture *fixture = context;
    assert(fixture->count < sizeof(fixture->updated));
    fixture->updated[fixture->count++] = reg;
    if (fixture->count == fixture->fail_at) {
        errno = EIO;
        return -1;
    }
    fixture->registers[reg] = (uint16_t)((fixture->registers[reg] & ~mask) | (value & mask));
    return 0;
}

static CodecFixture stock_route(void)
{
    CodecFixture fixture = {0};
    fixture.registers[0] = 0x0032;
    fixture.registers[2] = 0x00e5;
    fixture.registers[3] = 0x00e5;
    fixture.registers[5] = 0x0060;
    fixture.registers[10] = 0x00c0;
    fixture.registers[11] = 0x01c0;
    fixture.registers[25] = 0x00ea;
    fixture.registers[32] = 0x0010;
    fixture.registers[57] = 0x00c4;
    fixture.registers[58] = 0x00c4;
    return fixture;
}

static void unchanged_capture_and_dac_gain(const CodecFixture *fixture)
{
    assert(fixture->registers[0] == 0x0032);
    assert(fixture->registers[25] == 0x00ea);
    assert(fixture->registers[32] == 0x0010);
    assert(fixture->registers[10] == 0x00c0);
    assert(fixture->registers[11] == 0x01c0);
}

static void verify_playback(const CodecFixture *fixture)
{
    /* Disable the ADC source while preserving the other sidetone fields. */
    assert(fixture->registers[57] == 0x00c0);
    assert(fixture->registers[58] == 0x00c0);
    assert(fixture->registers[2] == 0x00e7);
    assert(fixture->registers[3] == 0x01e7);
    assert(fixture->registers[5] == 0x0060);
    unchanged_capture_and_dac_gain(fixture);
}

int main(void)
{
    uint16_t previous = 0;
    for (int percentage = 0; percentage <= 100; ++percentage) {
        uint16_t code;
        assert(wm_audio_codec_headphone_code(percentage, &code) == 0);
        assert(code >= previous && code <= WM_AUDIO_CODEC_HEADPHONE_ZERO_DB);
        previous = code;
    }
    uint16_t code;
    assert(wm_audio_codec_headphone_code(0, &code) == 0 && code == 85);
    assert(wm_audio_codec_headphone_code(50, &code) == 0 && code == 103);
    assert(wm_audio_codec_headphone_code(100, &code) == 0 && code == 121);
    assert(wm_audio_codec_headphone_code(-1, &code) < 0 && errno == ERANGE);
    assert(code == 121);
    assert(wm_audio_codec_headphone_code(101, &code) < 0 && errno == ERANGE);
    assert(code == 121);
    assert(wm_audio_codec_headphone_code(50, NULL) < 0 && errno == EINVAL);

    CodecFixture fixture = stock_route();
    assert(wm_audio_codec_start_playback(&fixture, update, 103) == 0);
    verify_playback(&fixture);
    static const uint8_t expected[] = {5, 57, 58, 2, 3, 5};
    assert(fixture.count == sizeof(expected));
    assert(memcmp(fixture.updated, expected, sizeof(expected)) == 0);

    /* Startup applies the same route again after driver/sequencer ownership. */
    fixture = stock_route();
    assert(wm_audio_codec_start_playback(&fixture, update, 103) == 0);
    fixture.registers[2] = fixture.registers[3] = 0x00e5;
    fixture.registers[57] = fixture.registers[58] = 0x00c4;
    fixture.count = 0;
    assert(wm_audio_codec_start_playback(&fixture, update, 103) == 0);
    verify_playback(&fixture);

    for (unsigned failure = 1; failure <= sizeof(expected); ++failure) {
        fixture = stock_route();
        fixture.fail_at = failure;
        assert(wm_audio_codec_start_playback(&fixture, update, 103) < 0);
        assert(errno == EIO && fixture.count == failure);
        if (failure > 1) assert(fixture.registers[5] & WM_AUDIO_CODEC_DAC_MUTE);
        unchanged_capture_and_dac_gain(&fixture);
    }
    fixture = stock_route();
    assert(wm_audio_codec_start_playback(&fixture, update, 122) < 0);
    assert(errno == ERANGE && fixture.count == 0);
    assert(wm_audio_codec_start_playback(&fixture, update, 121) == 0);
    assert(fixture.registers[2] == 0x00f9 && fixture.registers[3] == 0x01f9);
    unchanged_capture_and_dac_gain(&fixture);

    /* Teardown must mute the DAC, without touching the microphone PGA. */
    fixture = stock_route();
    assert(wm_audio_codec_mute_playback(&fixture, update) == 0);
    assert(fixture.count == 1 && fixture.updated[0] == 5);
    assert(fixture.registers[5] == 0x0068);
    unchanged_capture_and_dac_gain(&fixture);
    puts("PSVR2 playback-only codec path tests passed.");
    return 0;
}
