#ifndef WII_MENU_RESOURCE_RSAR_H
#define WII_MENU_RESOURCE_RSAR_H

#include "wii_menu/resource_audio.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* Views into a caller-owned, validated RSAR 1.1 buffer. The archive bytes
 * must outlive this structure and every sequence view returned from it. */
typedef struct WmRsar {
    const uint8_t *data;
    size_t size;
    size_t symbol_block;
    size_t info_block;
    size_t symbol_table;
    size_t sound_table;
    size_t bank_table;
    size_t file_table;
    size_t group_table;
} WmRsar;

typedef struct WmRsarSound {
    uint32_t file_index;
    uint32_t archive_index;
    uint8_t volume;
    uint8_t type; /* 1: RSEQ, 3: RWSD */
    size_t extra;
} WmRsarSound;

typedef struct WmRsarSequence {
    const uint8_t *data;
    size_t size;
    uint32_t start_offset;
    uint32_t bank_index;
} WmRsarSequence;

typedef struct WmRsarInstrument {
    uint32_t wave_index;
    uint8_t envelope[4];
    uint8_t root_key;
    uint8_t volume;
    uint8_t pan;
    float pitch;
} WmRsarInstrument;

bool wm_rsar_open(const uint8_t *data, size_t size, WmRsar *archive,
                  char *error, size_t error_capacity);
size_t wm_rsar_sound_count(const WmRsar *archive);
bool wm_rsar_sound_at(const WmRsar *archive, size_t index,
                      char *symbol, size_t symbol_capacity,
                      WmRsarSound *sound);
bool wm_rsar_find_sound(const WmRsar *archive, const char *symbol,
                        WmRsarSound *sound);
bool wm_rsar_decode_direct_wave(const WmRsar *archive,
                                const WmRsarSound *sound, WmAudioPcm *output,
                                char *error, size_t error_capacity);
bool wm_rsar_get_sequence(const WmRsar *archive, const WmRsarSound *sound,
                          WmRsarSequence *sequence,
                          char *error, size_t error_capacity);
bool wm_rsar_decode_bank_wave(const WmRsar *archive, uint32_t bank_index,
                              uint32_t wave_index, WmAudioPcm *output,
                              char *error, size_t error_capacity);
bool wm_rsar_get_instrument(const WmRsar *archive, uint32_t bank_index,
                            uint32_t program, uint8_t key, uint8_t velocity,
                            WmRsarInstrument *instrument,
                            char *error, size_t error_capacity);
bool wm_rsar_resolve_instrument(const WmRsar *archive, uint32_t bank_index,
                                uint32_t program, uint8_t key,
                                uint8_t velocity, uint32_t *wave_index,
                                uint8_t *root_key, float *pitch,
                                char *error, size_t error_capacity);

#endif
