#ifndef WM_APP_CORRUPTION_OUTLINE_H
#define WM_APP_CORRUPTION_OUTLINE_H

#include "wii_menu/platform/platform.h"

#include <stdbool.h>
#include <stdint.h>

enum { WM_CORRUPTION_OUTLINE_LINES = 3 };

typedef struct WmCorruptionOutline {
    uint32_t textures[WM_CORRUPTION_OUTLINE_LINES];
    bool use_font_weight;
} WmCorruptionOutline;

bool wm_corruption_outline_create(WmCorruptionOutline *outline,
                                  WmPlatform *platform,
                                  const char *assets_root,
                                  const char *message);
void wm_corruption_outline_draw(const WmCorruptionOutline *outline,
                                WmPlatform *platform);
void wm_corruption_outline_destroy(WmCorruptionOutline *outline,
                                   WmPlatform *platform);

#endif
