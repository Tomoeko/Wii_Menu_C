#ifndef WII_MENU_WAD_RETAIL_KEYS_H
#define WII_MENU_WAD_RETAIL_KEYS_H

#include <stdbool.h>
#include <stdint.h>

/* Resolve only recognized retail ticket indices. Console-specific keys are
 * not part of this local preparation default. */
bool wm_wad_retail_common_key(unsigned index, uint8_t key[16]);

#endif
