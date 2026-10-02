#ifndef WII_MENU_AUDIO_H
#define WII_MENU_AUDIO_H

#include "wii_menu/menu/menu.h"

#include <stdbool.h>
#include <stddef.h>

typedef struct WmAudio WmAudio;

/* The prepared audio directory is optional. Missing cues remain silent and
 * are reported once to stderr; this never substitutes an unrelated sound. */
WmAudio *wm_audio_create(const char *assets_directory);
void wm_audio_destroy(WmAudio *audio);

/* Stop joins the output callback without retiring queued voices. A failed
 * device is released so recording storage can still be detached safely. */
bool wm_audio_output_stop(WmAudio *audio);
bool wm_audio_output_start(WmAudio *audio);
bool wm_audio_output_available(const WmAudio *audio);

/* Begin/end require stopped output. Read is the single consumer of exact
 * post-volume, post-clipping stereo float words sent to the device. */
bool wm_audio_capture_begin(WmAudio *audio, size_t capacity_frames);
size_t wm_audio_capture_read(WmAudio *audio, float *stereo, size_t capacity_frames);
bool wm_audio_capture_failed(const WmAudio *audio);
void wm_audio_capture_end(WmAudio *audio);

/* Event symbols resolve through the prepared local audio catalog. */
bool wm_audio_play(WmAudio *audio, const char *name);
/* Give one new effect voice its own stereo position: -1 left, 0 center,
 * 1 right. Existing voices of the same cue keep their positions. */
bool wm_audio_play_panned(WmAudio *audio, const char *name, float pan);
bool wm_audio_start_loop(WmAudio *audio, const char *name);
/* Start or refresh one held loop with its controls in the same operation.
 * This avoids a callback playing a new drag voice at full gain first. */
bool wm_audio_hold_loop(WmAudio *audio, const char *name, float gain, float pan,
                        float pitch);
void wm_audio_stop_loop(WmAudio *audio, const char *name);
void wm_audio_set_loop(WmAudio *audio, const char *name, float gain, float pan,
                       float pitch);
void wm_audio_set_volume(WmAudio *audio, float volume);
void wm_audio_set_muted(WmAudio *audio, bool muted);
/* Retire all active voices when HOME requests a fresh Menu restart. Cached
 * decoded resources and the user's volume setting remain available. */
void wm_audio_reset_all(WmAudio *audio);

/* Call once per frame after menu input and transition advancement. It owns
 * background, channel-preview, and HOME pause lifetimes. */
void wm_audio_sync(WmAudio *audio, const WmMenu *menu);

#endif
