#ifndef WII_MENU_IMAGE_H
#define WII_MENU_IMAGE_H

#include <stdbool.h>
#include <stdint.h>

/* Versioned, backend-neutral RGBA8 resource. Generated files belong in the
 * ignored local asset directory, never in tracked source. */
typedef struct WmImage {
    uint32_t width;
    uint32_t height;
    uint8_t *pixels;
} WmImage;

bool wm_image_read(const char *path, WmImage *image);
bool wm_image_write(const char *path, const WmImage *image);
void wm_image_free(WmImage *image);

#endif
