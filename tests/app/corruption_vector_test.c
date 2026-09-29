#include "corruption_outline.h"
#include "corruption_vectors.h"

#include "wii_menu/fonts/outline_font.h"

#include <assert.h>
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

struct WmPlatform {
    unsigned marker;
};

static unsigned created;
static unsigned destroyed;
static unsigned drawn;

uint32_t wm_platform_create_texture(WmPlatform *platform, int width, int height,
                                    const uint8_t *rgba) {
    assert(platform && rgba && width == 2048 && height == 128);
    unsigned nonempty = 0;
    for (size_t pixel = 0; pixel < (size_t)width * height; pixel++) {
        if (rgba[pixel * 4 + 3])
            nonempty++;
    }
    assert(nonempty > 1000);
    return ++created;
}

void wm_platform_destroy_texture(WmPlatform *platform, uint32_t texture) {
    assert(platform && texture);
    destroyed++;
}

void wm_platform_draw_quad(WmPlatform *platform, const WmQuad *quad) {
    assert(platform && quad && quad->texture && quad->width > 0);
    drawn++;
}

static void compare_with_source(const char *path, const char *message) {
    WmOutlineFont *source = wm_outline_font_load(path, 0);
    assert(source);
    bool compared[128] = {0};
    for (const unsigned char *cursor = (const unsigned char *)message; *cursor;
         cursor++) {
        unsigned character = *cursor;
        if (character == '\n' || compared[character])
            continue;
        compared[character] = true;
        WmOutlineBitmap embedded = {0};
        WmOutlineBitmap original = {0};
        assert(wm_corruption_vector_raster(character, 94, &embedded));
        assert(wm_outline_font_raster(source, character, 94, &original));
        assert(embedded.left == original.left);
        assert(embedded.top == original.top);
        assert(embedded.width == original.width);
        assert(embedded.height == original.height);
        assert(fabsf(embedded.advance - original.advance) < 0.001f);
        size_t pixels = (size_t)embedded.width * embedded.height;
        if (pixels)
            assert(memcmp(embedded.alpha, original.alpha, pixels) == 0);
        wm_outline_bitmap_free(&embedded);
        wm_outline_bitmap_free(&original);
    }
    wm_outline_font_destroy(source, NULL);
}

int main(void) {
    const char message[] = "The system files are corrupted. \n"
                           "Please refer to the Wii Operations Manual\n"
                           "for help troubleshooting.";
    struct WmPlatform platform = {1};
    WmCorruptionOutline outline;
    assert(wm_corruption_outline_create(&outline, &platform, NULL, message));
    assert(created == 3);
    wm_corruption_outline_draw(&outline, &platform);
    wm_corruption_outline_draw(&outline, &platform);
    assert(drawn == 6 && created == 3);
    wm_corruption_outline_destroy(&outline, &platform);
    assert(destroyed == 3);

    const char *source = getenv("WM_CFF_TEST_FONT");
    if (source && source[0])
        compare_with_source(source, message);
    puts("In-code prompt contours and cached draw path passed.");
    return 0;
}
