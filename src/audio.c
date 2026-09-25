#define _POSIX_C_SOURCE 200809L

#include "wii_menu/audio.h"
#include "wii_menu/audio_wave.h"
#include "wii_menu/json.h"

#include "audio_platform.h"

#include <math.h>
#include <pthread.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

enum {
    WM_AUDIO_RATE = 48000,
    WM_AUDIO_MAX_CLIPS = 160,
    WM_AUDIO_MAX_VOICES = 32
};

typedef enum WmVoiceKind {
    WM_VOICE_EFFECT,
    WM_VOICE_HOME,
    WM_VOICE_BACKGROUND,
    WM_VOICE_INTRO,
    WM_VOICE_CHANNEL
} WmVoiceKind;

typedef struct WmAudioClip {
    char name[96];
    WmAudioPcm pcm;
    float gain;
    bool missing;
} WmAudioClip;

typedef struct WmAudioVoice {
    size_t clip_index;
    WmVoiceKind kind;
    double frame;
    double step;
    float gain;
    float pan_left;
    float pan_right;
    float fade_step;
    size_t fade_frames;
    bool active;
    bool paused;
} WmAudioVoice;

struct WmAudio {
    char assets[4096];
    WmAudioClip clips[WM_AUDIO_MAX_CLIPS];
    size_t clip_count;
    WmAudioVoice voices[WM_AUDIO_MAX_VOICES];
    pthread_mutex_t mutex;
    WmAudioDevice *device;
    WmJson direct_manifest;
    WmJson sequence_manifest;
    uint64_t last_hover_ns;
    bool background_started;
    bool menu_paused;
    bool background_paused;
    float master_volume;
    bool muted;
    int active_preview;
};

static uint64_t monotonic_nanoseconds(void)
{
    struct timespec time;
    clock_gettime(CLOCK_MONOTONIC, &time);
    return (uint64_t)time.tv_sec * 1000000000u + (uint64_t)time.tv_nsec;
}

static bool safe_name(const char *name)
{
    if (!name || !*name || strlen(name) >= sizeof(((WmAudioClip *)0)->name))
        return false;
    for (const unsigned char *part = (const unsigned char *)name; *part; part++) {
        if (!((*part >= 'A' && *part <= 'Z') ||
              (*part >= 'a' && *part <= 'z') ||
              (*part >= '0' && *part <= '9') || *part == '_' || *part == '-'))
            return false;
    }
    return true;
}

static bool path_for(char path[4096], const char *assets,
                     const char *directory, const char *name)
{
    int length = snprintf(path, 4096, "%s/%s/%s.wav", assets, directory, name);
    return length > 0 && length < 4096;
}

static float json_number(const WmJson *json, size_t token, float fallback)
{
    if (token >= json->count || json->tokens[token].type != WM_JSON_NUMBER)
        return fallback;
    size_t length = json->tokens[token].end - json->tokens[token].start;
    if (length == 0 || length >= 64) return fallback;
    char buffer[64];
    memcpy(buffer, json->source + json->tokens[token].start, length);
    buffer[length] = '\0';
    char *end;
    float value = strtof(buffer, &end);
    return end != buffer && *end == '\0' && isfinite(value) ? value : fallback;
}

static size_t manifest_entry(const WmAudio *audio, const char *name,
                             const WmJson **owner)
{
    const WmJson *manifests[] = {
        &audio->sequence_manifest,
        &audio->direct_manifest
    };
    for (size_t index = 0; index < sizeof(manifests) / sizeof(manifests[0]);
         index++) {
        const WmJson *manifest = manifests[index];
        if (!manifest->count) continue;
        size_t entry = wm_json_member(manifest, 0, name);
        if (entry != WM_JSON_INVALID) {
            *owner = manifest;
            return entry;
        }
    }
    return WM_JSON_INVALID;
}

