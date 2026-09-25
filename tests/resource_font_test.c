#include "wii_menu/resource_font.h"

#include <assert.h>
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

enum {
    TEST_FONT_SIZE = 153,
    FINF = 16,
    TGLP = 48,
    CWDH = 112,
    CMAP = 131
};

typedef struct CapturedFont {
    int count;
    float first_left;
    float first_top;
    float first_alpha;
    float first_bottom_alpha;
    uint32_t texture;
} CapturedFont;

static void write_be16(uint8_t *target, uint16_t value) {
    target[0] = (uint8_t)(value >> 8);
    target[1] = (uint8_t)value;
}

static void write_be32(uint8_t *target, uint32_t value) {
    target[0] = (uint8_t)(value >> 24);
    target[1] = (uint8_t)(value >> 16);
    target[2] = (uint8_t)(value >> 8);
    target[3] = (uint8_t)value;
}

static void make_font(uint8_t bytes[TEST_FONT_SIZE]) {
    memset(bytes, 0, TEST_FONT_SIZE);
    memcpy(bytes, "RFNT", 4);
    bytes[4] = 0xfe;
    bytes[5] = 0xff;
    write_be32(bytes + 8, TEST_FONT_SIZE);
    write_be16(bytes + 12, 16);
    write_be16(bytes + 14, 4);

    memcpy(bytes + FINF, "FINF", 4);
    write_be32(bytes + FINF + 4, 32);
    bytes[FINF + 9] = 4;
    bytes[FINF + 15] = 1;
    write_be32(bytes + FINF + 16, TGLP + 8);
    write_be32(bytes + FINF + 20, CWDH + 8);
    write_be32(bytes + FINF + 24, CMAP + 8);
    bytes[FINF + 28] = 4;
    bytes[FINF + 29] = 4;
    bytes[FINF + 30] = 3;

    memcpy(bytes + TGLP, "TGLP", 4);
    write_be32(bytes + TGLP + 4, 64);
    bytes[TGLP + 8] = 3;
    bytes[TGLP + 9] = 3;
    bytes[TGLP + 10] = 2;
    bytes[TGLP + 11] = 3;
    write_be32(bytes + TGLP + 12, 32);
    write_be16(bytes + TGLP + 16, 1);
    write_be16(bytes + TGLP + 18, 0);
    write_be16(bytes + TGLP + 20, 1);
    write_be16(bytes + TGLP + 22, 1);
    write_be16(bytes + TGLP + 24, 8);
    write_be16(bytes + TGLP + 26, 8);
    write_be32(bytes + TGLP + 28, TGLP + 32);
    memset(bytes + TGLP + 32, 0xff, 32);

    memcpy(bytes + CWDH, "CWDH", 4);
    write_be32(bytes + CWDH + 4, 19);
    bytes[CWDH + 16] = 1;
    bytes[CWDH + 17] = 2;
    bytes[CWDH + 18] = 3;

    memcpy(bytes + CMAP, "CMAP", 4);
    write_be32(bytes + CMAP + 4, 22);
    write_be16(bytes + CMAP + 8, 'A');
    write_be16(bytes + CMAP + 10, 'A');
    write_be16(bytes + CMAP + 12, 0);
}

static bool near(float actual, float expected) {
    return fabsf(actual - expected) < 0.0001f;
}

static bool sheet_provider(void *context, size_t sheet, uint32_t *texture) {
    (void)context;
    assert(sheet == 0);
    *texture = 42;
    return true;
}

static void capture_quad(void *context, const WmFontQuad *quad) {
    CapturedFont *capture = context;
    if (!capture->count) {
        capture->first_left = quad->vertices[0].position[0];
        capture->first_top = quad->vertices[0].position[1];
        capture->first_alpha = quad->vertices[0].color[3];
        capture->first_bottom_alpha = quad->vertices[2].color[3];
        capture->texture = quad->texture;
    }
    capture->count++;
    assert(quad->glyph_alpha_only);
    assert(quad->format == 0);
}

