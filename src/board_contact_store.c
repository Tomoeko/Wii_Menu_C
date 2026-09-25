#define _POSIX_C_SOURCE 200809L

#include "wii_menu/board_contact_store.h"

#include "wii_menu/json.h"

#include <errno.h>
#include <fcntl.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

enum {
    CONTACT_MAX_JSON_BYTES = 512 * 1024,
    CONTACT_MAX_OUTPUT_BYTES = CONTACT_MAX_JSON_BYTES,
    CONTACT_ADDRESS_BYTES = 400,
    CONTACT_NICKNAME_BYTES = 44
};

typedef struct StoredContact {
    bool occupied;
    bool wii;
    bool confirmed;
    char address[CONTACT_ADDRESS_BYTES];
    char nickname[CONTACT_NICKNAME_BYTES];
    char *source_json;
} StoredContact;

struct WmBoardContactStore {
    char *path;
    char *baseline;
    size_t baseline_length;
    bool had_file;
    size_t length;
    size_t occupied;
    StoredContact slots[WM_BOARD_CONTACT_CAPACITY];
};

typedef struct JsonBuffer {
    char *data;
    size_t length;
    size_t capacity;
} JsonBuffer;

typedef enum FileReadStatus {
    FILE_READ_OK,
    FILE_READ_MISSING,
    FILE_READ_ERROR
} FileReadStatus;

static void set_error(char *error, size_t capacity, const char *message) {
    if (error && capacity) snprintf(error, capacity, "%s", message);
}

static FileReadStatus read_regular_file(const char *path, size_t limit,
                                        char **contents, size_t *length) {
    *contents = NULL;
    *length = 0;
    struct stat named;
    if (lstat(path, &named) != 0)
        return errno == ENOENT ? FILE_READ_MISSING : FILE_READ_ERROR;
    if (!S_ISREG(named.st_mode)) return FILE_READ_ERROR;
    int flags = O_RDONLY;
#ifdef O_NOFOLLOW
    flags |= O_NOFOLLOW;
#endif
#ifdef O_CLOEXEC
    flags |= O_CLOEXEC;
#endif
    int descriptor = open(path, flags);
    if (descriptor < 0) return FILE_READ_ERROR;
    struct stat opened;
    bool valid = fstat(descriptor, &opened) == 0 &&
                 S_ISREG(opened.st_mode) &&
                 opened.st_dev == named.st_dev &&
                 opened.st_ino == named.st_ino &&
                 opened.st_size > 0 &&
                 (uint64_t)opened.st_size <= limit;
    if (!valid) {
        close(descriptor);
        return FILE_READ_ERROR;
    }
    size_t size = (size_t)opened.st_size;
    char *bytes = malloc(size + 1);
    if (!bytes) {
        close(descriptor);
        return FILE_READ_ERROR;
    }
    size_t offset = 0;
    while (offset < size) {
        ssize_t count = read(descriptor, bytes + offset, size - offset);
        if (count < 0 && errno == EINTR) continue;
        if (count <= 0) break;
        offset += (size_t)count;
    }
    char extra;
    ssize_t after;
    do {
        after = read(descriptor, &extra, 1);
    } while (after < 0 && errno == EINTR);
    valid = offset == size && after == 0;
    if (close(descriptor) != 0) valid = false;
    if (!valid) {
        free(bytes);
        return FILE_READ_ERROR;
    }
    bytes[size] = '\0';
    *contents = bytes;
    *length = size;
    return FILE_READ_OK;
}

