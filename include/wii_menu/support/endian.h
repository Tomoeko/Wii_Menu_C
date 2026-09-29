#ifndef WM_SUPPORT_ENDIAN_H
#define WM_SUPPORT_ENDIAN_H

#include <stdint.h>

/* Callers must verify that 2, 4, or 8 bytes remain before access. */
static inline uint16_t wm_read_be16(const uint8_t *bytes) {
    return (uint16_t)(((uint16_t)bytes[0] << 8) | bytes[1]);
}

static inline uint32_t wm_read_be32(const uint8_t *bytes) {
    return ((uint32_t)bytes[0] << 24) | ((uint32_t)bytes[1] << 16) |
           ((uint32_t)bytes[2] << 8) | bytes[3];
}

static inline uint64_t wm_read_be64(const uint8_t *bytes) {
    return ((uint64_t)wm_read_be32(bytes) << 32) | wm_read_be32(bytes + 4);
}

static inline void wm_write_be16(uint8_t *bytes, uint16_t value) {
    bytes[0] = (uint8_t)(value >> 8);
    bytes[1] = (uint8_t)value;
}

static inline void wm_write_be32(uint8_t *bytes, uint32_t value) {
    bytes[0] = (uint8_t)(value >> 24);
    bytes[1] = (uint8_t)(value >> 16);
    bytes[2] = (uint8_t)(value >> 8);
    bytes[3] = (uint8_t)value;
}

#endif
