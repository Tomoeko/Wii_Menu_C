#include "wii_menu/outline_font.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

enum {
    OUTLINE_MAX_FILE = 8 * 1024 * 1024,
    OUTLINE_MAX_TABLES = 64,
    OUTLINE_MAX_GLYPHS = 65535,
    OUTLINE_MAX_POINTS = 4096,
    OUTLINE_MAX_CONTOURS = 512,
    OUTLINE_MAX_EDGES = 8192,
    OUTLINE_MAX_ATLASES = 8,
    OUTLINE_FIRST_CODEPOINT = 32,
    OUTLINE_CODEPOINT_COUNT = 224,
    OUTLINE_MAX_PIXEL_SIZE = 72
};

typedef struct FontTable {
    size_t offset;
    size_t size;
} FontTable;

typedef struct OutlinePoint {
    float x;
    float y;
    bool on_curve;
} OutlinePoint;

typedef struct OutlineShape {
    OutlinePoint points[OUTLINE_MAX_POINTS];
    uint16_t end_points[OUTLINE_MAX_CONTOURS];
    unsigned point_count;
    unsigned contour_count;
} OutlineShape;

typedef struct OutlineEdge {
    float x0;
    float y0;
    float x1;
    float y1;
} OutlineEdge;

typedef struct OutlineEdges {
    OutlineEdge edges[OUTLINE_MAX_EDGES];
    unsigned count;
    float scale;
    float x_origin;
    float y_origin;
} OutlineEdges;

typedef struct AtlasGlyph {
    float u0;
    float v0;
    float u1;
    float v1;
    float advance;
    int left;
    int top;
    unsigned width;
    unsigned height;
} AtlasGlyph;

typedef struct OutlineAtlas {
    unsigned pixel_size;
    bool synthetic_bold;
    unsigned dimension;
    uint32_t texture;
    AtlasGlyph glyphs[OUTLINE_CODEPOINT_COUNT];
} OutlineAtlas;

struct WmOutlineFont {
    uint8_t *bytes;
    size_t size;
    FontTable cmap;
    FontTable glyf;
    FontTable hmtx;
    FontTable loca;
    size_t format4;
    size_t format4_size;
    unsigned units_per_em;
    unsigned glyph_count;
    unsigned horizontal_metrics;
    int ascent;
    int descent;
    bool long_loca;
    OutlineAtlas atlases[OUTLINE_MAX_ATLASES];
    unsigned atlas_count;
};

static bool fits(size_t size, size_t offset, size_t length) {
    return offset <= size && length <= size - offset;
}

static uint16_t be16(const uint8_t *bytes) {
    return (uint16_t)(((unsigned)bytes[0] << 8) | bytes[1]);
}

static int16_t signed16(const uint8_t *bytes) {
    return (int16_t)be16(bytes);
}

static uint32_t be32(const uint8_t *bytes) {
    return ((uint32_t)bytes[0] << 24) | ((uint32_t)bytes[1] << 16) |
           ((uint32_t)bytes[2] << 8) | bytes[3];
}

static bool tag_equals(const uint8_t *tag, const char name[4]) {
    return memcmp(tag, name, 4) == 0;
}

static bool find_table(const uint8_t *bytes, size_t size, size_t face,
                       const char name[4], FontTable *table) {
    if (!fits(size, face, 12)) return false;
    unsigned count = be16(bytes + face + 4);
    if (!count || count > OUTLINE_MAX_TABLES ||
        !fits(size, face + 12, (size_t)count * 16)) return false;
    for (unsigned index = 0; index < count; index++) {
        const uint8_t *entry = bytes + face + 12 + (size_t)index * 16;
        if (!tag_equals(entry, name)) continue;
        size_t offset = be32(entry + 8);
        size_t length = be32(entry + 12);
        if (!length || !fits(size, offset, length)) return false;
        *table = (FontTable){offset, length};
        return true;
    }
    return false;
}

