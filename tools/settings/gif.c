#include "gif.h"

#include <stdlib.h>
#include <string.h>

enum {
    GIF_DICTIONARY_SIZE = 4096,
    GIF_MAX_DIMENSION = 2048,
    GIF_MAX_COMPRESSED_BYTES = 16 * 1024 * 1024
};

typedef struct GifReader {
    const uint8_t *data;
    size_t size;
    size_t offset;
} GifReader;

typedef struct GifBits {
    const uint8_t *data;
    size_t size;
    size_t bit;
} GifBits;

static bool take(GifReader *reader, size_t count, const uint8_t **value) {
    if (count > reader->size - reader->offset) return false;
    *value = reader->data + reader->offset;
    reader->offset += count;
    return true;
}

static bool byte(GifReader *reader, uint8_t *value) {
    const uint8_t *source;
    if (!take(reader, 1, &source)) return false;
    *value = source[0];
    return true;
}

static unsigned little_u16(const uint8_t *source) {
    return (unsigned)source[0] | ((unsigned)source[1] << 8);
}

static bool skip_blocks(GifReader *reader) {
    for (;;) {
        uint8_t length;
        const uint8_t *ignored;
        if (!byte(reader, &length)) return false;
        if (!length) return true;
        if (!take(reader, length, &ignored)) return false;
    }
}

static bool collect_blocks(GifReader *reader, uint8_t **bytes,
                           size_t *length) {
    uint8_t *output = NULL;
    size_t used = 0;
    for (;;) {
        uint8_t block_size;
        const uint8_t *block;
        if (!byte(reader, &block_size)) break;
        if (!block_size) {
            *bytes = output;
            *length = used;
            return true;
        }
        if (!take(reader, block_size, &block) ||
            used > GIF_MAX_COMPRESSED_BYTES - block_size) break;
        uint8_t *grown = realloc(output, used + block_size);
        if (!grown) break;
        output = grown;
        memcpy(output + used, block, block_size);
        used += block_size;
    }
    free(output);
    return false;
}

static bool read_code(GifBits *bits, unsigned width, unsigned *code) {
    if (width > 12 || bits->bit > bits->size * 8 ||
        width > bits->size * 8 - bits->bit) return false;
    unsigned result = 0;
    for (unsigned index = 0; index < width; index++) {
        size_t position = bits->bit++;
        result |= (unsigned)((bits->data[position / 8] >>
                              (position % 8)) & 1u) << index;
    }
    *code = result;
    return true;
}

static bool decode_indices(const uint8_t *compressed, size_t size,
                           unsigned minimum_width, uint8_t *indices,
                           size_t count) {
    if (minimum_width < 2 || minimum_width > 8) return false;
    unsigned prefix[GIF_DICTIONARY_SIZE] = {0};
    uint8_t suffix[GIF_DICTIONARY_SIZE] = {0};
    uint8_t stack[GIF_DICTIONARY_SIZE];
    GifBits bits = {.data = compressed, .size = size};
    unsigned clear = 1u << minimum_width;
    unsigned finish = clear + 1;
    unsigned available = finish + 1;
    unsigned width = minimum_width + 1;
    int previous = -1;
    uint8_t first = 0;
    size_t produced = 0;
    bool ended = false;

    unsigned code;
    while (read_code(&bits, width, &code)) {
        if (code == clear) {
            available = finish + 1;
            width = minimum_width + 1;
            previous = -1;
            continue;
        }
        if (code == finish) {
            ended = true;
            break;
        }
        if (previous < 0) {
            if (code >= clear || produced >= count) return false;
            first = (uint8_t)code;
            indices[produced++] = first;
            previous = (int)code;
            continue;
        }
        unsigned original = code;
        size_t stacked = 0;
        if (code == available) {
            stack[stacked++] = first;
            code = (unsigned)previous;
        } else if (code > available) {
            return false;
        }
        while (code >= clear) {
            if (code >= available || stacked >= GIF_DICTIONARY_SIZE)
                return false;
            stack[stacked++] = suffix[code];
            code = prefix[code];
        }
        if (code >= clear || stacked >= GIF_DICTIONARY_SIZE)
            return false;
        first = (uint8_t)code;
        stack[stacked++] = first;
        if (stacked > count - produced) return false;
        while (stacked) indices[produced++] = stack[--stacked];

        if (available < GIF_DICTIONARY_SIZE) {
            prefix[available] = (unsigned)previous;
            suffix[available] = first;
            available++;
            if (available == (1u << width) && width < 12) width++;
        }
        previous = (int)original;
    }
    return ended && produced == count;
}

