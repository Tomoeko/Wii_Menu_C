#ifndef WM_RESOURCE_LAYOUT_INTERNAL_H
#define WM_RESOURCE_LAYOUT_INTERNAL_H

#include "wii_menu/resources/resource_layout.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

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

uint16_t wm_be16(const uint8_t *bytes);
int16_t wm_signed_be16(const uint8_t *bytes);
uint32_t wm_be32(const uint8_t *bytes);
bool wm_range(size_t size, size_t offset, size_t length);
void wm_set_error(char *error, size_t error_size, const char *message);
bool wm_float(const uint8_t *bytes, size_t size, size_t offset, float *value);
void wm_writer_bytes(WmJsonWriter *writer, const char *text, size_t length);
void wm_writer_text(WmJsonWriter *writer, const char *text);
void wm_writer_format(WmJsonWriter *writer, const char *format, ...);
void wm_writer_indent(WmJsonWriter *writer, unsigned depth);
void wm_writer_string(WmJsonWriter *writer, const uint8_t *bytes, size_t length);
bool wm_fixed_string(const uint8_t *bytes, size_t size, size_t offset,
                     size_t field_size, const uint8_t **value, size_t *length);
bool wm_cstring(const uint8_t *bytes, size_t size, size_t offset, const uint8_t **value,
                size_t *length);
bool wm_read_sections(const uint8_t *data, size_t size, const char signature[4],
                      WmSection *sections, size_t *section_count);
void wm_writer_float(WmJsonWriter *writer, float value);
void wm_normalize_commas(WmJsonWriter *writer);
bool wm_writer_floats(WmJsonWriter *writer, const uint8_t *bytes, size_t size,
                      size_t offset, size_t count);
bool wm_writer_bytes_array(WmJsonWriter *writer, const uint8_t *bytes, size_t size,
                           size_t offset, size_t count);

#endif
