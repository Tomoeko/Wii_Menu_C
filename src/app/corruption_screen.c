#define _POSIX_C_SOURCE 200809L

#include "corruption_screen.h"
#include "corruption_outline.h"

#include "wii_menu/fonts/font_cache.h"
#include "wii_menu/platform/platform.h"
#include "wii_menu/resources/resource_bmg.h"
#include "wii_menu/resources/resource_font.h"

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

/* English ipl_common.bmg message 12 in the prepared System Menu WAD. The
 * identical fallback is necessary when that BMG is itself missing or corrupt. */
static const char CORRUPTION_MESSAGE[] =
    "The system files are corrupted. \n"
    "Please refer to the Wii Operations Manual\n"
    "for help troubleshooting.";

typedef struct ScreenFont {
    WmPlatform *platform;
    WmCachedFont *face;
} ScreenFont;

typedef struct BitmapGlyph {
    char character;
    uint8_t rows[7];
} BitmapGlyph;

/* First-party emergency glyphs cover the WAD message when every extracted
 * font is unavailable. Each row uses the low five bits, left to right. */
static const BitmapGlyph EMERGENCY_GLYPHS[] = {
    {'T', {0x1f, 0x04, 0x04, 0x04, 0x04, 0x04, 0x04}},
    {'P', {0x1e, 0x11, 0x11, 0x1e, 0x10, 0x10, 0x10}},
    {'W', {0x11, 0x11, 0x11, 0x15, 0x15, 0x15, 0x0a}},
    {'O', {0x0e, 0x11, 0x11, 0x11, 0x11, 0x11, 0x0e}},
    {'M', {0x11, 0x1b, 0x15, 0x15, 0x11, 0x11, 0x11}},
    {'a', {0x00, 0x0e, 0x01, 0x0f, 0x11, 0x13, 0x0d}},
    {'b', {0x10, 0x10, 0x1e, 0x11, 0x11, 0x11, 0x1e}},
    {'c', {0x00, 0x0e, 0x11, 0x10, 0x10, 0x11, 0x0e}},
    {'d', {0x01, 0x01, 0x0f, 0x11, 0x11, 0x11, 0x0f}},
    {'e', {0x00, 0x0e, 0x11, 0x1f, 0x10, 0x11, 0x0e}},
    {'f', {0x06, 0x09, 0x08, 0x1c, 0x08, 0x08, 0x08}},
    {'g', {0x00, 0x0f, 0x11, 0x11, 0x0f, 0x01, 0x0e}},
    {'h', {0x10, 0x10, 0x1e, 0x11, 0x11, 0x11, 0x11}},
    {'i', {0x04, 0x00, 0x0c, 0x04, 0x04, 0x04, 0x0e}},
    {'l', {0x0c, 0x04, 0x04, 0x04, 0x04, 0x04, 0x0e}},
    {'m', {0x00, 0x1a, 0x15, 0x15, 0x15, 0x15, 0x15}},
    {'n', {0x00, 0x1e, 0x11, 0x11, 0x11, 0x11, 0x11}},
    {'o', {0x00, 0x0e, 0x11, 0x11, 0x11, 0x11, 0x0e}},
    {'p', {0x00, 0x1e, 0x11, 0x11, 0x1e, 0x10, 0x10}},
    {'r', {0x00, 0x16, 0x19, 0x10, 0x10, 0x10, 0x10}},
    {'s', {0x00, 0x0f, 0x10, 0x0e, 0x01, 0x01, 0x1e}},
    {'t', {0x08, 0x08, 0x1c, 0x08, 0x08, 0x09, 0x06}},
    {'u', {0x00, 0x11, 0x11, 0x11, 0x11, 0x13, 0x0d}},
    {'y', {0x00, 0x11, 0x11, 0x11, 0x0f, 0x01, 0x0e}},
    {'.', {0x00, 0x00, 0x00, 0x00, 0x00, 0x06, 0x06}}
};

static const BitmapGlyph *emergency_glyph(char character) {
    for (size_t index = 0;
         index < sizeof(EMERGENCY_GLYPHS) / sizeof(EMERGENCY_GLYPHS[0]);
         index++) {
        if (EMERGENCY_GLYPHS[index].character == character)
            return &EMERGENCY_GLYPHS[index];
    }
    return NULL;
}

static uint32_t create_emergency_text(WmPlatform *platform, const char *message) {
    enum { WIDTH = 640, HEIGHT = 112, SCALE = 2, ADVANCE = 6 };
    uint8_t *pixels = calloc((size_t)WIDTH * HEIGHT, 4);
    if (!pixels) return 0;
    const char *line = message;
    for (int row = 0; row < 3 && *line; row++) {
        const char *end = strchr(line, '\n');
        size_t length = end ? (size_t)(end - line) : strlen(line);
        if (length > 48) length = 48;
        int left = (WIDTH - (int)length * ADVANCE * SCALE) / 2;
        for (size_t column = 0; column < length; column++) {
            const BitmapGlyph *glyph = emergency_glyph(line[column]);
            if (!glyph) continue;
            for (int y = 0; y < 7; y++) {
                for (int x = 0; x < 5; x++) {
                    if (!(glyph->rows[y] & (uint8_t)(1u << (4 - x)))) continue;
                    for (int dy = 0; dy < SCALE; dy++) {
                        for (int dx = 0; dx < SCALE; dx++) {
                            int pixel_x = left + ((int)column * ADVANCE + x) * SCALE + dx;
                            int pixel_y = row * 34 + y * SCALE + dy;
                            if (pixel_x < 0 || pixel_x >= WIDTH || pixel_y >= HEIGHT)
                                continue;
                            size_t offset = ((size_t)pixel_y * WIDTH +
                                             (size_t)pixel_x) * 4;
                            pixels[offset] = 204;
                            pixels[offset + 1] = 204;
                            pixels[offset + 2] = 204;
                            pixels[offset + 3] = 255;
                        }
                    }
                }
            }
        }
        line = end ? end + 1 : line + strlen(line);
    }
    uint32_t texture = wm_platform_create_texture(platform, WIDTH, HEIGHT, pixels);
    free(pixels);
    return texture;
}

