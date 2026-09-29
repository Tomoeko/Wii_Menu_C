#define _POSIX_C_SOURCE 200809L

#include "wii_menu/audio/audio_wave.h"
#include "wii_menu/resources/resource_rsar.h"

#include <assert.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/resource.h>
#include <unistd.h>

int main(void) {
    int16_t samples[] = {0,   32767, -32768, 1250,  -1250, 200,
                         100, -100,  1234,   -1234, 0,     0};
    WmAudioPcm source = {.samples = samples,
                         .sample_rate = 32000,
                         .frame_count = 6,
                         .loop_start = 2,
                         .loop_end = 5,
                         .channels = 2,
                         .looping = true};
    char path[] = "wm-audio-wave-XXXXXX";
    int descriptor = mkstemp(path);
    assert(descriptor >= 0);
    close(descriptor);
    char error[128];
    assert(wm_audio_wav_write(path, &source, error, sizeof(error)));
    WmAudioPcm result = {0};
    assert(wm_audio_wav_read(path, &result, error, sizeof(error)));
    assert(result.sample_rate == source.sample_rate);
    assert(result.frame_count == source.frame_count);
    assert(result.channels == source.channels);
    assert(result.looping);
    assert(result.loop_start == source.loop_start);
    assert(result.loop_end == source.loop_end);
    assert(memcmp(result.samples, samples, sizeof(samples)) == 0);
    wm_audio_pcm_free(&result);

    /* Force the temporary write to fail after opening. The previous WAV
     * must still be readable, even when the final flush fails. */
    struct sigaction ignored = {0};
    struct sigaction previous_signal;
    struct rlimit previous_limit;
    assert(getrlimit(RLIMIT_FSIZE, &previous_limit) == 0);
    assert(previous_limit.rlim_max >= 1);
    ignored.sa_handler = SIG_IGN;
    assert(sigemptyset(&ignored.sa_mask) == 0);
    assert(sigaction(SIGXFSZ, &ignored, &previous_signal) == 0);
    struct rlimit limited = previous_limit;
    limited.rlim_cur = 1;
    assert(setrlimit(RLIMIT_FSIZE, &limited) == 0);
    assert(!wm_audio_wav_write(path, &source, error, sizeof(error)));
    assert(setrlimit(RLIMIT_FSIZE, &previous_limit) == 0);
    assert(sigaction(SIGXFSZ, &previous_signal, NULL) == 0);

    assert(wm_audio_wav_read(path, &result, error, sizeof(error)));
    assert(result.frame_count == source.frame_count);
    assert(memcmp(result.samples, samples, sizeof(samples)) == 0);
    wm_audio_pcm_free(&result);
    assert(remove(path) == 0);

    uint8_t truncated[32] = {0};
    WmRsar archive;
    assert(!wm_rsar_open(truncated, sizeof(truncated), &archive, error, sizeof(error)));
    return 0;
}