static bool select_cmap(WmOutlineFont *font) {
    const uint8_t *bytes = font->bytes;
    FontTable table = font->cmap;
    if (table.size < 4) return false;
    unsigned count = be16(bytes + table.offset + 2);
    if (!count || count > 256 || table.size < 4 + (size_t)count * 8)
        return false;
    size_t chosen = 0;
    size_t chosen_size = 0;
    unsigned chosen_rank = 0;
    for (unsigned index = 0; index < count; index++) {
        const uint8_t *entry = bytes + table.offset + 4 + (size_t)index * 8;
        unsigned platform = be16(entry);
        unsigned encoding = be16(entry + 2);
        size_t offset = be32(entry + 4);
        if (offset > table.size || table.size - offset < 16) continue;
        size_t subtable = table.offset + offset;
        if (be16(bytes + subtable) != 4) continue;
        size_t length = be16(bytes + subtable + 2);
        if (length < 24 || length > table.size - offset) continue;
        unsigned segments = be16(bytes + subtable + 6) / 2;
        if (!segments || segments > 8192 ||
            16 + (size_t)segments * 8 > length) continue;
        unsigned rank = platform == 3 && encoding == 1 ? 2 :
                        platform == 0 ? 1 : 0;
        if (rank > chosen_rank) {
            chosen = subtable;
            chosen_size = length;
            chosen_rank = rank;
        }
    }
    font->format4 = chosen;
    font->format4_size = chosen_size;
    return chosen_rank != 0;
}

WmOutlineFont *wm_outline_font_decode(const uint8_t *bytes, size_t size,
                                      unsigned face_index) {
    if (!bytes || size < 12 || size > OUTLINE_MAX_FILE) return NULL;
    size_t face = 0;
    if (memcmp(bytes, "ttcf", 4) == 0) {
        unsigned count = be32(bytes + 8);
        if (!count || count > 16 || face_index >= count ||
            !fits(size, 12, (size_t)count * 4)) return NULL;
        face = be32(bytes + 12 + (size_t)face_index * 4);
    } else if (face_index != 0) {
        return NULL;
    }
    if (!fits(size, face, 12) || be32(bytes + face) != 0x00010000u)
        return NULL;

    FontTable head, maxp, hhea, hmtx, loca, glyf, cmap;
    if (!find_table(bytes, size, face, "head", &head) || head.size < 54 ||
        !find_table(bytes, size, face, "maxp", &maxp) || maxp.size < 6 ||
        !find_table(bytes, size, face, "hhea", &hhea) || hhea.size < 36 ||
        !find_table(bytes, size, face, "hmtx", &hmtx) ||
        !find_table(bytes, size, face, "loca", &loca) ||
        !find_table(bytes, size, face, "glyf", &glyf) ||
        !find_table(bytes, size, face, "cmap", &cmap)) return NULL;
    unsigned units = be16(bytes + head.offset + 18);
    unsigned glyphs = be16(bytes + maxp.offset + 4);
    unsigned metrics = be16(bytes + hhea.offset + 34);
    int loca_format = signed16(bytes + head.offset + 50);
    if (units < 16 || units > 16384 || !glyphs ||
        glyphs > OUTLINE_MAX_GLYPHS || !metrics || metrics > glyphs ||
        (loca_format != 0 && loca_format != 1) ||
        hmtx.size < (size_t)metrics * 4 +
                    (size_t)(glyphs - metrics) * 2 ||
        loca.size < (size_t)(glyphs + 1) * (loca_format ? 4u : 2u))
        return NULL;

    WmOutlineFont *font = calloc(1, sizeof(*font));
    if (!font) return NULL;
    font->bytes = malloc(size);
    if (!font->bytes) {
        free(font);
        return NULL;
    }
    memcpy(font->bytes, bytes, size);
    font->size = size;
    font->cmap = cmap;
    font->glyf = glyf;
    font->hmtx = hmtx;
    font->loca = loca;
    font->units_per_em = units;
    font->glyph_count = glyphs;
    font->horizontal_metrics = metrics;
    font->ascent = signed16(bytes + hhea.offset + 4);
    font->descent = signed16(bytes + hhea.offset + 6);
    font->long_loca = loca_format == 1;
    if (!select_cmap(font)) {
        wm_outline_font_destroy(font, NULL);
        return NULL;
    }
    return font;
}

