#include "wii_menu/menu/language.h"

bool wm_language_valid(WmLanguage language) {
    return language >= WM_LANGUAGE_ENGLISH && language < WM_LANGUAGE_COUNT;
}

const char *wm_language_code(WmLanguage language) {
    static const char *const codes[] = {"ENG", "FRA", "SPA"};
    return codes[wm_language_valid(language) ? language : WM_LANGUAGE_ENGLISH];
}
