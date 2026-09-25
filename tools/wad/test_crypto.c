#include "crypto.h"

#include <stdio.h>
#include <string.h>

int main(void)
{
    const uint8_t key[16] = {
        0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07,
        0x08, 0x09, 0x0a, 0x0b, 0x0c, 0x0d, 0x0e, 0x0f,
    };
    const uint8_t ciphertext[16] = {
        0x69, 0xc4, 0xe0, 0xd8, 0x6a, 0x7b, 0x04, 0x30,
        0xd8, 0xcd, 0xb7, 0x80, 0x70, 0xb4, 0xc5, 0x5a,
    };
    const uint8_t plaintext[16] = {
        0x00, 0x11, 0x22, 0x33, 0x44, 0x55, 0x66, 0x77,
        0x88, 0x99, 0xaa, 0xbb, 0xcc, 0xdd, 0xee, 0xff,
    };
    const uint8_t abc_digest[20] = {
        0xa9, 0x99, 0x3e, 0x36, 0x47, 0x06, 0x81, 0x6a,
        0xba, 0x3e, 0x25, 0x71, 0x78, 0x50, 0xc2, 0x6c,
        0x9c, 0xd0, 0xd8, 0x9d,
    };

    WmAes128 aes;
    uint8_t decoded[16];
    wm_aes128_init(&aes, key);
    wm_aes128_decrypt_block(&aes, ciphertext, decoded);
    if (memcmp(decoded, plaintext, sizeof(decoded)) != 0) {
        fputs("AES-128 known vector failed.\n", stderr);
        return 1;
    }

    WmSha1 sha1;
    uint8_t digest[20];
    wm_sha1_init(&sha1);
    wm_sha1_update(&sha1, (const uint8_t *)"abc", 3);
    wm_sha1_final(&sha1, digest);
    if (memcmp(digest, abc_digest, sizeof(digest)) != 0) {
        fputs("SHA-1 known vector failed.\n", stderr);
        return 1;
    }

    puts("AES-128 and SHA-1 known vectors passed.");
    return 0;
}