int main(void) {
    uint8_t bytes[TEST_FONT_SIZE];
    make_font(bytes);
    char error[128];
    WmFont *font = wm_font_decode(bytes, sizeof(bytes), error, sizeof(error));
    if (!font) {
        fprintf(stderr, "font fixture failed: %s\n", error);
        return 1;
    }
    const WmFontMetrics *metrics = wm_font_metrics(font);
    assert(metrics->height == 4 && metrics->width == 4);
    assert(metrics->baseline == 2 && metrics->ascent == 3);
    assert(wm_font_sheet_count(font) == 1);
    const WmFontGlyph *glyph = wm_font_glyph(font, 'A');
    assert(glyph && glyph->x == 1 && glyph->y == 1);
    assert(glyph->width == 2 && glyph->height == 3);
    assert(glyph->left == 1 && glyph->advance == 3);
    assert(wm_font_glyph(font, 'B') == glyph);

    WmImage image;
    assert(wm_font_decode_sheet(font, 0, &image, error, sizeof(error)));
    assert(image.width == 8 && image.height == 8);
    for (size_t index = 0; index < 8 * 8 * 4; index++) {
        assert(image.pixels[index] == 255);
    }
    wm_image_free(&image);

    const float size[2] = {4, 4};
    assert(near(wm_font_text_width(font, "AA", size, 1), 7));
    CapturedFont line = {0};
    WmFontDrawOptions options = {
        .x = 10,
        .y = 20,
        .size = {4, 4},
        .spacing = 1,
        .alpha = 0.5f,
        .top_color = {100, 110, 120, 255},
        .bottom_color = {200, 210, 220, 128},
        .align = WM_FONT_ALIGN_CENTER,
        .sheet_provider = sheet_provider,
        .on_quad = capture_quad,
        .context = &line
    };
    wm_font_emit_line(font, "AA", &options);
    assert(line.count == 2 && line.texture == 42);
    assert(near(line.first_left, 7.5f) && near(line.first_top, 20));
    assert(near(line.first_alpha, 0.5f));
    assert(near(line.first_bottom_alpha, 128.0f / 255.0f * 0.5f));

    WmFontPane pane = {
        .size = {10, 12},
        .origin = 4,
        .text_position = 4,
        .font_size = {4, 4},
        .char_space = 1,
        .top_color = {255, 255, 255, 255},
        .bottom_color = {255, 255, 255, 255}
    };
    WmFontTextLayout *layout = wm_font_layout_pane(font, "AAA", &pane);
    assert(layout);
    CapturedFont wrapped = {0};
    wm_font_emit_pane(layout, NULL, 1, sheet_provider, capture_quad, &wrapped);
    assert(wrapped.count == 3);
    assert(near(wrapped.first_left, -2.5f) && near(wrapped.first_top, 3));
    wm_font_text_layout_destroy(layout);
    wm_font_destroy(font);

    make_font(bytes);
    write_be32(bytes + CWDH + 12, CWDH + 8);
    assert(!wm_font_decode(bytes, sizeof(bytes), error, sizeof(error)));

    make_font(bytes);
    write_be16(bytes + TGLP + 18, 0x8000);
    write_be32(bytes + TGLP + 32, 28);
    font = wm_font_decode(bytes, sizeof(bytes), error, sizeof(error));
    assert(font);
    assert(!wm_font_decode_sheet(font, 0, &image, error, sizeof(error)));
    wm_font_destroy(font);

    make_font(bytes);
    write_be16(bytes + TGLP + 18, 0x8000);
    write_be32(bytes + TGLP + 32, 12);
    const uint8_t huffman8[12] = {
        0x28, 32, 0, 0, 1, 0xc0, 0xff, 0xff, 0, 0, 0, 0
    };
    memcpy(bytes + TGLP + 36, huffman8, sizeof(huffman8));
    font = wm_font_decode(bytes, sizeof(bytes), error, sizeof(error));
    assert(font);
    assert(wm_font_decode_sheet(font, 0, &image, error, sizeof(error)));
    assert(image.width == 8 && image.height == 8);
    assert(image.pixels[0] == 255 && image.pixels[8 * 8 * 4 - 1] == 255);
    wm_image_free(&image);
    wm_font_destroy(font);

    make_font(bytes);
    write_be16(bytes + TGLP + 18, 0x8000);
    write_be32(bytes + TGLP + 32, 16);
    const uint8_t huffman4[16] = {
        0x24, 32, 0, 0, 1, 0xc0, 0x0f, 0x0f,
        0, 0, 0, 0, 0, 0, 0, 0
    };
    memcpy(bytes + TGLP + 36, huffman4, sizeof(huffman4));
    font = wm_font_decode(bytes, sizeof(bytes), error, sizeof(error));
    assert(font);
    assert(wm_font_decode_sheet(font, 0, &image, error, sizeof(error)));
    assert(image.pixels[0] == 255 && image.pixels[8 * 8 * 4 - 1] == 255);
    wm_image_free(&image);
    wm_font_destroy(font);
    return 0;
}
