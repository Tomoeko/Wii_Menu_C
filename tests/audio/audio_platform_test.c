#include "audio_platform.h"
#include "console_common/audio/output.h"

#include <assert.h>
#include <stdio.h>
#include <string.h>

struct CcAudioOutput {
    CcAudioOutputOptions options;
    bool running;
};

typedef struct {
    CcAudioOutput device;
    unsigned opens;
    unsigned starts;
    unsigned stops;
    unsigned closes;
    bool fail_open;
    bool fail_start;
    bool fail_stop;
} PlatformFixture;

static PlatformFixture fixture;
static unsigned render_context;

static void render(void *context, float *stereo, size_t frames) {
    assert(context == &render_context && frames == 7);
    for (size_t frame = 0; frame < frames; ++frame) {
        stereo[frame * 2] = 0.25f;
        stereo[frame * 2 + 1] = -0.5f;
    }
}

CcAudioOutput *cc_audio_output_open(const CcAudioOutputOptions *options) {
    assert(options && options->sample_rate == 48000);
    assert(options->mode == CC_AUDIO_OUTPUT_DIRECT);
    assert(options->render == render && options->context == &render_context);
    ++fixture.opens;
    if (fixture.fail_open)
        return NULL;
    fixture.device.options = *options;
    return &fixture.device;
}

bool cc_audio_output_start(CcAudioOutput *device) {
    assert(device == &fixture.device);
    ++fixture.starts;
    if (fixture.fail_start)
        return false;
    device->running = true;
    return true;
}

bool cc_audio_output_stop(CcAudioOutput *device) {
    assert(device == &fixture.device);
    ++fixture.stops;
    if (fixture.fail_stop)
        return false;
    device->running = false;
    return true;
}

void cc_audio_output_close(CcAudioOutput *device) {
    if (!device)
        return;
    assert(device == &fixture.device);
    ++fixture.closes;
    device->running = false;
}

static void test_open_failure(bool fail_open) {
    fixture = (PlatformFixture){0};
    fixture.fail_open = fail_open;
    fixture.fail_start = !fail_open;
    assert(!wm_audio_device_open(render, &render_context));
    assert(fixture.opens == 1);
    assert(fixture.closes == (fail_open ? 0u : 1u));
}

static void test_lifetime(void) {
    fixture = (PlatformFixture){0};
    WmAudioDevice *device = wm_audio_device_open(render, &render_context);
    assert(device && fixture.starts == 1 && fixture.device.running);
    float stereo[14];
    fixture.device.options.render(fixture.device.options.context, stereo, 7);
    for (size_t frame = 0; frame < 7; ++frame)
        assert(stereo[frame * 2] == 0.25f && stereo[frame * 2 + 1] == -0.5f);
    fixture.fail_stop = true;
    assert(!wm_audio_device_stop(device));
    fixture.fail_stop = false;
    assert(wm_audio_device_stop(device) && !fixture.device.running);
    fixture.fail_start = true;
    assert(!wm_audio_device_start(device));
    fixture.fail_start = false;
    assert(wm_audio_device_start(device) && fixture.device.running);
    wm_audio_device_close(device);
    assert(fixture.closes == 1 && !fixture.device.running);
}

int main(void) {
    assert(!wm_audio_device_open(NULL, NULL));
    assert(!wm_audio_device_start(NULL));
    assert(wm_audio_device_stop(NULL));
    wm_audio_device_close(NULL);
    test_open_failure(true);
    test_open_failure(false);
    test_lifetime();
    puts("Shared audio platform adapter tests passed.");
    return 0;
}
