#ifndef WII_MENU_FONT_CACHE_H
#define WII_MENU_FONT_CACHE_H

#include "wii_menu/platform.h"
#include "wii_menu/resource_font.h"
#include "wii_menu/layout_runtime.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef struct WmFontCache WmFontCache;
typedef struct WmCachedFont WmCachedFont;

typedef struct WmFontCacheStats {
    size_t loaded_fonts;
    size_t resident_sheets;
    size_t resident_bytes;
    size_t cached_layouts;
    uint64_t evictions;
} WmFontCacheStats;

/* Fonts are read from assets_root/fonts. gpu_budget_bytes bounds uploaded
 * sheet memory. Destroy the cache after the last frame using its textures. */
WmFontCache *wm_font_cache_create(WmPlatform *platform, const char *assets_root,
                                   size_t gpu_budget_bytes);
void wm_font_cache_destroy(WmFontCache *cache);
void wm_font_cache_begin_frame(WmFontCache *cache);

/* Missing layout fonts use the system menu's exported default BRFNT when it
 * exists. Returned face pointers remain valid until cache destruction. */
WmCachedFont *wm_font_cache_resolve(WmFontCache *cache, const char *font_name);
const WmFont *wm_cached_font_resource(const WmCachedFont *face);

/* Geometry is reused across color and alpha animation. Text and the other
 * WmFontPane fields are compared by value, not by caller pointer identity.
 * The cached layout uses white colors; presentation supplies the current tint. */
const WmFontTextLayout *wm_font_cache_layout(WmCachedFont *face,
                                             const char *utf8,
                                             const WmFontPane *pane);

/* WmFontSheetProvider adapter. Pass WmCachedFont as callback context. */
bool wm_font_cache_sheet(void *face, size_t sheet, uint32_t *texture);
WmFontCacheStats wm_font_cache_stats(const WmFontCache *cache);

/* Channel animation's source-font measurement callback. Length bounds one
 * line of a multi-line pane and need not end at a NUL terminator. */
float wm_font_cache_measure_text(void *context, const WmLayout *layout,
                                  const WmLayoutPaneState *pane,
                                  const char *utf8, size_t length);

#endif
