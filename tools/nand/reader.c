#define _POSIX_C_SOURCE 200809L
#ifdef __APPLE__
#define _DARWIN_C_SOURCE
#endif

#include "reader_internal.h"
#include "../wad/crypto.h"
#include "wii_menu/support/endian.h"

#include <ctype.h>
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

static const uint64_t WM_NAND_DUMP_SIZE = UINT64_C(0x21000000);

typedef struct PendingNode {
    uint16_t index;
    uint16_t parent;
    unsigned depth;
} PendingNode;

void wm_nand_set_error(char *error, size_t capacity, const char *message) {
    if (error != NULL && capacity != 0) {
        snprintf(error, capacity, "%s", message);
    }
}

static bool exact_file_size(FILE *stream, uint64_t *size) {
    if (fseeko(stream, 0, SEEK_END) != 0)
        return false;
    off_t length = ftello(stream);
    if (length < 0)
        return false;
    *size = (uint64_t)length;
    return fseeko(stream, 0, SEEK_SET) == 0;
}

static bool read_keys_file(const char *path, uint8_t key_data[0x400]) {
    FILE *stream = fopen(path, "rb");
    if (stream == NULL)
        return false;
    uint64_t size = 0;
    bool valid = exact_file_size(stream, &size) && size == 0x400 &&
                 fread(key_data, 1, 0x400, stream) == 0x400;
    fclose(stream);
    return valid;
}

static bool read_keys(WmNandReader *reader, const char *source_path,
                      const char *keys_path, uint64_t dump_size, char *error,
                      size_t error_capacity) {
    uint8_t key_data[0x400] = {0};
    bool valid = false;
    if (keys_path != NULL) {
        valid = read_keys_file(keys_path, key_data);
    } else if (dump_size == WM_NAND_DUMP_SIZE + 0x400) {
        valid =
            fseeko(reader->stream, (off_t)WM_NAND_DUMP_SIZE, SEEK_SET) == 0 &&
            fread(key_data, 1, sizeof(key_data), reader->stream) == sizeof(key_data);
    } else {
        char sibling[WM_NAND_PATH_CAPACITY];
        const char *slash = strrchr(source_path, '/');
        int length = slash == NULL
                         ? snprintf(sibling, sizeof(sibling), "keys.bin")
                         : snprintf(sibling, sizeof(sibling), "%.*skeys.bin",
                                    (int)(slash - source_path + 1), source_path);
        valid = length >= 0 && length < (int)sizeof(sibling) &&
                read_keys_file(sibling, key_data);
    }
    if (!valid) {
        wm_nand_set_error(
            error, error_capacity,
            "A matching 1024-byte BootMii key footer or keys file is required.");
        memset(key_data, 0, sizeof(key_data));
        return false;
    }
    memcpy(reader->hmac_key, key_data + 0x144, sizeof(reader->hmac_key));
    memcpy(reader->aes_key, key_data + 0x158, sizeof(reader->aes_key));
    wm_aes128_init(&reader->aes, reader->aes_key);
    memset(key_data, 0, sizeof(key_data));
    return true;
}

static bool read_cluster(WmNandReader *reader, uint16_t index,
                         uint8_t data[WM_NAND_CLUSTER_SIZE],
                         uint8_t spare[8][WM_NAND_SPARE_SIZE]) {
    if (index >= 0x8000)
        return false;
    uint8_t raw[WM_NAND_RAW_CLUSTER];
    off_t offset = (off_t)index * WM_NAND_RAW_CLUSTER;
    if (fseeko(reader->stream, offset, SEEK_SET) != 0 ||
        fread(raw, 1, sizeof(raw), reader->stream) != sizeof(raw)) {
        return false;
    }
    for (size_t page = 0; page < 8; page++) {
        size_t raw_offset = page * WM_NAND_PAGE_STRIDE;
        memcpy(data + page * WM_NAND_PAGE_SIZE, raw + raw_offset, WM_NAND_PAGE_SIZE);
        memcpy(spare[page], raw + raw_offset + WM_NAND_PAGE_SIZE, WM_NAND_SPARE_SIZE);
    }
    return true;
}

