#include "wii_menu/home_overlay.h"

#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <string.h>

typedef struct CueLog {
    char symbols[32][64];
    size_t count;
    size_t remote_changes;
} CueLog;

static void record_cue(void *context, const char *symbol)
{
    CueLog *log = context;
    assert(log->count < 32);
    snprintf(log->symbols[log->count++], sizeof(log->symbols[0]), "%s",
             symbol);
}

static void record_remote(void *context, const WmHomeRemoteState *state)
{
    CueLog *log = context;
    assert(state->volume >= 0.0f && state->volume <= 1.0f);
    log->remote_changes++;
}

/* This test traverses source curves but never submits GPU draws. */
void wm_platform_begin(WmPlatform *platform, WmColor clear_color)
{
    (void)platform;
    (void)clear_color;
    assert(false);
}

void wm_platform_end(WmPlatform *platform)
{
    (void)platform;
    assert(false);
}

void wm_platform_set_clip(WmPlatform *platform, const WmClipRect *rect)
{
    (void)platform;
    (void)rect;
    assert(false);
}

void wm_platform_prepare_material(WmPlatform *platform,
                                  const WmMaterialQuad *quad)
{
    (void)platform;
    (void)quad;
}

void wm_platform_draw_quad(WmPlatform *platform, const WmQuad *quad)
{
    (void)platform;
    (void)quad;
    assert(false);
}

void wm_platform_draw_vertices(WmPlatform *platform,
                               const WmDrawVertex vertices[4],
                               uint32_t texture)
{
    (void)platform;
    (void)vertices;
    (void)texture;
    assert(false);
}

void wm_platform_draw_material_quad(WmPlatform *platform,
                                    const WmMaterialQuad *quad)
{
    (void)platform;
    (void)quad;
    assert(false);
}

uint32_t wm_platform_create_texture(WmPlatform *platform, int width,
                                    int height, const uint8_t *rgba)
{
    (void)platform;
    (void)width;
    (void)height;
    (void)rgba;
    assert(false);
    return 0;
}

void wm_platform_destroy_texture(WmPlatform *platform, uint32_t texture)
{
    (void)platform;
    (void)texture;
    assert(false);
}

static bool ends_with(const CueLog *log, const char *symbol)
{
    return log->count && strcmp(log->symbols[log->count - 1], symbol) == 0;
}

static bool control_has_hit_area(WmHomeOverlay *home, WmHomeControl wanted)
{
    for (int y = 0; y < WM_FRAME_HEIGHT; y += 8) {
        for (int x = 0; x < WM_FRAME_WIDTH; x += 8) {
            if (wm_home_overlay_hit(home, x, y) == wanted) return true;
        }
    }
    return false;
}

static void test_close(WmHomeOverlay *home, CueLog *log)
{
    assert(wm_home_overlay_open(home));
    assert(!wm_home_overlay_ready(home));
    assert(wm_home_overlay_advance(home, 20.0f) == 0.0f);
    assert(log->count == 0);
    wm_home_overlay_advance(home, 1.0f);
    assert(wm_home_overlay_ready(home));
    assert(ends_with(log, "HOMESE_HOME_BUTTON"));
    assert(control_has_hit_area(home, WM_HOME_CONTROL_CLOSE));
    assert(control_has_hit_area(home, WM_HOME_CONTROL_RETURN));
    assert(control_has_hit_area(home, WM_HOME_CONTROL_OPTIONS));
    wm_home_overlay_hover(home, WM_HOME_CONTROL_CLOSE);
    assert(ends_with(log, "HOMESE_FOCUS"));
    assert(wm_home_overlay_back(home));
    assert(wm_home_overlay_phase(home) == WM_HOME_LEAVE);
    assert(ends_with(log, "HOMESE_RETURN_APP"));
    wm_home_overlay_advance(home, 38.0f);
    assert(wm_home_overlay_take_outcome(home) == WM_HOME_OUTCOME_NONE);
    assert(fabsf(wm_home_overlay_advance(home, 1.5f) - 0.5f) < 0.0001f);
    assert(wm_home_overlay_take_outcome(home) == WM_HOME_OUTCOME_CLOSED);
    assert(wm_home_overlay_take_outcome(home) == WM_HOME_OUTCOME_NONE);
}

