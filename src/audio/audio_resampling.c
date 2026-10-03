#include "audio_internal.h"

bool wm_audio_resampling_init(WmAudio *audio) {
    audio->held_resampler =
        cc_audio_resampler_create(WM_AUDIO_HELD_RATE, 1, WM_AUDIO_RATE);
    if (!audio->held_resampler)
        return false;
    size_t taps = cc_audio_resampler_taps(audio->held_resampler);
    for (size_t index = 0; index < WM_AUDIO_MAX_VOICES; ++index) {
        audio->voices[index].resample_state = cc_audio_resample_state_create(taps);
        if (!audio->voices[index].resample_state) {
            wm_audio_resampling_destroy(audio);
            return false;
        }
    }
    return true;
}

void wm_audio_resampling_destroy(WmAudio *audio) {
    if (!audio)
        return;
    for (size_t index = 0; index < WM_AUDIO_MAX_VOICES; ++index) {
        cc_audio_resample_state_destroy(audio->voices[index].resample_state);
        audio->voices[index].resample_state = NULL;
    }
    cc_audio_resampler_destroy(audio->held_resampler);
    audio->held_resampler = NULL;
}
