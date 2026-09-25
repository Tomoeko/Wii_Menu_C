#include "wii_menu/outline_font.h"

#include <assert.h>
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

struct WmPlatform { unsigned marker; };

static unsigned texture_creates;
static unsigned texture_destroys;
static unsigned draws;

uint32_t wm_platform_create_texture(WmPlatform *platform, int width,
                                    int height, const uint8_t *rgba) {
    assert(platform && rgba && width >= 512 && height >= 512);
    texture_creates++;
    return texture_creates;
}

void wm_platform_destroy_texture(WmPlatform *platform, uint32_t texture) {
    assert(platform && texture);
    texture_destroys++;
}

void wm_platform_draw_quad(WmPlatform *platform, const WmQuad *quad) {
    assert(platform && quad && quad->texture && quad->width > 0);
    draws++;
}

static void put16(uint8_t *bytes, size_t offset, unsigned value) {
    bytes[offset] = (uint8_t)(value >> 8);
    bytes[offset + 1] = (uint8_t)value;
}

static void put32(uint8_t *bytes, size_t offset, unsigned value) {
    bytes[offset] = (uint8_t)(value >> 24);
    bytes[offset + 1] = (uint8_t)(value >> 16);
    bytes[offset + 2] = (uint8_t)(value >> 8);
    bytes[offset + 3] = (uint8_t)value;
}

static unsigned get32(const uint8_t *bytes, size_t offset) {
    return ((unsigned)bytes[offset] << 24) |
           ((unsigned)bytes[offset + 1] << 16) |
           ((unsigned)bytes[offset + 2] << 8) | bytes[offset + 3];
}

static void directory(uint8_t *bytes, unsigned index, const char *name,
                      unsigned offset, unsigned length) {
    size_t entry = 12 + (size_t)index * 16;
    memcpy(bytes + entry, name, 4);
    put32(bytes, entry + 8, offset);
    put32(bytes, entry + 12, length);
}

/* A complete small SFNT: A is a four-point rectangle; B is a compound
 * referencing A. The fixture contains no private glyph data. */
static void make_font(uint8_t bytes[356]) {
    memset(bytes, 0, 356);
    put32(bytes, 0, 0x00010000);
    put16(bytes, 4, 7);
    directory(bytes, 0, "head", 128, 54);
    directory(bytes, 1, "maxp", 184, 6);
    directory(bytes, 2, "hhea", 192, 36);
    directory(bytes, 3, "hmtx", 228, 12);
    directory(bytes, 4, "loca", 240, 16);
    directory(bytes, 5, "glyf", 256, 52);
    directory(bytes, 6, "cmap", 312, 44);
    put16(bytes, 128 + 18, 1000); /* units per em */
    put16(bytes, 128 + 50, 1); /* 32-bit loca */
    put16(bytes, 184 + 4, 3); /* numGlyphs */
    put16(bytes, 192 + 4, 800); /* ascender */
    put16(bytes, 192 + 6, (unsigned)(uint16_t)-200); /* descender */
    put16(bytes, 192 + 34, 3); /* horizontal metric count */
    put16(bytes, 228, 500);
    put16(bytes, 232, 600);
    put16(bytes, 236, 600);
    put32(bytes, 240, 0);
    put32(bytes, 244, 0);
    put32(bytes, 248, 34);
    put32(bytes, 252, 52);

    put16(bytes, 256, 1); /* one simple contour */
    put16(bytes, 256 + 6, 500); /* xMax */
    put16(bytes, 256 + 8, 700); /* yMax */
    put16(bytes, 256 + 10, 3); /* final point */
    memset(bytes + 256 + 14, 1, 4); /* all on-curve */
    put16(bytes, 256 + 20, 500);
    put16(bytes, 256 + 24, (unsigned)(uint16_t)-500);
    put16(bytes, 256 + 30, 700);

    put16(bytes, 290, 0xffff); /* one compound component */
    put16(bytes, 290 + 6, 500);
    put16(bytes, 290 + 8, 700);
    put16(bytes, 290 + 10, 3); /* words and xy offsets */
    put16(bytes, 290 + 12, 1); /* reference A */

    put16(bytes, 312 + 2, 1); /* one cmap encoding */
    put16(bytes, 312 + 4, 3); /* Windows Unicode BMP */
    put16(bytes, 312 + 6, 1);
    put32(bytes, 312 + 8, 12);
    size_t map = 324;
    put16(bytes, map, 4);
    put16(bytes, map + 2, 32);
    put16(bytes, map + 6, 4); /* two segments */
    put16(bytes, map + 8, 4);
    put16(bytes, map + 10, 1);
    put16(bytes, map + 14, 66); /* A and B */
    put16(bytes, map + 16, 0xffff);
    put16(bytes, map + 20, 65);
    put16(bytes, map + 22, 0xffff);
    put16(bytes, map + 24, (unsigned)(uint16_t)-64);
    put16(bytes, map + 26, 1);
}

