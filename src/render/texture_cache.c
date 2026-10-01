#include "wii_menu/render/texture_cache.h"
#include "wii_menu/layout/layout_runtime.h"

bool wm_texture_cache_layout_image(void *context, const WmLayoutTexture *resource,
                                   uint32_t *handle) {
    if (handle) {
        *handle = 0;
    }
    if (!resource || resource->missing) {
        return false;
    }
    return wm_texture_cache_resolve(context, resource->url, handle);
}
