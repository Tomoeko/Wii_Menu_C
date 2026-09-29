#define _POSIX_C_SOURCE 200809L

#include "wii_menu/support/asset_manifest.h"
#include "wii_menu/support/sha1.h"

#include <dirent.h>
#include <errno.h>
#include <inttypes.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

enum { ASSET_PATH_LIMIT = 4096, MANIFEST_LINE_LIMIT = 8192 };

typedef struct AssetPaths {
    char **items;
    size_t count;
    size_t capacity;
} AssetPaths;

typedef enum AssetFileStatus {
    ASSET_FILE_FOUND,
    ASSET_FILE_MISSING,
    ASSET_FILE_WRONG_TYPE,
    ASSET_FILE_UNREADABLE
} AssetFileStatus;

static bool join_path(char output[ASSET_PATH_LIMIT], const char *root,
                      const char *relative) {
    int length = snprintf(output, ASSET_PATH_LIMIT, "%s/%s", root, relative);
    return length > 0 && length < ASSET_PATH_LIMIT;
}

static bool valid_relative_path(const char *path) {
    if (!path || !*path || *path == '/' ||
        strlen(path) >= ASSET_PATH_LIMIT || path[strlen(path) - 1] == '/') {
        return false;
    }
    for (const char *part = path; *part;) {
        const char *end = strchr(part, '/');
        size_t length = end ? (size_t)(end - part) : strlen(part);
        if (!length || part[0] == '.' || length > 255) return false;
        for (size_t index = 0; index < length; index++) {
            unsigned char ch = (unsigned char)part[index];
            if (ch < 32 || ch == 127 || ch == '\\') return false;
        }
        if (!end) break;
        part = end + 1;
    }
    return true;
}

static void clear_paths(AssetPaths *paths) {
    for (size_t index = 0; index < paths->count; index++) free(paths->items[index]);
    free(paths->items);
}

static bool append_path(AssetPaths *paths, const char *relative) {
    if (paths->count == paths->capacity) {
        size_t next = paths->capacity ? paths->capacity * 2 : 128;
        if (next <= paths->capacity || next > SIZE_MAX / sizeof(char *)) return false;
        char **items = realloc(paths->items, next * sizeof(char *));
        if (!items) return false;
        paths->items = items;
        paths->capacity = next;
    }
    paths->items[paths->count] = strdup(relative);
    if (!paths->items[paths->count]) return false;
    paths->count++;
    return true;
}

static bool collect_paths(const char *root, const char *directory,
                          AssetPaths *paths, FILE *diagnostics) {
    char location[ASSET_PATH_LIMIT];
    if (!join_path(location, root, directory)) return false;
    DIR *entries = opendir(location);
    if (!entries) {
        fprintf(diagnostics, "Cannot read prepared directory: %s\n", directory);
        return false;
    }
    bool okay = true;
    struct dirent *entry;
    while ((entry = readdir(entries)) != NULL) {
        if (entry->d_name[0] == '.') continue;
        char relative[ASSET_PATH_LIMIT];
        if (directory[0]) {
            int length = snprintf(relative, sizeof(relative), "%s/%s",
                                  directory, entry->d_name);
            if (length < 0 || length >= (int)sizeof(relative)) { okay = false; break; }
        } else {
            int length = snprintf(relative, sizeof(relative), "%s", entry->d_name);
            if (length < 0 || length >= (int)sizeof(relative)) { okay = false; break; }
        }
        if (!valid_relative_path(relative) ||
            strcmp(relative, WM_ASSET_MANIFEST_NAME) == 0 ||
            !join_path(location, root, relative)) {
            okay = false;
            break;
        }
        struct stat metadata;
        if (lstat(location, &metadata) != 0) { okay = false; break; }
        if (S_ISDIR(metadata.st_mode)) {
            if (!collect_paths(root, relative, paths, diagnostics)) { okay = false; break; }
        } else if (S_ISREG(metadata.st_mode)) {
            if (!append_path(paths, relative)) { okay = false; break; }
        } else {
            fprintf(diagnostics, "Unsupported prepared asset type: %s\n", relative);
            okay = false;
            break;
        }
    }
    closedir(entries);
    return okay;
}

static int compare_paths(const void *left, const void *right) {
    const char *const *a = left;
    const char *const *b = right;
    return strcmp(*a, *b);
}

