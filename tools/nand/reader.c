#define _POSIX_C_SOURCE 200809L
#ifdef __APPLE__
#define _DARWIN_C_SOURCE
#endif

#include "reader.h"
#include "../wad/crypto.h"

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

static const uint64_t WM_NAND_DUMP_SIZE = UINT64_C(0x21000000);

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

typedef struct PendingNode {
    uint16_t index;
    uint16_t parent;
    unsigned depth;
} PendingNode;

static void set_error(char *error, size_t capacity, const char *message)
{
    if (error != NULL && capacity != 0) {
        snprintf(error, capacity, "%s", message);
    }
}

static uint16_t be16(const uint8_t *bytes)
{
    return (uint16_t)(((uint16_t)bytes[0] << 8) | bytes[1]);
}

static uint32_t be32(const uint8_t *bytes)
{
    return ((uint32_t)bytes[0] << 24) | ((uint32_t)bytes[1] << 16) |
           ((uint32_t)bytes[2] << 8) | bytes[3];
}

static void write_be32(uint8_t *bytes, uint32_t value)
{
    bytes[0] = (uint8_t)(value >> 24);
    bytes[1] = (uint8_t)(value >> 16);
    bytes[2] = (uint8_t)(value >> 8);
    bytes[3] = (uint8_t)value;
}

static bool exact_file_size(FILE *stream, uint64_t *size)
{
    if (fseeko(stream, 0, SEEK_END) != 0) return false;
    off_t length = ftello(stream);
    if (length < 0) return false;
    *size = (uint64_t)length;
    return fseeko(stream, 0, SEEK_SET) == 0;
}

static bool read_keys_file(const char *path, uint8_t key_data[0x400])
{
    FILE *stream = fopen(path, "rb");
    if (stream == NULL) return false;
    uint64_t size = 0;
    bool valid = exact_file_size(stream, &size) && size == 0x400 &&
                 fread(key_data, 1, 0x400, stream) == 0x400;
    fclose(stream);
    return valid;
}

