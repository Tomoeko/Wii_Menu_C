#define _XOPEN_SOURCE 700
#define _POSIX_C_SOURCE 200809L
#define _DARWIN_C_SOURCE 1

#ifdef NDEBUG
#undef NDEBUG
#endif

#include "wii_menu/support/regular_file.h"

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

static void write_file(const char *path, const char *text) {
    FILE *file = fopen(path, "wb");
    assert(file);
    assert(fwrite(text, 1, strlen(text), file) == strlen(text));
    assert(fclose(file) == 0);
}

int main(void) {
    char directory[] = "/tmp/wm-regular-file-test-XXXXXX";
    assert(mkdtemp(directory));

    char path[256];
    char link_path[256];
    assert(snprintf(path, sizeof(path), "%s/input", directory) > 0);
    assert(snprintf(link_path, sizeof(link_path), "%s/link", directory) > 0);

    write_file(path, "");
    uint8_t *bytes = NULL;
    size_t length = 99;
    assert(wm_regular_file_read_bytes(path, 0, 4, &bytes, &length) ==
           WM_REGULAR_FILE_OK);
    assert(bytes && length == 0 && bytes[0] == 0);
    free(bytes);

    uint8_t marker = 0;
    bytes = &marker;
    length = 99;
    assert(wm_regular_file_read_bytes(path, 1, 4, &bytes, &length) ==
           WM_REGULAR_FILE_ERROR);
    assert(!bytes && length == 0);

    assert(wm_regular_file_read_bytes(link_path, 0, 4, &bytes, &length) ==
           WM_REGULAR_FILE_MISSING);
    assert(!bytes && length == 0);

    write_file(path, "Wii");
    assert(wm_regular_file_read_bytes(path, 3, 3, &bytes, &length) ==
           WM_REGULAR_FILE_OK);
    assert(length == 3 && memcmp(bytes, "Wii", 3) == 0 && bytes[3] == 0);
    free(bytes);
    assert(wm_regular_file_read_bytes(path, 0, 2, &bytes, &length) ==
           WM_REGULAR_FILE_ERROR);
    assert(!bytes && length == 0);
    assert(wm_regular_file_read_bytes(path, 4, 3, &bytes, &length) ==
           WM_REGULAR_FILE_ERROR);
    assert(!bytes && length == 0);

    assert(symlink(path, link_path) == 0);
    assert(wm_regular_file_read_bytes(link_path, 0, 4, &bytes, &length) ==
           WM_REGULAR_FILE_ERROR);
    assert(!bytes && length == 0);

    char *text = NULL;
    assert(wm_regular_file_read(path, 3, &text, &length) == WM_REGULAR_FILE_OK);
    assert(strcmp(text, "Wii") == 0 && length == 3);
    free(text);

    assert(unlink(link_path) == 0);
    assert(unlink(path) == 0);
    assert(rmdir(directory) == 0);
    return 0;
}
