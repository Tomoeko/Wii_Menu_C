#include "crypto.h"

#include <string.h>

static uint8_t gf_multiply(uint8_t left, uint8_t right) {
    uint8_t result = 0;
    for (int bit = 0; bit < 8; ++bit) {
        if (right & 1) {
            result ^= left;
        }
        left = (uint8_t)((left << 1) ^ ((left & 0x80) ? 0x1b : 0));
        right >>= 1;
    }
    return result;
}

static uint8_t gf_inverse(uint8_t value) {
    if (value == 0) {
        return 0;
    }
    uint8_t result = 1;
    uint8_t factor = value;
    unsigned exponent = 254;
    while (exponent) {
        if (exponent & 1) {
            result = gf_multiply(result, factor);
        }
        factor = gf_multiply(factor, factor);
        exponent >>= 1;
    }
    return result;
}

static uint8_t rotate_byte(uint8_t value, unsigned distance) {
    return (uint8_t)((value << distance) | (value >> (8 - distance)));
}

void wm_aes128_init(WmAes128 *aes, const uint8_t key[16]) {
    uint8_t sbox[256];
    for (unsigned value = 0; value < 256; ++value) {
        uint8_t inverse = gf_inverse((uint8_t)value);
        uint8_t substituted = inverse ^ rotate_byte(inverse, 1) ^
                              rotate_byte(inverse, 2) ^ rotate_byte(inverse, 3) ^
                              rotate_byte(inverse, 4) ^ 0x63;
        sbox[value] = substituted;
        aes->inverse_sbox[substituted] = (uint8_t)value;
        aes->multiply_9[value] = gf_multiply((uint8_t)value, 9);
        aes->multiply_11[value] = gf_multiply((uint8_t)value, 11);
        aes->multiply_13[value] = gf_multiply((uint8_t)value, 13);
        aes->multiply_14[value] = gf_multiply((uint8_t)value, 14);
    }

    memcpy(aes->round_keys, key, 16);
    uint8_t round_constant = 1;
    for (size_t offset = 16; offset < sizeof(aes->round_keys); offset += 4) {
        uint8_t word[4];
        memcpy(word, aes->round_keys + offset - 4, sizeof(word));
        if (offset % 16 == 0) {
            uint8_t first = word[0];
            word[0] = sbox[word[1]] ^ round_constant;
            word[1] = sbox[word[2]];
            word[2] = sbox[word[3]];
            word[3] = sbox[first];
            round_constant = gf_multiply(round_constant, 2);
        }
        for (int byte = 0; byte < 4; ++byte) {
            aes->round_keys[offset + byte] =
                aes->round_keys[offset - 16 + byte] ^ word[byte];
        }
    }
    memset(sbox, 0, sizeof(sbox));
}

static void aes_add_round_key(uint8_t state[16], const uint8_t *round_key) {
    for (int index = 0; index < 16; ++index) {
        state[index] ^= round_key[index];
    }
}

static void aes_inverse_shift_rows(uint8_t state[16]) {
    uint8_t previous[16];
    memcpy(previous, state, sizeof(previous));
    for (int row = 0; row < 4; ++row) {
        for (int column = 0; column < 4; ++column) {
            state[row + 4 * column] = previous[row + 4 * ((column - row + 4) % 4)];
        }
    }
}

static void aes_inverse_mix_columns(const WmAes128 *aes, uint8_t state[16]) {
    for (int column = 0; column < 4; ++column) {
        uint8_t *values = state + column * 4;
        uint8_t first = values[0];
        uint8_t second = values[1];
        uint8_t third = values[2];
        uint8_t fourth = values[3];
        values[0] = aes->multiply_14[first] ^ aes->multiply_11[second] ^
                    aes->multiply_13[third] ^ aes->multiply_9[fourth];
        values[1] = aes->multiply_9[first] ^ aes->multiply_14[second] ^
                    aes->multiply_11[third] ^ aes->multiply_13[fourth];
        values[2] = aes->multiply_13[first] ^ aes->multiply_9[second] ^
                    aes->multiply_14[third] ^ aes->multiply_11[fourth];
        values[3] = aes->multiply_11[first] ^ aes->multiply_13[second] ^
                    aes->multiply_9[third] ^ aes->multiply_14[fourth];
    }
}

void wm_aes128_decrypt_block(const WmAes128 *aes, const uint8_t input[16],
                             uint8_t output[16]) {
    uint8_t state[16];
    memcpy(state, input, sizeof(state));
    aes_add_round_key(state, aes->round_keys + 160);
    for (int round = 9; round >= 1; --round) {
        aes_inverse_shift_rows(state);
        for (int index = 0; index < 16; ++index) {
            state[index] = aes->inverse_sbox[state[index]];
        }
        aes_add_round_key(state, aes->round_keys + round * 16);
        aes_inverse_mix_columns(aes, state);
    }
    aes_inverse_shift_rows(state);
    for (int index = 0; index < 16; ++index) {
        state[index] = aes->inverse_sbox[state[index]];
    }
    aes_add_round_key(state, aes->round_keys);
    memcpy(output, state, sizeof(state));
    memset(state, 0, sizeof(state));
}

void wm_aes128_cbc_decrypt(WmAes128 *aes, uint8_t *data, size_t length,
                           const uint8_t initial_vector[16]) {
    uint8_t vector[16];
    memcpy(vector, initial_vector, sizeof(vector));
    for (size_t offset = 0; offset < length; offset += 16) {
        uint8_t ciphertext[16];
        uint8_t plaintext[16];
        memcpy(ciphertext, data + offset, sizeof(ciphertext));
        wm_aes128_decrypt_block(aes, ciphertext, plaintext);
        for (int index = 0; index < 16; ++index) {
            data[offset + index] = plaintext[index] ^ vector[index];
        }
        memcpy(vector, ciphertext, sizeof(vector));
        memset(plaintext, 0, sizeof(plaintext));
    }
    memset(vector, 0, sizeof(vector));
}
