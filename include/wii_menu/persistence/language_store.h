#ifndef WII_MENU_LANGUAGE_STORE_H
#define WII_MENU_LANGUAGE_STORE_H

#include "wii_menu/menu/language.h"
#include <stddef.h>

typedef struct WmLanguageStore WmLanguageStore;

/* Missing files select English without creating a file. Invalid JSON also
 * selects English and reports a warning; an explicit change may replace it.
 * The store owns its path and the last observed file contents. */
WmLanguageStore *wm_language_store_open(const char *path, WmLanguage *language,
                                        char *error, size_t error_capacity);
/* Save only a changed, confirmed choice. Atomic replacement; refuse to
 * overwrite a file edited externally since open or the last successful save. */
bool wm_language_store_save(WmLanguageStore *store, WmLanguage language, char *error,
                            size_t error_capacity);
void wm_language_store_destroy(WmLanguageStore *store);

#endif
