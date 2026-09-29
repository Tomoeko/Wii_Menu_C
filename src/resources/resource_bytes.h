#ifndef WM_RESOURCE_BYTES_H
#define WM_RESOURCE_BYTES_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

static inline bool wm_resource_range_fits(size_t size, size_t offset, size_t length) {
    return offset <= size && length <= size - offset;
}

/* Check the containing range before reading from either pointer. */
static inline uint16_t wm_resource_be16(const uint8_t *bytes) {
    return (uint16_t)(((uint16_t)bytes[0] << 8) | bytes[1]);
}

static inline uint32_t wm_resource_be32(const uint8_t *bytes) {
    return ((uint32_t)bytes[0] << 24) | ((uint32_t)bytes[1] << 16) |
           ((uint32_t)bytes[2] << 8) | bytes[3];
}

#endif
