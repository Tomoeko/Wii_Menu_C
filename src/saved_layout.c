#include "wii_menu/saved_layout.h"

#include <stdio.h>
#include <string.h>

static const uint32_t md5_constants[64] = {
    0xd76aa478, 0xe8c7b756, 0x242070db, 0xc1bdceee,
    0xf57c0faf, 0x4787c62a, 0xa8304613, 0xfd469501,
    0x698098d8, 0x8b44f7af, 0xffff5bb1, 0x895cd7be,
    0x6b901122, 0xfd987193, 0xa679438e, 0x49b40821,
    0xf61e2562, 0xc040b340, 0x265e5a51, 0xe9b6c7aa,
    0xd62f105d, 0x02441453, 0xd8a1e681, 0xe7d3fbc8,
    0x21e1cde6, 0xc33707d6, 0xf4d50d87, 0x455a14ed,
    0xa9e3e905, 0xfcefa3f8, 0x676f02d9, 0x8d2a4c8a,
    0xfffa3942, 0x8771f681, 0x6d9d6122, 0xfde5380c,
    0xa4beea44, 0x4bdecfa9, 0xf6bb4b60, 0xbebfbc70,
    0x289b7ec6, 0xeaa127fa, 0xd4ef3085, 0x04881d05,
    0xd9d4d039, 0xe6db99e5, 0x1fa27cf8, 0xc4ac5665,
    0xf4292244, 0x432aff97, 0xab9423a7, 0xfc93a039,
    0x655b59c3, 0x8f0ccc92, 0xffeff47d, 0x85845dd1,
    0x6fa87e4f, 0xfe2ce6e0, 0xa3014314, 0x4e0811a1,
    0xf7537e82, 0xbd3af235, 0x2ad7d2bb, 0xeb86d391
};

static const uint8_t md5_shifts[64] = {
    7, 12, 17, 22, 7, 12, 17, 22, 7, 12, 17, 22, 7, 12, 17, 22,
    5, 9, 14, 20, 5, 9, 14, 20, 5, 9, 14, 20, 5, 9, 14, 20,
    4, 11, 16, 23, 4, 11, 16, 23, 4, 11, 16, 23, 4, 11, 16, 23,
    6, 10, 15, 21, 6, 10, 15, 21, 6, 10, 15, 21, 6, 10, 15, 21
};

static uint32_t little_u32(const uint8_t *bytes) {
    return (uint32_t)bytes[0] | ((uint32_t)bytes[1] << 8) |
           ((uint32_t)bytes[2] << 16) | ((uint32_t)bytes[3] << 24);
}

static uint32_t big_u32(const uint8_t *bytes) {
    return ((uint32_t)bytes[0] << 24) | ((uint32_t)bytes[1] << 16) |
           ((uint32_t)bytes[2] << 8) | (uint32_t)bytes[3];
}

static uint32_t rotate_left(uint32_t value, unsigned bits) {
    return (value << bits) | (value >> (32 - bits));
}

static void md5_block(uint32_t state[4], const uint8_t block[64]) {
    uint32_t words[16];
    for (size_t index = 0; index < 16; index++) {
        words[index] = little_u32(block + index * 4);
    }
    uint32_t a = state[0], b = state[1], c = state[2], d = state[3];
    for (unsigned step = 0; step < 64; step++) {
        uint32_t choice;
        unsigned word;
        if (step < 16) {
            choice = (b & c) | (~b & d);
            word = step;
        } else if (step < 32) {
            choice = (d & b) | (~d & c);
            word = (5 * step + 1) & 15;
        } else if (step < 48) {
            choice = b ^ c ^ d;
            word = (3 * step + 5) & 15;
        } else {
            choice = c ^ (b | ~d);
            word = (7 * step) & 15;
        }
        uint32_t next = b + rotate_left(a + choice +
                                          md5_constants[step] + words[word],
                                          md5_shifts[step]);
        a = d;
        d = c;
        c = b;
        b = next;
    }
    state[0] += a;
    state[1] += b;
    state[2] += c;
    state[3] += d;
    memset(words, 0, sizeof(words));
}

static void md5_digest(const uint8_t *bytes, size_t size, uint8_t digest[16]) {
    uint32_t state[4] = {
        0x67452301, 0xefcdab89, 0x98badcfe, 0x10325476
    };
    size_t offset = 0;
    while (size - offset >= 64) {
        md5_block(state, bytes + offset);
        offset += 64;
    }
    uint8_t tail[128] = {0};
    size_t remaining = size - offset;
    memcpy(tail, bytes + offset, remaining);
    tail[remaining] = 0x80;
    size_t tail_size = remaining < 56 ? 64 : 128;
    uint64_t bit_length = (uint64_t)size * 8;
    for (size_t index = 0; index < 8; index++) {
        tail[tail_size - 8 + index] = (uint8_t)(bit_length >> (index * 8));
    }
    md5_block(state, tail);
    if (tail_size == 128) md5_block(state, tail + 64);
    for (size_t index = 0; index < 4; index++) {
        for (size_t byte = 0; byte < 4; byte++) {
            digest[index * 4 + byte] = (uint8_t)(state[index] >> (byte * 8));
        }
    }
    memset(tail, 0, sizeof(tail));
    memset(state, 0, sizeof(state));
}

static void set_error(char *error, size_t capacity, const char *message) {
    if (error && capacity) snprintf(error, capacity, "%s", message);
}

bool wm_saved_layout_parse(const uint8_t *bytes, size_t size,
                            WmSavedLayout *layout, char *error,
                            size_t error_capacity) {
    if (!bytes || !layout || size != WM_SAVED_LAYOUT_BYTES) {
        set_error(error, error_capacity, "Expected a 0x4c0-byte RIPL save");
        return false;
    }
    if (memcmp(bytes, "RIPL", 4) != 0 ||
        big_u32(bytes + 4) != WM_SAVED_LAYOUT_BYTES ||
        big_u32(bytes + 8) != 3) {
        set_error(error, error_capacity, "Unsupported RIPL save size/version");
        return false;
    }
    uint8_t checksum[16];
    md5_digest(bytes, size - sizeof(checksum), checksum);
    if (memcmp(checksum, bytes + size - sizeof(checksum),
               sizeof(checksum)) != 0) {
        set_error(error, error_capacity, "RIPL checksum mismatch");
        return false;
    }
    WmSavedLayout result = {0};
    result.previous_page = big_u32(bytes + 12);
    static const char hex[] = "0123456789abcdef";
    for (size_t index = 0; index < WM_SAVED_CHANNEL_SLOTS; index++) {
        const uint8_t *record = bytes + 0x10 + index * 16;
        WmSavedSlot *slot = &result.slots[index];
        slot->primary_type = record[0];
        slot->secondary_type = record[1];
        slot->scene_id = big_u32(record + 4);
        if (slot->primary_type == 1) {
            memcpy(slot->id, "disc", 5);
        } else if (slot->primary_type == 3) {
            for (size_t byte = 0; byte < 8; byte++) {
                uint8_t value = record[8 + byte];
                slot->id[byte * 2] = hex[value >> 4];
                slot->id[byte * 2 + 1] = hex[value & 15];
            }
            slot->id[16] = '\0';
        }
    }
    *layout = result;
    if (error && error_capacity) error[0] = '\0';
    return true;
}
