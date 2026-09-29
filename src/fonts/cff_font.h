#ifndef WII_MENU_CFF_FONT_H
#define WII_MENU_CFF_FONT_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef struct WmCffFont WmCffFont;

typedef struct WmCffPoint {
    float x;
    float y;
} WmCffPoint;

typedef enum WmCffSegmentKind {
    WM_CFF_MOVE,
    WM_CFF_LINE,
    WM_CFF_CUBIC
} WmCffSegmentKind;

typedef struct WmCffSegment {
    WmCffSegmentKind kind;
    WmCffPoint first;
    WmCffPoint second;
    WmCffPoint end;
} WmCffSegment;

enum { WM_CFF_MAX_SEGMENTS = 2048 };

typedef struct WmCffGlyph {
    WmCffSegment segments[WM_CFF_MAX_SEGMENTS];
    unsigned count;
} WmCffGlyph;

/* Views into the owned SFNT bytes. No font data is retained outside the
 * caller's buffer, and no CFF or Type 2 program is trusted without bounds. */
WmCffFont *wm_cff_font_parse(const uint8_t *bytes, size_t size,
                             size_t table_offset, size_t table_size,
                             unsigned glyph_count);
void wm_cff_font_destroy(WmCffFont *font);
bool wm_cff_font_glyph(const WmCffFont *font, unsigned glyph_index,
                       WmCffGlyph *glyph);

#endif
