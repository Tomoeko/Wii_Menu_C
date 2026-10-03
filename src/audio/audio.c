#define _POSIX_C_SOURCE 200809L

#include "audio_internal.h"
#include "wii_menu/menu/local_catalog.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

static uint64_t monotonic_nanoseconds(void) {
    struct timespec time;
    clock_gettime(CLOCK_MONOTONIC, &time);
    return (uint64_t)time.tv_sec * 1000000000u + (uint64_t)time.tv_nsec;
}

static WmAudioVoiceControl *voice_by_kind(WmAudio *audio, WmVoiceKind kind) {
    for (size_t index = 0; index < WM_AUDIO_MAX_VOICES; index++) {
        WmAudioVoiceControl *voice = &audio->controls[index];
        if (voice->active && voice->kind == kind)
            return voice;
    }
    return NULL;
}

static WmAudioVoiceControl *new_voice(WmAudio *audio, const WmAudioClip *clip,
                                      WmVoiceKind kind) {
    size_t clip_index = (size_t)(clip - audio->clips);
    for (size_t index = 0; index < WM_AUDIO_MAX_VOICES; index++) {
        WmAudioVoiceControl *voice = &audio->controls[index];
        if (voice->active)
            continue;
        uint64_t generation = voice->generation + 1;
        if (!generation)
            generation = 1;
        *voice = (WmAudioVoiceControl){
            .clip_index = clip_index,
            .kind = kind,
            .generation = generation,
            .step = (double)clip->pcm.sample_rate / WM_AUDIO_RATE,
            .gain = clip->gain,
            .pan_left = 1.0f,
            .pan_right = 1.0f,
            .pitch = 1.0f,
            .active = true,
            .paused = audio->menu_paused && kind != WM_VOICE_HOME};
        return voice;
    }
    return NULL;
}

static void fade_voice(WmAudioVoiceControl *voice, size_t frames) {
    if (!voice || !voice->active)
        return;
    if (!frames) {
        voice->active = false;
        return;
    }
    voice->fade_frames = frames;
    voice->releasing = true;
    voice->paused = false;
}

WmAudio *wm_audio_create(const char *assets_directory) {
    if (!assets_directory || !*assets_directory ||
        strlen(assets_directory) >= sizeof(((WmAudio *)0)->assets))
        return NULL;
    WmAudio *audio = calloc(1, sizeof(*audio));
    if (!audio)
        return NULL;
    strcpy(audio->assets, assets_directory);
    audio->master_volume = 1.0f;
    audio->mixer_volume = 1.0f;
    audio->active_preview = -1;
    if (pthread_mutex_init(&audio->mutex, NULL) != 0) {
        free(audio);
        return NULL;
    }
    if (!wm_audio_resampling_init(audio)) {
        pthread_mutex_destroy(&audio->mutex);
        free(audio);
        return NULL;
    }
    char path[4096];
    int length = snprintf(path, sizeof(path), "%s/audio-direct.json", audio->assets);
    if (length > 0 && length < (int)sizeof(path))
        wm_json_load(&audio->direct_manifest, path, 1024 * 1024);
    length = snprintf(path, sizeof(path), "%s/audio-sequence.json", audio->assets);
    if (length > 0 && length < (int)sizeof(path))
        wm_json_load(&audio->sequence_manifest, path, 1024 * 1024);
    wm_audio_load_held_profiles(audio);
    static const char *const startup_cues[] = {"background",
                                               "backgroundIntro",
                                               "hover",
                                               "buttonHover",
                                               "WIPL_SE_BOARD_FOCUS",
                                               "WIPL_SE_MSG_DISP",
                                               "WIPL_SE_MSG_HOUSE",
                                               "select",
                                               "click",
                                               "back",
                                               "page",
                                               "confirm",
                                               "cancel",
                                               "discPreview",
                                               "HOMESE_HOME_BUTTON",
                                               "HOMESE_FOCUS"};
    for (size_t index = 0; index < sizeof(startup_cues) / sizeof(startup_cues[0]);
         index++) {
        const WmJson *manifest = NULL;
        if (wm_audio_manifest_entry(audio, startup_cues[index], &manifest) !=
            WM_JSON_INVALID) {
            wm_audio_load_clip(audio, startup_cues[index], "audio");
        }
    }
    audio->device = wm_audio_device_open(wm_audio_mix, audio);
    audio->output_running = audio->device != NULL;
    if (!audio->device) {
        fprintf(stderr, "System audio output unavailable.\n");
    }
    return audio;
}

