#ifndef WM_APP_CORRUPTION_SCREEN_H
#define WM_APP_CORRUPTION_SCREEN_H

#include <signal.h>
#include <stdbool.h>
#include "console_common/capture/capture_writer.h"

/* Keep the diagnostic visible until the window is closed. Returns failure. */
int wm_app_show_corruption_screen(const char *assets_root, bool record, bool half_size,
                                  CcCaptureAudioMode audio_mode, bool antialiasing,
                                  const volatile sig_atomic_t *exit_requested);

#endif
