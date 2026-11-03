#ifndef WII_MENU_CHANNEL_LAYOUT_STORE_H
#define WII_MENU_CHANNEL_LAYOUT_STORE_H

#include "wii_menu/menu/menu.h"

#include <stddef.h>

typedef struct WmChannelLayoutStore WmChannelLayoutStore;

typedef enum WmChannelLayoutStatus {
    WM_CHANNEL_LAYOUT_MISSING,
    WM_CHANNEL_LAYOUT_LOADED,
    WM_CHANNEL_LAYOUT_INVALID,
    WM_CHANNEL_LAYOUT_ERROR
} WmChannelLayoutStatus;

/* The menu starts with catalog defaults. Apply a valid config atomically;
 * invalid configs leave those defaults intact and return an editable store.
 * The store owns its path and snapshots, and does not retain the menu. */
WmChannelLayoutStore *wm_channel_layout_store_open(const char *path, WmMenu *menu,
                                                   WmChannelLayoutStatus *status,
                                                   char *error, size_t error_capacity);
/* No write when positions match startup (or the last successful save). Refuse
 * to overwrite a config edited externally since open/the last save. */
bool wm_channel_layout_store_save(WmChannelLayoutStore *store, const WmMenu *menu,
                                  char *error, size_t error_capacity);
void wm_channel_layout_store_destroy(WmChannelLayoutStore *store);

#endif