WmOutlineFont *wm_outline_font_load(const char *path, unsigned face_index) {
    if (!path) return NULL;
    FILE *file = fopen(path, "rb");
    if (!file) return NULL;
    WmOutlineFont *font = NULL;
    if (fseek(file, 0, SEEK_END) == 0) {
        long length = ftell(file);
        if (length > 0 && length <= OUTLINE_MAX_FILE &&
            fseek(file, 0, SEEK_SET) == 0) {
            uint8_t *bytes = malloc((size_t)length);
            if (bytes && fread(bytes, 1, (size_t)length, file) ==
                             (size_t)length && fgetc(file) == EOF)
                font = wm_outline_font_decode(bytes, (size_t)length,
                                              face_index);
            free(bytes);
        }
    }
    fclose(file);
    return font;
}

void wm_outline_font_destroy(WmOutlineFont *font, WmPlatform *platform) {
    if (!font) return;
#ifndef WM_OUTLINE_FONT_PARSE_ONLY
    if (platform) {
        for (unsigned index = 0; index < font->atlas_count; index++) {
            if (font->atlases[index].texture)
                wm_platform_destroy_texture(platform,
                                            font->atlases[index].texture);
        }
    }
#else
    (void)platform;
#endif
    free(font->bytes);
    free(font);
}

static unsigned glyph_for_codepoint(const WmOutlineFont *font,
                                    uint32_t codepoint) {
    if (codepoint > 0xffff || codepoint == 0xffff) return 0;
    const uint8_t *table = font->bytes + font->format4;
    unsigned segments = be16(table + 6) / 2;
    size_t start_array = 16 + (size_t)segments * 2;
    size_t delta_array = start_array + (size_t)segments * 2;
    size_t range_array = delta_array + (size_t)segments * 2;
    for (unsigned index = 0; index < segments; index++) {
        unsigned end = be16(table + 14 + (size_t)index * 2);
        if (codepoint > end) continue;
        unsigned start = be16(table + start_array + (size_t)index * 2);
        if (codepoint < start) return 0;
        unsigned delta = be16(table + delta_array + (size_t)index * 2);
        unsigned range = be16(table + range_array + (size_t)index * 2);
        unsigned glyph;
        if (!range) {
            glyph = (unsigned)((codepoint + delta) & 0xffffu);
        } else {
            size_t offset = range_array + (size_t)index * 2 + range +
                            (size_t)(codepoint - start) * 2;
            if (!fits(font->format4_size, offset, 2)) return 0;
            glyph = be16(table + offset);
            if (glyph) glyph = (glyph + delta) & 0xffffu;
        }
        return glyph < font->glyph_count ? glyph : 0;
    }
    return 0;
}

static float glyph_advance(const WmOutlineFont *font, unsigned glyph,
                           unsigned pixels) {
    unsigned metric = glyph < font->horizontal_metrics
        ? glyph : font->horizontal_metrics - 1;
    unsigned width = be16(font->bytes + font->hmtx.offset +
                          (size_t)metric * 4);
    return (float)width * pixels / font->units_per_em;
}

static bool glyph_range(const WmOutlineFont *font, unsigned glyph,
                        size_t *offset, size_t *length) {
    if (glyph >= font->glyph_count) return false;
    size_t first, last;
    if (font->long_loca) {
        first = be32(font->bytes + font->loca.offset + (size_t)glyph * 4);
        last = be32(font->bytes + font->loca.offset +
                    (size_t)(glyph + 1) * 4);
    } else {
        first = (size_t)be16(font->bytes + font->loca.offset +
                             (size_t)glyph * 2) * 2;
        last = (size_t)be16(font->bytes + font->loca.offset +
                            (size_t)(glyph + 1) * 2) * 2;
    }
    if (last < first || last > font->glyf.size) return false;
    *offset = font->glyf.offset + first;
    *length = last - first;
    return true;
}

