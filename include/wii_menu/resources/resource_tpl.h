#ifndef WII_MENU_RESOURCE_TPL_H
#define WII_MENU_RESOURCE_TPL_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* RGBA8 rows begin at the image's top edge, ready for upload to the graphics
 * backend. The decoded TPL owns each image's pixels. */
typedef struct WmTplImage {
    uint16_t width;
    uint16_t height;
    uint32_t format;
    uint8_t *rgba;
} WmTplImage;

typedef struct WmTpl {
    WmTplImage *images;
    size_t count;
} WmTpl;

bool wm_tpl_decode(const uint8_t *data, size_t size, WmTpl *tpl,
                   char *error, size_t error_size);
void wm_tpl_free(WmTpl *tpl);

#endif
