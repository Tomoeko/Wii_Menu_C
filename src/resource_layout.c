#include "wii_menu/resource_layout.h"

#include <math.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

enum {
    WM_RESOURCE_MAX_SECTIONS = 2048,
    WM_RESOURCE_MAX_PANES = 4096,
    WM_RESOURCE_MAX_ITEMS = 8192,
    WM_RESOURCE_MAX_JSON = 32 * 1024 * 1024
};

typedef struct WmSection {
    const uint8_t *bytes;
    size_t size;
    char kind[5];
} WmSection;

typedef struct WmJsonWriter {
    char *text;
    size_t length;
    size_t capacity;
    bool failed;
} WmJsonWriter;

typedef struct WmPaneRecord {
    WmSection section;
    int first_child;
    int last_child;
    int next_sibling;
} WmPaneRecord;

static uint16_t wm_be16(const uint8_t *bytes)
{
    return (uint16_t)(((uint16_t)bytes[0] << 8) | bytes[1]);
}

static int16_t wm_signed_be16(const uint8_t *bytes)
{
    return (int16_t)wm_be16(bytes);
}

static uint32_t wm_be32(const uint8_t *bytes)
{
    return ((uint32_t)bytes[0] << 24) | ((uint32_t)bytes[1] << 16) |
           ((uint32_t)bytes[2] << 8) | bytes[3];
}

static bool wm_range(size_t size, size_t offset, size_t length)
{
    return offset <= size && length <= size - offset;
}

static void wm_set_error(char *error, size_t error_size, const char *message)
{
    if (error != NULL && error_size != 0) {
        snprintf(error, error_size, "%s", message);
    }
}

static bool wm_float(const uint8_t *bytes, size_t size, size_t offset, float *value)
{
    if (!wm_range(size, offset, 4)) {
        return false;
    }
    uint32_t bits = wm_be32(bytes + offset);
    memcpy(value, &bits, sizeof(bits));
    return isfinite(*value);
}

static bool wm_writer_reserve(WmJsonWriter *writer, size_t additional)
{
    if (writer->failed || additional > WM_RESOURCE_MAX_JSON - writer->length) {
        writer->failed = true;
        return false;
    }
    size_t needed = writer->length + additional + 1;
    if (needed <= writer->capacity) {
        return true;
    }
    size_t capacity = writer->capacity != 0 ? writer->capacity : 1024;
    while (capacity < needed) {
        capacity *= 2;
        if (capacity > WM_RESOURCE_MAX_JSON + 1u) {
            capacity = WM_RESOURCE_MAX_JSON + 1u;
            break;
        }
    }
    char *grown = realloc(writer->text, capacity);
    if (grown == NULL) {
        writer->failed = true;
        return false;
    }
    writer->text = grown;
    writer->capacity = capacity;
    return true;
}

static void wm_writer_bytes(WmJsonWriter *writer, const char *text, size_t length)
{
    if (!wm_writer_reserve(writer, length)) {
        return;
    }
    memcpy(writer->text + writer->length, text, length);
    writer->length += length;
    writer->text[writer->length] = '\0';
}

static void wm_writer_text(WmJsonWriter *writer, const char *text)
{
    wm_writer_bytes(writer, text, strlen(text));
}

static void wm_writer_format(WmJsonWriter *writer, const char *format, ...)
{
    if (writer->failed) {
        return;
    }
    va_list args;
    va_start(args, format);
    va_list copy;
    va_copy(copy, args);
    int needed = vsnprintf(NULL, 0, format, copy);
    va_end(copy);
    if (needed < 0 || !wm_writer_reserve(writer, (size_t)needed)) {
        writer->failed = true;
        va_end(args);
        return;
    }
    vsnprintf(writer->text + writer->length,
              writer->capacity - writer->length, format, args);
    writer->length += (size_t)needed;
    va_end(args);
}

static void wm_writer_indent(WmJsonWriter *writer, unsigned depth)
{
    wm_writer_text(writer, "\n");
    for (unsigned index = 0; index < depth; index++) {
        wm_writer_text(writer, "  ");
    }
}

static bool wm_valid_utf8(const uint8_t *bytes, size_t length)
{
    size_t index = 0;
    while (index < length) {
        uint8_t first = bytes[index];
        if (first < 0x80) {
            index++;
            continue;
        }
        size_t following;
        uint32_t codepoint;
        uint32_t minimum;
        if (first >= 0xc2 && first <= 0xdf) {
            following = 1;
            codepoint = first & 0x1fu;
            minimum = 0x80;
        } else if (first >= 0xe0 && first <= 0xef) {
            following = 2;
            codepoint = first & 0x0fu;
            minimum = 0x800;
        } else if (first >= 0xf0 && first <= 0xf4) {
            following = 3;
            codepoint = first & 0x07u;
            minimum = 0x10000;
        } else {
            return false;
        }
        if (following > length - index - 1) {
            return false;
        }
        for (size_t part = 1; part <= following; part++) {
            uint8_t value = bytes[index + part];
            if ((value & 0xc0u) != 0x80u) {
                return false;
            }
            codepoint = (codepoint << 6) | (value & 0x3fu);
        }
        if (codepoint < minimum || codepoint > 0x10ffffu ||
            (codepoint >= 0xd800u && codepoint <= 0xdfffu)) {
            return false;
        }
        index += following + 1;
    }
    return true;
}

static void wm_writer_string(WmJsonWriter *writer,
                             const uint8_t *bytes, size_t length)
{
    if (!wm_valid_utf8(bytes, length)) {
        writer->failed = true;
        return;
    }
    wm_writer_text(writer, "\"");
    for (size_t index = 0; index < length; index++) {
        uint8_t value = bytes[index];
        if (value == '"' || value == '\\') {
            char escaped[2] = {'\\', (char)value};
            wm_writer_bytes(writer, escaped, 2);
        } else if (value < 32) {
            wm_writer_format(writer, "\\u%04x", (unsigned)value);
        } else {
            wm_writer_bytes(writer, (const char *)&bytes[index], 1);
        }
    }
    wm_writer_text(writer, "\"");
}

static bool wm_fixed_string(const uint8_t *bytes, size_t size, size_t offset,
                            size_t field_size, const uint8_t **value, size_t *length)
{
    if (!wm_range(size, offset, field_size)) {
        return false;
    }
    size_t count = 0;
    while (count < field_size && bytes[offset + count] != 0) {
        count++;
    }
    *value = bytes + offset;
    *length = count;
    return true;
}

