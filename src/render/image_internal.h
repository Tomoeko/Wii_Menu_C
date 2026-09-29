#ifndef WM_RENDER_IMAGE_INTERNAL_H
#define WM_RENDER_IMAGE_INTERNAL_H

#include "wii_menu/render/image.h"

#include <stddef.h>
#include <stdio.h>

/* Reject an oversized header before allocating its pixel payload. */
bool wm_image_read_bounded(const char *path, size_t maximum_bytes, WmImage *image);
bool wm_image_read_bounded_stream(FILE *stream, size_t maximum_bytes, WmImage *image);

/* Reads a valid header and restores the stream to its start for decoding. */
bool wm_image_declared_bytes(FILE *stream, size_t *bytes);

#endif
