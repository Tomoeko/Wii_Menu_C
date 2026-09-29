#include "cff_font.h"

#include <assert.h>
#include <stdio.h>
#include <string.h>

/* Authored Type 2 fixture: a rectangular A with no private or global subrs.
 * It contains no extracted font data. */
static void make_cff(uint8_t bytes[51]) {
    static const uint8_t source[51] = {
        1,   0,   4,   4,           /* CFF header */
        0,   1,   1,   1,   2, 'A', /* Name INDEX */
        0,   1,   1,   1,   5,      /* Top DICT INDEX */
        28,  0,   23,  17,          /* CharStrings at byte 23 */
        0,   0,   0,   0,           /* String and Global Subr INDEXes */
        0,   2,   1,   1,   2, 23,  /* CharStrings INDEX */
        14,                         /* .notdef */
        139, 139, 21,               /* rmoveto 0, 0 */
        28,  1,   244, 139,         /* 500, 0 */
        139, 28,  2,   188,         /* 0, 700 */
        28,  254, 12,  139,         /* -500, 0 */
        139, 28,  253, 68,          /* 0, -700 */
        5,   14                     /* rlineto, endchar */
    };
    memcpy(bytes, source, sizeof(source));
}

int main(void) {
    uint8_t bytes[51];
    make_cff(bytes);
    assert(!wm_cff_font_parse(bytes, sizeof(bytes) - 1, 0, sizeof(bytes) - 1, 2));
    WmCffFont *font = wm_cff_font_parse(bytes, sizeof(bytes), 0, sizeof(bytes), 2);
    assert(font);
    WmCffGlyph glyph;
    assert(wm_cff_font_glyph(font, 1, &glyph));
    assert(glyph.count == 5);
    assert(glyph.segments[0].kind == WM_CFF_MOVE);
    assert(glyph.segments[1].kind == WM_CFF_LINE);
    assert(glyph.segments[1].end.x == 500);
    assert(glyph.segments[2].end.y == 700);
    assert(glyph.segments[4].end.x == 0);
    assert(glyph.segments[4].end.y == 0);
    assert(!wm_cff_font_glyph(font, 2, &glyph));
    wm_cff_font_destroy(font);

    make_cff(bytes);
    bytes[28] = 15; /* Replace A with a shorter curve-then-line program. */
    static const uint8_t curve_line[] = {
        139, 139, 21,                 /* rmoveto 0, 0 */
        189, 139, 139, 189, 189, 139, /* cubic to 100, 50 */
        139, 89,  24,  14             /* line to 100, 0 */
    };
    memcpy(bytes + 30, curve_line, sizeof(curve_line));
    font = wm_cff_font_parse(bytes, sizeof(bytes), 0, sizeof(bytes), 2);
    assert(font && wm_cff_font_glyph(font, 1, &glyph));
    assert(glyph.count == 3);
    assert(glyph.segments[1].kind == WM_CFF_CUBIC);
    assert(glyph.segments[1].end.x == 100);
    assert(glyph.segments[2].kind == WM_CFF_LINE);
    assert(glyph.segments[2].end.y == 0);
    wm_cff_font_destroy(font);

    make_cff(bytes);
    bytes[28] = 1; /* Third INDEX offset moves backwards. */
    assert(!wm_cff_font_parse(bytes, sizeof(bytes), 0, sizeof(bytes), 2));
    make_cff(bytes);
    bytes[49] = 12; /* Unsupported escaped operator, missing operand. */
    font = wm_cff_font_parse(bytes, sizeof(bytes), 0, sizeof(bytes), 2);
    assert(font);
    assert(!wm_cff_font_glyph(font, 1, &glyph));
    wm_cff_font_destroy(font);
    puts("Bounded CFF INDEX and Type 2 path parsing passed.");
    return 0;
}
