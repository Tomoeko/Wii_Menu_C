#include "wii_menu/resource_bmg.h"

#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

enum {
    BMG_HEADER_BYTES = 32,
    BMG_MAX_FILE_BYTES = 64 * 1024 * 1024,
    BMG_MAX_MESSAGE_BYTES = 1024 * 1024,
    BMG_MAX_SECTIONS = 64,
    BMG_PATH_CAPACITY = 4096
};

struct WmBmg {
    uint8_t *source;
    size_t source_size;
    char **texts;
    size_t count;
    size_t info_offset;
    size_t info_stride;
};

static uint16_t read_be16(const uint8_t *bytes) {
    return (uint16_t)(((uint16_t)bytes[0] << 8) | bytes[1]);
}

static uint32_t read_be32(const uint8_t *bytes) {
    return ((uint32_t)bytes[0] << 24) | ((uint32_t)bytes[1] << 16) |
           ((uint32_t)bytes[2] << 8) | bytes[3];
}

static bool fits(size_t size, size_t offset, size_t length) {
    return offset <= size && length <= size - offset;
}

static void set_error(char *error, size_t capacity, const char *reason) {
    if (error && capacity) snprintf(error, capacity, "%s", reason);
}

static size_t utf8_width(uint32_t point) {
    if (point < 0x80) return 1;
    if (point < 0x800) return 2;
    if (point < 0x10000) return 3;
    return 4;
}

static void put_utf8(char *output, size_t offset, uint32_t point) {
    if (point < 0x80) {
        output[offset] = (char)point;
    } else if (point < 0x800) {
        output[offset] = (char)(0xc0 | (point >> 6));
        output[offset + 1] = (char)(0x80 | (point & 0x3f));
    } else if (point < 0x10000) {
        output[offset] = (char)(0xe0 | (point >> 12));
        output[offset + 1] = (char)(0x80 | ((point >> 6) & 0x3f));
        output[offset + 2] = (char)(0x80 | (point & 0x3f));
    } else {
        output[offset] = (char)(0xf0 | (point >> 18));
        output[offset + 1] = (char)(0x80 | ((point >> 12) & 0x3f));
        output[offset + 2] = (char)(0x80 | ((point >> 6) & 0x3f));
        output[offset + 3] = (char)(0x80 | (point & 0x3f));
    }
}

static bool decode_message(const uint8_t *strings, size_t string_size,
                           size_t start, char *output, size_t *output_size,
                           char *error, size_t error_capacity) {
    if (!fits(string_size, start, 2)) {
        set_error(error, error_capacity, "BMG message offset is out of bounds.");
        return false;
    }
    size_t cursor = start;
    size_t written = 0;
    while (fits(string_size, cursor, 2)) {
        uint16_t unit = read_be16(strings + cursor);
        if (unit == 0) {
            if (output) output[written] = '\0';
            *output_size = written;
            return true;
        }
        if (unit == 0x001a) {
            if (!fits(string_size, cursor, 3)) {
                set_error(error, error_capacity, "Truncated BMG control packet.");
                return false;
            }
            size_t packet_size = strings[cursor + 2];
            if (packet_size < 4 || !fits(string_size, cursor, packet_size)) {
                set_error(error, error_capacity, "Invalid BMG control packet.");
                return false;
            }
            cursor += packet_size;
            continue;
        }
        cursor += 2;
        uint32_t point = unit;
        if (unit >= 0xd800 && unit <= 0xdbff) {
            if (!fits(string_size, cursor, 2)) {
                set_error(error, error_capacity, "Truncated BMG surrogate pair.");
                return false;
            }
            uint16_t lower = read_be16(strings + cursor);
            if (lower < 0xdc00 || lower > 0xdfff) {
                set_error(error, error_capacity, "Invalid BMG surrogate pair.");
                return false;
            }
            cursor += 2;
            point = 0x10000u + ((uint32_t)(unit - 0xd800u) << 10) +
                    (uint32_t)(lower - 0xdc00u);
        } else if (unit >= 0xdc00 && unit <= 0xdfff) {
            set_error(error, error_capacity, "Unpaired BMG low surrogate.");
            return false;
        }
        size_t width = utf8_width(point);
        if (written > BMG_MAX_MESSAGE_BYTES - width) {
            set_error(error, error_capacity, "BMG message is too long.");
            return false;
        }
        if (output) put_utf8(output, written, point);
        written += width;
    }
    set_error(error, error_capacity, "Unterminated BMG message.");
    return false;
}

