#ifndef WII_MENU_RESOURCE_AUDIO_H
#define WII_MENU_RESOURCE_AUDIO_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* Signed 16-bit PCM frames are interleaved in source channel order. The
 * decoder preserves the original sample rate and optional sample loop; it
 * does not synthesize playback repetitions, envelopes or mixing. */
typedef struct WmAudioPcm {
    int16_t *samples;
    uint32_t sample_rate;
    uint32_t frame_count;
    uint32_t loop_start;
    uint32_t loop_end; /* Exclusive, equal to frame_count for BNS loops. */
    uint8_t channels;
    bool looping;
} WmAudioPcm;

/* Nintendo DSP ADPCM: each eight-byte frame carries up to 14 PCM samples.
 * Initial histories and coefficients are supplied by the source container. */
bool wm_dsp_decode(const uint8_t *data, size_t size, uint32_t sample_count,
                   const int16_t coefficients[16], int16_t history1,
                   int16_t history2, int16_t *output,
                   char *error, size_t error_capacity);

/* The BNS 1.0 INFO/DATA variant used by channel banner sounds. Initialize the
 * output to zero and release an earlier decode before reusing it. */
bool wm_bns_decode(const uint8_t *data, size_t size, WmAudioPcm *output,
                   char *error, size_t error_capacity);
void wm_audio_pcm_free(WmAudioPcm *audio);

#endif
