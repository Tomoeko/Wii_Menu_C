#ifndef WM_OUTLINE_VECTOR_H
#define WM_OUTLINE_VECTOR_H

#include "cff_font.h"
#include "wii_menu/fonts/outline_font.h"

/* Render an authored cubic path through the same coverage rasterizer as
 * OpenType/CFF glyphs. Coordinates and advance are in font units. */
bool wm_outline_raster_vector(const WmCffSegment *segments,
                              unsigned segment_count, unsigned units_per_em,
                              unsigned pixel_size, float advance_units,
                              WmOutlineBitmap *bitmap);

#endif