static bool wm_cstring(const uint8_t *bytes, size_t size, size_t offset,
                       const uint8_t **value, size_t *length)
{
    if (offset >= size) {
        return false;
    }
    size_t count = 0;
    while (offset + count < size && bytes[offset + count] != 0) {
        count++;
    }
    if (offset + count == size) {
        return false;
    }
    *value = bytes + offset;
    *length = count;
    return true;
}

static bool wm_read_sections(const uint8_t *data, size_t size,
                             const char signature[4], WmSection *sections,
                             size_t *section_count)
{
    if (data == NULL || size < 16 || memcmp(data, signature, 4) != 0 ||
        data[4] != 0xfe || data[5] != 0xff) {
        return false;
    }
    size_t file_size = wm_be32(data + 8);
    size_t cursor = wm_be16(data + 12);
    size_t count = wm_be16(data + 14);
    if (file_size > size || cursor > file_size ||
        count > WM_RESOURCE_MAX_SECTIONS) {
        return false;
    }
    for (size_t index = 0; index < count; index++) {
        if (!wm_range(file_size, cursor, 8)) {
            return false;
        }
        size_t length = wm_be32(data + cursor + 4);
        if (length < 8 || !wm_range(file_size, cursor, length)) {
            return false;
        }
        sections[index].bytes = data + cursor;
        sections[index].size = length;
        memcpy(sections[index].kind, data + cursor, 4);
        sections[index].kind[4] = '\0';
        cursor += length;
    }
    *section_count = count;
    return true;
}

static void wm_writer_float(WmJsonWriter *writer, float value)
{
    if (!isfinite(value)) {
        writer->failed = true;
        return;
    }
    /* Preserve IEEE-754 values and keep JSON independent of host locale. */
    char number[64];
    int length = snprintf(number, sizeof(number), "%.17g", (double)value);
    if (length < 0 || length >= (int)sizeof(number)) {
        writer->failed = true;
        return;
    }
    for (int index = 0; index < length; index++) {
        if (number[index] == ',') {
            number[index] = '.';
        }
    }
    wm_writer_bytes(writer, number, (size_t)length);
}

static void wm_normalize_commas(WmJsonWriter *writer)
{
    /* Keep generated objects readable when a field is appended conditionally. */
    for (size_t index = 0; index < writer->length; index++) {
        if (writer->text[index] != '\n') {
            continue;
        }
        size_t comma = index + 1;
        while (comma < writer->length && writer->text[comma] == ' ') {
            comma++;
        }
        if (comma < writer->length && writer->text[comma] == ',') {
            memmove(writer->text + index + 1,
                    writer->text + index, comma - index);
            writer->text[index] = ',';
            index = comma;
        }
    }
}

static bool wm_writer_floats(WmJsonWriter *writer, const uint8_t *bytes,
                             size_t size, size_t offset, size_t count)
{
    if (count > (size - (offset <= size ? offset : size)) / 4 || offset > size) {
        return false;
    }
    wm_writer_text(writer, "[");
    for (size_t index = 0; index < count; index++) {
        float value;
        if (!wm_float(bytes, size, offset + index * 4, &value)) {
            return false;
        }
        if (index != 0) {
            wm_writer_text(writer, ", ");
        }
        wm_writer_float(writer, value);
    }
    wm_writer_text(writer, "]");
    return !writer->failed;
}

static bool wm_writer_bytes_array(WmJsonWriter *writer, const uint8_t *bytes,
                                  size_t size, size_t offset, size_t count)
{
    if (!wm_range(size, offset, count)) {
        return false;
    }
    wm_writer_text(writer, "[");
    for (size_t index = 0; index < count; index++) {
        if (index != 0) {
            wm_writer_text(writer, ", ");
        }
        wm_writer_format(writer, "%u", (unsigned)bytes[offset + index]);
    }
    wm_writer_text(writer, "]");
    return !writer->failed;
}

static bool wm_writer_srt(WmJsonWriter *writer, const uint8_t *bytes,
                          size_t size, size_t offset)
{
    if (!wm_range(size, offset, 20)) {
        return false;
    }
    wm_writer_text(writer, "{\"translate\": ");
    if (!wm_writer_floats(writer, bytes, size, offset, 2)) {
        return false;
    }
    wm_writer_text(writer, ", \"rotation\": ");
    float rotation;
    if (!wm_float(bytes, size, offset + 8, &rotation)) {
        return false;
    }
    wm_writer_float(writer, rotation);
    wm_writer_text(writer, ", \"scale\": ");
    if (!wm_writer_floats(writer, bytes, size, offset + 12, 2)) {
        return false;
    }
    wm_writer_text(writer, "}");
    return !writer->failed;
}

