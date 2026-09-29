#include "cff_font.h"

#include <math.h>
#include <stdlib.h>
#include <string.h>

enum {
    CFF_MAX_FONTS = 64,
    CFF_MAX_STACK = 48,
    CFF_MAX_SUBR_DEPTH = 10,
    CFF_MAX_PROGRAM_BYTES = 32768,
    CFF_MAX_STEPS = 200000
};

typedef struct CffIndex {
    size_t offsets;
    size_t data;
    size_t end;
    unsigned count;
    unsigned offset_size;
} CffIndex;

typedef struct CffPrivate {
    CffIndex subrs;
} CffPrivate;

typedef struct CffDictFields {
    size_t charstrings;
    size_t fd_array;
    size_t fd_select;
    size_t private_offset;
    size_t private_size;
    size_t subrs;
} CffDictFields;

struct WmCffFont {
    const uint8_t *bytes;
    size_t table_size;
    unsigned glyph_count;
    CffIndex charstrings;
    CffIndex global_subrs;
    CffPrivate *private_dicts;
    unsigned private_count;
    uint8_t *font_for_glyph;
};

typedef struct Type2State {
    const WmCffFont *font;
    WmCffGlyph *glyph;
    const CffIndex *local_subrs;
    float stack[CFF_MAX_STACK];
    unsigned stack_count;
    unsigned stems;
    unsigned steps;
    float x;
    float y;
    bool width_seen;
    bool stopped;
} Type2State;

static bool fits(size_t size, size_t offset, size_t length) {
    return offset <= size && length <= size - offset;
}

static unsigned be16(const uint8_t *bytes) {
    return ((unsigned)bytes[0] << 8) | bytes[1];
}

static uint32_t be32(const uint8_t *bytes) {
    return ((uint32_t)bytes[0] << 24) | ((uint32_t)bytes[1] << 16) |
           ((uint32_t)bytes[2] << 8) | bytes[3];
}

static bool offset_value(const uint8_t *bytes, size_t size,
                         size_t position, unsigned width, size_t *value) {
    if (width < 1 || width > 4 || !fits(size, position, width)) return false;
    size_t result = 0;
    for (unsigned index = 0; index < width; index++)
        result = (result << 8) | bytes[position + index];
    *value = result;
    return true;
}

static bool parse_index(const uint8_t *bytes, size_t size, size_t offset,
                        CffIndex *index) {
    if (!fits(size, offset, 2)) return false;
    unsigned count = be16(bytes + offset);
    *index = (CffIndex){.count = count, .end = offset + 2};
    if (!count) return true;
    if (!fits(size, offset + 2, 1)) return false;
    unsigned width = bytes[offset + 2];
    if (width < 1 || width > 4 ||
        !fits(size, offset + 3, ((size_t)count + 1) * width)) return false;
    size_t data = offset + 3 + ((size_t)count + 1) * width;
    size_t first, last;
    if (!offset_value(bytes, size, offset + 3, width, &first) ||
        !offset_value(bytes, size, offset + 3 + (size_t)count * width,
                      width, &last) || first != 1 || last < first ||
        !fits(size, data, last - 1)) return false;
    size_t previous = first;
    for (unsigned item = 1; item <= count; item++) {
        size_t current;
        if (!offset_value(bytes, size,
                          offset + 3 + (size_t)item * width,
                          width, &current) || current < previous ||
            current > last) return false;
        previous = current;
    }
    index->offsets = offset + 3;
    index->data = data;
    index->end = data + last - 1;
    index->offset_size = width;
    return true;
}

static bool index_item(const uint8_t *bytes, size_t size,
                       const CffIndex *index, unsigned item,
                       size_t *offset, size_t *length) {
    if (item >= index->count) return false;
    size_t first, last;
    size_t position = index->offsets + (size_t)item * index->offset_size;
    if (!offset_value(bytes, size, position, index->offset_size, &first) ||
        !offset_value(bytes, size, position + index->offset_size,
                      index->offset_size, &last) ||
        first < 1 || last < first ||
        !fits(index->end, index->data + first - 1, last - first)) return false;
    *offset = index->data + first - 1;
    *length = last - first;
    return true;
}

