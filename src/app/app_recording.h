#ifndef WM_APP_RECORDING_H
#define WM_APP_RECORDING_H

#include "wii_menu/audio/audio.h"
#include "wii_menu/platform/platform.h"
#include "console_common/capture/recording.h"

/* The app keeps audio/platform alive through close. Open stops device output;
 * start follows the first captured frame, and close stops before draining. */
CcRecording *wm_app_recording_open(WmPlatform *platform, WmAudio *audio, bool half_size,
                                   CcCaptureAudioMode audio_mode);
bool wm_app_recording_start(CcRecording *recording, WmAudio *audio, double now);
bool wm_app_recording_close(CcRecording *recording, WmAudio *audio, double now);

#endif
