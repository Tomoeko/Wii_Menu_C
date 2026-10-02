#include "app_options.h"

#include <assert.h>
#include <string.h>

int main(void) {
    WmAppOptions options;
    char *defaults[] = {"wii-menu"};
    assert(wm_app_parse_options(1, defaults, &options) == WM_APP_OPTIONS_READY);
    assert(!options.record && !options.record_half && !options.assets &&
           !options.layout_path);

    char *record[] = {"wii-menu", "--record",          "--assets", "prepared",
                      "--bypass", "--preview-channel", "disc"};
    assert(wm_app_parse_options(7, record, &options) == WM_APP_OPTIONS_READY);
    assert(options.record && !options.record_half && options.bypass);
    assert(strcmp(options.assets, "prepared") == 0);
    assert(strcmp(options.preview_channel, "disc") == 0);

    char *layout[] = {"wii-menu",    "--record",   "--layout",
                      "scene.json",  "--raw-root", "raw",
                      "--animation", "intro",      "--hide-masks"};
    assert(wm_app_parse_options(9, layout, &options) == WM_APP_OPTIONS_READY);
    assert(options.record && options.hide_masks);
    assert(strcmp(options.animation, "intro") == 0);

    char *half[] = {"wii-menu", "--record", "half", "--bypass", "--assets", "prepared"};
    assert(wm_app_parse_options(6, half, &options) == WM_APP_OPTIONS_READY);
    assert(options.record && options.record_half && options.bypass);
    assert(strcmp(options.assets, "prepared") == 0);
    char *half_alone[] = {"wii-menu", "half"};
    assert(wm_app_parse_options(2, half_alone, &options) == WM_APP_OPTIONS_INVALID);
    char *wrong_size[] = {"wii-menu", "--record", "quarter"};
    assert(wm_app_parse_options(3, wrong_size, &options) == WM_APP_OPTIONS_INVALID);

    char *value[] = {"wii-menu", "--record", "capture.mp4"};
    assert(wm_app_parse_options(3, value, &options) == WM_APP_OPTIONS_INVALID);
    char *missing[] = {"wii-menu", "--record", "--assets"};
    assert(wm_app_parse_options(3, missing, &options) == WM_APP_OPTIONS_INVALID);
    char *no_root[] = {"wii-menu", "--record", "--layout", "scene.json"};
    assert(wm_app_parse_options(4, no_root, &options) == WM_APP_OPTIONS_INVALID);
    char *conflict[] = {"wii-menu",   "--record", "--layout",          "scene.json",
                        "--raw-root", "raw",      "--preview-channel", "disc"};
    assert(wm_app_parse_options(8, conflict, &options) == WM_APP_OPTIONS_INVALID);
    char *help[] = {"wii-menu", "--record", "--help"};
    assert(wm_app_parse_options(3, help, &options) == WM_APP_OPTIONS_HELP);
    return 0;
}
