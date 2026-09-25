#include "wii_menu/pointer.h"

#include <assert.h>
#include <math.h>
#include <stdio.h>

typedef struct PointerDraw {
    WmMaterialQuad quads[4];
    size_t count;
} PointerDraw;

static PointerDraw drawn;
static uint32_t next_texture = 1;

void wm_platform_prepare_material(WmPlatform *platform,
                                  const WmMaterialQuad *quad) {
    (void)platform;
    (void)quad;
}

void wm_platform_draw_material_quad(WmPlatform *platform,
                                    const WmMaterialQuad *quad) {
    (void)platform;
    assert(drawn.count < sizeof(drawn.quads) / sizeof(drawn.quads[0]));
    drawn.quads[drawn.count++] = *quad;
}

void wm_platform_draw_vertices(WmPlatform *platform,
                               const WmDrawVertex vertices[4],
                               uint32_t texture) {
    (void)platform;
    (void)vertices;
    (void)texture;
    assert(false);
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

static bool near(float first, float second) {
    return fabsf(first - second) < 0.01f;
}

static uint32_t source_texture(WmTextureCache *textures, const char *name) {
    uint32_t handle = 0;
    assert(wm_texture_cache_resolve(textures, name, &handle));
    assert(handle);
    return handle;
}

static void assert_pointer_materials(const PointerDraw *draw,
                                     uint32_t hand_base,
                                     uint32_t hand_overlay,
                                     uint32_t shadow) {
    assert(draw->count == 2);
    const WmMaterialQuad *shadow_quad = &draw->quads[0];
    const WmMaterialQuad *hand_quad = &draw->quads[1];
    assert(shadow_quad->texture_count == 1);
    assert(shadow_quad->tev_stage_count == 1);
    assert(shadow_quad->textures[0] == shadow);
    assert(hand_quad->texture_count == 2);
    assert(hand_quad->tev_stage_count == 2);
    assert(hand_quad->textures[0] == hand_base);
    assert(hand_quad->textures[1] == hand_overlay);
    assert(hand_quad->has_blend_mode);
    /* N_SRot offsets the shadow by +3 source X and -3 source Y. */
    assert(near(shadow_quad->vertices[0].x - hand_quad->vertices[0].x,
                3.0f * 640.0f / 832.0f));
    assert(near(shadow_quad->vertices[0].y - hand_quad->vertices[0].y,
                3.0f));
}

int main(int argc, char **argv) {
    assert(!wm_pointer_grabbed_for_state(WM_CHANNEL_DRAG_NONE, false));
    assert(wm_pointer_grabbed_for_state(WM_CHANNEL_DRAG_GRAB, false));
    assert(wm_pointer_grabbed_for_state(WM_CHANNEL_DRAG_MOVING, false));
    assert(!wm_pointer_grabbed_for_state(WM_CHANNEL_DRAG_DROP_IN, false));
    assert(!wm_pointer_grabbed_for_state(WM_CHANNEL_DRAG_DROP_OUT, false));
    assert(!wm_pointer_grabbed_for_state(WM_CHANNEL_DRAG_CANCEL, false));
    assert(wm_pointer_grabbed_for_state(WM_CHANNEL_DRAG_NONE, true));

    const char *assets = argc > 1 ? argv[1] : ".local/native-assets";
    WmPlatform *platform = (WmPlatform *)1;
    WmTextureCache *textures = wm_texture_cache_create(
        platform, assets, 8u * 1024u * 1024u);
    if (!textures) {
        puts("Pointer comparison skipped: prepared assets unavailable.");
        return 0;
    }
    WmPointer *pointer = wm_pointer_create(platform, assets, textures);
    if (!pointer) {
        puts("Pointer comparison skipped: source layouts unavailable.");
        wm_texture_cache_destroy(textures);
        return 0;
    }

    uint32_t base = source_texture(
        textures, "textures/cursor/defcursor_final_p1.png");
    uint32_t default_overlay = source_texture(
        textures, "textures/cursor/defcursor_final64_a.png");
    uint32_t grab_overlay = source_texture(
        textures, "textures/cursor/defcursor_final64_b.png");
    uint32_t default_shadow = source_texture(
        textures, "textures/cursor/defcursor_sd_a.png");
    uint32_t grab_shadow = source_texture(
        textures, "textures/cursor/defcursor_sd_b.png");
    assert(default_overlay != grab_overlay);
    assert(default_shadow != grab_shadow);

    /* A new pointer is hidden until the first input event. At the source
     * center its parent transform is the identity in the 832-wide layout. */
    float matrix[12];
    wm_pointer_matrix(320.0f, 228.0f, matrix);
    assert(near(matrix[3], 0.0f) && near(matrix[7], 0.0f));
    wm_pointer_draw(pointer);
    assert(drawn.count == 0);

    wm_pointer_move(pointer, 320.0f, 228.0f);
    wm_texture_cache_begin_frame(textures);
    wm_pointer_draw(pointer);
    assert_pointer_materials(&drawn, base, default_overlay, default_shadow);
    PointerDraw normal = drawn;

    drawn.count = 0;
    wm_pointer_set_grabbed(pointer, true);
    wm_texture_cache_begin_frame(textures);
    wm_pointer_draw(pointer);
    assert_pointer_materials(&drawn, base, grab_overlay, grab_shadow);
    PointerDraw grabbed = drawn;
    /* The source Cat pane is shifted 8 units left and 24 units up from Def.
     * Horizontal units are fitted from its 832-wide layout into 640 pixels. */
    assert(near(grabbed.quads[1].vertices[0].x -
                normal.quads[1].vertices[0].x,
                -8.0f * 640.0f / 832.0f));
    assert(near(grabbed.quads[1].vertices[0].y -
                normal.quads[1].vertices[0].y, -24.0f));

    drawn.count = 0;
    wm_pointer_set_grabbed(pointer, false);
    wm_pointer_move(pointer, 360.0f, 240.0f);
    wm_texture_cache_begin_frame(textures);
    wm_pointer_draw(pointer);
    assert_pointer_materials(&drawn, base, default_overlay, default_shadow);
    assert(near(drawn.quads[1].vertices[0].x -
                normal.quads[1].vertices[0].x, 40.0f));
    assert(near(drawn.quads[1].vertices[0].y -
                normal.quads[1].vertices[0].y, 12.0f));

    drawn.count = 0;
    wm_pointer_set_grabbed(pointer, true);
    wm_pointer_move(pointer, -4.0f, 240.0f);
    wm_texture_cache_begin_frame(textures);
    wm_pointer_draw(pointer);
    assert_pointer_materials(&drawn, base, grab_overlay, grab_shadow);

    drawn.count = 0;
    wm_pointer_hide(pointer);
    wm_pointer_draw(pointer);
    assert(drawn.count == 0);
    wm_pointer_destroy(pointer);
    wm_texture_cache_destroy(textures);
    puts("Source default and grabbed pointer material draws passed.");
    return 0;
}
