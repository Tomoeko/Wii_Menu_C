#ifndef WM_RESOURCE_BYTES_H
#define WM_RESOURCE_BYTES_H

#include "wii_menu/support/bounds.h"
#include "wii_menu/support/endian.h"

static inline bool wm_resource_range_fits(size_t size, size_t offset, size_t length) {
    return wm_bounds_contains(size, offset, length);
}

/* Check the containing range before reading from either pointer. */
static inline uint16_t wm_resource_be16(const uint8_t *bytes) {
    return wm_read_be16(bytes);
}

static inline uint32_t wm_resource_be32(const uint8_t *bytes) {
    return wm_read_be32(bytes);
}

#endif