static void hmac_sha1(const uint8_t key[20], const uint8_t salt[64],
                      const uint8_t *data, size_t size, uint8_t digest[20]) {
    uint8_t inner_pad[64];
    uint8_t outer_pad[64];
    for (size_t index = 0; index < 64; index++) {
        uint8_t value = index < 20 ? key[index] : 0;
        inner_pad[index] = (uint8_t)(value ^ 0x36);
        outer_pad[index] = (uint8_t)(value ^ 0x5c);
    }
    uint8_t inner[20];
    WmSha1 sha1;
    wm_sha1_init(&sha1);
    wm_sha1_update(&sha1, inner_pad, sizeof(inner_pad));
    wm_sha1_update(&sha1, salt, 64);
    wm_sha1_update(&sha1, data, size);
    wm_sha1_final(&sha1, inner);
    wm_sha1_init(&sha1);
    wm_sha1_update(&sha1, outer_pad, sizeof(outer_pad));
    wm_sha1_update(&sha1, inner, sizeof(inner));
    wm_sha1_final(&sha1, digest);
    memset(inner_pad, 0, sizeof(inner_pad));
    memset(outer_pad, 0, sizeof(outer_pad));
    memset(inner, 0, sizeof(inner));
    memset(&sha1, 0, sizeof(sha1));
}

static bool digest_equal(const uint8_t *left, const uint8_t *right, size_t size) {
    uint8_t difference = 0;
    for (size_t index = 0; index < size; index++) {
        difference |= (uint8_t)(left[index] ^ right[index]);
    }
    return difference == 0;
}

static bool verify_spare_hmac(const uint8_t spare[8][WM_NAND_SPARE_SIZE],
                              const uint8_t expected[20]) {
    uint8_t second[20];
    memcpy(second, spare[6] + 21, 12);
    memcpy(second + 12, spare[7] + 1, 8);
    bool valid =
        digest_equal(spare[6] + 1, expected, 20) || digest_equal(second, expected, 20);
    memset(second, 0, sizeof(second));
    return valid;
}

static bool load_superblock(WmNandReader *reader, char *error, size_t error_capacity) {
    bool found = false;
    uint16_t first = 0;
    uint32_t generation = 0;
    uint8_t data[WM_NAND_CLUSTER_SIZE];
    uint8_t spare[8][WM_NAND_SPARE_SIZE];
    for (uint16_t slot = 0; slot < 16; slot++) {
        uint16_t candidate = (uint16_t)(WM_NAND_FIRST_SUPERBLOCK + slot * 16);
        if (!read_cluster(reader, candidate, data, spare)) {
            wm_nand_set_error(error, error_capacity, "NAND superblock is truncated.");
            return false;
        }
        if (memcmp(data, "SFFS", 4) != 0)
            continue;
        uint32_t current = wm_nand_be32(data + 4);
        if (!found || current >= generation) {
            found = true;
            generation = current;
            first = candidate;
        }
    }
    if (!found) {
        wm_nand_set_error(error, error_capacity,
                          "No SFFS filesystem metadata was found.");
        return false;
    }
    reader->superblock = malloc(WM_NAND_SUPERBLOCK_SIZE);
    if (reader->superblock == NULL) {
        wm_nand_set_error(error, error_capacity,
                          "Out of memory reading the NAND superblock.");
        return false;
    }
    for (uint16_t index = 0; index < 16; index++) {
        if (!read_cluster(reader, (uint16_t)(first + index), data, spare)) {
            wm_nand_set_error(error, error_capacity, "NAND superblock is truncated.");
            return false;
        }
        memcpy(reader->superblock + (size_t)index * WM_NAND_CLUSTER_SIZE, data,
               sizeof(data));
    }
    uint8_t salt[64] = {0};
    uint8_t expected[20];
    wm_write_be32(salt + 16, first);
    hmac_sha1(reader->hmac_key, salt, reader->superblock, WM_NAND_SUPERBLOCK_SIZE,
              expected);
    if (!verify_spare_hmac((const uint8_t(*)[WM_NAND_SPARE_SIZE])spare, expected)) {
        wm_nand_set_error(
            error, error_capacity,
            "Newest NAND filesystem metadata failed HMAC authentication.");
        return false;
    }
    reader->generation = generation;
    return true;
}