static void test_options_and_return(WmHomeOverlay *home, CueLog *log)
{
    assert(wm_home_overlay_open(home));
    wm_home_overlay_advance(home, 21.0f);
    assert(wm_home_overlay_activate(home, WM_HOME_CONTROL_OPTIONS));
    assert(ends_with(log, "HOMESE_SELECT"));
    assert(!wm_home_overlay_ready(home));
    wm_home_overlay_advance(home, 15.0f);
    assert(ends_with(log, "HOMESE_SELECT"));
    wm_home_overlay_advance(home, 1.0f);
    assert(ends_with(log, "HOMESE_OPEN_CONTROLLER"));
    wm_home_overlay_advance(home, 25.0f);
    assert(wm_home_overlay_ready(home));
    assert(control_has_hit_area(home, WM_HOME_CONTROL_VOLUME_DOWN));
    assert(control_has_hit_area(home, WM_HOME_CONTROL_VOLUME_UP));
    assert(control_has_hit_area(home, WM_HOME_CONTROL_RUMBLE_ON));
    assert(control_has_hit_area(home, WM_HOME_CONTROL_RUMBLE_OFF));
    assert(control_has_hit_area(home, WM_HOME_CONTROL_RECONNECT));
    assert(wm_home_overlay_activate(home, WM_HOME_CONTROL_VOLUME_UP));
    assert(ends_with(log, "HOME_SPEAKER_VOLUME"));
    assert(fabsf(wm_home_overlay_remote_state(home).volume - 0.8f) < 0.0001f);
    assert(log->remote_changes == 1);
    assert(wm_home_overlay_back(home));
    assert(ends_with(log, "HOMESE_CLOSE_CONTROLLER"));
    assert(!wm_home_overlay_ready(home));
    wm_home_overlay_advance(home, 40.0f);
    assert(wm_home_overlay_ready(home));

    assert(wm_home_overlay_activate(home, WM_HOME_CONTROL_RETURN));
    assert(!wm_home_overlay_activate(home, WM_HOME_CONTROL_YES));
    wm_home_overlay_advance(home, 40.0f);
    assert(wm_home_overlay_activate(home, WM_HOME_CONTROL_NO));
    assert(ends_with(log, "HOMESE_CANCEL"));
    wm_home_overlay_advance(home, 39.0f);
    assert(wm_home_overlay_ready(home));
    assert(wm_home_overlay_activate(home, WM_HOME_CONTROL_RETURN));
    wm_home_overlay_advance(home, 40.0f);
    assert(wm_home_overlay_activate(home, WM_HOME_CONTROL_YES));
    assert(ends_with(log, "HOMESE_GOTO_MENU"));
    wm_home_overlay_advance(home, 20.0f);
    assert(wm_home_overlay_phase(home) == WM_HOME_RETURN_FADE);
    wm_home_overlay_advance(home, 15.0f);
    assert(fabsf(wm_home_overlay_fade_alpha(home) - 127.0f / 255.0f) <
           0.0001f);
    wm_home_overlay_advance(home, 15.0f);
    assert(wm_home_overlay_take_outcome(home) ==
           WM_HOME_OUTCOME_RETURN_MENU);
}

static void test_reconnect(WmHomeOverlay *home, CueLog *log)
{
    WmHomeReconnectFixture fixture = {
        .mode = WM_HOME_RECONNECT_MANUAL,
        .players = {1, 2},
        .player_count = 2,
        .delay_frames = 180.0f,
        .interval_frames = 24.0f
    };
    assert(wm_home_overlay_set_reconnect_fixture(home, &fixture));
    assert(wm_home_overlay_open(home));
    wm_home_overlay_advance(home, 21.0f);
    assert(wm_home_overlay_activate(home, WM_HOME_CONTROL_OPTIONS));
    wm_home_overlay_advance(home, 41.0f);
    assert(wm_home_overlay_activate(home, WM_HOME_CONTROL_RECONNECT));
    assert(ends_with(log, "HOMESE_START_CONNECT_WINDOW"));
    wm_home_overlay_advance(home, 15.0f + 119.0f);
    assert(wm_home_overlay_connect(home, 1));
    assert(!wm_home_overlay_connect(home, 1));
    assert(wm_home_overlay_reconnect_key(home, '1', true));
    assert(wm_home_overlay_reconnect_key(home, '2', true));
    assert(wm_home_overlay_reconnect_key(home, '2', true));
    assert(ends_with(log, "HOMESE_CONNECTED2"));
    wm_home_overlay_advance(home, 24.0f);
    assert(ends_with(log, "HOME_SPEAKER_CONNECT2"));
    wm_home_overlay_advance(home, 6.0f + 19.0f);
    assert(ends_with(log, "HOMESE_END_CONNECT_WINDOW"));
    assert(wm_home_overlay_ready(home));
    WmHomeRemoteState remote = wm_home_overlay_remote_state(home);
    assert(remote.controllers[0].connected);
    assert(remote.controllers[1].connected);
    wm_home_overlay_reset(home);
}

