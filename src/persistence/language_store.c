#define _POSIX_C_SOURCE 200809L
#include "wii_menu/persistence/language_store.h"

#include "../support/atomic_file.h"
#include "wii_menu/support/error.h"
#include "wii_menu/support/json.h"
#include "wii_menu/support/regular_file.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

enum { LANGUAGE_CONFIG_MAX_BYTES = 2048 };

struct WmLanguageStore {
    char *path;
    uint8_t *contents;
    size_t length;
    WmLanguage baseline;
};

void wm_language_store_destroy(WmLanguageStore *store) {
    if (!store)
        return;
    free(store->path);
    free(store->contents);
    free(store);
}

static bool parse_language(const uint8_t *contents, size_t length,
                           WmLanguage *language) {
    WmJson json;
    if (!wm_json_parse(&json, (const char *)contents, length))
        return false;
    int version;
    bool valid =
        json.tokens[0].type == WM_JSON_OBJECT && json.tokens[0].children == 2 &&
        wm_json_integer(&json, wm_json_member(&json, 0, "schemaVersion"), &version) &&
        version == 1;
    bool found = false;
    for (int choice = 0; valid && choice < WM_LANGUAGE_COUNT; choice++) {
        if (wm_json_equals(&json, wm_json_member(&json, 0, "language"),
                           wm_language_code((WmLanguage)choice))) {
            *language = (WmLanguage)choice;
            found = true;
        }
    }
    wm_json_free(&json);
    return valid && found;
}

WmLanguageStore *wm_language_store_open(const char *path, WmLanguage *language,
                                        char *error, size_t error_capacity) {
    wm_error_set(error, error_capacity, "Could not read local language config");
    if (language)
        *language = WM_LANGUAGE_ENGLISH;
    if (!path || !path[0] || strlen(path) >= 4096 || !language)
        return NULL;
    WmLanguageStore *store = calloc(1, sizeof(*store));
    if (!store)
        return NULL;
    store->path = strdup(path);
    if (!store->path)
        goto release_store;
    WmRegularFileStatus status = wm_regular_file_read_bytes(
        path, 0, LANGUAGE_CONFIG_MAX_BYTES, &store->contents, &store->length);
    if (status == WM_REGULAR_FILE_ERROR)
        goto release_store;
    wm_error_set(error, error_capacity, "");
    if (status == WM_REGULAR_FILE_OK &&
        !parse_language(store->contents, store->length, language))
        wm_error_set(error, error_capacity,
                     "Invalid local language config; using English");
    store->baseline = *language;
    return store;
release_store:
    wm_language_store_destroy(store);
    return NULL;
}

static bool unchanged(const WmLanguageStore *store) {
    uint8_t *contents = NULL;
    size_t length = 0;
    WmRegularFileStatus status = wm_regular_file_read_bytes(
        store->path, 0, LANGUAGE_CONFIG_MAX_BYTES, &contents, &length);
    if (status == WM_REGULAR_FILE_MISSING)
        return !store->contents;
    if (status != WM_REGULAR_FILE_OK)
        return false;
    bool equal = store->contents && store->length == length &&
                 memcmp(store->contents, contents, length) == 0;
    free(contents);
    return equal;
}

bool wm_language_store_save(WmLanguageStore *store, WmLanguage language, char *error,
                            size_t error_capacity) {
    wm_error_set(error, error_capacity, "Could not save local language config");
    if (!store || !wm_language_valid(language))
        return false;
    if (language == store->baseline) {
        wm_error_set(error, error_capacity, "");
        return true;
    }
    if (!unchanged(store)) {
        wm_error_set(error, error_capacity,
                     "Language config changed externally; reload before saving");
        return false;
    }
    char output[128];
    int length = snprintf(output, sizeof(output),
                          "{\n    \"schemaVersion\": 1,\n    \"language\": \"%s\"\n}\n",
                          wm_language_code(language));
    if (length < 0 || (size_t)length >= sizeof(output))
        return false;
    uint8_t *snapshot = malloc((size_t)length);
    if (!snapshot)
        return false;
    memcpy(snapshot, output, (size_t)length);
    if (!wm_atomic_file_replace(store->path, output, (size_t)length)) {
        free(snapshot);
        return false;
    }
    free(store->contents);
    store->contents = snapshot;
    store->length = (size_t)length;
    store->baseline = language;
    wm_error_set(error, error_capacity, "");
    return true;
}
