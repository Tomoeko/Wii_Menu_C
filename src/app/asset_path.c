#define _XOPEN_SOURCE 700
#define _POSIX_C_SOURCE 200809L

#include "asset_path.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

static bool search_parents(char *directory, char *result, size_t result_size) {
    static const char suffix[] = ".local/native-assets";
    for (;;) {
        char candidate[WM_APP_ASSET_PATH_CAPACITY];
        const char *separator = strcmp(directory, "/") == 0 ? "" : "/";
        int length = snprintf(candidate, sizeof(candidate), "%s%s%s", directory,
                              separator, suffix);
        if (length > 0 && (size_t)length < sizeof(candidate) &&
            (size_t)length < result_size) {
            struct stat status;
            if (stat(candidate, &status) == 0 && S_ISDIR(status.st_mode)) {
                memcpy(result, candidate, (size_t)length + 1);
                return true;
            }
        }
        char *slash = strrchr(directory, '/');
        if (!slash || strcmp(directory, "/") == 0)
            return false;
        if (slash == directory)
            slash[1] = '\0';
        else
            *slash = '\0';
    }
}

char *wm_app_resolve_executable(const char *executable) {
    if (!executable || !executable[0])
        return NULL;
    if (strchr(executable, '/'))
        return realpath(executable, NULL);

    /* argv[0] may be a bare command name when launched through PATH. */
    const char *entry = getenv("PATH");
    size_t name_length = strlen(executable);
    while (entry) {
        const char *end = strchr(entry, ':');
        size_t length = end ? (size_t)(end - entry) : strlen(entry);
        char candidate[WM_APP_ASSET_PATH_CAPACITY];
        if (length < sizeof(candidate) &&
            name_length < sizeof(candidate) - length - 1) {
            memcpy(candidate, entry, length);
            size_t offset = length;
            if (length)
                candidate[offset++] = '/';
            memcpy(candidate + offset, executable, name_length + 1);
            struct stat status;
            if (stat(candidate, &status) == 0 && S_ISREG(status.st_mode) &&
                access(candidate, X_OK) == 0) {
                char *resolved = realpath(candidate, NULL);
                if (resolved)
                    return resolved;
            }
        }
        entry = end ? end + 1 : NULL;
    }
    return NULL;
}

bool wm_app_find_default_assets(const char *executable, char *result,
                                size_t result_size) {
    if (!result || result_size == 0)
        return false;
    result[0] = '\0';
    char directory[WM_APP_ASSET_PATH_CAPACITY];
    if (getcwd(directory, sizeof(directory)) &&
        search_parents(directory, result, result_size))
        return true;

    char *resolved = wm_app_resolve_executable(executable);
    if (!resolved)
        return false;
    bool found = false;
    size_t length = strlen(resolved);
    if (length < sizeof(directory)) {
        memcpy(directory, resolved, length + 1);
        char *slash = strrchr(directory, '/');
        if (slash) {
            if (slash == directory)
                slash[1] = '\0';
            else
                *slash = '\0';
            found = search_parents(directory, result, result_size);
        }
    }
    free(resolved);
    return found;
}
