#ifndef WII_MENU_PSVR2_AUDIO_CODEC_PATH_H
#define WII_MENU_PSVR2_AUDIO_CODEC_PATH_H

#include <errno.h>
#include <stdint.h>

enum {
    WM_AUDIO_CODEC_DAC_CONTROL = 5,
    WM_AUDIO_CODEC_DAC_MUTE = 0x0008,
    WM_AUDIO_CODEC_HEADPHONE_LEFT = 2,
    WM_AUDIO_CODEC_HEADPHONE_RIGHT = 3,
    WM_AUDIO_CODEC_HEADPHONE_FIELD = 0x01ff,
    WM_AUDIO_CODEC_HEADPHONE_ZERO_DB = 121,
    WM_AUDIO_CODEC_HEADPHONE_ZERO_CROSS = 0x0080,
    WM_AUDIO_CODEC_VOLUME_UPDATE = 0x0100,
    WM_AUDIO_CODEC_SIDETONE_0 = 57,
    WM_AUDIO_CODEC_SIDETONE_1 = 58,
    WM_AUDIO_CODEC_SIDETONE_SOURCE = 0x000c
};

typedef int (*WmAudioCodecUpdate)(void *context, uint8_t reg,
                                  uint16_t mask, uint16_t value);

/* Retail 06.00's headphone setter uses milli-dB / 1000 + 0x79.
 * Keep the existing 36 dB startup attenuation curve, with no positive gain:
 * 50% requests -18 dB (code 103), and 100% requests 0 dB (code 121).
 * The portable mixer's zero volume still provides exact digital silence. */
static inline int wm_audio_codec_headphone_code(int volume_percent, uint16_t *code)
{
    if (!code) {
        errno = EINVAL;
        return -1;
    }
    if (volume_percent < 0 || volume_percent > 100) {
        errno = ERANGE;
        return -1;
    }
    int milli_db = (volume_percent - 100) * 360;
    *code = (uint16_t)(WM_AUDIO_CODEC_HEADPHONE_ZERO_DB + milli_db / 1000);
    return 0;
}

/* The stock codec sequence includes microphone monitoring. The WM8961-family
 * map identifies registers 57/58 as ADC-to-DAC sidetone, not headphone gain.
 * Reapply the playback route after the headphone power sequencer completes. */
static inline int wm_audio_codec_start_playback(void *context, WmAudioCodecUpdate update,
                                                uint16_t headphone_volume)
{
    if (headphone_volume > WM_AUDIO_CODEC_HEADPHONE_ZERO_DB) {
        errno = ERANGE;
        return -1;
    }
    if (update(context, WM_AUDIO_CODEC_DAC_CONTROL,
               WM_AUDIO_CODEC_DAC_MUTE, WM_AUDIO_CODEC_DAC_MUTE) < 0 ||
        update(context, WM_AUDIO_CODEC_SIDETONE_0,
               WM_AUDIO_CODEC_SIDETONE_SOURCE, 0) < 0 ||
        update(context, WM_AUDIO_CODEC_SIDETONE_1,
               WM_AUDIO_CODEC_SIDETONE_SOURCE, 0) < 0 ||
        update(context, WM_AUDIO_CODEC_HEADPHONE_LEFT,
               WM_AUDIO_CODEC_HEADPHONE_FIELD,
               headphone_volume | WM_AUDIO_CODEC_HEADPHONE_ZERO_CROSS) < 0 ||
        update(context, WM_AUDIO_CODEC_HEADPHONE_RIGHT,
               WM_AUDIO_CODEC_HEADPHONE_FIELD,
               headphone_volume | WM_AUDIO_CODEC_HEADPHONE_ZERO_CROSS |
               WM_AUDIO_CODEC_VOLUME_UPDATE) < 0)
        return -1;
    return update(context, WM_AUDIO_CODEC_DAC_CONTROL, WM_AUDIO_CODEC_DAC_MUTE, 0);
}

static inline int wm_audio_codec_mute_playback(void *context, WmAudioCodecUpdate update)
{
    return update(context, WM_AUDIO_CODEC_DAC_CONTROL,
                  WM_AUDIO_CODEC_DAC_MUTE, WM_AUDIO_CODEC_DAC_MUTE);
}

#endif
