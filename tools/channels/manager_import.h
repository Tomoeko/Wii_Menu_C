#ifndef WM_CHANNEL_MANAGER_IMPORT_H
#define WM_CHANNEL_MANAGER_IMPORT_H

#include "wii_menu/menu/local_catalog.h"

#include <stdbool.h>

bool wm_channels_import_wad(const char *program, const char *assets, const char *wad,
                            const char *key_file, const char *key_index,
                            WmLocalCatalog *local);

#endif
