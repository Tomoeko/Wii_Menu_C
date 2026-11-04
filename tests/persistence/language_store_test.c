#include "console_common/support/host.h"
#include "wii_menu/persistence/language_store.h"

#include <assert.h>
#include <errno.h>
#include <stdio.h>
#include <string.h>

static void write_text(const char *path, const char *text) {
    FILE *file = cc_host_fopen(path, "wb");
    assert(file);
    assert(fwrite(text, 1, strlen(text), file) == strlen(text));
    assert(fclose(file) == 0);
}

static void missing(const char *path) {
    errno = 0;
    FILE *file = cc_host_fopen(path, "rb");
    assert(!file && errno == ENOENT);
}

int main(int argc, char **argv) {
    assert(argc == 2);
    const char *path = argv[1];
    remove(path);
    char error[160];
    WmLanguage language;
    WmLanguageStore *store =
        wm_language_store_open(path, &language, error, sizeof(error));
    assert(store && !error[0] && language == WM_LANGUAGE_ENGLISH);
    missing(path);
    assert(wm_language_store_save(store, language, error, sizeof(error)));
    missing(path);
    assert(wm_language_store_save(store, WM_LANGUAGE_FRENCH, error, sizeof(error)));
    wm_language_store_destroy(store);
    store = wm_language_store_open(path, &language, error, sizeof(error));
    assert(store && !error[0] && language == WM_LANGUAGE_FRENCH);
    assert(wm_language_store_save(store, WM_LANGUAGE_SPANISH, error, sizeof(error)));
    wm_language_store_destroy(store);
    store = wm_language_store_open(path, &language, error, sizeof(error));
    assert(store && !error[0] && language == WM_LANGUAGE_SPANISH);

    /* An external edit makes repeated writes observable without timestamps. */
    write_text(path, "external edit\n");
    assert(wm_language_store_save(store, WM_LANGUAGE_SPANISH, error, sizeof(error)));
    assert(!wm_language_store_save(store, WM_LANGUAGE_ENGLISH, error, sizeof(error)));
    assert(strstr(error, "externally"));
    wm_language_store_destroy(store);
    store = wm_language_store_open(path, &language, error, sizeof(error));
    assert(store && error[0] && language == WM_LANGUAGE_ENGLISH);
    assert(wm_language_store_save(store, language, error, sizeof(error)));
    assert(wm_language_store_save(store, WM_LANGUAGE_FRENCH, error, sizeof(error)));
    assert(!wm_language_store_save(store, WM_LANGUAGE_COUNT, error, sizeof(error)));
    wm_language_store_destroy(store);

    static const char *const invalid[] = {
        "{}",
        "[]",
        "{\"schemaVersion\":2,\"language\":\"ENG\"}",
        "{\"schemaVersion\":1,\"language\":\"JPN\"}",
        "{\"schemaVersion\":1,\"language\":\"ENG\\u0000FRA\"}",
        "{\"schemaVersion\":1,\"language\":\"FRA\",\"language\":\"SPA\"}"};
    for (size_t index = 0; index < sizeof(invalid) / sizeof(*invalid); index++) {
        write_text(path, invalid[index]);
        store = wm_language_store_open(path, &language, error, sizeof(error));
        assert(store && error[0] && language == WM_LANGUAGE_ENGLISH);
        wm_language_store_destroy(store);
    }
    write_text(path, "{\"schemaVersion\":1,\"language\":\"ENG\"}");
    store = wm_language_store_open(path, &language, error, sizeof(error));
    assert(store && !error[0] && language == WM_LANGUAGE_ENGLISH);
    wm_language_store_destroy(store);
    remove(path);
    puts("Language preference defaults, edits, round trips and write suppression "
         "passed.");
    return 0;
}
