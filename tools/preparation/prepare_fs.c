#if defined(__linux__)
#define _GNU_SOURCE 1
#endif
#define _XOPEN_SOURCE 700
#if defined(__APPLE__)
#define _DARWIN_C_SOURCE 1
#endif

#include "prepare_fs.h"

#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#if defined(__APPLE__)
#include <sys/stdio.h>
#elif defined(__linux__)
#include <linux/fs.h>
#include <sys/syscall.h>
#endif

bool path_join(char *result, size_t capacity, const char *directory, const char *name) {
    int length = snprintf(result, capacity, "%s/%s", directory, name);
    return length > 0 && (size_t)length < capacity;
}

bool publish_directory_no_replace(const char *source, const char *destination) {
#if defined(__APPLE__)
    return renamex_np(source, destination, RENAME_EXCL) == 0;
#elif defined(__linux__) && defined(SYS_renameat2) && defined(RENAME_NOREPLACE)
    return syscall(SYS_renameat2, AT_FDCWD, source, AT_FDCWD, destination,
                   RENAME_NOREPLACE) == 0;
#else
    (void)source;
    (void)destination;
    errno = ENOTSUP;
    return false;
#endif
}

bool validate_parents(const char *path, bool create_missing) {
    size_t length = strlen(path);
    if (length == 0 || length >= PREPARE_PATH_CAPACITY)
        return false;
    char current[PREPARE_PATH_CAPACITY];
    memcpy(current, path, length + 1);
    for (size_t index = 1; index <= length; index++) {
        if (current[index] != '/' && current[index] != '\0')
            continue;
        char saved = current[index];
        current[index] = '\0';
        if (current[0] && create_missing && mkdir(current, 0700) != 0 &&
            errno != EEXIST)
            return false;
        struct stat metadata;
        if (current[0] &&
            (lstat(current, &metadata) != 0 || !S_ISDIR(metadata.st_mode)))
            return false;
        current[index] = saved;
    }
    return true;
}

static bool make_parents(const char *path) {
    return validate_parents(path, true);
}

bool make_file_parent(const char *path) {
    char parent[PREPARE_PATH_CAPACITY];
    size_t length = strlen(path);
    if (length == 0 || length >= sizeof(parent))
        return false;
    memcpy(parent, path, length + 1);
    char *separator = strrchr(parent, '/');
    if (!separator)
        return true;
    if (separator == parent)
        separator[1] = '\0';
    else
        *separator = '\0';
    return make_parents(parent);
}

bool remove_tree(const char *path) {
    struct stat metadata;
    if (lstat(path, &metadata) != 0)
        return errno == ENOENT;
    if (!S_ISDIR(metadata.st_mode))
        return unlink(path) == 0;
    DIR *directory = opendir(path);
    if (!directory)
        return false;
    bool okay = true;
    struct dirent *entry;
    while ((entry = readdir(directory)) != NULL) {
        if (strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0)
            continue;
        char child[PREPARE_PATH_CAPACITY];
        if (!path_join(child, sizeof(child), path, entry->d_name) ||
            !remove_tree(child))
            okay = false;
    }
    if (closedir(directory) != 0)
        okay = false;
    if (okay && rmdir(path) != 0)
        okay = false;
    return okay;
}

bool copy_file(const char *source, const char *destination) {
    int input = open(source, O_RDONLY | O_NOFOLLOW);
    if (input < 0)
        return false;
    struct stat metadata;
    bool okay = fstat(input, &metadata) == 0 && S_ISREG(metadata.st_mode);
    int output =
        okay ? open(destination, O_WRONLY | O_CREAT | O_EXCL | O_NOFOLLOW, 0600) : -1;
    if (output < 0)
        okay = false;
    char bytes[65536];
    while (okay) {
        ssize_t count = read(input, bytes, sizeof(bytes));
        if (count < 0 && errno == EINTR)
            continue;
        if (count < 0)
            okay = false;
        if (count <= 0)
            break;
        size_t written = 0;
        while (written < (size_t)count) {
            ssize_t amount = write(output, bytes + written, (size_t)count - written);
            if (amount < 0 && errno == EINTR)
                continue;
            if (amount <= 0) {
                okay = false;
                break;
            }
            written += (size_t)amount;
        }
    }
    if (output >= 0 && close(output) != 0)
        okay = false;
    if (close(input) != 0)
        okay = false;
    if (!okay && output >= 0)
        unlink(destination);
    return okay;
}

bool copy_tree(const char *source, const char *destination) {
    struct stat metadata;
    if (lstat(source, &metadata) != 0)
        return false;
    if (S_ISREG(metadata.st_mode))
        return copy_file(source, destination);
    if (!S_ISDIR(metadata.st_mode) || mkdir(destination, 0700) != 0)
        return false;
    DIR *directory = opendir(source);
    if (!directory)
        return false;
    bool okay = true;
    struct dirent *entry;
    while ((entry = readdir(directory)) != NULL) {
        if (strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0)
            continue;
        char from[PREPARE_PATH_CAPACITY];
        char to[PREPARE_PATH_CAPACITY];
        if (!path_join(from, sizeof(from), source, entry->d_name) ||
            !path_join(to, sizeof(to), destination, entry->d_name) ||
            !copy_tree(from, to))
            okay = false;
        if (!okay)
            break;
    }
    if (closedir(directory) != 0)
        okay = false;
    return okay;
}

bool regular_tree(const char *path) {
    struct stat metadata;
    if (lstat(path, &metadata) != 0)
        return false;
    if (S_ISREG(metadata.st_mode))
        return true;
    if (!S_ISDIR(metadata.st_mode))
        return false;
    DIR *directory = opendir(path);
    if (!directory)
        return false;
    bool okay = true;
    struct dirent *entry;
    while ((entry = readdir(directory)) != NULL) {
        if (strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0)
            continue;
        char child[PREPARE_PATH_CAPACITY];
        if (!path_join(child, sizeof(child), path, entry->d_name) ||
            !regular_tree(child))
            okay = false;
        if (!okay)
            break;
    }
    if (closedir(directory) != 0)
        okay = false;
    return okay;
}
