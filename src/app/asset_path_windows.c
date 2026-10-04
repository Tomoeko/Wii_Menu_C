#include "asset_path.h"
#include "console_common/support/directory.h"
#include "console_common/support/host.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static bool search_parents(char *directory, char *result, size_t capacity) {
    do {
        char candidate[WM_APP_ASSET_PATH_CAPACITY];
        int length = snprintf(candidate, sizeof(candidate),
                              "%s/Files/.local/native-assets", directory);
        if (length > 0 && (size_t)length < sizeof(candidate) &&
            (size_t)length < capacity &&
            cc_path_information(candidate, true, NULL) == CC_PATH_DIRECTORY) {
            memcpy(result, candidate, (size_t)length + 1);
            return true;
        }
        char *separator = strrchr(directory, '/');
        if (!separator)
            break;
        *separator = '\0';
    } while (*directory);
    return false;
}

char *wm_app_resolve_executable(const char *executable) {
    return executable && *executable ? cc_host_executable_path() : NULL;
}

bool wm_app_find_default_assets(const char *executable, char *result, size_t capacity) {
    if (!result || !capacity)
        return false;
    result[0] = '\0';
    char *current = cc_host_current_directory();
    bool found = current && search_parents(current, result, capacity);
    free(current);
    if (found)
        return true;
    char *path = wm_app_resolve_executable(executable);
    if (path) {
        char *separator = strrchr(path, '/');
        if (separator) {
            *separator = '\0';
            found = search_parents(path, result, capacity);
        }
    }
    free(path);
    return found;
}