void wm_nand_close_reader(WmNandReader *reader) {
    if (reader == NULL)
        return;
    if (reader->entries != NULL) {
        for (size_t index = 0; index < WM_NAND_NODE_COUNT; index++) {
            free(reader->entries[index].path);
            free(reader->entries[index].clusters);
        }
    }
    free(reader->entries);
    free(reader->superblock);
    if (reader->stream != NULL)
        fclose(reader->stream);
    memset(reader, 0, sizeof(*reader));
}

static bool reserved_name(const uint8_t *name, size_t length) {
    size_t stem = 0;
    while (stem < length && name[stem] != '.')
        stem++;
    if (stem == 3) {
        static const char *const reserved[] = {"CON", "PRN", "AUX", "NUL"};
        for (size_t index = 0; index < sizeof(reserved) / sizeof(reserved[0]);
             index++) {
            bool same = true;
            for (size_t byte = 0; byte < 3; byte++) {
                if (toupper(name[byte]) != reserved[index][byte])
                    same = false;
            }
            if (same)
                return true;
        }
    }
    if (stem == 4 && name[3] >= '1' && name[3] <= '9') {
        bool com = toupper(name[0]) == 'C' && toupper(name[1]) == 'O' &&
                   toupper(name[2]) == 'M';
        bool lpt = toupper(name[0]) == 'L' && toupper(name[1]) == 'P' &&
                   toupper(name[2]) == 'T';
        return com || lpt;
    }
    return false;
}

static bool valid_component(const uint8_t name[12], size_t *length) {
    size_t size = 0;
    while (size < 12 && name[size] != 0)
        size++;
    if (size == 0 || (size == 1 && name[0] == '.') ||
        (size == 2 && name[0] == '.' && name[1] == '.') || name[size - 1] == '.' ||
        name[size - 1] == ' ' || reserved_name(name, size)) {
        return false;
    }
    for (size_t index = 0; index < size; index++) {
        uint8_t value = name[index];
        if (value < 32 || value > 126 || strchr("/\\:*?\"<>|", (int)value) != NULL) {
            return false;
        }
    }
    *length = size;
    return true;
}

static char *join_path(const char *parent, const uint8_t *name, size_t size) {
    size_t parent_size = strlen(parent);
    size_t separator = parent_size != 0 ? 1 : 0;
    if (parent_size > WM_NAND_PATH_CAPACITY - separator - size - 1) {
        return NULL;
    }
    char *path = malloc(parent_size + separator + size + 1);
    if (path == NULL)
        return NULL;
    memcpy(path, parent, parent_size);
    if (separator != 0)
        path[parent_size] = '/';
    memcpy(path + parent_size + separator, name, size);
    path[parent_size + separator + size] = '\0';
    return path;
}

static uint64_t portable_hash(const char *path) {
    uint64_t hash = UINT64_C(14695981039346656037);
    for (; *path != '\0'; path++) {
        hash ^= (unsigned char)tolower((unsigned char)*path);
        hash *= UINT64_C(1099511628211);
    }
    return hash;
}

static bool portable_equal(const char *left, const char *right) {
    for (;; left++, right++) {
        if (tolower((unsigned char)*left) != tolower((unsigned char)*right))
            return false;
        if (*left == '\0')
            return true;
    }
}

static bool insert_unique_path(uint16_t hashes[WM_NAND_PATH_HASH_SLOTS],
                               WmNandEntry *entries, uint16_t index) {
    const char *path = entries[index].path;
    size_t slot = (size_t)(portable_hash(path) % WM_NAND_PATH_HASH_SLOTS);
    for (size_t attempt = 0; attempt < WM_NAND_PATH_HASH_SLOTS; attempt++) {
        if (hashes[slot] == 0) {
            hashes[slot] = (uint16_t)(index + 1);
            return true;
        }
        if (portable_equal(path, entries[hashes[slot] - 1].path)) {
            return false;
        }
        slot = (slot + 1) % WM_NAND_PATH_HASH_SLOTS;
    }
    return false;
}

