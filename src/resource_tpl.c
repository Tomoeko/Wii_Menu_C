#include "wii_menu/resource_tpl.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

enum {
    WM_TPL_MAX_IMAGES = 4096,
    WM_TPL_MAX_RGBA_BYTES = 256 * 1024 * 1024
};

typedef struct WmTplShape {
    int width;
    int height;
    int bytes;
} WmTplShape;

static uint16_t wm_read_be16(const uint8_t *bytes)
{
    return (uint16_t)(((uint16_t)bytes[0] << 8) | bytes[1]);
}

static uint32_t wm_read_be32(const uint8_t *bytes)
{
    return ((uint32_t)bytes[0] << 24) | ((uint32_t)bytes[1] << 16) |
           ((uint32_t)bytes[2] << 8) | bytes[3];
}

static bool wm_range_fits(size_t size, size_t offset, size_t length)
{
    return offset <= size && length <= size - offset;
}

static void wm_error(char *error, size_t error_size, const char *message)
{
    if (error != NULL && error_size != 0) {
        snprintf(error, error_size, "%s", message);
    }
}

static void wm_rgb565(uint16_t value, uint8_t color[4])
{
    unsigned red = value >> 11;
    unsigned green = (value >> 5) & 63u;
    unsigned blue = value & 31u;
    color[0] = (uint8_t)((red << 3) | (red >> 2));
    color[1] = (uint8_t)((green << 2) | (green >> 4));
    color[2] = (uint8_t)((blue << 3) | (blue >> 2));
    color[3] = 255;
}

static void wm_rgb5a3(uint16_t value, uint8_t color[4])
{
    if ((value & 0x8000u) != 0) {
        unsigned red = (value >> 10) & 31u;
        unsigned green = (value >> 5) & 31u;
        unsigned blue = value & 31u;
        color[0] = (uint8_t)((red << 3) | (red >> 2));
        color[1] = (uint8_t)((green << 3) | (green >> 2));
        color[2] = (uint8_t)((blue << 3) | (blue >> 2));
        color[3] = 255;
    } else {
        unsigned alpha = (value >> 12) & 7u;
        color[0] = (uint8_t)(((value >> 8) & 15u) * 17u);
        color[1] = (uint8_t)(((value >> 4) & 15u) * 17u);
        color[2] = (uint8_t)((value & 15u) * 17u);
        color[3] = (uint8_t)((alpha << 5) | (alpha << 2) | (alpha >> 1));
    }
}

static bool wm_shape(uint32_t format, WmTplShape *shape)
{
    switch (format) {
    case 0: case 8: case 14:
        *shape = (WmTplShape){8, 8, 32};
        return true;
    case 1: case 2: case 9:
        *shape = (WmTplShape){8, 4, 32};
        return true;
    case 3: case 4: case 5: case 10:
        *shape = (WmTplShape){4, 4, 32};
        return true;
    case 6:
        *shape = (WmTplShape){4, 4, 64};
        return true;
    default:
        return false;
    }
}

static bool wm_read_palette(const uint8_t *data, size_t size, size_t header_offset,
                            uint8_t **palette, size_t *palette_count,
                            char *error, size_t error_size)
{
    if (!wm_range_fits(size, header_offset, 12)) {
        wm_error(error, error_size, "Truncated TPL palette header.");
        return false;
    }
    size_t count = wm_read_be16(data + header_offset);
    uint32_t format = wm_read_be32(data + header_offset + 4);
    size_t offset = wm_read_be32(data + header_offset + 8);
    if (count == 0 || format > 2 || !wm_range_fits(size, offset, count * 2)) {
        wm_error(error, error_size, "Invalid TPL palette.");
        return false;
    }

    uint8_t *colors = malloc(count * 4);
    if (colors == NULL) {
        wm_error(error, error_size, "Out of memory decoding TPL palette.");
        return false;
    }
    for (size_t index = 0; index < count; index++) {
        uint16_t value = wm_read_be16(data + offset + index * 2);
        uint8_t *color = colors + index * 4;
        if (format == 0) {
            color[0] = (uint8_t)value;
            color[1] = (uint8_t)value;
            color[2] = (uint8_t)value;
            color[3] = (uint8_t)(value >> 8);
        } else if (format == 1) {
            wm_rgb565(value, color);
        } else {
            wm_rgb5a3(value, color);
        }
    }
    *palette = colors;
    *palette_count = count;
    return true;
}