void wm_audio_destroy(WmAudio *audio) {
    if (!audio)
        return;
    wm_audio_device_close(audio->device);
    audio->device = NULL;
    audio->output_running = false;
    wm_audio_capture_end(audio);
    wm_audio_resampling_destroy(audio);
    for (size_t index = 0; index < audio->clip_count; index++)
        wm_audio_pcm_free(&audio->clips[index].pcm);
    wm_json_free(&audio->direct_manifest);
    wm_json_free(&audio->sequence_manifest);
    pthread_mutex_destroy(&audio->mutex);
    free(audio);
}

bool wm_audio_play(WmAudio *audio, const char *name) {
    return wm_audio_play_panned(audio, name, 0.0f);
}

bool wm_audio_play_panned(WmAudio *audio, const char *name, float pan) {
    if (!audio || !audio->device || !wm_audio_safe_name(name) || !isfinite(pan))
        return false;
    pan = fminf(fmaxf(pan, -1.0f), 1.0f);
    pthread_mutex_lock(&audio->mutex);
    bool muted = audio->muted;
    pthread_mutex_unlock(&audio->mutex);
    if (muted)
        return false;
    if (strcmp(name, "hover") == 0) {
        uint64_t now = monotonic_nanoseconds();
        if (audio->last_hover_ns && now - audio->last_hover_ns < 45000000u)
            return false;
        audio->last_hover_ns = now;
    }
    WmAudioClip *clip = wm_audio_load_clip(audio, name, "audio");
    if (!clip)
        return false;
    WmVoiceKind kind =
        strncmp(name, "HOMESE_", 7) == 0 || strncmp(name, "HOME_SPEAKER_", 13) == 0
            ? WM_VOICE_HOME
            : WM_VOICE_EFFECT;
    pthread_mutex_lock(&audio->mutex);
    WmAudioVoiceControl *voice = audio->muted ? NULL : new_voice(audio, clip, kind);
    if (voice) {
        voice->pan = pan;
        voice->pan_left = pan > 0.0f ? 1.0f - pan : 1.0f;
        voice->pan_right = pan < 0.0f ? 1.0f + pan : 1.0f;
    }
    pthread_mutex_unlock(&audio->mutex);
    return voice != NULL;
}

bool wm_audio_start_loop(WmAudio *audio, const char *name) {
    if (!audio || !audio->device || !wm_audio_safe_name(name))
        return false;
    WmAudioClip *clip = wm_audio_load_clip(audio, name, "audio");
    if (!clip || !clip->pcm.looping)
        return false;
    size_t clip_index = (size_t)(clip - audio->clips);
    pthread_mutex_lock(&audio->mutex);
    for (size_t index = 0; index < WM_AUDIO_MAX_VOICES; index++) {
        WmAudioVoiceControl *voice = &audio->controls[index];
        if (voice->active && !voice->releasing && voice->clip_index == clip_index) {
            pthread_mutex_unlock(&audio->mutex);
            return true;
        }
    }
    bool started = new_voice(audio, clip, WM_VOICE_EFFECT) != NULL;
    pthread_mutex_unlock(&audio->mutex);
    return started;
}

static void set_loop_controls(WmAudioVoiceControl *voice, const WmAudioClip *clip,
                              float gain, float pan, float pitch) {
    voice->gain = clip->gain * fminf(fmaxf(gain, 0.0f), 2.0f);
    voice->pan = fminf(fmaxf(pan, -1.0f), 1.0f);
    voice->pitch = fminf(fmaxf(pitch, 0.25f), 4.0f);
    voice->step = (double)clip->pcm.sample_rate * voice->pitch / WM_AUDIO_RATE;
    voice->pan_left = voice->pan > 0.0f ? 1.0f - voice->pan : 1.0f;
    voice->pan_right = voice->pan < 0.0f ? 1.0f + voice->pan : 1.0f;
}

