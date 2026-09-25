#ifndef WII_MENU_OUTLINE_FONT_H
#define WII_MENU_OUTLINE_FONT_H

#include "wii_menu/platform.h"
#include "wii_menu/resource_font.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef struct WmOutlineFont WmOutlineFont;

typedef struct WmOutlineBitmap {
    uint8_t *alpha;
    unsigned width;
    unsigned height;
    int left;
    int top; /* Pixel distance from the baseline to the bitmap's top. */
    float advance;
} WmOutlineBitmap;

/* The input may be an SFNT or a TrueType collection. The decoder copies the
 * bounded source bytes and retains no pointer into the caller's buffer.
 * Only Unicode BMP format-4 cmap, TrueType glyf outlines and hmtx are used. */
WmOutlineFont *wm_outline_font_decode(const uint8_t *bytes, size_t size,
                                      unsigned face_index);
WmOutlineFont *wm_outline_font_load(const char *path, unsigned face_index);
void wm_outline_font_destroy(WmOutlineFont *font, WmPlatform *platform);

/* A standalone glyph raster is useful for parser tests and local inspection.
 * Its 4 x 4 coverage antialiasing does not execute TrueType hint programs. */
bool wm_outline_font_raster(const WmOutlineFont *font, uint32_t codepoint,
                            unsigned pixel_size, WmOutlineBitmap *bitmap);
void wm_outline_bitmap_free(WmOutlineBitmap *bitmap);
float wm_outline_font_text_width(const WmOutlineFont *font,
                                 const char *utf8, unsigned pixel_size);

/* Prepare an ASCII/Latin-1 atlas lazily for each distinct Settings font size.
 * Once prepared, ordinary text drawing allocates no memory or GPU objects.
 * x_scale and x_offset project the Settings document into the logical frame. */
bool wm_outline_font_draw_line(WmOutlineFont *font, WmPlatform *platform,
                                const char *utf8, unsigned pixel_size,
                                float x, float top, WmFontAlign align,
                                float x_scale, float x_offset,
                                WmColor color, bool synthetic_bold);

#endif