static bool wm_decode_pixel(const uint8_t *tile, uint32_t format,
                            int x, int y, int tile_width,
                            const uint8_t *palette, size_t palette_count,
                            uint8_t color[4])
{
    int index = y * tile_width + x;
    uint32_t value;
    size_t palette_index = 0;

    switch (format) {
    case 0:
    case 8:
        value = (tile[index / 2] >> ((index & 1) != 0 ? 0 : 4)) & 15u;
        if (format == 8) {
            palette_index = value;
            break;
        }
        memset(color, (int)(value * 17u), 4);
        return true;
    case 1:
        memset(color, tile[index], 4);
        return true;
    case 2:
        value = tile[index];
        color[0] = (uint8_t)((value & 15u) * 17u);
        color[1] = color[0];
        color[2] = color[0];
        color[3] = (uint8_t)((value >> 4) * 17u);
        return true;
    case 3:
        value = wm_read_be16(tile + index * 2);
        color[0] = (uint8_t)value;
        color[1] = color[0];
        color[2] = color[0];
        color[3] = (uint8_t)(value >> 8);
        return true;
    case 4:
        wm_rgb565(wm_read_be16(tile + index * 2), color);
        return true;
    case 5:
        wm_rgb5a3(wm_read_be16(tile + index * 2), color);
        return true;
    case 6:
        color[0] = tile[index * 2 + 1];
        color[1] = tile[32 + index * 2];
        color[2] = tile[33 + index * 2];
        color[3] = tile[index * 2];
        return true;
    case 9:
        palette_index = tile[index];
        break;
    case 10:
        palette_index = wm_read_be16(tile + index * 2) & 0x3fffu;
        break;
    case 14: {
        size_t subblock = (size_t)((y / 4 * 2 + x / 4) * 8);
        uint16_t c0 = wm_read_be16(tile + subblock);
        uint16_t c1 = wm_read_be16(tile + subblock + 2);
        uint8_t first[4];
        uint8_t second[4];
        wm_rgb565(c0, first);
        wm_rgb565(c1, second);
        unsigned selector = (tile[subblock + 4 + (size_t)(y % 4)] >>
                             (6 - 2 * (x % 4))) & 3u;
        if (selector == 0 || selector == 1) {
            memcpy(color, selector == 0 ? first : second, 4);
            return true;
        }
        for (int channel = 0; channel < 3; channel++) {
            color[channel] = (uint8_t)(c0 > c1 ?
                (selector == 2 ?
                    (5u * first[channel] + 3u * second[channel]) >> 3 :
                    (3u * first[channel] + 5u * second[channel]) >> 3) :
                (first[channel] + second[channel]) / 2u);
        }
        color[3] = c0 <= c1 && selector == 3 ? 0 : 255;
        return true;
    }
    default:
        return false;
    }

    if (palette == NULL || palette_index >= palette_count) {
        return false;
    }
    memcpy(color, palette + palette_index * 4, 4);
    return true;
}