static bool wm_writer_animation_target(WmJsonWriter *writer,
                                       const uint8_t *block, size_t size,
                                       size_t base, unsigned depth)
{
    if (!wm_range(size, base, 24)) {
        return false;
    }
    const uint8_t *name;
    size_t name_length;
    if (!wm_fixed_string(block, size, base, 20, &name, &name_length)) {
        return false;
    }
    size_t tag_count = block[base + 20];
    if (tag_count > WM_RESOURCE_MAX_ITEMS ||
        !wm_range(size, base + 24, tag_count * 4)) {
        return false;
    }

    wm_writer_text(writer, "{\"name\": ");
    wm_writer_string(writer, name, name_length);
    wm_writer_format(writer, ", \"type\": %u, \"tracks\": [",
                     (unsigned)block[base + 21]);
    bool first_track = true;
    for (size_t tag_index = 0; tag_index < tag_count; tag_index++) {
        size_t relative = wm_be32(block + base + 24 + tag_index * 4);
        if (relative > size - base) {
            return false;
        }
        size_t tag = base + relative;
        if (!wm_range(size, tag, 8)) {
            return false;
        }
        size_t track_count = block[tag + 4];
        if (!wm_range(size, tag + 8, track_count * 4)) {
            return false;
        }
        for (size_t track_index = 0; track_index < track_count; track_index++) {
            size_t track_relative = wm_be32(block + tag + 8 + track_index * 4);
            if (track_relative > size - tag) {
                return false;
            }
            size_t track = tag + track_relative;
            if (!wm_range(size, track, 12)) {
                return false;
            }
            uint8_t curve = block[track + 2];
            size_t key_count = wm_be16(block + track + 4);
            size_t key_offset = wm_be32(block + track + 8);
            size_t stride = curve == 2 ? 12 : 8;
            if (key_count > WM_RESOURCE_MAX_ITEMS || key_offset > size - track ||
                !wm_range(size, track + key_offset, key_count * stride)) {
                return false;
            }
            if (!first_track) {
                wm_writer_text(writer, ",");
            }
            wm_writer_indent(writer, depth + 1);
            wm_writer_text(writer, "{\"kind\": ");
            wm_writer_string(writer, block + tag, 4);
            wm_writer_format(writer,
                ", \"id\": %u, \"target\": %u, \"curveType\": %u, \"keys\": [",
                (unsigned)block[track], (unsigned)block[track + 1], (unsigned)curve);
            for (size_t key_index = 0; key_index < key_count; key_index++) {
                size_t key = track + key_offset + key_index * stride;
                float frame;
                if (!wm_float(block, size, key, &frame)) {
                    return false;
                }
                if (key_index != 0) {
                    wm_writer_text(writer, ", ");
                }
                wm_writer_text(writer, "{\"frame\": ");
                wm_writer_float(writer, frame);
                wm_writer_text(writer, ", \"value\": ");
                if (curve == 2) {
                    float value;
                    float slope;
                    if (!wm_float(block, size, key + 4, &value) ||
                        !wm_float(block, size, key + 8, &slope)) {
                        return false;
                    }
                    wm_writer_float(writer, value);
                    wm_writer_text(writer, ", \"slope\": ");
                    wm_writer_float(writer, slope);
                } else {
                    wm_writer_format(writer, "%u", (unsigned)wm_be16(block + key + 4));
                }
                wm_writer_text(writer, "}");
            }
            wm_writer_text(writer, "]}");
            first_track = false;
        }
    }
    if (!first_track) {
        wm_writer_indent(writer, depth);
    }
    wm_writer_text(writer, "]}");
    return !writer->failed;
}

bool wm_brlan_to_json(const uint8_t *data, size_t size,
                      char **json, size_t *json_size,
                      char *error, size_t error_size)
{
    if (json == NULL || json_size == NULL) {
        wm_set_error(error, error_size, "Invalid BRLAN output.");
        return false;
    }
    *json = NULL;
    *json_size = 0;

    WmSection *sections = calloc(WM_RESOURCE_MAX_SECTIONS, sizeof(*sections));
    if (sections == NULL) {
        wm_set_error(error, error_size, "Out of memory reading BRLAN sections.");
        return false;
    }
    size_t section_count = 0;
    if (!wm_read_sections(data, size, "RLAN", sections, &section_count)) {
        wm_set_error(error, error_size, "Invalid BRLAN section table.");
        free(sections);
        return false;
    }

    const WmSection *last_pai = NULL;
    for (size_t index = 0; index < section_count; index++) {
        if (strcmp(sections[index].kind, "pai1") == 0) {
            last_pai = &sections[index];
        }
    }
    WmJsonWriter writer = {0};
    unsigned frames = 0;
    bool loop = false;
    if (last_pai != NULL) {
        if (!wm_range(last_pai->size, 8, 12)) {
            wm_set_error(error, error_size, "Truncated BRLAN animation header.");
            free(sections);
            return false;
        }
        frames = wm_be16(last_pai->bytes + 8);
        loop = last_pai->bytes[10] != 0;
    }
    wm_writer_format(&writer, "{\n  \"frames\": %u,\n  \"loop\": %s,\n  \"textures\": [",
                     frames, loop ? "true" : "false");
    if (last_pai != NULL) {
        const uint8_t *block = last_pai->bytes;
        size_t file_count = wm_be16(block + 12);
        if (file_count > WM_RESOURCE_MAX_ITEMS ||
            !wm_range(last_pai->size, 20, file_count * 4)) {
            wm_set_error(error, error_size, "Invalid BRLAN texture table.");
            goto fail;
        }
        for (size_t index = 0; index < file_count; index++) {
            size_t relative = wm_be32(block + 20 + index * 4);
            if (relative > last_pai->size - 20) {
                wm_set_error(error, error_size, "Invalid BRLAN texture name offset.");
                goto fail;
            }
            const uint8_t *name;
            size_t name_length;
            if (!wm_cstring(block, last_pai->size, 20 + relative,
                            &name, &name_length)) {
                wm_set_error(error, error_size, "Invalid BRLAN texture name.");
                goto fail;
            }
            if (index != 0) {
                wm_writer_text(&writer, ", ");
            }
            wm_writer_string(&writer, name, name_length);
        }
    }
    wm_writer_text(&writer, "],\n  \"targets\": [");

    bool first_target = true;
    for (size_t section_index = 0; section_index < section_count; section_index++) {
        const WmSection *section = &sections[section_index];
        if (strcmp(section->kind, "pai1") != 0) {
            continue;
        }
        const uint8_t *block = section->bytes;
        if (!wm_range(section->size, 8, 12)) {
            wm_set_error(error, error_size, "Truncated BRLAN animation header.");
            goto fail;
        }
        size_t target_count = wm_be16(block + 14);
        size_t table = wm_be32(block + 16);
        if (target_count > WM_RESOURCE_MAX_ITEMS ||
            !wm_range(section->size, table, target_count * 4)) {
            wm_set_error(error, error_size, "Invalid BRLAN target table.");
            goto fail;
        }
        for (size_t target_index = 0; target_index < target_count; target_index++) {
            size_t base = wm_be32(block + table + target_index * 4);
            if (!first_target) {
                wm_writer_text(&writer, ",");
            }
            wm_writer_indent(&writer, 2);
            if (!wm_writer_animation_target(&writer, block, section->size,
                                             base, 2)) {
                wm_set_error(error, error_size, "Invalid BRLAN target or key track.");
                goto fail;
            }
            first_target = false;
        }
    }
    if (!first_target) {
        wm_writer_indent(&writer, 1);
    }
    wm_writer_text(&writer, "]\n}\n");
    if (writer.failed) {
        wm_set_error(error, error_size, "BRLAN JSON exceeds the memory limit.");
        goto fail;
    }
    wm_normalize_commas(&writer);
    *json = writer.text;
    *json_size = writer.length;
    free(sections);
    return true;

fail:
    free(writer.text);
    free(sections);
    return false;
}