static bool append_simple(const uint8_t *bytes, size_t length,
                          unsigned contours, OutlineShape *shape) {
    if (!contours) return true;
    size_t end_size = (size_t)contours * 2;
    if (length < 10 || contours > OUTLINE_MAX_CONTOURS -
                               shape->contour_count ||
        !fits(length, 10, end_size + 2)) return false;
    unsigned points = be16(bytes + 10 + end_size - 2) + 1;
    if (!points || points > OUTLINE_MAX_POINTS - shape->point_count)
        return false;
    unsigned previous = 0;
    for (unsigned index = 0; index < contours; index++) {
        unsigned end = be16(bytes + 10 + (size_t)index * 2);
        if (end >= points || (index && end <= previous)) return false;
        shape->end_points[shape->contour_count + index] =
            (uint16_t)(shape->point_count + end);
        previous = end;
    }
    size_t cursor = 10 + end_size;
    unsigned instruction_size = be16(bytes + cursor);
    cursor += 2;
    if (!fits(length, cursor, instruction_size)) return false;
    cursor += instruction_size;
    uint8_t flags[OUTLINE_MAX_POINTS];
    for (unsigned index = 0; index < points;) {
        if (!fits(length, cursor, 1)) return false;
        uint8_t flag = bytes[cursor++];
        unsigned repeats = 1;
        if (flag & 0x08) {
            if (!fits(length, cursor, 1)) return false;
            repeats += bytes[cursor++];
        }
        if (repeats > points - index) return false;
        for (unsigned copy = 0; copy < repeats; copy++) flags[index++] = flag;
    }
    int coordinate = 0;
    for (unsigned index = 0; index < points; index++) {
        uint8_t flag = flags[index];
        int delta = 0;
        if (flag & 0x02) {
            if (!fits(length, cursor, 1)) return false;
            delta = bytes[cursor++];
            if (!(flag & 0x10)) delta = -delta;
        } else if (!(flag & 0x10)) {
            if (!fits(length, cursor, 2)) return false;
            delta = signed16(bytes + cursor);
            cursor += 2;
        }
        coordinate += delta;
        shape->points[shape->point_count + index].x = (float)coordinate;
        shape->points[shape->point_count + index].on_curve =
            (flag & 0x01) != 0;
    }
    coordinate = 0;
    for (unsigned index = 0; index < points; index++) {
        uint8_t flag = flags[index];
        int delta = 0;
        if (flag & 0x04) {
            if (!fits(length, cursor, 1)) return false;
            delta = bytes[cursor++];
            if (!(flag & 0x20)) delta = -delta;
        } else if (!(flag & 0x20)) {
            if (!fits(length, cursor, 2)) return false;
            delta = signed16(bytes + cursor);
            cursor += 2;
        }
        coordinate += delta;
        shape->points[shape->point_count + index].y = (float)coordinate;
    }
    shape->point_count += points;
    shape->contour_count += contours;
    return true;
}

