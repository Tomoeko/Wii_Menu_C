#ifndef WM_RENDER_TEXTURE_SOURCE_H
#define WM_RENDER_TEXTURE_SOURCE_H

#include <stdbool.h>
#include <stddef.h>
#include <stdio.h>

bool wm_texture_source_url_valid(const char *url, size_t *length);

/* Opens a regular .wmra file below an already-open asset directory. Every
 * relative component is opened without following symlinks. The caller owns
 * the returned stream and must close it. */
FILE *wm_texture_source_open(int root_directory, const char *url,
                             size_t url_length);

#endif
