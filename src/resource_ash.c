#include "wii_menu/resource_ash.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

enum {
    WM_ASH_MAX_NODES = 4096,
    WM_ASH_MAX_DEPTH = 64
};

typedef struct WmAshBits {
    const uint8_t *data;
    size_t size;
    size_t position;
} WmAshBits;

typedef struct WmAshNode {
    int child[2];
    uint16_t value;
    bool leaf;
} WmAshNode;

typedef struct WmAshTree {
    WmAshNode *nodes;
    size_t count;
} WmAshTree;

static uint32_t wm_read_be32(const uint8_t *bytes)
{
    return ((uint32_t)bytes[0] << 24) | ((uint32_t)bytes[1] << 16) |
           ((uint32_t)bytes[2] << 8) | bytes[3];
}

static void wm_error(char *error, size_t error_size, const char *message)
{
    if (error != NULL && error_size != 0) {
        snprintf(error, error_size, "%s", message);
    }
}

static bool wm_read_bits(WmAshBits *bits, unsigned count, uint32_t *value)
{
    uint32_t result = 0;
    for (unsigned index = 0; index < count; index++) {
        size_t byte_index = bits->position / 8;
        if (byte_index >= bits->size) {
            return false;
        }
        unsigned shift = 7u - (unsigned)(bits->position % 8);
        result = (result << 1) | ((bits->data[byte_index] >> shift) & 1u);
        bits->position++;
    }
    *value = result;
    return true;
}

static bool wm_parse_node(WmAshBits *bits, WmAshTree *tree,
                          unsigned leaf_bits, unsigned depth, int *node_index)
{
    if (depth >= WM_ASH_MAX_DEPTH || tree->count >= WM_ASH_MAX_NODES) {
        return false;
    }

    uint32_t branch;
    if (!wm_read_bits(bits, 1, &branch)) {
        return false;
    }
    int index = (int)tree->count++;
    *node_index = index;

    if (branch == 0) {
        uint32_t value;
        if (!wm_read_bits(bits, leaf_bits, &value)) {
            return false;
        }
        tree->nodes[index].leaf = true;
        tree->nodes[index].value = (uint16_t)value;
        return true;
    }

    return wm_parse_node(bits, tree, leaf_bits, depth + 1,
                         &tree->nodes[index].child[0]) &&
           wm_parse_node(bits, tree, leaf_bits, depth + 1,
                         &tree->nodes[index].child[1]);
}

static bool wm_decode_symbol(WmAshBits *bits, const WmAshTree *tree,
                             int root, uint32_t *symbol)
{
    int index = root;
    while (index >= 0 && (size_t)index < tree->count) {
        const WmAshNode *node = &tree->nodes[index];
        if (node->leaf) {
            *symbol = node->value;
            return true;
        }
        uint32_t branch;
        if (!wm_read_bits(bits, 1, &branch)) {
            return false;
        }
        index = node->child[branch];
    }
    return false;
}

bool wm_ash_decode(const uint8_t *data, size_t size,
                   uint8_t **output, size_t *output_size,
                   char *error, size_t error_size)
{
    if (output == NULL || output_size == NULL || data == NULL) {
        wm_error(error, error_size, "Invalid ASH input or output.");
        return false;
    }
    *output = NULL;
    *output_size = 0;

    if (size < 4 || memcmp(data, "ASH0", 4) != 0) {
        uint8_t *copy = malloc(size != 0 ? size : 1);
        if (copy == NULL) {
            wm_error(error, error_size, "Out of memory copying resource.");
            return false;
        }
        if (size != 0) {
            memcpy(copy, data, size);
        }
        *output = copy;
        *output_size = size;
        return true;
    }
    if (size < 12) {
        wm_error(error, error_size, "Truncated ASH header.");
        return false;
    }

    size_t decoded_size = wm_read_be32(data + 4) & 0x00ffffffu;
    size_t distance_offset = wm_read_be32(data + 8);
    if (distance_offset >= size || size > SIZE_MAX / 8) {
        wm_error(error, error_size, "Invalid ASH bitstream offset.");
        return false;
    }

    WmAshTree literals = {0};
    WmAshTree distances = {0};
    literals.nodes = calloc(WM_ASH_MAX_NODES, sizeof(*literals.nodes));
    distances.nodes = calloc(WM_ASH_MAX_NODES, sizeof(*distances.nodes));
    uint8_t *decoded = malloc(decoded_size != 0 ? decoded_size : 1);
    if (literals.nodes == NULL || distances.nodes == NULL || decoded == NULL) {
        wm_error(error, error_size, "Out of memory decoding ASH resource.");
        free(literals.nodes);
        free(distances.nodes);
        free(decoded);
        return false;
    }

    WmAshBits literal_bits = {data, size, 12 * 8};
    WmAshBits distance_bits = {data, size, distance_offset * 8};
    int literal_root = -1;
    int distance_root = -1;
    bool valid = wm_parse_node(&literal_bits, &literals, 9, 0, &literal_root) &&
                 wm_parse_node(&distance_bits, &distances, 11, 0, &distance_root);
    if (!valid) {
        wm_error(error, error_size, "Truncated or oversized ASH Huffman tree.");
    }

    size_t produced = 0;
    while (valid && produced < decoded_size) {
        uint32_t symbol;
        if (!wm_decode_symbol(&literal_bits, &literals, literal_root, &symbol)) {
            wm_error(error, error_size, "Truncated ASH literal stream.");
            valid = false;
            break;
        }
        if (symbol < 256) {
            decoded[produced++] = (uint8_t)symbol;
            continue;
        }

        uint32_t distance_symbol;
        if (!wm_decode_symbol(&distance_bits, &distances,
                              distance_root, &distance_symbol)) {
            wm_error(error, error_size, "Truncated ASH distance stream.");
            valid = false;
            break;
        }
        size_t distance = (size_t)distance_symbol + 1;
        if (distance > produced) {
            wm_error(error, error_size, "ASH back-reference precedes output.");
            valid = false;
            break;
        }
        size_t length = (size_t)symbol - 0xfdu;
        if (length > decoded_size - produced) {
            length = decoded_size - produced;
        }
        for (size_t index = 0; index < length; index++) {
            decoded[produced] = decoded[produced - distance];
            produced++;
        }
    }

    free(literals.nodes);
    free(distances.nodes);
    if (!valid) {
        free(decoded);
        return false;
    }
    *output = decoded;
    *output_size = decoded_size;
    return true;
}
