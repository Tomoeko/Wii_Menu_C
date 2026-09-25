#ifndef WII_MENU_RESOURCE_U8_H
#define WII_MENU_RESOURCE_U8_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* Entry bytes are views into the caller-owned archive buffer. The buffer must
 * outlive the parsed archive. Paths are owned by WmU8Archive. */
typedef struct WmU8Entry {
    char *path;
    const uint8_t *data;
    size_t size;
} WmU8Entry;

typedef struct WmU8Archive {
    WmU8Entry *entries;
    size_t count;
} WmU8Archive;

bool wm_u8_parse(const uint8_t *data, size_t size, WmU8Archive *archive,
                 char *error, size_t error_size);
const WmU8Entry *wm_u8_find(const WmU8Archive *archive, const char *path);
void wm_u8_free(WmU8Archive *archive);

#endif
