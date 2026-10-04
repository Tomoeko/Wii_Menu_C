#define _POSIX_C_SOURCE 200809L

#include "wii_menu/fonts/shared_font_export.h"
#include "wii_menu/support/error.h"

#include "wii_menu/resources/resource_font.h"
#include "wii_menu/resources/resource_u8.h"

#include "../support/atomic_file.h"

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "console_common/support/directory.h"

enum {
    WM_SHARED_FONT_ARCHIVE_LIMIT = 128 * 1024 * 1024,
    WM_SHARED_FONT_PATH_CAPACITY = 4096
};

typedef struct FontAlias {
    unsigned source;
    const char *name;
} FontAlias;

static const FontAlias WM_FONT_ALIASES[] = {
    {0, "wbf1.brfna"},
    {0, "RevoIpl_RodinNTLGPro_DB_32_I4.brfnt"},
    {0, "WiiBitmapFontType1.brfnt"},
    {1, "wbf2.brfna"},
    {1, "RevoIpl_UtrilloProGrecoStd_M_32_I4.brfnt"},
    {1, "WiiBitmapFontType2.brfnt"}};

static bool ensure_directory(const char *path) {
    size_t length = strnlen(path, WM_SHARED_FONT_PATH_CAPACITY);
    if (length == 0 || length == WM_SHARED_FONT_PATH_CAPACITY)
        return false;
    char normalized[WM_SHARED_FONT_PATH_CAPACITY];
    memcpy(normalized, path, length + 1);
    while (length > 1 && normalized[length - 1] == '/')
        normalized[--length] = '\0';
    const char *leaf = strrchr(normalized, '/');
    leaf = leaf ? leaf + 1 : normalized;
    if (strcmp(leaf, "..") == 0 ||
        (strcmp(leaf, ".") == 0 && strcmp(normalized, ".") != 0))
        return false;

    return cc_directory_create(normalized);
}

static bool output_path(char path[WM_SHARED_FONT_PATH_CAPACITY], const char *directory,
                        const char *name) {
    int length = snprintf(path, WM_SHARED_FONT_PATH_CAPACITY, "%s/%s", directory, name);
    return length > 0 && length < WM_SHARED_FONT_PATH_CAPACITY;
}

static bool write_atomic(const char *directory, const char *name, const uint8_t *data,
                         size_t size) {
    char target[WM_SHARED_FONT_PATH_CAPACITY];
    return output_path(target, directory, name) &&
           wm_atomic_file_replace(target, data, size);
}

bool wm_shared_font_export(const uint8_t *archive_bytes, size_t archive_size,
                           const char *assets_directory, char *error,
                           size_t error_capacity) {
    wm_error_set(error, error_capacity, "");
    if (!archive_bytes || archive_size < 32 ||
        archive_size > WM_SHARED_FONT_ARCHIVE_LIMIT || !assets_directory ||
        !assets_directory[0]) {
        wm_error_set(error, error_capacity, "Invalid archive or output directory.");
        return false;
    }
    WmU8Archive archive = {0};
    if (!wm_u8_parse(archive_bytes, archive_size, &archive, error, error_capacity))
        return false;
    const WmU8Entry *sources[2] = {wm_u8_find(&archive, "wbf1.brfna"),
                                   wm_u8_find(&archive, "wbf2.brfna")};
    bool valid = sources[0] && sources[1];
    if (!valid) {
        wm_error_set(error, error_capacity,
                     "Shared archive must contain wbf1.brfna and wbf2.brfna.");
    }
    for (size_t index = 0; index < 2 && valid; index++) {
        WmFont *font = wm_font_decode(sources[index]->data, sources[index]->size, error,
                                      error_capacity);
        if (!font)
            valid = false;
        wm_font_destroy(font);
    }
    char fonts_directory[WM_SHARED_FONT_PATH_CAPACITY];
    if (valid &&
        (!output_path(fonts_directory, assets_directory, "fonts") ||
         !ensure_directory(assets_directory) || !ensure_directory(fonts_directory))) {
        wm_error_set(error, error_capacity,
                     "Could not create the font output directory.");
        valid = false;
    }
    for (size_t index = 0;
         index < sizeof(WM_FONT_ALIASES) / sizeof(WM_FONT_ALIASES[0]) && valid;
         index++) {
        const FontAlias *alias = &WM_FONT_ALIASES[index];
        const WmU8Entry *source = sources[alias->source];
        if (!write_atomic(fonts_directory, alias->name, source->data, source->size)) {
            wm_error_set(error, error_capacity, "Could not write a shared font alias.");
            valid = false;
        }
    }
    wm_u8_free(&archive);
    return valid;
}
