#include "wii_menu/json.h"

#include <ctype.h>
#include <limits.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct Parser {
    WmJson *json;
    size_t position;
    unsigned depth;
} Parser;

static void skip_space(Parser *parser) {
    while (parser->position < parser->json->length &&
           (parser->json->source[parser->position] == ' ' ||
            parser->json->source[parser->position] == '\n' ||
            parser->json->source[parser->position] == '\r' ||
            parser->json->source[parser->position] == '\t')) {
        parser->position++;
    }
}

static size_t add_token(Parser *parser, WmJsonType type, size_t start) {
    WmJson *json = parser->json;
    if (json->count == json->capacity) {
        size_t capacity = json->capacity ? json->capacity * 2 : 256;
        /* The largest stock layout export contains more than 262144 tokens. */
        if (capacity > 1048576) return WM_JSON_INVALID;
        WmJsonToken *tokens = realloc(json->tokens, capacity * sizeof(*tokens));
        if (!tokens) return WM_JSON_INVALID;
        json->tokens = tokens;
        json->capacity = capacity;
    }
    size_t index = json->count++;
    json->tokens[index] = (WmJsonToken){
        .type = type,
        .start = start,
        .end = start,
        .next = index + 1,
        .children = 0
    };
    return index;
}

static bool parse_string(Parser *parser, size_t *result) {
    WmJson *json = parser->json;
    if (parser->position >= json->length || json->source[parser->position] != '"') {
        return false;
    }
    size_t index = add_token(parser, WM_JSON_STRING, ++parser->position);
    if (index == WM_JSON_INVALID) return false;
    while (parser->position < json->length) {
        unsigned char c = (unsigned char)json->source[parser->position++];
        if (c == '"') {
            json->tokens[index].end = parser->position - 1;
            *result = index;
            return true;
        }
        if (c < 0x20) return false;
        if (c != '\\') continue;
        if (parser->position >= json->length) return false;
        c = (unsigned char)json->source[parser->position++];
        if (strchr("\"\\/bfnrt", c)) continue;
        if (c != 'u' || parser->position + 4 > json->length) return false;
        for (int digit = 0; digit < 4; digit++) {
            if (!isxdigit((unsigned char)json->source[parser->position++])) return false;
        }
    }
    return false;
}

static bool parse_value(Parser *parser, size_t *result) {
    WmJson *json = parser->json;
    skip_space(parser);
    if (parser->position >= json->length || parser->depth >= 64) return false;
    char c = json->source[parser->position];
    if (c == '"') return parse_string(parser, result);

    if (c == '{' || c == '[') {
        bool object = c == '{';
        size_t index = add_token(parser, object ? WM_JSON_OBJECT : WM_JSON_ARRAY,
                                 parser->position++);
        if (index == WM_JSON_INVALID) return false;
        parser->depth++;
        skip_space(parser);
        char close = object ? '}' : ']';
        while (parser->position < json->length && json->source[parser->position] != close) {
            size_t child;
            if (object) {
                if (!parse_string(parser, &child)) return false;
                skip_space(parser);
                if (parser->position >= json->length || json->source[parser->position++] != ':') {
                    return false;
                }
            }
            if (!parse_value(parser, &child)) return false;
            json->tokens[index].children++;
            skip_space(parser);
            if (parser->position < json->length && json->source[parser->position] == ',') {
                parser->position++;
                skip_space(parser);
                if (parser->position < json->length && json->source[parser->position] == close) {
                    return false;
                }
            } else {
                break;
            }
        }
        if (parser->position >= json->length || json->source[parser->position++] != close) {
            return false;
        }
        parser->depth--;
        json->tokens[index].end = parser->position;
        json->tokens[index].next = json->count;
        *result = index;
        return true;
    }

    size_t start = parser->position;
    WmJsonType type;
    const char *literal = NULL;
    if (c == 't') { type = WM_JSON_BOOLEAN; literal = "true"; }
    else if (c == 'f') { type = WM_JSON_BOOLEAN; literal = "false"; }
    else if (c == 'n') { type = WM_JSON_NULL; literal = "null"; }
    else if (c == '-' || (c >= '0' && c <= '9')) {
        type = WM_JSON_NUMBER;
        if (c == '-') parser->position++;
        if (parser->position >= json->length) return false;
        if (json->source[parser->position] == '0') {
            parser->position++;
        } else {
            if (json->source[parser->position] < '1' || json->source[parser->position] > '9') {
                return false;
            }
            while (parser->position < json->length &&
                   isdigit((unsigned char)json->source[parser->position])) parser->position++;
        }
        if (parser->position < json->length && json->source[parser->position] == '.') {
            parser->position++;
            if (parser->position >= json->length ||
                !isdigit((unsigned char)json->source[parser->position])) return false;
            while (parser->position < json->length &&
                   isdigit((unsigned char)json->source[parser->position])) parser->position++;
        }
        if (parser->position < json->length &&
            (json->source[parser->position] == 'e' ||
             json->source[parser->position] == 'E')) {
            parser->position++;
            if (parser->position < json->length &&
                (json->source[parser->position] == '+' ||
                 json->source[parser->position] == '-')) parser->position++;
            if (parser->position >= json->length ||
                !isdigit((unsigned char)json->source[parser->position])) return false;
            while (parser->position < json->length &&
                   isdigit((unsigned char)json->source[parser->position])) parser->position++;
        }
    } else {
        return false;
    }
    if (literal) {
        size_t length = strlen(literal);
        if (parser->position + length > json->length ||
            memcmp(json->source + parser->position, literal, length) != 0) return false;
        parser->position += length;
    }
    size_t index = add_token(parser, type, start);
    if (index == WM_JSON_INVALID) return false;
    json->tokens[index].end = parser->position;
    *result = index;
    return true;
}

