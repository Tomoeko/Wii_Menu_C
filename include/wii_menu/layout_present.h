#ifndef WII_MENU_LAYOUT_PRESENT_H
#define WII_MENU_LAYOUT_PRESENT_H

#include <stdbool.h>

#include "wii_menu/layout_runtime.h"
#include "wii_menu/font_cache.h"
#include "wii_menu/platform.h"
#include "wii_menu/texture_cache.h"

/* Present picture panes of a parsed layout in the 640 x 456 raster. Material
 * interpretation is delegated to the selected graphics backend. */
void wm_layout_present(WmPlatform *platform, WmTextureCache *textures,
                       const WmLayout *layout, bool wide, WmLayoutMode mode,
                       const float parent_matrix[12]);
void wm_layout_present_filtered(WmPlatform *platform, WmTextureCache *textures,
                                const WmLayout *layout, bool wide,
                                WmLayoutMode mode, const float parent_matrix[12],
                                WmLayoutPaneCallback filter, void *filter_context);

/* Optional font cache renders txt1 panes in traversal order with the same
 * decoded BRFNT glyphs on both backends. A NULL cache skips text. */
void wm_layout_present_with_fonts(WmPlatform *platform,
                                  WmTextureCache *textures, WmFontCache *fonts,
                                  const WmLayout *layout, bool wide,
                                  WmLayoutMode mode,
                                  const float parent_matrix[12]);
/* Applies opacity to the complete layout, including panes whose source flags
 * do not inherit their parent's alpha. */
void wm_layout_present_with_fonts_opacity(
    WmPlatform *platform, WmTextureCache *textures, WmFontCache *fonts,
    const WmLayout *layout, bool wide, WmLayoutMode mode,
    const float parent_matrix[12], float opacity);
/* Suppress individual pane drawing while retaining traversal of descendants.
 * A caller can redraw text under a clip without repainting its parent window. */
typedef bool (*WmLayoutDrawPredicate)(void *context, const char *pane_name);
void wm_layout_present_with_fonts_opacity_masked(
    WmPlatform *platform, WmTextureCache *textures, WmFontCache *fonts,
    const WmLayout *layout, bool wide, WmLayoutMode mode,
    const float parent_matrix[12], float opacity,
    WmLayoutDrawPredicate predicate, void *predicate_context);
void wm_layout_present_filtered_with_fonts(
    WmPlatform *platform, WmTextureCache *textures, WmFontCache *fonts,
    const WmLayout *layout, bool wide, WmLayoutMode mode,
    const float parent_matrix[12], WmLayoutPaneCallback filter,
    void *filter_context);

#endif
