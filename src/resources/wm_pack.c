#if defined(__linux__)
#define _GNU_SOURCE 1
#endif
#define _XOPEN_SOURCE 700
#if defined(__APPLE__)
#define _DARWIN_C_SOURCE 1
#endif

#include "wii_menu/resources/wm_pack.h"

#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

#if defined(__APPLE__)
#include <sys/stdio.h>
#elif defined(__linux__)
#include <linux/fs.h>
#include <sys/syscall.h>
#endif

enum { PACK_HEADER_SIZE = 32, PACK_ENTRY_SIZE = 16, PACK_PATH_CAPACITY = 4096 };
static const uint8_t PACK_MAGIC[8] = {'W', 'M', 'P', 'A', 'C', 'K', '1', 0};

typedef struct PackFile {
    char *path;
    uint64_t size;
} PackFile;

typedef struct PackFiles {
    PackFile *items;
    size_t count;
    size_t capacity;
    size_t directories;
    uint64_t total;
} PackFiles;

static bool pack_error(char *error, size_t capacity, const char *message) {
    if (error && capacity) snprintf(error, capacity, "%s", message);
    return false;
}

static bool pack_join(char *result, size_t capacity,
                      const char *directory, const char *path) {
    int size = snprintf(result, capacity, "%s/%s", directory, path);
    return size > 0 && (size_t)size < capacity;
}

static uint32_t pack_u32(const uint8_t *bytes) {
    return (uint32_t)bytes[0] | (uint32_t)bytes[1] << 8 |
           (uint32_t)bytes[2] << 16 | (uint32_t)bytes[3] << 24;
}

static uint64_t pack_u64(const uint8_t *bytes) {
    return (uint64_t)pack_u32(bytes) | (uint64_t)pack_u32(bytes + 4) << 32;
}

static void pack_put_u32(uint8_t *bytes, uint32_t value) {
    for (unsigned index = 0; index < 4; index++) {
        bytes[index] = (uint8_t)(value >> (8 * index));
    }
}

static void pack_put_u64(uint8_t *bytes, uint64_t value) {
    pack_put_u32(bytes, (uint32_t)value);
    pack_put_u32(bytes + 4, (uint32_t)(value >> 32));
}

static uint32_t pack_crc(uint32_t crc, const uint8_t *bytes, size_t size) {
    uint32_t nibble_table[16];
    for (uint32_t index = 0; index < 16; index++) {
        uint32_t value = index;
        for (unsigned bit = 0; bit < 4; bit++)
            value = (value >> 1) ^ (UINT32_C(0xedb88320) &
                                    (uint32_t)-(int32_t)(value & 1));
        nibble_table[index] = value;
    }
    for (size_t index = 0; index < size; index++) {
        crc ^= bytes[index];
        crc = (crc >> 4) ^ nibble_table[crc & 15];
        crc = (crc >> 4) ^ nibble_table[crc & 15];
    }
    return crc;
}

static bool pack_safe_path(const char *path, size_t size) {
    if (!size || size > WM_PACK_MAX_PATH || path[0] == '/' ||
        path[size - 1] == '/') return false;
    size_t start = 0;
    unsigned depth = 0;
    for (size_t index = 0; index <= size; index++) {
        unsigned char value = (unsigned char)path[index];
        if (index < size && (value < 32 || value == 127 || value == '\\' ||
                             value == ':')) return false;
        if (index < size && value != '/') continue;
        size_t length = index - start;
        if (!length || (length == 1 && path[start] == '.') ||
            (length == 2 && path[start] == '.' && path[start + 1] == '.') ||
            ++depth > 64) return false;
        start = index + 1;
    }
    return true;
}

/* Every path component is opened relative to an already opened directory.
 * O_NOFOLLOW protects against changed source ancestors and extraction links. */
