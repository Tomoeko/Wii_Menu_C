#define _POSIX_C_SOURCE 200809L

#include "atomic_file.h"

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#define CHECK(condition)                                                       \
    do {                                                                       \
        if (!(condition)) {                                                    \
            fprintf(stderr, "%s:%d: %s failed\n", __FILE__, __LINE__,          \
                    #condition);                                              \
            return 1;                                                          \
        }                                                                      \
    } while (0)

static bool was_removed(const char *path) {
    errno = 0;
    return access(path, F_OK) != 0 && errno == ENOENT;
}

int main(void) {
    char directory[] = "/tmp/wm-atomic-file-XXXXXX";
    int descriptor = mkstemp(directory);
    CHECK(descriptor >= 0);
    CHECK(close(descriptor) == 0);
    CHECK(unlink(directory) == 0);
    CHECK(mkdir(directory, 0700) == 0);
    char path[256];
    int length = snprintf(path, sizeof(path), "%s/store.json", directory);
    CHECK(length > 0 && (size_t)length < sizeof(path));

    WmAtomicFile file;
    CHECK(wm_atomic_file_open(&file, path) == WM_ATOMIC_FILE_OK);
    char *temporary_path = strdup(file.temporary_path);
    CHECK(temporary_path);
    CHECK(fputs("discarded", file.stream) >= 0);
    wm_atomic_file_discard(&file);
    CHECK(!file.stream && !file.temporary_path);
    CHECK(was_removed(temporary_path));
    CHECK(was_removed(path));
    free(temporary_path);

    CHECK(wm_atomic_file_replace(path, "original", 8));
    CHECK(wm_atomic_file_open(&file, path) == WM_ATOMIC_FILE_OK);
    temporary_path = strdup(file.temporary_path);
    CHECK(temporary_path);
    CHECK(fputs("replacement", file.stream) >= 0);
    CHECK(wm_atomic_file_commit(&file, path));
    CHECK(!file.stream && !file.temporary_path);
    CHECK(was_removed(temporary_path));
    free(temporary_path);
    FILE *saved = fopen(path, "rb");
    CHECK(saved);
    char contents[16] = {0};
    CHECK(fread(contents, 1, sizeof(contents), saved) == 11);
    CHECK(strcmp(contents, "replacement") == 0);
    CHECK(fclose(saved) == 0);

    /* Failed replacement must remove its temporary and retain the target. */
    CHECK(wm_atomic_file_open(&file, path) == WM_ATOMIC_FILE_OK);
    temporary_path = strdup(file.temporary_path);
    CHECK(temporary_path);
    CHECK(fputs("failed", file.stream) >= 0);
    CHECK(!wm_atomic_file_commit(&file, directory));
    CHECK(!file.stream && !file.temporary_path);
    CHECK(was_removed(temporary_path));
    free(temporary_path);
    struct stat status;
    CHECK(stat(directory, &status) == 0 && S_ISDIR(status.st_mode));

    CHECK(unlink(path) == 0);
    CHECK(rmdir(directory) == 0);
    CHECK(wm_atomic_file_open(&file, path) == WM_ATOMIC_FILE_CREATE_FAILED);
    CHECK(!file.stream && !file.temporary_path);
    return 0;
}