static bool hash_file(const char *path, char hexadecimal[41], uint64_t *size) {
    FILE *file = fopen(path, "rb");
    if (!file) return false;
    WmSha1 sha1;
    wm_sha1_init(&sha1);
    uint8_t bytes[32768];
    uint64_t total = 0;
    size_t count;
    while ((count = fread(bytes, 1, sizeof(bytes), file)) != 0) {
        if (total > UINT64_MAX - count) { fclose(file); return false; }
        total += count;
        wm_sha1_update(&sha1, bytes, count);
    }
    bool okay = !ferror(file);
    if (fclose(file) != 0) okay = false;
    if (!okay) return false;
    uint8_t digest[20];
    wm_sha1_final(&sha1, digest);
    static const char digits[] = "0123456789abcdef";
    for (size_t index = 0; index < 20; index++) {
        hexadecimal[2 * index] = digits[digest[index] >> 4];
        hexadecimal[2 * index + 1] = digits[digest[index] & 15];
    }
    hexadecimal[40] = '\0';
    *size = total;
    return true;
}

bool wm_asset_manifest_write(const char *assets_root, FILE *diagnostics) {
    if (!assets_root || !diagnostics) return false;
    AssetPaths paths = {0};
    bool okay = collect_paths(assets_root, "", &paths, diagnostics);
    if (okay && paths.count == 0) okay = false;
    if (okay) qsort(paths.items, paths.count, sizeof(char *), compare_paths);
    char manifest_path[ASSET_PATH_LIMIT];
    if (okay) okay = join_path(manifest_path, assets_root, WM_ASSET_MANIFEST_NAME);
    FILE *manifest = okay ? fopen(manifest_path, "wb") : NULL;
    if (!manifest) okay = false;
    if (okay) okay = fputs("WM-ASSETS-1\n", manifest) >= 0;
    for (size_t index = 0; okay && index < paths.count; index++) {
        char path[ASSET_PATH_LIMIT], digest[41];
        uint64_t size;
        okay = join_path(path, assets_root, paths.items[index]) &&
               hash_file(path, digest, &size);
        if (!okay) {
            fprintf(diagnostics, "Cannot hash prepared asset: %s\n",
                    paths.items[index]);
            break;
        }
        okay = fprintf(manifest, "%s %" PRIu64 " %s\n", digest, size,
                       paths.items[index]) > 0;
    }
    if (manifest && fclose(manifest) != 0) okay = false;
    if (!okay) {
        if (manifest) remove(manifest_path);
        fprintf(diagnostics, "Could not seal prepared assets.\n");
    }
    clear_paths(&paths);
    return okay;
}

static AssetFileStatus file_status(const char *root, const char *relative,
                                   char path[ASSET_PATH_LIMIT], struct stat *metadata) {
    if (!join_path(path, root, relative)) return ASSET_FILE_UNREADABLE;
    size_t root_length = strlen(root);
    for (char *cursor = path + root_length + 1; *cursor; cursor++) {
        if (*cursor != '/') continue;
        *cursor = '\0';
        int result = lstat(path, metadata);
        int reason = errno;
        *cursor = '/';
        if (result != 0) {
            return reason == ENOENT ? ASSET_FILE_MISSING : ASSET_FILE_UNREADABLE;
        }
        if (!S_ISDIR(metadata->st_mode)) return ASSET_FILE_WRONG_TYPE;
    }
    if (lstat(path, metadata) != 0) {
        return errno == ENOENT ? ASSET_FILE_MISSING : ASSET_FILE_UNREADABLE;
    }
    return S_ISREG(metadata->st_mode) ? ASSET_FILE_FOUND : ASSET_FILE_WRONG_TYPE;
}

static void issue(FILE *diagnostics, unsigned *count, const char *kind,
                  const char *relative) {
    fprintf(diagnostics, "  %s: %s\n", kind, relative);
    if (*count < UINT32_MAX) (*count)++;
}

