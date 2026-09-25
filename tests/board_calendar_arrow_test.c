#include "wii_menu/board_calendar.h"
#include "wii_menu/texture_cache.h"

#include <assert.h>
#include <math.h>
#include <stdint.h>
#include <stdio.h>

typedef struct ArrowQuad {
    unsigned count;
    float x;
    float y;
    float width;
    float height;
    float alpha;
} ArrowQuad;

static uint32_t next_texture = 1;
static uint32_t arrow_textures[2];
static ArrowQuad arrows[2];

void wm_platform_begin(WmPlatform *platform, WmColor clear_color) {
    (void)platform;
    (void)clear_color;
}

void wm_platform_end(WmPlatform *platform) {
    (void)platform;
}

void wm_platform_set_clip(WmPlatform *platform, const WmClipRect *clip) {
    (void)platform;
    (void)clip;
}

void wm_platform_draw_quad(WmPlatform *platform, const WmQuad *quad) {
    (void)platform;
    (void)quad;
}

void wm_platform_draw_vertices(WmPlatform *platform,
                               const WmDrawVertex vertices[4],
                               uint32_t texture) {
    (void)platform;
    (void)vertices;
    (void)texture;
}

void wm_platform_prepare_material(WmPlatform *platform,
                                  const WmMaterialQuad *quad) {
    (void)platform;
    (void)quad;
}

void wm_platform_draw_material_quad(WmPlatform *platform,
                                    const WmMaterialQuad *quad) {
    (void)platform;
    for (size_t side = 0; side < 2; side++) {
        if (quad->texture_count == 0 ||
            quad->textures[0] != arrow_textures[side]) continue;
        ArrowQuad *arrow = &arrows[side];
        arrow->count++;
        arrow->x = quad->vertices[0].x;
        arrow->y = quad->vertices[0].y;
        arrow->width = quad->vertices[1].x - quad->vertices[0].x;
        arrow->height = quad->vertices[2].y - quad->vertices[0].y;
        arrow->alpha = quad->vertices[0].color.a;
    }
}

uint32_t wm_platform_create_texture(WmPlatform *platform, int width,
                                    int height, const uint8_t *rgba) {
    (void)platform;
    assert(width > 0 && height > 0 && rgba);
    return next_texture++;
}

void wm_platform_destroy_texture(WmPlatform *platform, uint32_t texture) {
    (void)platform;
    (void)texture;
}

uint32_t wm_platform_create_render_texture(WmPlatform *platform) {
    (void)platform;
    assert(false);
    return 0;
}

bool wm_platform_begin_target(WmPlatform *platform, uint32_t texture,
                              WmColor clear_color) {
    (void)platform;
    (void)texture;
    (void)clear_color;
    assert(false);
    return false;
}

static void draw_arrows(WmBoardCalendar *calendar, ArrowQuad output[2]) {
    arrows[0] = (ArrowQuad){0};
    arrows[1] = (ArrowQuad){0};
    wm_board_calendar_draw(calendar);
    assert(arrows[0].count == 1 && arrows[1].count == 1);
    assert(arrows[0].alpha > 0.99f && arrows[1].alpha > 0.99f);
    output[0] = arrows[0];
    output[1] = arrows[1];
}

static bool nearly_equal(float left, float right) {
    return fabsf(left - right) < 0.02f;
}

static void test_day_hover_continuity(WmBoardCalendar *calendar) {
    /* Every tile should keep one hit owner while the pointer moves inside it
     * and the six-frame focus clip grows. A changed hit would replay the
     * generic hover cue on the next pointer event. */
    static const int offsets[][2] = {
        {0, 0}, {24, 20}, {-24, 20}, {-24, -20},
        {24, -20}, {0, 0}
    };
    for (unsigned index = 0; index < 35; index++) {
        int center_x = 158 + (int)(index % 7) * 54;
        int center_y = 91 + (int)(index / 7) * 48;
        for (unsigned step = 0; step < sizeof(offsets) / sizeof(offsets[0]);
             step++) {
            WmBoardCalendarHit hit = wm_board_calendar_hit(
                calendar, center_x + offsets[step][0],
                center_y + offsets[step][1]);
            if (hit.control != WM_CALENDAR_CONTROL_DAY ||
                hit.day_index != index) {
                fprintf(stderr, "Calendar tile %u at (%d,%d), step %u: hit %d/%u\n",
                        index, center_x, center_y, step, hit.control,
                        hit.day_index);
            }
            assert(hit.control == WM_CALENDAR_CONTROL_DAY &&
                   hit.day_index == index);
            wm_board_calendar_hover(calendar, hit);
            wm_board_calendar_advance(calendar, 1.0f);
        }
    }
    wm_board_calendar_hover(calendar,
        (WmBoardCalendarHit){WM_CALENDAR_CONTROL_NONE, 0});
    wm_board_calendar_advance(calendar, 16.0f);
}

