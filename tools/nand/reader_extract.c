#define _POSIX_C_SOURCE 200809L
#ifdef __APPLE__
#define _DARWIN_C_SOURCE
#endif

#include "console_common/support/tool_io.h"
#include "reader_internal.h"

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Extract only authenticated, selected entries into a private stage. */
static bool ensure_directory(const char *path) {
    if (mkdir(path, 0700) == 0)
        return true;
    if (errno != EEXIST)
        return false;
    struct stat status;
    return lstat(path, &status) == 0 && S_ISDIR(status.st_mode);
}

static bool ensure_parent_directories(char *path, size_t first_separator) {
    for (size_t index = first_separator; path[index] != '\0'; index++) {
        if (path[index] != '/')
            continue;
        path[index] = '\0';
        bool valid = ensure_directory(path);
        path[index] = '/';
        if (!valid)
            return false;
    }
    return true;
}

static bool extract_file(WmNandReader *reader, uint16_t index, const char *stage,
                         WmNandSummary *summary, char *error, size_t error_capacity) {
    const WmNandEntry *entry = &reader->entries[index];
    char path[WM_NAND_PATH_CAPACITY];
    int length = snprintf(path, sizeof(path), "%s/%s", stage, entry->path);
    if (length < 0 || length >= (int)sizeof(path) ||
        !ensure_parent_directories(path, strlen(stage) + 1)) {
        wm_nand_set_error(error, error_capacity,
                          "Could not prepare the output directory.");
        return false;
    }
    int flags = O_WRONLY | O_CREAT | O_EXCL;
#ifdef O_NOFOLLOW
    flags |= O_NOFOLLOW;
#endif
    int descriptor = open(path, flags, 0600);
    if (descriptor < 0) {
        wm_nand_set_error(error, error_capacity, "Could not create an extracted file.");
        return false;
    }
    FILE *output = fdopen(descriptor, "wb");
    if (output == NULL) {
        close(descriptor);
        unlink(path);
        wm_nand_set_error(error, error_capacity, "Could not open an extracted file.");
        return false;
    }
    uint8_t data[WM_NAND_CLUSTER_SIZE];
    size_t remaining = entry->size;
    bool valid = true;
    for (size_t ordinal = 0; ordinal < entry->cluster_count; ordinal++) {
        if (!wm_nand_read_file_cluster(reader, entry, index, ordinal, data, error,
                                       error_capacity)) {
            valid = false;
            break;
        }
        size_t amount =
            remaining < WM_NAND_CLUSTER_SIZE ? remaining : WM_NAND_CLUSTER_SIZE;
        if (fwrite(data, 1, amount, output) != amount) {
            wm_nand_set_error(error, error_capacity,
                              "Could not write an extracted file.");
            valid = false;
            break;
        }
        remaining -= amount;
    }
    if (fclose(output) != 0)
        valid = false;
    if (!valid || remaining != 0) {
        unlink(path);
        if (valid) {
            wm_nand_set_error(error, error_capacity,
                              "NAND file length is inconsistent.");
        }
        return false;
    }
    summary->extracted_files++;
    summary->extracted_bytes += entry->size;
    return true;
}

static void remove_tree(const char *path) {
    struct stat status;
    if (lstat(path, &status) != 0)
        return;
    if (!S_ISDIR(status.st_mode)) {
        unlink(path);
        return;
    }
    DIR *directory = opendir(path);
    if (directory != NULL) {
        struct dirent *item;
        while ((item = readdir(directory)) != NULL) {
            if (strcmp(item->d_name, ".") == 0 || strcmp(item->d_name, "..") == 0) {
                continue;
            }
            char child[WM_NAND_PATH_CAPACITY];
            int length = snprintf(child, sizeof(child), "%s/%s", path, item->d_name);
            if (length >= 0 && length < (int)sizeof(child)) {
                remove_tree(child);
            }
        }
        closedir(directory);
    }
    rmdir(path);
}

static bool make_stage(const char *output_directory, char stage[WM_NAND_PATH_CAPACITY],
                       char *error, size_t error_capacity) {
    struct stat status;
    if (lstat(output_directory, &status) == 0 || errno != ENOENT) {
        wm_nand_set_error(error, error_capacity,
                          "The NAND extraction destination must not already exist.");
        return false;
    }
    const char *slash = strrchr(output_directory, '/');
    char parent[WM_NAND_PATH_CAPACITY];
    int parent_size = slash == NULL ? snprintf(parent, sizeof(parent), ".")
                      : slash == output_directory
                          ? snprintf(parent, sizeof(parent), "/")
                          : snprintf(parent, sizeof(parent), "%.*s",
                                     (int)(slash - output_directory), output_directory);
    if (parent_size < 0 || parent_size >= (int)sizeof(parent) ||
        lstat(parent, &status) != 0 || !S_ISDIR(status.st_mode)) {
        wm_nand_set_error(error, error_capacity,
                          "The NAND extraction parent directory must exist.");
        return false;
    }
    int length = snprintf(stage, WM_NAND_PATH_CAPACITY, "%s/.wm-nand-XXXXXX", parent);
    if (length < 0 || length >= WM_NAND_PATH_CAPACITY || mkdtemp(stage) == NULL) {
        wm_nand_set_error(error, error_capacity,
                          "Could not create a private NAND extraction stage.");
        return false;
    }
    return true;
}

bool wm_nand_extract_channels(const char *source_path, const char *keys_path,
                              const char *output_directory, WmNandSummary *summary,
                              char *error, size_t error_capacity) {
    if (error != NULL && error_capacity != 0)
        error[0] = '\0';
    if (source_path == NULL || source_path[0] == '\0' || output_directory == NULL ||
        output_directory[0] == '\0' || summary == NULL) {
        wm_nand_set_error(error, error_capacity, "Invalid NAND extraction arguments.");
        return false;
    }
    *summary = (WmNandSummary){0};
    WmNandReader reader = {0};
    if (!wm_nand_open_reader(&reader, source_path, keys_path, error, error_capacity)) {
        wm_nand_close_reader(&reader);
        return false;
    }
    summary->generation = reader.generation;
    if (!wm_nand_discover_channel_titles(&reader, &summary->discovered_titles, error,
                                         error_capacity)) {
        wm_nand_close_reader(&reader);
        return false;
    }
    if (!wm_nand_discover_shared_fonts(&reader, &summary->shared_font_archives, error,
                                       error_capacity)) {
        wm_nand_close_reader(&reader);
        return false;
    }

    char stage[WM_NAND_PATH_CAPACITY];
    if (!make_stage(output_directory, stage, error, error_capacity)) {
        wm_nand_close_reader(&reader);
        return false;
    }
    bool valid = true;
    for (size_t index = 0; index < WM_NAND_NODE_COUNT; index++) {
        if (!reader.entries[index].selected)
            continue;
        if (!extract_file(&reader, (uint16_t)index, stage, summary, error,
                          error_capacity)) {
            valid = false;
            break;
        }
    }
    wm_nand_close_reader(&reader);
    if (valid) {
        struct stat status;
        if (lstat(output_directory, &status) == 0 || errno != ENOENT ||
            rename(stage, output_directory) != 0) {
            wm_nand_set_error(error, error_capacity,
                              "Could not publish the authenticated NAND extraction.");
            valid = false;
        }
    }
    if (!valid) {
        remove_tree(stage);
        *summary = (WmNandSummary){0};
    }
    return valid;
}
