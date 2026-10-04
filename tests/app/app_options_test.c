#include "app_options.h"

#include <assert.h>
#include <string.h>

int main(void) {
    WmAppOptions options;
    char *defaults[] = {"wii-menu"};
    assert(wm_app_parse_options(1, defaults, &options) == WM_APP_OPTIONS_READY);
    assert(!options.record && !options.record_half && !options.assets &&
           !options.layout_path && options.antialiasing &&
           !options.antialiasing_override &&
           options.audio_mode == CC_CAPTURE_AUDIO_NORMAL);

    char *record[] = {"wii-menu", "--record",          "--assets", "prepared",
                      "--bypass", "--preview-channel", "disc"};
    assert(wm_app_parse_options(7, record, &options) == WM_APP_OPTIONS_READY);
    assert(options.record && !options.record_half && options.bypass);
    assert(strcmp(options.assets, "prepared") == 0);
    assert(strcmp(options.preview_channel, "disc") == 0);
    char *normal[] = {"wii-menu", "--record", "--audio", "normal"};
    assert(wm_app_parse_options(4, normal, &options) == WM_APP_OPTIONS_INVALID);
    char *web[] = {"wii-menu", "--aa", "--audio", "web", "--record", "half"};
    assert(wm_app_parse_options(6, web, &options) == WM_APP_OPTIONS_READY);
    assert(options.antialiasing && options.record && options.record_half &&
           options.audio_mode == CC_CAPTURE_AUDIO_WEB);
    char *web_after[] = {"wii-menu", "--record", "--audio", "web"};
    assert(wm_app_parse_options(4, web_after, &options) == WM_APP_OPTIONS_READY);
    assert(options.record && !options.record_half &&
           options.audio_mode == CC_CAPTURE_AUDIO_WEB);
    WmAppOptions previous = options;
    char *wrong_audio[] = {"wii-menu", "--aa", "--record", "--audio", "lossless"};
    assert(wm_app_parse_options(5, wrong_audio, &options) == WM_APP_OPTIONS_INVALID);
    assert(memcmp(&options, &previous, sizeof(options)) == 0);
    char *no_record[] = {"wii-menu", "--audio", "web"};
    assert(wm_app_parse_options(3, no_record, &options) == WM_APP_OPTIONS_INVALID);
    assert(memcmp(&options, &previous, sizeof(options)) == 0);
    char *normal_no_record[] = {"wii-menu", "--audio", "normal"};
    assert(wm_app_parse_options(3, normal_no_record, &options) ==
           WM_APP_OPTIONS_INVALID);
    char *missing_audio[] = {"wii-menu", "--record", "--audio"};
    assert(wm_app_parse_options(3, missing_audio, &options) == WM_APP_OPTIONS_INVALID);
    char *audio_flag[] = {"wii-menu", "--audio", "--record"};
    assert(wm_app_parse_options(3, audio_flag, &options) == WM_APP_OPTIONS_INVALID);
    char *aa[] = {"wii-menu", "--aa"};
    assert(wm_app_parse_options(2, aa, &options) == WM_APP_OPTIONS_READY);
    assert(options.antialiasing && options.antialiasing_override && !options.record);
    char *disabled[] = {"wii-menu", "--no-aa"};
    assert(wm_app_parse_options(2, disabled, &options) == WM_APP_OPTIONS_READY);
    assert(!options.antialiasing && options.antialiasing_override);

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
    assert(wm_app_parse_options(0, defaults, &options) == WM_APP_OPTIONS_INVALID);
    assert(wm_app_parse_options(1, NULL, &options) == WM_APP_OPTIONS_INVALID);
    assert(wm_app_parse_options(1, defaults, NULL) == WM_APP_OPTIONS_INVALID);
    return 0;
}
