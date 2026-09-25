#include "wii_menu/health_scene.h"
#include "wii_menu/scene_fader.h"

#include <assert.h>
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

/* State and resource-pose tests never submit GPU draws. */
void wm_platform_prepare_material(WmPlatform *platform,
                                  const WmMaterialQuad *quad) {
    (void)platform;
    (void)quad;
}

void wm_platform_draw_material_quad(WmPlatform *platform,
                                    const WmMaterialQuad *quad) {
    (void)platform;
    (void)quad;
    assert(false);
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
    (void)width;
    (void)height;
    (void)rgba;
    assert(false);
    return 0;
}

void wm_platform_destroy_texture(WmPlatform *platform, uint32_t texture) {
    (void)platform;
    (void)texture;
    assert(false);
}

typedef struct AlphaLookup {
    const char *name;
    float alpha;
    bool found;
} AlphaLookup;

static bool read_alpha(void *opaque, const WmLayoutPaneView *pane) {
    AlphaLookup *lookup = opaque;
    if (strcmp(lookup->name, pane->name) == 0) {
        lookup->alpha = pane->alpha;
        lookup->found = true;
    }
    return true;
}

static float pane_alpha(const WmLayout *layout, const char *name) {
    AlphaLookup lookup = {.name = name};
    wm_layout_visit_all_transforms(layout, true, WM_LAYOUT_IPL,
                                    NULL, read_alpha, &lookup);
    assert(lookup.found);
    return lookup.alpha;
}

static bool visible(const WmLayout *layout, const char *name) {
    WmLayoutPaneState state;
    assert(wm_layout_pane_state(layout, name, &state));
    return (state.flags & 1u) != 0;
}

int main(int argc, char **argv) {
    const char *assets = argc > 1 ? argv[1] : ".local/native-assets";
    char path[4096];
    int length = snprintf(path, sizeof(path),
                          "%s/layouts/health/it_Has_a.json", assets);
    assert(length > 0 && length < (int)sizeof(path));
    FILE *file = fopen(path, "rb");
    if (!file) {
        puts("Health scene resource test skipped: local WAD export absent.");
        return 0;
    }
    fclose(file);

    WmHealthScene *health = wm_health_scene_create(
        (WmPlatform *)1, assets, (WmTextureCache *)1,
        (WmFontCache *)1, true, NULL);
    assert(health);
    assert(wm_health_scene_active(health));
    assert(!wm_health_scene_ready(health));
    assert(!wm_health_scene_accept(health));
    assert(wm_health_scene_pose(health));
    const WmLayout *layout = wm_health_scene_layout(health);
    assert(visible(layout, "Has_US_ENG"));
    assert(!visible(layout, "Has_JPN"));
    assert(!visible(layout, "Push_US_ENG"));

    wm_health_scene_advance(health, 90.0f);
    assert(wm_health_scene_phase(health) == WM_HEALTH_ENTER);
    assert(!wm_health_scene_accept(health));
    wm_health_scene_advance(health, 1.0f);
    assert(wm_health_scene_ready(health));
    assert(wm_health_scene_pose(health));
    assert(visible(layout, "Push_US_ENG"));
    assert(!visible(layout, "Push_JPN"));
    assert(wm_health_scene_accept(health));
    assert(!wm_health_scene_accept(health));
    /* Samples from the maintained HTML controller with the same WAD track.
     * The warning fades continuously to black before the grid is handed off. */
    static const struct {
        float frame;
        float alpha;
    } exit_samples[] = {
        {0.0f, 255.0f},
        {5.0f, 242.0139f},
        {10.0f, 207.7778f},
        {15.0f, 159.3750f},
        {20.0f, 103.8889f},
        {25.0f, 48.4028f},
        {30.0f, 0.0f}
    };
    float previous_frame = 0.0f;
    for (size_t index = 0;
         index < sizeof(exit_samples) / sizeof(exit_samples[0]); index++) {
        wm_health_scene_advance(health,
                                exit_samples[index].frame - previous_frame);
        assert(wm_health_scene_pose(health));
        assert(fabsf(pane_alpha(layout, "N_All") -
                     exit_samples[index].alpha / 255.0f) < 0.002f);
        previous_frame = exit_samples[index].frame;
    }
    assert(wm_health_scene_active(health));
    wm_health_scene_advance(health, 1.0f);
    assert(!wm_health_scene_active(health));
    assert(wm_health_scene_pose(health));
    assert(fabsf(pane_alpha(layout, "N_All")) < 0.001f);
    /* The last Health frame and first grid frame both display black. The
     * source ColorFader then reveals a stationary grid over 22 updates. */
    assert(wm_menu_entrance_alpha(0.0f) == 1.0f);
    assert(wm_menu_entrance_alpha(23.0f) == 1.0f);
    assert(wm_menu_entrance_alpha(24.0f) == 1.0f);
    assert(fabsf(wm_menu_entrance_alpha(25.0f) -
                 243.0f / 255.0f) < 0.001f);
    assert(wm_menu_entrance_alpha(44.0f) == 0.0f);
    assert(!wm_menu_entrance_complete(44.0f));
    assert(wm_menu_entrance_complete(45.0f));
    wm_health_scene_advance(health, 10.0f);
    assert(wm_health_scene_pose(health));
    assert(fabsf(pane_alpha(layout, "N_All")) < 0.001f);

    wm_health_scene_reset(health, true);
    wm_health_scene_advance(health, 3691.0f);
    assert(wm_health_scene_phase(health) == WM_HEALTH_LEAVE);
    wm_health_scene_advance(health, 31.0f);
    assert(wm_health_scene_phase(health) == WM_HEALTH_DONE);
    wm_health_scene_reset(health, false);
    assert(!wm_health_scene_active(health));
    wm_health_scene_destroy(health);

    health = wm_health_scene_create(
        (WmPlatform *)1, assets, (WmTextureCache *)1,
        (WmFontCache *)1, true, "US_FRA");
    assert(health);
    assert(wm_health_scene_pose(health));
    layout = wm_health_scene_layout(health);
    assert(visible(layout, "Has_US_FRA"));
    assert(!visible(layout, "Has_US_ENG"));
    wm_health_scene_advance(health, 91.0f);
    assert(wm_health_scene_pose(health));
    assert(visible(layout, "Push_US_FRA"));
    wm_health_scene_destroy(health);
    puts("Health scene timing and WAD pose tests passed.");
    return 0;
}
