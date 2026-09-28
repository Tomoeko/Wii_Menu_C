#ifndef WM_SCENE_ASSETS_H
#define WM_SCENE_ASSETS_H

#include "wii_menu/layout/layout_runtime.h"

#include <stdbool.h>
#include <stddef.h>

enum { WM_SCENE_ASSET_PATH_CAPACITY = 4096 };

/* Join a trusted asset root with a lexical relative path. Reject absolute,
 * traversing, and malformed paths. This does not resolve symlinks. */
bool wm_scene_asset_path(char *path, size_t capacity, const char *root,
                         const char *relative);

/* A NULL scene_name keeps failures silent. The named scenes report JSON load
 * failures using their existing "Could not load ... layout" diagnostic. */
WmLayout *wm_scene_load_layout(const char *root, const char *relative,
                               const char *scene_name);

#endif
