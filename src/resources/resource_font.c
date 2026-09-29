#include "resource_font_internal.h"
#include "resource_bytes.h"
#include "wii_menu/resources/resource_tpl.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

enum {
    WM_FONT_MAX_FILE = 64 * 1024 * 1024,
    WM_FONT_MAX_SHEETS = 256,
    WM_FONT_MAX_CHAINS = 1024
};

static void font_error(char *error, size_t capacity, const char *message) {
    if (error && capacity)
        snprintf(error, capacity, "%s", message);
}

static uint32_t le32(const uint8_t *bytes) {
    return (uint32_t)bytes[0] | ((uint32_t)bytes[1] << 8) | ((uint32_t)bytes[2] << 16) |
           ((uint32_t)bytes[3] << 24);
}

static void write_be16(uint8_t *bytes, uint16_t value) {
    bytes[0] = (uint8_t)(value >> 8);
    bytes[1] = (uint8_t)value;
}

static void write_be32(uint8_t *bytes, uint32_t value) {
    bytes[0] = (uint8_t)(value >> 24);
    bytes[1] = (uint8_t)(value >> 16);
    bytes[2] = (uint8_t)(value >> 8);
    bytes[3] = (uint8_t)value;
}

static bool seen_offset(const size_t *visited, size_t count, size_t offset) {
    for (size_t index = 0; index < count; index++) {
        if (visited[index] == offset)
            return true;
    }
    return false;
}

static bool width_chain(WmFont *font, size_t start, bool fill, size_t *maximum) {
    size_t visited[WM_FONT_MAX_CHAINS];
    size_t count = 0;
    size_t position = start;
    while (position) {
        if (count == WM_FONT_MAX_CHAINS || seen_offset(visited, count, position) ||
            !wm_resource_range_fits(font->size, position, 8))
            return false;
        visited[count++] = position;
        const uint8_t *entry = font->data + position;
        size_t begin = wm_resource_be16(entry), end = wm_resource_be16(entry + 2);
        size_t next = wm_resource_be32(entry + 4);
        if (end < begin ||
            !wm_resource_range_fits(font->size, position + 8, (end - begin + 1) * 3))
            return false;
        if (end > *maximum)
            *maximum = end;
        if (fill) {
            for (size_t index = begin; index <= end; index++) {
                const uint8_t *record = entry + 8 + (index - begin) * 3;
                WmFontGlyph *glyph = &font->glyphs[index];
                glyph->left = (int8_t)record[0];
                glyph->width = record[1];
                glyph->advance = (int8_t)record[2];
                font->glyph_present[index] = 1;
            }
        }
        position = next;
    }
    return true;
}

static bool parse_character_maps(WmFont *font, size_t start) {
    size_t visited[WM_FONT_MAX_CHAINS];
    size_t count = 0;
    size_t position = start;
    while (position) {
        if (count == WM_FONT_MAX_CHAINS || seen_offset(visited, count, position) ||
            !wm_resource_range_fits(font->size, position, 12))
            return false;
        visited[count++] = position;
        const uint8_t *entry = font->data + position;
        uint16_t begin = wm_resource_be16(entry), end = wm_resource_be16(entry + 2);
        uint16_t method = wm_resource_be16(entry + 4);
        size_t next = wm_resource_be32(entry + 8);
        if (end < begin)
            return false;
        size_t range = (size_t)end - begin + 1;
        if (method == 0) {
            if (!wm_resource_range_fits(font->size, position + 12, 2))
                return false;
            size_t first = wm_resource_be16(entry + 12);
            if (first + range > 65536)
                return false;
            for (size_t code = begin; code <= end; code++) {
                font->characters[code] = (uint16_t)(first + code - begin);
            }
        } else if (method == 1) {
            if (!wm_resource_range_fits(font->size, position + 12, range * 2))
                return false;
            for (size_t code = begin; code <= end; code++) {
                uint16_t glyph = wm_resource_be16(entry + 12 + (code - begin) * 2);
                if (glyph != UINT16_MAX)
                    font->characters[code] = glyph;
            }
        } else if (method == 2) {
            if (!wm_resource_range_fits(font->size, position + 12, 2))
                return false;
            size_t entries = wm_resource_be16(entry + 12);
            if (!wm_resource_range_fits(font->size, position + 14, entries * 4))
                return false;
            for (size_t index = 0; index < entries; index++) {
                uint16_t code = wm_resource_be16(entry + 14 + index * 4);
                uint16_t glyph = wm_resource_be16(entry + 16 + index * 4);
                font->characters[code] = glyph;
            }
        } else {
            return false;
        }
        position = next;
    }
    return true;
}

