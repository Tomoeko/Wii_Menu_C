#ifndef WII_MENU_LOCAL_CATALOG_H
#define WII_MENU_LOCAL_CATALOG_H

#include <stdbool.h>
#include <stddef.h>

enum { WM_LOCAL_CHANNEL_LIMIT = 48, WM_LOCAL_HIDDEN_LIMIT = 256 };

typedef struct WmLocalChannel {
    char id[65];
    char title[128];
    bool imported;
    bool removed;
} WmLocalChannel;

typedef struct WmLocalCatalog {
    WmLocalChannel channels[WM_LOCAL_CHANNEL_LIMIT];
    size_t channel_count;
    char hidden[WM_LOCAL_HIDDEN_LIMIT][65];
    size_t hidden_count;
} WmLocalCatalog;

bool wm_local_channel_id_valid(const char *id);
bool wm_local_catalog_load(const char *assets, WmLocalCatalog *catalog);
bool wm_local_catalog_save(const char *assets, const WmLocalCatalog *catalog);

#endif
