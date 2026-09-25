#include "wii_menu/resource_font.h"
#include "wii_menu/resource_tpl.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

enum {
    WM_FONT_MAX_FILE = 64 * 1024 * 1024,
    WM_FONT_MAX_SHEETS = 256,
    WM_FONT_MAX_CHAINS = 1024,
    WM_FONT_MAX_TEXT_BYTES = 65536,
    WM_FONT_MAX_LINES = 4096
};

typedef struct FontSheet {
    WmFontSheetInfo info;
    size_t offset;
    size_t stored_size;
} FontSheet;

typedef struct FontLine {
    size_t first_byte;
    size_t byte_count;
    float width;
    float x;
    float y;
} FontLine;

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

struct WmFontTextLayout {
    const WmFont *font;
    char *text;
    WmFontPane pane;
    FontLine *lines;
    size_t line_count;
    size_t line_capacity;
};

static void font_error(char *error, size_t capacity, const char *message) {
    if (error && capacity) snprintf(error, capacity, "%s", message);
}

static bool range_fits(size_t size, size_t offset, size_t length) {
    return offset <= size && length <= size - offset;
}

static uint16_t be16(const uint8_t *bytes) {
    return (uint16_t)(((uint16_t)bytes[0] << 8) | bytes[1]);
}

static uint32_t be32(const uint8_t *bytes) {
    return ((uint32_t)bytes[0] << 24) | ((uint32_t)bytes[1] << 16) |
           ((uint32_t)bytes[2] << 8) | bytes[3];
}

static uint32_t le32(const uint8_t *bytes) {
    return (uint32_t)bytes[0] | ((uint32_t)bytes[1] << 8) |
           ((uint32_t)bytes[2] << 16) | ((uint32_t)bytes[3] << 24);
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
        if (visited[index] == offset) return true;
    }
    return false;
}

static bool width_chain(WmFont *font, size_t start, bool fill,
                        size_t *maximum) {
    size_t visited[WM_FONT_MAX_CHAINS];
    size_t count = 0;
    size_t position = start;
    while (position) {
        if (count == WM_FONT_MAX_CHAINS || seen_offset(visited, count, position) ||
            !range_fits(font->size, position, 8)) return false;
        visited[count++] = position;
        const uint8_t *entry = font->data + position;
        size_t begin = be16(entry), end = be16(entry + 2);
        size_t next = be32(entry + 4);
        if (end < begin || !range_fits(font->size, position + 8,
                                       (end - begin + 1) * 3)) return false;
        if (end > *maximum) *maximum = end;
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
            !range_fits(font->size, position, 12)) return false;
        visited[count++] = position;
        const uint8_t *entry = font->data + position;
        uint16_t begin = be16(entry), end = be16(entry + 2);
        uint16_t method = be16(entry + 4);
        size_t next = be32(entry + 8);
        if (end < begin) return false;
        size_t range = (size_t)end - begin + 1;
        if (method == 0) {
            if (!range_fits(font->size, position + 12, 2)) return false;
            size_t first = be16(entry + 12);
            if (first + range > 65536) return false;
            for (size_t code = begin; code <= end; code++) {
                font->characters[code] = (uint16_t)(first + code - begin);
            }
        } else if (method == 1) {
            if (!range_fits(font->size, position + 12, range * 2)) return false;
            for (size_t code = begin; code <= end; code++) {
                uint16_t glyph = be16(entry + 12 + (code - begin) * 2);
                if (glyph != UINT16_MAX) font->characters[code] = glyph;
            }
        } else if (method == 2) {
            if (!range_fits(font->size, position + 12, 2)) return false;
            size_t entries = be16(entry + 12);
            if (!range_fits(font->size, position + 14, entries * 4)) return false;
            for (size_t index = 0; index < entries; index++) {
                uint16_t code = be16(entry + 14 + index * 4);
                uint16_t glyph = be16(entry + 16 + index * 4);
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
            if (!range_fits(font->size, cursor, 4)) return false;
            size_t stored = be32(font->data + cursor);
            cursor += 4;
            if (!stored || !range_fits(font->size, cursor, stored)) return false;
            sheet->offset = cursor;
            sheet->stored_size = stored;
            cursor += stored;
        } else {
            if (!range_fits(font->size, cursor, font->sheet_size)) return false;
            sheet->offset = cursor;
            sheet->stored_size = font->sheet_size;
            cursor += font->sheet_size;
        }
    }
    return true;
}

