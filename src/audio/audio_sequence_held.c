#include "audio_sequence_driver_internal.h"
#include "wii_menu/audio/audio_sequence.h"

#include <math.h>
#include <stdio.h>
#include <string.h>

static void set_error(char *error, size_t capacity, const char *message)
{
    if (error && capacity) snprintf(error, capacity, "%s", message);
}

static bool has_bytes(const WmRsarSequence *sequence, size_t offset,
                      size_t length)
{
    return sequence && sequence->data && offset <= sequence->size &&
           length <= sequence->size - offset;
}

bool wm_sequence_extract_held(const WmRsar *archive, const WmRsarSound *sound,
                              const uint8_t *system_menu_dol, size_t dol_size,
                              WmAudioHeldProfile *profile,
                              WmAudioHeldTables *tables, WmAudioPcm *output,
                              char *error, size_t error_capacity)
{
    set_error(error, error_capacity, "");
    if (!profile || !tables || !output) return false;
    *output = (WmAudioPcm){0};
    *profile = (WmAudioHeldProfile){0};
    *tables = (WmAudioHeldTables){0};
    WmRsarSequence sequence;
    WmRsarInstrument instrument;
    SequenceTables driver;
    static const uint8_t drag_commands[] = {
        0x81, 0x0a, 0x3c, 0x7f, 0x00, 0xff
    };
    if (!sound || sound->volume > 127 ||
        !wm_rsar_get_sequence(archive, sound, &sequence,
                              error, error_capacity) ||
        !has_bytes(&sequence, sequence.start_offset, sizeof(drag_commands)) ||
        memcmp(sequence.data + sequence.start_offset, drag_commands,
                sizeof(drag_commands)) != 0 ||
        sequence.bank_index != 1 ||
        !wm_rsar_get_instrument(archive, sequence.bank_index, 10, 60, 127,
                                &instrument, error, error_capacity) ||
        instrument.wave_index != 13 || instrument.root_key != 60 ||
        instrument.pitch != 1.0f || instrument.volume != 127 ||
        instrument.pan != 64 || instrument.envelope[0] != 104 ||
        instrument.envelope[1] != 127 || instrument.envelope[2] != 127 ||
        instrument.envelope[3] != 125 ||
        !wm_sequence_driver_load_tables(system_menu_dol, dol_size, &driver) ||
        !wm_rsar_decode_bank_wave(archive, sequence.bank_index,
                                  instrument.wave_index, output,
                                  error, error_capacity)) {
        set_error(error, error_capacity, "Unsupported held drag source or driver.");
        return false;
    }
    if (output->channels != 1 || output->sample_rate != WM_AUDIO_HELD_RATE ||
        !output->looping || output->loop_start >= output->loop_end ||
        output->loop_end > output->frame_count) {
        wm_audio_pcm_free(output);
        set_error(error, error_capacity, "Held drag requires a looping mono 32 kHz wave.");
        return false;
    }
    *profile = (WmAudioHeldProfile){
        .attack_multiplier = driver.attack[instrument.envelope[0]],
        .decay_rate = wm_sequence_release_rate(instrument.envelope[1]),
        .sustain_level = driver.sustain[instrument.envelope[2]],
        .release_rate = wm_sequence_release_rate(instrument.envelope[3]),
        /* The audited command uses velocity 127 and requires volume 127. */
        .volume = 1.0f,
        .pan = ((float)instrument.pan - 64.0f) / 63.0f
    };
    memcpy(tables->decibels, driver.decibels, sizeof(tables->decibels));
    memcpy(tables->pan, driver.pan, sizeof(tables->pan));
    if (!wm_audio_held_tables_valid(tables) ||
        !isfinite(profile->attack_multiplier) ||
        profile->attack_multiplier < 0.0f || profile->attack_multiplier >= 1.0f) {
        wm_audio_pcm_free(output);
        set_error(error, error_capacity, "Invalid held drag lookup values.");
        return false;
    }
    return true;
}
