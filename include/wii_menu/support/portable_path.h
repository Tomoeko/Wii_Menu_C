#ifndef WII_MENU_SUPPORT_PORTABLE_PATH_H
#define WII_MENU_SUPPORT_PORTABLE_PATH_H

#include "wii_menu/support/ascii.h"

#include <stddef.h>
#include <stdlib.h>
#include <string.h>

/* A component is a byte span; it does not need a NUL terminator. */
static inline bool wm_path_reserved_component(const uint8_t *name, size_t size) {
    size_t stem = 0;
    while (stem < size && name[stem] != '.')
        stem++;
    if (stem != 3 && stem != 4)
        return false;

    unsigned char first = wm_ascii_lower(name[0]);
    unsigned char second = wm_ascii_lower(name[1]);
    unsigned char third = wm_ascii_lower(name[2]);
    if (stem == 3) {
        static const char *const reserved[] = {"con", "prn", "aux", "nul"};
        for (size_t index = 0; index < sizeof(reserved) / sizeof(reserved[0]);
             index++) {
            if (first == (unsigned char)reserved[index][0] &&
                second == (unsigned char)reserved[index][1] &&
                third == (unsigned char)reserved[index][2])
                return true;
        }
        return false;
    }
    bool port = (first == 'c' && second == 'o' && third == 'm') ||
                (first == 'l' && second == 'p' && third == 't');
    return port && name[3] >= '1' && name[3] <= '9';
}

/* Join an already validated component. byte_capacity includes the terminator;
 * the caller owns the returned path and releases it with free(). */
static inline char *wm_path_join_component(const char *parent, const uint8_t *name,
                                           size_t size, size_t byte_capacity) {
    size_t parent_size = strlen(parent);
    size_t separator = parent_size != 0 ? 1 : 0;
    if (parent_size >= byte_capacity || separator > byte_capacity - parent_size - 1 ||
        size > byte_capacity - parent_size - separator - 1)
        return NULL;

    size_t length = parent_size + separator + size;
    char *path = malloc(length + 1);
    if (!path)
        return NULL;
    memcpy(path, parent, parent_size);
    if (separator)
        path[parent_size] = '/';
    if (size)
        memcpy(path + parent_size + separator, name, size);
    path[length] = '\0';
    return path;
}

#endif
