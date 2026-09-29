#ifndef WM_APP_CORRUPTION_VECTORS_H
#define WM_APP_CORRUPTION_VECTORS_H

#include "wii_menu/fonts/outline_font.h"

#include <stdbool.h>

/* Rasterize one of the prompt's authored cubic glyphs at a requested size.
 * The caller owns the returned bitmap and frees it with
 * wm_outline_bitmap_free(). No font file or parser is needed. */
bool wm_corruption_vector_raster(unsigned char character, unsigned pixel_size,
                                 WmOutlineBitmap *bitmap);

#endif
