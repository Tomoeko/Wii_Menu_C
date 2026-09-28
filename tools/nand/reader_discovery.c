#include "reader_internal.h"

#include <ctype.h>
#include <stdlib.h>
#include <string.h>

static bool is_hex8(const char *text) {
    for (size_t index = 0; index < 8; index++) {
        if (!isxdigit((unsigned char)text[index]))
            return false;
    }
    return true;
}

static bool title_content_path(const char *path, char title[17],
                               const char **filename) {
    if (strlen(path) <= 32 || strncmp(path, "title/", 6) != 0 || !is_hex8(path + 6) ||
        path[14] != '/' || !is_hex8(path + 15) || path[23] != '/' ||
        strncmp(path + 24, "content/", 8) != 0 || path[32] == '\0' ||
        strchr(path + 32, '/') != NULL) {
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

static bool ends_with(const char *value, const char *suffix) {
    size_t value_size = strlen(value);
    size_t suffix_size = strlen(suffix);
    return value_size >= suffix_size &&
           strcmp(value + value_size - suffix_size, suffix) == 0;
}

static bool title_is_selected(const char (*titles)[17], size_t count,
                              const char *candidate) {
    for (size_t index = 0; index < count; index++) {
        if (strcmp(titles[index], candidate) == 0)
            return true;
    }
    return false;
}

bool wm_nand_discover_channel_titles(WmNandReader *reader, size_t *title_count,
                                     char *error, size_t error_capacity) {
    char(*titles)[17] = calloc(WM_NAND_NODE_COUNT, sizeof(*titles));
    if (titles == NULL) {
        wm_nand_set_error(error, error_capacity,
                          "Out of memory discovering channel titles.");
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
            strcmp(title, "0000000100000002") == 0 || !ends_with(filename, ".app")) {
            continue;
        }
        if (!wm_nand_read_file_cluster(reader, entry, (uint16_t)index, 0, data, error,
                                       error_capacity)) {
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
        if (imet &&
            !title_is_selected((const char(*)[17])titles, *title_count, title)) {
            strcpy(titles[(*title_count)++], title);
        }
    }
    if (valid) {
        for (size_t index = 0; index < WM_NAND_NODE_COUNT; index++) {
            WmNandEntry *entry = &reader->entries[index];
            if (!entry->present || entry->directory)
                continue;
            if (strcmp(entry->path, "title/00000001/00000002/data/iplsave.bin") == 0) {
                entry->selected = true;
                continue;
            }
            char title[17];
            const char *filename;
            if (title_content_path(entry->path, title, &filename) &&
                title_is_selected((const char(*)[17])titles, *title_count, title) &&
                (ends_with(filename, ".app") || strcmp(filename, "title.tmd") == 0)) {
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

static bool shared_font_member_magic(WmNandReader *reader, const WmNandEntry *entry,
                                     uint16_t node_index,
                                     const SharedFontMember *member, bool *matches,
                                     char *error, size_t error_capacity) {
    *matches = false;
    if (!member->found || member->size < 4 || member->offset > entry->size ||
        member->size > entry->size - member->offset) {
        return true;
    }
    size_t cluster = member->offset / WM_NAND_CLUSTER_SIZE;
    size_t position = member->offset % WM_NAND_CLUSTER_SIZE;
    if (position > WM_NAND_CLUSTER_SIZE - 4)
        return true;
    uint8_t data[WM_NAND_CLUSTER_SIZE];
    if (!wm_nand_read_file_cluster(reader, entry, node_index, cluster, data, error,
                                   error_capacity)) {
        return false;
    }
    *matches = memcmp(data + position, "RFNA", 4) == 0;
    return true;
}

/* A shared font archive is identified from its authenticated U8 table and
 * both RFNA member headers. Its hexadecimal shared1 filename is incidental. */
static bool shared_font_archive(WmNandReader *reader, const WmNandEntry *entry,
                                uint16_t node_index, bool *matches, char *error,
                                size_t error_capacity) {
    *matches = false;
    if (entry->cluster_count == 0 || entry->size < 0x60)
        return true;
    uint8_t data[WM_NAND_CLUSTER_SIZE];
    if (!wm_nand_read_file_cluster(reader, entry, node_index, 0, data, error,
                                   error_capacity)) {
        return false;
    }
    if (memcmp(data,
               "U\xaa"
               "8-",
               4) != 0)
        return true;
    size_t root = wm_nand_be32(data + 4);
    size_t header_size = wm_nand_be32(data + 8);
    if (root > WM_NAND_CLUSTER_SIZE - 12 || header_size > WM_NAND_CLUSTER_SIZE - root) {
        return true;
    }
    size_t count = wm_nand_be32(data + root + 8);
    if ((wm_nand_be32(data + root) >> 24) != 1 || count < 3 ||
        count > (WM_NAND_CLUSTER_SIZE - root) / 12) {
        return true;
    }
    size_t names_start = root + count * 12;
    size_t names_end = root + header_size;
    if (names_start > names_end)
        return true;
    SharedFontMember first = {0};
    SharedFontMember second = {0};
    for (size_t index = 1; index < count; index++) {
        const uint8_t *node = data + root + index * 12;
        uint32_t kind_and_name = wm_nand_be32(node);
        if ((kind_and_name >> 24) != 0)
            continue;
        size_t name_offset = kind_and_name & 0xffffff;
        if (name_offset >= names_end - names_start)
            continue;
        const char *name = (const char *)(data + names_start + name_offset);
        size_t available = names_end - names_start - name_offset;
        const char *end = memchr(name, '\0', available);
        if (end == NULL)
            continue;
        SharedFontMember *member = NULL;
        if (strcmp(name, "wbf1.brfna") == 0)
            member = &first;
        if (strcmp(name, "wbf2.brfna") == 0)
            member = &second;
        if (member != NULL) {
            member->found = true;
            member->offset = wm_nand_be32(node + 4);
            member->size = wm_nand_be32(node + 8);
        }
    }
    bool first_magic = false;
    bool second_magic = false;
    if (!shared_font_member_magic(reader, entry, node_index, &first, &first_magic,
                                  error, error_capacity) ||
        !shared_font_member_magic(reader, entry, node_index, &second, &second_magic,
                                  error, error_capacity)) {
        return false;
    }
    *matches = first_magic && second_magic;
    return true;
}

bool wm_nand_discover_shared_fonts(WmNandReader *reader, size_t *archive_count,
                                   char *error, size_t error_capacity) {
    *archive_count = 0;
    for (size_t index = 0; index < WM_NAND_NODE_COUNT; index++) {
        WmNandEntry *entry = &reader->entries[index];
        if (!entry->present || entry->directory ||
            strncmp(entry->path, "shared1/", 8) != 0 ||
            !ends_with(entry->path, ".app") || strchr(entry->path + 8, '/') != NULL) {
            continue;
        }
        bool matches = false;
        if (!shared_font_archive(reader, entry, (uint16_t)index, &matches, error,
                                 error_capacity)) {
            return false;
        }
        if (!matches)
            continue;
        if (*archive_count != 0) {
            wm_nand_set_error(error, error_capacity,
                              "NAND has multiple distinct shared font archives.");
            return false;
        }
        entry->selected = true;
        (*archive_count)++;
    }
    return true;
}
