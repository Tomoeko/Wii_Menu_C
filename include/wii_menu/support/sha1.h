#ifndef WII_MENU_SUPPORT_SHA1_H
#define WII_MENU_SUPPORT_SHA1_H

#include <stddef.h>
#include <stdint.h>

typedef struct WmSha1 {
    uint32_t words[5];
    uint64_t byte_count;
    uint8_t pending[64];
    size_t pending_count;
} WmSha1;

void wm_sha1_init(WmSha1 *sha1);
void wm_sha1_update(WmSha1 *sha1, const uint8_t *data, size_t length);
void wm_sha1_final(WmSha1 *sha1, uint8_t digest[20]);

#endif
