#ifndef WII_MENU_RESOURCE_FONT_INTERNAL_H
#define WII_MENU_RESOURCE_FONT_INTERNAL_H

#include "wii_menu/resources/resource_font.h"

/* The decoder owns these buffers. Text geometry borrows them while the font
 * remains alive; text layouts must be destroyed before the font. */
typedef struct FontSheet {
    WmFontSheetInfo info;
    size_t offset;
    size_t stored_size;
} FontSheet;

struct WmFont {
    uint8_t *data;
    size_t size;
    WmFontMetrics metrics;
    FontSheet *sheets;
    size_t sheet_count;
    size_t sheet_size;
    bool compressed;
    WmFontGlyph *glyphs;
    uint8_t *glyph_present;
    size_t glyph_count;
    uint16_t *characters;
};

#endif
