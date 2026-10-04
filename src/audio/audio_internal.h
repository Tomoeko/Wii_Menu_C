#ifndef WM_AUDIO_INTERNAL_H
#define WM_AUDIO_INTERNAL_H

#include "wii_menu/audio/audio.h"
#include "wii_menu/audio/audio_held.h"
#include "wii_menu/audio/audio_wave.h"
#include "wii_menu/support/json.h"

#include "audio_platform.h"
#include "console_common/capture/audio_buffer.h"
#include "console_common/audio/resampler.h"

#include "console_common/support/thread.h"
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

/* The UI owns requested controls under mutex. Playback cursors, envelopes,
 * and the last applied controls belong exclusively to the audio callback. */
typedef struct WmAudioVoiceControl {
    size_t clip_index;
    WmVoiceKind kind;
    uint64_t generation;
    double step;
    float gain;
    float pan_left;
    float pan_right;
    float pan;
    float pitch;
    size_t fade_frames;
    bool active;
    bool paused;
    bool releasing;
} WmAudioVoiceControl;

typedef struct WmAudioVoice {
    WmAudioVoiceControl applied;
    size_t clip_index;
    double frame;
    double step;
    float gain;
    float pan_left;
    float pan_right;
    float pan;
    float pitch;
    WmAudioHeldState held_state;
    float held_block[WM_AUDIO_HELD_BLOCK * 2];
    size_t held_cursor;
    CcAudioResampleState *resample_state;
    float fade_step;
    size_t fade_frames;
    bool active;
    bool paused;
} WmAudioVoice;

struct WmAudio {
    char assets[4096];
    WmAudioClip clips[WM_AUDIO_MAX_CLIPS];
    size_t clip_count;
    WmAudioVoiceControl controls[WM_AUDIO_MAX_VOICES];
    WmAudioVoice voices[WM_AUDIO_MAX_VOICES];
    CcMutex mutex;
    WmAudioDevice *device;
    CcAudioBuffer *capture;
    bool output_running;
    WmJson direct_manifest;
    WmJson sequence_manifest;
    WmAudioHeldTables held_tables;
    WmAudioHeldProfile held_profiles[2];
    CcAudioResampler *held_resampler;
    bool has_held_profiles;
    bool warned_held_profiles;
    uint64_t last_hover_ns;
    bool background_started;
    bool menu_paused;
    bool background_paused;
    float master_volume;
    bool muted;
    float mixer_volume;
    bool mixer_muted;
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
/* Neither operation waits for the UI thread. A busy control lock delays an
 * update until a later callback while current playback continues. */
void wm_audio_refresh_controls(WmAudio *audio);
void wm_audio_retire_finished(WmAudio *audio);
bool wm_audio_resampling_init(WmAudio *audio);
void wm_audio_resampling_destroy(WmAudio *audio);

#endif
