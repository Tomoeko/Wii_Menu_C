#include "scene_assets.h"

#include <limits.h>
#include <stdio.h>

enum { WM_SCENE_LAYOUT_ERROR_CAPACITY = 160 };

static bool valid_relative_path(const char *relative) {
    if (relative == NULL || relative[0] == '\0') {
        return false;
    }

    const char *component = relative;
    for (const unsigned char *at = (const unsigned char *)relative;; at++) {
        unsigned char character = *at;
        if (character == '/' || character == '\0') {
            size_t length = (size_t)((const char *)at - component);
            if (length == 0 || (length == 1 && component[0] == '.') ||
                (length == 2 && component[0] == '.' && component[1] == '.')) {
                return false;
            }
            if (character == '\0') {
                return true;
            }
            component = (const char *)at + 1;
        } else if (character == '\\' || character == ':' || character < 0x20 ||
                   character == 0x7f) {
            return false;
        }
    }
}

bool wm_scene_asset_path(char *path, size_t capacity, const char *root,
                         const char *relative) {
    if (path == NULL || capacity == 0 || capacity > INT_MAX || root == NULL ||
        root[0] == '\0' || !valid_relative_path(relative)) {
        return false;
    }

    int length = snprintf(path, capacity, "%s/%s", root, relative);
    return length >= 0 && (size_t)length < capacity;
}

WmLayout *wm_scene_load_layout(const char *root, const char *relative,
                               const char *scene_name) {
    char path[WM_SCENE_ASSET_PATH_CAPACITY];
    if (!wm_scene_asset_path(path, sizeof(path), root, relative)) {
        return NULL;
    }

    char error[WM_SCENE_LAYOUT_ERROR_CAPACITY] = {0};
    WmLayout *layout = wm_layout_load_json(path, error, sizeof(error));
    if (layout == NULL && scene_name != NULL) {
        fprintf(stderr, "Could not load %s layout %s: %s\n", scene_name, relative,
                error);
    }
    return layout;
}
