#define _POSIX_C_SOURCE 200809L

#include "wii_menu/board_store.h"

#include "wii_menu/json.h"

#include <errno.h>
#include <inttypes.h>
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

enum {
    STORE_MAX_MEMOS = 4096,
    STORE_MAX_TEXT_BYTES = 16 * 1024 * 1024,
    STORE_MAX_JSON_BYTES = 64 * 1024 * 1024
};

static void set_error(char *error, size_t capacity, const char *message) {
    if (error && capacity) snprintf(error, capacity, "%s", message);
}

static bool token_is(const WmJson *json, size_t token, WmJsonType type) {
    return token < json->count && json->tokens[token].type == type;
}

static bool json_bool(const WmJson *json, size_t token, bool *value) {
    if (!token_is(json, token, WM_JSON_BOOLEAN)) return false;
    const WmJsonToken *item = &json->tokens[token];
    size_t length = item->end - item->start;
    const char *source = json->source + item->start;
    if (length == 4 && memcmp(source, "true", 4) == 0) {
        *value = true;
        return true;
    }
    if (length == 5 && memcmp(source, "false", 5) == 0) {
        *value = false;
        return true;
    }
    return false;
}

static bool json_float(const WmJson *json, size_t token, float *value) {
    if (!token_is(json, token, WM_JSON_NUMBER)) return false;
    const WmJsonToken *item = &json->tokens[token];
    if (item->end <= item->start || item->end - item->start > 64) return false;
    const char *source = json->source;
    size_t cursor = item->start;
    double result = 0.0;
    bool negative = source[cursor] == '-';
    if (negative) cursor++;
    while (cursor < item->end && source[cursor] >= '0' && source[cursor] <= '9') {
        result = result * 10.0 + (source[cursor++] - '0');
    }
    if (cursor < item->end && source[cursor] == '.') {
        cursor++;
        double place = 0.1;
        while (cursor < item->end && source[cursor] >= '0' &&
               source[cursor] <= '9') {
            result += (source[cursor++] - '0') * place;
            place *= 0.1;
        }
    }
    if (cursor < item->end && (source[cursor] == 'e' || source[cursor] == 'E')) {
        cursor++;
        bool exponent_negative = source[cursor] == '-';
        if (source[cursor] == '-' || source[cursor] == '+') cursor++;
        unsigned exponent = 0;
        while (cursor < item->end && source[cursor] >= '0' &&
               source[cursor] <= '9') {
            if (exponent > 100) return false;
            exponent = exponent * 10 + (unsigned)(source[cursor++] - '0');
        }
        if (exponent > 100) return false;
        result *= pow(10.0, exponent_negative ? -(double)exponent :
                                           (double)exponent);
    }
    if (cursor != item->end) return false;
    result = negative ? -result : result;
    if (!isfinite(result) || result < -230.0 || result > 230.0) return false;
    *value = (float)result;
    return true;
}

static bool json_created_at(const WmJson *json, size_t token,
                            int64_t *value) {
    if (token == WM_JSON_INVALID) {
        *value = 0; /* Date-only files retain their original record order. */
        return true;
    }
    if (!token_is(json, token, WM_JSON_NUMBER)) return false;
    const WmJsonToken *item = &json->tokens[token];
    if (item->start >= item->end) return false;
    int64_t parsed = 0;
    for (size_t cursor = item->start; cursor < item->end; cursor++) {
        char digit = json->source[cursor];
        if (digit < '0' || digit > '9') return false;
        if (parsed > (INT64_C(253402300799999) - (digit - '0')) / 10) {
            return false;
        }
        parsed = parsed * 10 + (digit - '0');
    }
    *value = parsed;
    return true;
}

