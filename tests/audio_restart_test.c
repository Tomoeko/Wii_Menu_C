#define _POSIX_C_SOURCE 200809L

#include "wii_menu/audio.h"
#include "wii_menu/menu_restart.h"

#include "audio_platform.h"

#include <assert.h>
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

struct WmAudioDevice {
    WmAudioRender render;
    void *context;
};

static WmAudioDevice *device;

/* This test exercises the restart clock without creating a rendering scene.
 * An accidental GPU operation must fail rather than silently pass. */
void wm_platform_prepare_material(WmPlatform *platform,
                                  const WmMaterialQuad *quad) {
    (void)platform;
    (void)quad;
    assert(false);
}

void wm_platform_draw_material_quad(WmPlatform *platform,
                                    const WmMaterialQuad *quad) {
    (void)platform;
    (void)quad;
    assert(false);
}

void wm_platform_draw_vertices(WmPlatform *platform,
                               const WmDrawVertex vertices[4],
                               uint32_t texture) {
    (void)platform;
    (void)vertices;
    (void)texture;
    assert(false);
}

uint32_t wm_platform_create_texture(WmPlatform *platform, int width,
                                    int height, const uint8_t *rgba) {
    (void)platform;
    (void)width;
    (void)height;
    (void)rgba;
    assert(false);
    return 0;
}

void wm_platform_destroy_texture(WmPlatform *platform, uint32_t texture) {
    (void)platform;
    (void)texture;
    assert(false);
}

WmAudioDevice *wm_audio_device_open(WmAudioRender render, void *context) {
    WmAudioDevice *opened = calloc(1, sizeof(*opened));
    assert(opened);
    opened->render = render;
    opened->context = context;
    device = opened;
    return opened;
}

void wm_audio_device_close(WmAudioDevice *opened) {
    assert(opened == device);
    device = NULL;
    free(opened);
}

static void write_u16(FILE *file, uint16_t value) {
    assert(fputc(value & 0xff, file) != EOF);
    assert(fputc(value >> 8, file) != EOF);
}

static void write_u32(FILE *file, uint32_t value) {
    write_u16(file, (uint16_t)value);
    write_u16(file, (uint16_t)(value >> 16));
}

static void write_tone(const char *path, int16_t sample) {
    enum { SAMPLE_FRAMES = 480 };
    FILE *file = fopen(path, "wb");
    assert(file);
    assert(fwrite("RIFF", 1, 4, file) == 4);
    write_u32(file, 36 + SAMPLE_FRAMES * 4);
    assert(fwrite("WAVEfmt ", 1, 8, file) == 8);
    write_u32(file, 16);
    write_u16(file, 1);
    write_u16(file, 2);
    write_u32(file, 48000);
    write_u32(file, 48000 * 4);
    write_u16(file, 4);
    write_u16(file, 16);
    assert(fwrite("data", 1, 4, file) == 4);
    write_u32(file, SAMPLE_FRAMES * 4);
    for (int index = 0; index < SAMPLE_FRAMES; index++) {
        write_u16(file, (uint16_t)sample);
        write_u16(file, (uint16_t)sample);
    }
    assert(fclose(file) == 0);
}

static float render_first_sample(void) {
    float samples[32] = {0};
    assert(device);
    device->render(device->context, samples, 16);
    return samples[0];
}

int main(void) {
    char directory[] = "/tmp/wm-audio-restart-XXXXXX";
    int temporary = mkstemp(directory);
    assert(temporary >= 0);
    assert(close(temporary) == 0);
    assert(unlink(directory) == 0);
    assert(mkdir(directory, 0700) == 0);
    char audio_directory[256];
    char background_path[256];
    char intro_path[256];
    char click_path[256];
    assert(snprintf(audio_directory, sizeof(audio_directory), "%s/audio",
                    directory) < (int)sizeof(audio_directory));
    assert(mkdir(audio_directory, 0700) == 0);
    assert(snprintf(background_path, sizeof(background_path),
                    "%s/background.wav", audio_directory) <
           (int)sizeof(background_path));
    assert(snprintf(intro_path, sizeof(intro_path),
                    "%s/backgroundIntro.wav", audio_directory) <
           (int)sizeof(intro_path));
    assert(snprintf(click_path, sizeof(click_path),
                    "%s/click.wav", audio_directory) <
           (int)sizeof(click_path));
    write_tone(background_path, 1000);
    write_tone(intro_path, 2000);
    write_tone(click_path, 4000);

    WmAudio *audio = wm_audio_create(directory);
    assert(audio && device);
    WmMenu menu;
    wm_menu_init(&menu);
    wm_audio_sync(audio, &menu);
    const float starting_sample = render_first_sample();
    assert(fabsf(starting_sample - 3000.0f / 32768.0f) < 0.00001f);

    assert(wm_menu_toggle_home(&menu));
    wm_menu_tick(&menu, 1.0f);
    wm_audio_sync(audio, &menu);
    assert(render_first_sample() == 0.0f);

    /* HOME retires every old voice at blackout. The decoded clips stay
     * available, but no new voice may start during BackMenu or its black
     * service wait. The first grid buffer starts both soundtrack sources. */
    wm_audio_reset_all(audio);
    WmMenuRestartClock restart = {0};
    assert(wm_menu_restart_start(&restart));
    assert(!wm_menu_restart_advance(&restart, 159.5f));
    assert(restart.phase == WM_MENU_RESTART_BLACK);
    assert(render_first_sample() == 0.0f);
    assert(unlink(background_path) == 0);
    assert(unlink(intro_path) == 0);
    assert(wm_menu_restart_advance(&restart, 0.5f));
    assert(restart.phase == WM_MENU_RESTART_GRID);
    assert(wm_menu_return_to_menu(&menu));
    assert(menu.transition == WM_TRANSITION_NONE);
    wm_audio_sync(audio, &menu);
    assert(fabsf(render_first_sample() - starting_sample) < 0.00001f);

    /* HTML rejects a one-shot requested while muted. It must not become
     * audible later when the user restores volume. Background and loops are
     * deliberately different: they retain playback position while muted. */
    wm_audio_reset_all(audio);
    wm_audio_set_muted(audio, true);
    assert(!wm_audio_play(audio, "click"));
    wm_audio_set_muted(audio, false);
    assert(render_first_sample() == 0.0f);
    assert(wm_audio_play(audio, "click"));
    assert(fabsf(render_first_sample() - 4000.0f / 32768.0f) < 0.00001f);

    wm_audio_destroy(audio);
    assert(unlink(click_path) == 0);
    assert(rmdir(audio_directory) == 0);
    assert(rmdir(directory) == 0);
    puts("audio restart tests passed");
    return 0;
}