static bool utf8_next(const unsigned char *text, size_t length,
                      size_t *offset, uint32_t *codepoint, size_t *units) {
    if (*offset >= length) return false;
    unsigned first = text[*offset];
    size_t count = first < 0x80 ? 1 :
                   first >= 0xC2 && first <= 0xDF ? 2 :
                   first >= 0xE0 && first <= 0xEF ? 3 :
                   first >= 0xF0 && first <= 0xF4 ? 4 : 0;
    if (count == 0 || count > length - *offset) return false;
    uint32_t value = first & (count == 1 ? 0x7Fu :
                              count == 2 ? 0x1Fu :
                              count == 3 ? 0x0Fu : 0x07u);
    for (size_t index = 1; index < count; index++) {
        unsigned next = text[*offset + index];
        if ((next & 0xC0u) != 0x80u) return false;
        value = (value << 6) | (next & 0x3Fu);
    }
    if ((count == 2 && value < 0x80u) ||
        (count == 3 && value < 0x800u) ||
        (count == 4 && value < 0x10000u) ||
        value > 0x10FFFFu || (value >= 0xD800u && value <= 0xDFFFu)) {
        return false;
    }
    *offset += count;
    if (codepoint) *codepoint = value;
    if (units) *units = count == 4 ? 2 : 1;
    return true;
}

static bool unicode_space(uint32_t codepoint) {
    return codepoint == 0x20u ||
           (codepoint >= 0x09u && codepoint <= 0x0Du) ||
           codepoint == 0xA0u || codepoint == 0x1680u ||
           (codepoint >= 0x2000u && codepoint <= 0x200Au) ||
           codepoint == 0x2028u || codepoint == 0x2029u ||
           codepoint == 0x202Fu || codepoint == 0x205Fu ||
           codepoint == 0x3000u || codepoint == 0xFEFFu;
}

static bool valid_nickname(const char *text) {
    if (!text) return false;
    size_t length = strnlen(text, CONTACT_NICKNAME_BYTES);
    if (length == CONTACT_NICKNAME_BYTES) return false;
    size_t offset = 0, units = 0;
    bool nonspace = false;
    while (offset < length) {
        uint32_t codepoint;
        size_t character_units;
        if (!utf8_next((const unsigned char *)text, length, &offset,
                       &codepoint, &character_units)) return false;
        if (codepoint == 0 || codepoint == '\r' || codepoint == '\n')
            return false;
        if (!unicode_space(codepoint)) nonspace = true;
        units += character_units;
        if (units > 10) return false;
    }
    return nonspace;
}

/* Persisted HTML contacts intentionally admit older e-mail records. New
 * registration is checked by AddressEdit's stricter source predicate. */
static bool valid_stored_address(bool wii, const char *text) {
    if (!text) return false;
    size_t length = strnlen(text, CONTACT_ADDRESS_BYTES);
    if (length == CONTACT_ADDRESS_BYTES) return false;
    if (wii) {
        if (length != 16) return false;
        for (size_t index = 0; index < length; index++) {
            if (text[index] < '0' || text[index] > '9') return false;
        }
        return true;
    }
    size_t offset = 0, units = 0, at = SIZE_MAX;
    while (offset < length) {
        uint32_t codepoint;
        size_t character_units;
        size_t start = offset;
        if (!utf8_next((const unsigned char *)text, length, &offset,
                       &codepoint, &character_units) ||
            codepoint == 0 || unicode_space(codepoint)) return false;
        if (codepoint == '@') {
            if (at != SIZE_MAX) return false;
            at = start;
        }
        units += character_units;
        if (units > 99) return false;
    }
    return at != SIZE_MAX && at > 0 && at + 1 < length;
}

static bool raw_nul_escape(const WmJson *json, size_t token) {
    const WmJsonToken *item = &json->tokens[token];
    for (size_t offset = item->start; offset < item->end;) {
        if (json->source[offset] != '\\') {
            offset++;
            continue;
        }
        size_t slashes = 0;
        while (offset + slashes < item->end &&
               json->source[offset + slashes] == '\\') slashes++;
        offset += slashes;
        if ((slashes & 1u) && offset + 5 <= item->end &&
            json->source[offset] == 'u' &&
            memcmp(json->source + offset + 1, "0000", 4) == 0) return true;
    }
    return false;
}

