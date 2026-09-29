#define _POSIX_C_SOURCE 200809L

#include "wii_menu/support/regular_file.h"

#include <errno.h>
#include <fcntl.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>
#include <sys/stat.h>
#include <unistd.h>

static WmRegularFileStatus read_range(const char *path, size_t minimum, size_t limit,
                                      char **contents, size_t *length) {
    if (!contents || !length)
        return WM_REGULAR_FILE_ERROR;
    *contents = NULL;
    *length = 0;
    if (!path || minimum > limit)
        return WM_REGULAR_FILE_ERROR;

    struct stat named;
    if (lstat(path, &named) != 0) {
        return errno == ENOENT ? WM_REGULAR_FILE_MISSING : WM_REGULAR_FILE_ERROR;
    }
    if (!S_ISREG(named.st_mode))
        return WM_REGULAR_FILE_ERROR;

    /* A path swapped for a FIFO between lstat and open must not block here. */
    int flags = O_RDONLY | O_NONBLOCK;
#ifdef O_NOFOLLOW
    flags |= O_NOFOLLOW;
#endif
#ifdef O_CLOEXEC
    flags |= O_CLOEXEC;
#endif
    int descriptor = open(path, flags);
    if (descriptor < 0)
        return WM_REGULAR_FILE_ERROR;

    struct stat opened;
    bool valid = fstat(descriptor, &opened) == 0 && S_ISREG(opened.st_mode) &&
                 opened.st_dev == named.st_dev && opened.st_ino == named.st_ino &&
                 opened.st_size >= 0 && (uint64_t)opened.st_size >= minimum &&
                 (uint64_t)opened.st_size <= limit &&
                 (uint64_t)opened.st_size < SIZE_MAX;
    if (!valid) {
        close(descriptor);
        return WM_REGULAR_FILE_ERROR;
    }

    size_t size = (size_t)opened.st_size;
    char *bytes = malloc(size + 1);
    if (!bytes) {
        close(descriptor);
        return WM_REGULAR_FILE_ERROR;
    }
    size_t offset = 0;
    while (offset < size) {
        ssize_t count = read(descriptor, bytes + offset, size - offset);
        if (count < 0 && errno == EINTR)
            continue;
        if (count <= 0)
            break;
        offset += (size_t)count;
    }
    char extra;
    ssize_t after;
    do {
        after = read(descriptor, &extra, 1);
    } while (after < 0 && errno == EINTR);
    valid = offset == size && after == 0;
    if (close(descriptor) != 0)
        valid = false;
    if (!valid) {
        free(bytes);
        return WM_REGULAR_FILE_ERROR;
    }
    bytes[size] = '\0';
    *contents = bytes;
    *length = size;
    return WM_REGULAR_FILE_OK;
}

WmRegularFileStatus wm_regular_file_read(const char *path, size_t limit,
                                         char **contents, size_t *length) {
    return read_range(path, 1, limit, contents, length);
}

WmRegularFileStatus wm_regular_file_read_bytes(const char *path, size_t minimum,
                                               size_t limit, uint8_t **contents,
                                               size_t *length) {
    if (!contents)
        return WM_REGULAR_FILE_ERROR;
    *contents = NULL;
    char *bytes = NULL;
    WmRegularFileStatus status = read_range(path, minimum, limit, &bytes, length);
    if (status == WM_REGULAR_FILE_OK)
        *contents = (uint8_t *)bytes;
    return status;
}
