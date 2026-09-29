#include "resource_font_internal.h"

#include <math.h>
#include <stdlib.h>
#include <string.h>

enum {
    WM_FONT_MAX_TEXT_BYTES = 65536,
    WM_FONT_MAX_LINES = 4096
};

typedef struct FontLine {
    size_t first_byte;
    size_t byte_count;
    float width;
    float x;
    float y;
} FontLine;

struct WmFontTextLayout {
    const WmFont *font;
    char *text;
    WmFontPane pane;
    FontLine *lines;
    size_t line_count;
    size_t line_capacity;
};

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

bool wm_font_text_layout_move_caret_vertical(
    const WmFontTextLayout *layout, size_t from_byte, bool up,
    float preferred_x, size_t *to_byte) {
    if (!layout || !layout->line_count || !to_byte ||
        !isfinite(preferred_x)) return false;
    size_t text_bytes = strlen(layout->text);
    if (from_byte > text_bytes) from_byte = text_bytes;
    while (from_byte > 0 && from_byte < text_bytes &&
           ((unsigned char)layout->text[from_byte] & 0xc0u) == 0x80u)
        from_byte--;

    size_t current = 0;
    for (size_t index = 1; index < layout->line_count; index++) {
        if (layout->lines[index].first_byte > from_byte) break;
        current = index;
    }
    float current_y = layout->lines[current].y;
    size_t target = layout->line_count;
    for (size_t index = 0; index < layout->line_count; index++) {
        float y = layout->lines[index].y;
        bool adjacent = up ? y > current_y + 0.5f
                           : y < current_y - 0.5f;
        if (!adjacent) continue;
        if (target == layout->line_count ||
            (up ? y < layout->lines[target].y
                : y > layout->lines[target].y))
            target = index;
    }
    if (target == layout->line_count) return false;

    const FontLine *line = &layout->lines[target];
    size_t position = line->first_byte;
    size_t end = position + line->byte_count;
    size_t nearest = position;
    float nearest_distance = fabsf(line->x - preferred_x);
    float prefix_width = 0.0f;
    size_t characters = 0;
    float scale = layout->pane.font_size[0] / layout->font->metrics.width;
    while (position < end) {
        uint32_t codepoint = next_codepoint(layout->text, end, &position);
        const WmFontGlyph *glyph = wm_font_glyph(layout->font, codepoint);
        if (characters++) prefix_width += layout->pane.char_space;
        if (glyph) prefix_width += glyph->advance * scale;
        /* At an automatic wrap, this boundary belongs to the next line. */
        if (target + 1 < layout->line_count &&
            position == layout->lines[target + 1].first_byte)
            continue;
        float x = line->x + prefix_width + layout->pane.char_space;
        float distance = fabsf(x - preferred_x);
        if (distance < nearest_distance) {
            nearest_distance = distance;
            nearest = position;
        }
    }
    *to_byte = nearest;
    return true;
}

bool wm_font_text_layout_hit_caret(const WmFontTextLayout *layout,
                                   float x, float y, size_t *byte_index) {
    if (!layout || !layout->line_count || !byte_index ||
        !isfinite(x) || !isfinite(y)) return false;
    const WmFontPane *pane = &layout->pane;
    float scale = pane->font_size[0] / layout->font->metrics.width;
    float line_height = layout->font->metrics.line_feed *
        pane->font_size[1] / layout->font->metrics.height + pane->line_space;
    const FontLine *line = &layout->lines[0];
    float nearest_y = fabsf(y - (line->y - line_height * 0.5f));
    for (size_t index = 1; index < layout->line_count; index++) {
        const FontLine *candidate = &layout->lines[index];
        float distance = fabsf(y - (candidate->y - line_height * 0.5f));
        if (distance < nearest_y) {
            nearest_y = distance;
            line = candidate;
        }
    }
    size_t position = line->first_byte;
    size_t end = position + line->byte_count;
    size_t nearest = position;
    float caret_x = line->x;
    float nearest_x = fabsf(x - caret_x);
    while (position < end) {
        uint32_t codepoint = next_codepoint(layout->text, end, &position);
        const WmFontGlyph *glyph = wm_font_glyph(layout->font, codepoint);
        caret_x += (glyph ? glyph->advance * scale : 0.0f) + pane->char_space;
        float distance = fabsf(x - caret_x);
        if (distance < nearest_x) {
            nearest_x = distance;
            nearest = position;
        }
    }
    *byte_index = nearest;
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
