#ifndef WM_APP_OPTIONS_H
#define WM_APP_OPTIONS_H

#include <stdbool.h>
#include "console_common/capture/capture_writer.h"

typedef struct {
    const char *assets;
    const char *layout_path;
    const char *raw_root;
    const char *animation;
    const char *preview_channel;
    bool hide_masks;
    bool bypass;
    bool record;
    bool record_half;
    bool antialiasing;
    CcCaptureAudioMode audio_mode;
} WmAppOptions;

typedef enum {
    WM_APP_OPTIONS_READY,
    WM_APP_OPTIONS_HELP,
    WM_APP_OPTIONS_INVALID
} WmAppOptionsResult;

WmAppOptionsResult wm_app_parse_options(int argc, char **argv, WmAppOptions *options);

#endif