static bool parse_sheets(WmFont *font, size_t image_offset, uint16_t width,
                         uint16_t height, uint16_t format) {
    size_t cursor = image_offset;
    for (size_t index = 0; index < font->sheet_count; index++) {
        FontSheet *sheet = &font->sheets[index];
        sheet->info.width = width;
        sheet->info.height = height;
        sheet->info.format = (uint16_t)(format & 0x7fffu);
        if (font->compressed) {
            if (!wm_resource_range_fits(font->size, cursor, 4))
                return false;
            size_t stored = wm_resource_be32(font->data + cursor);
            cursor += 4;
            if (!stored || !wm_resource_range_fits(font->size, cursor, stored))
                return false;
            sheet->offset = cursor;
            sheet->stored_size = stored;
            cursor += stored;
        } else {
            if (!wm_resource_range_fits(font->size, cursor, font->sheet_size))
                return false;
            sheet->offset = cursor;
            sheet->stored_size = font->sheet_size;
            cursor += font->sheet_size;
        }
    }
    return true;
}

WmFont *wm_font_decode(const uint8_t *data, size_t size, char *error,
                       size_t error_capacity) {
    font_error(error, error_capacity, "");
    if (!data || size < 16 || size > WM_FONT_MAX_FILE ||
        (memcmp(data, "RFNT", 4) != 0 && memcmp(data, "RFNA", 4) != 0) ||
        data[4] != 0xfe || data[5] != 0xff) {
        font_error(error, error_capacity,
                   "Expected bounded big-endian RFNT/RFNA font.");
        return NULL;
    }
    size_t declared = wm_resource_be32(data + 8);
    size_t offset = wm_resource_be16(data + 12);
    size_t section_count = wm_resource_be16(data + 14);
    if (declared > size || offset < 16 || offset >= declared || section_count == 0 ||
        section_count > 1024) {
        font_error(error, error_capacity, "Invalid font section header.");
        return NULL;
    }
    size_t finf = 0, finf_length = 0;
    for (size_t index = 0; index < section_count; index++) {
        if (!wm_resource_range_fits(declared, offset, 8)) {
            font_error(error, error_capacity, "Truncated font section.");
            return NULL;
        }
        size_t length = wm_resource_be32(data + offset + 4);
        if (length < 8 || !wm_resource_range_fits(declared, offset, length)) {
            font_error(error, error_capacity, "Invalid font section size.");
            return NULL;
        }
        if (memcmp(data + offset, "FINF", 4) == 0) {
            finf = offset;
            finf_length = length;
        }
        offset += length;
    }
    if (!finf || finf_length < 31 || !wm_resource_range_fits(declared, finf, 31)) {
        font_error(error, error_capacity, "Font information section is missing.");
        return NULL;
    }

    WmFont *font = calloc(1, sizeof(*font));
    if (!font) {
        font_error(error, error_capacity, "Out of memory.");
        return NULL;
    }
    font->data = malloc(declared);
    if (!font->data)
        goto invalid;
    memcpy(font->data, data, declared);
    font->size = declared;
    const uint8_t *info = font->data + finf;
    font->metrics.line_feed = (int8_t)info[9];
    font->metrics.default_glyph = wm_resource_be16(info + 10);
    font->metrics.encoding = info[15];
    font->metrics.height = info[28];
    font->metrics.width = info[29];
    font->metrics.ascent = info[30];
    size_t glyph_offset = wm_resource_be32(info + 16);
    size_t width_offset = wm_resource_be32(info + 20);
    size_t map_offset = wm_resource_be32(info + 24);
    if (glyph_offset < 8 || width_offset < 8 || map_offset < 8 ||
        !wm_resource_range_fits(declared, glyph_offset, 24) ||
        !wm_resource_range_fits(declared, width_offset, 8) ||
        !wm_resource_range_fits(declared, map_offset, 12) ||
        memcmp(font->data + glyph_offset - 8, "TGLP", 4) != 0 ||
        memcmp(font->data + width_offset - 8, "CWDH", 4) != 0 ||
        memcmp(font->data + map_offset - 8, "CMAP", 4) != 0)
        goto invalid;
    const uint8_t *glyph_info = font->data + glyph_offset;
    font->metrics.cell_width = glyph_info[0];
    font->metrics.cell_height = glyph_info[1];
    font->metrics.baseline = (int8_t)glyph_info[2];
    font->sheet_size = wm_resource_be32(glyph_info + 4);
    font->sheet_count = wm_resource_be16(glyph_info + 8);
    uint16_t format = wm_resource_be16(glyph_info + 10);
    uint16_t columns = wm_resource_be16(glyph_info + 12);
    uint16_t rows = wm_resource_be16(glyph_info + 14);
    uint16_t sheet_width = wm_resource_be16(glyph_info + 16);
    uint16_t sheet_height = wm_resource_be16(glyph_info + 18);
    size_t image_offset = wm_resource_be32(glyph_info + 20);
    font->compressed = (format & 0x8000u) != 0;
    if (!font->metrics.width || !font->metrics.height || !font->sheet_size ||
        font->sheet_size > 16u * 1024u * 1024u || font->sheet_count == 0 ||
        font->sheet_count > WM_FONT_MAX_SHEETS || !columns || !rows || !sheet_width ||
        !sheet_height || sheet_width > 4096 || sheet_height > 4096 ||
        (uint64_t)sheet_width * sheet_height * 4 > 64u * 1024u * 1024u ||
        !wm_resource_range_fits(declared, image_offset, 1))
        goto invalid;
    font->sheets = calloc(font->sheet_count, sizeof(*font->sheets));
    font->characters = malloc(65536u * sizeof(*font->characters));
    if (!font->sheets || !font->characters)
        goto invalid;
    for (size_t code = 0; code < 65536; code++)
        font->characters[code] = UINT16_MAX;
    if (!parse_sheets(font, image_offset, sheet_width, sheet_height, format))
        goto invalid;

    size_t maximum = 0;
    if (!width_chain(font, width_offset, false, &maximum))
        goto invalid;
    font->glyph_count = maximum + 1;
    font->glyphs = calloc(font->glyph_count, sizeof(*font->glyphs));
    font->glyph_present = calloc(font->glyph_count, sizeof(*font->glyph_present));
    if (!font->glyphs || !font->glyph_present ||
        !width_chain(font, width_offset, true, &maximum) ||
        !parse_character_maps(font, map_offset))
        goto invalid;
    size_t cells_per_sheet = (size_t)columns * rows;
    for (size_t index = 0; index < font->glyph_count; index++) {
        if (!font->glyph_present[index])
            continue;
        size_t sheet = index / cells_per_sheet;
        size_t cell = index % cells_per_sheet;
        size_t x = (cell % columns) * ((size_t)font->metrics.cell_width + 1) + 1;
        size_t y = (cell / columns) * ((size_t)font->metrics.cell_height + 1) + 1;
        if (sheet >= font->sheet_count || x + font->glyphs[index].width > sheet_width ||
            y + font->metrics.cell_height > sheet_height)
            goto invalid;
        font->glyphs[index].sheet = (uint16_t)sheet;
        font->glyphs[index].x = (uint16_t)x;
        font->glyphs[index].y = (uint16_t)y;
        font->glyphs[index].height = font->metrics.cell_height;
    }
    return font;

invalid:
    font_error(error, error_capacity, "Invalid or unsupported font resource.");
    wm_font_destroy(font);
    return NULL;
}

