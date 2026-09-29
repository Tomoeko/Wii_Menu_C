#ifndef WM_NAND_READER_INTERNAL_H
#define WM_NAND_READER_INTERNAL_H

#include "reader.h"
#include "../wad/crypto.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>

enum {
    WM_NAND_PAGE_SIZE = 0x800,
    WM_NAND_PAGE_STRIDE = 0x840,
    WM_NAND_SPARE_SIZE = 0x40,
    WM_NAND_CLUSTER_SIZE = 0x4000,
    WM_NAND_RAW_CLUSTER = 8 * WM_NAND_PAGE_STRIDE,
    WM_NAND_SUPERBLOCK_SIZE = 0x40000,
    WM_NAND_FIRST_SUPERBLOCK = 0x7f00,
    WM_NAND_NODE_COUNT = 0x17ff,
    WM_NAND_END_NODE = 0xffff,
    WM_NAND_END_CLUSTER = 0xfffb,
    WM_NAND_PATH_CAPACITY = 4096,
    WM_NAND_PATH_HASH_SLOTS = 16384
};

typedef struct WmNandEntry {
    char *path;
    uint8_t name[12];
    uint16_t *clusters;
    size_t cluster_count;
    uint32_t size;
    uint32_t owner;
    uint32_t extra;
    bool present;
    bool directory;
    bool selected;
} WmNandEntry;

typedef struct WmNandReader {
    FILE *stream;
    uint8_t hmac_key[20];
    uint8_t aes_key[16];
    WmAes128 aes;
    uint8_t *superblock;
    WmNandEntry *entries;
    uint32_t generation;
} WmNandReader;

static inline uint32_t wm_nand_be32(const uint8_t *bytes) {
    return ((uint32_t)bytes[0] << 24) | ((uint32_t)bytes[1] << 16) |
           ((uint32_t)bytes[2] << 8) | bytes[3];
}

void wm_nand_set_error(char *error, size_t capacity, const char *message);
bool wm_nand_open_reader(WmNandReader *reader, const char *source_path,
                         const char *keys_path, char *error, size_t error_capacity);
void wm_nand_close_reader(WmNandReader *reader);
bool wm_nand_read_file_cluster(WmNandReader *reader, const WmNandEntry *entry,
                               uint16_t index, size_t ordinal,
                               uint8_t data[WM_NAND_CLUSTER_SIZE], char *error,
                               size_t error_capacity);
bool wm_nand_discover_channel_titles(WmNandReader *reader, size_t *title_count,
                                     char *error, size_t error_capacity);
bool wm_nand_discover_shared_fonts(WmNandReader *reader, size_t *archive_count,
                                   char *error, size_t error_capacity);

#endif
