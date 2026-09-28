#ifndef WII_MENU_AUDIO_SEQUENCE_DRIVER_INTERNAL_H
#define WII_MENU_AUDIO_SEQUENCE_DRIVER_INTERNAL_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

enum {
    WM_SEQUENCE_RATE = 32000
};

typedef struct SequenceTables {
    float attack[128];
    int16_t sustain[128];
    float decibels[965];
    float pan[257];
    uint32_t reverb_frames[8];
    float reverb_preset[6];
} SequenceTables;

/* Decode the audited USA 4.3 driver tables from caller-owned DOL bytes. */
bool wm_sequence_driver_load_tables(const uint8_t *dol, size_t dol_size,
                                    SequenceTables *tables);
float wm_sequence_release_rate(uint8_t value);

#endif
