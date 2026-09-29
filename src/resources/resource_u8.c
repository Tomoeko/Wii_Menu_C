#include "wii_menu/resources/resource_u8.h"
#include "wii_menu/support/utf8.h"
#include "wii_menu/support/error.h"

#include "resource_bytes.h"

#include <stdlib.h>
#include <string.h>

enum { WM_U8_MAX_NODES = 100000, WM_U8_MAX_PATH = 4096 };

typedef struct WmU8Directory {
    size_t end_index;
    size_t node_index;
} WmU8Directory;

static unsigned char wm_ascii_lower(unsigned char value) {
    if (value >= 'A' && value <= 'Z') {
        return (unsigned char)(value - 'A' + 'a');
    }
    return value;
}

static bool wm_same_portable_path(const char *left, const char *right) {
    for (;; left++, right++) {
        if (wm_ascii_lower((unsigned char)*left) !=
            wm_ascii_lower((unsigned char)*right)) {
            return false;
        }
        if (*left == '\0') {
            return true;
        }
    }
}

static uint64_t wm_path_hash(const char *path) {
    uint64_t hash = UINT64_C(14695981039346656037);
    for (; *path != '\0'; path++) {
        hash ^= wm_ascii_lower((unsigned char)*path);
        hash *= UINT64_C(1099511628211);
    }
    return hash;
}

static bool wm_reserved_name(const uint8_t *name, size_t size) {
    size_t stem = 0;
    while (stem < size && name[stem] != '.') {
        stem++;
    }
    if (stem == 3) {
        static const char *const reserved[] = {"con", "prn", "aux", "nul"};
        for (size_t index = 0; index < sizeof(reserved) / sizeof(reserved[0]);
             index++) {
            if (wm_ascii_lower(name[0]) == (unsigned char)reserved[index][0] &&
                wm_ascii_lower(name[1]) == (unsigned char)reserved[index][1] &&
                wm_ascii_lower(name[2]) == (unsigned char)reserved[index][2]) {
                return true;
            }
        }
    }
    return stem == 4 &&
           ((wm_ascii_lower(name[0]) == 'c' && wm_ascii_lower(name[1]) == 'o' &&
             wm_ascii_lower(name[2]) == 'm') ||
            (wm_ascii_lower(name[0]) == 'l' && wm_ascii_lower(name[1]) == 'p' &&
             wm_ascii_lower(name[2]) == 't')) &&
           name[3] >= '1' && name[3] <= '9';
}

static bool wm_valid_name(const uint8_t *name, size_t size) {
    if (size == 0 || size > 255 || (size == 1 && name[0] == '.') ||
        (size == 2 && name[0] == '.' && name[1] == '.') || name[size - 1] == '.' ||
        name[size - 1] == ' ' || wm_reserved_name(name, size) ||
        !wm_utf8_valid(name, size)) {
        return false;
    }

    for (size_t index = 0; index < size; index++) {
        uint8_t value = name[index];
        if (value < 32 || value == 127 || strchr("/\\:*?\"<>|", value) != NULL) {
            return false;
        }
    }
    return true;
}

static char *wm_join_path(const char *parent, const uint8_t *name, size_t name_size) {
    size_t parent_size = strlen(parent);
    size_t separator = parent_size != 0 ? 1 : 0;
    if (parent_size + separator + name_size > WM_U8_MAX_PATH) {
        return NULL;
    }

    char *path = malloc(parent_size + separator + name_size + 1);
    if (path == NULL) {
        return NULL;
    }
    memcpy(path, parent, parent_size);
    if (separator != 0) {
        path[parent_size] = '/';
    }
    memcpy(path + parent_size + separator, name, name_size);
    path[parent_size + separator + name_size] = '\0';
    return path;
}

void wm_u8_free(WmU8Archive *archive) {
    if (archive == NULL) {
        return;
    }
    for (size_t index = 0; index < archive->count; index++) {
        free(archive->entries[index].path);
    }
    free(archive->entries);
    *archive = (WmU8Archive){0};
}

const WmU8Entry *wm_u8_find(const WmU8Archive *archive, const char *path) {
    if (archive == NULL || path == NULL) {
        return NULL;
    }
    for (size_t index = 0; index < archive->count; index++) {
        if (strcmp(archive->entries[index].path, path) == 0) {
            return &archive->entries[index];
        }
    }
    return NULL;
}