int main(void) {
    uint8_t bytes[356];
    make_font(bytes);
    assert(!wm_outline_font_decode(bytes, sizeof(bytes) - 1, 0));
    assert(!wm_outline_font_decode(bytes, sizeof(bytes), 1));
    WmOutlineFont *font = wm_outline_font_decode(bytes, sizeof(bytes), 0);
    assert(font);
    WmOutlineBitmap first = {0}, compound = {0};
    assert(wm_outline_font_raster(font, 'A', 20, &first));
    assert(wm_outline_font_raster(font, 'B', 20, &compound));
    assert(first.width == 10 && first.height == 14 && first.left == 0 &&
           first.top == 14 && fabsf(first.advance - 12.0f) < 0.001f);
    assert(compound.width == first.width &&
           compound.height == first.height);
    assert(memcmp(first.alpha, compound.alpha,
                  (size_t)first.width * first.height) == 0);
    for (size_t index = 0; index < (size_t)first.width * first.height;
         index++) assert(first.alpha[index] == 255);
    assert(fabsf(wm_outline_font_text_width(font, "AB", 20) - 24.0f)
           < 0.001f);
    wm_outline_bitmap_free(&first);
    wm_outline_bitmap_free(&compound);

    struct WmPlatform platform = {1};
    assert(wm_outline_font_draw_line(font, &platform, "AB", 20, 10, 20,
                                     WM_FONT_ALIGN_LEFT, 1, 0,
                                     (WmColor){1, 1, 1, 1}, false));
    assert(texture_creates == 1 && draws == 2);
    assert(wm_outline_font_draw_line(font, &platform, "A", 20, 10, 20,
                                     WM_FONT_ALIGN_LEFT, 1, 0,
                                     (WmColor){1, 1, 1, 1}, false));
    assert(texture_creates == 1 && draws == 3);
    wm_outline_font_destroy(font, &platform);
    assert(texture_destroys == 1);

    put32(bytes, 12 + 5 * 16 + 8, 350); /* glyf outside the input */
    assert(!wm_outline_font_decode(bytes, sizeof(bytes), 0));
    make_font(bytes);
    uint8_t collection[376] = {0};
    memcpy(collection, "ttcf", 4);
    put32(collection, 4, 0x00010000);
    put32(collection, 8, 2);
    put32(collection, 12, 20);
    put32(collection, 16, 20);
    memcpy(collection + 20, bytes, sizeof(bytes));
    for (unsigned table = 0; table < 7; table++) {
        size_t offset = 20 + 12 + (size_t)table * 16 + 8;
        put32(collection, offset, 20 + get32(collection, offset));
    }
    WmOutlineFont *second = wm_outline_font_decode(collection,
                                                   sizeof(collection), 1);
    assert(second);
    wm_outline_font_destroy(second, NULL);
    put32(collection, 16, 376); /* face 1 points past the TTC */
    assert(!wm_outline_font_decode(collection, sizeof(collection), 1));
    put32(collection, 16, 20);
    put32(collection, 20 + 12 + 6 * 16 + 8, 375); /* truncated cmap */
    assert(!wm_outline_font_decode(collection, sizeof(collection), 1));
    const char *local_font = getenv("WM_OUTLINE_TEST_FONT");
    if (local_font && local_font[0]) {
        WmOutlineFont *original = wm_outline_font_load(local_font, 1);
        assert(original);
        unsigned inked_glyphs = 0;
        for (uint32_t codepoint = 32; codepoint < 256; codepoint++) {
            WmOutlineBitmap glyph = {0};
            assert(wm_outline_font_raster(original, codepoint, 24, &glyph));
            for (size_t pixel = 0; pixel < (size_t)glyph.width *
                                       glyph.height; pixel++) {
                if (glyph.alpha[pixel]) {
                    inked_glyphs++;
                    break;
                }
            }
            wm_outline_bitmap_free(&glyph);
        }
        assert(inked_glyphs > 150);
        assert(wm_outline_font_text_width(original,
                                          "Wii System Settings 1", 24)
               > 150.0f);
        assert(wm_outline_font_draw_line(original, &platform,
                                         "Wii System Settings 1", 24,
                                         32, 31, WM_FONT_ALIGN_LEFT, 1, 0,
                                         (WmColor){0.2f, 0.2f, 0.2f, 1},
                                         true));
        wm_outline_font_destroy(original, &platform);
    }
    puts("Outline SFNT parsing, simple/compound raster, and atlas reuse passed.");
    return 0;
}