void wm_font_destroy(WmFont *font) {
    if (!font)
        return;
    free(font->data);
    free(font->sheets);
    free(font->glyphs);
    free(font->glyph_present);
    free(font->characters);
    free(font);
}

const WmFontMetrics *wm_font_metrics(const WmFont *font) {
    return font ? &font->metrics : NULL;
}

size_t wm_font_sheet_count(const WmFont *font) {
    return font ? font->sheet_count : 0;
}

const WmFontSheetInfo *wm_font_sheet_info(const WmFont *font, size_t sheet) {
    return font && sheet < font->sheet_count ? &font->sheets[sheet].info : NULL;
}

const WmFontGlyph *wm_font_glyph(const WmFont *font, uint32_t codepoint) {
    if (!font)
        return NULL;
    uint16_t index = codepoint < 65536 ? font->characters[codepoint] : UINT16_MAX;
    /* The exported phone-key marker uses a private-use codepoint. Accept
     * both the legacy marker and the U+2423 open box. */
    if (index == UINT16_MAX && (codepoint == 0x23b5 || codepoint == 0x2423))
        index = font->characters[0xe057];
    if (index == UINT16_MAX)
        index = font->metrics.default_glyph;
    return index < font->glyph_count && font->glyph_present[index]
               ? &font->glyphs[index]
               : NULL;
}