static bool parse_source(WmJson *json) {
    Parser parser = { .json = json, .position = 0, .depth = 0 };
    size_t root;
    bool success = parse_value(&parser, &root);
    skip_space(&parser);
    if (!success || root != 0 || parser.position != json->length) {
        wm_json_free(json);
        return false;
    }
    return true;
}

bool wm_json_parse(WmJson *json, const char *source, size_t length) {
    if (!json) return false;
    memset(json, 0, sizeof(*json));
    if (!source || length == 0 || length == SIZE_MAX) return false;
    json->source = malloc(length + 1);
    if (!json->source) return false;
    memcpy(json->source, source, length);
    json->source[length] = '\0';
    json->length = length;
    return parse_source(json);
}

bool wm_json_load(WmJson *json, const char *path, size_t max_bytes) {
    memset(json, 0, sizeof(*json));
    FILE *file = fopen(path, "rb");
    if (!file) return false;
    if (fseek(file, 0, SEEK_END) != 0) { fclose(file); return false; }
    long length = ftell(file);
    if (length <= 0 || (unsigned long)length > max_bytes || fseek(file, 0, SEEK_SET) != 0) {
        fclose(file);
        return false;
    }
    json->source = malloc((size_t)length + 1);
    if (!json->source) { fclose(file); return false; }
    size_t count = fread(json->source, 1, (size_t)length, file);
    fclose(file);
    if (count != (size_t)length) { wm_json_free(json); return false; }
    json->length = count;
    json->source[count] = '\0';
    return parse_source(json);
}

void wm_json_free(WmJson *json) {
    free(json->source);
    free(json->tokens);
    memset(json, 0, sizeof(*json));
}

size_t wm_json_member(const WmJson *json, size_t object, const char *key) {
    if (object >= json->count || json->tokens[object].type != WM_JSON_OBJECT) {
        return WM_JSON_INVALID;
    }
    size_t cursor = object + 1;
    while (cursor < json->tokens[object].next) {
        size_t value = json->tokens[cursor].next;
        if (value >= json->tokens[object].next) return WM_JSON_INVALID;
        if (wm_json_equals(json, cursor, key)) return value;
        cursor = json->tokens[value].next;
    }
    return WM_JSON_INVALID;
}

size_t wm_json_index(const WmJson *json, size_t array, size_t index) {
    if (array >= json->count || json->tokens[array].type != WM_JSON_ARRAY ||
        index >= json->tokens[array].children) return WM_JSON_INVALID;
    size_t cursor = array + 1;
    while (index--) cursor = json->tokens[cursor].next;
    return cursor;
}

bool wm_json_equals(const WmJson *json, size_t token, const char *value) {
    if (token >= json->count || json->tokens[token].type != WM_JSON_STRING) return false;
    const WmJsonToken *item = &json->tokens[token];
    size_t length = item->end - item->start;
    return strlen(value) == length && memcmp(json->source + item->start, value, length) == 0;
}