static bool wm_writer_rgba_rows(WmJsonWriter *writer, const uint8_t *bytes,
                                size_t size, size_t offset, size_t rows)
{
    if (rows > (size - (offset <= size ? offset : size)) / 4 || offset > size) {
        return false;
    }
    wm_writer_text(writer, "[");
    for (size_t index = 0; index < rows; index++) {
        if (index != 0) {
            wm_writer_text(writer, ", ");
        }
        if (!wm_writer_bytes_array(writer, bytes, size, offset + index * 4, 4)) {
            return false;
        }
    }
    wm_writer_text(writer, "]");
    return !writer->failed;
}

static bool wm_writer_material(WmJsonWriter *writer,
                               const uint8_t *block, size_t size,
                               size_t base, unsigned depth)
{
    if (!wm_range(size, base, 64)) {
        return false;
    }
    const uint8_t *name;
    size_t name_length;
    if (!wm_fixed_string(block, size, base, 20, &name, &name_length)) {
        return false;
    }
    uint32_t flags = wm_be32(block + base + 60);
    wm_writer_text(writer, "{");
    wm_writer_indent(writer, depth + 1);
    wm_writer_text(writer, "\"name\": ");
    wm_writer_string(writer, name, name_length);
    wm_writer_indent(writer, depth + 1);
    wm_writer_text(writer, ",\"colors\": [");
    for (size_t row = 0; row < 3; row++) {
        if (row != 0) {
            wm_writer_text(writer, ", ");
        }
        wm_writer_text(writer, "[");
        for (size_t channel = 0; channel < 4; channel++) {
            if (channel != 0) {
                wm_writer_text(writer, ", ");
            }
            wm_writer_format(writer, "%d",
                (int)wm_signed_be16(block + base + 20 + row * 8 + channel * 2));
        }
        wm_writer_text(writer, "]");
    }
    wm_writer_text(writer, "],");
    wm_writer_indent(writer, depth + 1);
    wm_writer_text(writer, "\"konstColors\": ");
    if (!wm_writer_rgba_rows(writer, block, size, base + 44, 4)) {
        return false;
    }
    wm_writer_format(writer, ",\n%*s\"flags\": %u,",
                     (int)((depth + 1) * 2), "", flags);

    size_t cursor = base + 64;
    size_t count = flags & 15u;
    if (!wm_range(size, cursor, count * 4)) {
        return false;
    }
    wm_writer_indent(writer, depth + 1);
    wm_writer_text(writer, "\"textureMaps\": [");
    for (size_t index = 0; index < count; index++) {
        if (index != 0) {
            wm_writer_text(writer, ", ");
        }
        wm_writer_format(writer,
            "{\"texture\": %u, \"wrapS\": %u, \"wrapT\": %u}",
            (unsigned)wm_be16(block + cursor + index * 4),
            (unsigned)block[cursor + index * 4 + 2],
            (unsigned)block[cursor + index * 4 + 3]);
    }
    wm_writer_text(writer, "],");
    cursor += count * 4;

    count = (flags >> 4) & 15u;
    if (!wm_range(size, cursor, count * 20)) {
        return false;
    }
    wm_writer_indent(writer, depth + 1);
    wm_writer_text(writer, "\"textureSRTs\": [");
    for (size_t index = 0; index < count; index++) {
        if (index != 0) {
            wm_writer_text(writer, ", ");
        }
        if (!wm_writer_srt(writer, block, size, cursor + index * 20)) {
            return false;
        }
    }
    wm_writer_text(writer, "],");
    cursor += count * 20;

    count = (flags >> 8) & 15u;
    if (!wm_range(size, cursor, count * 4)) {
        return false;
    }
    wm_writer_indent(writer, depth + 1);
    wm_writer_text(writer, "\"texCoordGens\": [");
    for (size_t index = 0; index < count; index++) {
        if (index != 0) {
            wm_writer_text(writer, ", ");
        }
        wm_writer_format(writer,
            "{\"type\": %u, \"source\": %u, \"matrix\": %u}",
            (unsigned)block[cursor + index * 4],
            (unsigned)block[cursor + index * 4 + 1],
            (unsigned)block[cursor + index * 4 + 2]);
    }
    wm_writer_text(writer, "],");
    cursor += count * 4;

    if ((flags & (1u << 25)) != 0) {
        wm_writer_indent(writer, depth + 1);
        wm_writer_text(writer, "\"channelControl\": ");
        if (!wm_writer_bytes_array(writer, block, size, cursor, 4)) {
            return false;
        }
        wm_writer_text(writer, ",");
        cursor += 4;
    }
    if ((flags & (1u << 27)) != 0) {
        wm_writer_indent(writer, depth + 1);
        wm_writer_text(writer, "\"materialColor\": ");
        if (!wm_writer_bytes_array(writer, block, size, cursor, 4)) {
            return false;
        }
        wm_writer_text(writer, ",");
        cursor += 4;
    }
    if ((flags & (1u << 12)) != 0) {
        wm_writer_indent(writer, depth + 1);
        wm_writer_text(writer, "\"tevSwapTable\": ");
        if (!wm_writer_bytes_array(writer, block, size, cursor, 4)) {
            return false;
        }
        wm_writer_text(writer, ",");
        cursor += 4;
    }

    count = (flags >> 13) & 3u;
    if (!wm_range(size, cursor, count * 20)) {
        return false;
    }
    wm_writer_indent(writer, depth + 1);
    wm_writer_text(writer, "\"indirectSRTs\": [");
    for (size_t index = 0; index < count; index++) {
        if (index != 0) {
            wm_writer_text(writer, ", ");
        }
        if (!wm_writer_srt(writer, block, size, cursor + index * 20)) {
            return false;
        }
    }
    wm_writer_text(writer, "],");
    cursor += count * 20;

    count = (flags >> 15) & 7u;
    if (!wm_range(size, cursor, count * 4)) {
        return false;
    }
    wm_writer_indent(writer, depth + 1);
    wm_writer_text(writer, "\"indirectStages\": [");
    for (size_t index = 0; index < count; index++) {
        if (index != 0) {
            wm_writer_text(writer, ", ");
        }
        if (!wm_writer_bytes_array(writer, block, size, cursor + index * 4, 4)) {
            return false;
        }
    }
    wm_writer_text(writer, "],");
    cursor += count * 4;

    count = (flags >> 18) & 31u;
    if (!wm_range(size, cursor, count * 16)) {
        return false;
    }
    wm_writer_indent(writer, depth + 1);
    wm_writer_text(writer, "\"tevStages\": [");
    for (size_t index = 0; index < count; index++) {
        if (index != 0) {
            wm_writer_text(writer, ", ");
        }
        if (!wm_writer_bytes_array(writer, block, size, cursor + index * 16, 16)) {
            return false;
        }
    }
    wm_writer_text(writer, "]");
    cursor += count * 16;

    if ((flags & (1u << 23)) != 0) {
        wm_writer_indent(writer, depth + 1);
        wm_writer_text(writer, ",\"alphaCompare\": ");
        if (!wm_writer_bytes_array(writer, block, size, cursor, 4)) {
            return false;
        }
        cursor += 4;
    }
    if ((flags & (1u << 24)) != 0) {
        wm_writer_indent(writer, depth + 1);
        wm_writer_text(writer, ",\"blendMode\": ");
        if (!wm_writer_bytes_array(writer, block, size, cursor, 4)) {
            return false;
        }
    }
    wm_writer_indent(writer, depth);
    wm_writer_text(writer, "}");
    return !writer->failed;
}