static bool append_glyph(const WmOutlineFont *font, unsigned glyph,
                         OutlineShape *shape, unsigned depth) {
    if (depth > 8) return false;
    size_t offset, length;
    if (!glyph_range(font, glyph, &offset, &length)) return false;
    if (!length) return true;
    if (length < 10) return false;
    const uint8_t *bytes = font->bytes + offset;
    int contours = signed16(bytes);
    if (contours >= 0)
        return append_simple(bytes, length, (unsigned)contours, shape);
    if (contours != -1) return false;

    size_t cursor = 10;
    unsigned flags;
    unsigned components = 0;
    do {
        if (!fits(length, cursor, 4) || ++components > 64) return false;
        flags = be16(bytes + cursor);
        unsigned child = be16(bytes + cursor + 2);
        cursor += 4;
        bool words = (flags & 0x0001) != 0;
        bool xy_values = (flags & 0x0002) != 0;
        if (!xy_values || !fits(length, cursor, words ? 4 : 2)) return false;
        int dx = words ? signed16(bytes + cursor) : (int8_t)bytes[cursor];
        int dy = words ? signed16(bytes + cursor + 2) :
                         (int8_t)bytes[cursor + 1];
        cursor += words ? 4 : 2;
        float xx = 1, xy = 0, yx = 0, yy = 1;
        if (flags & 0x0008) {
            if (!fits(length, cursor, 2)) return false;
            xx = yy = signed16(bytes + cursor) / 16384.0f;
            cursor += 2;
        } else if (flags & 0x0040) {
            if (!fits(length, cursor, 4)) return false;
            xx = signed16(bytes + cursor) / 16384.0f;
            yy = signed16(bytes + cursor + 2) / 16384.0f;
            cursor += 4;
        } else if (flags & 0x0080) {
            if (!fits(length, cursor, 8)) return false;
            xx = signed16(bytes + cursor) / 16384.0f;
            xy = signed16(bytes + cursor + 2) / 16384.0f;
            yx = signed16(bytes + cursor + 4) / 16384.0f;
            yy = signed16(bytes + cursor + 6) / 16384.0f;
            cursor += 8;
        }
        unsigned first = shape->point_count;
        if (!append_glyph(font, child, shape, depth + 1)) return false;
        for (unsigned index = first; index < shape->point_count; index++) {
            float x = shape->points[index].x;
            float y = shape->points[index].y;
            shape->points[index].x = xx * x + xy * y + dx;
            shape->points[index].y = yx * x + yy * y + dy;
        }
    } while (flags & 0x0020);
    if (flags & 0x0100) {
        if (!fits(length, cursor, 2)) return false;
        unsigned instructions = be16(bytes + cursor);
        cursor += 2;
        if (!fits(length, cursor, instructions)) return false;
    }
    return true;
}

static OutlinePoint midpoint(OutlinePoint a, OutlinePoint b) {
    return (OutlinePoint){(a.x + b.x) * 0.5f, (a.y + b.y) * 0.5f, true};
}

static bool add_line(OutlineEdges *output, OutlinePoint from,
                     OutlinePoint to) {
    if (output->count == OUTLINE_MAX_EDGES) return false;
    if (from.y == to.y) return true;
    output->edges[output->count++] = (OutlineEdge){
        from.x * output->scale - output->x_origin,
        output->y_origin - from.y * output->scale,
        to.x * output->scale - output->x_origin,
        output->y_origin - to.y * output->scale
    };
    return true;
}

static bool add_curve(OutlineEdges *output, OutlinePoint from,
                      OutlinePoint control, OutlinePoint to,
                      unsigned depth) {
    float deviation = hypotf(from.x - 2.0f * control.x + to.x,
                             from.y - 2.0f * control.y + to.y) *
                      output->scale;
    if (depth == 8 || deviation < 0.25f)
        return add_line(output, from, to);
    OutlinePoint first = midpoint(from, control);
    OutlinePoint second = midpoint(control, to);
    OutlinePoint center = midpoint(first, second);
    return add_curve(output, from, first, center, depth + 1) &&
           add_curve(output, center, second, to, depth + 1);
}

