#include "corruption_outline.h"

#include "wii_menu/fonts/outline_font.h"

#include <math.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Rasterize from the prepared Wii outline font at the supplied still's
 * 3840 x 2160 resolution. Scaling a 32-pixel bitmap font to that size makes
 * its texture filtering visible across several output pixels. */
enum {
    REFERENCE_WIDTH = 3840,
    REFERENCE_HEIGHT = 2160,
    LINE_TEXTURE_WIDTH = 2048,
    LINE_TEXTURE_HEIGHT = 128,
    FONT_PIXEL_SIZE = 94,
    LINE_BASELINE = 84,
    FIRST_LINE_TOP = 902,
    LINE_SPACING = 134
};

static bool copy_line(const char **cursor, char line[128]) {
    const char *end = strchr(*cursor, '\n');
    size_t length = end ? (size_t)(end - *cursor) : strlen(*cursor);
    if (length >= 128) return false;
    memcpy(line, *cursor, length);
    line[length] = '\0';
    *cursor = end ? end + 1 : *cursor + length;
    return true;
}

static bool raster_line(const WmOutlineFont *font, const char *line,
                        uint8_t *coverage) {
    float width = wm_outline_font_text_width(font, line, FONT_PIXEL_SIZE);
    if (!isfinite(width) || width > LINE_TEXTURE_WIDTH - 8) return false;
    float pen = (LINE_TEXTURE_WIDTH - width) * 0.5f;
    for (const unsigned char *cursor = (const unsigned char *)line;
         *cursor; cursor++) {
        WmOutlineBitmap glyph;
        if (!wm_outline_font_raster(font, *cursor, FONT_PIXEL_SIZE, &glyph))
            return false;
        int left = (int)lroundf(pen + glyph.left);
        int top = LINE_BASELINE - glyph.top;
        for (unsigned y = 0; y < glyph.height; y++) {
            int output_y = top + (int)y;
            if (output_y < 0 || output_y >= LINE_TEXTURE_HEIGHT) continue;
            for (unsigned x = 0; x < glyph.width; x++) {
                int output_x = left + (int)x;
                if (output_x < 0 || output_x >= LINE_TEXTURE_WIDTH) continue;
                size_t destination = (size_t)output_y * LINE_TEXTURE_WIDTH +
                                     (size_t)output_x;
                uint8_t value = glyph.alpha[(size_t)y * glyph.width + x];
                if (value > coverage[destination]) coverage[destination] = value;
            }
        }
        pen += glyph.advance;
        wm_outline_bitmap_free(&glyph);
    }
    return true;
}

static uint8_t bold_coverage(const uint8_t *coverage, int x, int y) {
    uint8_t maximum = 0;
    for (int offset_y = -1; offset_y <= 1; offset_y++) {
        int source_y = y + offset_y;
        if (source_y < 0 || source_y >= LINE_TEXTURE_HEIGHT) continue;
        /* The reference strokes are slightly heavier than a one-pixel
         * outline expansion. Extend one more source pixel to the left while
         * retaining the original one-pixel vertical antialiasing. */
        for (int offset_x = -1; offset_x <= 2; offset_x++) {
            int source_x = x + offset_x;
            if (source_x < 0 || source_x >= LINE_TEXTURE_WIDTH) continue;
            uint8_t value = coverage[(size_t)source_y * LINE_TEXTURE_WIDTH +
                                     (size_t)source_x];
            if (value > maximum) maximum = value;
        }
    }
    return maximum;
}

static uint32_t create_line_texture(WmPlatform *platform,
                                    const WmOutlineFont *font,
                                    const char *line) {
    size_t pixels = (size_t)LINE_TEXTURE_WIDTH * LINE_TEXTURE_HEIGHT;
    uint8_t *coverage = calloc(pixels, 1);
    uint8_t *rgba = malloc(pixels * 4);
    if (!coverage || !rgba || !raster_line(font, line, coverage)) {
        free(coverage);
        free(rgba);
        return 0;
    }
    for (int y = 0; y < LINE_TEXTURE_HEIGHT; y++) {
        for (int x = 0; x < LINE_TEXTURE_WIDTH; x++) {
            size_t index = (size_t)y * LINE_TEXTURE_WIDTH + (size_t)x;
            rgba[index * 4] = 255;
            rgba[index * 4 + 1] = 255;
            rgba[index * 4 + 2] = 255;
            rgba[index * 4 + 3] = bold_coverage(coverage, x, y);
        }
    }
    uint32_t texture = wm_platform_create_texture(
        platform, LINE_TEXTURE_WIDTH, LINE_TEXTURE_HEIGHT, rgba);
    free(coverage);
    free(rgba);
    return texture;
}

bool wm_corruption_outline_create(WmCorruptionOutline *outline,
                                  WmPlatform *platform,
                                  const char *assets_root,
                                  const char *message) {
    if (!outline || !platform || !assets_root || !message) return false;
    memset(outline, 0, sizeof(*outline));
    char path[4096];
    int length = snprintf(path, sizeof(path), "%s/fonts/settings-latin.ttc",
                          assets_root);
    if (length < 0 || length >= (int)sizeof(path)) return false;
    WmOutlineFont *font = wm_outline_font_load(path, 1);
    if (!font) return false;
    const char *cursor = message;
    bool complete = true;
    for (int row = 0; row < WM_CORRUPTION_OUTLINE_LINES; row++) {
        char line[128];
        if (!copy_line(&cursor, line)) {
            complete = false;
            break;
        }
        outline->textures[row] = create_line_texture(platform, font, line);
        if (!outline->textures[row]) {
            complete = false;
            break;
        }
    }
    wm_outline_font_destroy(font, NULL);
    if (!complete || *cursor) {
        wm_corruption_outline_destroy(outline, platform);
        return false;
    }
    return true;
}

void wm_corruption_outline_draw(const WmCorruptionOutline *outline,
                                WmPlatform *platform) {
    if (!outline || !platform) return;
    const float width = LINE_TEXTURE_WIDTH * 1.06f *
                        WM_FRAME_WIDTH / REFERENCE_WIDTH;
    const float height = LINE_TEXTURE_HEIGHT *
                         WM_FRAME_HEIGHT / REFERENCE_HEIGHT;
    for (int row = 0; row < WM_CORRUPTION_OUTLINE_LINES; row++) {
        WmQuad line = {
            .x = (WM_FRAME_WIDTH - width) * 0.5f,
            .y = (FIRST_LINE_TOP + LINE_SPACING * row) *
                 (float)WM_FRAME_HEIGHT / REFERENCE_HEIGHT,
            .width = width, .height = height,
            .u0 = 0.0f, .v0 = 0.0f, .u1 = 1.0f, .v1 = 1.0f,
            .color = {204.0f / 255.0f, 204.0f / 255.0f,
                      204.0f / 255.0f, 1.0f},
            .texture = outline->textures[row]
        };
        wm_platform_draw_quad(platform, &line);
    }
}

void wm_corruption_outline_destroy(WmCorruptionOutline *outline,
                                   WmPlatform *platform) {
    if (!outline || !platform) return;
    for (int row = 0; row < WM_CORRUPTION_OUTLINE_LINES; row++) {
        if (outline->textures[row])
            wm_platform_destroy_texture(platform, outline->textures[row]);
        outline->textures[row] = 0;
    }
}