static bool wm_writer_picture(WmJsonWriter *writer,
                              const uint8_t *block, size_t size,
                              size_t offset, unsigned depth)
{
    if (!wm_range(size, offset, 20)) {
        return false;
    }
    size_t count = block[offset + 18];
    if (count > 16 || !wm_range(size, offset + 20, count * 32)) {
        return false;
    }
    wm_writer_indent(writer, depth);
    wm_writer_text(writer, ",\"vertexColors\": ");
    if (!wm_writer_rgba_rows(writer, block, size, offset, 4)) {
        return false;
    }
    wm_writer_format(writer, ",\n%*s\"material\": %u,",
                     (int)(depth * 2), "", (unsigned)wm_be16(block + offset + 16));
    wm_writer_indent(writer, depth);
    wm_writer_text(writer, "\"texCoords\": [");
    for (size_t coord = 0; coord < count; coord++) {
        if (coord != 0) {
            wm_writer_text(writer, ", ");
        }
        wm_writer_text(writer, "[");
        for (size_t vertex = 0; vertex < 4; vertex++) {
            if (vertex != 0) {
                wm_writer_text(writer, ", ");
            }
            if (!wm_writer_floats(writer, block, size,
                                  offset + 20 + coord * 32 + vertex * 8, 2)) {
                return false;
            }
        }
        wm_writer_text(writer, "]");
    }
    wm_writer_text(writer, "]");
    return !writer->failed;
}

static void wm_writer_codepoint(WmJsonWriter *writer, uint32_t codepoint)
{
    if (codepoint == '"' || codepoint == '\\') {
        char escaped[2] = {'\\', (char)codepoint};
        wm_writer_bytes(writer, escaped, 2);
    } else if (codepoint < 32) {
        wm_writer_format(writer, "\\u%04x", (unsigned)codepoint);
    } else if (codepoint < 128) {
        char value = (char)codepoint;
        wm_writer_bytes(writer, &value, 1);
    } else {
        char utf8[4];
        size_t count;
        if (codepoint < 0x800) {
            utf8[0] = (char)(0xc0u | (codepoint >> 6));
            utf8[1] = (char)(0x80u | (codepoint & 0x3fu));
            count = 2;
        } else if (codepoint < 0x10000) {
            utf8[0] = (char)(0xe0u | (codepoint >> 12));
            utf8[1] = (char)(0x80u | ((codepoint >> 6) & 0x3fu));
            utf8[2] = (char)(0x80u | (codepoint & 0x3fu));
            count = 3;
        } else {
            utf8[0] = (char)(0xf0u | (codepoint >> 18));
            utf8[1] = (char)(0x80u | ((codepoint >> 12) & 0x3fu));
            utf8[2] = (char)(0x80u | ((codepoint >> 6) & 0x3fu));
            utf8[3] = (char)(0x80u | (codepoint & 0x3fu));
            count = 4;
        }
        wm_writer_bytes(writer, utf8, count);
    }
}

static bool wm_writer_utf16(WmJsonWriter *writer,
                            const uint8_t *bytes, size_t size,
                            size_t offset, size_t length)
{
    if ((length & 1u) != 0 || !wm_range(size, offset, length)) {
        return false;
    }
    while (length >= 2 && wm_be16(bytes + offset + length - 2) == 0) {
        length -= 2;
    }
    wm_writer_text(writer, "\"");
    for (size_t index = 0; index < length; index += 2) {
        uint32_t value = wm_be16(bytes + offset + index);
        if (value >= 0xd800 && value <= 0xdbff) {
            if (index + 4 > length) {
                return false;
            }
            uint32_t low = wm_be16(bytes + offset + index + 2);
            if (low < 0xdc00 || low > 0xdfff) {
                return false;
            }
            value = 0x10000u + ((value - 0xd800u) << 10) + (low - 0xdc00u);
            index += 2;
        } else if (value >= 0xdc00 && value <= 0xdfff) {
            return false;
        }
        wm_writer_codepoint(writer, value);
    }
    wm_writer_text(writer, "\"");
    return !writer->failed;
}

