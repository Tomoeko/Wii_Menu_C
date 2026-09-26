#ifndef WII_MENU_RESOURCE_LAYOUT_H
#define WII_MENU_RESOURCE_LAYOUT_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef struct WmResourceTexture {
    const char *name;       /* TPL basename, including .tpl. */
    const char *url;        /* Path to locally generated RGBA resource. */
    uint16_t width;
    uint16_t height;
    uint32_t format;
    const char *source;    /* Optional original archive/resource path. */
} WmResourceTexture;

typedef struct WmResourceAnimation {
    const char *name;       /* BRLAN stem. */
    const uint8_t *data;
    size_t size;
} WmResourceAnimation;

/* Both functions return a newly allocated, NUL-terminated JSON document.
 * The caller frees it with free(). Input byte buffers and descriptors are
 * borrowed only until the call returns. */
bool wm_brlan_to_json(const uint8_t *data, size_t size,
                      char **json, size_t *json_size,
                      char *error, size_t error_size);

bool wm_brlyt_to_json(const uint8_t *data, size_t size,
                      const char *name, const char *package,
                      const WmResourceTexture *textures, size_t texture_count,
                      const WmResourceAnimation *animations, size_t animation_count,
                      char **json, size_t *json_size,
                      char *error, size_t error_size);

/* Adds the optional source provenance field used by prepared HTML assets. */
bool wm_brlyt_to_json_with_source(
    const uint8_t *data, size_t size,
    const char *name, const char *package, const char *source,
    const WmResourceTexture *textures, size_t texture_count,
    const WmResourceAnimation *animations, size_t animation_count,
    char **json, size_t *json_size,
    char *error, size_t error_size);

#endif