static bool number(const uint8_t *bytes, size_t size, size_t *cursor,
                   float *value) {
    if (!fits(size, *cursor, 1)) return false;
    uint8_t first = bytes[(*cursor)++];
    if (first >= 32 && first <= 246) {
        *value = (float)((int)first - 139);
    } else if (first >= 247 && first <= 250) {
        if (!fits(size, *cursor, 1)) return false;
        *value = (float)((first - 247) * 256 + bytes[(*cursor)++] + 108);
    } else if (first >= 251 && first <= 254) {
        if (!fits(size, *cursor, 1)) return false;
        *value = (float)(-((int)first - 251) * 256 - bytes[(*cursor)++] - 108);
    } else if (first == 28) {
        if (!fits(size, *cursor, 2)) return false;
        *value = (float)(int16_t)be16(bytes + *cursor);
        *cursor += 2;
    } else if (first == 29) {
        if (!fits(size, *cursor, 4)) return false;
        *value = (float)(int32_t)be32(bytes + *cursor);
        *cursor += 4;
    } else if (first == 255) {
        if (!fits(size, *cursor, 4)) return false;
        *value = (float)(int32_t)be32(bytes + *cursor) / 65536.0f;
        *cursor += 4;
    } else if (first == 30) {
        /* Real numbers are used by FontMatrix, not the offsets we need. */
        bool ended = false;
        while (fits(size, *cursor, 1)) {
            uint8_t pair = bytes[(*cursor)++];
            if ((pair >> 4) == 15 || (pair & 15) == 15) {
                ended = true;
                break;
            }
        }
        if (!ended) return false;
        *value = NAN;
    } else {
        return false;
    }
    return true;
}

static bool dict_offset(float value, size_t limit, size_t *output) {
    if (!isfinite(value) || value < 0 || value > (float)limit ||
        floorf(value) != value) return false;
    *output = (size_t)value;
    return true;
}

static bool parse_dict(const uint8_t *bytes, size_t size, size_t offset,
                       size_t length, CffDictFields *fields) {
    if (!fits(size, offset, length)) return false;
    float operands[CFF_MAX_STACK];
    unsigned count = 0;
    size_t cursor = offset;
    while (cursor < offset + length) {
        unsigned op = bytes[cursor];
        if (op >= 32 || op == 28 || op == 29 || op == 30) {
            if (count == CFF_MAX_STACK ||
                !number(bytes, offset + length, &cursor, &operands[count]))
                return false;
            count++;
            continue;
        }
        cursor++;
        if (op == 12) {
            if (cursor == offset + length) return false;
            op = 0x100u | bytes[cursor++];
        }
        if (op == 17 && count == 1) {
            if (!dict_offset(operands[0], size, &fields->charstrings))
                return false;
        } else if (op == 0x124 && count == 1) {
            if (!dict_offset(operands[0], size, &fields->fd_array))
                return false;
        } else if (op == 0x125 && count == 1) {
            if (!dict_offset(operands[0], size, &fields->fd_select))
                return false;
        } else if (op == 18 && count == 2) {
            if (!dict_offset(operands[0], size, &fields->private_size) ||
                !dict_offset(operands[1], size, &fields->private_offset))
                return false;
        } else if (op == 19 && count == 1) {
            if (!dict_offset(operands[0], size, &fields->subrs))
                return false;
        }
        count = 0;
    }
    return count == 0;
}

static bool parse_private(WmCffFont *font, CffPrivate *output,
                          size_t offset, size_t length) {
    if (!length) return true;
    CffDictFields fields = {0};
    if (!parse_dict(font->bytes, font->table_size, offset, length, &fields))
        return false;
    if (fields.subrs) {
        if (fields.subrs > font->table_size - offset ||
            !parse_index(font->bytes, font->table_size,
                         offset + fields.subrs, &output->subrs)) return false;
    }
    return true;
}

