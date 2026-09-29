#include "resource_layout_internal.h"
#include "resource_bytes.h"
#include "wii_menu/support/utf8.h"
#include "wii_menu/support/error.h"

#include <math.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Bounds and JSON writing are shared by BRLAN and BRLYT exporters. */
uint16_t wm_be16(const uint8_t *bytes) {
    return wm_resource_be16(bytes);
}

int16_t wm_signed_be16(const uint8_t *bytes) {
    return (int16_t)wm_be16(bytes);
}

uint32_t wm_be32(const uint8_t *bytes) {
    return wm_resource_be32(bytes);
}

bool wm_range(size_t size, size_t offset, size_t length) {
    return wm_resource_range_fits(size, offset, length);
}

void wm_set_error(char *error, size_t error_size, const char *message) {
    wm_error_set(error, error_size, message);
}

bool wm_float(const uint8_t *bytes, size_t size, size_t offset, float *value) {
    if (!wm_range(size, offset, 4)) {
        return false;
    }
    uint32_t bits = wm_be32(bytes + offset);
    memcpy(value, &bits, sizeof(bits));
    return isfinite(*value);
}

static bool wm_writer_reserve(WmJsonWriter *writer, size_t additional) {
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

void wm_writer_bytes(WmJsonWriter *writer, const char *text, size_t length) {
    if (!wm_writer_reserve(writer, length)) {
        return;
    }
    memcpy(writer->text + writer->length, text, length);
    writer->length += length;
    writer->text[writer->length] = '\0';
}

void wm_writer_text(WmJsonWriter *writer, const char *text) {
    wm_writer_bytes(writer, text, strlen(text));
}

void wm_writer_format(WmJsonWriter *writer, const char *format, ...) {
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
    vsnprintf(writer->text + writer->length, writer->capacity - writer->length, format,
              args);
    writer->length += (size_t)needed;
    va_end(args);
}

void wm_writer_indent(WmJsonWriter *writer, unsigned depth) {
    wm_writer_text(writer, "\n");
    for (unsigned index = 0; index < depth; index++) {
        wm_writer_text(writer, "  ");
    }
}

void wm_writer_string(WmJsonWriter *writer, const uint8_t *bytes, size_t length) {
    if (!wm_utf8_valid(bytes, length)) {
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

bool wm_fixed_string(const uint8_t *bytes, size_t size, size_t offset,
                     size_t field_size, const uint8_t **value, size_t *length) {
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

bool wm_cstring(const uint8_t *bytes, size_t size, size_t offset, const uint8_t **value,
                size_t *length) {
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

bool wm_read_sections(const uint8_t *data, size_t size, const char signature[4],
                      WmSection *sections, size_t *section_count) {
    if (data == NULL || size < 16 || memcmp(data, signature, 4) != 0 ||
        data[4] != 0xfe || data[5] != 0xff) {
        return false;
    }
    size_t file_size = wm_be32(data + 8);
    size_t cursor = wm_be16(data + 12);
    size_t count = wm_be16(data + 14);
    if (file_size > size || cursor > file_size || count > WM_RESOURCE_MAX_SECTIONS) {
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

void wm_writer_float(WmJsonWriter *writer, float value) {
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

void wm_normalize_commas(WmJsonWriter *writer) {
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
            memmove(writer->text + index + 1, writer->text + index, comma - index);
            writer->text[index] = ',';
            index = comma;
        }
    }
}

bool wm_writer_floats(WmJsonWriter *writer, const uint8_t *bytes, size_t size,
                      size_t offset, size_t count) {
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

bool wm_writer_bytes_array(WmJsonWriter *writer, const uint8_t *bytes, size_t size,
                           size_t offset, size_t count) {
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
