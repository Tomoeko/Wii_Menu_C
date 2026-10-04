#define _POSIX_C_SOURCE 200809L

#include "console_common/support/tool_io.h"
#include "export_directory.h"

#include "wii_menu/fonts/outline_font.h"
#include "wii_menu/resources/resource_u8.h"
#include "wii_menu/support/regular_file.h"

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

enum {
    OUTLINE_EXPORT_PATH_CAPACITY = 4096,
    OUTLINE_EXPORT_MAX_SOURCE = 8 * 1024 * 1024
};

static bool valid_latin_face(const WmU8Entry *entry) {
    if (!entry || entry->size < 20 || memcmp(entry->data, "ttcf", 4) != 0 ||
        entry->size > OUTLINE_EXPORT_MAX_SOURCE)
        return false;
    WmOutlineFont *font = wm_outline_font_decode(entry->data, entry->size, 1);
    if (!font)
        return false;
    const uint32_t sample[] = {'A', 'B', '0', 'W', '?'};
    unsigned rendered = 0;
    bool valid = true;
    for (size_t index = 0; index < sizeof(sample) / sizeof(sample[0]); index++) {
        WmOutlineBitmap bitmap = {0};
        if (!wm_outline_font_raster(font, sample[index], 24, &bitmap)) {
            valid = false;
            break;
        }
        for (size_t pixel = 0; pixel < (size_t)bitmap.width * bitmap.height; pixel++) {
            if (bitmap.alpha[pixel]) {
                rendered++;
                break;
            }
        }
        wm_outline_bitmap_free(&bitmap);
    }
    wm_outline_font_destroy(font, NULL);
    return valid && rendered >= 4;
}

static bool write_new_file(const char *path, const uint8_t *bytes, size_t size) {
    /* O_CREAT | O_EXCL refuses both existing files and symlinks, including
     * dangling symlinks. Add O_NOFOLLOW where the SDK exposes it. */
    int flags = O_WRONLY | O_CREAT | O_EXCL;
#ifdef O_NOFOLLOW
    flags |= O_NOFOLLOW;
#endif
    int output = open(path, flags, 0600);
    if (output < 0)
        return false;
    bool valid = true;
    size_t cursor = 0;
    while (cursor < size) {
        ssize_t written = write(output, bytes + cursor, size - cursor);
        if (written < 0 && errno == EINTR)
            continue;
        if (written <= 0) {
            valid = false;
            break;
        }
        cursor += (size_t)written;
    }
    if (close(output) != 0)
        valid = false;
    if (!valid)
        remove(path);
    return valid;
}

int main(int argc, char **argv) {
    if (argc != 3) {
        fprintf(stderr, "Usage: %s RESOURCE_10_APP OUTPUT_ASSETS\n", argv[0]);
        return 2;
    }
    size_t source_size = 0;
    uint8_t *source = NULL;
    wm_regular_file_read_bytes(argv[1], 1, OUTLINE_EXPORT_MAX_SOURCE, &source,
                               &source_size);
    WmU8Archive archive = {0};
    char error[160] = {0};
    if (!source || !wm_u8_parse(source, source_size, &archive, error, sizeof(error))) {
        fprintf(stderr, "Invalid outline font archive: %s\n", error);
        free(source);
        return 1;
    }
    const WmU8Entry *entry = wm_u8_find(&archive, "WiiNTLG-Regular.ttc");
    if (!valid_latin_face(entry)) {
        fputs("Missing or invalid proportional Settings font.\n", stderr);
        wm_u8_free(&archive);
        free(source);
        return 1;
    }

    char directory[OUTLINE_EXPORT_PATH_CAPACITY];
    char path[OUTLINE_EXPORT_PATH_CAPACITY];
    int directory_length = snprintf(directory, sizeof(directory), "%s/fonts", argv[2]);
    int path_length =
        snprintf(path, sizeof(path), "%s/fonts/settings-latin.ttc", argv[2]);
    bool valid = directory_length > 0 && (size_t)directory_length < sizeof(directory) &&
                 path_length > 0 && (size_t)path_length < sizeof(path) &&
                 wm_export_directory_root(argv[2], 0700) &&
                 wm_export_directory_child(argv[2], "fonts", 0700);
    if (valid)
        valid = write_new_file(path, entry->data, entry->size);
    wm_u8_free(&archive);
    free(source);
    if (!valid) {
        fputs("Could not write local Settings font.\n", stderr);
        return 1;
    }
    puts("Exported local Settings outline font.");
    return 0;
}
