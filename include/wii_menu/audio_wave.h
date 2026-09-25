#ifndef WII_MENU_AUDIO_WAVE_H
#define WII_MENU_AUDIO_WAVE_H

#include "wii_menu/resource_audio.h"

#include <stdbool.h>
#include <stddef.h>

/* First-party RIFF PCM16 bridge for local export and playback. */
bool wm_audio_wav_write(const char *path, const WmAudioPcm *audio,
                        char *error, size_t error_capacity);
bool wm_audio_wav_read(const char *path, WmAudioPcm *audio,
                       char *error, size_t error_capacity);

#endif