static bool read_string(const WmJson *json, size_t token,
                        char *output, size_t capacity) {
    return token < json->count &&
           json->tokens[token].type == WM_JSON_STRING &&
           !raw_nul_escape(json, token) &&
           wm_json_copy(json, token, output, capacity);
}

static bool parse_contact(const WmJson *json, size_t token,
                          StoredContact *contact) {
    if (token >= json->count) return false;
    const WmJsonToken *item = &json->tokens[token];
    if (item->type == WM_JSON_NULL) return true;
    if (item->type != WM_JSON_OBJECT) return false;
    char kind[8];
    if (!read_string(json, wm_json_member(json, token, "kind"),
                     kind, sizeof(kind)) ||
        (strcmp(kind, "wii") != 0 && strcmp(kind, "email") != 0) ||
        !read_string(json, wm_json_member(json, token, "address"),
                     contact->address, sizeof(contact->address)) ||
        !read_string(json, wm_json_member(json, token, "nickname"),
                     contact->nickname, sizeof(contact->nickname))) return false;
    contact->wii = strcmp(kind, "wii") == 0;
    if (!valid_stored_address(contact->wii, contact->address) ||
        !valid_nickname(contact->nickname)) return false;
    size_t confirmed = wm_json_member(json, token, "confirmed");
    contact->confirmed = true;
    if (confirmed != WM_JSON_INVALID) {
        if (confirmed >= json->count ||
            json->tokens[confirmed].type != WM_JSON_BOOLEAN) return false;
        contact->confirmed = json->source[json->tokens[confirmed].start] == 't';
    }
    size_t raw_length = item->end - item->start;
    contact->source_json = malloc(raw_length + 1);
    if (!contact->source_json) return false;
    memcpy(contact->source_json, json->source + item->start, raw_length);
    contact->source_json[raw_length] = '\0';
    contact->occupied = true;
    return true;
}

void wm_board_contact_store_destroy(WmBoardContactStore *store) {
    if (!store) return;
    for (size_t slot = 0; slot < store->length; slot++)
        free(store->slots[slot].source_json);
    free(store->baseline);
    free(store->path);
    free(store);
}

WmBoardContactStore *wm_board_contact_store_open(
    const char *path, WmBoardContactStoreStatus *status,
    char *error, size_t error_capacity) {
    if (status) *status = WM_BOARD_CONTACT_STORE_ERROR;
    if (!path || !path[0] || strlen(path) >= 4096) {
        set_error(error, error_capacity, "Invalid Address Book path");
        return NULL;
    }
    WmBoardContactStore *store = calloc(1, sizeof(*store));
    if (!store) {
        set_error(error, error_capacity, "Could not allocate Address Book");
        return NULL;
    }
    store->path = strdup(path);
    if (!store->path) {
        set_error(error, error_capacity, "Could not allocate Address Book path");
        wm_board_contact_store_destroy(store);
        return NULL;
    }
    char *contents;
    size_t length;
    FileReadStatus read_status = read_regular_file(
        path, CONTACT_MAX_JSON_BYTES, &contents, &length);
    if (read_status == FILE_READ_MISSING) {
        if (status) *status = WM_BOARD_CONTACT_STORE_MISSING;
        set_error(error, error_capacity, "");
        return store;
    }
    if (read_status != FILE_READ_OK) {
        set_error(error, error_capacity, "Invalid Address Book file");
        wm_board_contact_store_destroy(store);
        return NULL;
    }
    WmJson json;
    bool parsed = wm_json_parse(&json, contents, length);
    free(contents);
    if (!parsed) {
        set_error(error, error_capacity, "Malformed Address Book JSON");
        wm_board_contact_store_destroy(store);
        return NULL;
    }
    bool valid = json.tokens[0].type == WM_JSON_ARRAY &&
                 json.tokens[0].children <= WM_BOARD_CONTACT_CAPACITY;
    size_t offset = 0;
    while (valid && offset < json.length) {
        if (!utf8_next((const unsigned char *)json.source, json.length,
                       &offset, NULL, NULL)) valid = false;
    }
    if (valid) {
        store->length = json.tokens[0].children;
        for (size_t slot = 0; slot < store->length; slot++) {
            size_t token = wm_json_index(&json, 0, slot);
            if (!parse_contact(&json, token, &store->slots[slot])) {
                valid = false;
                break;
            }
            if (store->slots[slot].occupied) store->occupied++;
        }
    }
    if (!valid) {
        set_error(error, error_capacity, "Invalid Address Book contacts");
        wm_json_free(&json);
        wm_board_contact_store_destroy(store);
        return NULL;
    }
    store->baseline = json.source;
    store->baseline_length = json.length;
    store->had_file = true;
    json.source = NULL;
    wm_json_free(&json);
    if (status) *status = WM_BOARD_CONTACT_STORE_OK;
    set_error(error, error_capacity, "");
    return store;
}

