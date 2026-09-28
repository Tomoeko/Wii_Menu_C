#ifndef WM_SUPPORT_REGULAR_FILE_H
#define WM_SUPPORT_REGULAR_FILE_H

#include <stddef.h>

typedef enum WmRegularFileStatus {
    WM_REGULAR_FILE_OK,
    WM_REGULAR_FILE_MISSING,
    WM_REGULAR_FILE_ERROR
} WmRegularFileStatus;

/* Read one stable, non-symlink regular file. The caller owns *contents on
 * success; missing files are distinct from malformed or unsafe paths. */
WmRegularFileStatus wm_regular_file_read(const char *path, size_t limit,
                                         char **contents, size_t *length);

#endif
