#include "audio_sequence_driver_internal.h"

#include <math.h>
#include <string.h>

static uint16_t big_u16(const uint8_t *data) {
    return (uint16_t)(((uint16_t)data[0] << 8) | data[1]);
}

static uint32_t big_u32(const uint8_t *data) {
    return ((uint32_t)data[0] << 24) | ((uint32_t)data[1] << 16) |
           ((uint32_t)data[2] << 8) | data[3];
}

static bool dol_range(const uint8_t *dol, size_t dol_size, uint32_t address,
                      size_t count, const uint8_t **bytes) {
    if (!dol || dol_size < 0x100 || count > UINT32_MAX || address > UINT32_MAX - count)
        return false;
    for (size_t section = 0; section < 18; section++) {
        uint32_t file_offset = big_u32(dol + section * 4);
        uint32_t start = big_u32(dol + 0x48 + section * 4);
        uint32_t size = big_u32(dol + 0x90 + section * 4);
        if (!size || file_offset < 0x100 || file_offset > dol_size ||
            size > dol_size - file_offset)
            continue;
        if (address >= start && address - start <= size &&
            count <= size - (address - start)) {
            *bytes = dol + file_offset + (address - start);
            return true;
        }
    }
    return false;
}

static float big_float(const uint8_t *data) {
    uint32_t bits = big_u32(data);
    float number;
    memcpy(&number, &bits, sizeof(number));
    return number;
}

bool wm_sequence_driver_load_tables(const uint8_t *dol, size_t dol_size,
                                    SequenceTables *tables) {
    const uint8_t *bytes;
    if (!dol_range(dol, dol_size, 0x8161e3f0u, sizeof(tables->attack), &bytes))
        return false;
    for (size_t index = 0; index < 128; index++) {
        tables->attack[index] = big_float(bytes + index * 4);
        if (!isfinite(tables->attack[index]))
            return false;
    }
    if (!dol_range(dol, dol_size, 0x8161e2f0u, sizeof(tables->sustain), &bytes))
        return false;
    for (size_t index = 0; index < 128; index++) {
        tables->sustain[index] = (int16_t)big_u16(bytes + index * 2);
    }
    if (!dol_range(dol, dol_size, 0x8161ead8u, sizeof(tables->decibels), &bytes))
        return false;
    for (size_t index = 0; index < 965; index++) {
        tables->decibels[index] = big_float(bytes + index * 4);
        if (!isfinite(tables->decibels[index]))
            return false;
    }
    if (!dol_range(dol, dol_size, 0x8161f9ecu, sizeof(tables->pan), &bytes))
        return false;
    for (size_t index = 0; index < 257; index++) {
        tables->pan[index] = big_float(bytes + index * 4);
        if (!isfinite(tables->pan[index]))
            return false;
    }
    if (!dol_range(dol, dol_size, 0x81685da0u, sizeof(tables->reverb_frames), &bytes))
        return false;
    for (size_t index = 0; index < 8; index++) {
        tables->reverb_frames[index] = big_u32(bytes + index * 4);
        if (!tables->reverb_frames[index] ||
            tables->reverb_frames[index] > WM_SEQUENCE_RATE)
            return false;
    }
    if (!dol_range(dol, dol_size, 0x8160f048u, sizeof(tables->reverb_preset), &bytes))
        return false;
    for (size_t index = 0; index < 6; index++) {
        tables->reverb_preset[index] = big_float(bytes + index * 4);
        if (!isfinite(tables->reverb_preset[index]))
            return false;
    }
    return tables->reverb_preset[0] == 0 && tables->reverb_preset[1] > 0 &&
           tables->reverb_preset[1] <= 10 && tables->reverb_preset[4] == 0;
}

float wm_sequence_release_rate(uint8_t value) {
    if (value == 127)
        return 65535;
    if (value == 126)
        return 24;
    if (value < 50)
        return ((float)(value * 2 + 1) / 128.0f) / 5.0f;
    return (60.0f / (126.0f - (float)value)) / 5.0f;
}