static bool valid_utf8(const char *text) {
    const unsigned char *bytes = (const unsigned char *)text;
    size_t length = strlen(text);
    for (size_t index = 0; index < length;) {
        unsigned first = bytes[index];
        if (first < 0x80) {
            index++;
            continue;
        }
        unsigned count, codepoint, minimum;
        if (first >= 0xc2 && first <= 0xdf) {
            count = 2;
            codepoint = first & 0x1f;
            minimum = 0x80;
        } else if (first >= 0xe0 && first <= 0xef) {
            count = 3;
            codepoint = first & 0x0f;
            minimum = 0x800;
        } else if (first >= 0xf0 && first <= 0xf4) {
            count = 4;
            codepoint = first & 7;
            minimum = 0x10000;
        } else {
            return false;
        }
        if (count > length - index) return false;
        for (unsigned byte = 1; byte < count; byte++) {
            if ((bytes[index + byte] & 0xc0) != 0x80) return false;
            codepoint = (codepoint << 6) | (bytes[index + byte] & 0x3f);
        }
        if (codepoint < minimum || codepoint > 0x10ffff ||
            (codepoint >= 0xd800 && codepoint <= 0xdfff)) return false;
        index += count;
    }
    return true;
}

static bool raw_nul_escape(const WmJson *json, size_t token) {
    const WmJsonToken *item = &json->tokens[token];
    for (size_t index = item->start; index < item->end;) {
        if (json->source[index] != '\\') {
            index++;
            continue;
        }
        size_t run = 0;
        while (index + run < item->end &&
               json->source[index + run] == '\\') run++;
        index += run;
        if ((run & 1u) != 0 && index + 5 <= item->end &&
            json->source[index] == 'u' &&
            memcmp(json->source + index + 1, "0000", 4) == 0) return true;
    }
    return false;
}

static char *copy_json_string(const WmJson *json, size_t token) {
    if (!token_is(json, token, WM_JSON_STRING) ||
        raw_nul_escape(json, token)) return NULL;
    const WmJsonToken *item = &json->tokens[token];
    size_t raw_length = item->end - item->start;
    if (raw_length > STORE_MAX_TEXT_BYTES) return NULL;
    char *copy = malloc(raw_length + 1);
    if (!copy) return NULL;
    if (!wm_json_copy(json, token, copy, raw_length + 1) ||
        !valid_utf8(copy)) {
        free(copy);
        return NULL;
    }
    return copy;
}

static void free_records(WmBoardMemo *records, size_t count) {
    if (!records) return;
    for (size_t index = 0; index < count; index++) {
        free((void *)records[index].id);
        free((void *)records[index].text);
    }
    free(records);
}

static bool parse_record(const WmJson *json, size_t token,
                         WmBoardMemo *memo, size_t *total_bytes) {
    if (!token_is(json, token, WM_JSON_OBJECT)) return false;
    size_t date = wm_json_member(json, token, "date");
    size_t position = wm_json_member(json, token, "position");
    if (!token_is(json, date, WM_JSON_OBJECT) ||
        !token_is(json, position, WM_JSON_OBJECT)) return false;
    memo->id = copy_json_string(json, wm_json_member(json, token, "id"));
    memo->text = copy_json_string(json, wm_json_member(json, token, "text"));
    if (!memo->id || !memo->id[0] || !memo->text ||
        !wm_json_integer(json, wm_json_member(json, date, "year"),
                         &memo->date.year) ||
        !wm_json_integer(json, wm_json_member(json, date, "month"),
                         &memo->date.month) ||
        !wm_json_integer(json, wm_json_member(json, date, "day"),
                         &memo->date.day) ||
        !wm_board_date_valid(memo->date) ||
        !json_created_at(json, wm_json_member(json, token, "createdAtMs"),
                         &memo->created_at_ms) ||
        !json_float(json, wm_json_member(json, position, "x"), &memo->x) ||
        !json_float(json, wm_json_member(json, position, "y"), &memo->y) ||
        memo->x < -230.0f || memo->x > 230.0f ||
        memo->y < -80.0f || memo->y > 180.0f ||
        !json_bool(json, wm_json_member(json, token, "read"), &memo->read)) {
        return false;
    }
    size_t id_bytes = strlen(memo->id), text_bytes = strlen(memo->text);
    if (id_bytes > STORE_MAX_TEXT_BYTES - *total_bytes ||
        text_bytes > STORE_MAX_TEXT_BYTES - *total_bytes - id_bytes) {
        return false;
    }
    *total_bytes += id_bytes + text_bytes;
    memo->has_position = true;
    return true;
}

