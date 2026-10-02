#include "audio_internal.h"

bool wm_audio_output_stop(WmAudio *audio) {
    if (!audio || !audio->output_running)
        return true;
    bool okay = wm_audio_device_stop(audio->device);
    if (!okay) {
        wm_audio_device_close(audio->device);
        audio->device = NULL;
    }
    audio->output_running = false;
    return okay;
}

bool wm_audio_output_start(WmAudio *audio) {
    if (!audio || !audio->device)
        return false;
    if (!audio->output_running)
        audio->output_running = wm_audio_device_start(audio->device);
    return audio->output_running;
}

bool wm_audio_output_available(const WmAudio *audio) {
    return audio && audio->device;
}

bool wm_audio_capture_begin(WmAudio *audio, size_t capacity_frames) {
    if (!audio || audio->output_running || audio->capture)
        return false;
    audio->capture = cc_audio_buffer_create(capacity_frames);
    return audio->capture != NULL;
}

size_t wm_audio_capture_read(WmAudio *audio, float *stereo, size_t capacity_frames) {
    return audio ? cc_audio_buffer_read(audio->capture, stereo, capacity_frames, NULL)
                 : 0;
}

bool wm_audio_capture_failed(const WmAudio *audio) {
    return audio && cc_audio_buffer_failed(audio->capture);
}

void wm_audio_capture_end(WmAudio *audio) {
    if (!audio || audio->output_running)
        return;
    cc_audio_buffer_destroy(audio->capture);
    audio->capture = NULL;
}
