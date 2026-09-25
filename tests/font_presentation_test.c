#define _POSIX_C_SOURCE 200809L
#define _DARWIN_C_SOURCE 1

#include "wii_menu/font_cache.h"
#include "wii_menu/layout_present.h"

#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

enum {
    TEST_FONT_SIZE = 153,
    FINF = 16,
    TGLP = 48,
    CWDH = 112,
    CMAP = 131
};

struct WmPlatform {
    unsigned uploads;
    unsigned destructions;
    unsigned draws;
    uint32_t last_texture;
    WmDrawVertex first_vertices[4];
    float drawn_red[32];
};

static void write_be16(uint8_t *target, uint16_t value) {
    target[0] = (uint8_t)(value >> 8);
    target[1] = (uint8_t)value;
}

static void write_be32(uint8_t *target, uint32_t value) {
    target[0] = (uint8_t)(value >> 24);
    target[1] = (uint8_t)(value >> 16);
    target[2] = (uint8_t)(value >> 8);
    target[3] = (uint8_t)value;
}

static void make_font(uint8_t bytes[TEST_FONT_SIZE]) {
    memset(bytes, 0, TEST_FONT_SIZE);
    memcpy(bytes, "RFNT", 4);
    bytes[4] = 0xfe;
    bytes[5] = 0xff;
    write_be32(bytes + 8, TEST_FONT_SIZE);
    write_be16(bytes + 12, 16);
    write_be16(bytes + 14, 4);

    memcpy(bytes + FINF, "FINF", 4);
    write_be32(bytes + FINF + 4, 32);
    bytes[FINF + 9] = 4;
    bytes[FINF + 15] = 1;
    write_be32(bytes + FINF + 16, TGLP + 8);
    write_be32(bytes + FINF + 20, CWDH + 8);
    write_be32(bytes + FINF + 24, CMAP + 8);
    bytes[FINF + 28] = 4;
    bytes[FINF + 29] = 4;
    bytes[FINF + 30] = 3;

    memcpy(bytes + TGLP, "TGLP", 4);
    write_be32(bytes + TGLP + 4, 64);
    bytes[TGLP + 8] = 3;
    bytes[TGLP + 9] = 3;
    bytes[TGLP + 10] = 2;
    bytes[TGLP + 11] = 3;
    write_be32(bytes + TGLP + 12, 32);
    write_be16(bytes + TGLP + 16, 1);
    write_be16(bytes + TGLP + 18, 0);
    write_be16(bytes + TGLP + 20, 1);
    write_be16(bytes + TGLP + 22, 1);
    write_be16(bytes + TGLP + 24, 8);
    write_be16(bytes + TGLP + 26, 8);
    write_be32(bytes + TGLP + 28, TGLP + 32);
    memset(bytes + TGLP + 32, 0xff, 32);
    bytes[TGLP + 32] = 0x0f;

    memcpy(bytes + CWDH, "CWDH", 4);
    write_be32(bytes + CWDH + 4, 19);
    bytes[CWDH + 16] = 1;
    bytes[CWDH + 17] = 2;
    bytes[CWDH + 18] = 3;

    memcpy(bytes + CMAP, "CMAP", 4);
    write_be32(bytes + CMAP + 4, 22);
    write_be16(bytes + CMAP + 8, 'A');
    write_be16(bytes + CMAP + 10, 'A');
    write_be16(bytes + CMAP + 12, 0);
}

uint32_t wm_platform_create_texture(WmPlatform *platform, int width, int height,
                                    const uint8_t *rgba) {
    assert(platform && width == 8 && height == 8 && rgba);
    assert(rgba[0] == 255 && rgba[1] == 255 && rgba[2] == 255 && rgba[3] == 0);
    assert(rgba[4] == 255 && rgba[5] == 255 && rgba[6] == 255 && rgba[7] == 255);
    return 100 + ++platform->uploads;
}

void wm_platform_destroy_texture(WmPlatform *platform, uint32_t texture) {
    assert(platform && texture >= 101);
    platform->destructions++;
}

void wm_platform_draw_vertices(WmPlatform *platform,
                               const WmDrawVertex vertices[4], uint32_t texture) {
    assert(platform && vertices && texture >= 101);
    if (platform->draws == 0) {
        memcpy(platform->first_vertices, vertices, sizeof(platform->first_vertices));
    }
    if (platform->draws < 32)
        platform->drawn_red[platform->draws] = vertices[0].color.r;
    platform->last_texture = texture;
    platform->draws++;
}

void wm_platform_draw_material_quad(WmPlatform *platform,
                                    const WmMaterialQuad *quad) {
    (void)platform;
    (void)quad;
    assert(false);
}

static bool near(float actual, float expected) {
    return fabsf(actual - expected) < 0.0001f;
}

static void write_fixture_font(const char *path) {
    uint8_t bytes[TEST_FONT_SIZE];
    make_font(bytes);
    FILE *file = fopen(path, "wb");
    assert(file);
    assert(fwrite(bytes, 1, sizeof(bytes), file) == sizeof(bytes));
    assert(fclose(file) == 0);
}

