#ifndef WM_CHANNELS_MANAGER_DELETE_H
#define WM_CHANNELS_MANAGER_DELETE_H

#include "wii_menu/menu/local_catalog.h"

int wm_channels_purge(const char *assets, WmLocalCatalog *local, const char *id);

#endif
