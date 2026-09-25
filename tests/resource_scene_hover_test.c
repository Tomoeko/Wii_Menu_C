#include "wii_menu/resource_scene.h"
#include "wii_menu/preview_scene.h"

#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <string.h>

static uint32_t next_texture = 1;
static uint32_t focus_texture;
static unsigned focus_quads;
static float focus_alpha;
static unsigned capture_count;

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
    if (quad->texture_count == 0 || quad->textures[0] != focus_texture)
        return;
    float alpha = quad->vertices[0].color.a;
    if (alpha <= 0.01f) return;
    focus_quads++;
    if (alpha > focus_alpha) focus_alpha = alpha;
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
    capture_count++;
    return true;
}

void wm_platform_destroy_texture(WmPlatform *platform, uint32_t texture) {
    (void)platform;
    (void)texture;
}

static void draw_hover(WmResourceScene *scene, const WmMenu *menu,
                       float seconds, WmHit hover) {
    focus_quads = 0;
    focus_alpha = 0.0f;
    const WmResourceSceneFrame frame = {
        .elapsed_seconds = seconds,
        .hover = hover
    };
    wm_resource_scene_draw(scene, menu, &frame);
}

int main(int argc, char **argv) {
    const char *assets = argc > 1 ? argv[1] : ".local/native-assets";
    WmPlatform *platform = (WmPlatform *)1;
    WmMenu menu;
    wm_menu_init(&menu);
    if (!wm_catalog_load(&menu, assets)) {
        puts("Grid hover comparison skipped: prepared assets unavailable.");
        return 0;
    }
    WmTextureCache *textures = wm_texture_cache_create(
        platform, assets, 128u * 1024u * 1024u);
    WmFontCache *fonts = wm_font_cache_create(
        platform, assets, 16u * 1024u * 1024u);
    assert(textures && fonts);
    assert(wm_texture_cache_resolve(textures,
        "textures/chanSel/my_TV_f.png", &focus_texture));
    WmResourceScene *scene = wm_resource_scene_create(
        platform, assets, &menu, textures, fonts);
    assert(scene);

    /* An occupied channel holds the final FocusOn frame without a blank or
     * repeated first frame when the pointer remains over that channel. */
    WmHit channel = {WM_HIT_CHANNEL, 0};
    draw_hover(scene, &menu, 0.0f, channel);
    float steady_alpha = 0.0f;
    for (int frame = 6; frame <= 42; frame++) {
        WmHit hit = wm_resource_scene_hit(
            scene, &menu, 172 + frame % 5 - 2, 53 + frame % 3 - 1);
        assert(hit.type == WM_HIT_CHANNEL && hit.slot == 0);
        draw_hover(scene, &menu, (float)frame / 60.0f, hit);
        assert(focus_quads > 0);
        assert(focus_alpha > 0.0f);
        if (frame == 6) steady_alpha = focus_alpha;
        assert(fabsf(focus_alpha - steady_alpha) < 0.01f);
    }

    /* The right arrow's authored hit pane overlaps the adjacent channel on
     * this source layout. It retains priority while available, including
     * after the focus bubble has expanded. */
    menu.slots[7].occupied = true;
    bool found_overlap = false;
    int arrow_x = -1;
    int arrow_y = -1;
    for (int y = 0; y < 360 && !found_overlap; y++) {
        for (int x = 500; x < 640; x++) {
            if (wm_resource_scene_slot_at(scene, &menu, x, y) != 7)
                continue;
            if (wm_resource_scene_hit(scene, &menu, x, y).type !=
                WM_HIT_PAGE_NEXT) continue;
            found_overlap = true;
            arrow_x = x;
            arrow_y = y;
            break;
        }
    }
    assert(found_overlap);
    draw_hover(scene, &menu, 0.8f,
               (WmHit){WM_HIT_PAGE_NEXT, -1});
    draw_hover(scene, &menu, 1.1f,
               (WmHit){WM_HIT_PAGE_NEXT, -1});
    assert(wm_resource_scene_hit(scene, &menu, arrow_x, arrow_y).type ==
           WM_HIT_PAGE_NEXT);

    /* A held bubble must not resurrect a page arrow that has ceased to
     * exist on the last page. */
    menu.page = WM_PAGE_COUNT - 1;
    WmHit edge = wm_resource_scene_hit(scene, &menu, arrow_x, arrow_y);
    assert(edge.type != WM_HIT_PAGE_NEXT);

    /* Hovering a usable tile prepares frame zero once. SELECT reuses that
     * capture; a changed channel label invalidates it even in the same slot. */
    wm_menu_init(&menu);
    wm_resource_scene_restart(scene);
    WmPreviewScene *preview = wm_preview_scene_create(
        platform, assets, &menu, textures, fonts);
    assert(preview);
    WmResourceSceneFrame frame = {
        .hover = {WM_HIT_CHANNEL, 0},
        .preview_scene = preview
    };
    capture_count = 0;
    frame.elapsed_seconds = 2.0f;
    wm_resource_scene_draw(scene, &menu, &frame);
    assert(capture_count == 0);
    frame.elapsed_seconds = 2.0f + 2.0f / 60.0f;
    wm_resource_scene_draw(scene, &menu, &frame);
    assert(capture_count == 1);
    frame.elapsed_seconds += 1.0f / 60.0f;
    wm_resource_scene_draw(scene, &menu, &frame);
    assert(capture_count == 1);
    assert(wm_menu_select(&menu, 0));
    frame.elapsed_seconds += 1.0f / 60.0f;
    wm_resource_scene_draw(scene, &menu, &frame);
    assert(capture_count == 1);
    strcpy(menu.slots[0].title, "Updated Disc Channel");
    frame.elapsed_seconds += 1.0f / 60.0f;
    wm_resource_scene_draw(scene, &menu, &frame);
    assert(capture_count == 2);
    wm_preview_scene_destroy(preview);

    wm_resource_scene_destroy(scene);
    wm_texture_cache_destroy(textures);
    wm_font_cache_destroy(fonts);
    puts("Grid focus, arrow priority, and hovered preview capture are stable.");
    return 0;
}
