#ifndef WII_MENU_SAVED_LAYOUT_H
#define WII_MENU_SAVED_LAYOUT_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

enum { WM_SAVED_CHANNEL_SLOTS = 48, WM_SAVED_LAYOUT_BYTES = 0x4c0 };

typedef struct WmSavedSlot {
    char id[17]; /* "disc", a 16-digit title ID, or empty. */
    uint8_t primary_type;
    uint8_t secondary_type;
    uint32_t scene_id;
} WmSavedSlot;

typedef struct WmSavedLayout {
    uint32_t previous_page;
    WmSavedSlot slots[WM_SAVED_CHANNEL_SLOTS];
} WmSavedLayout;

/* Parse savedata::Manager RIPL v3 placement. The source's trailing MD5 is
 * checked before any slot is exposed. This is a data checksum, not a signature. */
bool wm_saved_layout_parse(const uint8_t *bytes, size_t size,
                            WmSavedLayout *layout, char *error,
                            size_t error_capacity);

#endif
