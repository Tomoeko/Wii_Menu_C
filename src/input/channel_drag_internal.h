#ifndef WM_CHANNEL_DRAG_INTERNAL_H
#define WM_CHANNEL_DRAG_INTERNAL_H

#include "wii_menu/input/channel_drag.h"

#include <stdint.h>

typedef struct WmChannelDragPresentation WmChannelDragPresentation;

/* Controller state is valid without a renderer. The optional presentation
 * owns all three layouts and borrows the platform and caches. */
struct WmChannelDrag {
    WmChannelDragLengths lengths;
    WmChannelDragState state;
    uint64_t revision;
    WmChannelDragPresentation *presentation;
};

/* Publishes authored lengths only after all three layouts load successfully. */
WmChannelDragPresentation *
wm_channel_drag_presentation_create(WmPlatform *platform, const char *assets_directory,
                                    WmTextureCache *textures, WmFontCache *fonts,
                                    bool wide, WmChannelDragLengths *lengths);
void wm_channel_drag_presentation_destroy(WmChannelDragPresentation *presentation);

#endif
