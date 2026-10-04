#include "app_options.h"

#include <string.h>

WmAppOptionsResult wm_app_parse_options(int argc, char **argv, WmAppOptions *options) {
    if (argc < 1 || !argv || !options)
        return WM_APP_OPTIONS_INVALID;
    for (int index = 0; index < argc; index++) {
        if (!argv[index])
            return WM_APP_OPTIONS_INVALID;
    }
    WmAppOptions parsed = {.antialiasing = true};
    bool has_audio_mode = false;
    for (int index = 1; index < argc; index++) {
        if (strcmp(argv[index], "--help") == 0 || strcmp(argv[index], "-h") == 0)
            return WM_APP_OPTIONS_HELP;
        if (strcmp(argv[index], "--assets") == 0 && index + 1 < argc) {
            parsed.assets = argv[++index];
        } else if (strcmp(argv[index], "--layout") == 0 && index + 1 < argc) {
            parsed.layout_path = argv[++index];
        } else if (strcmp(argv[index], "--raw-root") == 0 && index + 1 < argc) {
            parsed.raw_root = argv[++index];
        } else if (strcmp(argv[index], "--animation") == 0 && index + 1 < argc) {
            parsed.animation = argv[++index];
        } else if (strcmp(argv[index], "--preview-channel") == 0 && index + 1 < argc) {
            parsed.preview_channel = argv[++index];
        } else if (strcmp(argv[index], "--hide-masks") == 0) {
            parsed.hide_masks = true;
        } else if (strcmp(argv[index], "--record") == 0) {
            parsed.record = true;
            if (index + 1 < argc && strcmp(argv[index + 1], "half") == 0) {
                parsed.record_half = true;
                index++;
            }
        } else if (strcmp(argv[index], "--audio") == 0 && index + 1 < argc) {
            const char *mode = argv[++index];
            if (strcmp(mode, "web") == 0)
                parsed.audio_mode = CC_CAPTURE_AUDIO_WEB;
            else
                return WM_APP_OPTIONS_INVALID;
            has_audio_mode = true;
        } else if (strcmp(argv[index], "--aa") == 0 ||
                   strcmp(argv[index], "--no-aa") == 0) {
            parsed.antialiasing = strcmp(argv[index], "--aa") == 0;
            parsed.antialiasing_override = true;
        } else if (strcmp(argv[index], "--bypass") == 0) {
            parsed.bypass = true;
        } else {
            return WM_APP_OPTIONS_INVALID;
        }
    }
    if ((parsed.layout_path && !parsed.raw_root) ||
        ((parsed.animation || parsed.hide_masks) && !parsed.layout_path) ||
        (parsed.preview_channel && parsed.layout_path) ||
        (has_audio_mode && !parsed.record))
        return WM_APP_OPTIONS_INVALID;
    *options = parsed;
    return WM_APP_OPTIONS_READY;
}