static bool read_entry_clusters(const WmNandReader *reader, WmNandEntry *entry,
                                uint16_t first, bool *used_clusters, char *error,
                                size_t error_capacity) {
    size_t clusters = (size_t)(entry->size / WM_NAND_CLUSTER_SIZE) +
                      (entry->size % WM_NAND_CLUSTER_SIZE != 0);
    if (clusters > WM_NAND_FIRST_SUPERBLOCK) {
        wm_nand_set_error(error, error_capacity,
                          "NAND file exceeds the flash allocation limit.");
        return false;
    }
    if (clusters != 0) {
        entry->clusters = malloc(clusters * sizeof(*entry->clusters));
        if (entry->clusters == NULL) {
            wm_nand_set_error(error, error_capacity,
                              "Out of memory reading a NAND allocation chain.");
            return false;
        }
    }
    uint16_t cluster = first;
    for (size_t ordinal = 0; ordinal < clusters; ordinal++) {
        if (cluster < 0x40 || cluster >= WM_NAND_FIRST_SUPERBLOCK ||
            used_clusters[cluster]) {
            wm_nand_set_error(error, error_capacity,
                              "NAND file has an invalid or shared cluster chain.");
            return false;
        }
        used_clusters[cluster] = true;
        entry->clusters[ordinal] = cluster;
        entry->cluster_count++;
        cluster = wm_read_be16(reader->superblock + 12 + (size_t)cluster * 2);
    }
    if (clusters != 0 && cluster != WM_NAND_END_CLUSTER) {
        wm_nand_set_error(error, error_capacity,
                          "NAND file length does not match its cluster chain.");
        return false;
    }
    return true;
}

static bool parse_entries(WmNandReader *reader, char *error, size_t error_capacity) {
    bool *visited = calloc(WM_NAND_NODE_COUNT, sizeof(*visited));
    bool *used_clusters = calloc(WM_NAND_FIRST_SUPERBLOCK, sizeof(*used_clusters));
    uint16_t *hashes = calloc(WM_NAND_PATH_HASH_SLOTS, sizeof(*hashes));
    PendingNode *pending = calloc(WM_NAND_NODE_COUNT * 2, sizeof(*pending));
    reader->entries = calloc(WM_NAND_NODE_COUNT, sizeof(*reader->entries));
    if (visited == NULL || used_clusters == NULL || hashes == NULL || pending == NULL ||
        reader->entries == NULL) {
        wm_nand_set_error(error, error_capacity,
                          "Out of memory reading the NAND filesystem.");
        free(visited);
        free(used_clusters);
        free(hashes);
        free(pending);
        return false;
    }

    size_t top = 0;
    pending[top++] = (PendingNode){0, WM_NAND_END_NODE, 0};
    bool valid = true;
    while (top != 0 && valid) {
        PendingNode task = pending[--top];
        uint16_t index = task.index;
        if (index == WM_NAND_END_NODE)
            continue;
        if (index >= WM_NAND_NODE_COUNT || visited[index] || task.depth > 64) {
            wm_nand_set_error(error, error_capacity,
                              "NAND filesystem has an invalid or repeated node.");
            valid = false;
            break;
        }
        visited[index] = true;
        const uint8_t *node = reader->superblock + 0x1000c + (size_t)index * 32;
        uint8_t mode = node[12];
        uint16_t first = wm_read_be16(node + 14);
        uint16_t sibling = wm_read_be16(node + 16);
        bool directory = (mode & 3) == 2;
        if ((mode & 3) != 1 && !directory) {
            wm_nand_set_error(error, error_capacity,
                              "NAND filesystem has an unsupported node type.");
            valid = false;
            break;
        }
        if (index == 0 && (!directory || sibling != WM_NAND_END_NODE)) {
            wm_nand_set_error(error, error_capacity,
                              "NAND filesystem has an invalid root.");
            valid = false;
            break;
        }
        WmNandEntry *entry = &reader->entries[index];
        entry->present = true;
        entry->directory = directory;
        memcpy(entry->name, node, sizeof(entry->name));
        entry->size = wm_nand_be32(node + 18);
        entry->owner = wm_nand_be32(node + 22);
        entry->extra = wm_nand_be32(node + 28);
        if (index == 0) {
            entry->path = malloc(1);
            if (entry->path != NULL)
                entry->path[0] = '\0';
        } else {
            size_t name_size = 0;
            if (task.parent >= WM_NAND_NODE_COUNT ||
                !reader->entries[task.parent].present ||
                !valid_component(node, &name_size)) {
                wm_nand_set_error(error, error_capacity,
                                  "NAND filesystem contains an unsafe filename.");
                valid = false;
                break;
            }
            entry->path = join_path(reader->entries[task.parent].path, node, name_size);
            if (entry->path != NULL &&
                !insert_unique_path(hashes, reader->entries, index)) {
                wm_nand_set_error(error, error_capacity,
                                  "NAND filesystem contains duplicate portable paths.");
                valid = false;
                break;
            }
        }
        if (entry->path == NULL) {
            wm_nand_set_error(error, error_capacity,
                              "NAND path exceeds the supported limit.");
            valid = false;
            break;
        }
        if (top + 2 > WM_NAND_NODE_COUNT * 2) {
            wm_nand_set_error(error, error_capacity,
                              "NAND filesystem nesting is invalid.");
            valid = false;
            break;
        }
        pending[top++] = (PendingNode){sibling, task.parent, task.depth};
        if (directory) {
            pending[top++] = (PendingNode){first, index, task.depth + 1};
        } else if (!read_entry_clusters(reader, entry, first, used_clusters, error,
                                        error_capacity)) {
            valid = false;
        }
    }
    free(visited);
    free(used_clusters);
    free(hashes);
    free(pending);
    return valid;
}

