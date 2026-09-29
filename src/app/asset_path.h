#ifndef WM_APP_ASSET_PATH_H
#define WM_APP_ASSET_PATH_H

#include <stdbool.h>
#include <stddef.h>

enum { WM_APP_ASSET_PATH_CAPACITY = 4096 };

/* Search only .local/native-assets in the current directory and its parents,
 * then beside the resolved executable and its parents. The caller owns result;
 * failure leaves it empty. No working-directory changes are made. */
bool wm_app_find_default_assets(const char *executable, char *result,
                                size_t result_size);
/* Caller frees the resolved path. Searches PATH for bare executable names. */
char *wm_app_resolve_executable(const char *executable);

#endif
