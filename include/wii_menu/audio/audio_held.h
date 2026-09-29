#ifndef WII_MENU_AUDIO_HELD_H
#define WII_MENU_AUDIO_HELD_H

#include "wii_menu/resources/resource_audio.h"

#include <stdbool.h>
#include <stdint.h>

enum {
    WM_AUDIO_HELD_RATE = 32000,
    WM_AUDIO_HELD_BLOCK = 96,
    WM_AUDIO_HELD_DECIBELS = 965,
    WM_AUDIO_HELD_PAN = 257
};

/* These lookup values are extracted locally from the supplied menu driver. */
typedef struct WmAudioHeldTables {
    float decibels[WM_AUDIO_HELD_DECIBELS];
    float pan[WM_AUDIO_HELD_PAN];
} WmAudioHeldTables;

typedef struct WmAudioHeldProfile {
    float attack_multiplier;
    float decay_rate;
    float sustain_level;
    float release_rate;
    float volume; /* Instrument and note gain; archive gain belongs to caller. */
    float pan;
} WmAudioHeldProfile;

typedef struct WmAudioHeldState {
    double position;
    float envelope_level;
    float previous_gain;
    uint8_t stage;
    bool active;
    bool has_previous_gain;
} WmAudioHeldState;

void wm_audio_held_start(WmAudioHeldState *state);
void wm_audio_held_release(WmAudioHeldState *state);
bool wm_audio_held_tables_valid(const WmAudioHeldTables *tables);

/* Render one native 3 ms block without allocation. PCM must be a looping mono
 * 32 kHz wave. Controls apply before the integer envelope and pan stages.
 * Linear source interpolation remains an approximation to the AX four-tap SRC.
 * Returns false for invalid inputs; inactive states produce a silent block. */
bool wm_audio_held_render(WmAudioHeldState *state, const WmAudioHeldProfile *profile,
                          const WmAudioHeldTables *tables, const WmAudioPcm *pcm,
                          float gain, float pan, float pitch,
                          float stereo[WM_AUDIO_HELD_BLOCK * 2]);

#endif