static bool wm_writer_pane(WmJsonWriter *writer, const WmPaneRecord *panes,
                           size_t pane_count, int index, unsigned depth)
{
    if (index < 0 || (size_t)index >= pane_count || depth > WM_RESOURCE_MAX_PANES) {
        return false;
    }
    const WmSection *section = &panes[index].section;
    const uint8_t *block = section->bytes;
    size_t size = section->size;
    if (!wm_range(size, 0, 76)) {
        return false;
    }
    const uint8_t *name;
    size_t name_length;
    if (!wm_fixed_string(block, size, 12, 16, &name, &name_length)) {
        return false;
    }

    wm_writer_text(writer, "{");
    wm_writer_indent(writer, depth + 1);
    wm_writer_text(writer, "\"name\": ");
    wm_writer_string(writer, name, name_length);
    wm_writer_indent(writer, depth + 1);
    wm_writer_text(writer, ",\"type\": ");
    wm_writer_string(writer, (const uint8_t *)section->kind, 4);
    wm_writer_format(writer,
        ",\n%*s\"flags\": %u, \"origin\": %u, \"alpha\": %u,",
        (int)((depth + 1) * 2), "",
        (unsigned)block[8], (unsigned)block[9], (unsigned)block[10]);
    static const size_t offsets[] = {36, 48, 60, 68};
    static const size_t counts[] = {3, 3, 2, 2};
    static const char *const labels[] = {"translation", "rotation", "scale", "size"};
    for (size_t field = 0; field < 4; field++) {
        wm_writer_indent(writer, depth + 1);
        wm_writer_format(writer, "\"%s\": ", labels[field]);
        if (!wm_writer_floats(writer, block, size, offsets[field], counts[field])) {
            return false;
        }
        if (field != 3) {
            wm_writer_text(writer, ",");
        }
    }

    if (strcmp(section->kind, "pic1") == 0) {
        if (!wm_writer_picture(writer, block, size, 76, depth + 1)) {
            return false;
        }
    } else if (strcmp(section->kind, "txt1") == 0) {
        if (!wm_range(size, 76, 40)) {
            return false;
        }
        size_t length = wm_be16(block + 78);
        size_t text_offset = wm_be32(block + 88);
        wm_writer_format(writer,
            "\n%*s,\"material\": %u, \"font\": %u, \"textPosition\": %u,",
            (int)((depth + 1) * 2), "",
            (unsigned)wm_be16(block + 80), (unsigned)wm_be16(block + 82),
            (unsigned)block[84]);
        wm_writer_indent(writer, depth + 1);
        wm_writer_text(writer, "\"text\": ");
        if (!wm_writer_utf16(writer, block, size, text_offset, length)) {
            return false;
        }
        wm_writer_indent(writer, depth + 1);
        wm_writer_text(writer, ",\"textColors\": ");
        if (!wm_writer_rgba_rows(writer, block, size, 92, 2)) {
            return false;
        }
        wm_writer_indent(writer, depth + 1);
        wm_writer_text(writer, ",\"fontSize\": ");
        if (!wm_writer_floats(writer, block, size, 100, 2)) {
            return false;
        }
        float char_space;
        float line_space;
        if (!wm_float(block, size, 108, &char_space) ||
            !wm_float(block, size, 112, &line_space)) {
            return false;
        }
        wm_writer_indent(writer, depth + 1);
        wm_writer_text(writer, ",\"charSpace\": ");
        wm_writer_float(writer, char_space);
        wm_writer_indent(writer, depth + 1);
        wm_writer_text(writer, ",\"lineSpace\": ");
        wm_writer_float(writer, line_space);
    } else if (strcmp(section->kind, "wnd1") == 0) {
        if (!wm_range(size, 76, 28)) {
            return false;
        }
        wm_writer_indent(writer, depth + 1);
        wm_writer_text(writer, ",\"inflation\": ");
        if (!wm_writer_floats(writer, block, size, 76, 4)) {
            return false;
        }
        size_t frame_count = block[92];
        size_t content_offset = wm_be32(block + 96);
        size_t frames_offset = wm_be32(block + 100);
        if (!wm_writer_picture(writer, block, size, content_offset, depth + 1) ||
            frame_count > WM_RESOURCE_MAX_ITEMS ||
            !wm_range(size, frames_offset, frame_count * 4)) {
            return false;
        }
        wm_writer_indent(writer, depth + 1);
        wm_writer_text(writer, ",\"frames\": [");
        for (size_t frame = 0; frame < frame_count; frame++) {
            size_t frame_offset = wm_be32(block + frames_offset + frame * 4);
            if (!wm_range(size, frame_offset, 3)) {
                return false;
            }
            if (frame != 0) {
                wm_writer_text(writer, ", ");
            }
            wm_writer_format(writer, "{\"material\": %u, \"flip\": %u}",
                (unsigned)wm_be16(block + frame_offset),
                (unsigned)block[frame_offset + 2]);
        }
        wm_writer_text(writer, "]");
    }

    wm_writer_indent(writer, depth + 1);
    wm_writer_text(writer, ",\"children\": [");
    int child = panes[index].first_child;
    while (child >= 0) {
        wm_writer_indent(writer, depth + 2);
        if (!wm_writer_pane(writer, panes, pane_count, child, depth + 2)) {
            return false;
        }
        child = panes[child].next_sibling;
        if (child >= 0) {
            wm_writer_text(writer, ",");
        }
    }
    if (panes[index].first_child >= 0) {
        wm_writer_indent(writer, depth + 1);
    }
    wm_writer_text(writer, "]");
    wm_writer_indent(writer, depth);
    wm_writer_text(writer, "}");
    return !writer->failed;
}

static const WmResourceTexture *wm_find_texture(const WmResourceTexture *textures,
                                                size_t count,
                                                const uint8_t *name, size_t name_size)
{
    for (size_t index = 0; index < count; index++) {
        if (strlen(textures[index].name) == name_size &&
            memcmp(textures[index].name, name, name_size) == 0) {
            return &textures[index];
        }
    }
    return NULL;
}

static void wm_writer_texture_descriptor(WmJsonWriter *writer,
                                         const uint8_t *name, size_t name_size,
                                         const WmResourceTexture *texture)
{
    wm_writer_text(writer, "{\"name\": ");
    wm_writer_string(writer, name, name_size);
    if (texture == NULL) {
        wm_writer_text(writer, ", \"missing\": true}");
        return;
    }
    wm_writer_text(writer, ", \"url\": ");
    wm_writer_string(writer, (const uint8_t *)texture->url, strlen(texture->url));
    wm_writer_format(writer, ", \"width\": %u, \"height\": %u, \"format\": %u",
                     (unsigned)texture->width, (unsigned)texture->height,
                     (unsigned)texture->format);
    if (texture->source != NULL) {
        wm_writer_text(writer, ", \"source\": ");
        wm_writer_string(writer, (const uint8_t *)texture->source,
                         strlen(texture->source));
    }
    wm_writer_text(writer, "}");
}

