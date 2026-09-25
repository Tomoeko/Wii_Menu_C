#ifndef WII_MENU_RESOURCE_FONT_H
#define WII_MENU_RESOURCE_FONT_H

#include "wii_menu/image.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef struct WmFont WmFont;
typedef struct WmFontTextLayout WmFontTextLayout;

typedef struct WmFontMetrics {
    uint8_t cell_width;
    uint8_t cell_height;
    int8_t baseline;
    int8_t line_feed;
    uint8_t height;
    uint8_t width;
    uint8_t ascent;
    uint8_t encoding;
    uint16_t default_glyph;
} WmFontMetrics;

typedef struct WmFontGlyph {
    uint16_t sheet;
    uint16_t x;
    uint16_t y;
    uint8_t width;
    uint8_t height;
    int8_t left;
    int8_t advance;
} WmFontGlyph;

typedef struct WmFontSheetInfo {
    uint16_t width;
    uint16_t height;
    uint16_t format;
} WmFontSheetInfo;

typedef struct WmFontVertex {
    float position[3];
    float uv[2];
    float color[4];
} WmFontVertex;

typedef struct WmFontQuad {
    uint16_t sheet;
    uint16_t format;
    size_t byte_index; /* UTF-8 byte offset in the submitted text. */
    bool glyph_alpha_only; /* GX I4/I8: texture alpha masks vertex color. */
    uint32_t texture;
    WmFontVertex vertices[4]; /* LT, RT, LB, RB. */
} WmFontQuad;

typedef bool (*WmFontSheetProvider)(void *context, size_t sheet, uint32_t *texture);
typedef void (*WmFontQuadCallback)(void *context, const WmFontQuad *quad);

typedef enum WmFontAlign {
    WM_FONT_ALIGN_LEFT,
    WM_FONT_ALIGN_CENTER,
    WM_FONT_ALIGN_RIGHT
} WmFontAlign;

typedef struct WmFontDrawOptions {
    float x;
    float y;
    float size[2]; /* Native font width/height scale to this requested size. */
    float spacing;
    float alpha;
    uint8_t top_color[4];
    uint8_t bottom_color[4];
    WmFontAlign align;
    const float *matrix; /* Optional row-major 3 x 4 parent transform. */
    WmFontSheetProvider sheet_provider;
    WmFontQuadCallback on_quad;
    void *context;
} WmFontDrawOptions;

typedef struct WmFontPane {
    float size[2];
    unsigned origin;
    unsigned text_position;
    float font_size[2];
    float char_space;
    float line_space;
    bool no_wrap;
    uint8_t top_color[4];
    uint8_t bottom_color[4];
} WmFontPane;

/* Reads RFNT/RFNA metrics, character maps and sheet locations. Compressed
 * sheets stay compressed until wm_font_decode_sheet is called. */
WmFont *wm_font_decode(const uint8_t *data, size_t size,
                       char *error, size_t error_capacity);
void wm_font_destroy(WmFont *font);
const WmFontMetrics *wm_font_metrics(const WmFont *font);
size_t wm_font_sheet_count(const WmFont *font);
const WmFontSheetInfo *wm_font_sheet_info(const WmFont *font, size_t sheet);
const WmFontGlyph *wm_font_glyph(const WmFont *font, uint32_t codepoint);

/* RGBA8 image rows begin at the top edge. The caller owns the output image. */
bool wm_font_decode_sheet(const WmFont *font, size_t sheet, WmImage *image,
                          char *error, size_t error_capacity);

/* Text is UTF-8; invalid sequences use the default glyph. This API uses the
 * source's two-axis size form: X scales by size[0]/font width, Y by
 * size[1]/font height. For the source's scalar size S, use
 * {S * font width / font height, S}. */
float wm_font_text_width(const WmFont *font, const char *text,
                         const float size[2], float spacing);
float wm_font_text_width_n(const WmFont *font, const char *text, size_t length,
                           const float size[2], float spacing);
void wm_font_emit_line(const WmFont *font, const char *text,
                        const WmFontDrawOptions *options);

/* Cache a pane layout while its text, pane geometry and font size are stable.
 * This preserves the HTML port's wrapping and 3 x 3 alignment calculations. */
WmFontTextLayout *wm_font_layout_pane(const WmFont *font, const char *text,
                                      const WmFontPane *pane);
size_t wm_font_text_layout_line_count(const WmFontTextLayout *layout);
/* Local insertion point on the same line grid used to draw the text. The
 * caller supplies a UTF-8 byte boundary; automatic wraps choose the next
 * line, while an explicit newline chooses the preceding line's end. */
bool wm_font_text_layout_caret(const WmFontTextLayout *layout,
                               size_t byte_index, float *x, float *y);
void wm_font_text_layout_destroy(WmFontTextLayout *layout);
void wm_font_emit_pane(const WmFontTextLayout *layout,
                        const float parent_matrix[12], float alpha,
                        WmFontSheetProvider sheet_provider,
                        WmFontQuadCallback on_quad, void *context);

#endif