static bool shape_edges(const OutlineShape *shape, OutlineEdges *edges) {
    unsigned first = 0;
    for (unsigned contour = 0; contour < shape->contour_count; contour++) {
        unsigned end = shape->end_points[contour];
        unsigned count = end - first + 1;
        if (!count) return false;
        OutlinePoint begin = shape->points[first];
        OutlinePoint last = shape->points[end];
        OutlinePoint current = begin.on_curve ? begin :
            last.on_curve ? last : midpoint(begin, last);
        OutlinePoint start = current;
        for (unsigned step = begin.on_curve ? 1 : 0; step < count;) {
            OutlinePoint point = shape->points[first + step % count];
            if (point.on_curve) {
                if (!add_line(edges, current, point)) return false;
                current = point;
                step++;
            } else {
                OutlinePoint next =
                    shape->points[first + (step + 1) % count];
                OutlinePoint target = next.on_curve
                    ? next : midpoint(point, next);
                if (!add_curve(edges, current, point, target, 0))
                    return false;
                current = target;
                step += next.on_curve ? 2 : 1;
            }
        }
        if (!add_line(edges, current, start)) return false;
        first = end + 1;
    }
    return first == shape->point_count;
}

static bool inside_shape(const OutlineEdges *shape, float x, float y) {
    int winding = 0;
    for (unsigned index = 0; index < shape->count; index++) {
        const OutlineEdge *edge = &shape->edges[index];
        if (edge->y0 <= y && edge->y1 > y) {
            float crossing = edge->x0 + (y - edge->y0) *
                             (edge->x1 - edge->x0) /
                             (edge->y1 - edge->y0);
            if (crossing > x) winding++;
        } else if (edge->y1 <= y && edge->y0 > y) {
            float crossing = edge->x0 + (y - edge->y0) *
                             (edge->x1 - edge->x0) /
                             (edge->y1 - edge->y0);
            if (crossing > x) winding--;
        }
    }
    return winding != 0;
}

bool wm_outline_font_raster(const WmOutlineFont *font, uint32_t codepoint,
                            unsigned pixel_size, WmOutlineBitmap *bitmap) {
    if (!font || !bitmap || pixel_size < 8 ||
        pixel_size > OUTLINE_MAX_PIXEL_SIZE) return false;
    memset(bitmap, 0, sizeof(*bitmap));
    unsigned glyph = glyph_for_codepoint(font, codepoint);
    if (!glyph && codepoint != 0) glyph = glyph_for_codepoint(font, '?');
    bitmap->advance = glyph_advance(font, glyph, pixel_size);
    size_t offset, length;
    if (!glyph_range(font, glyph, &offset, &length)) return false;
    if (!length) return true;
    if (length < 10) return false;
    const uint8_t *source = font->bytes + offset;
    float scale = (float)pixel_size / font->units_per_em;
    int left = (int)floorf(signed16(source + 2) * scale);
    int right = (int)ceilf(signed16(source + 6) * scale);
    int bottom = (int)floorf(signed16(source + 4) * scale);
    int top = (int)ceilf(signed16(source + 8) * scale);
    if (right < left || top < bottom || right - left > 128 ||
        top - bottom > 128) return false;
    bitmap->left = left;
    bitmap->top = top;
    bitmap->width = (unsigned)(right - left);
    bitmap->height = (unsigned)(top - bottom);
    if (!bitmap->width || !bitmap->height) return true;
    bitmap->alpha = calloc((size_t)bitmap->width * bitmap->height, 1);
    if (!bitmap->alpha) return false;
    OutlineShape *shape = calloc(1, sizeof(*shape));
    OutlineEdges *edges = calloc(1, sizeof(*edges));
    if (!shape || !edges || !append_glyph(font, glyph, shape, 0)) {
        free(shape);
        free(edges);
        wm_outline_bitmap_free(bitmap);
        return false;
    }
    edges->scale = scale;
    edges->x_origin = (float)left;
    edges->y_origin = (float)top;
    bool valid = shape_edges(shape, edges);
    if (valid) {
        for (unsigned y = 0; y < bitmap->height; y++) {
            for (unsigned x = 0; x < bitmap->width; x++) {
                unsigned covered = 0;
                for (unsigned sample_y = 0; sample_y < 4; sample_y++) {
                    for (unsigned sample_x = 0; sample_x < 4;
                         sample_x++) {
                        if (inside_shape(edges,
                                x + (sample_x + 0.5f) * 0.25f,
                                y + (sample_y + 0.5f) * 0.25f))
                            covered++;
                    }
                }
                bitmap->alpha[(size_t)y * bitmap->width + x] =
                    (uint8_t)((covered * 255 + 8) / 16);
            }
        }
    }
    free(shape);
    free(edges);
    if (!valid) wm_outline_bitmap_free(bitmap);
    return valid;
}