static bool wm_decode_image(const uint8_t *data, size_t size,
                            size_t image_header, size_t palette_header,
                            WmTplImage *image, char *error, size_t error_size)
{
    if (!wm_range_fits(size, image_header, 12)) {
        wm_error(error, error_size, "Truncated TPL image header.");
        return false;
    }
    uint16_t height = wm_read_be16(data + image_header);
    uint16_t width = wm_read_be16(data + image_header + 2);
    uint32_t format = wm_read_be32(data + image_header + 4);
    size_t offset = wm_read_be32(data + image_header + 8);
    WmTplShape shape;
    if (height == 0 || width == 0 || !wm_shape(format, &shape)) {
        wm_error(error, error_size, "Unsupported TPL image dimensions or format.");
        return false;
    }

    size_t tiles_x = ((size_t)width + (size_t)shape.width - 1) / (size_t)shape.width;
    size_t tiles_y = ((size_t)height + (size_t)shape.height - 1) / (size_t)shape.height;
    size_t max_tiles = SIZE_MAX / (size_t)shape.bytes;
    if (tiles_x > max_tiles / tiles_y ||
        !wm_range_fits(size, offset, tiles_x * tiles_y * (size_t)shape.bytes) ||
        (size_t)width > WM_TPL_MAX_RGBA_BYTES / 4 / (size_t)height) {
        wm_error(error, error_size, "TPL image data is truncated or too large.");
        return false;
    }

    uint8_t *palette = NULL;
    size_t palette_count = 0;
    if (format == 8 || format == 9 || format == 10) {
        if (palette_header == 0 ||
            !wm_read_palette(data, size, palette_header,
                             &palette, &palette_count, error, error_size)) {
            if (palette_header == 0) {
                wm_error(error, error_size, "Indexed TPL image has no palette.");
            }
            return false;
        }
    }

    uint8_t *rgba = malloc((size_t)width * height * 4);
    if (rgba == NULL) {
        wm_error(error, error_size, "Out of memory decoding TPL image.");
        free(palette);
        return false;
    }

    bool valid = true;
    size_t tile_index = 0;
    for (size_t by = 0; by < height && valid; by += (size_t)shape.height) {
        for (size_t bx = 0; bx < width && valid; bx += (size_t)shape.width) {
            const uint8_t *tile = data + offset + tile_index * (size_t)shape.bytes;
            tile_index++;
            for (int y = 0; y < shape.height && valid; y++) {
                for (int x = 0; x < shape.width; x++) {
                    if (bx + (size_t)x >= width || by + (size_t)y >= height) {
                        continue;
                    }
                    uint8_t color[4];
                    if (!wm_decode_pixel(tile, format, x, y, shape.width,
                                         palette, palette_count, color)) {
                        wm_error(error, error_size, "Invalid TPL palette index.");
                        valid = false;
                        break;
                    }
                    size_t pixel_offset = ((by + (size_t)y) * width +
                                           bx + (size_t)x) * 4;
                    memcpy(rgba + pixel_offset, color, 4);
                }
            }
        }
    }
    free(palette);
    if (!valid) {
        free(rgba);
        return false;
    }
    *image = (WmTplImage){width, height, format, rgba};
    return true;
}

void wm_tpl_free(WmTpl *tpl)
{
    if (tpl == NULL) {
        return;
    }
    for (size_t index = 0; index < tpl->count; index++) {
        free(tpl->images[index].rgba);
    }
    free(tpl->images);
    *tpl = (WmTpl){0};
}

bool wm_tpl_decode(const uint8_t *data, size_t size, WmTpl *tpl,
                   char *error, size_t error_size)
{
    if (data == NULL || tpl == NULL || size < 12 ||
        wm_read_be32(data) != 0x0020af30u) {
        wm_error(error, error_size, "Expected a TPL texture archive.");
        return false;
    }
    *tpl = (WmTpl){0};

    size_t count = wm_read_be32(data + 4);
    size_t table = wm_read_be32(data + 8);
    if (count == 0 || count > WM_TPL_MAX_IMAGES ||
        !wm_range_fits(size, table, count * 8)) {
        wm_error(error, error_size, "Invalid TPL texture table.");
        return false;
    }

    tpl->images = calloc(count, sizeof(*tpl->images));
    if (tpl->images == NULL) {
        wm_error(error, error_size, "Out of memory decoding TPL archive.");
        return false;
    }
    tpl->count = count;
    for (size_t index = 0; index < count; index++) {
        size_t image_header = wm_read_be32(data + table + index * 8);
        size_t palette_header = wm_read_be32(data + table + index * 8 + 4);
        if (!wm_decode_image(data, size, image_header, palette_header,
                             &tpl->images[index], error, error_size)) {
            wm_tpl_free(tpl);
            return false;
        }
    }
    return true;
}
