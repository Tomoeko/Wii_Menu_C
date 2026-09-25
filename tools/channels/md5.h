#ifndef WM_CHANNEL_MD5_H
#define WM_CHANNEL_MD5_H

#include <stddef.h>
#include <stdint.h>

typedef struct WmMd5 {
    uint32_t state[4];
    uint64_t length;
    uint8_t pending[64];
    size_t pending_count;
} WmMd5;

void wm_md5_init(WmMd5 *md5);
void wm_md5_update(WmMd5 *md5, const uint8_t *data, size_t size);
void wm_md5_final(WmMd5 *md5, uint8_t digest[16]);

#endif