static bool decode_hex4(const char *source, size_t length, size_t *cursor,
                        unsigned *codepoint) {
    if (*cursor + 4 > length) return false;
    unsigned value = 0;
    for (int index = 0; index < 4; index++) {
        unsigned char digit = (unsigned char)source[(*cursor)++];
        value <<= 4;
        if (digit >= '0' && digit <= '9') value |= digit - '0';
        else if (digit >= 'a' && digit <= 'f') value |= digit - 'a' + 10;
        else if (digit >= 'A' && digit <= 'F') value |= digit - 'A' + 10;
        else return false;
    }
    *codepoint = value;
    return true;
}

static bool append_utf8(char *destination, size_t capacity, size_t *output,
                        unsigned codepoint) {
    unsigned char bytes[4];
    size_t length;
    if (codepoint <= 0x7f) {
        bytes[0] = (unsigned char)codepoint;
        length = 1;
    } else if (codepoint <= 0x7ff) {
        bytes[0] = (unsigned char)(0xc0 | (codepoint >> 6));
        bytes[1] = (unsigned char)(0x80 | (codepoint & 0x3f));
        length = 2;
    } else if (codepoint <= 0xffff) {
        bytes[0] = (unsigned char)(0xe0 | (codepoint >> 12));
        bytes[1] = (unsigned char)(0x80 | ((codepoint >> 6) & 0x3f));
        bytes[2] = (unsigned char)(0x80 | (codepoint & 0x3f));
        length = 3;
    } else if (codepoint <= 0x10ffff) {
        bytes[0] = (unsigned char)(0xf0 | (codepoint >> 18));
        bytes[1] = (unsigned char)(0x80 | ((codepoint >> 12) & 0x3f));
        bytes[2] = (unsigned char)(0x80 | ((codepoint >> 6) & 0x3f));
        bytes[3] = (unsigned char)(0x80 | (codepoint & 0x3f));
        length = 4;
    } else {
        return false;
    }
    if (*output + length >= capacity) return false;
    memcpy(destination + *output, bytes, length);
    *output += length;
    return true;
}

bool wm_json_copy(const WmJson *json, size_t token, char *destination, size_t capacity) {
    if (!capacity || token >= json->count || json->tokens[token].type != WM_JSON_STRING) {
        return false;
    }
    const WmJsonToken *item = &json->tokens[token];
    size_t output = 0;
    for (size_t cursor = item->start; cursor < item->end; cursor++) {
        unsigned char c = (unsigned char)json->source[cursor];
        if (c == '\\' && ++cursor < item->end) {
            c = (unsigned char)json->source[cursor];
            if (c == 'n') c = '\n';
            else if (c == 'r') c = '\r';
            else if (c == 't') c = '\t';
            else if (c == 'b') c = '\b';
            else if (c == 'f') c = '\f';
            else if (c == 'u') {
                unsigned codepoint;
                size_t digits = cursor + 1;
                if (!decode_hex4(json->source, item->end, &digits, &codepoint)) return false;
                if (codepoint >= 0xd800 && codepoint <= 0xdbff) {
                    if (digits + 6 > item->end || json->source[digits] != '\\' ||
                        json->source[digits + 1] != 'u') return false;
                    digits += 2;
                    unsigned low;
                    if (!decode_hex4(json->source, item->end, &digits, &low) ||
                        low < 0xdc00 || low > 0xdfff) return false;
                    codepoint = 0x10000 + ((codepoint - 0xd800) << 10) + (low - 0xdc00);
                } else if (codepoint >= 0xdc00 && codepoint <= 0xdfff) {
                    return false;
                }
                if (!append_utf8(destination, capacity, &output, codepoint)) return false;
                cursor = digits - 1;
                continue;
            }
        }
        if (output + 1 >= capacity) return false;
        destination[output++] = (char)c;
    }
    destination[output] = '\0';
    return true;
}

bool wm_json_integer(const WmJson *json, size_t token, int *value) {
    if (token >= json->count || json->tokens[token].type != WM_JSON_NUMBER) return false;
    const WmJsonToken *item = &json->tokens[token];
    char buffer[32];
    size_t length = item->end - item->start;
    if (!length || length >= sizeof(buffer)) return false;
    memcpy(buffer, json->source + item->start, length);
    buffer[length] = '\0';
    char *end;
    long number = strtol(buffer, &end, 10);
    if (*end || number < INT_MIN || number > INT_MAX) return false;
    *value = (int)number;
    return true;
}
