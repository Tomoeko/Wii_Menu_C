#define _XOPEN_SOURCE 700

#include "wii_menu/persistence/board_contact_store.h"

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#define CHECK(condition)                                                       \
    do {                                                                       \
        if (!(condition)) {                                                    \
            fprintf(stderr, "%s:%d: %s\n", __FILE__, __LINE__, #condition); \
            abort();                                                           \
        }                                                                      \
    } while (0)

static void path_in(char output[512], const char *directory,
                    const char *name) {
    int length = snprintf(output, 512, "%s/%s", directory, name);
    CHECK(length > 0 && length < 512);
}

static void write_text(const char *path, const char *text) {
    FILE *file = fopen(path, "wb");
    CHECK(file);
    size_t length = strlen(text);
    CHECK(fwrite(text, 1, length, file) == length);
    CHECK(fclose(file) == 0);
}

static char *read_text(const char *path) {
    FILE *file = fopen(path, "rb");
    CHECK(file);
    CHECK(fseek(file, 0, SEEK_END) == 0);
    long length = ftell(file);
    CHECK(length >= 0 && fseek(file, 0, SEEK_SET) == 0);
    char *text = malloc((size_t)length + 1);
    CHECK(text);
    CHECK(fread(text, 1, (size_t)length, file) == (size_t)length);
    text[length] = '\0';
    CHECK(fclose(file) == 0);
    return text;
}

int main(void) {
    char directory[] = "/tmp/wm-contact-XXXXXX";
    int directory_fd = mkstemp(directory);
    CHECK(directory_fd >= 0);
    CHECK(close(directory_fd) == 0);
    CHECK(unlink(directory) == 0);
    CHECK(mkdir(directory, 0700) == 0);
    char fresh[512], legacy[512], malformed[512], full[512], conflict[512];
    char linked[512], near_limit[512];
    path_in(fresh, directory, "fresh.json");
    path_in(legacy, directory, "legacy.json");
    path_in(malformed, directory, "malformed.json");
    path_in(full, directory, "full.json");
    path_in(conflict, directory, "conflict.json");
    path_in(linked, directory, "linked.json");
    path_in(near_limit, directory, "near-limit.json");
    char error[160];
    WmBoardContactStoreStatus status;
    FILE *output;
    WmBoardContactStore *store = wm_board_contact_store_open(
        fresh, &status, error, sizeof(error));
    CHECK(store && status == WM_BOARD_CONTACT_STORE_MISSING);
    CHECK(wm_board_contact_store_length(store) == 0);
    WmBoardContact email = {
        .wii = false, .confirmed = true,
        .address = "a+tag@b.c", .nickname = "Local"
    };
    size_t slot = SIZE_MAX;
    CHECK(wm_board_contact_store_register(store, email, &slot,
                                            error, sizeof(error)));
    CHECK(slot == 0 && wm_board_contact_store_occupied(store) == 1);
    WmBoardContact read;
    CHECK(wm_board_contact_store_get(store, slot, &read));
    CHECK(!read.wii && read.confirmed &&
           strcmp(read.address, email.address) == 0 &&
           strcmp(read.nickname, email.nickname) == 0);
    CHECK(!wm_board_contact_store_register(store, email, &slot,
                                             error, sizeof(error)));
    CHECK(wm_board_contact_store_occupied(store) == 1);
    wm_board_contact_store_destroy(store);

    /* A successful write must remain within the loader's own size limit. */
    output = fopen(near_limit, "wb");
    CHECK(output);
    const char *large_prefix =
        "[{\"kind\":\"email\",\"address\":\"large@b\","
        "\"nickname\":\"Large\",\"extra\":\"";
    CHECK(fputs(large_prefix, output) >= 0);
    size_t filler = 512u * 1024u - strlen(large_prefix) - 3u - 20u;
    for (size_t index = 0; index < filler; index++)
        CHECK(fputc('a', output) == 'a');
    CHECK(fputs("\"}]", output) >= 0);
    CHECK(fclose(output) == 0);
    store = wm_board_contact_store_open(near_limit, &status,
                                        error, sizeof(error));
    CHECK(store && status == WM_BOARD_CONTACT_STORE_OK);
    CHECK(!wm_board_contact_store_register(store, email, &slot,
                                             error, sizeof(error)));
    CHECK(wm_board_contact_store_occupied(store) == 1);
    wm_board_contact_store_destroy(store);
    CHECK(symlink(fresh, linked) == 0);
    CHECK(!wm_board_contact_store_open(linked, &status, error,
                                         sizeof(error)));
    CHECK(status == WM_BOARD_CONTACT_STORE_ERROR);
    CHECK(unlink(linked) == 0);
    store = wm_board_contact_store_open(fresh, &status, error,
                                        sizeof(error));
    CHECK(store && status == WM_BOARD_CONTACT_STORE_OK);
    CHECK(wm_board_contact_store_get(store, 0, &read));
    CHECK(strcmp(read.nickname, "Local") == 0);
    wm_board_contact_store_destroy(store);

    write_text(legacy,
        "[null,{\"kind\":\"email\",\"address\":\"older@b\","
        "\"nickname\":\"Older\",\"confirmed\":false,"
        "\"extra\":{\"kept\":true}},"
        "{\"kind\":\"email\",\"address\":\"later@b\","
        "\"nickname\":\"Later\"}]");
    store = wm_board_contact_store_open(legacy, &status, error,
                                        sizeof(error));
    CHECK(store && status == WM_BOARD_CONTACT_STORE_OK);
    CHECK(wm_board_contact_store_length(store) == 3);
    CHECK(wm_board_contact_store_occupied(store) == 2);
    CHECK(!wm_board_contact_store_get(store, 0, &read));
    CHECK(wm_board_contact_store_get(store, 1, &read));
    CHECK(!read.confirmed);
    CHECK(!wm_board_contact_store_rename(store, 1, "   ",
                                           error, sizeof(error)));
    CHECK(wm_board_contact_store_rename(store, 1, "New \"pal\"",
                                          error, sizeof(error)));
    CHECK(wm_board_contact_store_get(store, 1, &read));
    CHECK(strcmp(read.nickname, "New \"pal\"") == 0);
    CHECK(!read.confirmed);
    char *written = read_text(legacy);
    CHECK(strstr(written, "\"nickname\":\"New \\\"pal\\\"\""));
    CHECK(strstr(written, "\"extra\":{\"kept\":true}"));
    free(written);
    CHECK(wm_board_contact_store_erase(store, 1, error, sizeof(error)));
    CHECK(wm_board_contact_store_length(store) == 3);
    CHECK(wm_board_contact_store_occupied(store) == 1);
    CHECK(!wm_board_contact_store_get(store, 1, &read));
    CHECK(wm_board_contact_store_get(store, 2, &read));
    CHECK(strcmp(read.nickname, "Later") == 0);
    CHECK(wm_board_contact_store_register(store, email, &slot,
                                            error, sizeof(error)));
    CHECK(slot == 0 && wm_board_contact_store_length(store) == 3);
    written = read_text(legacy);
    CHECK(strstr(written, "null,\n  {\"kind\":\"email\",\"address\":\"later@b\""));
    free(written);
    wm_board_contact_store_destroy(store);

    write_text(malformed, "{\"contacts\":[]}");
    store = wm_board_contact_store_open(malformed, &status, error,
                                        sizeof(error));
    CHECK(!store && status == WM_BOARD_CONTACT_STORE_ERROR);
    written = read_text(malformed);
    CHECK(strcmp(written, "{\"contacts\":[]}") == 0);
    free(written);
    write_text(malformed,
        "[{\"kind\":\"email\",\"address\":\"a@b\","
        "\"nickname\":\"\\u0000bad\"}]");
    CHECK(!wm_board_contact_store_open(malformed, &status, error,
                                         sizeof(error)));

    write_text(malformed,
        "[{\"kind\":\"email\",\"address\":\"a@b\\u0000hidden\","
        "\"nickname\":\"Local\"}]");
    CHECK(!wm_board_contact_store_open(malformed, &status, error,
                                         sizeof(error)));
    write_text(malformed,
        "[{\"kind\":\"email\\u0000hidden\",\"address\":\"a@b\","
        "\"nickname\":\"Local\"}]");
    CHECK(!wm_board_contact_store_open(malformed, &status, error,
                                         sizeof(error)));
    write_text(malformed,
        "[{\"kind\":\"email\",\"address\":\"a@b\","
        "\"nickname\":\"\\\\u0000\"}]");
    store = wm_board_contact_store_open(malformed, &status, error,
                                        sizeof(error));
    CHECK(store && status == WM_BOARD_CONTACT_STORE_OK);
    CHECK(wm_board_contact_store_get(store, 0, &read));
    CHECK(strcmp(read.nickname, "\\u0000") == 0);
    wm_board_contact_store_destroy(store);

    output = fopen(full, "wb");
    CHECK(output);
    CHECK(fputc('[', output) == '[');
    for (int index = 0; index < 101; index++) {
        if (index) CHECK(fputc(',', output) == ',');
        CHECK(fputs("null", output) >= 0);
    }
    CHECK(fputc(']', output) == ']');
    CHECK(fclose(output) == 0);
    CHECK(!wm_board_contact_store_open(full, &status, error,
                                         sizeof(error)));

    write_text(conflict, "[]");
    store = wm_board_contact_store_open(conflict, &status, error,
                                        sizeof(error));
    CHECK(store && status == WM_BOARD_CONTACT_STORE_OK);
    CHECK(unlink(conflict) == 0);
    CHECK(symlink(fresh, conflict) == 0);
    CHECK(!wm_board_contact_store_register(store, email, &slot,
                                             error, sizeof(error)));
    CHECK(unlink(conflict) == 0);
    write_text(conflict, "[null]");
    CHECK(!wm_board_contact_store_register(store, email, &slot,
                                             error, sizeof(error)));
    CHECK(wm_board_contact_store_length(store) == 0);
    written = read_text(conflict);
    CHECK(strcmp(written, "[null]") == 0);
    free(written);
    wm_board_contact_store_destroy(store);

    const char *stable_contact =
        "[{\"kind\":\"email\",\"address\":\"stable@b\","
        "\"nickname\":\"Before\",\"extra\":{\"kept\":true}}]";
    write_text(conflict, stable_contact);
    store = wm_board_contact_store_open(conflict, &status,
                                        error, sizeof(error));
    CHECK(store && status == WM_BOARD_CONTACT_STORE_OK);
    write_text(conflict,
        "[{\"kind\":\"email\",\"address\":\"stable@b\","
        "\"nickname\":\"Outside\"}]");
    CHECK(!wm_board_contact_store_rename(store, 0, "Edited",
                                           error, sizeof(error)));
    CHECK(!wm_board_contact_store_erase(store, 0,
                                          error, sizeof(error)));
    CHECK(wm_board_contact_store_get(store, 0, &read));
    CHECK(strcmp(read.nickname, "Before") == 0);
    written = read_text(conflict);
    CHECK(strstr(written, "\"nickname\":\"Outside\""));
    free(written);
    /* Failed writes retain the original baseline and slot ownership. Once
     * the external edit is undone, a retry can still preserve extra fields. */
    write_text(conflict, stable_contact);
    CHECK(wm_board_contact_store_rename(store, 0, "Retry",
                                          error, sizeof(error)));
    CHECK(wm_board_contact_store_get(store, 0, &read));
    CHECK(strcmp(read.nickname, "Retry") == 0);
    written = read_text(conflict);
    CHECK(strstr(written, "\"extra\":{\"kept\":true}"));
    free(written);
    wm_board_contact_store_destroy(store);

    CHECK(unlink(conflict) == 0);
    store = wm_board_contact_store_open(conflict, &status, error,
                                        sizeof(error));
    CHECK(store && status == WM_BOARD_CONTACT_STORE_MISSING);
    write_text(conflict, "[]");
    CHECK(!wm_board_contact_store_register(store, email, &slot,
                                             error, sizeof(error)));
    CHECK(wm_board_contact_store_length(store) == 0);
    wm_board_contact_store_destroy(store);

    CHECK(unlink(fresh) == 0);
    CHECK(unlink(legacy) == 0);
    CHECK(unlink(malformed) == 0);
    CHECK(unlink(full) == 0);
    CHECK(unlink(conflict) == 0);
    CHECK(unlink(near_limit) == 0);
    CHECK(rmdir(directory) == 0);
    puts("Address Book contact store tests passed.");
    return 0;
}