static bool parse_fd_select(WmCffFont *font, size_t offset) {
    const uint8_t *bytes = font->bytes;
    size_t size = font->table_size;
    if (!fits(size, offset, 1)) return false;
    unsigned format = bytes[offset++];
    if (format == 0) {
        if (!fits(size, offset, font->glyph_count)) return false;
        memcpy(font->font_for_glyph, bytes + offset, font->glyph_count);
    } else if (format == 3) {
        if (!fits(size, offset, 2)) return false;
        unsigned ranges = be16(bytes + offset);
        offset += 2;
        if (!ranges || !fits(size, offset, (size_t)ranges * 3 + 2))
            return false;
        unsigned previous = be16(bytes + offset);
        if (previous != 0) return false;
        for (unsigned index = 0; index < ranges; index++) {
            unsigned first = be16(bytes + offset + (size_t)index * 3);
            unsigned next = be16(bytes + offset + (size_t)(index + 1) * 3);
            if (index + 1 == ranges)
                next = be16(bytes + offset + (size_t)ranges * 3);
            unsigned fd = bytes[offset + (size_t)index * 3 + 2];
            if (first != previous || next <= first ||
                next > font->glyph_count || fd >= font->private_count)
                return false;
            memset(font->font_for_glyph + first, (int)fd, next - first);
            previous = next;
        }
        if (previous != font->glyph_count) return false;
    } else {
        return false;
    }
    for (unsigned glyph = 0; glyph < font->glyph_count; glyph++) {
        if (font->font_for_glyph[glyph] >= font->private_count) return false;
    }
    return true;
}

WmCffFont *wm_cff_font_parse(const uint8_t *bytes, size_t size,
                             size_t table_offset, size_t table_size,
                             unsigned glyph_count) {
    if (!bytes || !glyph_count || !fits(size, table_offset, table_size) ||
        table_size < 4) return NULL;
    WmCffFont *font = calloc(1, sizeof(*font));
    if (!font) return NULL;
    font->bytes = bytes + table_offset;
    font->table_size = table_size;
    font->glyph_count = glyph_count;
    const uint8_t *table = font->bytes;
    size_t cursor = table[2];
    CffIndex names, top, strings;
    bool valid = table[0] == 1 && cursor >= 4 && cursor <= table_size &&
        parse_index(table, table_size, cursor, &names);
    if (valid) {
        cursor = names.end;
        valid = parse_index(table, table_size, cursor, &top) && top.count == 1;
    }
    if (valid) {
        cursor = top.end;
        valid = parse_index(table, table_size, cursor, &strings);
    }
    if (valid) {
        cursor = strings.end;
        valid = parse_index(table, table_size, cursor, &font->global_subrs);
    }
    size_t top_offset = 0, top_size = 0;
    CffDictFields fields = {0};
    if (valid) valid = index_item(table, table_size, &top, 0,
                                 &top_offset, &top_size) &&
                       parse_dict(table, table_size, top_offset, top_size,
                                  &fields) && fields.charstrings &&
                       parse_index(table, table_size, fields.charstrings,
                                   &font->charstrings) &&
                       font->charstrings.count == glyph_count;
    if (valid) {
        valid = fields.fd_array == 0 || fields.fd_select != 0;
        CffIndex fd_array = {0};
        if (valid && fields.fd_array)
            valid = parse_index(table, table_size, fields.fd_array,
                                &fd_array) && fd_array.count &&
                    fd_array.count <= CFF_MAX_FONTS;
        if (valid) {
            font->private_count = fields.fd_array ? fd_array.count : 1;
            font->private_dicts = calloc(font->private_count,
                                         sizeof(*font->private_dicts));
            font->font_for_glyph = calloc(glyph_count, 1);
            valid = font->private_dicts && font->font_for_glyph;
        }
        if (valid && fields.fd_array) {
            for (unsigned fd = 0; fd < fd_array.count && valid; fd++) {
                size_t offset, length;
                CffDictFields face = {0};
                valid = index_item(table, table_size, &fd_array, fd,
                                   &offset, &length) &&
                        parse_dict(table, table_size, offset, length,
                                   &face) &&
                        parse_private(font, &font->private_dicts[fd],
                                      face.private_offset,
                                      face.private_size);
            }
            if (valid) valid = parse_fd_select(font, fields.fd_select);
        } else if (valid) {
            valid = parse_private(font, font->private_dicts,
                                  fields.private_offset, fields.private_size);
        }
    }
    if (!valid) {
        wm_cff_font_destroy(font);
        return NULL;
    }
    return font;
}

void wm_cff_font_destroy(WmCffFont *font) {
    if (!font) return;
    free(font->private_dicts);
    free(font->font_for_glyph);
    free(font);
}

