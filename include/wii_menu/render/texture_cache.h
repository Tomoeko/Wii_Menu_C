#include "console_common/compat/wii.h"
#ifndef WII_MENU_TEXTURE_CACHE_ADAPTER_H
#define WII_MENU_TEXTURE_CACHE_ADAPTER_H

#include "console_common/render/texture_cache.h"

typedef struct WmLayoutTexture WmLayoutTexture;

/* Adapter for Wii layout image providers; the cache remains shared. */
bool wm_texture_cache_layout_image(void *context, const WmLayoutTexture *resource,
                                   uint32_t *handle);

#endif