bool wm_asset_manifest_verify(const char *assets_root, FILE *diagnostics,
                              unsigned *issue_count) {
    if (issue_count) *issue_count = 0;
    if (!assets_root || !diagnostics || !issue_count) return false;
    char manifest_path[ASSET_PATH_LIMIT];
    if (!join_path(manifest_path, assets_root, WM_ASSET_MANIFEST_NAME)) {
        issue(diagnostics, issue_count, "invalid path", WM_ASSET_MANIFEST_NAME);
        return false;
    }
    struct stat metadata;
    if (stat(assets_root, &metadata) != 0) {
        issue(diagnostics, issue_count,
              errno == ENOENT ? "missing directory" : "unreadable directory",
              assets_root);
        return false;
    }
    if (!S_ISDIR(metadata.st_mode)) {
        issue(diagnostics, issue_count, "wrong type for directory", assets_root);
        return false;
    }
    if (lstat(manifest_path, &metadata) != 0) {
        issue(diagnostics, issue_count, "missing", WM_ASSET_MANIFEST_NAME);
        return false;
    }
    if (!S_ISREG(metadata.st_mode) || metadata.st_size > 16 * 1024 * 1024) {
        issue(diagnostics, issue_count, "invalid", WM_ASSET_MANIFEST_NAME);
        return false;
    }
    FILE *manifest = fopen(manifest_path, "rb");
    if (!manifest) {
        issue(diagnostics, issue_count, "unreadable", WM_ASSET_MANIFEST_NAME);
        return false;
    }
    char line[MANIFEST_LINE_LIMIT];
    bool valid = fgets(line, sizeof(line), manifest) &&
                 strcmp(line, "WM-ASSETS-1\n") == 0;
    char previous[ASSET_PATH_LIMIT] = {0};
    size_t entries = 0;
    while (valid && fgets(line, sizeof(line), manifest)) {
        size_t length = strlen(line);
        if (!length || line[length - 1] != '\n' || length < 44 ||
            strncmp(line + 40, " ", 1) != 0) { valid = false; break; }
        char expected[41];
        memcpy(expected, line, 40);
        expected[40] = '\0';
        for (size_t index = 0; index < 40; index++) {
            if (!((expected[index] >= '0' && expected[index] <= '9') ||
                  (expected[index] >= 'a' && expected[index] <= 'f'))) valid = false;
        }
        char *end;
        errno = 0;
        if (line[41] < '0' || line[41] > '9') valid = false;
        uint64_t expected_size = strtoull(line + 41, &end, 10);
        if (errno || end == line + 41 || *end != ' ') valid = false;
        if (!valid) break;
        char *relative = end + 1;
        relative[strlen(relative) - 1] = '\0';
        if (!valid_relative_path(relative) ||
            strcmp(relative, WM_ASSET_MANIFEST_NAME) == 0 ||
            (previous[0] && strcmp(previous, relative) >= 0)) {
            valid = false;
            break;
        }
        strcpy(previous, relative);
        entries++;
        char path[ASSET_PATH_LIMIT];
        AssetFileStatus status = file_status(assets_root, relative, path, &metadata);
        if (status != ASSET_FILE_FOUND) {
            const char *reason = status == ASSET_FILE_MISSING ? "missing" :
                                 status == ASSET_FILE_WRONG_TYPE ? "wrong type" :
                                 "unreadable";
            issue(diagnostics, issue_count, reason, relative);
            continue;
        }
        if ((uint64_t)metadata.st_size != expected_size) {
            fprintf(diagnostics,
                    "  size mismatch: %s (expected %" PRIu64
                    " bytes, found %" PRIu64 " bytes)\n",
                    relative, expected_size, (uint64_t)metadata.st_size);
            if (*issue_count < UINT32_MAX) (*issue_count)++;
            continue;
        }
        char actual[41];
        uint64_t size;
        if (!hash_file(path, actual, &size)) {
            issue(diagnostics, issue_count, "unreadable", relative);
        } else if (size != expected_size || strcmp(actual, expected) != 0) {
            fprintf(diagnostics,
                    "  SHA-1 mismatch: %s (expected %s, found %s)\n",
                    relative, expected, actual);
            if (*issue_count < UINT32_MAX) (*issue_count)++;
        }
    }
    if (ferror(manifest)) valid = false;
    if (fclose(manifest) != 0) valid = false;
    if (!valid || entries == 0) issue(diagnostics, issue_count, "invalid", WM_ASSET_MANIFEST_NAME);
    return valid && entries != 0 && *issue_count == 0;
}
