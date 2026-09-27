#define _POSIX_C_SOURCE 200809L

#include "wii_menu/audio/audio.h"
#include "audio_platform.h"

#include <assert.h>
#include <errno.h>
#include <pthread.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

struct WmAudioDevice {
    WmAudioRender render;
    void *context;
};

static WmAudioDevice *device;
static uint64_t elapsed_nanoseconds;
static bool deny_mixer_lock;

int clock_gettime(clockid_t clock, struct timespec *value)
{
    assert(clock == CLOCK_MONOTONIC);
    value->tv_sec = (time_t)(elapsed_nanoseconds / UINT64_C(1000000000));
    value->tv_nsec = (long)(elapsed_nanoseconds % UINT64_C(1000000000));
    return 0;
}

int pthread_mutex_trylock(pthread_mutex_t *mutex)
{
    /* There are no concurrent threads in this fake backend. A denied lock
     * represents sustained main-thread contention on every render callback. */
    return deny_mixer_lock ? EBUSY : pthread_mutex_lock(mutex);
}

WmAudioDevice *wm_audio_device_open(WmAudioRender render, void *context)
{
    assert(!device);
    device = calloc(1, sizeof(*device));
    assert(device);
    device->render = render;
    device->context = context;
    return device;
}

void wm_audio_device_close(WmAudioDevice *opened)
{
    assert(opened == device);
    free(device);
    device = NULL;
}

static void render(void)
{
    float samples[8];
    for (size_t sample = 0; sample < 8; ++sample) samples[sample] = 1.0f;
    elapsed_nanoseconds += UINT64_C(500000000);
    device->render(device->context, samples, 4);
    for (size_t sample = 0; sample < 8; ++sample) assert(samples[sample] == 0.0f);
}

int main(void)
{
    char directory[] = "/tmp/wm-audio-statistics-XXXXXX";
    int temporary = mkstemp(directory);
    assert(temporary >= 0);
    assert(close(temporary) == 0 && unlink(directory) == 0);
    assert(mkdir(directory, 0700) == 0);
    FILE *capture = tmpfile();
    assert(capture);
    int saved_stderr = dup(STDERR_FILENO);
    assert(saved_stderr >= 0);
    assert(fflush(stderr) == 0);
    assert(dup2(fileno(capture), STDERR_FILENO) >= 0);

    assert(unsetenv("WM_PSVR2_AUDIO_STATS") == 0);
    deny_mixer_lock = true;
    WmAudio *audio = wm_audio_create(directory);
    assert(audio && device);
    render();
    render();
    wm_audio_destroy(audio);
    assert(fflush(stderr) == 0 && ftell(capture) == 0);

    assert(setenv("WM_PSVR2_AUDIO_STATS", "1", 1) == 0);
    audio = wm_audio_create(directory);
    assert(audio && device);
    render();
    render();
    deny_mixer_lock = false;
    render();
    render();
    wm_audio_destroy(audio);
    assert(fflush(stderr) == 0);
    rewind(capture);
    char output[1024];
    size_t bytes = fread(output, 1, sizeof(output) - 1, capture);
    output[bytes] = '\0';
    assert(!ferror(capture) && feof(capture));
    assert(strstr(output, "buffers=2 lock_misses=2 nonzero=0 peak=0 snapshot=busy\n"));
    assert(strstr(output, "buffers=2 lock_misses=0 nonzero=0 peak=0 snapshot=locked"));
    assert(strstr(output, "bgm=inactive bgm_frame=0 started=0 menu_paused=0 muted=0 volume=1"));
    assert(dup2(saved_stderr, STDERR_FILENO) >= 0);
    assert(close(saved_stderr) == 0 && fclose(capture) == 0);
    assert(rmdir(directory) == 0);
    puts("audio statistics tests passed");
    return 0;
}
