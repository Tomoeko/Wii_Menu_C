#include "app_options.h"

#include <string.h>

WmAppOptionsResult wm_app_parse_options(int argc, char **argv, WmAppOptions *options) {
    *options = (WmAppOptions){0};
    for (int index = 1; index < argc; index++) {
        if (strcmp(argv[index], "--help") == 0 || strcmp(argv[index], "-h") == 0)
            return WM_APP_OPTIONS_HELP;
        if (strcmp(argv[index], "--assets") == 0 && index + 1 < argc) {
            options->assets = argv[++index];
        } else if (strcmp(argv[index], "--layout") == 0 && index + 1 < argc) {
            options->layout_path = argv[++index];
        } else if (strcmp(argv[index], "--raw-root") == 0 && index + 1 < argc) {
            options->raw_root = argv[++index];
        } else if (strcmp(argv[index], "--animation") == 0 && index + 1 < argc) {
            options->animation = argv[++index];
        } else if (strcmp(argv[index], "--preview-channel") == 0 && index + 1 < argc) {
            options->preview_channel = argv[++index];
        } else if (strcmp(argv[index], "--hide-masks") == 0) {
            options->hide_masks = true;
        } else if (strcmp(argv[index], "--record") == 0) {
            options->record = true;
            if (index + 1 < argc && strcmp(argv[index + 1], "half") == 0) {
                options->record_half = true;
                index++;
            }
        } else if (strcmp(argv[index], "--bypass") == 0) {
            options->bypass = true;
        } else {
            return WM_APP_OPTIONS_INVALID;
        }
    }
    if ((options->layout_path && !options->raw_root) ||
        ((options->animation || options->hide_masks) && !options->layout_path) ||
        (options->preview_channel && options->layout_path))
        return WM_APP_OPTIONS_INVALID;
    return WM_APP_OPTIONS_READY;
}
