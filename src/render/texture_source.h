#ifndef WM_RENDER_TEXTURE_SOURCE_H
#define WM_RENDER_TEXTURE_SOURCE_H

#include <stdbool.h>
#include <stddef.h>

/* raw_root must be canonical. Returned paths are allocated and remain inside it. */
bool wm_texture_source_url_valid(const char *url, size_t *length);
char *wm_texture_source_resolve(const char *raw_root, const char *url,
                                size_t url_length);
bool wm_texture_source_declared_bytes(const char *path, size_t *bytes);

#endif
