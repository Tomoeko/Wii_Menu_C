#include "wii_menu/ui.h"

#include <ctype.h>
#include <stdio.h>
#include <string.h>
#include <time.h>

typedef struct Glyph {
    char letter;
    unsigned char rows[7];
} Glyph;

/* A compact first-party UI font. Each row uses the low five bits. */
static const Glyph glyphs[] = {
    {'A', {14, 17, 17, 31, 17, 17, 17}},
    {'B', {30, 17, 17, 30, 17, 17, 30}},
    {'C', {14, 17, 16, 16, 16, 17, 14}},
    {'D', {30, 17, 17, 17, 17, 17, 30}},
    {'E', {31, 16, 16, 30, 16, 16, 31}},
    {'F', {31, 16, 16, 30, 16, 16, 16}},
    {'G', {14, 17, 16, 23, 17, 17, 14}},
    {'H', {17, 17, 17, 31, 17, 17, 17}},
    {'I', {31, 4, 4, 4, 4, 4, 31}},
    {'J', {7, 2, 2, 2, 18, 18, 12}},
    {'K', {17, 18, 20, 24, 20, 18, 17}},
    {'L', {16, 16, 16, 16, 16, 16, 31}},
    {'M', {17, 27, 21, 21, 17, 17, 17}},
    {'N', {17, 25, 21, 19, 17, 17, 17}},
    {'O', {14, 17, 17, 17, 17, 17, 14}},
    {'P', {30, 17, 17, 30, 16, 16, 16}},
    {'Q', {14, 17, 17, 17, 21, 18, 13}},
    {'R', {30, 17, 17, 30, 20, 18, 17}},
    {'S', {15, 16, 16, 14, 1, 1, 30}},
    {'T', {31, 4, 4, 4, 4, 4, 4}},
    {'U', {17, 17, 17, 17, 17, 17, 14}},
    {'V', {17, 17, 17, 17, 17, 10, 4}},
    {'W', {17, 17, 17, 21, 21, 21, 10}},
    {'X', {17, 17, 10, 4, 10, 17, 17}},
    {'Y', {17, 17, 10, 4, 4, 4, 4}},
    {'Z', {31, 1, 2, 4, 8, 16, 31}},
    {'0', {14, 17, 19, 21, 25, 17, 14}},
    {'1', {4, 12, 4, 4, 4, 4, 14}},
    {'2', {14, 17, 1, 2, 4, 8, 31}},
    {'3', {30, 1, 1, 14, 1, 1, 30}},
    {'4', {2, 6, 10, 18, 31, 2, 2}},
    {'5', {31, 16, 16, 30, 1, 1, 30}},
    {'6', {14, 16, 16, 30, 17, 17, 14}},
    {'7', {31, 1, 2, 4, 8, 8, 8}},
    {'8', {14, 17, 17, 14, 17, 17, 14}},
    {'9', {14, 17, 17, 15, 1, 1, 14}},
    {'.', {0, 0, 0, 0, 0, 12, 12}},
    {',', {0, 0, 0, 0, 0, 12, 8}},
    {':', {0, 12, 12, 0, 12, 12, 0}},
    {'-', {0, 0, 0, 31, 0, 0, 0}},
    {'/', {1, 1, 2, 4, 8, 16, 16}},
    {'!', {4, 4, 4, 4, 4, 0, 4}},
    {'?', {14, 17, 1, 2, 4, 0, 4}},
    {'+', {0, 4, 4, 31, 4, 4, 0}},
    {'<', {2, 4, 8, 16, 8, 4, 2}},
    {'>', {8, 4, 2, 1, 2, 4, 8}},
    {'&', {12, 18, 20, 8, 21, 18, 13}},
    {'(', {2, 4, 8, 8, 8, 4, 2}},
    {')', {8, 4, 2, 2, 2, 4, 8}},
    {'\'', {4, 4, 8, 0, 0, 0, 0}},
    {' ', {0, 0, 0, 0, 0, 0, 0}}
};

