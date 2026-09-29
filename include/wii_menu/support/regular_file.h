#ifndef WM_SUPPORT_REGULAR_FILE_H
#define WM_SUPPORT_REGULAR_FILE_H

#include <stddef.h>
#include <stdint.h>

typedef enum WmRegularFileStatus {
    WM_REGULAR_FILE_OK,
    WM_REGULAR_FILE_MISSING,
    WM_REGULAR_FILE_ERROR
} WmRegularFileStatus;

/* Read one stable, non-symlink regular file. The caller owns *contents on
 * success; missing files are distinct from malformed or unsafe paths. */
WmRegularFileStatus wm_regular_file_read(const char *path, size_t limit,
                                         char **contents, size_t *length);

/* Like wm_regular_file_read, with explicit size bounds and byte output.
 * A zero minimum accepts empty files. Both functions NUL-terminate their
 * allocations for callers that also need a string view. */
WmRegularFileStatus wm_regular_file_read_bytes(const char *path, size_t minimum,
                                               size_t limit, uint8_t **contents,
                                               size_t *length);

#endif
