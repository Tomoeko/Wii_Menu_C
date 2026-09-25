#define _POSIX_C_SOURCE 200809L

#include "audio_platform.h"

#include <dlfcn.h>
#include <pthread.h>
#include <stdbool.h>
#include <stdatomic.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

/* ALSA is loaded from the host audio system at runtime. This keeps the C
 * build independent of distribution headers and optional audio packages. */
struct WmAudioDevice {
    void *library;
    void *pcm;
    WmAudioRender render;
    void *context;
    pthread_t thread;
    atomic_bool running;
    bool thread_started;
    int (*pcm_open)(void **, const char *, int, int);
    int (*pcm_close)(void *);
    int (*pcm_set_params)(void *, int, int, unsigned, unsigned, int, unsigned);
    long (*pcm_writei)(void *, const void *, unsigned long);
    int (*pcm_recover)(void *, int, int);
    int (*pcm_format_value)(const char *);
    int (*pcm_access_value)(const char *);
};

static bool load_symbol(void *library, const char *name,
                        void *function, size_t function_size)
{
    void *symbol = dlsym(library, name);
    if (!symbol || function_size != sizeof(symbol)) return false;
    memcpy(function, &symbol, sizeof(symbol));
    return true;
}

static void *output_thread(void *context)
{
    WmAudioDevice *device = context;
    float buffer[512 * 2];
    while (atomic_load_explicit(&device->running, memory_order_relaxed)) {
        device->render(device->context, buffer, 512);
        unsigned long position = 0;
        while (position < 512 &&
               atomic_load_explicit(&device->running, memory_order_relaxed)) {
            long written = device->pcm_writei(device->pcm,
                                               buffer + position * 2,
                                               512 - position);
            if (written < 0) {
                if (device->pcm_recover(device->pcm, (int)written, 1) < 0)
                    atomic_store(&device->running, false);
                break;
            }
            if (!written) break;
            position += (unsigned long)written;
        }
    }
    return NULL;
}

WmAudioDevice *wm_audio_device_open(WmAudioRender render, void *context)
{
    if (!render) return NULL;
    WmAudioDevice *device = calloc(1, sizeof(*device));
    if (!device) return NULL;
    device->library = dlopen("libasound.so.2", RTLD_NOW | RTLD_LOCAL);
    if (!device->library ||
        !load_symbol(device->library, "snd_pcm_open", &device->pcm_open,
                     sizeof(device->pcm_open)) ||
        !load_symbol(device->library, "snd_pcm_close", &device->pcm_close,
                     sizeof(device->pcm_close)) ||
        !load_symbol(device->library, "snd_pcm_set_params", &device->pcm_set_params,
                     sizeof(device->pcm_set_params)) ||
        !load_symbol(device->library, "snd_pcm_writei", &device->pcm_writei,
                     sizeof(device->pcm_writei)) ||
        !load_symbol(device->library, "snd_pcm_recover", &device->pcm_recover,
                     sizeof(device->pcm_recover)) ||
        !load_symbol(device->library, "snd_pcm_format_value",
                     &device->pcm_format_value,
                     sizeof(device->pcm_format_value)) ||
        !load_symbol(device->library, "snd_pcm_access_value",
                     &device->pcm_access_value,
                     sizeof(device->pcm_access_value)) ||
        device->pcm_open(&device->pcm, "default", 0, 0) < 0 ||
        device->pcm_set_params(device->pcm,
                               device->pcm_format_value("FLOAT_LE"),
                               device->pcm_access_value("RW_INTERLEAVED"),
                               2, 48000, 1, 20000) < 0) {
        wm_audio_device_close(device);
        return NULL;
    }
    device->render = render;
    device->context = context;
    atomic_store(&device->running, true);
    if (pthread_create(&device->thread, NULL, output_thread, device) != 0) {
        wm_audio_device_close(device);
        return NULL;
    }
    device->thread_started = true;
    return device;
}

void wm_audio_device_close(WmAudioDevice *device)
{
    if (!device) return;
    atomic_store(&device->running, false);
    if (device->thread_started) pthread_join(device->thread, NULL);
    if (device->pcm && device->pcm_close) device->pcm_close(device->pcm);
    if (device->library) dlclose(device->library);
    free(device);
}