static WmColor color(float r, float g, float b, float a) {
    return (WmColor){r, g, b, a};
}

static void rectangle(WmPlatform *platform, float x, float y, float width,
                      float height, WmColor fill) {
    WmQuad quad = {
        .x = x, .y = y, .width = width, .height = height,
        .u0 = 0.0f, .v0 = 0.0f, .u1 = 1.0f, .v1 = 1.0f,
        .color = fill, .texture = 0
    };
    wm_platform_draw_quad(platform, &quad);
}

static const unsigned char *glyph_rows(char letter) {
    static const unsigned char unknown[7] = {14, 17, 1, 2, 4, 0, 4};
    char upper = (char)toupper((unsigned char)letter);
    for (size_t index = 0; index < sizeof(glyphs) / sizeof(glyphs[0]); index++) {
        if (glyphs[index].letter == upper) return glyphs[index].rows;
    }
    return unknown;
}

static void draw_text(WmPlatform *platform, const char *text, float x, float y,
                      float scale, WmColor ink) {
    if (!text) return;
    for (const unsigned char *letter = (const unsigned char *)text; *letter; letter++) {
        const unsigned char *rows = glyph_rows((char)*letter);
        for (int row = 0; row < 7; row++) {
            unsigned char bits = rows[row];
            int column = 0;
            while (column < 5) {
                if (!(bits & (1u << (4 - column)))) { column++; continue; }
                int first = column;
                while (column < 5 && (bits & (1u << (4 - column)))) column++;
                rectangle(platform, x + first * scale, y + row * scale,
                          (column - first) * scale, scale, ink);
            }
        }
        x += 6.0f * scale;
    }
}

static void draw_text_center(WmPlatform *platform, const char *text, float center_x,
                             float y, float scale, WmColor ink) {
    float width = (float)strlen(text) * 6.0f * scale;
    draw_text(platform, text, center_x - width * 0.5f, y, scale, ink);
}

static void draw_button(WmPlatform *platform, float x, float y, float width,
                        float height, const char *label, bool hovered) {
    WmColor border = hovered ? color(0.20f, 0.70f, 0.88f, 1.0f)
                             : color(0.65f, 0.75f, 0.79f, 1.0f);
    rectangle(platform, x, y + 2.0f, width, height, color(0.70f, 0.75f, 0.77f, 0.5f));
    rectangle(platform, x, y, width, height, border);
    rectangle(platform, x + 2.0f, y + 2.0f, width - 4.0f, height - 4.0f,
              hovered ? color(0.90f, 0.99f, 1.0f, 1.0f) : color(1, 1, 1, 1));
    draw_text_center(platform, label, x + width * 0.5f, y + height * 0.5f - 5.0f,
                     1.6f, color(0.27f, 0.31f, 0.34f, 1.0f));
}

static bool inside(int x, int y, int left, int top, int width, int height) {
    return x >= left && x < left + width && y >= top && y < top + height;
}