static WmAudioClip *load_clip(WmAudio *audio, const char *name,
                              const char *directory)
{
    if (!audio || !safe_name(name)) return NULL;
    for (size_t index = 0; index < audio->clip_count; index++) {
        if (strcmp(audio->clips[index].name, name) == 0)
            return audio->clips[index].missing ? NULL : &audio->clips[index];
    }
    if (audio->clip_count == WM_AUDIO_MAX_CLIPS) return NULL;
    WmAudioClip *clip = &audio->clips[audio->clip_count++];
    strcpy(clip->name, name);
    char path[4096], error[128];
    if (!path_for(path, audio->assets, directory, name) ||
        !wm_audio_wav_read(path, &clip->pcm, error, sizeof(error))) {
        clip->missing = true;
        fprintf(stderr, "Audio cue unavailable: %s\n", name);
        return NULL;
    }
    clip->gain = 1.0f;
    if (strcmp(directory, "audio") == 0) {
        const WmJson *manifest = NULL;
        size_t entry = manifest_entry(audio, name, &manifest);
        if (entry != WM_JSON_INVALID) {
            clip->gain = json_number(manifest,
                                     wm_json_member(manifest, entry, "gain"),
                                     1.0f);
            float loop_start = json_number(
                manifest, wm_json_member(manifest, entry, "loopStart"), -1.0f);
            float loop_end = json_number(
                manifest, wm_json_member(manifest, entry, "loopEnd"), -1.0f);
            size_t loop_flag = wm_json_member(manifest, entry, "loop");
            if (loop_flag != WM_JSON_INVALID &&
                manifest->tokens[loop_flag].type == WM_JSON_BOOLEAN &&
                manifest->source[manifest->tokens[loop_flag].start] == 't' &&
                loop_start >= 0.0f && loop_end > loop_start) {
                uint32_t first = (uint32_t)lroundf(
                    loop_start * clip->pcm.sample_rate);
                uint32_t end = (uint32_t)lroundf(
                    loop_end * clip->pcm.sample_rate);
                if (first < end && end <= clip->pcm.frame_count) {
                    clip->pcm.looping = true;
                    clip->pcm.loop_start = first;
                    clip->pcm.loop_end = end;
                }
            }
        }
    }
    if (!isfinite(clip->gain) || clip->gain < 0.0f) clip->gain = 1.0f;
    return clip;
}

static WmAudioVoice *voice_by_kind(WmAudio *audio, WmVoiceKind kind)
{
    for (size_t index = 0; index < WM_AUDIO_MAX_VOICES; index++) {
        WmAudioVoice *voice = &audio->voices[index];
        if (voice->active && voice->kind == kind) return voice;
    }
    return NULL;
}

static WmAudioVoice *new_voice(WmAudio *audio, const WmAudioClip *clip,
                               WmVoiceKind kind)
{
    size_t clip_index = (size_t)(clip - audio->clips);
    for (size_t index = 0; index < WM_AUDIO_MAX_VOICES; index++) {
        WmAudioVoice *voice = &audio->voices[index];
        if (voice->active) continue;
        *voice = (WmAudioVoice){
            .clip_index = clip_index,
            .kind = kind,
            .step = (double)clip->pcm.sample_rate / WM_AUDIO_RATE,
            .gain = clip->gain,
            .pan_left = 1.0f,
            .pan_right = 1.0f,
            .active = true,
            .paused = audio->menu_paused && kind != WM_VOICE_HOME
        };
        return voice;
    }
    return NULL;
}

static void fade_voice(WmAudioVoice *voice, size_t frames)
{
    if (!voice || !voice->active) return;
    if (!frames) {
        voice->active = false;
        return;
    }
    voice->fade_frames = frames;
    voice->fade_step = -voice->gain / (float)frames;
    voice->paused = false;
}

static float sample_at(const WmAudioClip *clip, uint32_t frame, uint8_t channel)
{
    if (clip->pcm.channels == 1) channel = 0;
    return (float)clip->pcm.samples[(size_t)frame * clip->pcm.channels + channel] /
           32768.0f;
}

