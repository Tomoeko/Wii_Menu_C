#ifndef WM_AUDIO_SEQUENCE_BOUNDS_H
#define WM_AUDIO_SEQUENCE_BOUNDS_H

#include "wii_menu/resources/resource_rsar.h"
#include "wii_menu/support/bounds.h"

static inline bool wm_sequence_has_bytes(const WmRsarSequence *sequence, size_t offset,
                                         size_t length) {
    return sequence && sequence->data &&
           wm_bounds_contains(sequence->size, offset, length);
}

#endif