static int pack_parent_fd(int root, const char *path, bool create,
                          size_t *directories,
                          char basename[WM_PACK_MAX_PATH + 1]) {
    char copy[WM_PACK_MAX_PATH + 1];
    strcpy(copy, path);
    int parent = dup(root);
    if (parent < 0) return -1;
    char *component = copy;
    char *separator;
    while ((separator = strchr(component, '/')) != NULL) {
        *separator = '\0';
        if (create) {
            int created = mkdirat(parent, component, 0700);
            if ((created != 0 && errno != EEXIST) ||
                (created == 0 && ++*directories > WM_PACK_MAX_DIRECTORIES)) {
                close(parent);
                return -1;
            }
        }
        int next = openat(parent, component, O_RDONLY | O_DIRECTORY | O_NOFOLLOW);
        close(parent);
        if (next < 0) return -1;
        parent = next;
        component = separator + 1;
    }
    strcpy(basename, component);
    return parent;
}

static bool pack_collect(int root, const char *relative, PackFiles *files) {
    char basename[WM_PACK_MAX_PATH + 1];
    int parent = relative[0] ? pack_parent_fd(root, relative, false, NULL, basename) : -1;
    int descriptor = relative[0] ?
        (parent >= 0 ? openat(parent, basename,
                             O_RDONLY | O_DIRECTORY | O_NOFOLLOW) : -1) : dup(root);
    if (parent >= 0) close(parent);
    if (descriptor < 0) return false;
    DIR *directory = fdopendir(descriptor);
    if (!directory) { close(descriptor); return false; }
    bool okay = true;
    struct dirent *entry;
    while (okay && (entry = readdir(directory)) != NULL) {
        if (strcmp(entry->d_name, ".") == 0 ||
            strcmp(entry->d_name, "..") == 0) continue;
        char path[WM_PACK_MAX_PATH + 1];
        int length = snprintf(path, sizeof(path), "%s%s%s", relative,
                              relative[0] ? "/" : "", entry->d_name);
        struct stat metadata;
        if (length <= 0 || (size_t)length >= sizeof(path) ||
            !pack_safe_path(path, (size_t)length) ||
            fstatat(descriptor, entry->d_name, &metadata, AT_SYMLINK_NOFOLLOW) != 0) {
            okay = false;
        } else if (S_ISDIR(metadata.st_mode)) {
            okay = ++files->directories <= WM_PACK_MAX_DIRECTORIES &&
                   pack_collect(root, path, files);
        } else if (!S_ISREG(metadata.st_mode) || metadata.st_nlink != 1 ||
                   metadata.st_size < 0 ||
                   (uint64_t)metadata.st_size > WM_PACK_MAX_FILE_BYTES ||
                   files->count >= WM_PACK_MAX_FILES ||
                   (uint64_t)metadata.st_size > WM_PACK_MAX_BYTES - files->total) {
            okay = false;
        } else {
            if (files->count == files->capacity) {
                size_t capacity = files->capacity ? files->capacity * 2 : 64;
                PackFile *items = realloc(files->items, capacity * sizeof(*items));
                if (!items) { okay = false; continue; }
                files->items = items;
                files->capacity = capacity;
            }
            char *name = strdup(path);
            if (!name) { okay = false; continue; }
            files->items[files->count++] = (PackFile){name, (uint64_t)metadata.st_size};
            files->total += (uint64_t)metadata.st_size;
        }
    }
    if (closedir(directory) != 0) okay = false;
    return okay;
}

static int pack_compare(const void *left, const void *right) {
    return strcmp(((const PackFile *)left)->path, ((const PackFile *)right)->path);
}

static void pack_files_free(PackFiles *files) {
    for (size_t index = 0; index < files->count; index++) free(files->items[index].path);
    free(files->items);
}

