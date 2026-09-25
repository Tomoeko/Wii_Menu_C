#include "png.h"

#include <stdlib.h>
#include <string.h>

enum {
    PNG_MAX_DIMENSION = 4096,
    PNG_MAX_COMPRESSED = 32 * 1024 * 1024,
    PNG_MAX_DECOMPRESSED = 64 * 1024 * 1024,
    DEFLATE_TABLE_BITS = 15,
    DEFLATE_TABLE_SIZE = 1 << DEFLATE_TABLE_BITS
};

typedef struct BitStream {
    const uint8_t *data;
    size_t size;
    size_t bit;
} BitStream;

typedef struct HuffmanEntry {
    uint16_t symbol;
    uint8_t length;
} HuffmanEntry;

typedef struct Huffman {
    HuffmanEntry *entries;
} Huffman;

static uint32_t be32(const uint8_t *bytes) {
    return ((uint32_t)bytes[0] << 24) | ((uint32_t)bytes[1] << 16) |
           ((uint32_t)bytes[2] << 8) | bytes[3];
}

static uint32_t crc32_update(uint32_t crc, const uint8_t *data, size_t size) {
    for (size_t index = 0; index < size; index++) {
        crc ^= data[index];
        for (int bit = 0; bit < 8; bit++)
            crc = crc & 1 ? (crc >> 1) ^ UINT32_C(0xedb88320) : crc >> 1;
    }
    return crc;
}

static uint32_t adler32(const uint8_t *data, size_t size) {
    uint32_t first = 1;
    uint32_t second = 0;
    for (size_t index = 0; index < size; index++) {
        first = (first + data[index]) % 65521u;
        second = (second + first) % 65521u;
    }
    return (second << 16) | first;
}

static bool read_bits(BitStream *stream, unsigned count, unsigned *value) {
    if (count > 16 || stream->bit > stream->size * 8 ||
        count > stream->size * 8 - stream->bit) return false;
    unsigned result = 0;
    for (unsigned index = 0; index < count; index++) {
        size_t position = stream->bit++;
        result |= (unsigned)((stream->data[position / 8] >>
                              (position % 8)) & 1u) << index;
    }
    *value = result;
    return true;
}

static unsigned reverse_bits(unsigned code, unsigned width) {
    unsigned reversed = 0;
    for (unsigned index = 0; index < width; index++) {
        reversed = (reversed << 1) | (code & 1u);
        code >>= 1;
    }
    return reversed;
}

static bool build_huffman(Huffman *table, const uint8_t *lengths,
                          unsigned count) {
    unsigned totals[16] = {0};
    unsigned next_code[16] = {0};
    for (unsigned symbol = 0; symbol < count; symbol++) {
        if (lengths[symbol] > DEFLATE_TABLE_BITS) return false;
        totals[lengths[symbol]]++;
    }
    unsigned code = 0;
    for (unsigned width = 1; width <= DEFLATE_TABLE_BITS; width++) {
        unsigned previous = width == 1 ? 0 : totals[width - 1];
        code = (code + previous) << 1;
        next_code[width] = code;
        if (code + totals[width] > (1u << width)) return false;
    }
    table->entries = calloc(DEFLATE_TABLE_SIZE, sizeof(*table->entries));
    if (!table->entries) return false;
    for (unsigned symbol = 0; symbol < count; symbol++) {
        unsigned width = lengths[symbol];
        if (!width) continue;
        unsigned reversed = reverse_bits(next_code[width]++, width);
        for (unsigned index = reversed; index < DEFLATE_TABLE_SIZE;
             index += 1u << width) {
            table->entries[index].symbol = (uint16_t)symbol;
            table->entries[index].length = (uint8_t)width;
        }
    }
    return true;
}

