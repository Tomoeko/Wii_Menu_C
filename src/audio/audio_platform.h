#ifndef WII_MENU_AUDIO_PLATFORM_H
#define WII_MENU_AUDIO_PLATFORM_H

#include <stdbool.h>
#include <stddef.h>

typedef struct WmAudioDevice WmAudioDevice;
typedef void (*WmAudioRender)(void *context, float *interleaved, size_t frames);

/* Fixed 48 kHz, stereo, interleaved float output. The renderer must perform
 * no allocation, file access, or blocking work on the audio callback. */
WmAudioDevice *wm_audio_device_open(WmAudioRender render, void *context);
bool wm_audio_device_start(WmAudioDevice *device);
bool wm_audio_device_stop(WmAudioDevice *device);
void wm_audio_device_close(WmAudioDevice *device);

#endif
