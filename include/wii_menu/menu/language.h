#ifndef WII_MENU_LANGUAGE_H
#define WII_MENU_LANGUAGE_H

#include <stdbool.h>

typedef enum WmLanguage {
    WM_LANGUAGE_ENGLISH,
    WM_LANGUAGE_FRENCH,
    WM_LANGUAGE_SPANISH,
    WM_LANGUAGE_COUNT
} WmLanguage;

/* Languages offered by the recovered USA Settings document. */
bool wm_language_valid(WmLanguage language);
const char *wm_language_code(WmLanguage language);

#endif
