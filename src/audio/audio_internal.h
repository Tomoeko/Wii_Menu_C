#ifndef WM_AUDIO_INTERNAL_H
#define WM_AUDIO_INTERNAL_H

#include "wii_menu/audio/audio.h"
#include "wii_menu/audio/audio_held.h"
#include "wii_menu/audio/audio_wave.h"
#include "wii_menu/support/json.h"

#include "audio_platform.h"

#include <pthread.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

enum { WM_AUDIO_RATE = 48000, WM_AUDIO_MAX_CLIPS = 160, WM_AUDIO_MAX_VOICES = 32 };

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
    const WmAudioHeldProfile *held_profile;
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
    float pan;
    float pitch;
    WmAudioHeldState held_state;
    float held_block[WM_AUDIO_HELD_BLOCK * 2];
    float held_current[2];
    float held_next[2];
    size_t held_cursor;
    unsigned held_phase;
    bool held_primed;
    bool held_has_next;
    bool releasing;
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
    WmAudioHeldTables held_tables;
    WmAudioHeldProfile held_profiles[2];
    bool has_held_profiles;
    bool warned_held_profiles;
    uint64_t last_hover_ns;
    bool background_started;
    bool menu_paused;
    bool background_paused;
    float master_volume;
    bool muted;
    int active_preview;
};

/* Main-thread asset loading and the device-thread renderer share fixed storage.
 * A loaded clip's address remains stable while an active voice references it. */
bool wm_audio_safe_name(const char *name);
size_t wm_audio_manifest_entry(const WmAudio *audio, const char *name,
                               const WmJson **owner);
void wm_audio_load_held_profiles(WmAudio *audio);
WmAudioClip *wm_audio_load_clip(WmAudio *audio, const char *name,
                                const char *directory);
void wm_audio_mix(void *context, float *interleaved, size_t frames);

#endif
