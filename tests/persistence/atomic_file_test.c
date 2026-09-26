#define _POSIX_C_SOURCE 200809L

#include "atomic_file.h"

#include <assert.h>
#include <errno.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

static void assert_removed(const char *path) {
    errno = 0;
    assert(access(path, F_OK) != 0 && errno == ENOENT);
}

int main(void) {
    char directory[] = "/tmp/wm-atomic-file-XXXXXX";
    int descriptor = mkstemp(directory);
    assert(descriptor >= 0);
    assert(close(descriptor) == 0);
    assert(unlink(directory) == 0);
    assert(mkdir(directory, 0700) == 0);
    char path[256];
    int length = snprintf(path, sizeof(path), "%s/store.json", directory);
    assert(length > 0 && (size_t)length < sizeof(path));

    WmAtomicFile file;
    assert(wm_atomic_file_open(&file, path) == WM_ATOMIC_FILE_OK);
    char *temporary_path = strdup(file.temporary_path);
    assert(temporary_path);
    assert(fputs("discarded", file.stream) >= 0);
    wm_atomic_file_discard(&file);
    assert(!file.stream && !file.temporary_path);
    assert_removed(temporary_path);
    assert_removed(path);
    free(temporary_path);

    assert(wm_atomic_file_replace(path, "original", 8));
    assert(wm_atomic_file_open(&file, path) == WM_ATOMIC_FILE_OK);
    temporary_path = strdup(file.temporary_path);
    assert(temporary_path);
    assert(fputs("replacement", file.stream) >= 0);
    assert(wm_atomic_file_commit(&file, path));
    assert(!file.stream && !file.temporary_path);
    assert_removed(temporary_path);
    free(temporary_path);
    FILE *saved = fopen(path, "rb");
    assert(saved);
    char contents[16] = {0};
    assert(fread(contents, 1, sizeof(contents), saved) == 11);
    assert(strcmp(contents, "replacement") == 0);
    assert(fclose(saved) == 0);

    /* Failed replacement must remove its temporary and retain the target. */
    assert(wm_atomic_file_open(&file, path) == WM_ATOMIC_FILE_OK);
    temporary_path = strdup(file.temporary_path);
    assert(temporary_path);
    assert(fputs("failed", file.stream) >= 0);
    assert(!wm_atomic_file_commit(&file, directory));
    assert(!file.stream && !file.temporary_path);
    assert_removed(temporary_path);
    free(temporary_path);
    struct stat status;
    assert(stat(directory, &status) == 0 && S_ISDIR(status.st_mode));

    assert(unlink(path) == 0);
    assert(rmdir(directory) == 0);
    assert(wm_atomic_file_open(&file, path) == WM_ATOMIC_FILE_CREATE_FAILED);
    assert(!file.stream && !file.temporary_path);
    return 0;
}
