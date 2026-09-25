#include "md5.h"

#include <math.h>
#include <stdbool.h>
#include <string.h>

static uint32_t wm_left_rotate(uint32_t value, unsigned shift)
{
    return (value << shift) | (value >> (32 - shift));
}

static uint32_t wm_little32(const uint8_t *bytes)
{
    return (uint32_t)bytes[0] | ((uint32_t)bytes[1] << 8) |
           ((uint32_t)bytes[2] << 16) | ((uint32_t)bytes[3] << 24);
}

static void wm_store32(uint8_t *bytes, uint32_t value)
{
    for (unsigned index = 0; index < 4; ++index) {
        bytes[index] = (uint8_t)(value >> (index * 8));
    }
}

static void wm_md5_block(WmMd5 *md5, const uint8_t bytes[64])
{
    static const unsigned rotations[64] = {
        7,12,17,22, 7,12,17,22, 7,12,17,22, 7,12,17,22,
        5, 9,14,20, 5, 9,14,20, 5, 9,14,20, 5, 9,14,20,
        4,11,16,23, 4,11,16,23, 4,11,16,23, 4,11,16,23,
        6,10,15,21, 6,10,15,21, 6,10,15,21, 6,10,15,21
    };
    static uint32_t constants[64];
    static bool ready;
    if (!ready) {
        for (unsigned index = 0; index < 64; ++index) {
            constants[index] = (uint32_t)(fabs(sin((double)(index + 1))) *
                                          4294967296.0);
        }
        ready = true;
    }
    uint32_t words[16];
    for (unsigned index = 0; index < 16; ++index) {
        words[index] = wm_little32(bytes + index * 4);
    }
    uint32_t a = md5->state[0];
    uint32_t b = md5->state[1];
    uint32_t c = md5->state[2];
    uint32_t d = md5->state[3];
    for (unsigned index = 0; index < 64; ++index) {
        uint32_t function;
        unsigned word;
        if (index < 16) {
            function = (b & c) | (~b & d);
            word = index;
        } else if (index < 32) {
            function = (d & b) | (~d & c);
            word = (5 * index + 1) & 15;
        } else if (index < 48) {
            function = b ^ c ^ d;
            word = (3 * index + 5) & 15;
        } else {
            function = c ^ (b | ~d);
            word = (7 * index) & 15;
        }
        uint32_t next = b + wm_left_rotate(a + function +
                         constants[index] + words[word], rotations[index]);
        a = d;
        d = c;
        c = b;
        b = next;
    }
    md5->state[0] += a;
    md5->state[1] += b;
    md5->state[2] += c;
    md5->state[3] += d;
}

void wm_md5_init(WmMd5 *md5)
{
    *md5 = (WmMd5){ .state = {
        UINT32_C(0x67452301), UINT32_C(0xefcdab89),
        UINT32_C(0x98badcfe), UINT32_C(0x10325476)
    }};
}

void wm_md5_update(WmMd5 *md5, const uint8_t *data, size_t size)
{
    md5->length += size;
    while (size != 0) {
        size_t amount = 64 - md5->pending_count;
        if (amount > size) amount = size;
        memcpy(md5->pending + md5->pending_count, data, amount);
        md5->pending_count += amount;
        data += amount;
        size -= amount;
        if (md5->pending_count == 64) {
            wm_md5_block(md5, md5->pending);
            md5->pending_count = 0;
        }
    }
}

void wm_md5_final(WmMd5 *md5, uint8_t digest[16])
{
    uint64_t bit_length = md5->length * 8;
    static const uint8_t marker = 0x80;
    static const uint8_t zeros[64] = {0};
    wm_md5_update(md5, &marker, 1);
    size_t padding = md5->pending_count <= 56
        ? 56 - md5->pending_count : 120 - md5->pending_count;
    wm_md5_update(md5, zeros, padding);
    uint8_t length[8];
    for (unsigned index = 0; index < 8; ++index) {
        length[index] = (uint8_t)(bit_length >> (index * 8));
    }
    wm_md5_update(md5, length, sizeof(length));
    for (unsigned index = 0; index < 4; ++index) {
        wm_store32(digest + index * 4, md5->state[index]);
    }
}