static bool wm_writer_name_table(WmJsonWriter *writer, const WmSection *section,
                                 const WmResourceTexture *textures, size_t texture_count,
                                 bool texture_table)
{
    if (section == NULL) {
        wm_writer_text(writer, "[]");
        return true;
    }
    const uint8_t *block = section->bytes;
    size_t size = section->size;
    if (!wm_range(size, 8, 4)) {
        return false;
    }
    size_t count = wm_be16(block + 8);
    if (count > WM_RESOURCE_MAX_ITEMS || !wm_range(size, 12, count * 8)) {
        return false;
    }
    wm_writer_text(writer, "[");
    for (size_t index = 0; index < count; index++) {
        size_t relative = wm_be32(block + 12 + index * 8);
        if (relative > size - 12) {
            return false;
        }
        const uint8_t *name;
        size_t name_size;
        if (!wm_cstring(block, size, 12 + relative, &name, &name_size)) {
            return false;
        }
        if (index != 0) {
            wm_writer_text(writer, ", ");
        }
        if (texture_table) {
            wm_writer_texture_descriptor(writer, name, name_size,
                wm_find_texture(textures, texture_count, name, name_size));
        } else {
            wm_writer_string(writer, name, name_size);
        }
    }
    wm_writer_text(writer, "]");
    return !writer->failed;
}

static bool wm_writer_material_table(WmJsonWriter *writer, const WmSection *section)
{
    if (section == NULL) {
        wm_writer_text(writer, "[]");
        return true;
    }
    const uint8_t *block = section->bytes;
    size_t size = section->size;
    if (!wm_range(size, 8, 4)) {
        return false;
    }
    size_t count = wm_be16(block + 8);
    if (count > WM_RESOURCE_MAX_ITEMS || !wm_range(size, 12, count * 4)) {
        return false;
    }
    wm_writer_text(writer, "[");
    for (size_t index = 0; index < count; index++) {
        size_t offset = wm_be32(block + 12 + index * 4);
        if (index != 0) {
            wm_writer_text(writer, ",");
        }
        wm_writer_indent(writer, 2);
        if (!wm_writer_material(writer, block, size, offset, 2)) {
            return false;
        }
    }
    if (count != 0) {
        wm_writer_indent(writer, 1);
    }
    wm_writer_text(writer, "]");
    return !writer->failed;
}

static bool wm_writer_groups(WmJsonWriter *writer,
                             const WmSection *sections, size_t section_count)
{
    wm_writer_text(writer, "{");
    bool first = true;
    for (size_t section_index = 0; section_index < section_count; section_index++) {
        const WmSection *section = &sections[section_index];
        if (strcmp(section->kind, "grp1") != 0) {
            continue;
        }
        const uint8_t *block = section->bytes;
        size_t size = section->size;
        if (!wm_range(size, 8, 20)) {
            return false;
        }
        const uint8_t *name;
        size_t name_size;
        if (!wm_fixed_string(block, size, 8, 16, &name, &name_size)) {
            return false;
        }
        size_t count = wm_be16(block + 24);
        if (count > WM_RESOURCE_MAX_ITEMS || !wm_range(size, 28, count * 16)) {
            return false;
        }
        if (!first) {
            wm_writer_text(writer, ",");
        }
        wm_writer_indent(writer, 2);
        wm_writer_string(writer, name, name_size);
        wm_writer_text(writer, ": [");
        for (size_t index = 0; index < count; index++) {
            const uint8_t *member;
            size_t member_size;
            if (!wm_fixed_string(block, size, 28 + index * 16,
                                  16, &member, &member_size)) {
                return false;
            }
            if (index != 0) {
                wm_writer_text(writer, ", ");
            }
            wm_writer_string(writer, member, member_size);
        }
        wm_writer_text(writer, "]");
        first = false;
    }
    if (!first) {
        wm_writer_indent(writer, 1);
    }
    wm_writer_text(writer, "}");
    return !writer->failed;
}

