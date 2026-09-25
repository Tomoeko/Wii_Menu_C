#include "../tools/settings/png.h"

#include <assert.h>
#include <stdio.h>
#include <string.h>

static void test_rgba_and_integrity(void) {
    static const uint8_t png[] = {
        0x89, 'P', 'N', 'G', 13, 10, 26, 10,
        0, 0, 0, 13, 'I', 'H', 'D', 'R',
        0, 0, 0, 2, 0, 0, 0, 2, 8, 6, 0, 0, 0,
        0x72, 0xb6, 0x0d, 0x24,
        0, 0, 0, 23, 'I', 'D', 'A', 'T',
        0x78, 0x9c, 0x05, 0xc1, 0x01, 0x01, 0, 0, 0,
        0x82, 0x20, 0xa6, 0xf7, 0xdc, 0x40, 0x24,
        0x43, 0xc1, 0x01, 0x3a, 0xdc, 0x05, 0x7c,
        0xf2, 0x4a, 0x44, 0x5b,
        0, 0, 0, 0, 'I', 'E', 'N', 'D',
        0xae, 0x42, 0x60, 0x82
    };
    WmImage image = {0};
    assert(wm_settings_png_decode(png, sizeof(png), &image));
    assert(image.width == 2 && image.height == 2);
    static const uint8_t expected[] = {
        255, 0, 0, 255, 0, 255, 0, 128,
        0, 0, 255, 255, 0, 0, 0, 0
    };
    assert(memcmp(image.pixels, expected, sizeof(expected)) == 0);
    wm_image_free(&image);

    assert(!wm_settings_png_decode(png, sizeof(png) - 1, &image));
    uint8_t corrupted[sizeof(png)];
    memcpy(corrupted, png, sizeof(png));
    corrupted[42] ^= 1;
    assert(!wm_settings_png_decode(corrupted, sizeof(corrupted), &image));
    assert(!wm_settings_png_decode(png, 32, &image));
}

int main(void) {
    test_rgba_and_integrity();
    puts("Settings PNG decoding passed.");
    return 0;
}