void wm_outline_bitmap_free(WmOutlineBitmap *bitmap) {
    if (!bitmap) return;
    free(bitmap->alpha);
    memset(bitmap, 0, sizeof(*bitmap));
}

static uint32_t next_codepoint(const char **text) {
    const unsigned char *bytes = (const unsigned char *)*text;
    uint32_t codepoint = bytes[0];
    if (!codepoint) return 0;
    if (codepoint < 0x80) {
        *text += 1;
        return codepoint;
    }
    if (codepoint >= 0xc2 && codepoint <= 0xdf &&
        (bytes[1] & 0xc0) == 0x80) {
        *text += 2;
        return ((codepoint & 0x1f) << 6) | (bytes[1] & 0x3f);
    }
    if (codepoint >= 0xe0 && codepoint <= 0xef && bytes[1] &&
        (bytes[1] & 0xc0) == 0x80 && bytes[2] &&
        (bytes[2] & 0xc0) == 0x80) {
        *text += 3;
        return ((codepoint & 0x0f) << 12) |
               ((uint32_t)(bytes[1] & 0x3f) << 6) | (bytes[2] & 0x3f);
    }
    *text += 1;
    return '?';
}

float wm_outline_font_text_width(const WmOutlineFont *font,
                                 const char *utf8, unsigned pixel_size) {
    if (!font || !utf8 || !pixel_size) return 0;
    float width = 0;
    const char *cursor = utf8;
    while (*cursor) {
        uint32_t codepoint = next_codepoint(&cursor);
        unsigned glyph = glyph_for_codepoint(font, codepoint);
        if (!glyph && codepoint) glyph = glyph_for_codepoint(font, '?');
        width += glyph_advance(font, glyph, pixel_size);
    }
    return width;
}

#ifndef WM_OUTLINE_FONT_PARSE_ONLY
static bool populate_atlas(const WmOutlineFont *font, OutlineAtlas *atlas,
                           uint8_t *rgba, unsigned dimension) {
    unsigned x = 1, y = 1, row_height = 0;
    for (unsigned index = 0; index < OUTLINE_CODEPOINT_COUNT; index++) {
        WmOutlineBitmap bitmap;
        if (!wm_outline_font_raster(font,
                                    OUTLINE_FIRST_CODEPOINT + index,
                                    atlas->pixel_size, &bitmap)) return false;
        unsigned ink_width = bitmap.width +
                             (atlas->synthetic_bold && bitmap.width ? 1u : 0u);
        unsigned cell_width = ink_width + 2;
        unsigned cell_height = bitmap.height + 2;
        if (x + cell_width > dimension) {
            x = 1;
            y += row_height;
            row_height = 0;
        }
        if (y + cell_height > dimension) {
            wm_outline_bitmap_free(&bitmap);
            return false;
        }
        AtlasGlyph *glyph = &atlas->glyphs[index];
        glyph->advance = bitmap.advance;
        glyph->left = bitmap.left;
        glyph->top = bitmap.top;
        glyph->width = ink_width;
        glyph->height = bitmap.height;
        glyph->u0 = (float)x / dimension;
        glyph->v0 = (float)y / dimension;
        glyph->u1 = (float)(x + ink_width) / dimension;
        glyph->v1 = (float)(y + bitmap.height) / dimension;
        for (unsigned row = 0; row < bitmap.height; row++) {
            for (unsigned column = 0; column < ink_width; column++) {
                size_t pixel = ((size_t)(y + row) * dimension +
                                x + column) * 4;
                uint8_t coverage = column < bitmap.width
                    ? bitmap.alpha[(size_t)row * bitmap.width + column]
                    : 0;
                if (atlas->synthetic_bold && column > 0) {
                    uint8_t previous = bitmap.alpha[
                        (size_t)row * bitmap.width + column - 1];
                    if (previous > coverage) coverage = previous;
                }
                rgba[pixel] = 255;
                rgba[pixel + 1] = 255;
                rgba[pixel + 2] = 255;
                rgba[pixel + 3] = coverage;
            }
        }
        x += cell_width;
        if (cell_height > row_height) row_height = cell_height;
        wm_outline_bitmap_free(&bitmap);
    }
    return true;
}