static bool emit(Type2State *state, WmCffSegmentKind kind,
                 WmCffPoint first, WmCffPoint second, WmCffPoint end) {
    if (state->glyph->count == WM_CFF_MAX_SEGMENTS ||
        !isfinite(end.x) || !isfinite(end.y) ||
        fabsf(end.x) > 100000.0f || fabsf(end.y) > 100000.0f)
        return false;
    state->glyph->segments[state->glyph->count++] =
        (WmCffSegment){kind, first, second, end};
    state->x = end.x;
    state->y = end.y;
    return true;
}

static bool move(Type2State *state, float dx, float dy) {
    WmCffPoint end = {state->x + dx, state->y + dy};
    return emit(state, WM_CFF_MOVE, end, end, end);
}

static bool line(Type2State *state, float dx, float dy) {
    WmCffPoint end = {state->x + dx, state->y + dy};
    return emit(state, WM_CFF_LINE, end, end, end);
}

static bool curve(Type2State *state, float dx1, float dy1,
                  float dx2, float dy2, float dx3, float dy3) {
    WmCffPoint first = {state->x + dx1, state->y + dy1};
    WmCffPoint second = {first.x + dx2, first.y + dy2};
    WmCffPoint end = {second.x + dx3, second.y + dy3};
    return emit(state, WM_CFF_CUBIC, first, second, end);
}

static int subr_bias(unsigned count) {
    return count < 1240 ? 107 : count < 33900 ? 1131 : 32768;
}

static bool run_program(Type2State *state, const uint8_t *bytes,
                        size_t length, unsigned depth);

static bool call_subr(Type2State *state, const CffIndex *index,
                      unsigned depth) {
    if (!state->stack_count || depth >= CFF_MAX_SUBR_DEPTH) return false;
    float operand = state->stack[--state->stack_count];
    if (!isfinite(operand) || floorf(operand) != operand) return false;
    int item = (int)operand + subr_bias(index->count);
    if (item < 0 || (unsigned)item >= index->count) return false;
    size_t offset, length;
    if (!index_item(state->font->bytes, state->font->table_size, index,
                    (unsigned)item, &offset, &length)) return false;
    return run_program(state, state->font->bytes + offset, length, depth + 1);
}

static bool stems(Type2State *state) {
    unsigned first = 0;
    if (!state->width_seen && (state->stack_count & 1)) first = 1;
    if ((state->stack_count - first) & 1) return false;
    state->stems += (state->stack_count - first) / 2;
    if (state->stems > 96) return false;
    state->width_seen = true;
    state->stack_count = 0;
    return true;
}

