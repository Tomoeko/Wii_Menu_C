#include "wii_menu/resource_scene.h"

#include <assert.h>
#include <math.h>
#include <stdio.h>

/* This test only traverses layouts. These stubs satisfy the renderer symbols
 * in the scene object's link unit and fail if drawing enters this test. */
void wm_platform_begin(WmPlatform *platform, WmColor clear_color)
{
    (void)platform;
    (void)clear_color;
    assert(false);
}

void wm_platform_set_clip(WmPlatform *platform, const WmClipRect *rect)
{
    (void)platform;
    (void)rect;
    assert(false);
}

void wm_platform_draw_quad(WmPlatform *platform, const WmQuad *quad)
{
    (void)platform;
    (void)quad;
    assert(false);
}

void wm_platform_draw_vertices(WmPlatform *platform,
                               const WmDrawVertex vertices[4], uint32_t texture)
{
    (void)platform;
    (void)vertices;
    (void)texture;
    assert(false);
}

void wm_platform_draw_material_quad(WmPlatform *platform,
                                    const WmMaterialQuad *quad)
{
    (void)platform;
    (void)quad;
    assert(false);
}

void wm_platform_prepare_material(WmPlatform *platform,
                                  const WmMaterialQuad *quad)
{
    (void)platform;
    (void)quad;
    assert(false);
}

void wm_platform_end(WmPlatform *platform)
{
    (void)platform;
    assert(false);
}

uint32_t wm_platform_create_texture(WmPlatform *platform, int width, int height,
                                    const uint8_t *rgba)
{
    (void)platform;
    (void)width;
    (void)height;
    (void)rgba;
    assert(false);
    return 0;
}

uint32_t wm_platform_create_render_texture(WmPlatform *platform)
{
    (void)platform;
    assert(false);
    return 0;
}

bool wm_platform_begin_target(WmPlatform *platform, uint32_t texture,
                              WmColor clear_color)
{
    (void)platform;
    (void)texture;
    (void)clear_color;
    assert(false);
    return false;
}

void wm_platform_destroy_texture(WmPlatform *platform, uint32_t texture)
{
    (void)platform;
    (void)texture;
    assert(false);
}

static bool near(float actual, float expected)
{
    return fabsf(actual - expected) < 0.02f;
}

static void expect_tile(const WmGridTile *tile, int slot, int page_offset)
{
    assert(tile->slot == slot);
    assert(tile->page_offset == page_offset);
    assert(near(tile->clip.width, 170.0f * 640.0f / 832.0f));
    assert(near(tile->clip.height, 96.0f));
}

int main(void)
{
    char error[160] = {0};
    WmLayout *grid = wm_layout_load_json("tests/grid_tiles_fixture.json",
                                          error, sizeof(error));
    assert(grid != NULL);
    WmGridTile tiles[5 * WM_CHANNELS_PER_PAGE];

    size_t count = wm_resource_scene_collect_tiles(grid, 0, tiles,
                                                     sizeof(tiles) /
                                                         sizeof(tiles[0]));
    assert(count == 2);
    expect_tile(&tiles[0], 0, 0);
    expect_tile(&tiles[1], 12, 1);
    assert(tiles[1].clip.x < WM_FRAME_WIDTH);
    assert(tiles[1].clip.x + tiles[1].clip.width > WM_FRAME_WIDTH);

    count = wm_resource_scene_collect_tiles(grid, 1, tiles,
                                             sizeof(tiles) / sizeof(tiles[0]));
    assert(count == 3);
    expect_tile(&tiles[0], 3, -1);
    expect_tile(&tiles[1], 12, 0);
    expect_tile(&tiles[2], 24, 1);
    assert(tiles[0].clip.x < 0.0f);
    assert(tiles[0].clip.x + tiles[0].clip.width > 0.0f);

    count = wm_resource_scene_collect_tiles(grid, 3, tiles,
                                             sizeof(tiles) / sizeof(tiles[0]));
    assert(count == 2);
    expect_tile(&tiles[0], 27, -1);
    expect_tile(&tiles[1], 36, 0);
    assert(wm_resource_scene_collect_tiles(grid, -1, tiles, 60) == 0);
    assert(wm_resource_scene_collect_tiles(grid, 4, tiles, 60) == 0);
    assert(wm_resource_scene_collect_tiles(grid, 1, NULL, 0) == 3);

    wm_layout_destroy(grid);
    puts("Neighboring grid tile tests passed.");
    return 0;
}
