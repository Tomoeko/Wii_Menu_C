#include "audio_internal.h"

static void retire_finished_locked(WmAudio *audio) {
    for (size_t index = 0; index < WM_AUDIO_MAX_VOICES; index++) {
        WmAudioVoiceControl *control = &audio->controls[index];
        const WmAudioVoice *voice = &audio->voices[index];
        /* A new request may already have reused this slot. Completion of
         * the previous generation must never cancel that pending request. */
        if (!voice->active && control->generation == voice->applied.generation) {
            control->active = false;
        }
    }
}

static void apply_control(WmAudio *audio, size_t index) {
    const WmAudioVoiceControl *control = &audio->controls[index];
    WmAudioVoice *voice = &audio->voices[index];
    if (!control->active) {
        voice->active = false;
        voice->applied = *control;
        return;
    }

    const WmAudioClip *clip = &audio->clips[control->clip_index];
    bool starting = control->generation != voice->applied.generation;
    if (starting) {
        CcAudioResampleState *resample_state = voice->resample_state;
        cc_audio_resample_state_reset(resample_state);
        *voice = (WmAudioVoice){.clip_index = control->clip_index,
                                .held_cursor = WM_AUDIO_HELD_BLOCK,
                                .resample_state = resample_state,
                                .active = true};
        if (clip->held_profile)
            wm_audio_held_start(&voice->held_state);
    }

    /* A repeated snapshot must not reset a fade's evolving gain. */
    if (starting || control->gain != voice->applied.gain)
        voice->gain = control->gain;
    voice->step = control->step;
    voice->pan_left = control->pan_left;
    voice->pan_right = control->pan_right;
    voice->pan = control->pan;
    voice->pitch = control->pitch;
    voice->paused = control->paused;
    if (control->releasing && !voice->applied.releasing) {
        if (clip->held_profile) {
            wm_audio_held_release(&voice->held_state);
        } else if (control->fade_frames) {
            voice->fade_frames = control->fade_frames;
            voice->fade_step = -voice->gain / (float)control->fade_frames;
        }
    }
    voice->applied = *control;
}

void wm_audio_refresh_controls(WmAudio *audio) {
    if (pthread_mutex_trylock(&audio->mutex) != 0)
        return;
    retire_finished_locked(audio);
    for (size_t index = 0; index < WM_AUDIO_MAX_VOICES; index++)
        apply_control(audio, index);
    audio->mixer_volume = audio->master_volume;
    audio->mixer_muted = audio->muted;
    pthread_mutex_unlock(&audio->mutex);
}

void wm_audio_retire_finished(WmAudio *audio) {
    if (pthread_mutex_trylock(&audio->mutex) != 0)
        return;
    retire_finished_locked(audio);
    pthread_mutex_unlock(&audio->mutex);
}
