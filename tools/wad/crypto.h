#ifndef WII_MENU_WAD_CRYPTO_H
#define WII_MENU_WAD_CRYPTO_H

#include <stddef.h>
#include <stdint.h>

#include "wii_menu/support/sha1.h"

typedef struct WmAes128 {
    uint8_t round_keys[176];
    uint8_t inverse_sbox[256];
    uint8_t multiply_9[256];
    uint8_t multiply_11[256];
    uint8_t multiply_13[256];
    uint8_t multiply_14[256];
} WmAes128;

void wm_aes128_init(WmAes128 *aes, const uint8_t key[16]);
void wm_aes128_decrypt_block(const WmAes128 *aes, const uint8_t input[16],
                             uint8_t output[16]);
void wm_aes128_cbc_decrypt(WmAes128 *aes, uint8_t *data, size_t length,
                            const uint8_t initial_vector[16]);

#endif
