#ifndef WM_SUPPORT_BOUNDS_H
#define WM_SUPPORT_BOUNDS_H

#include <stdbool.h>
#include <stddef.h>

/* Subtraction after the offset check avoids overflow in offset + length. */
static inline bool wm_bounds_contains(size_t size, size_t offset, size_t length) {
    return offset <= size && length <= size - offset;
}

#endif
