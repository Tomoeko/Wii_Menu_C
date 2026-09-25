#ifndef WM_SETTINGS_GIF_H
#define WM_SETTINGS_GIF_H

#include "wii_menu/image.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* Decode one static GIF image from the locally extracted Settings archive.
 * Animated frames and application extensions are intentionally unsupported. */
bool wm_settings_gif_decode(const uint8_t *data, size_t size, WmImage *image);

#endif