bool wm_nand_open_reader(WmNandReader *reader, const char *source_path,
                         const char *keys_path, char *error, size_t error_capacity) {
    reader->stream = fopen(source_path, "rb");
    if (reader->stream == NULL) {
        wm_nand_set_error(error, error_capacity, "Could not open the NAND dump.");
        return false;
    }
    uint64_t size = 0;
    if (!exact_file_size(reader->stream, &size) ||
        (size != WM_NAND_DUMP_SIZE && size != WM_NAND_DUMP_SIZE + 0x400)) {
        wm_nand_set_error(error, error_capacity,
                          "Expected a 512 MiB Wii BootMii dump with spare areas.");
        return false;
    }
    return read_keys(reader, source_path, keys_path, size, error, error_capacity) &&
           load_superblock(reader, error, error_capacity) &&
           parse_entries(reader, error, error_capacity);
}

bool wm_nand_read_file_cluster(WmNandReader *reader, const WmNandEntry *entry,
                               uint16_t node_index, size_t ordinal,
                               uint8_t data[WM_NAND_CLUSTER_SIZE], char *error,
                               size_t error_capacity) {
    uint8_t spare[8][WM_NAND_SPARE_SIZE];
    if (ordinal >= entry->cluster_count ||
        !read_cluster(reader, entry->clusters[ordinal], data, spare)) {
        wm_nand_set_error(error, error_capacity, "A NAND file cluster is truncated.");
        return false;
    }
    const uint8_t zero_iv[16] = {0};
    wm_aes128_cbc_decrypt(&reader->aes, data, WM_NAND_CLUSTER_SIZE, zero_iv);
    uint8_t salt[64] = {0};
    uint8_t expected[20];
    wm_write_be32(salt, entry->owner);
    memcpy(salt + 4, entry->name, 12);
    wm_write_be32(salt + 16, (uint32_t)ordinal);
    wm_write_be32(salt + 20, node_index);
    wm_write_be32(salt + 24, entry->extra);
    hmac_sha1(reader->hmac_key, salt, data, WM_NAND_CLUSTER_SIZE, expected);
    if (!verify_spare_hmac((const uint8_t(*)[WM_NAND_SPARE_SIZE])spare, expected)) {
        wm_nand_set_error(
            error, error_capacity,
            "A NAND file failed HMAC authentication (wrong keys or damaged dump).");
        return false;
    }
    return true;
}
