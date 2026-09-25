#ifndef WII_MENU_TEXTURE_CACHE_H
#define WII_MENU_TEXTURE_CACHE_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "wii_menu/platform.h"

typedef struct WmLayoutTexture WmLayoutTexture;
typedef struct WmTextureCache WmTextureCache;

typedef struct WmTextureCacheStats {
    size_t budget_bytes;
    size_t resident_bytes;
    size_t resident_textures;
    size_t failed_sources;
    size_t known_sources;
    uint64_t evictions;
} WmTextureCacheStats;

/* raw_root must be an existing directory containing prepared .wmra images.
 * Destroy the cache after the last platform_end for its submitted quads. */
WmTextureCache *wm_texture_cache_create(WmPlatform *platform,
                                         const char *raw_root,
                                         size_t budget_bytes);
void wm_texture_cache_destroy(WmTextureCache *cache);

/* Call once before each frame's draw submissions. A texture requested during
 * this frame cannot be evicted until the next begin_frame call. */
void wm_texture_cache_begin_frame(WmTextureCache *cache);

/* Accepts a relative prepared .png URL and loads its sibling .wmra file.
 * Returns false with handle zero when invalid, absent, or over budget. */
bool wm_texture_cache_resolve(WmTextureCache *cache,
                              const char *relative_png_url,
                              uint32_t *handle);

/* Direct adapter for WmLayoutDrawOptions.image_provider. Its context must be
 * the cache; resources marked missing resolve to the white fallback. */
bool wm_texture_cache_layout_image(void *context,
                                   const WmLayoutTexture *resource,
                                   uint32_t *handle);

/* Missing/corrupt files and failed uploads are not retried each frame. Call
 * after preparing assets again to retry those entries on demand. */
void wm_texture_cache_retry_failed(WmTextureCache *cache);
WmTextureCacheStats wm_texture_cache_stats(const WmTextureCache *cache);

#endif