WmFont *wm_font_decode(const uint8_t *data, size_t size,
                       char *error, size_t error_capacity) {
    font_error(error, error_capacity, "");
    if (!data || size < 16 || size > WM_FONT_MAX_FILE ||
        (memcmp(data, "RFNT", 4) != 0 && memcmp(data, "RFNA", 4) != 0) ||
        data[4] != 0xfe || data[5] != 0xff) {
        font_error(error, error_capacity, "Expected bounded big-endian RFNT/RFNA font.");
        return NULL;
    }
    size_t declared = be32(data + 8);
    size_t offset = be16(data + 12);
    size_t section_count = be16(data + 14);
    if (declared > size || offset < 16 || offset >= declared ||
        section_count == 0 || section_count > 1024) {
        font_error(error, error_capacity, "Invalid font section header.");
        return NULL;
    }
    size_t finf = 0, finf_length = 0;
    for (size_t index = 0; index < section_count; index++) {
        if (!range_fits(declared, offset, 8)) {
            font_error(error, error_capacity, "Truncated font section.");
            return NULL;
        }
        size_t length = be32(data + offset + 4);
        if (length < 8 || !range_fits(declared, offset, length)) {
            font_error(error, error_capacity, "Invalid font section size.");
            return NULL;
        }
        if (memcmp(data + offset, "FINF", 4) == 0) {
            finf = offset;
            finf_length = length;
        }
        offset += length;
    }
    if (!finf || finf_length < 31 || !range_fits(declared, finf, 31)) {
        font_error(error, error_capacity, "Font information section is missing.");
        return NULL;
    }

    WmFont *font = calloc(1, sizeof(*font));
    if (!font) {
        font_error(error, error_capacity, "Out of memory.");
        return NULL;
    }
    font->data = malloc(declared);
    if (!font->data) goto invalid;
    memcpy(font->data, data, declared);
    font->size = declared;
    const uint8_t *info = font->data + finf;
    font->metrics.line_feed = (int8_t)info[9];
    font->metrics.default_glyph = be16(info + 10);
    font->metrics.encoding = info[15];
    font->metrics.height = info[28];
    font->metrics.width = info[29];
    font->metrics.ascent = info[30];
    size_t glyph_offset = be32(info + 16);
    size_t width_offset = be32(info + 20);
    size_t map_offset = be32(info + 24);
    if (glyph_offset < 8 || width_offset < 8 || map_offset < 8 ||
        !range_fits(declared, glyph_offset, 24) ||
        !range_fits(declared, width_offset, 8) ||
        !range_fits(declared, map_offset, 12) ||
        memcmp(font->data + glyph_offset - 8, "TGLP", 4) != 0 ||
        memcmp(font->data + width_offset - 8, "CWDH", 4) != 0 ||
        memcmp(font->data + map_offset - 8, "CMAP", 4) != 0) goto invalid;
    const uint8_t *glyph_info = font->data + glyph_offset;
    font->metrics.cell_width = glyph_info[0];
    font->metrics.cell_height = glyph_info[1];
    font->metrics.baseline = (int8_t)glyph_info[2];
    font->sheet_size = be32(glyph_info + 4);
    font->sheet_count = be16(glyph_info + 8);
    uint16_t format = be16(glyph_info + 10);
    uint16_t columns = be16(glyph_info + 12);
    uint16_t rows = be16(glyph_info + 14);
    uint16_t sheet_width = be16(glyph_info + 16);
    uint16_t sheet_height = be16(glyph_info + 18);
    size_t image_offset = be32(glyph_info + 20);
    font->compressed = (format & 0x8000u) != 0;
    if (!font->metrics.width || !font->metrics.height || !font->sheet_size ||
        font->sheet_size > 16u * 1024u * 1024u || font->sheet_count == 0 ||
        font->sheet_count > WM_FONT_MAX_SHEETS || !columns || !rows ||
        !sheet_width || !sheet_height || sheet_width > 4096 || sheet_height > 4096 ||
        (uint64_t)sheet_width * sheet_height * 4 > 64u * 1024u * 1024u ||
        !range_fits(declared, image_offset, 1)) goto invalid;
    font->sheets = calloc(font->sheet_count, sizeof(*font->sheets));
    font->characters = malloc(65536u * sizeof(*font->characters));
    if (!font->sheets || !font->characters) goto invalid;
    for (size_t code = 0; code < 65536; code++) font->characters[code] = UINT16_MAX;
    if (!parse_sheets(font, image_offset, sheet_width, sheet_height, format)) goto invalid;

    size_t maximum = 0;
    if (!width_chain(font, width_offset, false, &maximum)) goto invalid;
    font->glyph_count = maximum + 1;
    font->glyphs = calloc(font->glyph_count, sizeof(*font->glyphs));
    font->glyph_present = calloc(font->glyph_count, sizeof(*font->glyph_present));
    if (!font->glyphs || !font->glyph_present ||
        !width_chain(font, width_offset, true, &maximum) ||
        !parse_character_maps(font, map_offset)) goto invalid;
    size_t cells_per_sheet = (size_t)columns * rows;
    for (size_t index = 0; index < font->glyph_count; index++) {
        if (!font->glyph_present[index]) continue;
        size_t sheet = index / cells_per_sheet;
        size_t cell = index % cells_per_sheet;
        size_t x = (cell % columns) * ((size_t)font->metrics.cell_width + 1) + 1;
        size_t y = (cell / columns) * ((size_t)font->metrics.cell_height + 1) + 1;
        if (sheet >= font->sheet_count || x + font->glyphs[index].width > sheet_width ||
            y + font->metrics.cell_height > sheet_height) goto invalid;
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
    if (!font) return;
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
    if (!font) return NULL;
    uint16_t index = codepoint < 65536 ? font->characters[codepoint] : UINT16_MAX;
    /* The exported phone-key marker is stored under a private-use codepoint.
     * Accept the earlier HTML marker and the requested U+2423 open box. */
    if (index == UINT16_MAX &&
        (codepoint == 0x23b5 || codepoint == 0x2423))
        index = font->characters[0xe057];
    if (index == UINT16_MAX) index = font->metrics.default_glyph;
    return index < font->glyph_count && font->glyph_present[index]
               ? &font->glyphs[index] : NULL;
}

static bool decode_huffman(const uint8_t *stream, size_t size,
                           uint8_t *output, size_t output_size) {
    if (size < 5 || (stream[0] != 0x24 && stream[0] != 0x28)) return false;
    unsigned depth = stream[0] & 15u;
    size_t declared = (size_t)stream[1] | ((size_t)stream[2] << 8) |
                      ((size_t)stream[3] << 16);
    size_t table = 4;
    if (!declared) {
        if (size < 9) return false;
        declared = le32(stream + 4);
        table = 8;
    }
    if (declared != output_size) return false;
    size_t tree_end = table + ((size_t)stream[table] + 1) * 2;
    if (tree_end > size || table + 1 >= tree_end) return false;
    size_t node = table + 1;
    size_t input = tree_end;
    size_t produced = 0;
    int lower_nibble = -1;
    while (produced < output_size) {
        if (!range_fits(size, input, 4)) return false;
        uint32_t word = le32(stream + input);
        input += 4;
        for (int shift = 31; shift >= 0; shift--) {
            unsigned bit = (word >> shift) & 1u;
            if (node >= tree_end) return false;
            uint8_t descriptor = stream[node];
            size_t next = (node & ~(size_t)1) +
                          ((size_t)(descriptor & 63u) + 1) * 2 + bit;
            if (next >= tree_end) return false;
            node = next;
            if ((descriptor & (0x80u >> bit)) == 0) continue;
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
            if (produced == output_size) return true;
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
        if (!expanded || !decode_huffman(pixels, sheet->stored_size,
                                         expanded, font->sheet_size)) {
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
    bool success = wm_tpl_decode(wrapped, wrapped_size, &decoded,
                                 error, error_capacity);
    free(wrapped);
    if (!success) return false;
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
    return true;
}

static uint32_t next_codepoint(const char *text, size_t length, size_t *position) {
    uint8_t first = (uint8_t)text[(*position)++];
    if (first < 0x80) return first;
    uint32_t value;
    size_t following;
    uint32_t minimum;
    if (first >= 0xc2 && first <= 0xdf) {
        value = first & 0x1fu;
        following = 1;
        minimum = 0x80;
    } else if (first >= 0xe0 && first <= 0xef) {
        value = first & 0x0fu;
        following = 2;
        minimum = 0x800;
    } else if (first >= 0xf0 && first <= 0xf4) {
        value = first & 0x07u;
        following = 3;
        minimum = 0x10000;
    } else {
        return 0xfffd;
    }
    if (following > length - *position) return 0xfffd;
    for (size_t index = 0; index < following; index++) {
        uint8_t part = (uint8_t)text[*position];
        if ((part & 0xc0u) != 0x80u) return 0xfffd;
        value = (value << 6) | (part & 0x3fu);
        (*position)++;
    }
    if (value < minimum || value > 0x10ffff ||
        (value >= 0xd800 && value <= 0xdfff)) return 0xfffd;
    return value;
}

static float text_width_span(const WmFont *font, const char *text, size_t length,
                             float scale_x, float spacing) {
    float width = 0;
    size_t position = 0, characters = 0;
    while (position < length) {
        uint32_t codepoint = next_codepoint(text, length, &position);
        const WmFontGlyph *glyph = wm_font_glyph(font, codepoint);
        if (characters++) width += spacing;
        if (glyph) width += glyph->advance * scale_x;
    }
    return width;
}

float wm_font_text_width(const WmFont *font, const char *text,
                         const float size[2], float spacing) {
    if (!text) return 0;
    return wm_font_text_width_n(font, text, strlen(text), size, spacing);
}

float wm_font_text_width_n(const WmFont *font, const char *text, size_t length,
                           const float size[2], float spacing) {
    if (!font || !text || !size || !font->metrics.width || !isfinite(size[0]) ||
        !isfinite(spacing)) return 0;
    if (length > WM_FONT_MAX_TEXT_BYTES) return 0;
    return text_width_span(font, text, length,
                           size[0] / font->metrics.width, spacing);
}

static void transform_font_point(const float *matrix, float x, float y,
                                 float point[3]) {
    if (!matrix) {
        point[0] = x;
        point[1] = y;
        point[2] = 0;
        return;
    }
    point[0] = matrix[0] * x + matrix[1] * y + matrix[3];
    point[1] = matrix[4] * x + matrix[5] * y + matrix[7];
    point[2] = matrix[8] * x + matrix[9] * y + matrix[11];
}

static void emit_span(const WmFont *font, const char *text, size_t length,
                      size_t base_byte, float width,
                      const WmFontDrawOptions *options) {
    if (!options->on_quad) return;
    float scale_x = options->size[0] / font->metrics.width;
    float scale_y = options->size[1] / font->metrics.height;
    float cursor = options->x;
    if (options->align == WM_FONT_ALIGN_CENTER) cursor -= width / 2;
    else if (options->align == WM_FONT_ALIGN_RIGHT) cursor -= width;
    size_t position = 0;
    while (position < length) {
        size_t glyph_byte = position;
        uint32_t codepoint = next_codepoint(text, length, &position);
        const WmFontGlyph *glyph = wm_font_glyph(font, codepoint);
        if (!glyph) continue;
        if (glyph->width && glyph->sheet < font->sheet_count) {
            const WmFontSheetInfo *sheet = &font->sheets[glyph->sheet].info;
            float left = cursor + glyph->left * scale_x;
            float top = options->y;
            float right = left + glyph->width * scale_x;
            float bottom = top - glyph->height * scale_y;
            WmFontQuad quad = {
                .sheet = glyph->sheet,
                .format = sheet->format,
                .byte_index = base_byte + glyph_byte,
                .glyph_alpha_only = sheet->format == 0 || sheet->format == 1
            };
            if (options->sheet_provider) {
                uint32_t texture = 0;
                if (options->sheet_provider(options->context, glyph->sheet, &texture)) {
                    quad.texture = texture;
                }
            }
            transform_font_point(options->matrix, left, top, quad.vertices[0].position);
            transform_font_point(options->matrix, right, top, quad.vertices[1].position);
            transform_font_point(options->matrix, left, bottom, quad.vertices[2].position);
            transform_font_point(options->matrix, right, bottom, quad.vertices[3].position);
            float u0 = (float)glyph->x / sheet->width;
            float u1 = (float)(glyph->x + glyph->width) / sheet->width;
            float v0 = (float)glyph->y / sheet->height;
            float v1 = (float)(glyph->y + glyph->height) / sheet->height;
            quad.vertices[0].uv[0] = quad.vertices[2].uv[0] = u0;
            quad.vertices[1].uv[0] = quad.vertices[3].uv[0] = u1;
            quad.vertices[0].uv[1] = quad.vertices[1].uv[1] = v0;
            quad.vertices[2].uv[1] = quad.vertices[3].uv[1] = v1;
            for (size_t vertex = 0; vertex < 4; vertex++) {
                const uint8_t *color = vertex < 2 ? options->top_color :
                                                     options->bottom_color;
                for (size_t channel = 0; channel < 3; channel++) {
                    quad.vertices[vertex].color[channel] = color[channel] / 255.0f;
                }
                quad.vertices[vertex].color[3] =
                    color[3] / 255.0f * options->alpha;
            }
            options->on_quad(options->context, &quad);
        }
        cursor += glyph->advance * scale_x + options->spacing;
    }
}

void wm_font_emit_line(const WmFont *font, const char *text,
                        const WmFontDrawOptions *options) {
    if (!font || !text || !options || !font->metrics.width || !font->metrics.height ||
        !isfinite(options->size[0]) || !isfinite(options->size[1]) ||
        options->size[0] <= 0 || options->size[1] <= 0 ||
        !isfinite(options->spacing) || !isfinite(options->alpha)) return;
    size_t length = strlen(text);
    if (length > WM_FONT_MAX_TEXT_BYTES) return;
    float width = text_width_span(font, text, length,
                                  options->size[0] / font->metrics.width,
                                  options->spacing);
    emit_span(font, text, length, 0, width, options);
}

static bool append_line(WmFontTextLayout *layout, size_t first, size_t count,
                        float width) {
    if (layout->line_count == WM_FONT_MAX_LINES) return false;
    if (layout->line_count == layout->line_capacity) {
        size_t capacity = layout->line_capacity ? layout->line_capacity * 2 : 8;
        if (capacity > WM_FONT_MAX_LINES) capacity = WM_FONT_MAX_LINES;
        FontLine *lines = realloc(layout->lines, capacity * sizeof(*lines));
        if (!lines) return false;
        layout->lines = lines;
        layout->line_capacity = capacity;
    }
    layout->lines[layout->line_count++] = (FontLine){
        .first_byte = first,
        .byte_count = count,
        .width = width
    };
    return true;
}

WmFontTextLayout *wm_font_layout_pane(const WmFont *font, const char *text,
                                      const WmFontPane *pane) {
    if (!font || !text || !pane || pane->origin > 8 || pane->text_position > 8 ||
        !font->metrics.width || !font->metrics.height ||
        !isfinite(pane->size[0]) || !isfinite(pane->size[1]) ||
        !isfinite(pane->font_size[0]) || !isfinite(pane->font_size[1]) ||
        !isfinite(pane->char_space) || !isfinite(pane->line_space) ||
        pane->font_size[0] <= 0 || pane->font_size[1] <= 0) return NULL;
    size_t length = strlen(text);
    if (length > WM_FONT_MAX_TEXT_BYTES) return NULL;
    WmFontTextLayout *layout = calloc(1, sizeof(*layout));
    if (!layout) return NULL;
    layout->font = font;
    layout->pane = *pane;
    layout->text = malloc(length + 1);
    if (!layout->text) goto invalid_layout;
    memcpy(layout->text, text, length + 1);

    float scale_x = pane->font_size[0] / font->metrics.width;
    float scale_y = pane->font_size[1] / font->metrics.height;
    size_t line_start = 0, position = 0, characters = 0;
    float width = 0;
    while (position < length) {
        size_t before = position;
        uint32_t codepoint = next_codepoint(text, length, &position);
        if (codepoint == '\n') {
            if (!append_line(layout, line_start, before - line_start, width)) goto invalid_layout;
            line_start = position;
            characters = 0;
            width = 0;
            continue;
        }
        const WmFontGlyph *glyph = wm_font_glyph(font, codepoint);
        float advance = glyph ? glyph->advance * scale_x : 0;
        float next_width = width + (characters ? pane->char_space : 0) + advance;
        if (!pane->no_wrap && characters && next_width > pane->size[0]) {
            if (!append_line(layout, line_start, before - line_start, width)) goto invalid_layout;
            line_start = before;
            characters = 0;
            width = 0;
            next_width = advance;
        }
        width = next_width;
        characters++;
    }
    if (!append_line(layout, line_start, length - line_start, width)) goto invalid_layout;

    float line_height = font->metrics.line_feed * scale_y + pane->line_space;
    float text_height = layout->line_count * line_height;
    float horizontal = (pane->text_position % 3) / 2.0f;
    float vertical = (pane->text_position / 3) / 2.0f;
    float left = -(float)(pane->origin % 3) * pane->size[0] / 2;
    float top = (float)(pane->origin / 3) * pane->size[1] / 2 -
                (pane->size[1] - text_height) * vertical;
    float glyph_offset = (font->metrics.ascent - font->metrics.baseline) * scale_y;
    for (size_t index = 0; index < layout->line_count; index++) {
        FontLine *line = &layout->lines[index];
        line->x = left + (pane->size[0] - line->width) * horizontal;
        line->y = top - index * line_height - glyph_offset;
    }
    return layout;

invalid_layout:
    wm_font_text_layout_destroy(layout);
    return NULL;
}

void wm_font_text_layout_destroy(WmFontTextLayout *layout) {
    if (!layout) return;
    free(layout->text);
    free(layout->lines);
    free(layout);
}

size_t wm_font_text_layout_line_count(const WmFontTextLayout *layout) {
    return layout ? layout->line_count : 0;
}

bool wm_font_text_layout_caret(const WmFontTextLayout *layout,
                               size_t byte_index, float *x, float *y) {
    if (!layout || !layout->line_count || !x || !y) return false;
    size_t text_bytes = strlen(layout->text);
    if (byte_index > text_bytes) byte_index = text_bytes;
    while (byte_index > 0 && byte_index < text_bytes &&
           ((unsigned char)layout->text[byte_index] & 0xc0u) == 0x80u) {
        byte_index--;
    }
    const FontLine *line = &layout->lines[0];
    for (size_t index = 1; index < layout->line_count; index++) {
        if (layout->lines[index].first_byte > byte_index) break;
        line = &layout->lines[index];
    }
    size_t prefix_bytes = byte_index - line->first_byte;
    if (prefix_bytes > line->byte_count) prefix_bytes = line->byte_count;
    float prefix_width = text_width_span(layout->font,
        layout->text + line->first_byte, prefix_bytes,
        layout->pane.font_size[0] / layout->font->metrics.width,
        layout->pane.char_space);
    *x = line->x + prefix_width +
         (prefix_bytes ? layout->pane.char_space : 0.0f);
    *y = line->y;
    return true;
}

void wm_font_emit_pane(const WmFontTextLayout *layout,
                        const float parent_matrix[12], float alpha,
                        WmFontSheetProvider sheet_provider,
                        WmFontQuadCallback on_quad, void *context) {
    if (!layout || !isfinite(alpha) || !on_quad) return;
    const WmFontPane *pane = &layout->pane;
    WmFontDrawOptions options = {
        .size = {pane->font_size[0], pane->font_size[1]},
        .spacing = pane->char_space,
        .alpha = alpha,
        .align = WM_FONT_ALIGN_LEFT,
        .matrix = parent_matrix,
        .sheet_provider = sheet_provider,
        .on_quad = on_quad,
        .context = context
    };
    memcpy(options.top_color, pane->top_color, 4);
    memcpy(options.bottom_color, pane->bottom_color, 4);
    for (size_t index = 0; index < layout->line_count; index++) {
        const FontLine *line = &layout->lines[index];
        options.x = line->x;
        options.y = line->y;
        emit_span(layout->font, layout->text + line->first_byte, line->byte_count,
                  line->first_byte, line->width, &options);
    }
}