static void mix_audio(void *context, float *interleaved, size_t frames)
{
    WmAudio *audio = context;
    if (!audio || !interleaved || frames > SIZE_MAX / (2 * sizeof(float)))
        return;
    memset(interleaved, 0, frames * 2 * sizeof(float));
    if (pthread_mutex_trylock(&audio->mutex) != 0) return;
    for (size_t index = 0; index < WM_AUDIO_MAX_VOICES; index++) {
        WmAudioVoice *voice = &audio->voices[index];
        if (!voice->active || voice->paused) continue;
        const WmAudioClip *clip = &audio->clips[voice->clip_index];
        uint32_t end = clip->pcm.looping ? clip->pcm.loop_end :
                       clip->pcm.frame_count;
        for (size_t output = 0; output < frames; output++) {
            if (voice->frame >= end) {
                if (!clip->pcm.looping) {
                    voice->active = false;
                    break;
                }
                double span = end - clip->pcm.loop_start;
                voice->frame = clip->pcm.loop_start +
                               fmod(voice->frame - clip->pcm.loop_start, span);
            }
            uint32_t first = (uint32_t)voice->frame;
            uint32_t next = first + 1;
            if (next >= end) next = clip->pcm.looping ? clip->pcm.loop_start : first;
            float fraction = (float)(voice->frame - first);
            for (uint8_t channel = 0; channel < 2; channel++) {
                float first_sample = sample_at(clip, first, channel);
                float next_sample = sample_at(clip, next, channel);
                interleaved[output * 2 + channel] +=
                    (first_sample + (next_sample - first_sample) * fraction) *
                    voice->gain *
                    (channel == 0 ? voice->pan_left : voice->pan_right);
            }
            voice->frame += voice->step;
            if (voice->fade_frames) {
                voice->gain += voice->fade_step;
                voice->fade_frames--;
                if (!voice->fade_frames || voice->gain <= 0.0f) {
                    voice->active = false;
                    break;
                }
            }
        }
    }
    for (size_t frame = 0; frame < frames * 2; frame++) {
        interleaved[frame] *= audio->muted ? 0.0f : audio->master_volume;
        if (interleaved[frame] > 1.0f) interleaved[frame] = 1.0f;
        if (interleaved[frame] < -1.0f) interleaved[frame] = -1.0f;
    }
    pthread_mutex_unlock(&audio->mutex);
}

WmAudio *wm_audio_create(const char *assets_directory)
{
    if (!assets_directory || !*assets_directory ||
        strlen(assets_directory) >= sizeof(((WmAudio *)0)->assets))
        return NULL;
    WmAudio *audio = calloc(1, sizeof(*audio));
    if (!audio) return NULL;
    strcpy(audio->assets, assets_directory);
    audio->master_volume = 1.0f;
    audio->active_preview = -1;
    if (pthread_mutex_init(&audio->mutex, NULL) != 0) {
        free(audio);
        return NULL;
    }
    char path[4096];
    int length = snprintf(path, sizeof(path), "%s/audio-direct.json",
                          audio->assets);
    if (length > 0 && length < (int)sizeof(path))
        wm_json_load(&audio->direct_manifest, path, 1024 * 1024);
    length = snprintf(path, sizeof(path), "%s/audio-sequence.json", audio->assets);
    if (length > 0 && length < (int)sizeof(path))
        wm_json_load(&audio->sequence_manifest, path, 1024 * 1024);
    static const char *const startup_cues[] = {
        "background", "backgroundIntro", "hover", "buttonHover",
        "select", "click", "back", "page", "confirm", "cancel",
        "discPreview", "HOMESE_HOME_BUTTON", "HOMESE_FOCUS"
    };
    for (size_t index = 0;
         index < sizeof(startup_cues) / sizeof(startup_cues[0]); index++) {
        const WmJson *manifest = NULL;
        if (manifest_entry(audio, startup_cues[index], &manifest) !=
            WM_JSON_INVALID) {
            load_clip(audio, startup_cues[index], "audio");
        }
    }
    audio->device = wm_audio_device_open(mix_audio, audio);
    if (!audio->device) {
        fprintf(stderr, "System audio output unavailable.\n");
    }
    return audio;
}

void wm_audio_destroy(WmAudio *audio)
{
    if (!audio) return;
    wm_audio_device_close(audio->device);
    for (size_t index = 0; index < audio->clip_count; index++)
        wm_audio_pcm_free(&audio->clips[index].pcm);
    wm_json_free(&audio->direct_manifest);
    wm_json_free(&audio->sequence_manifest);
    pthread_mutex_destroy(&audio->mutex);
    free(audio);
}