static void test_automatic_reconnect(WmHomeOverlay *home, CueLog *log)
{
    WmHomeReconnectFixture fixture = {
        .mode = WM_HOME_RECONNECT_AUTOMATIC,
        .players = {3, 1, 4, 2},
        .player_count = 4,
        .delay_frames = 1.0f,
        .interval_frames = 0.0f
    };
    assert(wm_home_overlay_set_reconnect_fixture(home, &fixture));
    assert(wm_home_overlay_open(home));
    wm_home_overlay_advance(home, 21.0f);
    assert(wm_home_overlay_activate(home, WM_HOME_CONTROL_OPTIONS));
    wm_home_overlay_advance(home, 41.0f);
    assert(wm_home_overlay_activate(home, WM_HOME_CONTROL_RECONNECT));
    wm_home_overlay_advance(home, 15.0f + 119.0f + 1.0f);
    assert(ends_with(log, "HOMESE_CONNECTED2"));
    size_t before_speakers = log->count;
    wm_home_overlay_advance(home, 24.0f);
    assert(log->count == before_speakers + 4);
    assert(strcmp(log->symbols[before_speakers],
                  "HOME_SPEAKER_CONNECT3") == 0);
    assert(strcmp(log->symbols[before_speakers + 1],
                  "HOME_SPEAKER_CONNECT1") == 0);
    assert(strcmp(log->symbols[before_speakers + 2],
                  "HOME_SPEAKER_CONNECT4") == 0);
    assert(strcmp(log->symbols[before_speakers + 3],
                  "HOME_SPEAKER_CONNECT2") == 0);
    wm_home_overlay_advance(home, 6.0f + 19.0f);
    assert(wm_home_overlay_ready(home));
    WmHomeRemoteState remote = wm_home_overlay_remote_state(home);
    for (size_t index = 0; index < 4; index++) {
        assert(remote.controllers[index].connected);
    }
    wm_home_overlay_reset(home);
}

static void test_reconnect_timeout(WmHomeOverlay *home, CueLog *log)
{
    WmHomeReconnectFixture fixture = {
        .mode = WM_HOME_RECONNECT_TIMEOUT,
        .players = {1},
        .player_count = 1,
        .delay_frames = 180.0f,
        .interval_frames = 24.0f,
        .start_failures = 1,
        .stop_failures = 1
    };
    assert(wm_home_overlay_set_reconnect_fixture(home, &fixture));
    assert(wm_home_overlay_open(home));
    wm_home_overlay_advance(home, 21.0f);
    assert(wm_home_overlay_activate(home, WM_HOME_CONTROL_OPTIONS));
    wm_home_overlay_advance(home, 41.0f);
    assert(wm_home_overlay_activate(home, WM_HOME_CONTROL_RECONNECT));
    wm_home_overlay_advance(home, 15.0f + 119.0f + 6.0f + 3601.0f +
                                  6.0f + 19.0f);
    assert(wm_home_overlay_ready(home));
    assert(ends_with(log, "HOMESE_END_CONNECT_WINDOW"));
    WmHomeRemoteState remote = wm_home_overlay_remote_state(home);
    for (size_t index = 0; index < 4; index++) {
        assert(!remote.controllers[index].connected);
    }
    wm_home_overlay_reset(home);
}

int main(int argc, char **argv)
{
    const char *assets = argc > 1 ? argv[1] : ".local/native-assets";
    char source[4096];
    int length = snprintf(source, sizeof(source),
                          "%s/layouts/homeBtn1/th_HomeBtn_d.json", assets);
    assert(length > 0 && length < (int)sizeof(source));
    FILE *check = fopen(source, "rb");
    if (!check) {
        puts("HOME overlay resource test skipped: local WAD export absent.");
        return 0;
    }
    fclose(check);
    CueLog log = {0};
    WmHomeOverlay *home = wm_home_overlay_create(
        (WmPlatform *)1, assets, (WmTextureCache *)1,
        (WmFontCache *)1, record_cue, record_remote, &log);
    assert(home);
    test_close(home, &log);
    memset(&log, 0, sizeof(log));
    test_options_and_return(home, &log);
    wm_home_overlay_destroy(home);

    memset(&log, 0, sizeof(log));
    home = wm_home_overlay_create(
        (WmPlatform *)1, assets, (WmTextureCache *)1,
        (WmFontCache *)1, record_cue, record_remote, &log);
    assert(home);
    test_reconnect(home, &log);
    wm_home_overlay_destroy(home);

    memset(&log, 0, sizeof(log));
    home = wm_home_overlay_create(
        (WmPlatform *)1, assets, (WmTextureCache *)1,
        (WmFontCache *)1, record_cue, record_remote, &log);
    assert(home);
    test_automatic_reconnect(home, &log);
    wm_home_overlay_destroy(home);

    memset(&log, 0, sizeof(log));
    home = wm_home_overlay_create(
        (WmPlatform *)1, assets, (WmTextureCache *)1,
        (WmFontCache *)1, record_cue, record_remote, &log);
    assert(home);
    test_reconnect_timeout(home, &log);
    wm_home_overlay_destroy(home);
    puts("HOME overlay transitions and cue tests passed.");
    return 0;
}
