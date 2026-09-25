#include "wii_menu/resource_scene.h"

#ifdef NDEBUG
#undef NDEBUG
#endif
#include <assert.h>
#include <math.h>
#include <stdio.h>

typedef struct BalloonSample {
    unsigned quads;
    float left;
    float right;
    float top;
    float bottom;
    float alpha;
    bool cue;
} BalloonSample;

static uint32_t next_texture = 1;
static uint32_t balloon_texture;
static BalloonSample drawn;

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
    if (quad->texture_count == 0 || quad->textures[0] != balloon_texture)
        return;
    if (quad->vertices[0].color.a <= 0.01f) return;
    for (size_t index = 0; index < 4; index++) {
        const WmMaterialVertex *vertex = &quad->vertices[index];
        drawn.left = fminf(drawn.left, vertex->x);
        drawn.right = fmaxf(drawn.right, vertex->x);
        drawn.top = fminf(drawn.top, vertex->y);
        drawn.bottom = fmaxf(drawn.bottom, vertex->y);
        drawn.alpha = fmaxf(drawn.alpha, vertex->color.a);
    }
    drawn.quads++;
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

static BalloonSample sample_with_fade(WmResourceScene *scene,
                                      const WmMenu *menu, int frame,
                                      WmHit hover, bool fading) {
    drawn = (BalloonSample){
        .left = INFINITY, .right = -INFINITY,
        .top = INFINITY, .bottom = -INFINITY
    };
    WmResourceSceneFrame scene_frame = {
        .elapsed_seconds = (float)frame / 60.0f,
        .hover = hover,
        .suppress_balloons = fading
    };
    wm_resource_scene_draw(scene, menu, &scene_frame);
    drawn.cue = wm_resource_scene_take_balloon_sound(scene);
    return drawn;
}

static BalloonSample sample(WmResourceScene *scene, const WmMenu *menu,
                            int frame, WmHit hover) {
    return sample_with_fade(scene, menu, frame, hover, false);
}

static void expect_footer_position(BalloonSample state, float margin,
                                   bool right_side, float y) {
    assert(state.quads > 0);
    float actual_x = (state.left + state.right) * 0.5f;
    float actual_y = (state.top + state.bottom) * 0.5f;
    float logical_width = (state.right - state.left) * 832.0f / 640.0f;
    float logical_x = right_side
        ? 416.0f - margin - logical_width * 0.5f
        : -416.0f + margin + logical_width * 0.5f;
    /* N_Balloon's authored location adjustment compensates geometry, while
     * its translation still inherits the IPL root's 832/608 X scale. */
    float x = 320.0f + logical_x * 640.0f / 608.0f;
    if (fabsf(actual_x - x) >= 2.0f || fabsf(actual_y - y) >= 2.0f)
        fprintf(stderr, "Balloon center: observed %.2f, %.2f; expected %.2f, %.2f\n",
                actual_x, actual_y, x, y);
    assert(fabsf(actual_x - x) < 2.0f);
    assert(fabsf(actual_y - y) < 2.0f);
}