bool wm_u8_parse(const uint8_t *data, size_t size, WmU8Archive *archive, char *error,
                 size_t error_size) {
    if (archive == NULL || data == NULL || size < 32 ||
        memcmp(data,
               "U\xaa"
               "8-",
               4) != 0) {
        wm_error_set(error, error_size, "Expected a U8 archive.");
        return false;
    }
    *archive = (WmU8Archive){0};

    size_t root = wm_resource_be32(data + 4);
    size_t header_size = wm_resource_be32(data + 8);
    size_t data_offset = wm_resource_be32(data + 12);
    if (root < 32 || !wm_resource_range_fits(size, root, 12) ||
        !wm_resource_range_fits(size, root, header_size)) {
        wm_error_set(error, error_size, "Invalid U8 header bounds.");
        return false;
    }

    uint32_t root_kind = wm_resource_be32(data + root);
    uint32_t root_parent = wm_resource_be32(data + root + 4);
    size_t node_count = wm_resource_be32(data + root + 8);
    if ((root_kind >> 24) != 1 || root_parent != 0 || node_count == 0 ||
        node_count > WM_U8_MAX_NODES || node_count > (size - root) / 12) {
        wm_error_set(error, error_size, "Invalid U8 root node.");
        return false;
    }

    size_t names_start = root + node_count * 12;
    size_t names_end = root + header_size;
    if (names_start > names_end || names_end > data_offset || data_offset > size) {
        wm_error_set(error, error_size, "Invalid U8 archive table.");
        return false;
    }

    char **paths = calloc(node_count, sizeof(*paths));
    bool *directories = calloc(node_count, sizeof(*directories));
    WmU8Directory *stack = calloc(node_count, sizeof(*stack));
    WmU8Entry *entries = calloc(node_count, sizeof(*entries));
    size_t hash_capacity = 1;
    while (hash_capacity < node_count * 2) {
        hash_capacity *= 2;
    }
    size_t *seen = calloc(hash_capacity, sizeof(*seen));
    if (paths == NULL || directories == NULL || stack == NULL || entries == NULL ||
        seen == NULL) {
        wm_error_set(error, error_size, "Out of memory parsing U8 archive.");
        free(paths);
        free(directories);
        free(stack);
        free(entries);
        free(seen);
        return false;
    }

    size_t depth = 1;
    size_t entry_count = 0;
    stack[0] = (WmU8Directory){node_count, 0};
    directories[0] = true;
    bool valid = true;

    for (size_t index = 1; index < node_count; index++) {
        while (depth > 0 && stack[depth - 1].end_index <= index) {
            depth--;
        }
        if (depth == 0) {
            wm_error_set(error, error_size, "Invalid U8 directory boundary.");
            valid = false;
            break;
        }

        const uint8_t *node = data + root + index * 12;
        uint32_t kind_name = wm_resource_be32(node);
        uint32_t kind = kind_name >> 24;
        size_t name_offset = kind_name & 0x00ffffffu;
        size_t offset = wm_resource_be32(node + 4);
        size_t length = wm_resource_be32(node + 8);
        if ((kind != 0 && kind != 1) || name_offset >= names_end - names_start) {
            wm_error_set(error, error_size, "Invalid U8 entry.");
            valid = false;
            break;
        }

        size_t name_start = names_start + name_offset;
        size_t name_end = name_start;
        while (name_end < names_end && data[name_end] != 0) {
            name_end++;
        }
        size_t name_size = name_end - name_start;
        if (name_end == names_end || !wm_valid_name(data + name_start, name_size)) {
            wm_error_set(error, error_size, "Invalid U8 entry name.");
            valid = false;
            break;
        }

        const char *parent = paths[stack[depth - 1].node_index];
        paths[index] =
            wm_join_path(parent != NULL ? parent : "", data + name_start, name_size);
        if (paths[index] == NULL) {
            wm_error_set(error, error_size,
                         "U8 path is too long or memory is exhausted.");
            valid = false;
            break;
        }
        size_t slot = (size_t)(wm_path_hash(paths[index]) & (hash_capacity - 1));
        while (seen[slot] != 0) {
            if (wm_same_portable_path(paths[index], paths[seen[slot] - 1])) {
                wm_error_set(error, error_size, "Duplicate U8 entry path.");
                valid = false;
                break;
            }
            slot = (slot + 1) & (hash_capacity - 1);
        }
        if (!valid) {
            break;
        }
        seen[slot] = index + 1;

        if (kind == 1) {
            if (length <= index || length > stack[depth - 1].end_index) {
                wm_error_set(error, error_size, "Invalid U8 directory boundary.");
                valid = false;
                break;
            }
            directories[index] = true;
            stack[depth++] = (WmU8Directory){length, index};
        } else {
            if (!wm_resource_range_fits(size, offset, length)) {
                wm_error_set(error, error_size, "Truncated U8 file.");
                valid = false;
                break;
            }
            entries[entry_count++] = (WmU8Entry){paths[index], data + offset, length};
        }
    }

    if (valid) {
        for (size_t index = 1; index < node_count; index++) {
            if (directories[index]) {
                free(paths[index]);
            }
        }
        archive->entries = entries;
        archive->count = entry_count;
    } else {
        for (size_t index = 1; index < node_count; index++) {
            free(paths[index]);
        }
        free(entries);
    }
    free(paths);
    free(directories);
    free(stack);
    free(seen);
    return valid;
}
