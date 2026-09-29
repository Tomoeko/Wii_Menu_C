#define _POSIX_C_SOURCE 200809L

#include "board_contact_format.h"
#include "wii_menu/support/error.h"

#include "wii_menu/support/json.h"

#include "../support/atomic_file.h"
#include "wii_menu/support/regular_file.h"

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

struct WmBoardContactStore {
    char *path;
    char *baseline;
    size_t baseline_length;
    bool had_file;
    size_t length;
    size_t occupied;
    StoredContact slots[WM_BOARD_CONTACT_CAPACITY];
};

void wm_board_contact_store_destroy(WmBoardContactStore *store) {
    if (!store)
        return;
    for (size_t slot = 0; slot < store->length; slot++)
        free(store->slots[slot].source_json);
    free(store->baseline);
    free(store->path);
    free(store);
}

WmBoardContactStore *wm_board_contact_store_open(const char *path,
                                                 WmBoardContactStoreStatus *status,
                                                 char *error, size_t error_capacity) {
    if (status)
        *status = WM_BOARD_CONTACT_STORE_ERROR;
    if (!path || !path[0] || strlen(path) >= 4096) {
        wm_error_set(error, error_capacity, "Invalid Address Book path");
        return NULL;
    }
    WmBoardContactStore *store = calloc(1, sizeof(*store));
    if (!store) {
        wm_error_set(error, error_capacity, "Could not allocate Address Book");
        return NULL;
    }
    store->path = strdup(path);
    if (!store->path) {
        wm_error_set(error, error_capacity, "Could not allocate Address Book path");
        wm_board_contact_store_destroy(store);
        return NULL;
    }
    char *contents;
    size_t length;
    WmRegularFileStatus read_status =
        wm_regular_file_read(path, CONTACT_MAX_JSON_BYTES, &contents, &length);
    if (read_status == WM_REGULAR_FILE_MISSING) {
        if (status)
            *status = WM_BOARD_CONTACT_STORE_MISSING;
        wm_error_set(error, error_capacity, "");
        return store;
    }
    if (read_status != WM_REGULAR_FILE_OK) {
        wm_error_set(error, error_capacity, "Invalid Address Book file");
        wm_board_contact_store_destroy(store);
        return NULL;
    }
    WmJson json;
    bool parsed = wm_json_parse(&json, contents, length);
    free(contents);
    if (!parsed) {
        wm_error_set(error, error_capacity, "Malformed Address Book JSON");
        wm_board_contact_store_destroy(store);
        return NULL;
    }
    bool valid = json.tokens[0].type == WM_JSON_ARRAY &&
                 json.tokens[0].children <= WM_BOARD_CONTACT_CAPACITY &&
                 contact_json_utf8_valid(json.source, json.length);
    if (valid) {
        store->length = json.tokens[0].children;
        for (size_t slot = 0; slot < store->length; slot++) {
            size_t token = wm_json_index(&json, 0, slot);
            if (!contact_parse(&json, token, &store->slots[slot])) {
                valid = false;
                break;
            }
            if (store->slots[slot].occupied)
                store->occupied++;
        }
    }
    if (!valid) {
        wm_error_set(error, error_capacity, "Invalid Address Book contacts");
        wm_json_free(&json);
        wm_board_contact_store_destroy(store);
        return NULL;
    }
    store->baseline = json.source;
    store->baseline_length = json.length;
    store->had_file = true;
    json.source = NULL;
    wm_json_free(&json);
    if (status)
        *status = WM_BOARD_CONTACT_STORE_OK;
    wm_error_set(error, error_capacity, "");
    return store;
}

size_t wm_board_contact_store_length(const WmBoardContactStore *store) {
    return store ? store->length : 0;
}

size_t wm_board_contact_store_occupied(const WmBoardContactStore *store) {
    return store ? store->occupied : 0;
}

bool wm_board_contact_store_get(const WmBoardContactStore *store, size_t slot,
                                WmBoardContact *contact) {
    if (!store || !contact || slot >= store->length || !store->slots[slot].occupied)
        return false;
    const StoredContact *stored = &store->slots[slot];
    *contact = (WmBoardContact){.wii = stored->wii,
                                .confirmed = stored->confirmed,
                                .address = stored->address,
                                .nickname = stored->nickname};
    return true;
}

static bool baseline_unchanged(const WmBoardContactStore *store) {
    char *contents;
    size_t length;
    WmRegularFileStatus status =
        wm_regular_file_read(store->path, CONTACT_MAX_JSON_BYTES, &contents, &length);
    if (status == WM_REGULAR_FILE_MISSING)
        return !store->had_file;
    if (status != WM_REGULAR_FILE_OK)
        return false;
    bool equal = store->had_file && length == store->baseline_length &&
                 memcmp(contents, store->baseline, length) == 0;
    free(contents);
    return equal;
}

