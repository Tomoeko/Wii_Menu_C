#include "../tools/settings/gif.h"

#include <assert.h>
#include <stdio.h>
#include <string.h>

static void test_palette_and_bounds(void) {
    static const uint8_t pixels[] = {
        'G', 'I', 'F', '8', '7', 'a', 2, 0, 2, 0, 0x81, 0, 0,
        255, 0, 0, 0, 255, 0, 0, 0, 255, 0, 0, 0,
        0x2c, 0, 0, 0, 0, 2, 0, 2, 0, 0,
        8, 7, 0, 1, 4, 16, 48, 32, 32, 0, 0x3b
    };
    WmImage image = {0};
    assert(wm_settings_gif_decode(pixels, sizeof(pixels), &image));
    assert(image.width == 2 && image.height == 2);
    static const uint8_t expected[] = {
        255, 0, 0, 255, 0, 255, 0, 255,
        0, 0, 255, 255, 0, 0, 0, 255
    };
    assert(memcmp(image.pixels, expected, sizeof(expected)) == 0);
    wm_image_free(&image);

    assert(!wm_settings_gif_decode(pixels, sizeof(pixels) - 1, &image));
    assert(!wm_settings_gif_decode(pixels, 12, &image));
    uint8_t malformed[sizeof(pixels)];
    memcpy(malformed, pixels, sizeof(pixels));
    malformed[6] = 0;
    assert(!wm_settings_gif_decode(malformed, sizeof(malformed), &image));
}

int main(void) {
    test_palette_and_bounds();
    puts("Settings GIF decoding passed.");
    return 0;
}
