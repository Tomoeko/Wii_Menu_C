#ifndef WM_APP_PACKED_ASSETS_H
#define WM_APP_PACKED_ASSETS_H

#include <stdbool.h>
#include <stddef.h>

typedef struct WmPackedAssets {
    int argc;
    char **argv;
    char package_path[4096];
    char temporary_root[4096];
    char assets_path[4096];
} WmPackedAssets;

bool wm_packed_assets_open(WmPackedAssets *assets, int argc, char **argv,
                           char *error, size_t error_capacity);
void wm_packed_assets_close(WmPackedAssets *assets);

#endif