static bool write_replaced_slot(WmBoardContactStore *store, size_t slot,
                                const char *replacement, char *error,
                                size_t error_capacity) {
    char *output = NULL;
    size_t output_length = 0;
    if (!contact_build_array(store->slots, store->length, slot, replacement, &output,
                             &output_length)) {
        wm_error_set(error, error_capacity, "Address Book is too large");
        return false;
    }
    if (!baseline_unchanged(store)) {
        wm_error_set(error, error_capacity,
                     "Address Book changed; reload before saving");
        free(output);
        return false;
    }
    if (!wm_atomic_file_replace(store->path, output, output_length)) {
        wm_error_set(error, error_capacity, "Could not save Address Book");
        free(output);
        return false;
    }
    free(store->baseline);
    store->baseline = output;
    store->baseline_length = output_length;
    store->had_file = true;
    wm_error_set(error, error_capacity, "");
    return true;
}

bool wm_board_contact_store_rename(WmBoardContactStore *store, size_t slot,
                                   const char *nickname, char *error,
                                   size_t error_capacity) {
    if (!store || slot >= store->length || !store->slots[slot].occupied ||
        !contact_nickname_valid(nickname)) {
        wm_error_set(error, error_capacity, "Invalid Address Book nickname");
        return false;
    }
    const char *original = store->slots[slot].source_json;
    char *changed = NULL;
    ContactRewriteStatus rewrite =
        contact_rewrite_nickname(original, nickname, &changed);
    if (rewrite == CONTACT_REWRITE_INVALID_SOURCE) {
        wm_error_set(error, error_capacity, "Invalid Address Book contact");
        return false;
    }
    if (rewrite != CONTACT_REWRITE_OK) {
        wm_error_set(error, error_capacity, "Could not update Address Book nickname");
        return false;
    }
    if (!write_replaced_slot(store, slot, changed, error, error_capacity)) {
        free(changed);
        return false;
    }
    StoredContact *contact = &store->slots[slot];
    free(contact->source_json);
    contact->source_json = changed;
    snprintf(contact->nickname, sizeof(contact->nickname), "%s", nickname);
    return true;
}

bool wm_board_contact_store_erase(WmBoardContactStore *store, size_t slot, char *error,
                                  size_t error_capacity) {
    if (!store || slot >= store->length || !store->slots[slot].occupied) {
        wm_error_set(error, error_capacity, "Invalid Address Book slot");
        return false;
    }
    if (!write_replaced_slot(store, slot, "null", error, error_capacity))
        return false;
    free(store->slots[slot].source_json);
    memset(&store->slots[slot], 0, sizeof(store->slots[slot]));
    store->occupied--;
    return true;
}

bool wm_board_contact_store_register(WmBoardContactStore *store, WmBoardContact contact,
                                     size_t *slot, char *error, size_t error_capacity) {
    if (!store || !contact.address || !contact.nickname ||
        !contact_stored_address_valid(contact.wii, contact.address) ||
        !contact_nickname_valid(contact.nickname)) {
        wm_error_set(error, error_capacity, "Invalid Address Book contact");
        return false;
    }
    if (store->occupied >= WM_BOARD_CONTACT_CAPACITY) {
        wm_error_set(error, error_capacity, "Address Book is full");
        return false;
    }
    size_t target = store->length;
    for (size_t index = 0; index < store->length; index++) {
        if (!store->slots[index].occupied && target == store->length)
            target = index;
        if (store->slots[index].occupied && store->slots[index].wii == contact.wii &&
            strcmp(store->slots[index].address, contact.address) == 0) {
            wm_error_set(error, error_capacity, "Address Book contact already exists");
            return false;
        }
    }
    if (target >= WM_BOARD_CONTACT_CAPACITY) {
        wm_error_set(error, error_capacity, "Address Book is full");
        return false;
    }
    char *canonical = NULL;
    if (!contact_serialize(contact, &canonical)) {
        wm_error_set(error, error_capacity, "Address Book is too large");
        return false;
    }
    if (!write_replaced_slot(store, target, canonical, error, error_capacity)) {
        free(canonical);
        return false;
    }
    StoredContact *saved = &store->slots[target];
    saved->occupied = true;
    saved->wii = contact.wii;
    saved->confirmed = true;
    snprintf(saved->address, sizeof(saved->address), "%s", contact.address);
    snprintf(saved->nickname, sizeof(saved->nickname), "%s", contact.nickname);
    saved->source_json = canonical;
    if (target == store->length)
        store->length++;
    store->occupied++;
    if (slot)
        *slot = target;
    wm_error_set(error, error_capacity, "");
    return true;
}
