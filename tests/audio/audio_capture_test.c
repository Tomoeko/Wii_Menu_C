#include "audio_internal.h"

#include <assert.h>
#include <stdlib.h>
#include <string.h>

struct WmAudioDevice {
    unsigned starts;
    unsigned stops;
    unsigned closes;
    bool fail_stop;
};

bool wm_audio_device_start(WmAudioDevice *device) {
    device->starts++;
    return true;
}

bool wm_audio_device_stop(WmAudioDevice *device) {
    device->stops++;
    return !device->fail_stop;
}

void wm_audio_device_close(WmAudioDevice *device) {
    device->closes++;
}

static void reset_voice(WmAudio *audio, const int16_t *samples, float volume,
                        bool muted) {
    audio->clips[0].pcm = (WmAudioPcm){.samples = (int16_t *)samples,
                                       .frame_count = 8,
                                       .sample_rate = 48000,
                                       .loop_end = 8,
                                       .channels = 2,
                                       .looping = true};
    uint64_t generation = audio->controls[0].generation + 1;
    audio->controls[0] = (WmAudioVoiceControl){.generation = generation,
                                               .gain = 3.0f,
                                               .step = 1.0,
                                               .pan_left = 1.0f,
                                               .pan_right = 0.5f,
                                               .pitch = 1.0f,
                                               .active = true};
    audio->master_volume = volume;
    audio->muted = muted;
}

int main(void) {
    WmAudio *audio = calloc(1, sizeof(*audio));
    assert(audio && cc_mutex_init(&audio->mutex) == 0);
    WmAudioDevice device = {0};
    audio->device = &device;
    audio->output_running = true;
    const int16_t samples[16] = {30000, -30000, 12000, -12000, 1000, -1000, 0, 0,
                                 30000, -30000, 12000, -12000, 1000, -1000, 0, 0};
    assert(wm_audio_output_available(audio));
    assert(!wm_audio_capture_begin(audio, 8));
    assert(wm_audio_output_stop(audio));
    assert(wm_audio_output_stop(audio) && device.stops == 1);
    assert(!audio->output_running && audio->device == &device);
    assert(!wm_audio_capture_begin(audio, 0));
    assert(wm_audio_capture_begin(audio, 8));
    assert(!wm_audio_capture_begin(audio, 8));
    assert(wm_audio_output_start(audio));
    assert(wm_audio_output_start(audio) && device.starts == 1);
    wm_audio_capture_end(audio);
    assert(audio->capture);

    reset_voice(audio, samples, 0.75f, false);
    float rendered[16];
    wm_audio_mix(audio, rendered, 4);
    assert(rendered[0] == 1.0f && rendered[1] == -1.0f);
    float captured[16] = {0};
    assert(wm_audio_capture_read(audio, captured, 3) == 3);
    assert(memcmp(rendered, captured, 6 * sizeof(float)) == 0);
    assert(wm_audio_capture_read(audio, captured, 8) == 1);
    assert(memcmp(rendered + 6, captured, 2 * sizeof(float)) == 0);
    assert(wm_audio_capture_read(audio, captured, 8) == 0);
    assert(!wm_audio_capture_failed(audio));

    reset_voice(audio, samples, 0.75f, true);
    wm_audio_mix(audio, rendered, 4);
    assert(wm_audio_capture_read(audio, captured, 8) == 4);
    assert(memcmp(rendered, captured, 8 * sizeof(float)) == 0);
    for (unsigned index = 0; index < 8; ++index)
        assert(captured[index] == 0.0f);
    assert(wm_audio_output_stop(audio));
    wm_audio_capture_end(audio);
    assert(!audio->capture);

    assert(wm_audio_capture_begin(audio, 4));
    assert(wm_audio_output_start(audio));
    reset_voice(audio, samples, 1.0f, false);
    wm_audio_mix(audio, rendered, 3);
    wm_audio_mix(audio, rendered + 6, 3);
    assert(wm_audio_capture_failed(audio));
    assert(wm_audio_capture_read(audio, captured, 8) == 3);
    assert(memcmp(rendered, captured, 6 * sizeof(float)) == 0);
    wm_audio_mix(audio, rendered, 1);
    assert(wm_audio_capture_read(audio, captured, 8) == 0);
    assert(rendered[0] != 0.0f);
    assert(wm_audio_output_stop(audio));
    wm_audio_capture_end(audio);

    assert(wm_audio_output_start(audio));
    device.fail_stop = true;
    assert(!wm_audio_output_stop(audio));
    assert(!wm_audio_output_available(audio) && !audio->output_running);
    assert(device.closes == 1 && !wm_audio_output_start(audio));
    assert(wm_audio_output_stop(NULL));
    assert(!wm_audio_output_available(NULL));
    assert(!wm_audio_capture_failed(NULL));
    wm_audio_capture_end(NULL);
    cc_mutex_destroy(&audio->mutex);
    free(audio);
    return 0;
}