static bool font_sheet(void *context, size_t sheet, uint32_t *texture) {
    ScreenFont *font = context;
    return wm_font_cache_sheet(font->face, sheet, texture);
}

static void font_quad(void *context, const WmFontQuad *quad) {
    ScreenFont *font = context;
    if (!quad || !quad->texture) return;
    WmDrawVertex vertices[4];
    for (int index = 0; index < 4; index++) {
        vertices[index] = (WmDrawVertex){
            .x = WM_FRAME_WIDTH * 0.5f + quad->vertices[index].position[0],
            .y = WM_FRAME_HEIGHT * 0.5f - quad->vertices[index].position[1],
            .u = quad->vertices[index].uv[0],
            .v = quad->vertices[index].uv[1],
            .color = {
                quad->vertices[index].color[0],
                quad->vertices[index].color[1],
                quad->vertices[index].color[2],
                quad->vertices[index].color[3]
            }
        };
    }
    wm_platform_draw_vertices(font->platform, vertices, quad->texture);
}

static void draw_wad_text(ScreenFont *screen_font, const char *message) {
    /* The 16:9 presentation stretches the 640 x 456 logical frame. Scale the
     * font's two axes separately to retain the source image's glyph shape. */
    const float text_width = 19.6f;
    const float text_height = 26.4f;
    const WmFont *font = wm_cached_font_resource(screen_font->face);
    const WmFontMetrics *metrics = wm_font_metrics(font);
    if (!metrics || !metrics->height) return;
    const char *line = message;
    for (int row = 0; row < 3 && *line; row++) {
        const char *end = strchr(line, '\n');
        size_t length = end ? (size_t)(end - line) : strlen(line);
        char buffer[256];
        if (length >= sizeof(buffer)) length = sizeof(buffer) - 1;
        memcpy(buffer, line, length);
        buffer[length] = '\0';
        WmFontDrawOptions options = {
            .x = 0.0f,
            .y = WM_FRAME_HEIGHT * 0.5f - (188.0f + 28.0f * row),
            .size = {text_width * metrics->width / metrics->height,
                     text_height},
            .alpha = 1.0f,
            .top_color = {204, 204, 204, 255},
            .bottom_color = {204, 204, 204, 255},
            .align = WM_FONT_ALIGN_CENTER,
            .sheet_provider = font_sheet,
            .on_quad = font_quad,
            .context = screen_font
        };
        wm_font_emit_line(font, buffer, &options);
        line = end ? end + 1 : line + strlen(line);
    }
}

int wm_app_show_corruption_screen(const char *assets_root) {
    WmPlatform *platform = wm_platform_create("Wii Menu in C", 960, 540);
    if (!platform) {
        fprintf(stderr, "Could not show the corruption screen: graphics backend failed.\n");
        return 1;
    }
    char error[160];
    WmBmg *messages = assets_root
        ? wm_bmg_load_assets(assets_root, "eng", error, sizeof(error)) : NULL;
    const char *message = wm_bmg_text(messages, 12);
    if (!message || strcmp(message, CORRUPTION_MESSAGE) != 0)
        message = CORRUPTION_MESSAGE;

    WmCorruptionOutline outline = {0};
    bool has_outline = assets_root && wm_corruption_outline_create(
        &outline, platform, assets_root, message);
    WmFontCache *fonts = assets_root && !has_outline
        ? wm_font_cache_create(platform, assets_root, 4u * 1024u * 1024u) : NULL;
    ScreenFont screen_font = {
        .platform = platform,
        .face = wm_font_cache_resolve(
            fonts, "RevoIpl_RodinNTLGPro_DB_32_I4.brfnt")
    };
    uint32_t fallback = has_outline || screen_font.face
        ? 0 : create_emergency_text(platform, message);
    /* The emergency bitmap uses the same centered message width as the WAD font. */
    const WmQuad fallback_quad = {
        .x = 120.0f, .y = 170.0f, .width = 400.0f, .height = 112.0f,
        .u0 = 0.0f, .v0 = 0.0f, .u1 = 1.0f, .v1 = 1.0f,
        .color = {1.0f, 1.0f, 1.0f, 1.0f}, .texture = fallback
    };
    bool running = true;
    while (running) {
        WmEvent event;
        while (wm_platform_poll(platform, &event)) {
            if (event.type == WM_EVENT_QUIT ||
                (event.type == WM_EVENT_KEY_DOWN && event.key == WM_KEY_ESCAPE)) {
                running = false;
            }
        }
        if (!running) break;
        wm_platform_begin(platform, (WmColor){0.0f, 0.0f, 0.0f, 1.0f});
        if (has_outline) {
            wm_corruption_outline_draw(&outline, platform);
        } else if (screen_font.face) {
            wm_font_cache_begin_frame(fonts);
            draw_wad_text(&screen_font, message);
        } else if (fallback) {
            wm_platform_draw_quad(platform, &fallback_quad);
        }
        wm_platform_end(platform);
        struct timespec pause = {.tv_sec = 0, .tv_nsec = 16000000};
        nanosleep(&pause, NULL);
    }
    if (fallback) wm_platform_destroy_texture(platform, fallback);
    wm_font_cache_destroy(fonts);
    wm_corruption_outline_destroy(&outline, platform);
    wm_bmg_destroy(messages);
    wm_platform_destroy(platform);
    return 1;
}