void wm_bmg_destroy(WmBmg *bmg) {
    if (!bmg) return;
    for (size_t index = 0; index < bmg->count; index++) {
        free(bmg->texts[index]);
    }
    free(bmg->texts);
    free(bmg->source);
    free(bmg);
}

WmBmg *wm_bmg_parse(const uint8_t *data, size_t size,
                    char *error, size_t error_capacity) {
    if (!data || size < BMG_HEADER_BYTES || size > BMG_MAX_FILE_BYTES ||
        memcmp(data, "MESGbmg1", 8) != 0 || data[16] != 2) {
        set_error(error, error_capacity,
                  "Expected a big-endian UTF-16 BMG file.");
        return NULL;
    }
    size_t total = read_be32(data + 8);
    size_t sections = read_be32(data + 12);
    if (total < BMG_HEADER_BYTES || total > size ||
        sections == 0 || sections > BMG_MAX_SECTIONS) {
        set_error(error, error_capacity, "Invalid BMG header bounds.");
        return NULL;
    }
    WmBmg *bmg = calloc(1, sizeof(*bmg));
    if (!bmg) {
        set_error(error, error_capacity, "Out of memory parsing BMG.");
        return NULL;
    }
    bmg->source = malloc(total);
    if (!bmg->source) {
        set_error(error, error_capacity, "Out of memory parsing BMG.");
        wm_bmg_destroy(bmg);
        return NULL;
    }
    memcpy(bmg->source, data, total);
    bmg->source_size = total;
    size_t info_offset = 0, info_size = 0;
    size_t dat_offset = 0, dat_size = 0;
    bool has_info = false, has_data = false;
    size_t offset = BMG_HEADER_BYTES;
    for (size_t index = 0; index < sections; index++) {
        if (!fits(total, offset, 8)) {
            set_error(error, error_capacity, "Truncated BMG section.");
            wm_bmg_destroy(bmg);
            return NULL;
        }
        size_t length = read_be32(bmg->source + offset + 4);
        if (length < 8 || !fits(total, offset, length)) {
            set_error(error, error_capacity, "Invalid BMG section size.");
            wm_bmg_destroy(bmg);
            return NULL;
        }
        if (memcmp(bmg->source + offset, "INF1", 4) == 0) {
            if (has_info) {
                set_error(error, error_capacity, "Duplicate BMG INF1 section.");
                wm_bmg_destroy(bmg);
                return NULL;
            }
            info_offset = offset + 8;
            info_size = length - 8;
            has_info = true;
        } else if (memcmp(bmg->source + offset, "DAT1", 4) == 0) {
            if (has_data) {
                set_error(error, error_capacity, "Duplicate BMG DAT1 section.");
                wm_bmg_destroy(bmg);
                return NULL;
            }
            dat_offset = offset + 8;
            dat_size = length - 8;
            has_data = true;
        }
        offset += length;
    }
    if (info_size < 8 || dat_size < 2) {
        set_error(error, error_capacity, "Missing BMG message sections.");
        wm_bmg_destroy(bmg);
        return NULL;
    }
    const uint8_t *info = bmg->source + info_offset;
    const uint8_t *strings = bmg->source + dat_offset;
    size_t count = read_be16(info);
    size_t stride = read_be16(info + 2);
    if (stride < 4 || count > (info_size - 8) / stride) {
        set_error(error, error_capacity, "Invalid BMG message records.");
        wm_bmg_destroy(bmg);
        return NULL;
    }
    bmg->texts = calloc(count ? count : 1, sizeof(*bmg->texts));
    if (!bmg->texts) {
        set_error(error, error_capacity, "Out of memory parsing BMG.");
        wm_bmg_destroy(bmg);
        return NULL;
    }
    bmg->count = count;
    bmg->info_offset = info_offset + 8;
    bmg->info_stride = stride;
    for (size_t index = 0; index < count; index++) {
        size_t start = read_be32(info + 8 + index * stride);
        size_t length = 0;
        if (!decode_message(strings, dat_size, start, NULL, &length,
                            error, error_capacity)) {
            wm_bmg_destroy(bmg);
            return NULL;
        }
        bmg->texts[index] = malloc(length + 1);
        if (!bmg->texts[index]) {
            set_error(error, error_capacity, "Out of memory parsing BMG.");
            wm_bmg_destroy(bmg);
            return NULL;
        }
        size_t second_length = 0;
        if (!decode_message(strings, dat_size, start, bmg->texts[index],
                            &second_length, error, error_capacity) ||
            second_length != length) {
            wm_bmg_destroy(bmg);
            return NULL;
        }
    }
    return bmg;
}