int main(int argc, char **argv) {
    const char *assets = argc > 1 ? argv[1] : ".local/native-assets";
    char path[1024];
    int length = snprintf(path, sizeof(path),
                          "%s/layouts/cmnBtn/my_IplTop_e.json", assets);
    assert(length > 0 && length < (int)sizeof(path));
    FILE *check = fopen(path, "rb");
    if (!check) {
        puts("Calendar arrow comparison skipped: prepared assets unavailable.");
        return 0;
    }
    fclose(check);

    WmPlatform *platform = (WmPlatform *)1;
    WmTextureCache *textures = wm_texture_cache_create(
        platform, assets, 32u * 1024u * 1024u);
    WmFontCache *fonts = wm_font_cache_create(
        platform, assets, 16u * 1024u * 1024u);
    assert(textures && fonts);
    assert(wm_texture_cache_resolve(textures,
        "textures/cmnBtn/my_arw_b.png", &arrow_textures[0]));
    assert(wm_texture_cache_resolve(textures,
        "textures/cmnBtn/my_arw_a.png", &arrow_textures[1]));
    WmBoardCalendar *calendar = wm_board_calendar_create(
        platform, assets, textures, fonts);
    assert(calendar);
    const WmBoardDate today = {2026, 9, 25};
    assert(wm_board_calendar_open(calendar, today, today));

    ArrowQuad entering[2], middle[2], settled[2], focused[2];
    ArrowQuad pressed[2], left_focused[2], exiting[2];
    wm_board_calendar_advance(calendar, 40.0f);
    draw_arrows(calendar, entering);
    wm_board_calendar_advance(calendar, 5.0f);
    draw_arrows(calendar, middle);
    wm_board_calendar_advance(calendar, 5.0f);
    draw_arrows(calendar, settled);
    test_day_hover_continuity(calendar);
    assert(entering[0].x < middle[0].x &&
           middle[0].x < settled[0].x);
    assert(entering[1].x > middle[1].x &&
           middle[1].x > settled[1].x);
    assert(entering[0].x + entering[0].width < 0.0f);
    assert(entering[1].x > WM_FRAME_WIDTH);
    assert(settled[0].x + settled[0].width > 0.0f);
    assert(settled[1].x < WM_FRAME_WIDTH);

    wm_board_calendar_hover(calendar,
        (WmBoardCalendarHit){WM_CALENDAR_CONTROL_NEXT, 0});
    wm_board_calendar_advance(calendar, 4.0f);
    draw_arrows(calendar, focused);
    assert(focused[1].height < settled[1].height * 0.8f);
    /* The authored arrow idle loop shifts X slightly while the other side
     * focuses; the left arrow's full-height pose must remain unchanged. */
    assert(fabsf(focused[0].x - settled[0].x) < 1.0f);
    assert(nearly_equal(focused[0].height, settled[0].height));

    assert(wm_board_calendar_activate(calendar,
        (WmBoardCalendarHit){WM_CALENDAR_CONTROL_NEXT, 0}));
    wm_board_calendar_advance(calendar, 3.0f);
    draw_arrows(calendar, pressed);
    assert(pressed[0].x + pressed[0].width > 0.0f);
    assert(pressed[1].x < WM_FRAME_WIDTH);
    wm_board_calendar_advance(calendar, 27.0f);
    assert(wm_board_calendar_phase(calendar) == WM_CALENDAR_IDLE);

    wm_board_calendar_hover(calendar,
        (WmBoardCalendarHit){WM_CALENDAR_CONTROL_NONE, 0});
    wm_board_calendar_advance(calendar, 15.0f);
    wm_board_calendar_hover(calendar,
        (WmBoardCalendarHit){WM_CALENDAR_CONTROL_PREVIOUS, 0});
    wm_board_calendar_advance(calendar, 4.0f);
    draw_arrows(calendar, left_focused);
    assert(left_focused[0].height < settled[0].height * 0.8f);
    assert(nearly_equal(left_focused[1].height, settled[1].height));
    wm_board_calendar_hover(calendar,
        (WmBoardCalendarHit){WM_CALENDAR_CONTROL_NONE, 0});
    wm_board_calendar_advance(calendar, 15.0f);
    assert(wm_board_calendar_back(calendar));
    wm_board_calendar_advance(calendar, 5.0f);
    draw_arrows(calendar, exiting);
    assert(exiting[0].x < settled[0].x);
    assert(exiting[1].x > settled[1].x);
    assert(nearly_equal(exiting[0].height, settled[0].height));
    assert(nearly_equal(exiting[1].height, settled[1].height));

    wm_board_calendar_destroy(calendar);
    wm_font_cache_destroy(fonts);
    wm_texture_cache_destroy(textures);
    puts("Calendar arrows use independent WAD enter, exit, and focus groups.");
    return 0;
}