static bool huffman_symbol(BitStream *stream, const Huffman *table,
                           unsigned *symbol) {
    if (!table->entries || stream->bit >= stream->size * 8) return false;
    unsigned index = 0;
    size_t available = stream->size * 8 - stream->bit;
    unsigned bits = available < DEFLATE_TABLE_BITS
        ? (unsigned)available : DEFLATE_TABLE_BITS;
    for (unsigned offset = 0; offset < bits; offset++) {
        size_t position = stream->bit + offset;
        index |= (unsigned)((stream->data[position / 8] >>
                             (position % 8)) & 1u) << offset;
    }
    HuffmanEntry entry = table->entries[index];
    if (!entry.length || entry.length > available) return false;
    stream->bit += entry.length;
    *symbol = entry.symbol;
    return true;
}

static bool dynamic_tables(BitStream *stream, Huffman *literal,
                           Huffman *distance) {
    static const unsigned order[19] = {
        16, 17, 18, 0, 8, 7, 9, 6, 10, 5,
        11, 4, 12, 3, 13, 2, 14, 1, 15
    };
    unsigned h_lit, h_dist, h_code;
    if (!read_bits(stream, 5, &h_lit) ||
        !read_bits(stream, 5, &h_dist) ||
        !read_bits(stream, 4, &h_code)) return false;
    h_lit += 257;
    h_dist += 1;
    h_code += 4;
    if (h_lit > 286 || h_dist > 32) return false;
    uint8_t code_lengths[19] = {0};
    for (unsigned index = 0; index < h_code; index++) {
        unsigned length;
        if (!read_bits(stream, 3, &length)) return false;
        code_lengths[order[index]] = (uint8_t)length;
    }
    Huffman code_table = {0};
    if (!build_huffman(&code_table, code_lengths, 19)) return false;
    uint8_t lengths[286 + 32] = {0};
    unsigned total = h_lit + h_dist;
    unsigned used = 0;
    bool valid = true;
    while (used < total) {
        unsigned symbol;
        if (!huffman_symbol(stream, &code_table, &symbol)) {
            valid = false;
            break;
        }
        if (symbol <= 15) {
            lengths[used++] = (uint8_t)symbol;
            continue;
        }
        unsigned extra = symbol == 16 ? 2 : symbol == 17 ? 3 : 7;
        unsigned base = symbol == 16 ? 3 : symbol == 17 ? 3 : 11;
        unsigned value;
        if (symbol > 18 || (symbol == 16 && used == 0) ||
            !read_bits(stream, extra, &value) ||
            base + value > total - used) {
            valid = false;
            break;
        }
        uint8_t length = symbol == 16 ? lengths[used - 1] : 0;
        for (unsigned repeat = 0; repeat < base + value; repeat++)
            lengths[used++] = length;
    }
    free(code_table.entries);
    if (!valid || !lengths[256] ||
        !build_huffman(literal, lengths, h_lit) ||
        !build_huffman(distance, lengths + h_lit, h_dist)) return false;
    return true;
}

static bool fixed_tables(Huffman *literal, Huffman *distance) {
    uint8_t lengths[288];
    for (unsigned symbol = 0; symbol < 288; symbol++)
        lengths[symbol] = symbol <= 143 ? 8 : symbol <= 255 ? 9
                          : symbol <= 279 ? 7 : 8;
    uint8_t distances[32];
    memset(distances, 5, sizeof(distances));
    return build_huffman(literal, lengths, 288) &&
           build_huffman(distance, distances, 32);
}