static bool pack_write_file(FILE *output, int root, const PackFile *file) {
    char basename[WM_PACK_MAX_PATH + 1];
    int parent = pack_parent_fd(root, file->path, false, NULL, basename);
    int descriptor = parent >= 0 ? openat(parent, basename, O_RDONLY | O_NOFOLLOW) : -1;
    if (parent >= 0) close(parent);
    if (descriptor < 0) return false;
    struct stat metadata;
    bool okay = fstat(descriptor, &metadata) == 0 && S_ISREG(metadata.st_mode) &&
                metadata.st_nlink == 1 && metadata.st_size >= 0 &&
                (uint64_t)metadata.st_size == file->size;
    off_t record_offset = ftello(output);
    uint8_t record[PACK_ENTRY_SIZE] = {0};
    pack_put_u32(record, (uint32_t)strlen(file->path));
    pack_put_u64(record + 8, file->size);
    if (okay) okay = record_offset >= 0 &&
        fwrite(record, 1, sizeof(record), output) == sizeof(record) &&
        fwrite(file->path, 1, strlen(file->path), output) == strlen(file->path);
    uint64_t remaining = file->size;
    uint32_t crc = UINT32_MAX;
    uint8_t bytes[65536];
    while (okay && remaining) {
        size_t request = remaining < sizeof(bytes) ? (size_t)remaining : sizeof(bytes);
        ssize_t count = read(descriptor, bytes, request);
        if (count < 0 && errno == EINTR) continue;
        if (count <= 0) { okay = false; break; }
        crc = pack_crc(crc, bytes, (size_t)count);
        okay = fwrite(bytes, 1, (size_t)count, output) == (size_t)count;
        remaining -= (uint64_t)count;
    }
    uint8_t extra;
    if (okay) okay = read(descriptor, &extra, 1) == 0;
    if (close(descriptor) != 0) okay = false;
    off_t end = ftello(output);
    if (okay) {
        pack_put_u32(record + 4, crc ^ UINT32_MAX);
        okay = end >= 0 && fseeko(output, record_offset, SEEK_SET) == 0 &&
               fwrite(record, 1, sizeof(record), output) == sizeof(record) &&
               fseeko(output, end, SEEK_SET) == 0;
    }
    return okay;
}

static bool pack_parent_safe(const char *path) {
    char copy[PACK_PATH_CAPACITY];
    size_t length = strlen(path);
    if (!length || length >= sizeof(copy)) return false;
    memcpy(copy, path, length + 1);
    for (size_t index = 1; index < length; index++) {
        if (copy[index] != '/') continue;
        copy[index] = '\0';
        struct stat metadata;
        bool okay = lstat(copy, &metadata) == 0 && S_ISDIR(metadata.st_mode);
        copy[index] = '/';
        if (!okay) return false;
    }
    return true;
}

bool wm_pack_create(const char *source_directory, const char *package_path,
                    char *error, size_t error_capacity) {
    if (error && error_capacity) error[0] = '\0';
    if (!source_directory || !package_path || !pack_parent_safe(source_directory) ||
        !pack_parent_safe(package_path))
        return pack_error(error, error_capacity, "Invalid package destination or linked parent.");
    struct stat destination;
    bool exists = lstat(package_path, &destination) == 0;
    if ((exists && (!S_ISREG(destination.st_mode) || destination.st_nlink != 1)) ||
        (!exists && errno != ENOENT))
        return pack_error(error, error_capacity, "Package destination is not a regular file.");
    int root = open(source_directory, O_RDONLY | O_DIRECTORY | O_NOFOLLOW);
    if (root < 0) return pack_error(error, error_capacity, "Could not open asset directory.");
    PackFiles files = {0};
    bool okay = pack_collect(root, "", &files);
    if (!okay) {
        close(root);
        pack_files_free(&files);
        return pack_error(error, error_capacity, "Assets contain links, special files or exceed package limits.");
    }
    if (files.count > 1) qsort(files.items, files.count, sizeof(*files.items), pack_compare);
    char temporary[PACK_PATH_CAPACITY];
    int length = snprintf(temporary, sizeof(temporary), "%s.tmp.XXXXXX", package_path);
    int descriptor = length > 0 && (size_t)length < sizeof(temporary) ? mkstemp(temporary) : -1;
    FILE *output = descriptor >= 0 ? fdopen(descriptor, "w+b") : NULL;
    if (!output && descriptor >= 0) close(descriptor);
    okay = output != NULL;
    uint8_t header[PACK_HEADER_SIZE] = {0};
    memcpy(header, PACK_MAGIC, sizeof(PACK_MAGIC));
    pack_put_u32(header + 8, 1);
    pack_put_u32(header + 12, (uint32_t)files.count);
    pack_put_u64(header + 16, files.total);
    if (okay) okay = fwrite(header, 1, sizeof(header), output) == sizeof(header);
    for (size_t index = 0; okay && index < files.count; index++)
        okay = pack_write_file(output, root, &files.items[index]);
    if (okay) okay = fflush(output) == 0 && fsync(fileno(output)) == 0;
    if (output && fclose(output) != 0) okay = false;
    if (close(root) != 0) okay = false;
    pack_files_free(&files);
    if (okay) okay = rename(temporary, package_path) == 0;
    if (!okay && descriptor >= 0) unlink(temporary);
    return okay || pack_error(error, error_capacity, "Could not publish complete asset package.");
}

