#define _POSIX_C_SOURCE 200809L

#include "board_contact_format.h"

#include <stdint.h>
#include <stdlib.h>
#include <string.h>

enum { CONTACT_MAX_OUTPUT_BYTES = CONTACT_MAX_JSON_BYTES };

typedef struct JsonBuffer {
    char *data;
    size_t length;
    size_t capacity;
} JsonBuffer;

static bool utf8_next(const unsigned char *text, size_t length, size_t *offset,
                      uint32_t *codepoint, size_t *units) {
    if (*offset >= length)
        return false;
    unsigned first = text[*offset];
    size_t count = first < 0x80                     ? 1
                   : first >= 0xC2 && first <= 0xDF ? 2
                   : first >= 0xE0 && first <= 0xEF ? 3
                   : first >= 0xF0 && first <= 0xF4 ? 4
                                                    : 0;
    if (count == 0 || count > length - *offset)
        return false;
    uint32_t value = first & (count == 1   ? 0x7Fu
                              : count == 2 ? 0x1Fu
                              : count == 3 ? 0x0Fu
                                           : 0x07u);
    for (size_t index = 1; index < count; index++) {
        unsigned next = text[*offset + index];
        if ((next & 0xC0u) != 0x80u)
            return false;
        value = (value << 6) | (next & 0x3Fu);
    }
    if ((count == 2 && value < 0x80u) || (count == 3 && value < 0x800u) ||
        (count == 4 && value < 0x10000u) || value > 0x10FFFFu ||
        (value >= 0xD800u && value <= 0xDFFFu)) {
        return false;
    }
    *offset += count;
    if (codepoint)
        *codepoint = value;
    if (units)
        *units = count == 4 ? 2 : 1;
    return true;
}

bool contact_json_utf8_valid(const char *text, size_t length) {
    size_t offset = 0;
    while (offset < length) {
        if (!utf8_next((const unsigned char *)text, length, &offset, NULL, NULL))
            return false;
    }
    return true;
}

static bool unicode_space(uint32_t codepoint) {
    return codepoint == 0x20u || (codepoint >= 0x09u && codepoint <= 0x0Du) ||
           codepoint == 0xA0u || codepoint == 0x1680u ||
           (codepoint >= 0x2000u && codepoint <= 0x200Au) || codepoint == 0x2028u ||
           codepoint == 0x2029u || codepoint == 0x202Fu || codepoint == 0x205Fu ||
           codepoint == 0x3000u || codepoint == 0xFEFFu;
}

bool contact_nickname_valid(const char *text) {
    if (!text)
        return false;
    size_t length = strnlen(text, CONTACT_NICKNAME_BYTES);
    if (length == CONTACT_NICKNAME_BYTES)
        return false;
    size_t offset = 0, units = 0;
    bool nonspace = false;
    while (offset < length) {
        uint32_t codepoint;
        size_t character_units;
        if (!utf8_next((const unsigned char *)text, length, &offset, &codepoint,
                       &character_units))
            return false;
        if (codepoint == 0 || codepoint == '\r' || codepoint == '\n')
            return false;
        if (!unicode_space(codepoint))
            nonspace = true;
        units += character_units;
        if (units > 10)
            return false;
    }
    return nonspace;
}

/* Persisted contacts intentionally admit older e-mail records. New
 * registration is checked by AddressEdit's stricter source predicate. */
bool contact_stored_address_valid(bool wii, const char *text) {
    if (!text)
        return false;
    size_t length = strnlen(text, CONTACT_ADDRESS_BYTES);
    if (length == CONTACT_ADDRESS_BYTES)
        return false;
    if (wii) {
        if (length != 16)
            return false;
        for (size_t index = 0; index < length; index++) {
            if (text[index] < '0' || text[index] > '9')
                return false;
        }
        return true;
    }
    size_t offset = 0, units = 0, at = SIZE_MAX;
    while (offset < length) {
        uint32_t codepoint;
        size_t character_units;
        size_t start = offset;
        if (!utf8_next((const unsigned char *)text, length, &offset, &codepoint,
                       &character_units) ||
            codepoint == 0 || unicode_space(codepoint))
            return false;
        if (codepoint == '@') {
            if (at != SIZE_MAX)
                return false;
            at = start;
        }
        units += character_units;
        if (units > 99)
            return false;
    }
    return at != SIZE_MAX && at > 0 && at + 1 < length;
}

bool contact_parse(const WmJson *json, size_t token, StoredContact *contact) {
    if (token >= json->count)
        return false;
    const WmJsonToken *item = &json->tokens[token];
    if (item->type == WM_JSON_NULL)
        return true;
    if (item->type != WM_JSON_OBJECT)
        return false;
    char kind[8];
    if (!wm_json_copy_text(json, wm_json_member(json, token, "kind"), kind,
                           sizeof(kind)) ||
        (strcmp(kind, "wii") != 0 && strcmp(kind, "email") != 0) ||
        !wm_json_copy_text(json, wm_json_member(json, token, "address"),
                           contact->address, sizeof(contact->address)) ||
        !wm_json_copy_text(json, wm_json_member(json, token, "nickname"),
                           contact->nickname, sizeof(contact->nickname)))
        return false;
    contact->wii = strcmp(kind, "wii") == 0;
    if (!contact_stored_address_valid(contact->wii, contact->address) ||
        !contact_nickname_valid(contact->nickname))
        return false;
    size_t confirmed = wm_json_member(json, token, "confirmed");
    contact->confirmed = true;
    if (confirmed != WM_JSON_INVALID) {
        if (confirmed >= json->count || json->tokens[confirmed].type != WM_JSON_BOOLEAN)
            return false;
        contact->confirmed = json->source[json->tokens[confirmed].start] == 't';
    }
    size_t raw_length = item->end - item->start;
    contact->source_json = malloc(raw_length + 1);
    if (!contact->source_json)
        return false;
    memcpy(contact->source_json, json->source + item->start, raw_length);
    contact->source_json[raw_length] = '\0';
    contact->occupied = true;
    return true;
}

