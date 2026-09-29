#include "wii_menu/support/sha1.h"

#include <string.h>

static uint32_t rotate_word(uint32_t value, unsigned distance) {
    return (value << distance) | (value >> (32 - distance));
}

static uint32_t read_be32(const uint8_t *bytes) {
    return ((uint32_t)bytes[0] << 24) | ((uint32_t)bytes[1] << 16) |
           ((uint32_t)bytes[2] << 8) | bytes[3];
}

static void write_be32(uint8_t *bytes, uint32_t value) {
    bytes[0] = (uint8_t)(value >> 24);
    bytes[1] = (uint8_t)(value >> 16);
    bytes[2] = (uint8_t)(value >> 8);
    bytes[3] = (uint8_t)value;
}

static void sha1_compress(WmSha1 *sha1, const uint8_t block[64]) {
    uint32_t words[80];
    for (int index = 0; index < 16; ++index) {
        words[index] = read_be32(block + index * 4);
    }
    for (int index = 16; index < 80; ++index) {
        words[index] = rotate_word(words[index - 3] ^ words[index - 8] ^
                                       words[index - 14] ^ words[index - 16],
                                   1);
    }

    uint32_t a = sha1->words[0];
    uint32_t b = sha1->words[1];
    uint32_t c = sha1->words[2];
    uint32_t d = sha1->words[3];
    uint32_t e = sha1->words[4];
    for (int index = 0; index < 80; ++index) {
        uint32_t function;
        uint32_t constant;
        if (index < 20) {
            function = (b & c) | (~b & d);
            constant = 0x5a827999;
        } else if (index < 40) {
            function = b ^ c ^ d;
            constant = 0x6ed9eba1;
        } else if (index < 60) {
            function = (b & c) | (b & d) | (c & d);
            constant = 0x8f1bbcdc;
        } else {
            function = b ^ c ^ d;
            constant = 0xca62c1d6;
        }
        uint32_t next = rotate_word(a, 5) + function + e + constant + words[index];
        e = d;
        d = c;
        c = rotate_word(b, 30);
        b = a;
        a = next;
    }
    sha1->words[0] += a;
    sha1->words[1] += b;
    sha1->words[2] += c;
    sha1->words[3] += d;
    sha1->words[4] += e;
    memset(words, 0, sizeof(words));
}

void wm_sha1_init(WmSha1 *sha1) {
    sha1->words[0] = 0x67452301;
    sha1->words[1] = 0xefcdab89;
    sha1->words[2] = 0x98badcfe;
    sha1->words[3] = 0x10325476;
    sha1->words[4] = 0xc3d2e1f0;
    sha1->byte_count = 0;
    sha1->pending_count = 0;
}

void wm_sha1_update(WmSha1 *sha1, const uint8_t *data, size_t length) {
    sha1->byte_count += length;
    while (length > 0) {
        size_t available = sizeof(sha1->pending) - sha1->pending_count;
        size_t amount = length < available ? length : available;
        memcpy(sha1->pending + sha1->pending_count, data, amount);
        sha1->pending_count += amount;
        data += amount;
        length -= amount;
        if (sha1->pending_count == sizeof(sha1->pending)) {
            sha1_compress(sha1, sha1->pending);
            sha1->pending_count = 0;
        }
    }
}

void wm_sha1_final(WmSha1 *sha1, uint8_t digest[20]) {
    uint64_t bit_count = sha1->byte_count * 8;
    uint8_t one = 0x80;
    wm_sha1_update(sha1, &one, 1);
    uint8_t zero = 0;
    while (sha1->pending_count != 56) {
        wm_sha1_update(sha1, &zero, 1);
    }
    uint8_t length[8];
    for (int index = 0; index < 8; ++index) {
        length[7 - index] = (uint8_t)(bit_count >> (index * 8));
    }
    wm_sha1_update(sha1, length, sizeof(length));
    for (int index = 0; index < 5; ++index) {
        write_be32(digest + index * 4, sha1->words[index]);
    }
    memset(sha1, 0, sizeof(*sha1));
}