static bool image_pixels(const uint8_t *indices, unsigned left,
                         unsigned top, unsigned width, unsigned height,
                         bool interlaced, const uint8_t *palette,
                         unsigned colors, int transparent, WmImage *image) {
    static const unsigned starts[4] = {0, 4, 2, 1};
    static const unsigned steps[4] = {8, 8, 4, 2};
    unsigned source_row = 0;
    unsigned passes = interlaced ? 4 : 1;
    for (unsigned pass = 0; pass < passes; pass++) {
        unsigned first_row = interlaced ? starts[pass] : 0;
        unsigned step = interlaced ? steps[pass] : 1;
        for (unsigned y = first_row; y < height; y += step) {
            for (unsigned x = 0; x < width; x++) {
                unsigned index = indices[(size_t)source_row * width + x];
                if (index >= colors) return false;
                size_t output = ((size_t)(top + y) * image->width +
                                 left + x) * 4;
                image->pixels[output] = palette[index * 3];
                image->pixels[output + 1] = palette[index * 3 + 1];
                image->pixels[output + 2] = palette[index * 3 + 2];
                image->pixels[output + 3] = index == (unsigned)transparent
                    ? 0 : 255;
            }
            source_row++;
        }
    }
    return source_row == height;
}

bool wm_settings_gif_decode(const uint8_t *data, size_t size, WmImage *image) {
    if (!data || !image) return false;
    memset(image, 0, sizeof(*image));
    GifReader reader = {.data = data, .size = size};
    const uint8_t *header;
    if (!take(&reader, 13, &header) ||
        (memcmp(header, "GIF87a", 6) != 0 &&
         memcmp(header, "GIF89a", 6) != 0)) return false;
    unsigned canvas_width = little_u16(header + 6);
    unsigned canvas_height = little_u16(header + 8);
    if (!canvas_width || !canvas_height ||
        canvas_width > GIF_MAX_DIMENSION ||
        canvas_height > GIF_MAX_DIMENSION) return false;
    unsigned global_colors = header[10] & 0x80u
        ? 1u << ((header[10] & 7u) + 1u) : 0;
    const uint8_t *global_palette = NULL;
    if (global_colors &&
        !take(&reader, global_colors * 3, &global_palette)) return false;
    int transparent = -1;

    for (;;) {
        uint8_t marker;
        if (!byte(&reader, &marker) || marker == 0x3b) return false;
        if (marker == 0x21) {
            uint8_t kind;
            if (!byte(&reader, &kind)) return false;
            if (kind == 0xf9) {
                const uint8_t *control;
                uint8_t terminator;
                if (!take(&reader, 5, &control) || control[0] != 4 ||
                    !byte(&reader, &terminator) || terminator != 0)
                    return false;
                transparent = control[1] & 1 ? control[4] : -1;
            } else if (!skip_blocks(&reader)) {
                return false;
            }
            continue;
        }
        if (marker != 0x2c) return false;
        const uint8_t *descriptor;
        if (!take(&reader, 9, &descriptor)) return false;
        unsigned left = little_u16(descriptor);
        unsigned top = little_u16(descriptor + 2);
        unsigned width = little_u16(descriptor + 4);
        unsigned height = little_u16(descriptor + 6);
        if (!width || !height || left > canvas_width - width ||
            top > canvas_height - height || width > canvas_width ||
            height > canvas_height) return false;
        unsigned local_colors = descriptor[8] & 0x80u
            ? 1u << ((descriptor[8] & 7u) + 1u) : 0;
        const uint8_t *palette = global_palette;
        unsigned colors = global_colors;
        if (local_colors) {
            if (!take(&reader, local_colors * 3, &palette)) return false;
            colors = local_colors;
        }
        if (!palette) return false;
        uint8_t minimum_width;
        uint8_t *compressed = NULL;
        size_t compressed_size = 0;
        bool valid = byte(&reader, &minimum_width) &&
                     collect_blocks(&reader, &compressed, &compressed_size);
        if (!valid) return false;
        size_t count = (size_t)width * height;
        uint8_t *indices = malloc(count);
        if (!indices) {
            free(compressed);
            return false;
        }
        valid = decode_indices(compressed, compressed_size, minimum_width,
                               indices, count);
        free(compressed);
        if (valid) {
            image->width = canvas_width;
            image->height = canvas_height;
            image->pixels = calloc((size_t)canvas_width * canvas_height, 4);
            valid = image->pixels && image_pixels(
                indices, left, top, width, height,
                (descriptor[8] & 0x40u) != 0,
                palette, colors, transparent, image);
        }
        /* This exporter accepts one static frame and its trailer. A truncated
         * stream must not silently become a valid local texture. */
        valid = valid && reader.offset + 1 == reader.size &&
                reader.data[reader.offset] == 0x3b;
        free(indices);
        if (!valid) wm_image_free(image);
        return valid;
    }
}
