#define _POSIX_C_SOURCE 200809L

#include "atomic_file.h"

#include <errno.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

WmAtomicFileStatus wm_atomic_file_open(WmAtomicFile *file, const char *path) {
    *file = (WmAtomicFile){0};
    size_t path_length = strlen(path);
    static const char suffix[] = ".tmp.XXXXXX";
    if (path_length > SIZE_MAX - sizeof(suffix)) {
        return WM_ATOMIC_FILE_PATH_TOO_LONG;
    }
    file->temporary_path = malloc(path_length + sizeof(suffix));
    if (!file->temporary_path)
        return WM_ATOMIC_FILE_ALLOCATION_FAILED;
    memcpy(file->temporary_path, path, path_length);
    memcpy(file->temporary_path + path_length, suffix, sizeof(suffix));
    int descriptor = mkstemp(file->temporary_path);
    if (descriptor < 0) {
        free(file->temporary_path);
        *file = (WmAtomicFile){0};
        return WM_ATOMIC_FILE_CREATE_FAILED;
    }
    file->stream = fdopen(descriptor, "wb");
    if (!file->stream) {
        close(descriptor);
        wm_atomic_file_discard(file);
        return WM_ATOMIC_FILE_OPEN_FAILED;
    }
    return WM_ATOMIC_FILE_OK;
}

void wm_atomic_file_discard(WmAtomicFile *file) {
    if (file->stream)
        fclose(file->stream);
    if (file->temporary_path)
        unlink(file->temporary_path);
    free(file->temporary_path);
    *file = (WmAtomicFile){0};
}

bool wm_atomic_file_commit(WmAtomicFile *file, const char *path) {
    bool success = !ferror(file->stream) && fflush(file->stream) == 0 &&
                   fsync(fileno(file->stream)) == 0;
    if (fclose(file->stream) != 0)
        success = false;
    file->stream = NULL;
    if (success)
        success = rename(file->temporary_path, path) == 0;
    if (!success)
        unlink(file->temporary_path);
    free(file->temporary_path);
    *file = (WmAtomicFile){0};
    return success;
}

bool wm_atomic_file_replace(const char *path, const void *data, size_t length) {
    WmAtomicFile file;
    if (wm_atomic_file_open(&file, path) != WM_ATOMIC_FILE_OK)
        return false;
    size_t offset = 0;
    const unsigned char *bytes = data;
    /* This path writes directly to the descriptor; the stream remains empty. */
    int descriptor = fileno(file.stream);
    while (offset < length) {
        ssize_t count = write(descriptor, bytes + offset, length - offset);
        if (count < 0 && errno == EINTR)
            continue;
        if (count <= 0) {
            wm_atomic_file_discard(&file);
            return false;
        }
        offset += (size_t)count;
    }
    return wm_atomic_file_commit(&file, path);
}