WmHit wm_ui_hit(const WmMenu *menu, int x, int y) {
    if (menu->home_open) {
        if (inside(x, y, 137, 277, 170, 45)) return (WmHit){WM_HIT_HOME_MENU, -1};
        if (inside(x, y, 333, 277, 170, 45)) return (WmHit){WM_HIT_HOME_CLOSE, -1};
        return (WmHit){WM_HIT_NONE, -1};
    }
    if (inside(x, y, 280, 412, 80, 36)) return (WmHit){WM_HIT_HOME, -1};
    if (menu->screen == WM_SCREEN_GRID) {
        for (int row = 0; row < 3; row++) {
            for (int column = 0; column < 4; column++) {
                int local_slot = row * 4 + column;
                int slot = menu->page * WM_CHANNELS_PER_PAGE + local_slot;
                if (inside(x, y, 25 + 150 * column, 34 + 92 * row, 140, 82) &&
                    menu->slots[slot].occupied) return (WmHit){WM_HIT_CHANNEL, slot};
            }
        }
        if (inside(x, y, 2, 150, 21, 88) && menu->page > 0)
            return (WmHit){WM_HIT_PAGE_PREVIOUS, -1};
        if (inside(x, y, 617, 150, 21, 88) && menu->page < WM_PAGE_COUNT - 1)
            return (WmHit){WM_HIT_PAGE_NEXT, -1};
        if (inside(x, y, 24, 360, 120, 43)) return (WmHit){WM_HIT_SETTINGS, -1};
        if (inside(x, y, 496, 360, 120, 43)) return (WmHit){WM_HIT_BOARD, -1};
        if (inside(x, y, 260, 360, 120, 43)) return (WmHit){WM_HIT_SD, -1};
        return (WmHit){WM_HIT_NONE, -1};
    }
    if (inside(x, y, 32, 364, 130, 39)) return (WmHit){WM_HIT_BACK, -1};
    if (menu->screen == WM_SCREEN_PREVIEW) {
        if (inside(x, y, 4, 144, 26, 120)) return (WmHit){WM_HIT_PREVIEW_PREVIOUS, -1};
        if (inside(x, y, 610, 144, 26, 120)) return (WmHit){WM_HIT_PREVIEW_NEXT, -1};
    }
    return (WmHit){WM_HIT_NONE, -1};
}

static void draw_tile_title(WmPlatform *platform, const char *title, float center_x,
                            float top) {
    char first[18] = {0};
    char second[18] = {0};
    size_t length = strlen(title);
    size_t cursor = 0;
    while (cursor < length && cursor < 16) { first[cursor] = title[cursor]; cursor++; }
    while (cursor < length && title[cursor] == ' ') cursor++;
    size_t second_index = 0;
    while (cursor < length && second_index < 16) {
        second[second_index++] = title[cursor++];
    }
    WmColor ink = color(0.29f, 0.34f, 0.38f, 1.0f);
    draw_text_center(platform, first, center_x, top, 1.2f, ink);
    if (second[0]) draw_text_center(platform, second, center_x, top + 11.0f, 1.2f, ink);
}

static void draw_grid_tiles(WmPlatform *platform, const WmMenu *menu,
                            int page, float offset_x, WmHit hover) {
    for (int row = 0; row < 3; row++) {
        for (int column = 0; column < 4; column++) {
            int slot = page * WM_CHANNELS_PER_PAGE + row * 4 + column;
            float x = 25.0f + 150.0f * column + offset_x;
            float y = 34.0f + 92.0f * row;
            bool highlighted = hover.type == WM_HIT_CHANNEL && hover.slot == slot;
            rectangle(platform, x + 2, y + 3, 140, 82,
                      color(0.53f, 0.61f, 0.65f, 0.28f));
            rectangle(platform, x, y, 140, 82,
                      highlighted ? color(0.21f, 0.72f, 0.88f, 1.0f)
                                  : color(0.68f, 0.73f, 0.75f, 1.0f));
            rectangle(platform, x + 2, y + 2, 136, 78, color(1, 1, 1, 1));
            if (menu->slots[slot].occupied) {
                rectangle(platform, x + 9, y + 9, 122, 43,
                          slot == 0 ? color(0.88f, 0.94f, 0.97f, 1.0f)
                                    : color(0.93f, 0.96f, 0.97f, 1.0f));
                rectangle(platform, x + 9, y + 51, 122, 1,
                          color(0.75f, 0.85f, 0.89f, 1.0f));
                draw_tile_title(platform, menu->slots[slot].title, x + 70.0f, y + 55.0f);
            }
        }
    }
}

