#ifndef WII_MENU_SUPPORT_ASSET_MANIFEST_H
#define WII_MENU_SUPPORT_ASSET_MANIFEST_H

#include <stdbool.h>
#include <stdio.h>

#define WM_ASSET_MANIFEST_NAME "asset-manifest.sha1"

/* Only prepared, non-hidden files are sealed. Runtime state uses hidden
 * filenames and is deliberately outside this immutable asset inventory. */
bool wm_asset_manifest_write(const char *assets_root, FILE *diagnostics);
bool wm_asset_manifest_verify(const char *assets_root, FILE *diagnostics,
                              unsigned *issue_count);

#endif