static bool append_bytes(JsonBuffer *buffer, const char *bytes, size_t count) {
    if (count > CONTACT_MAX_OUTPUT_BYTES - buffer->length)
        return false;
    size_t needed = buffer->length + count + 1;
    if (needed > buffer->capacity) {
        size_t capacity = buffer->capacity ? buffer->capacity : 1024;
        while (capacity < needed) {
            if (capacity > (CONTACT_MAX_OUTPUT_BYTES + 1) / 2)
                capacity = CONTACT_MAX_OUTPUT_BYTES + 1;
            else
                capacity *= 2;
        }
        char *data = realloc(buffer->data, capacity);
        if (!data)
            return false;
        buffer->data = data;
        buffer->capacity = capacity;
    }
    memcpy(buffer->data + buffer->length, bytes, count);
    buffer->length += count;
    buffer->data[buffer->length] = '\0';
    return true;
}

static bool append_text(JsonBuffer *buffer, const char *text) {
    return append_bytes(buffer, text, strlen(text));
}

static bool append_json_string(JsonBuffer *buffer, const char *text) {
    if (!append_text(buffer, "\""))
        return false;
    static const char hex[] = "0123456789abcdef";
    for (const unsigned char *byte = (const unsigned char *)text; *byte; byte++) {
        if (*byte == '"' || *byte == '\\') {
            char escape[2] = {'\\', (char)*byte};
            if (!append_bytes(buffer, escape, sizeof(escape)))
                return false;
        } else if (*byte < 0x20u) {
            char escape[6] = {'\\', 'u', '0', '0', hex[*byte >> 4], hex[*byte & 15u]};
            if (!append_bytes(buffer, escape, sizeof(escape)))
                return false;
        } else if (!append_bytes(buffer, (const char *)byte, 1)) {
            return false;
        }
    }
    return append_text(buffer, "\"");
}

static bool serialize_contact(JsonBuffer *buffer, WmBoardContact contact) {
    return append_text(buffer, "{\"kind\":") &&
           append_json_string(buffer, contact.wii ? "wii" : "email") &&
           append_text(buffer, ",\"address\":") &&
           append_json_string(buffer, contact.address) &&
           append_text(buffer, ",\"nickname\":") &&
           append_json_string(buffer, contact.nickname) && append_text(buffer, "}");
}

bool contact_serialize(WmBoardContact contact, char **output) {
    if (!output)
        return false;
    *output = NULL;
    JsonBuffer buffer = {0};
    if (!serialize_contact(&buffer, contact)) {
        free(buffer.data);
        return false;
    }
    *output = buffer.data;
    return true;
}

ContactRewriteStatus contact_rewrite_nickname(const char *original,
                                              const char *nickname, char **output) {
    if (output)
        *output = NULL;
    if (!original || !nickname || !output)
        return CONTACT_REWRITE_ERROR;
    WmJson source;
    if (!wm_json_parse(&source, original, strlen(original))) {
        return CONTACT_REWRITE_INVALID_SOURCE;
    }
    size_t field = wm_json_member(&source, 0, "nickname");
    JsonBuffer changed = {0};
    bool valid =
        field != WM_JSON_INVALID && source.tokens[field].type == WM_JSON_STRING;
    if (valid) {
        size_t first_quote = source.tokens[field].start - 1;
        size_t after_quote = source.tokens[field].end + 1;
        valid = append_bytes(&changed, original, first_quote) &&
                append_json_string(&changed, nickname) &&
                append_text(&changed, original + after_quote);
    }
    wm_json_free(&source);
    if (!valid) {
        free(changed.data);
        return CONTACT_REWRITE_ERROR;
    }
    *output = changed.data;
    return CONTACT_REWRITE_OK;
}

bool contact_build_array(const StoredContact slots[WM_BOARD_CONTACT_CAPACITY],
                         size_t current_length, size_t slot, const char *replacement,
                         char **output, size_t *output_length) {
    if (output)
        *output = NULL;
    if (output_length)
        *output_length = 0;
    if (!slots || !replacement || !output || !output_length ||
        current_length > WM_BOARD_CONTACT_CAPACITY || slot > current_length ||
        slot >= WM_BOARD_CONTACT_CAPACITY) {
        return false;
    }
    JsonBuffer buffer = {0};
    bool serialized = append_text(&buffer, "[\n");
    size_t length = slot == current_length ? current_length + 1 : current_length;
    for (size_t index = 0; serialized && index < length; index++) {
        if (index)
            serialized = append_text(&buffer, ",\n");
        serialized = serialized && append_text(&buffer, "  ");
        const char *entry = index == slot           ? replacement
                            : slots[index].occupied ? slots[index].source_json
                                                    : "null";
        serialized = serialized && entry && append_text(&buffer, entry);
    }
    serialized = serialized && append_text(&buffer, "\n]\n");
    if (!serialized) {
        free(buffer.data);
        return false;
    }
    *output = buffer.data;
    *output_length = buffer.length;
    return true;
}