bool wm_audio_play(WmAudio *audio, const char *name)
{
    if (!audio || !audio->device || !safe_name(name)) return false;
    pthread_mutex_lock(&audio->mutex);
    bool muted = audio->muted;
    pthread_mutex_unlock(&audio->mutex);
    if (muted) return false;
    if (strcmp(name, "hover") == 0) {
        uint64_t now = monotonic_nanoseconds();
        if (audio->last_hover_ns && now - audio->last_hover_ns < 45000000u)
            return false;
        audio->last_hover_ns = now;
    }
    WmAudioClip *clip = load_clip(audio, name, "audio");
    if (!clip) return false;
    WmVoiceKind kind = strncmp(name, "HOMESE_", 7) == 0 ||
                       strncmp(name, "HOME_SPEAKER_", 13) == 0
                           ? WM_VOICE_HOME : WM_VOICE_EFFECT;
    pthread_mutex_lock(&audio->mutex);
    WmAudioVoice *voice = audio->muted ? NULL : new_voice(audio, clip, kind);
    pthread_mutex_unlock(&audio->mutex);
    return voice != NULL;
}

bool wm_audio_start_loop(WmAudio *audio, const char *name)
{
    if (!audio || !audio->device || !safe_name(name)) return false;
    WmAudioClip *clip = load_clip(audio, name, "audio");
    if (!clip || !clip->pcm.looping) return false;
    size_t clip_index = (size_t)(clip - audio->clips);
    pthread_mutex_lock(&audio->mutex);
    for (size_t index = 0; index < WM_AUDIO_MAX_VOICES; index++) {
        WmAudioVoice *voice = &audio->voices[index];
        if (voice->active && voice->clip_index == clip_index) {
            pthread_mutex_unlock(&audio->mutex);
            return true;
        }
    }
    bool started = new_voice(audio, clip, WM_VOICE_EFFECT) != NULL;
    pthread_mutex_unlock(&audio->mutex);
    return started;
}

void wm_audio_stop_loop(WmAudio *audio, const char *name)
{
    if (!audio || !safe_name(name)) return;
    pthread_mutex_lock(&audio->mutex);
    for (size_t index = 0; index < WM_AUDIO_MAX_VOICES; index++) {
        WmAudioVoice *voice = &audio->voices[index];
        if (voice->active &&
            strcmp(audio->clips[voice->clip_index].name, name) == 0)
            fade_voice(voice, 0);
    }
    pthread_mutex_unlock(&audio->mutex);
}

void wm_audio_set_loop(WmAudio *audio, const char *name,
                       float gain, float pan, float pitch)
{
    if (!audio || !safe_name(name) || !isfinite(gain) ||
        !isfinite(pan) || !isfinite(pitch)) return;
    if (gain < 0.0f) gain = 0.0f;
    if (gain > 2.0f) gain = 2.0f;
    if (pan < -1.0f) pan = -1.0f;
    if (pan > 1.0f) pan = 1.0f;
    if (pitch < 0.25f) pitch = 0.25f;
    if (pitch > 4.0f) pitch = 4.0f;
    pthread_mutex_lock(&audio->mutex);
    for (size_t index = 0; index < WM_AUDIO_MAX_VOICES; index++) {
        WmAudioVoice *voice = &audio->voices[index];
        if (!voice->active ||
            strcmp(audio->clips[voice->clip_index].name, name) != 0) continue;
        const WmAudioClip *clip = &audio->clips[voice->clip_index];
        voice->gain = clip->gain * gain;
        voice->step = (double)clip->pcm.sample_rate * pitch / WM_AUDIO_RATE;
        voice->pan_left = pan > 0.0f ? 1.0f - pan : 1.0f;
        voice->pan_right = pan < 0.0f ? 1.0f + pan : 1.0f;
    }
    pthread_mutex_unlock(&audio->mutex);
}

void wm_audio_set_volume(WmAudio *audio, float volume)
{
    if (!audio || !isfinite(volume)) return;
    if (volume < 0.0f) volume = 0.0f;
    if (volume > 1.0f) volume = 1.0f;
    pthread_mutex_lock(&audio->mutex);
    audio->master_volume = volume;
    pthread_mutex_unlock(&audio->mutex);
}

void wm_audio_set_muted(WmAudio *audio, bool muted)
{
    if (!audio) return;
    pthread_mutex_lock(&audio->mutex);
    audio->muted = muted;
    pthread_mutex_unlock(&audio->mutex);
}

