#include "wii_menu/image.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

enum { WM_IMAGE_HEADER_SIZE = 16, WM_IMAGE_MAX_DIMENSION = 8192 };

static uint32_t read_u32(const uint8_t *bytes) {
    return (uint32_t)bytes[0] | ((uint32_t)bytes[1] << 8) |
           ((uint32_t)bytes[2] << 16) | ((uint32_t)bytes[3] << 24);
}

static void write_u32(uint8_t *bytes, uint32_t value) {
    bytes[0] = (uint8_t)value;
    bytes[1] = (uint8_t)(value >> 8);
    bytes[2] = (uint8_t)(value >> 16);
    bytes[3] = (uint8_t)(value >> 24);
}

static bool image_size(uint32_t width, uint32_t height, size_t *size) {
    if (!width || !height || width > WM_IMAGE_MAX_DIMENSION ||
        height > WM_IMAGE_MAX_DIMENSION) return false;
    uint64_t count = (uint64_t)width * height * 4;
    if (count > 256ULL * 1024 * 1024 || count > SIZE_MAX) return false;
    *size = (size_t)count;
    return true;
}

bool wm_image_read(const char *path, WmImage *image) {
    if (!path || !image) return false;
    memset(image, 0, sizeof(*image));
    FILE *file = fopen(path, "rb");
    if (!file) return false;
    uint8_t header[WM_IMAGE_HEADER_SIZE];
    bool valid = fread(header, 1, sizeof(header), file) == sizeof(header) &&
                 memcmp(header, "WMRA", 4) == 0 && read_u32(header + 4) == 1;
    size_t size = 0;
    if (valid) {
        image->width = read_u32(header + 8);
        image->height = read_u32(header + 12);
        valid = image_size(image->width, image->height, &size);
    }
    if (valid) {
        image->pixels = malloc(size);
        valid = image->pixels && fread(image->pixels, 1, size, file) == size &&
                fgetc(file) == EOF && !ferror(file);
    }
    fclose(file);
    if (!valid) wm_image_free(image);
    return valid;
}

bool wm_image_write(const char *path, const WmImage *image) {
    if (!path || !image || !image->pixels) return false;
    size_t size;
    if (!image_size(image->width, image->height, &size)) return false;
    uint8_t header[WM_IMAGE_HEADER_SIZE] = {'W', 'M', 'R', 'A'};
    write_u32(header + 4, 1);
    write_u32(header + 8, image->width);
    write_u32(header + 12, image->height);
    FILE *file = fopen(path, "wb");
    if (!file) return false;
    bool success = fwrite(header, 1, sizeof(header), file) == sizeof(header) &&
                   fwrite(image->pixels, 1, size, file) == size;
    if (fclose(file) != 0) success = false;
    if (!success) remove(path);
    return success;
}

void wm_image_free(WmImage *image) {
    if (!image) return;
    free(image->pixels);
    memset(image, 0, sizeof(*image));
}
