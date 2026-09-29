#include "frame_damage.h"

#include <assert.h>
#include <stdio.h>

static void quad(WmFrameDamage *damage, float x, float y, float width, float height,
                 uint32_t texture) {
    WmDrawVertex vertices[4] = {{.x = x, .y = y},
                                {.x = x + width, .y = y},
                                {.x = x, .y = y + height},
                                {.x = x + width, .y = y + height}};
    assert(wm_frame_damage_quad(damage, vertices, texture, NULL));
}

static bool covers(const WmViewport *regions, size_t count, int x, int y) {
    for (size_t index = 0; index < count; index++) {
        WmViewport region = regions[index];
        if (x >= region.x && x < region.x + region.width && y >= region.y &&
            y < region.y + region.height)
            return true;
    }
    return false;
}

int main(void) {
    WmFrameDamage *damage = wm_frame_damage_create();
    assert(damage);
    WmColor clear = {0.2f, 0.3f, 0.4f, 0.5f};
    size_t count = 0;
    wm_frame_damage_begin(damage, 961, 541, clear);
    quad(damage, 20, 20, 20, 20, 1);
    quad(damage, 400, 300, 20, 20, 2);
    const WmViewport *regions = wm_frame_damage_regions(damage, &count);
    assert(count == 1 && regions[0].width == 961 && regions[0].height == 541);
    wm_frame_damage_commit(damage);

    wm_frame_damage_begin(damage, 961, 541, clear);
    quad(damage, 20, 20, 20, 20, 1);
    quad(damage, 400, 300, 20, 20, 2);
    wm_frame_damage_regions(damage, &count);
    assert(count == 0);
    wm_frame_damage_commit(damage);

    /* Inserting a command must not invalidate a distant unchanged suffix. */
    wm_frame_damage_begin(damage, 961, 541, clear);
    quad(damage, 20, 20, 20, 20, 1);
    quad(damage, 60, 20, 20, 20, 3);
    quad(damage, 400, 300, 20, 20, 2);
    regions = wm_frame_damage_regions(damage, &count);
    assert(count > 0 && covers(regions, count, 100, 30));
    assert(!covers(regions, count, 610, 370));
    wm_frame_damage_commit(damage);

    /* Moving a draw invalidates both its old pixels and its new pixels. */
    wm_frame_damage_begin(damage, 961, 541, clear);
    quad(damage, 20, 20, 20, 20, 1);
    quad(damage, 160, 20, 20, 20, 3);
    quad(damage, 400, 300, 20, 20, 2);
    regions = wm_frame_damage_regions(damage, &count);
    assert(covers(regions, count, 100, 30));
    assert(covers(regions, count, 250, 30));
    assert(!covers(regions, count, 610, 370));
    wm_frame_damage_commit(damage);

    wm_frame_damage_begin(damage, 961, 541, clear);
    regions = wm_frame_damage_regions(damage, &count);
    assert(covers(regions, count, 40, 30));
    assert(covers(regions, count, 250, 30));
    assert(covers(regions, count, 610, 370));
    wm_frame_damage_commit(damage);

    wm_frame_damage_begin(damage, 960, 540, clear);
    regions = wm_frame_damage_regions(damage, &count);
    assert(count == 1 && regions[0].width == 960);
    wm_frame_damage_commit(damage);
    wm_frame_damage_begin(damage, 960, 540, (WmColor){0});
    wm_frame_damage_regions(damage, &count);
    assert(count == 1);

    WmDrawVertex vertices[4] = {0};
    for (size_t index = 0; index < WM_FRAME_COMMAND_CAPACITY; index++)
        assert(wm_frame_damage_quad(damage, vertices, 0, NULL));
    assert(!wm_frame_damage_quad(damage, vertices, 0, NULL));
    wm_frame_damage_destroy(damage);
    puts("Frame damage tests passed.");
    return 0;
}
