#ifndef WII_MENU_RESOURCE_CHANS_H
#define WII_MENU_RESOURCE_CHANS_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

enum { WM_CHANS_MAX_TEXTS = 64, WM_CHANS_TEXT_BYTES = 1024 };

typedef struct WmChansText {
    char language[4];
    char pane[128];
    char text[WM_CHANS_TEXT_BYTES];
    float font_size[2];
} WmChansText;

typedef struct WmChansLanguages {
    WmChansText texts[WM_CHANS_MAX_TEXTS];
    size_t count;
} WmChansLanguages;

/* Recover the bounded setRegionPanes / setTitle language program found in
 * Get Connected's RCHE script. Other script services are not executed.
 * All output strings are owned by the result; malformed/unsupported programs
 * fail without publishing partial results. See docs/channel-language.md. */
bool wm_chans_languages_decode(const uint8_t *data, size_t size,
                               WmChansLanguages *result, char *error,
                               size_t error_capacity);

#endif