static bool decode_huffman(const uint8_t *stream, size_t size, uint8_t *output,
                           size_t output_size) {
    if (size < 5 || (stream[0] != 0x24 && stream[0] != 0x28))
        return false;
    unsigned depth = stream[0] & 15u;
    size_t declared =
        (size_t)stream[1] | ((size_t)stream[2] << 8) | ((size_t)stream[3] << 16);
    size_t table = 4;
    if (!declared) {
        if (size < 9)
            return false;
        declared = le32(stream + 4);
        table = 8;
    }
    if (declared != output_size)
        return false;
    size_t tree_end = table + ((size_t)stream[table] + 1) * 2;
    if (tree_end > size || table + 1 >= tree_end)
        return false;
    size_t node = table + 1;
    size_t input = tree_end;
    size_t produced = 0;
    int lower_nibble = -1;
    while (produced < output_size) {
        if (!wm_resource_range_fits(size, input, 4))
            return false;
        uint32_t word = le32(stream + input);
        input += 4;
        for (int shift = 31; shift >= 0; shift--) {
            unsigned bit = (word >> shift) & 1u;
            if (node >= tree_end)
                return false;
            uint8_t descriptor = stream[node];
            size_t next =
                (node & ~(size_t)1) + ((size_t)(descriptor & 63u) + 1) * 2 + bit;
            if (next >= tree_end)
                return false;
            node = next;
            if ((descriptor & (0x80u >> bit)) == 0)
                continue;
            uint8_t symbol = stream[node];
            node = table + 1;
            if (depth == 8) {
                output[produced++] = symbol;
            } else if (lower_nibble < 0) {
                lower_nibble = symbol & 15u;
            } else {
                output[produced++] =
                    (uint8_t)((unsigned)lower_nibble | ((symbol & 15u) << 4));
                lower_nibble = -1;
            }
            if (produced == output_size)
                return true;
        }
    }
    return true;
}

bool wm_font_decode_sheet(const WmFont *font, size_t sheet_index, WmImage *image,
                          char *error, size_t error_capacity) {
    font_error(error, error_capacity, "");
    if (!font || !image || sheet_index >= font->sheet_count) {
        font_error(error, error_capacity, "Invalid font sheet index.");
        return false;
    }
    *image = (WmImage){0};
    const FontSheet *sheet = &font->sheets[sheet_index];
    uint8_t *expanded = NULL;
    const uint8_t *pixels = font->data + sheet->offset;
    if (font->compressed) {
        expanded = malloc(font->sheet_size);
        if (!expanded ||
            !decode_huffman(pixels, sheet->stored_size, expanded, font->sheet_size)) {
            free(expanded);
            font_error(error, error_capacity, "Invalid compressed font sheet.");
            return false;
        }
        pixels = expanded;
    }

    if (font->sheet_size > SIZE_MAX - 32) {
        free(expanded);
        font_error(error, error_capacity, "Font sheet is too large.");
        return false;
    }
    size_t wrapped_size = 32 + font->sheet_size;
    uint8_t *wrapped = calloc(wrapped_size, 1);
    if (!wrapped) {
        free(expanded);
        font_error(error, error_capacity, "Out of memory decoding font sheet.");
        return false;
    }
    write_be32(wrapped, 0x0020af30);
    write_be32(wrapped + 4, 1);
    write_be32(wrapped + 8, 12);
    write_be32(wrapped + 12, 20);
    write_be16(wrapped + 20, sheet->info.height);
    write_be16(wrapped + 22, sheet->info.width);
    write_be32(wrapped + 24, sheet->info.format);
    write_be32(wrapped + 28, 32);
    memcpy(wrapped + 32, pixels, font->sheet_size);
    free(expanded);

    WmTpl decoded = {0};
    bool success =
        wm_tpl_decode(wrapped, wrapped_size, &decoded, error, error_capacity);
    free(wrapped);
    if (!success)
        return false;
    if (decoded.count != 1) {
        wm_tpl_free(&decoded);
        font_error(error, error_capacity, "Invalid font sheet image count.");
        return false;
    }
    image->width = decoded.images[0].width;
    image->height = decoded.images[0].height;
    image->pixels = decoded.images[0].rgba;
    decoded.images[0].rgba = NULL;
    wm_tpl_free(&decoded);
    if (sheet->info.format == 0 || sheet->info.format == 1) {
        /* GX I4/I8 font texels mask the requested text color. Preserve their
         * intensity as coverage only; multiplying RGB by it as well darkens
         * antialiased edges a second time during alpha blending. */
        size_t pixel_count = (size_t)image->width * image->height;
        for (size_t index = 0; index < pixel_count; index++) {
            uint8_t *pixel = image->pixels + index * 4;
            pixel[0] = 255;
            pixel[1] = 255;
            pixel[2] = 255;
        }
    }
    return true;
}