bool wm_audio_hold_loop(WmAudio *audio, const char *name, float gain, float pan,
                        float pitch) {
    if (!audio || !audio->device || !wm_audio_safe_name(name) || !isfinite(gain) ||
        !isfinite(pan) || !isfinite(pitch))
        return false;
    WmAudioClip *clip = wm_audio_load_clip(audio, name, "audio");
    if (!clip || !clip->pcm.looping)
        return false;
    size_t clip_index = (size_t)(clip - audio->clips);
    pthread_mutex_lock(&audio->mutex);
    WmAudioVoiceControl *held = NULL;
    for (size_t index = 0; index < WM_AUDIO_MAX_VOICES; index++) {
        WmAudioVoiceControl *voice = &audio->controls[index];
        if (voice->active && !voice->releasing && voice->clip_index == clip_index) {
            held = voice;
            break;
        }
    }
    if (!held)
        held = new_voice(audio, clip, WM_VOICE_EFFECT);
    if (held)
        set_loop_controls(held, clip, gain, pan, pitch);
    pthread_mutex_unlock(&audio->mutex);
    return held != NULL;
}

void wm_audio_stop_loop(WmAudio *audio, const char *name) {
    if (!audio || !wm_audio_safe_name(name))
        return;
    pthread_mutex_lock(&audio->mutex);
    for (size_t index = 0; index < WM_AUDIO_MAX_VOICES; index++) {
        WmAudioVoiceControl *voice = &audio->controls[index];
        if (!voice->active || voice->releasing ||
            strcmp(audio->clips[voice->clip_index].name, name) != 0)
            continue;
        if (audio->clips[voice->clip_index].held_profile) {
            voice->releasing = true;
        } else {
            fade_voice(voice, 0);
        }
    }
    pthread_mutex_unlock(&audio->mutex);
}

void wm_audio_set_loop(WmAudio *audio, const char *name, float gain, float pan,
                       float pitch) {
    if (!audio || !wm_audio_safe_name(name) || !isfinite(gain) || !isfinite(pan) ||
        !isfinite(pitch))
        return;
    pthread_mutex_lock(&audio->mutex);
    for (size_t index = 0; index < WM_AUDIO_MAX_VOICES; index++) {
        WmAudioVoiceControl *voice = &audio->controls[index];
        if (!voice->active || voice->releasing ||
            strcmp(audio->clips[voice->clip_index].name, name) != 0)
            continue;
        const WmAudioClip *clip = &audio->clips[voice->clip_index];
        set_loop_controls(voice, clip, gain, pan, pitch);
    }
    pthread_mutex_unlock(&audio->mutex);
}

void wm_audio_set_volume(WmAudio *audio, float volume) {
    if (!audio || !isfinite(volume))
        return;
    if (volume < 0.0f)
        volume = 0.0f;
    if (volume > 1.0f)
        volume = 1.0f;
    pthread_mutex_lock(&audio->mutex);
    audio->master_volume = volume;
    pthread_mutex_unlock(&audio->mutex);
}

void wm_audio_set_muted(WmAudio *audio, bool muted) {
    if (!audio)
        return;
    pthread_mutex_lock(&audio->mutex);
    audio->muted = muted;
    pthread_mutex_unlock(&audio->mutex);
}

void wm_audio_reset_all(WmAudio *audio) {
    if (!audio)
        return;
    pthread_mutex_lock(&audio->mutex);
    for (size_t index = 0; index < WM_AUDIO_MAX_VOICES; index++)
        audio->controls[index].active = false;
    audio->background_started = false;
    audio->background_paused = false;
    audio->menu_paused = false;
    audio->active_preview = -1;
    audio->last_hover_ns = 0;
    pthread_mutex_unlock(&audio->mutex);
}

static void set_paused(WmAudio *audio, WmVoiceKind kind, bool paused) {
    for (size_t index = 0; index < WM_AUDIO_MAX_VOICES; index++) {
        WmAudioVoiceControl *voice = &audio->controls[index];
        if (voice->active && voice->kind == kind)
            voice->paused = paused;
    }
}

