#include "retail_keys.h"

#include <stddef.h>

/* Match the HTML preparation tool's public retail constants, checked there
 * against Dolphin IOSC::LoadDefaultEntries at revision
 * ee018d00e60b9eb727489908a8daec5c537f44a8. The XOR representation is cosmetic,
 * not encryption or a security boundary. These are not console credentials. */
static const uint8_t obscured_retail_keys[2][16] = {
    {
        0x4c, 0x43, 0x8d, 0x85, 0xf9, 0x22, 0x34, 0x43,
        0xef, 0x7e, 0x62, 0xe2, 0xd4, 0x26, 0x0d, 0x50
    },
    {
        0xc4, 0x1f, 0x8c, 0x13, 0x53, 0xc6, 0xe9, 0x89,
        0xb4, 0x55, 0x59, 0x5c, 0x1d, 0xeb, 0x3c, 0xd9
    }
};

bool wm_wad_retail_common_key(unsigned index, uint8_t key[16]) {
    if (!key || index >= 2) return false;
    for (size_t byte = 0; byte < 16; byte++) {
        key[byte] = obscured_retail_keys[index][byte] ^ 0xa7;
    }
    return true;
}