static bool pack_remove_tree(const char *path) {
    struct stat metadata;
    if (lstat(path, &metadata) != 0) return errno == ENOENT;
    if (!S_ISDIR(metadata.st_mode)) return unlink(path) == 0;
    DIR *directory = opendir(path);
    if (!directory) return false;
    bool okay = true;
    struct dirent *entry;
    while ((entry = readdir(directory)) != NULL) {
        if (strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0) continue;
        char child[PACK_PATH_CAPACITY];
        if (!pack_join(child, sizeof(child), path, entry->d_name) ||
            !pack_remove_tree(child)) okay = false;
    }
    if (closedir(directory) != 0 || rmdir(path) != 0) okay = false;
    return okay;
}

static bool pack_extract_file(FILE *input, int root, const char *path,
                              uint64_t size, uint32_t expected_crc,
                              size_t *directories) {
    char basename[WM_PACK_MAX_PATH + 1];
    int parent = pack_parent_fd(root, path, true, directories, basename);
    int descriptor = parent >= 0 ? openat(parent, basename,
        O_WRONLY | O_CREAT | O_EXCL | O_NOFOLLOW, 0600) : -1;
    if (parent >= 0) close(parent);
    if (descriptor < 0) return false;
    uint32_t crc = UINT32_MAX;
    uint8_t bytes[65536];
    bool okay = true;
    while (okay && size) {
        size_t request = size < sizeof(bytes) ? (size_t)size : sizeof(bytes);
        if (fread(bytes, 1, request, input) != request) { okay = false; break; }
        crc = pack_crc(crc, bytes, request);
        size_t offset = 0;
        while (offset < request) {
            ssize_t written = write(descriptor, bytes + offset, request - offset);
            if (written < 0 && errno == EINTR) continue;
            if (written <= 0) { okay = false; break; }
            offset += (size_t)written;
        }
        size -= request;
    }
    if (close(descriptor) != 0) okay = false;
    return okay && (crc ^ UINT32_MAX) == expected_crc;
}

static bool pack_publish_tree(const char *source, const char *destination) {
#if defined(__APPLE__)
    return renamex_np(source, destination, RENAME_EXCL) == 0;
#elif defined(__linux__) && defined(SYS_renameat2) && defined(RENAME_NOREPLACE)
    return syscall(SYS_renameat2, AT_FDCWD, source, AT_FDCWD,
                   destination, RENAME_NOREPLACE) == 0;
#else
    /* OpenBSD lacks rename-with-no-replace. The caller owns its private RAM
     * parent; recheck immediately before the same-filesystem publication. */
    struct stat metadata;
    return lstat(destination, &metadata) != 0 && errno == ENOENT &&
           rename(source, destination) == 0;
#endif
}

