#include "wii_menu/resource_scene.h"
#include "wii_menu/channel_drag.h"
#include "wii_menu/pointer.h"

#ifdef NDEBUG
#undef NDEBUG
#endif
#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <string.h>

typedef struct ArrowSample {
    unsigned quads;
    float left;
    float right;
    float top;
    float bottom;
} ArrowSample;

static uint32_t next_texture = 1;
static uint32_t right_arrow_texture;
static uint32_t grab_overlay_texture;
static ArrowSample drawn;
static unsigned draw_event;
static unsigned last_arrow_event;
static unsigned grab_pointer_event;

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
    draw_event++;
    if (quad->texture_count >= 2 &&
        quad->textures[1] == grab_overlay_texture)
        grab_pointer_event = draw_event;
    if (!quad->texture_count || quad->textures[0] != right_arrow_texture ||
        quad->vertices[0].color.a <= 0.01f) return;
    last_arrow_event = draw_event;
    drawn.quads++;
    for (size_t index = 0; index < 4; index++) {
        float x = quad->vertices[index].x;
        float y = quad->vertices[index].y;
        drawn.left = fminf(drawn.left, x);
        drawn.right = fmaxf(drawn.right, x);
        drawn.top = fminf(drawn.top, y);
        drawn.bottom = fmaxf(drawn.bottom, y);
    }
}

uint32_t wm_platform_create_texture(WmPlatform *platform, int width,
                                    int height, const uint8_t *rgba) {
    (void)platform;
    assert(width > 0 && height > 0 && rgba);
    return next_texture++;
}

uint32_t wm_platform_create_render_texture(WmPlatform *platform) {
    (void)platform;
    return next_texture++;
}

bool wm_platform_begin_target(WmPlatform *platform, uint32_t texture,
                              WmColor clear_color) {
    (void)platform;
    (void)texture;
    (void)clear_color;
    return true;
}

void wm_platform_destroy_texture(WmPlatform *platform, uint32_t texture) {
    (void)platform;
    (void)texture;
}

static ArrowSample sample(WmResourceScene *scene, const WmMenu *menu,
                          float seconds) {
    drawn = (ArrowSample){
        .left = INFINITY,
        .right = -INFINITY,
        .top = INFINITY,
        .bottom = -INFINITY
    };
    const WmResourceSceneFrame frame = {
        .elapsed_seconds = seconds,
        .hover = {WM_HIT_NONE, -1}
    };
    wm_resource_scene_draw(scene, menu, &frame);
    return drawn;
}

static float width(ArrowSample sample) {
    return sample.right - sample.left;
}

static float height(ArrowSample sample) {
    return sample.bottom - sample.top;
}

int main(int argc, char **argv) {
    const char *assets = argc > 1 ? argv[1] : ".local/native-assets";
    WmPlatform *platform = (WmPlatform *)1;
    WmMenu menu;
    wm_menu_init(&menu);
    if (!wm_catalog_load(&menu, assets)) {
        puts("Footer arrow transition test skipped: prepared assets unavailable.");
        return 0;
    }
    WmTextureCache *textures = wm_texture_cache_create(
        platform, assets, 128u * 1024u * 1024u);
    WmFontCache *fonts = wm_font_cache_create(
        platform, assets, 16u * 1024u * 1024u);
    assert(textures && fonts);
    assert(wm_texture_cache_resolve(textures,
        "textures/cmnBtn/my_arw_a.png", &right_arrow_texture));
    WmResourceScene *scene = wm_resource_scene_create(
        platform, assets, &menu, textures, fonts);
    assert(scene);

    ArrowSample settled = sample(scene, &menu, 0.0f);
    assert(settled.quads == 1);
    assert(wm_menu_select(&menu, 0));
    ArrowSample start = sample(scene, &menu, 0.0f);
    assert(start.quads == 1);
    wm_menu_tick(&menu, 5.0f / 60.0f);
    ArrowSample middle = sample(scene, &menu, 5.0f / 60.0f);
    assert(middle.quads == 1);
    wm_menu_tick(&menu, 5.0f / 60.0f);
    ArrowSample end = sample(scene, &menu, 10.0f / 60.0f);
    assert(end.quads == 1);

    /* G_ArwR_End travels outward under the full-screen IPL projection.
     * The channel camera must never scale the common footer arrow. */
    assert(fabsf(width(start) - width(settled)) < 0.25f);
    assert(fabsf(height(start) - height(settled)) < 0.25f);
    assert(fabsf(width(middle) - width(settled)) < 0.25f);
    assert(fabsf(height(middle) - height(settled)) < 0.25f);
    assert(fabsf(width(end) - width(settled)) < 0.25f);
    assert(fabsf(height(end) - height(settled)) < 0.25f);
    assert(middle.left > start.left);
    assert(end.left > middle.left);

    /* The channel stays beneath the footer, while its grabbed hand is the
     * final overlay even when an arrow owns the pointer area. */
    wm_menu_init(&menu);
    strcpy(menu.slots[1].id, "local-channel");
    menu.slots[1].occupied = true;
    wm_resource_scene_restart(scene);
    WmPointer *pointer = wm_pointer_create(platform, assets, textures);
    WmChannelDrag *drag = wm_channel_drag_create(
        platform, assets, textures, fonts, true);
    assert(pointer && drag);
    assert(wm_texture_cache_resolve(textures,
        "textures/cursor/defcursor_final64_b.png", &grab_overlay_texture));
    assert(wm_channel_drag_start(drag, 1, &menu, 605.0f, 200.0f));
    wm_pointer_move(pointer, 605.0f, 200.0f);
    wm_pointer_set_grabbed(pointer, true);
    draw_event = last_arrow_event = grab_pointer_event = 0;
    const WmResourceSceneFrame drag_frame = {
        .elapsed_seconds = 0.0f,
        .hover = {WM_HIT_PAGE_NEXT, -1},
        .pointer = pointer,
        .drag = drag
    };
    wm_resource_scene_draw(scene, &menu, &drag_frame);
    assert(last_arrow_event > 0);
    assert(grab_pointer_event > last_arrow_event);
    wm_channel_drag_destroy(drag);
    wm_pointer_destroy(pointer);

    wm_resource_scene_destroy(scene);
    wm_font_cache_destroy(fonts);
    wm_texture_cache_destroy(textures);
    puts("Home arrows retain source size; grab hand tops the footer.");
    return 0;
}