WmBmg *wm_bmg_load_file(const char *path,
                        char *error, size_t error_capacity) {
    if (!path || !path[0]) {
        set_error(error, error_capacity, "Missing BMG path.");
        return NULL;
    }
    FILE *file = fopen(path, "rb");
    if (!file || fseek(file, 0, SEEK_END) != 0) {
        if (file) fclose(file);
        set_error(error, error_capacity, "Could not read local BMG file.");
        return NULL;
    }
    long length = ftell(file);
    if (length < BMG_HEADER_BYTES || length > BMG_MAX_FILE_BYTES ||
        fseek(file, 0, SEEK_SET) != 0) {
        fclose(file);
        set_error(error, error_capacity, "Invalid local BMG file size.");
        return NULL;
    }
    uint8_t *data = malloc((size_t)length);
    if (!data) {
        fclose(file);
        set_error(error, error_capacity, "Out of memory reading BMG.");
        return NULL;
    }
    bool complete = fread(data, 1, (size_t)length, file) == (size_t)length;
    if (fclose(file) != 0) complete = false;
    WmBmg *result = complete
                        ? wm_bmg_parse(data, (size_t)length,
                                       error, error_capacity)
                        : NULL;
    free(data);
    if (!complete) set_error(error, error_capacity,
                             "Could not read local BMG file.");
    return result;
}

WmBmg *wm_bmg_load_assets(const char *assets_directory, const char *locale,
                          char *error, size_t error_capacity) {
    if (!assets_directory || !locale || strlen(locale) != 3) {
        set_error(error, error_capacity, "Expected a three-letter BMG locale.");
        return NULL;
    }
    char normalized[4];
    for (size_t index = 0; index < 3; index++) {
        char value = locale[index];
        if (value >= 'A' && value <= 'Z') value = (char)(value - 'A' + 'a');
        if (value < 'a' || value > 'z') {
            set_error(error, error_capacity, "Invalid BMG locale.");
            return NULL;
        }
        normalized[index] = value;
    }
    normalized[3] = '\0';
    char path[BMG_PATH_CAPACITY];
    int length = snprintf(path, sizeof(path),
                          "%s/messages/%s/ipl_common.bmg",
                          assets_directory, normalized);
    if (length < 0 || length >= (int)sizeof(path)) {
        set_error(error, error_capacity, "BMG asset path is too long.");
        return NULL;
    }
    return wm_bmg_load_file(path, error, error_capacity);
}

size_t wm_bmg_count(const WmBmg *bmg) {
    return bmg ? bmg->count : 0;
}

const char *wm_bmg_text(const WmBmg *bmg, unsigned message_id) {
    if (!bmg || message_id >= bmg->count) return NULL;
    return bmg->texts[message_id];
}

const char *wm_bmg_message(void *context, unsigned message_id) {
    return wm_bmg_text(context, message_id);
}

const uint8_t *wm_bmg_attributes(const WmBmg *bmg, unsigned message_id,
                                  size_t *size) {
    if (size) *size = 0;
    if (!bmg || message_id >= bmg->count) return NULL;
    if (size) *size = bmg->info_stride - 4;
    return bmg->source + bmg->info_offset +
           (size_t)message_id * bmg->info_stride + 4;
}
