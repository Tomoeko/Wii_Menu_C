#ifndef WM_LAYOUT_ASSETS_H
#define WM_LAYOUT_ASSETS_H

#include "wii_menu/layout/layout_runtime.h"

#include <stdbool.h>
#include <stddef.h>

enum { WM_LAYOUT_ASSET_PATH_CAPACITY = 4096 };

/* Join a trusted asset root with a lexical relative path. Reject absolute,
 * traversing, and malformed paths. This does not resolve symlinks. */
bool wm_layout_asset_path(char *path, size_t capacity, const char *root,
                          const char *relative);

/* A NULL owner_name keeps failures silent. Named callers report JSON load
 * failures using their existing "Could not load ... layout" diagnostic. */
WmLayout *wm_layout_load_asset(const char *root, const char *relative,
                               const char *owner_name);

#endif