static void draw_grid_controls(WmPlatform *platform, const WmMenu *menu,
                               WmHit hover) {
    if (menu->page > 0) draw_button(platform, 2, 150, 21, 88, "<",
                                   hover.type == WM_HIT_PAGE_PREVIOUS);
    if (menu->page < WM_PAGE_COUNT - 1) draw_button(platform, 617, 150, 21, 88, ">",
                                                   hover.type == WM_HIT_PAGE_NEXT);
    draw_button(platform, 24, 360, 120, 43, "WII", hover.type == WM_HIT_SETTINGS);
    draw_button(platform, 260, 360, 120, 43, "SD", hover.type == WM_HIT_SD);
    draw_button(platform, 496, 360, 120, 43, "BOARD", hover.type == WM_HIT_BOARD);
    char page_text[24];
    snprintf(page_text, sizeof(page_text), "PAGE %d OF %d", menu->page + 1, WM_PAGE_COUNT);
    draw_text_center(platform, page_text, 320, 323, 1.3f,
                     color(0.40f, 0.53f, 0.59f, 1.0f));
}

static void draw_grid(WmPlatform *platform, const WmMenu *menu, WmHit hover) {
    if (menu->transition == WM_TRANSITION_PAGE) {
        float progress = wm_menu_transition_progress(menu);
        float eased = progress * progress * (3.0f - 2.0f * progress);
        float direction = (float)menu->transition_direction;
        WmClipRect viewport = {0.0f, 0.0f, WM_FRAME_WIDTH, WM_FRAME_HEIGHT};
        wm_platform_set_clip(platform, &viewport);
        draw_grid_tiles(platform, menu, menu->transition_from_page,
                        -direction * WM_FRAME_WIDTH * eased,
                        (WmHit){WM_HIT_NONE, -1});
        draw_grid_tiles(platform, menu, menu->page,
                        direction * WM_FRAME_WIDTH * (1.0f - eased),
                        (WmHit){WM_HIT_NONE, -1});
        wm_platform_set_clip(platform, NULL);
    } else {
        draw_grid_tiles(platform, menu, menu->page, 0.0f, hover);
    }
    draw_grid_controls(platform, menu, hover);
}

static void draw_preview(WmPlatform *platform, const WmMenu *menu, WmHit hover) {
    rectangle(platform, 31, 25, 578, 309, color(0.68f, 0.73f, 0.75f, 1));
    rectangle(platform, 34, 28, 572, 303, color(1, 1, 1, 1));
    rectangle(platform, 49, 44, 542, 263, color(0.93f, 0.96f, 0.97f, 1));
    const char *title = menu->selected >= 0 ? menu->slots[menu->selected].title : "CHANNEL";
    draw_text_center(platform, title, 320, 166, 2.0f, color(0.24f, 0.36f, 0.41f, 1));
    draw_button(platform, 32, 364, 130, 39, "BACK", hover.type == WM_HIT_BACK);
    draw_button(platform, 478, 364, 130, 39, "START", false);
    if (menu->selected > 0 || menu->slots[WM_SLOT_COUNT - 1].occupied)
        draw_button(platform, 4, 144, 26, 120, "<",
                    hover.type == WM_HIT_PREVIEW_PREVIOUS);
    draw_button(platform, 610, 144, 26, 120, ">",
                hover.type == WM_HIT_PREVIEW_NEXT);
}

static void draw_secondary(WmPlatform *platform, WmScreen screen, WmHit hover) {
    const char *title = screen == WM_SCREEN_SETTINGS ? "WII OPTIONS"
                        : screen == WM_SCREEN_BOARD ? "MESSAGE BOARD" : "SD MENU";
    rectangle(platform, 38, 30, 564, 305, color(0.70f, 0.77f, 0.80f, 1));
    rectangle(platform, 40, 32, 560, 301, color(1, 1, 1, 1));
    draw_text_center(platform, title, 320, 96, 2.6f,
                     color(0.22f, 0.37f, 0.43f, 1));
    draw_text_center(platform, "SCENE PORT IN PROGRESS", 320, 185, 1.6f,
                     color(0.43f, 0.55f, 0.59f, 1));
    draw_button(platform, 32, 364, 130, 39, "BACK", hover.type == WM_HIT_BACK);
}

