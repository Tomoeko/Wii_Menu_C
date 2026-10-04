#ifndef WM_TEST_DIRECTORY_H
#define WM_TEST_DIRECTORY_H

#include "console_common/support/directory.h"

#include <stdbool.h>
#include <stdio.h>
#include <string.h>

#ifdef _WIN32
#include <windows.h>
#include <direct.h>

static bool wm_test_directory(char *path, size_t capacity) {
    size_t length = strlen(path);
    if (length < 6 || capacity <= length || strcmp(path + length - 6, "XXXXXX"))
        return false;
    LARGE_INTEGER counter;
    if (!QueryPerformanceCounter(&counter))
        return false;
    for (unsigned attempt = 0; attempt < 256; ++attempt) {
        unsigned suffix =
            ((unsigned)counter.QuadPart + GetCurrentProcessId() + attempt) & 0xffffff;
        snprintf(path + length - 6, 7, "%06x", suffix);
        if (CreateDirectoryA(path, NULL))
            return true;
        if (GetLastError() != ERROR_ALREADY_EXISTS)
            return false;
    }
    return false;
}

#define wm_test_remove_directory(path) _rmdir(path)
#else
#include <stdlib.h>
#include <sys/stat.h>
#include <unistd.h>

static bool wm_test_directory(char *path, size_t capacity) {
    if (capacity <= strlen(path))
        return false;
    int descriptor = mkstemp(path);
    if (descriptor < 0)
        return false;
    if (close(descriptor) != 0 || unlink(path) != 0)
        return false;
    return mkdir(path, 0700) == 0;
}

#define wm_test_remove_directory(path) rmdir(path)
#endif

#endif