static OutlineAtlas *atlas_for_size(WmOutlineFont *font,
                                    WmPlatform *platform,
                                    unsigned pixel_size, bool synthetic_bold) {
    for (unsigned index = 0; index < font->atlas_count; index++) {
        if (font->atlases[index].pixel_size == pixel_size &&
            font->atlases[index].synthetic_bold == synthetic_bold)
            return &font->atlases[index];
    }
    if (font->atlas_count == OUTLINE_MAX_ATLASES ||
        pixel_size < 8 || pixel_size > OUTLINE_MAX_PIXEL_SIZE) return NULL;
    OutlineAtlas atlas = {
        .pixel_size = pixel_size,
        .synthetic_bold = synthetic_bold
    };
    unsigned dimension = pixel_size <= 32 ? 512 : 1024;
    for (; dimension <= 2048; dimension *= 2) {
        uint8_t *rgba = calloc((size_t)dimension * dimension, 4);
        if (!rgba) return NULL;
        bool complete = populate_atlas(font, &atlas, rgba, dimension);
        if (complete) {
            atlas.texture = wm_platform_create_texture(platform,
                                                        (int)dimension,
                                                        (int)dimension,
                                                        rgba);
            atlas.dimension = dimension;
        }
        free(rgba);
        if (complete) break;
    }
    if (!atlas.texture) return NULL;
    font->atlases[font->atlas_count] = atlas;
    return &font->atlases[font->atlas_count++];
}

bool wm_outline_font_draw_line(WmOutlineFont *font, WmPlatform *platform,
                                const char *utf8, unsigned pixel_size,
                                float x, float top, WmFontAlign align,
                                float x_scale, float x_offset,
                                WmColor color, bool synthetic_bold) {
    if (!font || !platform || !utf8 || !isfinite(x) || !isfinite(top) ||
        !isfinite(x_scale) || !isfinite(x_offset) || x_scale <= 0)
        return false;
    OutlineAtlas *atlas = atlas_for_size(font, platform, pixel_size,
                                         synthetic_bold);
    if (!atlas) return false;
    float pen = x;
    float width = wm_outline_font_text_width(font, utf8, pixel_size);
    if (align == WM_FONT_ALIGN_CENTER) pen -= width * 0.5f;
    else if (align == WM_FONT_ALIGN_RIGHT) pen -= width;
    float baseline = top + (float)font->ascent * pixel_size /
                           font->units_per_em;
    const char *cursor = utf8;
    while (*cursor) {
        uint32_t codepoint = next_codepoint(&cursor);
        if (codepoint < OUTLINE_FIRST_CODEPOINT ||
            codepoint >= OUTLINE_FIRST_CODEPOINT + OUTLINE_CODEPOINT_COUNT)
            codepoint = '?';
        const AtlasGlyph *glyph =
            &atlas->glyphs[codepoint - OUTLINE_FIRST_CODEPOINT];
        if (glyph->width && glyph->height) {
            WmQuad quad = {
                .x = (pen + glyph->left) * x_scale + x_offset,
                .y = baseline - glyph->top,
                .width = glyph->width * x_scale,
                .height = glyph->height,
                .u0 = glyph->u0, .v0 = glyph->v0,
                .u1 = glyph->u1, .v1 = glyph->v1,
                .color = color,
                .texture = atlas->texture
            };
            wm_platform_draw_quad(platform, &quad);
        }
        pen += glyph->advance;
    }
    return true;
}
#endif