WmHit wm_ui_notice_hit(const WmMenu *menu, int x, int y) {
    if (menu && menu->notice[0] && inside(x, y, 225, 282, 190, 46)) {
        return (WmHit){WM_HIT_NOTICE_DISMISS, -1};
    }
    return (WmHit){WM_HIT_NONE, -1};
}

void wm_ui_draw_notice(WmPlatform *platform, const WmMenu *menu) {
    if (!platform || !menu || !menu->notice[0]) return;
    rectangle(platform, 0, 0, WM_FRAME_WIDTH, WM_FRAME_HEIGHT,
              color(1, 1, 1, 0.88f));
    const char *break_at = strchr(menu->notice, '\n');
    char title[sizeof(menu->notice)];
    size_t length = break_at ? (size_t)(break_at - menu->notice)
                             : strlen(menu->notice);
    if (length >= sizeof(title)) length = sizeof(title) - 1;
    memcpy(title, menu->notice, length);
    title[length] = '\0';
    float scale = length > 30 ? 1.7f : 2.8f;
    draw_text_center(platform, title, 320, 170, scale,
                     color(0.24f, 0.34f, 0.38f, 1));
    if (break_at && break_at[1]) {
        draw_text_center(platform, break_at + 1, 320, 204, 2.3f,
                         color(0.24f, 0.34f, 0.38f, 1));
    }
    draw_button(platform, 225, 282, 190, 46, "Back", false);
}

void wm_ui_draw(WmPlatform *platform, const WmMenu *menu, WmHit hover,
                const WmPointer *pointer) {
    wm_platform_begin(platform, color(0.93f, 0.97f, 0.99f, 1));
    for (int band = 0; band < 24; band++) {
        float factor = (float)band / 23.0f;
        rectangle(platform, 0, band * 19.0f, 640, 19.0f,
                  color(0.96f - factor * 0.07f, 0.99f - factor * 0.04f,
                        1.0f - factor * 0.02f, 1));
    }
    rectangle(platform, 0, 344, 640, 112, color(0.83f, 0.92f, 0.96f, 1));
    rectangle(platform, 0, 343, 640, 2, color(0.67f, 0.84f, 0.90f, 1));
    if (menu->screen == WM_SCREEN_GRID) draw_grid(platform, menu, hover);
    else if (menu->screen == WM_SCREEN_PREVIEW) draw_preview(platform, menu, hover);
    else draw_secondary(platform, menu->screen, hover);
    draw_button(platform, 280, 412, 80, 36, "HOME", hover.type == WM_HIT_HOME);

    if (menu->home_open) {
        rectangle(platform, 0, 0, 640, 456, color(0, 0, 0, 0.48f));
        rectangle(platform, 94, 95, 452, 252, color(0.45f, 0.53f, 0.57f, 1));
        rectangle(platform, 98, 99, 444, 244, color(0.97f, 0.99f, 1, 1));
        draw_text_center(platform, "HOME MENU", 320, 151, 2.8f,
                         color(0.28f, 0.39f, 0.45f, 1));
        draw_button(platform, 137, 277, 170, 45, "WII MENU",
                    hover.type == WM_HIT_HOME_MENU);
        draw_button(platform, 333, 277, 170, 45, "CLOSE",
                    hover.type == WM_HIT_HOME_CLOSE);
    }
    if (menu->transition != WM_TRANSITION_NONE && menu->transition_duration > 0) {
        float progress = menu->transition_elapsed / menu->transition_duration;
        float fade = (1.0f - progress) * 0.13f;
        rectangle(platform, 0, 0, 640, 456, color(1, 1, 1, fade));
    }
    wm_ui_draw_notice(platform, menu);
    wm_pointer_draw(pointer);
    wm_platform_end(platform);
}