static bool draw_operator(Type2State *state, unsigned op) {
    const float *a = state->stack;
    unsigned n = state->stack_count;
    unsigned cursor = 0;
    bool valid = true;
    if (op == 1 || op == 3 || op == 18 || op == 23)
        return stems(state);
    if (op == 4 || op == 21 || op == 22) {
        unsigned needed = op == 21 ? 2 : 1;
        if (!state->width_seen && n == needed + 1) cursor = 1;
        valid = n - cursor == needed &&
                move(state, op == 4 ? 0 : a[cursor],
                     op == 22 ? 0 : a[cursor + (op == 21)]);
        state->width_seen = true;
    } else if (op == 5) {
        valid = n >= 2 && !(n & 1);
        for (; valid && cursor < n; cursor += 2)
            valid = line(state, a[cursor], a[cursor + 1]);
    } else if (op == 6 || op == 7) {
        valid = n > 0;
        for (; valid && cursor < n; cursor++) {
            bool horizontal = ((cursor & 1) == 0) == (op == 6);
            valid = line(state, horizontal ? a[cursor] : 0,
                         horizontal ? 0 : a[cursor]);
        }
    } else if (op == 8) {
        valid = n >= 6 && n % 6 == 0;
        for (; valid && cursor < n; cursor += 6)
            valid = curve(state, a[cursor], a[cursor + 1],
                          a[cursor + 2], a[cursor + 3],
                          a[cursor + 4], a[cursor + 5]);
    } else if (op == 24) {
        valid = n >= 8 && (n - 2) % 6 == 0;
        for (; valid && cursor + 8 <= n; cursor += 6)
            valid = curve(state, a[cursor], a[cursor + 1],
                          a[cursor + 2], a[cursor + 3],
                          a[cursor + 4], a[cursor + 5]);
        if (valid) valid = line(state, a[n - 2], a[n - 1]);
    } else if (op == 25) {
        valid = n >= 8 && (n - 6) % 2 == 0;
        for (; valid && cursor + 6 < n; cursor += 2)
            valid = line(state, a[cursor], a[cursor + 1]);
        if (valid) valid = curve(state, a[n - 6], a[n - 5], a[n - 4],
                                 a[n - 3], a[n - 2], a[n - 1]);
    } else if (op == 26 || op == 27) {
        unsigned extra = n & 1;
        valid = n >= 4 && (n - extra) % 4 == 0;
        for (cursor = extra; valid && cursor < n; cursor += 4) {
            if (op == 26)
                valid = curve(state, cursor == extra && extra ? a[0] : 0,
                              a[cursor], a[cursor + 1], a[cursor + 2],
                              0, a[cursor + 3]);
            else
                valid = curve(state, a[cursor],
                              cursor == extra && extra ? a[0] : 0,
                              a[cursor + 1], a[cursor + 2],
                              a[cursor + 3], 0);
        }
    } else if (op == 30 || op == 31) {
        valid = n >= 4 && (n % 4 == 0 || n % 4 == 1);
        bool vertical = op == 30;
        for (; valid && cursor + 4 <= n; cursor += 4) {
            bool extra = cursor + 5 == n;
            if (vertical)
                valid = curve(state, 0, a[cursor], a[cursor + 1],
                              a[cursor + 2], a[cursor + 3],
                              extra ? a[cursor + 4] : 0);
            else
                valid = curve(state, a[cursor], 0, a[cursor + 1],
                              a[cursor + 2], extra ? a[cursor + 4] : 0,
                              a[cursor + 3]);
            vertical = !vertical;
        }
    } else {
        valid = false;
    }
    state->stack_count = 0;
    return valid;
}

static bool run_program(Type2State *state, const uint8_t *bytes,
                        size_t length, unsigned depth) {
    if (length > CFF_MAX_PROGRAM_BYTES) return false;
    size_t cursor = 0;
    while (cursor < length && !state->stopped) {
        if (++state->steps > CFF_MAX_STEPS) return false;
        unsigned op = bytes[cursor];
        if (op >= 32 || op == 28 || op == 255) {
            if (state->stack_count == CFF_MAX_STACK ||
                !number(bytes, length, &cursor,
                        &state->stack[state->stack_count])) return false;
            state->stack_count++;
            continue;
        }
        cursor++;
        if (op == 10 || op == 29) {
            const CffIndex *index = op == 10 ? state->local_subrs :
                                            &state->font->global_subrs;
            if (!call_subr(state, index, depth)) return false;
        } else if (op == 11) {
            return depth > 0;
        } else if (op == 14) {
            if (state->stack_count > 1) return false;
            state->stack_count = 0;
            state->stopped = true;
        } else if (op == 19 || op == 20) {
            if (!stems(state)) return false;
            size_t mask_bytes = (state->stems + 7) / 8;
            if (!fits(length, cursor, mask_bytes)) return false;
            cursor += mask_bytes;
        } else if (op == 12) {
            /* These glyphs use only the basic Type 2 path operators. Reject
             * unsupported escaped instructions instead of drawing garbage. */
            return false;
        } else if (!draw_operator(state, op)) {
            return false;
        }
    }
    return depth > 0 || state->stopped;
}

bool wm_cff_font_glyph(const WmCffFont *font, unsigned glyph_index,
                       WmCffGlyph *glyph) {
    if (!font || !glyph || glyph_index >= font->glyph_count) return false;
    memset(glyph, 0, sizeof(*glyph));
    size_t offset, length;
    if (!index_item(font->bytes, font->table_size, &font->charstrings,
                    glyph_index, &offset, &length)) return false;
    unsigned fd = font->font_for_glyph[glyph_index];
    Type2State state = {
        .font = font,
        .glyph = glyph,
        .local_subrs = &font->private_dicts[fd].subrs
    };
    return run_program(&state, font->bytes + offset, length, 0);
}