bool wm_brlyt_to_json_with_source(
    const uint8_t *data, size_t size,
    const char *name, const char *package, const char *source,
    const WmResourceTexture *textures, size_t texture_count,
    const WmResourceAnimation *animations, size_t animation_count,
    char **json, size_t *json_size,
    char *error, size_t error_size)
{
    if (json == NULL || json_size == NULL || name == NULL || package == NULL ||
        (texture_count != 0 && textures == NULL) ||
        (animation_count != 0 && animations == NULL) ||
        texture_count > WM_RESOURCE_MAX_ITEMS ||
        animation_count > WM_RESOURCE_MAX_ITEMS) {
        wm_set_error(error, error_size, "Invalid BRLYT export arguments.");
        return false;
    }
    *json = NULL;
    *json_size = 0;

    WmSection *sections = calloc(WM_RESOURCE_MAX_SECTIONS, sizeof(*sections));
    WmPaneRecord *panes = calloc(WM_RESOURCE_MAX_PANES, sizeof(*panes));
    int *stack = calloc(WM_RESOURCE_MAX_PANES, sizeof(*stack));
    if (sections == NULL || panes == NULL || stack == NULL) {
        wm_set_error(error, error_size, "Out of memory reading BRLYT layout.");
        free(sections);
        free(panes);
        free(stack);
        return false;
    }
    size_t section_count = 0;
    if (!wm_read_sections(data, size, "RLYT", sections, &section_count)) {
        wm_set_error(error, error_size, "Invalid BRLYT section table.");
        free(sections);
        free(panes);
        free(stack);
        return false;
    }

    const WmSection *layout_header = NULL;
    const WmSection *texture_table = NULL;
    const WmSection *font_table = NULL;
    const WmSection *material_table = NULL;
    size_t pane_count = 0;
    size_t depth = 0;
    int last = -1;
    int root = -1;
    bool valid = true;
    for (size_t index = 0; index < section_count; index++) {
        const WmSection *section = &sections[index];
        if (strcmp(section->kind, "lyt1") == 0) {
            layout_header = section;
        } else if (strcmp(section->kind, "txl1") == 0) {
            texture_table = section;
        } else if (strcmp(section->kind, "fnl1") == 0) {
            font_table = section;
        } else if (strcmp(section->kind, "mat1") == 0) {
            material_table = section;
        } else if (strcmp(section->kind, "pan1") == 0 ||
                   strcmp(section->kind, "bnd1") == 0 ||
                   strcmp(section->kind, "pic1") == 0 ||
                   strcmp(section->kind, "txt1") == 0 ||
                   strcmp(section->kind, "wnd1") == 0) {
            if (pane_count >= WM_RESOURCE_MAX_PANES) {
                valid = false;
                break;
            }
            int current = (int)pane_count++;
            panes[current].section = *section;
            panes[current].first_child = -1;
            panes[current].last_child = -1;
            panes[current].next_sibling = -1;
            if (depth == 0) {
                root = current;
            } else {
                WmPaneRecord *parent = &panes[stack[depth - 1]];
                if (parent->last_child >= 0) {
                    panes[parent->last_child].next_sibling = current;
                } else {
                    parent->first_child = current;
                }
                parent->last_child = current;
            }
            last = current;
        } else if (strcmp(section->kind, "pas1") == 0) {
            if (last < 0 || depth >= WM_RESOURCE_MAX_PANES) {
                valid = false;
                break;
            }
            stack[depth++] = last;
        } else if (strcmp(section->kind, "pae1") == 0) {
            if (depth == 0) {
                valid = false;
                break;
            }
            depth--;
        }
    }
    if (!valid || depth != 0) {
        wm_set_error(error, error_size, "Invalid BRLYT pane hierarchy.");
        free(sections);
        free(panes);
        free(stack);
        return false;
    }

    float width = 0;
    float height = 0;
    unsigned origin_type = 1;
    if (layout_header != NULL) {
        if (!wm_range(layout_header->size, 8, 12) ||
            !wm_float(layout_header->bytes, layout_header->size, 12, &width) ||
            !wm_float(layout_header->bytes, layout_header->size, 16, &height)) {
            wm_set_error(error, error_size, "Invalid BRLYT layout dimensions.");
            free(sections);
            free(panes);
            free(stack);
            return false;
        }
        origin_type = layout_header->bytes[8];
    }

    WmJsonWriter writer = {0};
    wm_writer_text(&writer, "{\n  \"name\": ");
    wm_writer_string(&writer, (const uint8_t *)name, strlen(name));
    wm_writer_text(&writer, ",\n  \"package\": ");
    wm_writer_string(&writer, (const uint8_t *)package, strlen(package));
    if (source != NULL) {
        wm_writer_text(&writer, ",\n  \"source\": ");
        wm_writer_string(&writer, (const uint8_t *)source, strlen(source));
    }
    wm_writer_text(&writer, ",\n  \"width\": ");
    wm_writer_float(&writer, width);
    wm_writer_text(&writer, ",\n  \"height\": ");
    wm_writer_float(&writer, height);
    wm_writer_format(&writer, ",\n  \"originType\": %u,\n  \"textures\": ",
                     origin_type);
    if (!wm_writer_name_table(&writer, texture_table,
                               textures, texture_count, true)) {
        wm_set_error(error, error_size, "Invalid BRLYT texture table.");
        goto fail;
    }
    wm_writer_text(&writer, ",\n  \"resourceTextures\": {");
    for (size_t index = 0; index < texture_count; index++) {
        if (index != 0) {
            wm_writer_text(&writer, ",");
        }
        wm_writer_indent(&writer, 2);
        wm_writer_string(&writer, (const uint8_t *)textures[index].name,
                          strlen(textures[index].name));
        wm_writer_text(&writer, ": ");
        wm_writer_texture_descriptor(&writer,
            (const uint8_t *)textures[index].name,
            strlen(textures[index].name), &textures[index]);
    }
    if (texture_count != 0) {
        wm_writer_indent(&writer, 1);
    }
    wm_writer_text(&writer, "},\n  \"fonts\": ");
    if (!wm_writer_name_table(&writer, font_table, NULL, 0, false)) {
        wm_set_error(error, error_size, "Invalid BRLYT font table.");
        goto fail;
    }
    wm_writer_text(&writer, ",\n  \"materials\": ");
    if (!wm_writer_material_table(&writer, material_table)) {
        wm_set_error(error, error_size, "Invalid BRLYT material table.");
        goto fail;
    }
    wm_writer_text(&writer, ",\n  \"groups\": ");
    if (!wm_writer_groups(&writer, sections, section_count)) {
        wm_set_error(error, error_size, "Invalid BRLYT group table.");
        goto fail;
    }
    wm_writer_text(&writer, ",\n  \"root\": ");
    if (root >= 0) {
        if (!wm_writer_pane(&writer, panes, pane_count, root, 1)) {
            wm_set_error(error, error_size, "Invalid BRLYT pane content.");
            goto fail;
        }
    } else {
        wm_writer_text(&writer, "null");
    }
    wm_writer_text(&writer, ",\n  \"animations\": {");
    for (size_t index = 0; index < animation_count; index++) {
        char *animation_json = NULL;
        size_t animation_size = 0;
        if (!wm_brlan_to_json(animations[index].data, animations[index].size,
                              &animation_json, &animation_size, error, error_size)) {
            goto fail;
        }
        if (index != 0) {
            wm_writer_text(&writer, ",");
        }
        wm_writer_indent(&writer, 2);
        wm_writer_string(&writer, (const uint8_t *)animations[index].name,
                          strlen(animations[index].name));
        wm_writer_text(&writer, ": ");
        for (size_t byte = 0; byte < animation_size; byte++) {
            if (animation_json[byte] == '\n' && byte + 1 < animation_size) {
                wm_writer_text(&writer, "\n    ");
            } else if (animation_json[byte] != '\n' || byte + 1 < animation_size) {
                wm_writer_bytes(&writer, animation_json + byte, 1);
            }
        }
        free(animation_json);
    }
    if (animation_count != 0) {
        wm_writer_indent(&writer, 1);
    }
    wm_writer_text(&writer, "}\n}\n");
    if (writer.failed) {
        wm_set_error(error, error_size, "BRLYT JSON exceeds the memory limit.");
        goto fail;
    }
    wm_normalize_commas(&writer);
    *json = writer.text;
    *json_size = writer.length;
    free(sections);
    free(panes);
    free(stack);
    return true;

fail:
    free(writer.text);
    free(sections);
    free(panes);
    free(stack);
    return false;
}

bool wm_brlyt_to_json(const uint8_t *data, size_t size,
                      const char *name, const char *package,
                      const WmResourceTexture *textures, size_t texture_count,
                      const WmResourceAnimation *animations, size_t animation_count,
                      char **json, size_t *json_size,
                      char *error, size_t error_size)
{
    return wm_brlyt_to_json_with_source(
        data, size, name, package, NULL, textures, texture_count,
        animations, animation_count, json, json_size, error, error_size);
}