WmBoardStoreStatus wm_board_store_load(const char *path, WmBoardScene *board,
                                       char *error, size_t error_capacity) {
    if (!path || !path[0] || !board) {
        set_error(error, error_capacity, "Invalid Board store input");
        return WM_BOARD_STORE_ERROR;
    }
    struct stat status;
    if (stat(path, &status) != 0) {
        if (errno == ENOENT) {
            set_error(error, error_capacity, "");
            return WM_BOARD_STORE_MISSING;
        }
        set_error(error, error_capacity, "Could not access Board store");
        return WM_BOARD_STORE_ERROR;
    }
    if (!S_ISREG(status.st_mode)) {
        set_error(error, error_capacity, "Board store is not a regular file");
        return WM_BOARD_STORE_ERROR;
    }
    WmJson json;
    if (!wm_json_load(&json, path, STORE_MAX_JSON_BYTES)) {
        set_error(error, error_capacity, "Invalid or oversized Board JSON");
        return WM_BOARD_STORE_ERROR;
    }
    int version = 0;
    size_t array = wm_json_member(&json, 0, "memos");
    bool valid = token_is(&json, 0, WM_JSON_OBJECT) &&
                 wm_json_integer(&json, wm_json_member(&json, 0,
                                 "schemaVersion"), &version) &&
                 version == 1 && token_is(&json, array, WM_JSON_ARRAY) &&
                 json.tokens[array].children <= STORE_MAX_MEMOS;
    size_t count = valid ? json.tokens[array].children : 0;
    WmBoardMemo *records = count ? calloc(count, sizeof(*records)) : NULL;
    if (count && !records) valid = false;
    size_t total_bytes = 0;
    for (size_t index = 0; valid && index < count; index++) {
        size_t token = wm_json_index(&json, array, index);
        valid = parse_record(&json, token, &records[index], &total_bytes);
        for (size_t previous = 0; valid && previous < index; previous++) {
            if (strcmp(records[previous].id, records[index].id) == 0) {
                valid = false;
            }
        }
    }
    if (valid) valid = wm_board_scene_set_memos(board, records, count);
    free_records(records, count);
    wm_json_free(&json);
    if (!valid) {
        set_error(error, error_capacity, "Malformed Board memo record");
        return WM_BOARD_STORE_ERROR;
    }
    set_error(error, error_capacity, "");
    return WM_BOARD_STORE_OK;
}

static size_t escaped_length(const char *value) {
    size_t length = 2;
    for (const unsigned char *byte = (const unsigned char *)value;
         *byte; byte++) {
        if (*byte < 0x20) length += 6;
        else if (*byte == '"' || *byte == '\\') length += 2;
        else length++;
    }
    return length;
}

static void write_string(FILE *file, const char *value) {
    static const char hex[] = "0123456789abcdef";
    fputc('"', file);
    for (const unsigned char *byte = (const unsigned char *)value;
         *byte; byte++) {
        if (*byte == '"' || *byte == '\\') {
            fputc('\\', file);
            fputc(*byte, file);
        } else if (*byte < 0x20) {
            fputs("\\u00", file);
            fputc(hex[*byte >> 4], file);
            fputc(hex[*byte & 15], file);
        } else {
            fputc(*byte, file);
        }
    }
    fputc('"', file);
}

static bool json_number(float value, char output[32]) {
    if (!isfinite(value)) return false;
    int length = snprintf(output, 32, "%.9g", (double)value);
    if (length <= 0 || length >= 32) return false;
    for (int index = 0; index < length; index++) {
        if (output[index] == ',') output[index] = '.';
    }
    return true;
}

