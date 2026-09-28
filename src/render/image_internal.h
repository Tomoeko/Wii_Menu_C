#ifndef WM_RENDER_IMAGE_INTERNAL_H
#define WM_RENDER_IMAGE_INTERNAL_H

#include "wii_menu/render/image.h"

#include <stddef.h>

/* Reject an oversized header before allocating its pixel payload. */
bool wm_image_read_bounded(const char *path, size_t maximum_bytes, WmImage *image);

#endif