void wm_audio_reset_all(WmAudio *audio)
{
    if (!audio) return;
    pthread_mutex_lock(&audio->mutex);
    for (size_t index = 0; index < WM_AUDIO_MAX_VOICES; index++)
        audio->voices[index].active = false;
    audio->background_started = false;
    audio->background_paused = false;
    audio->menu_paused = false;
    audio->active_preview = -1;
    audio->last_hover_ns = 0;
    pthread_mutex_unlock(&audio->mutex);
}

static void set_paused(WmAudio *audio, WmVoiceKind kind, bool paused)
{
    for (size_t index = 0; index < WM_AUDIO_MAX_VOICES; index++) {
        WmAudioVoice *voice = &audio->voices[index];
        if (voice->active && voice->kind == kind) voice->paused = paused;
    }
}

static void update_background(WmAudio *audio, bool paused)
{
    if (!audio->background_started) {
        WmAudioClip *background = load_clip(audio, "background", "audio");
        WmAudioClip *intro = load_clip(audio, "backgroundIntro", "audio");
        pthread_mutex_lock(&audio->mutex);
        if (background) new_voice(audio, background, WM_VOICE_BACKGROUND);
        if (intro) new_voice(audio, intro, WM_VOICE_INTRO);
        audio->background_started = true;
        pthread_mutex_unlock(&audio->mutex);
    }
    audio->background_paused = paused;
    pthread_mutex_lock(&audio->mutex);
    set_paused(audio, WM_VOICE_BACKGROUND, paused || audio->menu_paused);
    set_paused(audio, WM_VOICE_INTRO, paused || audio->menu_paused);
    pthread_mutex_unlock(&audio->mutex);
}

static bool safe_channel_id(const char *id)
{
    if (!safe_name(id)) return false;
    for (const char *digit = id; *digit; digit++) {
        if ((*digit < '0' || *digit > '9') &&
            (*digit < 'a' || *digit > 'f') &&
            (*digit < 'A' || *digit > 'F')) return false;
    }
    return true;
}

static void update_preview(WmAudio *audio, const WmMenu *menu)
{
    bool has_preview = menu->screen == WM_SCREEN_PREVIEW &&
                       menu->selected >= 0 && menu->selected < WM_SLOT_COUNT;
    bool change_in = menu->transition == WM_TRANSITION_PREVIEW &&
                     wm_menu_preview_presentation(menu).phase ==
                         WM_PREVIEW_PHASE_CHANGE_IN;
    bool change_out = menu->transition == WM_TRANSITION_PREVIEW && !change_in;
    if (change_in) return;
    if (change_out || !has_preview ||
        menu->transition == WM_TRANSITION_SELECT) {
        if (audio->active_preview >= 0) {
            pthread_mutex_lock(&audio->mutex);
            fade_voice(voice_by_kind(audio, WM_VOICE_CHANNEL),
                       menu->transition == WM_TRANSITION_BACK ?
                           (size_t)(28 * WM_AUDIO_RATE / 60) : 0);
            pthread_mutex_unlock(&audio->mutex);
            audio->active_preview = -1;
        }
        return;
    }
    if (audio->active_preview == menu->selected ||
        menu->transition != WM_TRANSITION_NONE) return;
    const WmChannel *channel = &menu->slots[menu->selected];
    WmAudioClip *clip = NULL;
    if (strcmp(channel->id, "disc") == 0) {
        clip = load_clip(audio, "discPreview", "audio");
    } else if (safe_channel_id(channel->id)) {
        clip = load_clip(audio, channel->id, "channel-audio");
    }
    audio->active_preview = menu->selected;
    if (!clip) return;
    pthread_mutex_lock(&audio->mutex);
    fade_voice(voice_by_kind(audio, WM_VOICE_CHANNEL), 0);
    new_voice(audio, clip, WM_VOICE_CHANNEL);
    pthread_mutex_unlock(&audio->mutex);
}

void wm_audio_sync(WmAudio *audio, const WmMenu *menu)
{
    if (!audio || !menu || !audio->device) return;
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
    bool background_paused = menu->screen == WM_SCREEN_PREVIEW ||
                             menu->transition == WM_TRANSITION_BACK;
    update_background(audio, background_paused);
    update_preview(audio, menu);
}
