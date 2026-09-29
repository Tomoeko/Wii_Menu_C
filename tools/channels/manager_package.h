#ifndef WM_CHANNELS_MANAGER_PACKAGE_H
#define WM_CHANNELS_MANAGER_PACKAGE_H

#include <stdbool.h>

typedef struct WmChannelPackage {
    char id[65];
    char title[128];
    char directory[4096];
    bool has_audio;
} WmChannelPackage;

bool wm_channels_join(char output[4096], const char *root, const char *leaf);
bool wm_channels_regular_file(const char *path);
bool wm_channels_directory(const char *path);
bool wm_channels_package_read(const char *folder, WmChannelPackage *package);
bool wm_channels_texture_filename(const char *name);
bool wm_channels_package_validate(const WmChannelPackage *package);
bool wm_channels_package_directory(char path[4096], const char *assets, const char *id);
bool wm_channels_package_install(const char *assets, const WmChannelPackage *package);
/* Returns 1 for an open slot, 0 when full, and -1 for invalid catalog data. */
int wm_channels_open_slot_status(const char *assets);
bool wm_channels_installed_ready(const char *assets, const char *id, bool imported);

#endif