int main(int argc, char **argv) {
    const char *assets = argc > 1 ? argv[1] : ".local/native-assets";
    WmPlatform *platform = (WmPlatform *)1;
    WmMenu menu;
    wm_menu_init(&menu);
    if (!wm_catalog_load(&menu, assets)) {
        puts("Footer balloon comparison skipped: prepared assets unavailable.");
        return 0;
    }
    WmTextureCache *textures = wm_texture_cache_create(
        platform, assets, 128u * 1024u * 1024u);
    WmFontCache *fonts = wm_font_cache_create(
        platform, assets, 16u * 1024u * 1024u);
    assert(textures && fonts);
    assert(wm_texture_cache_resolve(textures,
        "textures/balloon/my_Balloon_a.png", &balloon_texture));
    WmResourceScene *scene = wm_resource_scene_create(
        platform, assets, &menu, textures, fonts);
    assert(scene);

    const WmHit settings = {WM_HIT_SETTINGS, -1};
    assert(sample(scene, &menu, 0, (WmHit){WM_HIT_NONE, -1}).quads == 0);
    assert(sample(scene, &menu, 0, settings).quads == 0);
    assert(sample(scene, &menu, 16, settings).quads == 0);
    BalloonSample appearance = sample(scene, &menu, 17, settings);
    assert(appearance.cue);
    BalloonSample held = sample(scene, &menu, 23, settings);
    assert(held.quads > 0 && !held.cue);
    /* Home footer anchors and margin clamping from the maintained HTML
     * controller, projected to the WAD's 640 by 456 framebuffer. */
    expect_footer_position(held, 120.0f, false, 333.0f);

    wm_resource_scene_dismiss_balloon(scene);
    BalloonSample leaving = sample(scene, &menu, 24, settings);
    assert(leaving.quads > 0 && leaving.alpha < held.alpha);
    assert(sample(scene, &menu, 29, settings).quads == 0);
    assert(sample(scene, &menu, 70, settings).quads == 0);
    wm_resource_scene_pointer_moved(scene);
    assert(!sample(scene, &menu, 71, settings).cue);
    sample(scene, &menu, 72, (WmHit){WM_HIT_NONE, -1});
    assert(sample(scene, &menu, 73, settings).quads == 0);
    assert(!sample(scene, &menu, 88, settings).cue);
    assert(sample(scene, &menu, 89, settings).cue);

    wm_resource_scene_restart(scene);
    sample(scene, &menu, 0, (WmHit){WM_HIT_NONE, -1});
    sample(scene, &menu, 0, (WmHit){WM_HIT_BOARD, -1});
    BalloonSample board = sample(scene, &menu, 23,
                                 (WmHit){WM_HIT_BOARD, -1});
    expect_footer_position(board, 120.0f, true, 334.0f);

    wm_resource_scene_restart(scene);
    sample(scene, &menu, 0, (WmHit){WM_HIT_NONE, -1});
    sample(scene, &menu, 0, (WmHit){WM_HIT_SD, -1});
    BalloonSample sd = sample(scene, &menu, 23,
                              (WmHit){WM_HIT_SD, -1});
    expect_footer_position(sd, 200.0f, false, 350.0f);

    wm_resource_scene_restart(scene);
    sample(scene, &menu, 0, (WmHit){WM_HIT_NONE, -1});
    sample(scene, &menu, 0, settings);
    assert(sample(scene, &menu, 16, settings).quads == 0);
    BalloonSample fading = sample_with_fade(scene, &menu, 17,
                                            settings, true);
    assert(fading.quads == 0 && !fading.cue);
    fading = sample_with_fade(scene, &menu, 40, settings, true);
    assert(fading.quads == 0 && !fading.cue);
    /* A stationary pointer cannot resurrect the bubble after the fade.
     * A fresh movement over the same control starts a new source wait. */
    assert(sample(scene, &menu, 41, settings).quads == 0);
    assert(sample(scene, &menu, 56, settings).quads == 0);
    assert(!sample(scene, &menu, 57, settings).cue);
    wm_resource_scene_pointer_moved(scene);
    assert(sample(scene, &menu, 58, settings).quads == 0);
    assert(!sample(scene, &menu, 73, settings).cue);
    assert(sample(scene, &menu, 74, settings).cue);
    assert(sample(scene, &menu, 80, settings).quads > 0);

    /* ChannelObj keeps an already visible bubble through its seven-frame
     * exit when the pointer briefly leaves and returns. Only after that
     * exit does the source start another twenty-frame appearance wait. */
    const WmHit disc = {WM_HIT_CHANNEL, 0};
    wm_resource_scene_restart(scene);
    sample(scene, &menu, 0, (WmHit){WM_HIT_NONE, -1});
    sample(scene, &menu, 0, disc);
    assert(sample(scene, &menu, 19, disc).quads == 0);
    assert(sample(scene, &menu, 20, disc).cue);
    assert(sample(scene, &menu, 28, disc).quads > 0);
    assert(sample(scene, &menu, 29,
                  (WmHit){WM_HIT_NONE, -1}).quads > 0);
    BalloonSample returning = sample(scene, &menu, 30, disc);
    assert(returning.quads > 0 && !returning.cue);
    assert(sample(scene, &menu, 35, disc).quads > 0);
    assert(sample(scene, &menu, 38, disc).quads == 0);
    assert(!sample(scene, &menu, 57, disc).cue);
    assert(sample(scene, &menu, 58, disc).cue);

    wm_resource_scene_destroy(scene);
    wm_texture_cache_destroy(textures);
    wm_font_cache_destroy(fonts);
    puts("Home footer and channel bubbles match HTML timing.");
    return 0;
}