bool wm_pack_extract(const char *package_path, const char *directory,
                     char *error, size_t error_capacity) {
    if (error && error_capacity) error[0] = '\0';
    if (!package_path || !directory || !pack_parent_safe(directory))
        return pack_error(error, error_capacity, "Invalid extraction destination or linked parent.");
    struct stat metadata;
    if (lstat(directory, &metadata) == 0 || errno != ENOENT)
        return pack_error(error, error_capacity, "Extraction destination already exists.");
    int descriptor = open(package_path, O_RDONLY | O_NOFOLLOW);
    FILE *input = NULL;
    if (descriptor >= 0 && fstat(descriptor, &metadata) == 0 &&
        S_ISREG(metadata.st_mode) && metadata.st_nlink == 1 && metadata.st_size >= 0)
        input = fdopen(descriptor, "rb");
    if (!input) {
        if (descriptor >= 0) close(descriptor);
        return pack_error(error, error_capacity, "Could not open regular asset package.");
    }
    uint8_t header[PACK_HEADER_SIZE];
    bool okay = fread(header, 1, sizeof(header), input) == sizeof(header) &&
        memcmp(header, PACK_MAGIC, sizeof(PACK_MAGIC)) == 0 &&
        pack_u32(header + 8) == 1 && pack_u32(header + 12) <= WM_PACK_MAX_FILES &&
        pack_u64(header + 16) <= WM_PACK_MAX_BYTES && pack_u64(header + 24) == 0;
    uint32_t count = okay ? pack_u32(header + 12) : 0;
    uint64_t expected_total = okay ? pack_u64(header + 16) : 0;
    uint64_t total = 0;
    size_t directories = 0;
    uint64_t maximum_size = PACK_HEADER_SIZE + (uint64_t)WM_PACK_MAX_BYTES +
        (uint64_t)WM_PACK_MAX_FILES * (PACK_ENTRY_SIZE + WM_PACK_MAX_PATH);
    okay = okay && (uint64_t)metadata.st_size <= maximum_size;
    char temporary[PACK_PATH_CAPACITY];
    int length = snprintf(temporary, sizeof(temporary), "%s.tmp.XXXXXX", directory);
    bool staged = okay && length > 0 && (size_t)length < sizeof(temporary) &&
                  mkdtemp(temporary) != NULL;
    int root = staged ? open(temporary, O_RDONLY | O_DIRECTORY | O_NOFOLLOW) : -1;
    okay = okay && root >= 0;
    char previous[WM_PACK_MAX_PATH + 1] = {0};
    for (uint32_t index = 0; okay && index < count; index++) {
        uint8_t record[PACK_ENTRY_SIZE];
        okay = fread(record, 1, sizeof(record), input) == sizeof(record);
        uint32_t path_size = okay ? pack_u32(record) : 0;
        uint64_t size = okay ? pack_u64(record + 8) : 0;
        char path[WM_PACK_MAX_PATH + 1];
        okay = okay && path_size > 0 && path_size <= WM_PACK_MAX_PATH &&
            size <= WM_PACK_MAX_FILE_BYTES && size <= expected_total - total &&
            fread(path, 1, path_size, input) == path_size;
        if (!okay) break;
        path[path_size] = '\0';
        okay = pack_safe_path(path, path_size) && strlen(path) == path_size &&
               strcmp(previous, path) < 0;
        if (okay) okay = pack_extract_file(input, root, path, size,
                                            pack_u32(record + 4), &directories);
        if (okay) {
            strcpy(previous, path);
            total += size;
        }
    }
    if (okay) okay = total == expected_total && fgetc(input) == EOF && !ferror(input);
    if (root >= 0 && close(root) != 0) okay = false;
    if (fclose(input) != 0) okay = false;
    if (okay) okay = pack_publish_tree(temporary, directory);
    if (!okay && staged) pack_remove_tree(temporary);
    return okay || pack_error(error, error_capacity,
        "Asset package is malformed, damaged, exceeds limits or could not be extracted.");
}

bool wm_pack_adjacent(const char *executable_path, char *package_path,
                      size_t capacity) {
    if (!executable_path || !package_path || !capacity) return false;
    const char *basename = strrchr(executable_path, '/');
    basename = basename ? basename + 1 : executable_path;
    if (!basename[0]) return false;
    const char *suffix = strrchr(basename, '.');
    size_t length = suffix && suffix != basename ? (size_t)(suffix - executable_path) :
                                                 strlen(executable_path);
    if (length > SIZE_MAX - 4 || length + 4 > capacity) return false;
    memmove(package_path, executable_path, length);
    memcpy(package_path + length, ".wm", 4);
    return true;
}
