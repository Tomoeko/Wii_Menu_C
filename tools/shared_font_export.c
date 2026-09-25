#define _POSIX_C_SOURCE 200809L

#include "wii_menu/shared_font_export.h"

#include <dirent.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

enum {
    WM_SHARED_FONT_CLI_MAX_INPUT = 128 * 1024 * 1024,
    WM_SHARED_FONT_CLI_PATH = 4096
};

static void usage(FILE *stream) {
    fputs("Usage: wm-shared-font-export <extracted-nand-root|decrypted-font.app> "
          "<ignored-native-assets-directory>\n"
          "\n"
          "Reads an optional decrypted NAND shared-font U8 archive containing\n"
          "wbf1.brfna and wbf2.brfna. Writes their native layout aliases under\n"
          "the output directory's fonts/ subdirectory. The System Menu WAD\n"
          "alone does not contain these shared glyphs. Keep input and output\n"
          "resources in an ignored local directory.\n", stream);
}

static uint8_t *read_input(const char *path, size_t *size) {
    FILE *file = fopen(path, "rb");
    if (!file) return NULL;
    if (fseek(file, 0, SEEK_END) != 0) { fclose(file); return NULL; }
    long length = ftell(file);
    if (length < 32 || length > WM_SHARED_FONT_CLI_MAX_INPUT ||
        fseek(file, 0, SEEK_SET) != 0) {
        fclose(file);
        return NULL;
    }
    uint8_t *data = malloc((size_t)length);
    if (!data) { fclose(file); return NULL; }
    bool complete = fread(data, 1, (size_t)length, file) == (size_t)length;
    fclose(file);
    if (!complete) { free(data); return NULL; }
    *size = (size_t)length;
    return data;
}

static bool export_from_directory(const char *root, const char *output,
                                  char *error, size_t error_capacity) {
    char directory_path[WM_SHARED_FONT_CLI_PATH];
    int length = snprintf(directory_path, sizeof(directory_path),
                          "%s/shared1", root);
    if (length < 0 || (size_t)length >= sizeof(directory_path)) return false;
    DIR *directory = opendir(directory_path);
    if (!directory) return false;
    bool exported = false;
    size_t examined = 0;
    struct dirent *entry;
    while ((entry = readdir(directory)) != NULL && examined < 4096) {
        size_t name_length = strlen(entry->d_name);
        if (name_length < 5 ||
            strcmp(entry->d_name + name_length - 4, ".app") != 0) continue;
        examined++;
        char path[WM_SHARED_FONT_CLI_PATH];
        length = snprintf(path, sizeof(path), "%s/%s",
                          directory_path, entry->d_name);
        if (length < 0 || (size_t)length >= sizeof(path)) continue;
        struct stat status;
        if (lstat(path, &status) != 0 || !S_ISREG(status.st_mode)) continue;
        size_t size = 0;
        uint8_t *data = read_input(path, &size);
        if (!data) continue;
        exported = wm_shared_font_export(data, size, output,
                                          error, error_capacity);
        free(data);
        if (exported) break;
    }
    closedir(directory);
    return exported;
}

int main(int argc, char **argv) {
    if (argc == 2 && strcmp(argv[1], "--help") == 0) {
        usage(stdout);
        return 0;
    }
    if (argc != 3) {
        usage(stderr);
        return 2;
    }
    char error[160];
    bool success = false;
    struct stat status;
    if (lstat(argv[1], &status) == 0 && S_ISDIR(status.st_mode)) {
        success = export_from_directory(argv[1], argv[2], error,
                                        sizeof(error));
    } else {
        size_t size = 0;
        uint8_t *data = read_input(argv[1], &size);
        if (data) {
            success = wm_shared_font_export(data, size, argv[2],
                                             error, sizeof(error));
            free(data);
        }
    }
    if (!success) {
        fputs("Could not export a valid shared font archive.\n", stderr);
        return 1;
    }
    puts("Exported two shared RFNA fonts under six native layout names.");
    return 0;
}