static bool read_keys(WmNandReader *reader, const char *source_path,
                      const char *keys_path, uint64_t dump_size,
                      char *error, size_t error_capacity)
{
    uint8_t key_data[0x400] = {0};
    bool valid = false;
    if (keys_path != NULL) {
        valid = read_keys_file(keys_path, key_data);
    } else if (dump_size == WM_NAND_DUMP_SIZE + 0x400) {
        valid = fseeko(reader->stream, (off_t)WM_NAND_DUMP_SIZE, SEEK_SET) == 0 &&
                fread(key_data, 1, sizeof(key_data), reader->stream) ==
                    sizeof(key_data);
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
        set_error(error, error_capacity,
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
                         uint8_t spare[8][WM_NAND_SPARE_SIZE])
{
    if (index >= 0x8000) return false;
    uint8_t raw[WM_NAND_RAW_CLUSTER];
    off_t offset = (off_t)index * WM_NAND_RAW_CLUSTER;
    if (fseeko(reader->stream, offset, SEEK_SET) != 0 ||
        fread(raw, 1, sizeof(raw), reader->stream) != sizeof(raw)) {
        return false;
    }
    for (size_t page = 0; page < 8; page++) {
        size_t raw_offset = page * WM_NAND_PAGE_STRIDE;
        memcpy(data + page * WM_NAND_PAGE_SIZE,
               raw + raw_offset, WM_NAND_PAGE_SIZE);
        memcpy(spare[page], raw + raw_offset + WM_NAND_PAGE_SIZE,
               WM_NAND_SPARE_SIZE);
    }
    return true;
}

static void hmac_sha1(const uint8_t key[20], const uint8_t salt[64],
                      const uint8_t *data, size_t size, uint8_t digest[20])
{
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

static bool digest_equal(const uint8_t *left, const uint8_t *right, size_t size)
{
    uint8_t difference = 0;
    for (size_t index = 0; index < size; index++) {
        difference |= (uint8_t)(left[index] ^ right[index]);
    }
    return difference == 0;
}

static bool verify_spare_hmac(const uint8_t spare[8][WM_NAND_SPARE_SIZE],
                              const uint8_t expected[20])
{
    uint8_t second[20];
    memcpy(second, spare[6] + 21, 12);
    memcpy(second + 12, spare[7] + 1, 8);
    bool valid = digest_equal(spare[6] + 1, expected, 20) ||
                 digest_equal(second, expected, 20);
    memset(second, 0, sizeof(second));
    return valid;
}

static bool load_superblock(WmNandReader *reader,
                            char *error, size_t error_capacity)
{
    bool found = false;
    uint16_t first = 0;
    uint32_t generation = 0;
    uint8_t data[WM_NAND_CLUSTER_SIZE];
    uint8_t spare[8][WM_NAND_SPARE_SIZE];
    for (uint16_t slot = 0; slot < 16; slot++) {
        uint16_t candidate = (uint16_t)(WM_NAND_FIRST_SUPERBLOCK + slot * 16);
        if (!read_cluster(reader, candidate, data, spare)) {
            set_error(error, error_capacity, "NAND superblock is truncated.");
            return false;
        }
        if (memcmp(data, "SFFS", 4) != 0) continue;
        uint32_t current = be32(data + 4);
        if (!found || current >= generation) {
            found = true;
            generation = current;
            first = candidate;
        }
    }
    if (!found) {
        set_error(error, error_capacity, "No SFFS filesystem metadata was found.");
        return false;
    }
    reader->superblock = malloc(WM_NAND_SUPERBLOCK_SIZE);
    if (reader->superblock == NULL) {
        set_error(error, error_capacity, "Out of memory reading the NAND superblock.");
        return false;
    }
    for (uint16_t index = 0; index < 16; index++) {
        if (!read_cluster(reader, (uint16_t)(first + index),
                          data, spare)) {
            set_error(error, error_capacity, "NAND superblock is truncated.");
            return false;
        }
        memcpy(reader->superblock + (size_t)index * WM_NAND_CLUSTER_SIZE,
               data, sizeof(data));
    }
    uint8_t salt[64] = {0};
    uint8_t expected[20];
    write_be32(salt + 16, first);
    hmac_sha1(reader->hmac_key, salt, reader->superblock,
              WM_NAND_SUPERBLOCK_SIZE, expected);
    if (!verify_spare_hmac((const uint8_t (*)[WM_NAND_SPARE_SIZE])spare,
                            expected)) {
        set_error(error, error_capacity,
                  "Newest NAND filesystem metadata failed HMAC authentication.");
        return false;
    }
    reader->generation = generation;
    return true;
}

static void close_reader(WmNandReader *reader)
{
    if (reader == NULL) return;
    if (reader->entries != NULL) {
        for (size_t index = 0; index < WM_NAND_NODE_COUNT; index++) {
            free(reader->entries[index].path);
            free(reader->entries[index].clusters);
        }
    }
    free(reader->entries);
    free(reader->superblock);
    if (reader->stream != NULL) fclose(reader->stream);
    memset(reader, 0, sizeof(*reader));
}

static bool reserved_name(const uint8_t *name, size_t length)
{
    size_t stem = 0;
    while (stem < length && name[stem] != '.') stem++;
    if (stem == 3) {
        static const char *const reserved[] = {"CON", "PRN", "AUX", "NUL"};
        for (size_t index = 0; index < sizeof(reserved) / sizeof(reserved[0]);
             index++) {
            bool same = true;
            for (size_t byte = 0; byte < 3; byte++) {
                if (toupper(name[byte]) != reserved[index][byte]) same = false;
            }
            if (same) return true;
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

static bool valid_component(const uint8_t name[12], size_t *length)
{
    size_t size = 0;
    while (size < 12 && name[size] != 0) size++;
    if (size == 0 || (size == 1 && name[0] == '.') ||
        (size == 2 && name[0] == '.' && name[1] == '.') ||
        name[size - 1] == '.' || name[size - 1] == ' ' ||
        reserved_name(name, size)) {
        return false;
    }
    for (size_t index = 0; index < size; index++) {
        uint8_t value = name[index];
        if (value < 32 || value > 126 ||
            strchr("/\\:*?\"<>|", (int)value) != NULL) {
            return false;
        }
    }
    *length = size;
    return true;
}

static char *join_path(const char *parent, const uint8_t *name, size_t size)
{
    size_t parent_size = strlen(parent);
    size_t separator = parent_size != 0 ? 1 : 0;
    if (parent_size > WM_NAND_PATH_CAPACITY - separator - size - 1) {
        return NULL;
    }
    char *path = malloc(parent_size + separator + size + 1);
    if (path == NULL) return NULL;
    memcpy(path, parent, parent_size);
    if (separator != 0) path[parent_size] = '/';
    memcpy(path + parent_size + separator, name, size);
    path[parent_size + separator + size] = '\0';
    return path;
}

static uint64_t portable_hash(const char *path)
{
    uint64_t hash = UINT64_C(14695981039346656037);
    for (; *path != '\0'; path++) {
        hash ^= (unsigned char)tolower((unsigned char)*path);
        hash *= UINT64_C(1099511628211);
    }
    return hash;
}

static bool portable_equal(const char *left, const char *right)
{
    for (;; left++, right++) {
        if (tolower((unsigned char)*left) !=
            tolower((unsigned char)*right)) return false;
        if (*left == '\0') return true;
    }
}

static bool insert_unique_path(uint16_t hashes[WM_NAND_PATH_HASH_SLOTS],
                               WmNandEntry *entries, uint16_t index)
{
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

static bool parse_entries(WmNandReader *reader,
                          char *error, size_t error_capacity)
{
    bool *visited = calloc(WM_NAND_NODE_COUNT, sizeof(*visited));
    bool *used_clusters = calloc(WM_NAND_FIRST_SUPERBLOCK,
                                  sizeof(*used_clusters));
    uint16_t *hashes = calloc(WM_NAND_PATH_HASH_SLOTS, sizeof(*hashes));
    PendingNode *pending = calloc(WM_NAND_NODE_COUNT * 2,
                                   sizeof(*pending));
    reader->entries = calloc(WM_NAND_NODE_COUNT, sizeof(*reader->entries));
    if (visited == NULL || used_clusters == NULL || hashes == NULL ||
        pending == NULL || reader->entries == NULL) {
        set_error(error, error_capacity, "Out of memory reading the NAND filesystem.");
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
        if (index == WM_NAND_END_NODE) continue;
        if (index >= WM_NAND_NODE_COUNT || visited[index] || task.depth > 64) {
            set_error(error, error_capacity,
                      "NAND filesystem has an invalid or repeated node.");
            valid = false;
            break;
        }
        visited[index] = true;
        const uint8_t *node = reader->superblock + 0x1000c + (size_t)index * 32;
        uint8_t mode = node[12];
        uint16_t first = be16(node + 14);
        uint16_t sibling = be16(node + 16);
        bool directory = (mode & 3) == 2;
        if ((mode & 3) != 1 && !directory) {
            set_error(error, error_capacity,
                      "NAND filesystem has an unsupported node type.");
            valid = false;
            break;
        }
        if (index == 0 && (!directory || sibling != WM_NAND_END_NODE)) {
            set_error(error, error_capacity, "NAND filesystem has an invalid root.");
            valid = false;
            break;
        }
        WmNandEntry *entry = &reader->entries[index];
        entry->present = true;
        entry->directory = directory;
        memcpy(entry->name, node, sizeof(entry->name));
        entry->size = be32(node + 18);
        entry->owner = be32(node + 22);
        entry->extra = be32(node + 28);
        if (index == 0) {
            entry->path = malloc(1);
            if (entry->path != NULL) entry->path[0] = '\0';
        } else {
            size_t name_size = 0;
            if (task.parent >= WM_NAND_NODE_COUNT ||
                !reader->entries[task.parent].present ||
                !valid_component(node, &name_size)) {
                set_error(error, error_capacity,
                          "NAND filesystem contains an unsafe filename.");
                valid = false;
                break;
            }
            entry->path = join_path(reader->entries[task.parent].path,
                                    node, name_size);
            if (entry->path != NULL &&
                !insert_unique_path(hashes, reader->entries, index)) {
                set_error(error, error_capacity,
                          "NAND filesystem contains duplicate portable paths.");
                valid = false;
                break;
            }
        }
        if (entry->path == NULL) {
            set_error(error, error_capacity, "NAND path exceeds the supported limit.");
            valid = false;
            break;
        }
        if (top + 2 > WM_NAND_NODE_COUNT * 2) {
            set_error(error, error_capacity, "NAND filesystem nesting is invalid.");
            valid = false;
            break;
        }
        pending[top++] = (PendingNode){sibling, task.parent, task.depth};
        if (directory) {
            pending[top++] = (PendingNode){first, index, task.depth + 1};
        } else {
            size_t clusters = ((size_t)entry->size + WM_NAND_CLUSTER_SIZE - 1) /
                              WM_NAND_CLUSTER_SIZE;
            if (clusters > WM_NAND_FIRST_SUPERBLOCK) {
                set_error(error, error_capacity,
                          "NAND file exceeds the flash allocation limit.");
                valid = false;
                break;
            }
            if (clusters != 0) {
                entry->clusters = malloc(clusters * sizeof(*entry->clusters));
                if (entry->clusters == NULL) {
                    set_error(error, error_capacity,
                              "Out of memory reading a NAND allocation chain.");
                    valid = false;
                    break;
                }
            }
            uint16_t cluster = first;
            for (size_t ordinal = 0; ordinal < clusters; ordinal++) {
                if (cluster < 0x40 || cluster >= WM_NAND_FIRST_SUPERBLOCK ||
                    used_clusters[cluster]) {
                    set_error(error, error_capacity,
                              "NAND file has an invalid or shared cluster chain.");
                    valid = false;
                    break;
                }
                used_clusters[cluster] = true;
                entry->clusters[ordinal] = cluster;
                entry->cluster_count++;
                cluster = be16(reader->superblock + 12 + (size_t)cluster * 2);
            }
            if (!valid) break;
            if (clusters != 0 && cluster != WM_NAND_END_CLUSTER) {
                set_error(error, error_capacity,
                          "NAND file length does not match its cluster chain.");
                valid = false;
            }
        }
    }
    free(visited);
    free(used_clusters);
    free(hashes);
    free(pending);
    return valid;
}

static bool open_reader(WmNandReader *reader, const char *source_path,
                        const char *keys_path,
                        char *error, size_t error_capacity)
{
    reader->stream = fopen(source_path, "rb");
    if (reader->stream == NULL) {
        set_error(error, error_capacity, "Could not open the NAND dump.");
        return false;
    }
    uint64_t size = 0;
    if (!exact_file_size(reader->stream, &size) ||
        (size != WM_NAND_DUMP_SIZE && size != WM_NAND_DUMP_SIZE + 0x400)) {
        set_error(error, error_capacity,
                  "Expected a 512 MiB Wii BootMii dump with spare areas.");
        return false;
    }
    return read_keys(reader, source_path, keys_path, size,
                     error, error_capacity) &&
           load_superblock(reader, error, error_capacity) &&
           parse_entries(reader, error, error_capacity);
}

static bool read_file_cluster(WmNandReader *reader, const WmNandEntry *entry,
                              uint16_t node_index, size_t ordinal,
                              uint8_t data[WM_NAND_CLUSTER_SIZE],
                              char *error, size_t error_capacity)
{
    uint8_t spare[8][WM_NAND_SPARE_SIZE];
    if (ordinal >= entry->cluster_count ||
        !read_cluster(reader, entry->clusters[ordinal], data, spare)) {
        set_error(error, error_capacity, "A NAND file cluster is truncated.");
        return false;
    }
    const uint8_t zero_iv[16] = {0};
    wm_aes128_cbc_decrypt(&reader->aes, data, WM_NAND_CLUSTER_SIZE, zero_iv);
    uint8_t salt[64] = {0};
    uint8_t expected[20];
    write_be32(salt, entry->owner);
    memcpy(salt + 4, entry->name, 12);
    write_be32(salt + 16, (uint32_t)ordinal);
    write_be32(salt + 20, node_index);
    write_be32(salt + 24, entry->extra);
    hmac_sha1(reader->hmac_key, salt, data, WM_NAND_CLUSTER_SIZE, expected);
    if (!verify_spare_hmac((const uint8_t (*)[WM_NAND_SPARE_SIZE])spare,
                            expected)) {
        set_error(error, error_capacity,
                  "A NAND file failed HMAC authentication (wrong keys or damaged dump).");
        return false;
    }
    return true;
}

static bool is_hex8(const char *text)
{
    for (size_t index = 0; index < 8; index++) {
        if (!isxdigit((unsigned char)text[index])) return false;
    }
    return true;
}

static bool title_content_path(const char *path, char title[17],
                                const char **filename)
{
    if (strlen(path) <= 32 || strncmp(path, "title/", 6) != 0 ||
        !is_hex8(path + 6) || path[14] != '/' ||
        !is_hex8(path + 15) || path[23] != '/' ||
        strncmp(path + 24, "content/", 8) != 0 ||
        path[32] == '\0' || strchr(path + 32, '/') != NULL) {
        return false;
    }
    for (size_t index = 0; index < 8; index++) {
        title[index] = (char)tolower((unsigned char)path[6 + index]);
        title[8 + index] = (char)tolower((unsigned char)path[15 + index]);
    }
    title[16] = '\0';
    *filename = path + 32;
    return true;
}

static bool ends_with(const char *value, const char *suffix)
{
    size_t value_size = strlen(value);
    size_t suffix_size = strlen(suffix);
    return value_size >= suffix_size &&
           strcmp(value + value_size - suffix_size, suffix) == 0;
}

static bool title_is_selected(const char (*titles)[17], size_t count,
                               const char *candidate)
{
    for (size_t index = 0; index < count; index++) {
        if (strcmp(titles[index], candidate) == 0) return true;
    }
    return false;
}

static bool discover_channel_titles(WmNandReader *reader, size_t *title_count,
                                    char *error, size_t error_capacity)
{
    char (*titles)[17] = calloc(WM_NAND_NODE_COUNT, sizeof(*titles));
    if (titles == NULL) {
        set_error(error, error_capacity, "Out of memory discovering channel titles.");
        return false;
    }
    *title_count = 0;
    bool valid = true;
    uint8_t data[WM_NAND_CLUSTER_SIZE];
    for (size_t index = 0; index < WM_NAND_NODE_COUNT; index++) {
        WmNandEntry *entry = &reader->entries[index];
        if (!entry->present || entry->directory || entry->cluster_count == 0) {
            continue;
        }
        char title[17];
        const char *filename;
        if (!title_content_path(entry->path, title, &filename) ||
            strcmp(title, "0000000100000002") == 0 ||
            !ends_with(filename, ".app")) {
            continue;
        }
        if (!read_file_cluster(reader, entry, (uint16_t)index, 0,
                               data, error, error_capacity)) {
            valid = false;
            break;
        }
        size_t inspect = entry->size < 0xa3 ? entry->size : 0xa3;
        bool imet = false;
        for (size_t byte = 0; byte + 4 <= inspect; byte++) {
            if (memcmp(data + byte, "IMET", 4) == 0) {
                imet = true;
                break;
            }
        }
        if (imet && !title_is_selected((const char (*)[17])titles,
                                         *title_count, title)) {
            strcpy(titles[(*title_count)++], title);
        }
    }
    if (valid) {
        for (size_t index = 0; index < WM_NAND_NODE_COUNT; index++) {
            WmNandEntry *entry = &reader->entries[index];
            if (!entry->present || entry->directory) continue;
            if (strcmp(entry->path,
                       "title/00000001/00000002/data/iplsave.bin") == 0) {
                entry->selected = true;
                continue;
            }
            char title[17];
            const char *filename;
            if (title_content_path(entry->path, title, &filename) &&
                title_is_selected((const char (*)[17])titles,
                                  *title_count, title) &&
                (ends_with(filename, ".app") ||
                 strcmp(filename, "title.tmd") == 0)) {
                entry->selected = true;
            }
        }
    }
    free(titles);
    return valid;
}

typedef struct SharedFontMember {
    bool found;
    uint32_t offset;
    uint32_t size;
} SharedFontMember;

static bool shared_font_member_magic(WmNandReader *reader,
                                     const WmNandEntry *entry,
                                     uint16_t node_index,
                                     const SharedFontMember *member,
                                     bool *matches,
                                     char *error, size_t error_capacity)
{
    *matches = false;
    if (!member->found || member->size < 4 ||
        member->offset > entry->size ||
        member->size > entry->size - member->offset) {
        return true;
    }
    size_t cluster = member->offset / WM_NAND_CLUSTER_SIZE;
    size_t position = member->offset % WM_NAND_CLUSTER_SIZE;
    if (position > WM_NAND_CLUSTER_SIZE - 4) return true;
    uint8_t data[WM_NAND_CLUSTER_SIZE];
    if (!read_file_cluster(reader, entry, node_index, cluster,
                           data, error, error_capacity)) {
        return false;
    }
    *matches = memcmp(data + position, "RFNA", 4) == 0;
    return true;
}

/* A shared font archive is identified from its authenticated U8 table and
 * both RFNA member headers. Its hexadecimal shared1 filename is incidental. */
static bool shared_font_archive(WmNandReader *reader,
                                const WmNandEntry *entry, uint16_t node_index,
                                bool *matches,
                                char *error, size_t error_capacity)
{
    *matches = false;
    if (entry->cluster_count == 0 || entry->size < 0x60) return true;
    uint8_t data[WM_NAND_CLUSTER_SIZE];
    if (!read_file_cluster(reader, entry, node_index, 0,
                           data, error, error_capacity)) {
        return false;
    }
    if (memcmp(data, "U\xaa" "8-", 4) != 0) return true;
    size_t root = be32(data + 4);
    size_t header_size = be32(data + 8);
    if (root > WM_NAND_CLUSTER_SIZE - 12 ||
        header_size > WM_NAND_CLUSTER_SIZE - root) {
        return true;
    }
    size_t count = be32(data + root + 8);
    if ((be32(data + root) >> 24) != 1 || count < 3 ||
        count > (WM_NAND_CLUSTER_SIZE - root) / 12) {
        return true;
    }
    size_t names_start = root + count * 12;
    size_t names_end = root + header_size;
    if (names_start > names_end) return true;
    SharedFontMember first = {0};
    SharedFontMember second = {0};
    for (size_t index = 1; index < count; index++) {
        const uint8_t *node = data + root + index * 12;
        uint32_t kind_and_name = be32(node);
        if ((kind_and_name >> 24) != 0) continue;
        size_t name_offset = kind_and_name & 0xffffff;
        if (name_offset >= names_end - names_start) continue;
        const char *name = (const char *)(data + names_start + name_offset);
        size_t available = names_end - names_start - name_offset;
        const char *end = memchr(name, '\0', available);
        if (end == NULL) continue;
        SharedFontMember *member = NULL;
        if (strcmp(name, "wbf1.brfna") == 0) member = &first;
        if (strcmp(name, "wbf2.brfna") == 0) member = &second;
        if (member != NULL) {
            member->found = true;
            member->offset = be32(node + 4);
            member->size = be32(node + 8);
        }
    }
    bool first_magic = false;
    bool second_magic = false;
    if (!shared_font_member_magic(reader, entry, node_index, &first,
                                  &first_magic, error, error_capacity) ||
        !shared_font_member_magic(reader, entry, node_index, &second,
                                  &second_magic, error, error_capacity)) {
        return false;
    }
    *matches = first_magic && second_magic;
    return true;
}

static bool discover_shared_fonts(WmNandReader *reader, size_t *archive_count,
                                  char *error, size_t error_capacity)
{
    *archive_count = 0;
    for (size_t index = 0; index < WM_NAND_NODE_COUNT; index++) {
        WmNandEntry *entry = &reader->entries[index];
        if (!entry->present || entry->directory ||
            strncmp(entry->path, "shared1/", 8) != 0 ||
            !ends_with(entry->path, ".app") ||
            strchr(entry->path + 8, '/') != NULL) {
            continue;
        }
        bool matches = false;
        if (!shared_font_archive(reader, entry, (uint16_t)index, &matches,
                                  error, error_capacity)) {
            return false;
        }
        if (!matches) continue;
        if (*archive_count != 0) {
            set_error(error, error_capacity,
                      "NAND has multiple distinct shared font archives.");
            return false;
        }
        entry->selected = true;
        (*archive_count)++;
    }
    return true;
}

static bool ensure_directory(const char *path)
{
    if (mkdir(path, 0700) == 0) return true;
    if (errno != EEXIST) return false;
    struct stat status;
    return lstat(path, &status) == 0 && S_ISDIR(status.st_mode);
}

static bool ensure_parent_directories(char *path, size_t first_separator)
{
    for (size_t index = first_separator; path[index] != '\0'; index++) {
        if (path[index] != '/') continue;
        path[index] = '\0';
        bool valid = ensure_directory(path);
        path[index] = '/';
        if (!valid) return false;
    }
    return true;
}

static bool extract_file(WmNandReader *reader, uint16_t index,
                         const char *stage, WmNandSummary *summary,
                         char *error, size_t error_capacity)
{
    const WmNandEntry *entry = &reader->entries[index];
    char path[WM_NAND_PATH_CAPACITY];
    int length = snprintf(path, sizeof(path), "%s/%s", stage, entry->path);
    if (length < 0 || length >= (int)sizeof(path) ||
        !ensure_parent_directories(path, strlen(stage) + 1)) {
        set_error(error, error_capacity, "Could not prepare the output directory.");
        return false;
    }
    int flags = O_WRONLY | O_CREAT | O_EXCL;
#ifdef O_NOFOLLOW
    flags |= O_NOFOLLOW;
#endif
    int descriptor = open(path, flags, 0600);
    if (descriptor < 0) {
        set_error(error, error_capacity, "Could not create an extracted file.");
        return false;
    }
    FILE *output = fdopen(descriptor, "wb");
    if (output == NULL) {
        close(descriptor);
        unlink(path);
        set_error(error, error_capacity, "Could not open an extracted file.");
        return false;
    }
    uint8_t data[WM_NAND_CLUSTER_SIZE];
    size_t remaining = entry->size;
    bool valid = true;
    for (size_t ordinal = 0; ordinal < entry->cluster_count; ordinal++) {
        if (!read_file_cluster(reader, entry, index, ordinal,
                               data, error, error_capacity)) {
            valid = false;
            break;
        }
        size_t amount = remaining < WM_NAND_CLUSTER_SIZE
                            ? remaining : WM_NAND_CLUSTER_SIZE;
        if (fwrite(data, 1, amount, output) != amount) {
            set_error(error, error_capacity, "Could not write an extracted file.");
            valid = false;
            break;
        }
        remaining -= amount;
    }
    if (fclose(output) != 0) valid = false;
    if (!valid || remaining != 0) {
        unlink(path);
        if (valid) {
            set_error(error, error_capacity, "NAND file length is inconsistent.");
        }
        return false;
    }
    summary->extracted_files++;
    summary->extracted_bytes += entry->size;
    return true;
}

static void remove_tree(const char *path)
{
    struct stat status;
    if (lstat(path, &status) != 0) return;
    if (!S_ISDIR(status.st_mode)) {
        unlink(path);
        return;
    }
    DIR *directory = opendir(path);
    if (directory != NULL) {
        struct dirent *item;
        while ((item = readdir(directory)) != NULL) {
            if (strcmp(item->d_name, ".") == 0 ||
                strcmp(item->d_name, "..") == 0) {
                continue;
            }
            char child[WM_NAND_PATH_CAPACITY];
            int length = snprintf(child, sizeof(child), "%s/%s",
                                  path, item->d_name);
            if (length >= 0 && length < (int)sizeof(child)) {
                remove_tree(child);
            }
        }
        closedir(directory);
    }
    rmdir(path);
}

static bool make_stage(const char *output_directory,
                       char stage[WM_NAND_PATH_CAPACITY],
                       char *error, size_t error_capacity)
{
    struct stat status;
    if (lstat(output_directory, &status) == 0 || errno != ENOENT) {
        set_error(error, error_capacity,
                  "The NAND extraction destination must not already exist.");
        return false;
    }
    const char *slash = strrchr(output_directory, '/');
    char parent[WM_NAND_PATH_CAPACITY];
    int parent_size = slash == NULL
        ? snprintf(parent, sizeof(parent), ".")
        : slash == output_directory
            ? snprintf(parent, sizeof(parent), "/")
            : snprintf(parent, sizeof(parent), "%.*s",
                       (int)(slash - output_directory), output_directory);
    if (parent_size < 0 || parent_size >= (int)sizeof(parent) ||
        lstat(parent, &status) != 0 || !S_ISDIR(status.st_mode)) {
        set_error(error, error_capacity,
                  "The NAND extraction parent directory must exist.");
        return false;
    }
    int length = snprintf(stage, WM_NAND_PATH_CAPACITY,
                          "%s/.wm-nand-XXXXXX", parent);
    if (length < 0 || length >= WM_NAND_PATH_CAPACITY ||
        mkdtemp(stage) == NULL) {
        set_error(error, error_capacity,
                  "Could not create a private NAND extraction stage.");
        return false;
    }
    return true;
}

bool wm_nand_extract_channels(const char *source_path,
                               const char *keys_path,
                               const char *output_directory,
                               WmNandSummary *summary,
                               char *error, size_t error_capacity)
{
    if (error != NULL && error_capacity != 0) error[0] = '\0';
    if (source_path == NULL || source_path[0] == '\0' ||
        output_directory == NULL || output_directory[0] == '\0' ||
        summary == NULL) {
        set_error(error, error_capacity, "Invalid NAND extraction arguments.");
        return false;
    }
    *summary = (WmNandSummary){0};
    WmNandReader reader = {0};
    if (!open_reader(&reader, source_path, keys_path,
                     error, error_capacity)) {
        close_reader(&reader);
        return false;
    }
    summary->generation = reader.generation;
    if (!discover_channel_titles(&reader, &summary->discovered_titles,
                                  error, error_capacity)) {
        close_reader(&reader);
        return false;
    }
    if (!discover_shared_fonts(&reader, &summary->shared_font_archives,
                                error, error_capacity)) {
        close_reader(&reader);
        return false;
    }

    char stage[WM_NAND_PATH_CAPACITY];
    if (!make_stage(output_directory, stage, error, error_capacity)) {
        close_reader(&reader);
        return false;
    }
    bool valid = true;
    for (size_t index = 0; index < WM_NAND_NODE_COUNT; index++) {
        if (!reader.entries[index].selected) continue;
        if (!extract_file(&reader, (uint16_t)index, stage, summary,
                          error, error_capacity)) {
            valid = false;
            break;
        }
    }
    close_reader(&reader);
    if (valid) {
        struct stat status;
        if (lstat(output_directory, &status) == 0 || errno != ENOENT ||
            rename(stage, output_directory) != 0) {
            set_error(error, error_capacity,
                      "Could not publish the authenticated NAND extraction.");
            valid = false;
        }
    }
    if (!valid) {
        remove_tree(stage);
        *summary = (WmNandSummary){0};
    }
    return valid;
}