size_t wm_board_contact_store_length(const WmBoardContactStore *store) {
    return store ? store->length : 0;
}

size_t wm_board_contact_store_occupied(const WmBoardContactStore *store) {
    return store ? store->occupied : 0;
}

bool wm_board_contact_store_get(const WmBoardContactStore *store,
                                size_t slot, WmBoardContact *contact) {
    if (!store || !contact || slot >= store->length ||
        !store->slots[slot].occupied) return false;
    const StoredContact *stored = &store->slots[slot];
    *contact = (WmBoardContact){
        .wii = stored->wii,
        .confirmed = stored->confirmed,
        .address = stored->address,
        .nickname = stored->nickname
    };
    return true;
}

static bool append_bytes(JsonBuffer *buffer, const char *bytes, size_t count) {
    if (count > CONTACT_MAX_OUTPUT_BYTES - buffer->length) return false;
    size_t needed = buffer->length + count + 1;
    if (needed > buffer->capacity) {
        size_t capacity = buffer->capacity ? buffer->capacity : 1024;
        while (capacity < needed) {
            if (capacity > (CONTACT_MAX_OUTPUT_BYTES + 1) / 2)
                capacity = CONTACT_MAX_OUTPUT_BYTES + 1;
            else capacity *= 2;
        }
        char *data = realloc(buffer->data, capacity);
        if (!data) return false;
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
    if (!append_text(buffer, "\"")) return false;
    static const char hex[] = "0123456789abcdef";
    for (const unsigned char *byte = (const unsigned char *)text;
         *byte; byte++) {
        if (*byte == '"' || *byte == '\\') {
            char escape[2] = {'\\', (char)*byte};
            if (!append_bytes(buffer, escape, sizeof(escape))) return false;
        } else if (*byte < 0x20u) {
            char escape[6] = {'\\', 'u', '0', '0',
                              hex[*byte >> 4], hex[*byte & 15u]};
            if (!append_bytes(buffer, escape, sizeof(escape))) return false;
        } else if (!append_bytes(buffer, (const char *)byte, 1)) {
            return false;
        }
    }
    return append_text(buffer, "\"");
}

static bool serialize_contact(JsonBuffer *buffer,
                              WmBoardContact contact) {
    return append_text(buffer, "{\"kind\":") &&
           append_json_string(buffer, contact.wii ? "wii" : "email") &&
           append_text(buffer, ",\"address\":") &&
           append_json_string(buffer, contact.address) &&
           append_text(buffer, ",\"nickname\":") &&
           append_json_string(buffer, contact.nickname) &&
           append_text(buffer, "}");
}

static bool baseline_unchanged(const WmBoardContactStore *store) {
    char *contents;
    size_t length;
    FileReadStatus status = read_regular_file(
        store->path, CONTACT_MAX_JSON_BYTES, &contents, &length);
    if (status == FILE_READ_MISSING) return !store->had_file;
    if (status != FILE_READ_OK || !store->had_file) return false;
    bool equal = length == store->baseline_length &&
                 memcmp(contents, store->baseline, length) == 0;
    free(contents);
    return equal;
}

static bool replace_file(const char *path, const char *data, size_t length) {
    size_t path_length = strlen(path);
    static const char suffix[] = ".tmp.XXXXXX";
    if (path_length > SIZE_MAX - sizeof(suffix)) return false;
    char *temporary = malloc(path_length + sizeof(suffix));
    if (!temporary) return false;
    memcpy(temporary, path, path_length);
    memcpy(temporary + path_length, suffix, sizeof(suffix));
    int descriptor = mkstemp(temporary);
    if (descriptor < 0) {
        free(temporary);
        return false;
    }
    bool complete = true;
    size_t offset = 0;
    while (offset < length) {
        ssize_t count = write(descriptor, data + offset, length - offset);
        if (count < 0 && errno == EINTR) continue;
        if (count <= 0) {
            complete = false;
            break;
        }
        offset += (size_t)count;
    }
    if (complete && fsync(descriptor) != 0) complete = false;
    if (close(descriptor) != 0) complete = false;
    if (complete && rename(temporary, path) != 0) complete = false;
    if (!complete) unlink(temporary);
    free(temporary);
    return complete;
}

static bool write_replaced_slot(WmBoardContactStore *store, size_t slot,
                                const char *replacement,
                                char *error, size_t error_capacity) {
    JsonBuffer output = {0};
    bool serialized = append_text(&output, "[\n");
    for (size_t index = 0; serialized && index < store->length; index++) {
        if (index) serialized = append_text(&output, ",\n");
        serialized = serialized && append_text(&output, "  ");
        const char *entry = index == slot ? replacement :
                            store->slots[index].occupied
                                ? store->slots[index].source_json : "null";
        serialized = serialized && append_text(&output, entry);
    }
    serialized = serialized && append_text(&output, "\n]\n");
    if (!serialized) {
        set_error(error, error_capacity, "Address Book is too large");
        free(output.data);
        return false;
    }
    if (!baseline_unchanged(store)) {
        set_error(error, error_capacity,
                  "Address Book changed; reload before saving");
        free(output.data);
        return false;
    }
    if (!replace_file(store->path, output.data, output.length)) {
        set_error(error, error_capacity, "Could not save Address Book");
        free(output.data);
        return false;
    }
    free(store->baseline);
    store->baseline = output.data;
    store->baseline_length = output.length;
    store->had_file = true;
    set_error(error, error_capacity, "");
    return true;
}

bool wm_board_contact_store_rename(WmBoardContactStore *store, size_t slot,
                                   const char *nickname, char *error,
                                   size_t error_capacity) {
    if (!store || slot >= store->length ||
        !store->slots[slot].occupied || !valid_nickname(nickname)) {
        set_error(error, error_capacity, "Invalid Address Book nickname");
        return false;
    }
    WmJson source;
    const char *original = store->slots[slot].source_json;
    if (!wm_json_parse(&source, original, strlen(original))) {
        set_error(error, error_capacity, "Invalid Address Book contact");
        return false;
    }
    size_t field = wm_json_member(&source, 0, "nickname");
    JsonBuffer changed = {0};
    bool valid = field != WM_JSON_INVALID &&
                 source.tokens[field].type == WM_JSON_STRING;
    if (valid) {
        size_t first_quote = source.tokens[field].start - 1;
        size_t after_quote = source.tokens[field].end + 1;
        valid = append_bytes(&changed, original, first_quote) &&
                append_json_string(&changed, nickname) &&
                append_text(&changed, original + after_quote);
    }
    wm_json_free(&source);
    if (!valid || !write_replaced_slot(store, slot, changed.data,
                                       error, error_capacity)) {
        if (!valid)
            set_error(error, error_capacity,
                      "Could not update Address Book nickname");
        free(changed.data);
        return false;
    }
    StoredContact *contact = &store->slots[slot];
    free(contact->source_json);
    contact->source_json = changed.data;
    snprintf(contact->nickname, sizeof(contact->nickname), "%s", nickname);
    return true;
}

bool wm_board_contact_store_erase(WmBoardContactStore *store, size_t slot,
                                  char *error, size_t error_capacity) {
    if (!store || slot >= store->length || !store->slots[slot].occupied) {
        set_error(error, error_capacity, "Invalid Address Book slot");
        return false;
    }
    if (!write_replaced_slot(store, slot, "null", error, error_capacity))
        return false;
    free(store->slots[slot].source_json);
    memset(&store->slots[slot], 0, sizeof(store->slots[slot]));
    store->occupied--;
    return true;
}

bool wm_board_contact_store_register(WmBoardContactStore *store,
                                     WmBoardContact contact, size_t *slot,
                                     char *error, size_t error_capacity) {
    if (!store || !contact.address || !contact.nickname ||
        !valid_stored_address(contact.wii, contact.address) ||
        !valid_nickname(contact.nickname)) {
        set_error(error, error_capacity, "Invalid Address Book contact");
        return false;
    }
    if (store->occupied >= WM_BOARD_CONTACT_CAPACITY) {
        set_error(error, error_capacity, "Address Book is full");
        return false;
    }
    size_t target = store->length;
    for (size_t index = 0; index < store->length; index++) {
        if (!store->slots[index].occupied && target == store->length)
            target = index;
        if (store->slots[index].occupied &&
            store->slots[index].wii == contact.wii &&
            strcmp(store->slots[index].address, contact.address) == 0) {
            set_error(error, error_capacity, "Address Book contact already exists");
            return false;
        }
    }
    if (target >= WM_BOARD_CONTACT_CAPACITY) {
        set_error(error, error_capacity, "Address Book is full");
        return false;
    }
    JsonBuffer canonical = {0};
    JsonBuffer output = {0};
    bool serialized = serialize_contact(&canonical, contact) &&
                      append_text(&output, "[\n");
    size_t length = target == store->length ? store->length + 1 :
                                              store->length;
    for (size_t index = 0; serialized && index < length; index++) {
        if (index) serialized = append_text(&output, ",\n");
        if (!serialized) break;
        serialized = append_text(&output, "  ");
        if (index == target)
            serialized = serialized &&
                         append_bytes(&output, canonical.data,
                                      canonical.length);
        else if (store->slots[index].occupied)
            serialized = serialized &&
                         append_text(&output,
                                     store->slots[index].source_json);
        else
            serialized = serialized && append_text(&output, "null");
    }
    serialized = serialized && append_text(&output, "\n]\n");
    if (!serialized) {
        set_error(error, error_capacity, "Address Book is too large");
        free(canonical.data);
        free(output.data);
        return false;
    }
    if (!baseline_unchanged(store)) {
        set_error(error, error_capacity,
                  "Address Book changed; reload before saving");
        free(canonical.data);
        free(output.data);
        return false;
    }
    if (!replace_file(store->path, output.data, output.length)) {
        set_error(error, error_capacity, "Could not save Address Book");
        free(canonical.data);
        free(output.data);
        return false;
    }
    StoredContact *saved = &store->slots[target];
    saved->occupied = true;
    saved->wii = contact.wii;
    saved->confirmed = true;
    snprintf(saved->address, sizeof(saved->address), "%s", contact.address);
    snprintf(saved->nickname, sizeof(saved->nickname), "%s",
             contact.nickname);
    saved->source_json = canonical.data;
    store->length = length;
    store->occupied++;
    free(store->baseline);
    store->baseline = output.data;
    store->baseline_length = output.length;
    store->had_file = true;
    if (slot) *slot = target;
    set_error(error, error_capacity, "");
    return true;
}
