#ifndef WII_MENU_ATOMIC_FILE_H
#define WII_MENU_ATOMIC_FILE_H

#include <stdbool.h>
#include <stddef.h>
#include <stdio.h>

typedef enum WmAtomicFileStatus {
    WM_ATOMIC_FILE_OK,
    WM_ATOMIC_FILE_PATH_TOO_LONG,
    WM_ATOMIC_FILE_ALLOCATION_FAILED,
    WM_ATOMIC_FILE_CREATE_FAILED,
    WM_ATOMIC_FILE_OPEN_FAILED
} WmAtomicFileStatus;

/* Owns a same-directory temporary file until commit or discard consumes it. */
typedef struct WmAtomicFile {
    char *temporary_path;
    FILE *stream;
} WmAtomicFile;

WmAtomicFileStatus wm_atomic_file_open(WmAtomicFile *file, const char *path);
bool wm_atomic_file_commit(WmAtomicFile *file, const char *path);
void wm_atomic_file_discard(WmAtomicFile *file);
bool wm_atomic_file_replace(const char *path, const char *data, size_t length);

#endif