static void update_background(WmAudio *audio, bool paused) {
    WmAudioClip *background = NULL;
    WmAudioClip *intro = NULL;
    if (!audio->background_started) {
        background = wm_audio_load_clip(audio, "background", "audio");
        intro = wm_audio_load_clip(audio, "backgroundIntro", "audio");
    }
    pthread_mutex_lock(&audio->mutex);
    audio->background_paused = paused;
    if (!audio->background_started) {
        size_t required = (background ? 1u : 0u) + (intro ? 1u : 0u);
        size_t available = 0;
        for (size_t index = 0; index < WM_AUDIO_MAX_VOICES; index++) {
            if (!audio->controls[index].active)
                available++;
        }
        /* Both sources start together. A full effect pool postpones the
         * start rather than permanently dropping part of the soundtrack. */
        if (available >= required) {
            if (background)
                new_voice(audio, background, WM_VOICE_BACKGROUND);
            if (intro)
                new_voice(audio, intro, WM_VOICE_INTRO);
            audio->background_started = true;
        }
    }
    set_paused(audio, WM_VOICE_BACKGROUND, paused || audio->menu_paused);
    set_paused(audio, WM_VOICE_INTRO, paused || audio->menu_paused);
    pthread_mutex_unlock(&audio->mutex);
}

static bool safe_channel_id(const char *id) {
    if (!wm_audio_safe_name(id))
        return false;
    if (wm_local_channel_id_valid(id))
        return true;
    if (strlen(id) != 16)
        return false;
    for (const char *digit = id; *digit; digit++) {
        if ((*digit < '0' || *digit > '9') && (*digit < 'a' || *digit > 'f') &&
            (*digit < 'A' || *digit > 'F'))
            return false;
    }
    return true;
}

static void update_preview(WmAudio *audio, const WmMenu *menu) {
    bool has_preview = menu->screen == WM_SCREEN_PREVIEW && menu->selected >= 0 &&
                       menu->selected < WM_SLOT_COUNT;
    bool change_in =
        menu->transition == WM_TRANSITION_PREVIEW &&
        wm_menu_preview_presentation(menu).phase == WM_PREVIEW_PHASE_CHANGE_IN;
    bool change_out = menu->transition == WM_TRANSITION_PREVIEW && !change_in;
    if (change_in)
        return;
    if (change_out || !has_preview || menu->transition == WM_TRANSITION_SELECT) {
        if (audio->active_preview >= 0) {
            pthread_mutex_lock(&audio->mutex);
            fade_voice(voice_by_kind(audio, WM_VOICE_CHANNEL),
                       menu->transition == WM_TRANSITION_BACK
                           ? (size_t)(28 * WM_AUDIO_RATE / 60)
                           : 0);
            pthread_mutex_unlock(&audio->mutex);
            audio->active_preview = -1;
        }
        return;
    }
    if (audio->active_preview == menu->selected ||
        menu->transition != WM_TRANSITION_NONE)
        return;
    const WmChannel *channel = &menu->slots[menu->selected];
    WmAudioClip *clip = NULL;
    if (strcmp(channel->id, "disc") == 0) {
        clip = wm_audio_load_clip(audio, "discPreview", "audio");
    } else if (safe_channel_id(channel->id)) {
        clip = wm_audio_load_clip(audio, channel->id, "channel-audio");
    }
    audio->active_preview = menu->selected;
    if (!clip)
        return;
    pthread_mutex_lock(&audio->mutex);
    fade_voice(voice_by_kind(audio, WM_VOICE_CHANNEL), 0);
    new_voice(audio, clip, WM_VOICE_CHANNEL);
    pthread_mutex_unlock(&audio->mutex);
}

void wm_audio_sync(WmAudio *audio, const WmMenu *menu) {
    if (!audio || !menu || !audio->device)
        return;
    bool home_paused;
    if (menu->transition == WM_TRANSITION_HOME) {
        home_paused = menu->transition_from_home_open;
    } else {
        home_paused = menu->home_open;
    }
    if (home_paused != audio->menu_paused) {
        pthread_mutex_lock(&audio->mutex);
        audio->menu_paused = home_paused;
        set_paused(audio, WM_VOICE_EFFECT, home_paused);
        set_paused(audio, WM_VOICE_CHANNEL, home_paused);
        pthread_mutex_unlock(&audio->mutex);
    }
    bool background_paused =
        menu->screen == WM_SCREEN_PREVIEW || menu->transition == WM_TRANSITION_BACK;
    update_background(audio, background_paused);
    update_preview(audio, menu);
}