bool wm_board_store_save(const char *path, const WmBoardScene *board,
                         char *error, size_t error_capacity) {
    if (!path || !path[0] || !board) {
        set_error(error, error_capacity, "Invalid Board store input");
        return false;
    }
    size_t count = wm_board_scene_memo_count(board);
    if (count > STORE_MAX_MEMOS) {
        set_error(error, error_capacity, "Too many Board memos");
        return false;
    }
    size_t json_size = 64, text_size = 0;
    for (size_t index = 0; index < count; index++) {
        WmBoardMemo memo;
        if (!wm_board_scene_get_memo(board, index, &memo) ||
            !memo.id || !memo.id[0] || !memo.text ||
            !valid_utf8(memo.id) || !valid_utf8(memo.text) ||
            !wm_board_date_valid(memo.date) ||
            memo.created_at_ms < 0 ||
            memo.created_at_ms > INT64_C(253402300799999) ||
            !isfinite(memo.x) || !isfinite(memo.y) ||
            memo.x < -230.0f || memo.x > 230.0f ||
            memo.y < -80.0f || memo.y > 180.0f) {
            set_error(error, error_capacity, "Invalid Board memo");
            return false;
        }
        for (size_t previous = 0; previous < index; previous++) {
            WmBoardMemo earlier;
            if (!wm_board_scene_get_memo(board, previous, &earlier) ||
                strcmp(earlier.id, memo.id) == 0) {
                set_error(error, error_capacity, "Duplicate Board memo ID");
                return false;
            }
        }
        size_t id_bytes = strlen(memo.id), text_bytes = strlen(memo.text);
        if (id_bytes > STORE_MAX_TEXT_BYTES - text_size ||
            text_bytes > STORE_MAX_TEXT_BYTES - text_size - id_bytes) {
            set_error(error, error_capacity, "Board memo text limit exceeded");
            return false;
        }
        text_size += id_bytes + text_bytes;
        size_t estimated = 224 + escaped_length(memo.id) +
                           escaped_length(memo.text);
        if (estimated > STORE_MAX_JSON_BYTES - json_size) {
            set_error(error, error_capacity, "Board store size limit exceeded");
            return false;
        }
        json_size += estimated;
    }
    size_t path_length = strlen(path);
    static const char suffix[] = ".tmp.XXXXXX";
    if (path_length > SIZE_MAX - sizeof(suffix)) {
        set_error(error, error_capacity, "Board store path too long");
        return false;
    }
    char *temporary = malloc(path_length + sizeof(suffix));
    if (!temporary) {
        set_error(error, error_capacity, "Could not allocate Board store path");
        return false;
    }
    memcpy(temporary, path, path_length);
    memcpy(temporary + path_length, suffix, sizeof(suffix));
    int descriptor = mkstemp(temporary);
    if (descriptor < 0) {
        free(temporary);
        set_error(error, error_capacity, "Could not create Board store temporary file");
        return false;
    }
    FILE *file = fdopen(descriptor, "wb");
    if (!file) {
        close(descriptor);
        unlink(temporary);
        free(temporary);
        set_error(error, error_capacity, "Could not open Board store temporary file");
        return false;
    }
    fputs("{\n    \"schemaVersion\": 1,\n    \"memos\": [\n", file);
    for (size_t index = 0; index < count; index++) {
        WmBoardMemo memo;
        wm_board_scene_get_memo(board, index, &memo);
        char x[32], y[32];
        if (!json_number(memo.x, x) || !json_number(memo.y, y)) {
            fclose(file);
            unlink(temporary);
            free(temporary);
            set_error(error, error_capacity, "Invalid Board memo position");
            return false;
        }
        fputs("        {\"id\": ", file);
        write_string(file, memo.id);
        fputs(", \"text\": ", file);
        write_string(file, memo.text);
        fprintf(file,
                ", \"date\": {\"year\": %d, \"month\": %d, \"day\": %d}, "
                "\"createdAtMs\": %" PRId64 ", "
                "\"position\": {\"x\": %s, \"y\": %s}, \"read\": %s}%s\n",
                memo.date.year, memo.date.month, memo.date.day,
                memo.created_at_ms,
                x, y, memo.read ? "true" : "false",
                index + 1 < count ? "," : "");
    }
    fputs("    ]\n}\n", file);
    bool success = !ferror(file) && fflush(file) == 0 &&
                   fsync(fileno(file)) == 0;
    if (fclose(file) != 0) success = false;
    if (success) success = rename(temporary, path) == 0;
    if (!success) unlink(temporary);
    free(temporary);
    set_error(error, error_capacity,
              success ? "" : "Could not atomically save Board memos");
    return success;
}
