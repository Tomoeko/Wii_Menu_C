#ifndef WII_MENU_AUDIO_H
#define WII_MENU_AUDIO_H

#include "wii_menu/menu.h"

#include <stdbool.h>

typedef struct WmAudio WmAudio;

/* The prepared audio directory is optional. Missing cues remain silent and
 * are reported once to stderr; this never substitutes an unrelated sound. */
WmAudio *wm_audio_create(const char *assets_directory);
void wm_audio_destroy(WmAudio *audio);

/* Event symbols follow the HTML controller's local audio catalog. */
bool wm_audio_play(WmAudio *audio, const char *name);
bool wm_audio_start_loop(WmAudio *audio, const char *name);
void wm_audio_stop_loop(WmAudio *audio, const char *name);
void wm_audio_set_loop(WmAudio *audio, const char *name,
                       float gain, float pan, float pitch);
void wm_audio_set_volume(WmAudio *audio, float volume);
void wm_audio_set_muted(WmAudio *audio, bool muted);
/* Retire all active voices when HOME requests a fresh Menu restart. Cached
 * decoded resources and the user's volume setting remain available. */
void wm_audio_reset_all(WmAudio *audio);

/* Call once per frame after menu input and transition advancement. It owns
 * background, channel-preview, and HOME pause lifetimes. */
void wm_audio_sync(WmAudio *audio, const WmMenu *menu);

#endif