static bool inflate_block(BitStream *stream, uint8_t *output,
                          size_t capacity, size_t *used,
                          const Huffman *literal,
                          const Huffman *distance) {
    static const uint16_t length_base[29] = {
        3, 4, 5, 6, 7, 8, 9, 10, 11, 13,
        15, 17, 19, 23, 27, 31, 35, 43, 51, 59,
        67, 83, 99, 115, 131, 163, 195, 227, 258
    };
    static const uint8_t length_extra[29] = {
        0, 0, 0, 0, 0, 0, 0, 0, 1, 1,
        1, 1, 2, 2, 2, 2, 3, 3, 3, 3,
        4, 4, 4, 4, 5, 5, 5, 5, 0
    };
    static const uint16_t distance_base[30] = {
        1, 2, 3, 4, 5, 7, 9, 13, 17, 25,
        33, 49, 65, 97, 129, 193, 257, 385, 513, 769,
        1025, 1537, 2049, 3073, 4097, 6145, 8193, 12289,
        16385, 24577
    };
    static const uint8_t distance_extra[30] = {
        0, 0, 0, 0, 1, 1, 2, 2, 3, 3,
        4, 4, 5, 5, 6, 6, 7, 7, 8, 8,
        9, 9, 10, 10, 11, 11, 12, 12, 13, 13
    };
    for (;;) {
        unsigned symbol;
        if (!huffman_symbol(stream, literal, &symbol)) return false;
        if (symbol < 256) {
            if (*used >= capacity) return false;
            output[(*used)++] = (uint8_t)symbol;
        } else if (symbol == 256) {
            return true;
        } else {
            if (symbol > 285) return false;
            unsigned length_index = symbol - 257;
            unsigned extra_length;
            unsigned distance_symbol;
            if (!read_bits(stream, length_extra[length_index],
                           &extra_length) ||
                !huffman_symbol(stream, distance, &distance_symbol) ||
                distance_symbol >= 30) return false;
            unsigned extra_distance;
            if (!read_bits(stream, distance_extra[distance_symbol],
                           &extra_distance)) return false;
            size_t length = length_base[length_index] + extra_length;
            size_t offset = distance_base[distance_symbol] + extra_distance;
            if (offset > *used || length > capacity - *used) return false;
            for (size_t index = 0; index < length; index++) {
                output[*used] = output[*used - offset];
                (*used)++;
            }
        }
    }
}

static bool inflate_zlib(const uint8_t *compressed, size_t size,
                         uint8_t *output, size_t expected) {
    if (size < 6 || (compressed[0] & 15u) != 8 ||
        (compressed[0] >> 4) > 7 || (compressed[1] & 0x20u) != 0 ||
        (((unsigned)compressed[0] << 8) | compressed[1]) % 31 != 0)
        return false;
    BitStream stream = {
        .data = compressed + 2,
        .size = size - 6
    };
    size_t used = 0;
    bool final = false;
    while (!final) {
        unsigned last, type;
        if (!read_bits(&stream, 1, &last) ||
            !read_bits(&stream, 2, &type)) return false;
        final = last != 0;
        if (type == 0) {
            stream.bit = (stream.bit + 7u) & ~(size_t)7u;
            unsigned length, complement;
            if (!read_bits(&stream, 16, &length) ||
                !read_bits(&stream, 16, &complement) ||
                ((length ^ complement) & 0xffffu) != 0xffffu ||
                length > expected - used) return false;
            for (unsigned index = 0; index < length; index++) {
                unsigned value;
                if (!read_bits(&stream, 8, &value)) return false;
                output[used++] = (uint8_t)value;
            }
        } else if (type == 1 || type == 2) {
            Huffman literal = {0};
            Huffman distance = {0};
            bool valid = type == 1
                ? fixed_tables(&literal, &distance)
                : dynamic_tables(&stream, &literal, &distance);
            if (valid)
                valid = inflate_block(&stream, output, expected, &used,
                                      &literal, &distance);
            free(literal.entries);
            free(distance.entries);
            if (!valid) return false;
        } else {
            return false;
        }
    }
    return used == expected &&
           adler32(output, expected) == be32(compressed + size - 4);
}

static uint8_t paeth(uint8_t left, uint8_t above, uint8_t diagonal) {
    int prediction = (int)left + above - diagonal;
    int left_distance = abs(prediction - left);
    int above_distance = abs(prediction - above);
    int diagonal_distance = abs(prediction - diagonal);
    if (left_distance <= above_distance &&
        left_distance <= diagonal_distance) return left;
    return above_distance <= diagonal_distance ? above : diagonal;
}

