#ifndef WM_SETTINGS_PNG_H
#define WM_SETTINGS_PNG_H

#include "wii_menu/image.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* Decode the non-interlaced RGBA8 PNG variant used by Settings artwork. */
bool wm_settings_png_decode(const uint8_t *data, size_t size, WmImage *image);

#endif
