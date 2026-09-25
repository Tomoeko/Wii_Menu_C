#define _XOPEN_SOURCE 700

#include "wii_menu/board_contact_store.h"

#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

static void path_in(char output[512], const char *directory,
                    const char *name) {
    int length = snprintf(output, 512, "%s/%s", directory, name);
    assert(length > 0 && length < 512);
}

static void write_text(const char *path, const char *text) {
    FILE *file = fopen(path, "wb");
    assert(file);
    size_t length = strlen(text);
    assert(fwrite(text, 1, length, file) == length);
    assert(fclose(file) == 0);
}

static char *read_text(const char *path) {
    FILE *file = fopen(path, "rb");
    assert(file);
    assert(fseek(file, 0, SEEK_END) == 0);
    long length = ftell(file);
    assert(length >= 0 && fseek(file, 0, SEEK_SET) == 0);
    char *text = malloc((size_t)length + 1);
    assert(text);
    assert(fread(text, 1, (size_t)length, file) == (size_t)length);
    text[length] = '\0';
    assert(fclose(file) == 0);
    return text;
}

int main(void) {
    char directory[] = "/tmp/wm-contact-XXXXXX";
    int directory_fd = mkstemp(directory);
    assert(directory_fd >= 0);
    assert(close(directory_fd) == 0);
    assert(unlink(directory) == 0);
    assert(mkdir(directory, 0700) == 0);
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
    assert(store && status == WM_BOARD_CONTACT_STORE_MISSING);
    assert(wm_board_contact_store_length(store) == 0);
    WmBoardContact email = {
        .wii = false, .confirmed = true,
        .address = "a+tag@b.c", .nickname = "Local"
    };
    size_t slot = SIZE_MAX;
    assert(wm_board_contact_store_register(store, email, &slot,
                                            error, sizeof(error)));
    assert(slot == 0 && wm_board_contact_store_occupied(store) == 1);
    WmBoardContact read;
    assert(wm_board_contact_store_get(store, slot, &read));
    assert(!read.wii && read.confirmed &&
           strcmp(read.address, email.address) == 0 &&
           strcmp(read.nickname, email.nickname) == 0);
    assert(!wm_board_contact_store_register(store, email, &slot,
                                             error, sizeof(error)));
    assert(wm_board_contact_store_occupied(store) == 1);
    wm_board_contact_store_destroy(store);

    /* A successful write must remain within the loader's own size limit. */
    output = fopen(near_limit, "wb");
    assert(output);
    const char *large_prefix =
        "[{\"kind\":\"email\",\"address\":\"large@b\","
        "\"nickname\":\"Large\",\"extra\":\"";
    assert(fputs(large_prefix, output) >= 0);
    size_t filler = 512u * 1024u - strlen(large_prefix) - 3u - 20u;
    for (size_t index = 0; index < filler; index++)
        assert(fputc('a', output) == 'a');
    assert(fputs("\"}]", output) >= 0);
    assert(fclose(output) == 0);
    store = wm_board_contact_store_open(near_limit, &status,
                                        error, sizeof(error));
    assert(store && status == WM_BOARD_CONTACT_STORE_OK);
    assert(!wm_board_contact_store_register(store, email, &slot,
                                             error, sizeof(error)));
    assert(wm_board_contact_store_occupied(store) == 1);
    wm_board_contact_store_destroy(store);
    assert(symlink(fresh, linked) == 0);
    assert(!wm_board_contact_store_open(linked, &status, error,
                                         sizeof(error)));
    assert(status == WM_BOARD_CONTACT_STORE_ERROR);
    assert(unlink(linked) == 0);
    store = wm_board_contact_store_open(fresh, &status, error,
                                        sizeof(error));
    assert(store && status == WM_BOARD_CONTACT_STORE_OK);
    assert(wm_board_contact_store_get(store, 0, &read));
    assert(strcmp(read.nickname, "Local") == 0);
    wm_board_contact_store_destroy(store);

    write_text(legacy,
        "[null,{\"kind\":\"email\",\"address\":\"older@b\","
        "\"nickname\":\"Older\",\"confirmed\":false,"
        "\"extra\":{\"kept\":true}},"
        "{\"kind\":\"email\",\"address\":\"later@b\","
        "\"nickname\":\"Later\"}]");
    store = wm_board_contact_store_open(legacy, &status, error,
                                        sizeof(error));
    assert(store && status == WM_BOARD_CONTACT_STORE_OK);
    assert(wm_board_contact_store_length(store) == 3);
    assert(wm_board_contact_store_occupied(store) == 2);
    assert(!wm_board_contact_store_get(store, 0, &read));
    assert(wm_board_contact_store_get(store, 1, &read));
    assert(!read.confirmed);
    assert(!wm_board_contact_store_rename(store, 1, "   ",
                                           error, sizeof(error)));
    assert(wm_board_contact_store_rename(store, 1, "New \"pal\"",
                                          error, sizeof(error)));
    assert(wm_board_contact_store_get(store, 1, &read));
    assert(strcmp(read.nickname, "New \"pal\"") == 0);
    assert(!read.confirmed);
    char *written = read_text(legacy);
    assert(strstr(written, "\"nickname\":\"New \\\"pal\\\"\""));
    assert(strstr(written, "\"extra\":{\"kept\":true}"));
    free(written);
    assert(wm_board_contact_store_erase(store, 1, error, sizeof(error)));
    assert(wm_board_contact_store_length(store) == 3);
    assert(wm_board_contact_store_occupied(store) == 1);
    assert(!wm_board_contact_store_get(store, 1, &read));
    assert(wm_board_contact_store_get(store, 2, &read));
    assert(strcmp(read.nickname, "Later") == 0);
    assert(wm_board_contact_store_register(store, email, &slot,
                                            error, sizeof(error)));
    assert(slot == 0 && wm_board_contact_store_length(store) == 3);
    written = read_text(legacy);
    assert(strstr(written, "null,\n  {\"kind\":\"email\",\"address\":\"later@b\""));
    free(written);
    wm_board_contact_store_destroy(store);

    write_text(malformed, "{\"contacts\":[]}");
    store = wm_board_contact_store_open(malformed, &status, error,
                                        sizeof(error));
    assert(!store && status == WM_BOARD_CONTACT_STORE_ERROR);
    written = read_text(malformed);
    assert(strcmp(written, "{\"contacts\":[]}") == 0);
    free(written);
    write_text(malformed,
        "[{\"kind\":\"email\",\"address\":\"a@b\","
        "\"nickname\":\"\\u0000bad\"}]");
    assert(!wm_board_contact_store_open(malformed, &status, error,
                                         sizeof(error)));

    output = fopen(full, "wb");
    assert(output);
    assert(fputc('[', output) == '[');
    for (int index = 0; index < 101; index++) {
        if (index) assert(fputc(',', output) == ',');
        assert(fputs("null", output) >= 0);
    }
    assert(fputc(']', output) == ']');
    assert(fclose(output) == 0);
    assert(!wm_board_contact_store_open(full, &status, error,
                                         sizeof(error)));

    write_text(conflict, "[]");
    store = wm_board_contact_store_open(conflict, &status, error,
                                        sizeof(error));
    assert(store && status == WM_BOARD_CONTACT_STORE_OK);
    assert(unlink(conflict) == 0);
    assert(symlink(fresh, conflict) == 0);
    assert(!wm_board_contact_store_register(store, email, &slot,
                                             error, sizeof(error)));
    assert(unlink(conflict) == 0);
    write_text(conflict, "[null]");
    assert(!wm_board_contact_store_register(store, email, &slot,
                                             error, sizeof(error)));
    assert(wm_board_contact_store_length(store) == 0);
    written = read_text(conflict);
    assert(strcmp(written, "[null]") == 0);
    free(written);
    wm_board_contact_store_destroy(store);

    write_text(conflict,
        "[{\"kind\":\"email\",\"address\":\"stable@b\","
        "\"nickname\":\"Before\"}]");
    store = wm_board_contact_store_open(conflict, &status,
                                        error, sizeof(error));
    assert(store && status == WM_BOARD_CONTACT_STORE_OK);
    write_text(conflict,
        "[{\"kind\":\"email\",\"address\":\"stable@b\","
        "\"nickname\":\"Outside\"}]");
    assert(!wm_board_contact_store_rename(store, 0, "Edited",
                                           error, sizeof(error)));
    assert(!wm_board_contact_store_erase(store, 0,
                                          error, sizeof(error)));
    assert(wm_board_contact_store_get(store, 0, &read));
    assert(strcmp(read.nickname, "Before") == 0);
    written = read_text(conflict);
    assert(strstr(written, "\"nickname\":\"Outside\""));
    free(written);
    wm_board_contact_store_destroy(store);

    assert(unlink(fresh) == 0);
    assert(unlink(legacy) == 0);
    assert(unlink(malformed) == 0);
    assert(unlink(full) == 0);
    assert(unlink(conflict) == 0);
    assert(unlink(near_limit) == 0);
    assert(rmdir(directory) == 0);
    puts("Address Book contact store tests passed.");
    return 0;
}