static bool reconstruct_rows(const uint8_t *filtered, WmImage *image) {
    size_t row_bytes = (size_t)image->width * 4;
    for (uint32_t y = 0; y < image->height; y++) {
        const uint8_t *source = filtered + (size_t)y * (row_bytes + 1);
        unsigned filter = source[0];
        if (filter > 4) return false;
        uint8_t *row = image->pixels + (size_t)y * row_bytes;
        const uint8_t *previous = y ? row - row_bytes : NULL;
        for (size_t index = 0; index < row_bytes; index++) {
            uint8_t left = index >= 4 ? row[index - 4] : 0;
            uint8_t above = previous ? previous[index] : 0;
            uint8_t diagonal = previous && index >= 4
                ? previous[index - 4] : 0;
            uint8_t predictor = filter == 1 ? left
                : filter == 2 ? above
                : filter == 3 ? (uint8_t)(((unsigned)left + above) / 2)
                : filter == 4 ? paeth(left, above, diagonal) : 0;
            row[index] = (uint8_t)(source[index + 1] + predictor);
        }
    }
    return true;
}

bool wm_settings_png_decode(const uint8_t *data, size_t size, WmImage *image) {
    static const uint8_t signature[8] = {
        137, 'P', 'N', 'G', 13, 10, 26, 10
    };
    if (!data || !image) return false;
    memset(image, 0, sizeof(*image));
    if (size < sizeof(signature) ||
        memcmp(data, signature, sizeof(signature)) != 0) return false;
    size_t offset = sizeof(signature);
    uint8_t *compressed = NULL;
    size_t compressed_size = 0;
    bool header_seen = false;
    bool finished = false;
    while (offset < size) {
        if (size - offset < 12) break;
        size_t length = be32(data + offset);
        if (length > size - offset - 12) break;
        const uint8_t *kind = data + offset + 4;
        const uint8_t *body = data + offset + 8;
        uint32_t checksum = crc32_update(UINT32_C(0xffffffff),
                                          kind, length + 4) ^
                            UINT32_C(0xffffffff);
        if (checksum != be32(body + length)) break;
        if (memcmp(kind, "IHDR", 4) == 0) {
            if (header_seen || length != 13 || compressed_size) break;
            image->width = be32(body);
            image->height = be32(body + 4);
            if (!image->width || !image->height ||
                image->width > PNG_MAX_DIMENSION ||
                image->height > PNG_MAX_DIMENSION ||
                body[8] != 8 || body[9] != 6 || body[10] ||
                body[11] || body[12]) break;
            header_seen = true;
        } else if (memcmp(kind, "IDAT", 4) == 0) {
            if (!header_seen || length > PNG_MAX_COMPRESSED -
                                          compressed_size) break;
            uint8_t *grown = realloc(compressed,
                                     compressed_size + length);
            if (!grown) break;
            compressed = grown;
            memcpy(compressed + compressed_size, body, length);
            compressed_size += length;
        } else if (memcmp(kind, "IEND", 4) == 0) {
            finished = length == 0 && header_seen && compressed_size &&
                       offset + 12 == size;
            break;
        } else if (!(kind[0] & 0x20u)) {
            break;
        }
        offset += length + 12;
    }
    if (!finished) {
        free(compressed);
        wm_image_free(image);
        return false;
    }
    size_t row_bytes = (size_t)image->width * 4;
    size_t filtered_size = (row_bytes + 1) * image->height;
    bool valid = filtered_size <= PNG_MAX_DECOMPRESSED;
    uint8_t *filtered = valid ? malloc(filtered_size) : NULL;
    image->pixels = valid ? malloc(row_bytes * image->height) : NULL;
    valid = filtered && image->pixels &&
            inflate_zlib(compressed, compressed_size,
                         filtered, filtered_size) &&
            reconstruct_rows(filtered, image);
    free(compressed);
    free(filtered);
    if (!valid) wm_image_free(image);
    return valid;
}