int main(void) {
    const char *temporary = getenv("TMPDIR");
    if (!temporary || !temporary[0]) temporary = "/tmp";
    char root[1024];
    int length = snprintf(root, sizeof(root), "%s/wm-font-test-XXXXXX", temporary);
    assert(length > 0 && (size_t)length < sizeof(root));
    assert(mkdtemp(root));
    char directory[1152];
    length = snprintf(directory, sizeof(directory), "%s/fonts", root);
    assert(length > 0 && (size_t)length < sizeof(directory));
    assert(mkdir(directory, 0700) == 0);
    char first_path[1280], default_path[1280];
    length = snprintf(first_path, sizeof(first_path), "%s/Second.brfnt", directory);
    assert(length > 0 && (size_t)length < sizeof(first_path));
    length = snprintf(default_path, sizeof(default_path),
                      "%s/RevoIpl_RodinNTLGPro_DB_48_IA4.brfnt", directory);
    assert(length > 0 && (size_t)length < sizeof(default_path));
    write_fixture_font(first_path);
    write_fixture_font(default_path);

    WmPlatform platform = {0};
    WmFontCache *fonts = wm_font_cache_create(&platform, root, 8 * 8 * 4);
    assert(fonts);
    char error[128];
    WmLayout *layout = wm_layout_load_json("tests/layout_text_fixture.json",
                                            error, sizeof(error));
    assert(layout);
    wm_font_cache_begin_frame(fonts);
    wm_layout_present_with_fonts(&platform, NULL, fonts, layout, false,
                                  WM_LAYOUT_LOCAL, NULL);
    assert(platform.draws == 4 && platform.uploads == 1);
    assert(platform.last_texture == 101);
    assert(near(platform.first_vertices[0].color.r,
                (12.0f / 255) * (128.0f / 255)));
    assert(near(platform.first_vertices[0].color.g,
                (34.0f / 255) * (64.0f / 255)));
    assert(near(platform.first_vertices[0].color.a,
                (78.0f / 255) * (128.0f / 255) * (200.0f / 255)));
    assert(near(platform.first_vertices[2].color.r,
                (90.0f / 255) * (128.0f / 255)));
    assert(wm_font_cache_stats(fonts).cached_layouts == 1);
    assert(wm_font_cache_stats(fonts).resident_sheets == 1);

    wm_font_cache_begin_frame(fonts);
    wm_layout_present_with_fonts(&platform, NULL, fonts, layout, false,
                                  WM_LAYOUT_LOCAL, NULL);
    assert(platform.draws == 8 && platform.uploads == 1);
    assert(wm_font_cache_stats(fonts).cached_layouts == 1);
    assert(wm_layout_pose(layout, NULL, 0));
    assert(wm_layout_set_pose_text(layout, "Label", "A\xc3\xa9" "A"));
    WmLayoutTextColorRange highlight = {
        .first_byte = 1,
        .end_byte = 3,
        .rgba = {255, 50, 50, 255}
    };
    assert(wm_layout_set_pose_text_colors(layout, "Label", &highlight, 1));
    wm_layout_present_with_fonts(&platform, NULL, fonts, layout, false,
                                  WM_LAYOUT_LOCAL, NULL);
    assert(platform.draws == 11);
    assert(near(platform.drawn_red[8], (12.0f / 255) * (128.0f / 255)));
    assert(near(platform.drawn_red[9], 128.0f / 255));
    assert(near(platform.drawn_red[10], (12.0f / 255) * (128.0f / 255)));
    assert(wm_layout_pose(layout, NULL, 0));
    assert(wm_layout_set_pose_text(layout, "Label", "AA"));
    wm_layout_present_with_fonts(&platform, NULL, fonts, layout, false,
                                  WM_LAYOUT_LOCAL, NULL);
    assert(platform.draws == 13);
    assert(near(platform.drawn_red[12], (12.0f / 255) * (128.0f / 255)));
    WmCachedFont *face = wm_font_cache_resolve(fonts, "Second.brfnt");
    assert(face && wm_cached_font_resource(face));
    WmFontPane pane = {
        .size = {120, 40},
        .origin = 4,
        .text_position = 8,
        .font_size = {24, 28},
        .char_space = 1.5f,
        .line_space = 2.5f,
        .no_wrap = true
    };
    const char *value = "A\xc3\xa9\xf0\x9f\x98\x80\nB";
    const WmFontTextLayout *cached = wm_font_cache_layout(face, value, &pane);
    pane.top_color[0] = 222;
    assert(wm_font_cache_layout(face, value, &pane) == cached);
    assert(wm_font_cache_resolve(fonts, "Missing.brfnt"));
    assert(wm_font_cache_stats(fonts).loaded_fonts == 2);
    assert(wm_font_cache_resolve(fonts, "../outside.brfnt"));

    wm_layout_destroy(layout);
    wm_font_cache_destroy(fonts);
    assert(platform.destructions == platform.uploads);
    assert(remove(first_path) == 0);
    assert(remove(default_path) == 0);
    assert(rmdir(directory) == 0);
    assert(rmdir(root) == 0);
    return 0;
}
