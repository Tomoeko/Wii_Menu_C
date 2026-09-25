#include "wii_menu/sd_scene.h"
#include "wii_menu/layout_runtime.h"
#include "wii_menu/resource_scene.h"
#include "wii_menu/source_hit.h"

#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <string.h>

typedef struct RenderProbe {
    unsigned visible_glyphs;
    unsigned page_label_glyphs;
    unsigned visible_page_label_glyphs;
    float visible_page_label_alpha;
    unsigned footer_title_glyphs;
    float footer_title_left;
    float footer_title_right;
    float footer_title_alpha;
    float next_button_left;
    float back_button_alpha;
    float back_center_alpha;
    float back_right_alpha;
    unsigned back_label_glyphs;
    float back_label_alpha;
} RenderProbe;

static bool capture_render;
static uint32_t next_texture_handle = 1;
static RenderProbe render_probe;

static void reset_render_probe(void) {
    render_probe = (RenderProbe){
        .next_button_left = INFINITY,
        .visible_page_label_alpha = -1.0f,
        .back_button_alpha = -1.0f,
        .back_center_alpha = -1.0f,
        .back_right_alpha = -1.0f,
        .back_label_alpha = -1.0f,
        .footer_title_left = INFINITY,
        .footer_title_right = -INFINITY,
        .footer_title_alpha = INFINITY
    };
}

static void assert_back_visuals(float expected_alpha) {
    assert(fabsf(render_probe.back_button_alpha - expected_alpha) < 0.015f);
    assert(fabsf(render_probe.back_center_alpha - expected_alpha) < 0.015f);
    assert(fabsf(render_probe.back_right_alpha - expected_alpha) < 0.015f);
    assert(render_probe.back_label_glyphs >= 4);
    assert(fabsf(render_probe.back_label_alpha - expected_alpha) < 0.015f);
}

static void assert_footer_title_unchanged(const RenderProbe *reference) {
    assert(render_probe.footer_title_glyphs ==
           reference->footer_title_glyphs);
    assert(fabsf(render_probe.footer_title_left -
                 reference->footer_title_left) < 0.01f);
    assert(fabsf(render_probe.footer_title_right -
                 reference->footer_title_right) < 0.01f);
    assert(fabsf(render_probe.footer_title_alpha -
                 reference->footer_title_alpha) < 0.01f);
}

void wm_platform_begin(WmPlatform *platform, WmColor clear_color) {
    (void)platform;
    (void)clear_color;
    assert(capture_render);
}

void wm_platform_end(WmPlatform *platform) {
    (void)platform;
    assert(capture_render);
}

void wm_platform_prepare_material(WmPlatform *platform,
                                  const WmMaterialQuad *quad) {
    (void)platform;
    (void)quad;
}

void wm_platform_set_clip(WmPlatform *platform, const WmClipRect *rect) {
    (void)platform;
    (void)rect;
    assert(capture_render);
}

void wm_platform_draw_quad(WmPlatform *platform, const WmQuad *quad) {
    (void)platform;
    (void)quad;
    assert(capture_render);
}

void wm_platform_draw_vertices(WmPlatform *platform,
                               const WmDrawVertex vertices[4],
                               uint32_t texture) {
    (void)platform;
    (void)texture;
    assert(capture_render);
    float x = vertices[0].x;
    float y = vertices[0].y;
    if (vertices[0].color.a > 0.01f)
        render_probe.visible_glyphs++;
    if (x > 190.0f && x < 265.0f && y > 328.0f && y < 342.0f) {
        render_probe.back_label_glyphs++;
        render_probe.back_label_alpha = fmaxf(
            render_probe.back_label_alpha, vertices[0].color.a);
    }
    if (x > 280.0f && x < 355.0f && y > 320.0f && y < 345.0f) {
        render_probe.page_label_glyphs++;
    }
    if (x > 0.0f && x < 640.0f && y > 320.0f && y < 345.0f) {
        render_probe.visible_page_label_glyphs++;
        render_probe.visible_page_label_alpha = fmaxf(
            render_probe.visible_page_label_alpha, vertices[0].color.a);
    }
    if (x > 225.0f && x < 415.0f && y > 385.0f && y < 420.0f) {
        render_probe.footer_title_glyphs++;
        render_probe.footer_title_left = fminf(render_probe.footer_title_left, x);
        render_probe.footer_title_right = fmaxf(render_probe.footer_title_right,
                                                vertices[1].x);
        render_probe.footer_title_alpha = fminf(
            render_probe.footer_title_alpha, vertices[0].color.a);
    }
}

void wm_platform_draw_material_quad(WmPlatform *platform,
                                    const WmMaterialQuad *quad) {
    (void)platform;
    assert(capture_render);
    float x = quad->vertices[0].x;
    float y = quad->vertices[0].y;
    float width = quad->vertices[1].x - x;
    if (y > 305.0f && y < 316.0f && width > 20.0f && width < 30.0f) {
        if (x > 310.0f && x < 340.0f &&
            x < render_probe.next_button_left) {
            render_probe.next_button_left = x;
        }
        if (x > 115.0f && x < 175.0f) {
            render_probe.back_button_alpha = fmaxf(
                render_probe.back_button_alpha,
                quad->vertices[0].color.a);
        }
        if (x > 265.0f && x < 315.0f) {
            render_probe.back_right_alpha = fmaxf(
                render_probe.back_right_alpha,
                quad->vertices[0].color.a);
        }
    }
    if (y > 305.0f && y < 316.0f && width > 100.0f &&
        width < 145.0f && x > 150.0f && x < 195.0f) {
        render_probe.back_center_alpha = fmaxf(
            render_probe.back_center_alpha,
            quad->vertices[0].color.a);
    }
}

uint32_t wm_platform_create_texture(WmPlatform *platform, int width,
                                    int height, const uint8_t *rgba) {
    (void)platform;
    (void)width;
    (void)height;
    (void)rgba;
    assert(capture_render);
    return next_texture_handle++;
}

void wm_platform_destroy_texture(WmPlatform *platform, uint32_t texture) {
    (void)platform;
    (void)texture;
    assert(capture_render);
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

static void test_page_arithmetic(void) {
    assert(!wm_sd_page_can_move(0, -1));
    assert(wm_sd_page_can_move(0, 1));
    assert(wm_sd_page_can_move(19, -1));
    assert(!wm_sd_page_can_move(19, 1));
    assert(!wm_sd_page_can_move(20, -1));
    assert(!wm_sd_page_can_move(10, 2));
    assert(fabsf(wm_sd_scroll_animation_frame(-1, 0.0f)) < 0.001f);
    assert(fabsf(wm_sd_scroll_animation_frame(-1, 10.0f) - 10.0f) < 0.001f);
    assert(fabsf(wm_sd_scroll_animation_frame(1, 0.0f) - 40.0f) < 0.001f);
    assert(fabsf(wm_sd_scroll_animation_frame(1, 20.0f) - 60.0f) < 0.001f);
    assert(fabsf(wm_sd_scroll_animation_frame(1, 100.0f) - 60.0f) < 0.001f);
}

static WmSdScene *load_scene(int argc, char **argv) {
    const char *assets = argc > 1 ? argv[1] : ".local/native-assets";
    char path[4096];
    int length = snprintf(path, sizeof(path),
                          "%s/layouts/sdChanSel/mn_SdcardMenu_a.json", assets);
    assert(length > 0 && length < (int)sizeof(path));
    FILE *check = fopen(path, "rb");
    if (!check) {
        puts("SD Card source test skipped: local WAD export absent.");
        return NULL;
    }
    fclose(check);
    WmSdScene *scene = wm_sd_scene_create(
        (WmPlatform *)1, assets, (WmTextureCache *)1, (WmFontCache *)1);
    assert(scene);
    return scene;
}

static void test_grid_sd_button_anchor(int argc, char **argv) {
    const char *assets = argc > 1 ? argv[1] : ".local/native-assets";
    char path[4096];
    int length = snprintf(path, sizeof(path),
                          "%s/layouts/cmnBtn/mn_Sdcard_Btn.json", assets);
    assert(length > 0 && length < (int)sizeof(path));
    char error[160] = {0};
    WmLayout *button = wm_layout_load_json(path, error, sizeof(error));
    assert(button);
    assert(wm_layout_pose(button, NULL, 0));
    /* HTML display.sdX is -245 for 16:9. The shared 640-wide raster maps
     * that source position to 131.54, at Y=400 for the -172 footer anchor. */
    const float parent[12] = {
        1, 0, 0, -245,
        0, 1, 0, -172,
        0, 0, 1, 0
    };
    WmSourceRect rect;
    assert(wm_source_pane_rect(button, "Ac", true, WM_LAYOUT_IPL,
                               parent, &rect));
    float center_x = rect.x + rect.width * 0.5f;
    float center_y = rect.y + rect.height * 0.5f;
    assert(fabsf(center_x - 131.538f) < 0.05f);
    assert(fabsf(center_y - 400.0f) < 0.05f);
    assert(fabsf(rect.width - 33.846f) < 0.05f);
    assert(fabsf(rect.height - 55.0f) < 0.05f);
    wm_layout_destroy(button);

    WmMenu menu;
    wm_menu_init(&menu);
    capture_render = true;
    WmTextureCache *textures = wm_texture_cache_create(
        (WmPlatform *)1, assets, 128u * 1024u * 1024u);
    WmFontCache *fonts = wm_font_cache_create(
        (WmPlatform *)1, assets, 16u * 1024u * 1024u);
    assert(textures && fonts);
    WmResourceScene *grid = wm_resource_scene_create(
        (WmPlatform *)1, assets, &menu, textures, fonts);
    assert(grid);
    assert(wm_resource_scene_hit(grid, &menu, 132, 400).type == WM_HIT_SD);
    assert(wm_resource_scene_hit(grid, &menu, 183, 400).type != WM_HIT_SD);
    wm_resource_scene_destroy(grid);
    wm_texture_cache_destroy(textures);
    wm_font_cache_destroy(fonts);
    capture_render = false;
}

static bool find_event(WmSdScene *scene, WmSdEventType wanted) {
    WmSdEvent event;
    bool found = false;
    while (wm_sd_scene_take_event(scene, &event)) {
        if (event.type == wanted) found = true;
    }
    return found;
}

typedef struct AlphaProbe {
    const char *name;
    bool found;
    float alpha;
} AlphaProbe;

static bool probe_alpha(void *context, const WmLayoutPaneView *pane) {
    AlphaProbe *probe = context;
    if (strcmp(probe->name, pane->name) == 0) {
        probe->found = true;
        probe->alpha = pane->alpha;
    }
    return true;
}

static float displayed_alpha(WmLayout *layout, const char *name) {
    AlphaProbe probe = {.name = name};
    WmLayoutDrawOptions options = {
        .wide = true, .mode = WM_LAYOUT_IPL, .alpha = 1.0f,
        .on_pane = probe_alpha, .context = &probe
    };
    wm_layout_draw(layout, &options);
    assert(probe.found);
    return probe.alpha;
}

static void test_dialog_alpha(int argc, char **argv) {
    const char *assets = argc > 1 ? argv[1] : ".local/native-assets";
    char path[4096];
    int length = snprintf(path, sizeof(path),
                          "%s/layouts/dlgWdw/my_DialogWindow_a2.json", assets);
    assert(length > 0 && length < (int)sizeof(path));
    char error[160] = {0};
    WmLayout *layout = wm_layout_load_json(path, error, sizeof(error));
    assert(layout);
    assert(wm_layout_pose(layout, NULL, 0));
    assert(wm_layout_set_pane_alpha(layout, "T_Dialog", 128.0f));
    assert(fabsf(displayed_alpha(layout, "T_Dialog") - 128.0f / 255.0f)
           < 0.001f);
    assert(!wm_layout_set_pane_alpha(layout, "T_Dialog", -1.0f));
    assert(!wm_layout_set_pane_alpha(layout, "missing", 64.0f));
    assert(wm_layout_set_descendant_alpha(layout, "N_BtnA_Pic", 64.0f));
    assert(fabsf(displayed_alpha(layout, "BtnA0") - 64.0f / 255.0f)
           < 0.001f);
    assert(wm_layout_pose(layout, NULL, 0));
    assert(displayed_alpha(layout, "T_Dialog") > 0.9f);
    wm_layout_destroy(layout);
}

static void test_arrow_source_intervals(int argc, char **argv) {
    const char *assets = argc > 1 ? argv[1] : ".local/native-assets";
    char path[4096];
    int length = snprintf(path, sizeof(path),
                          "%s/layouts/sdButton/mn_SdcardMenu_b.json", assets);
    assert(length > 0 && length < (int)sizeof(path));
    char error[160] = {0};
    WmLayout *footer = wm_layout_load_json(path, error, sizeof(error));
    assert(footer);
    const struct {
        const char *name;
        float frames;
    } intervals[] = {
        {"mn_SdcardMenu_b_ArwL_in", 11.0f},
        {"mn_SdcardMenu_b_ArwL_out", 11.0f},
        {"mn_SdcardMenu_b_ArwL_rollover", 9.0f},
        {"mn_SdcardMenu_b_ArwL_rollout", 13.0f},
        {"mn_SdcardMenu_b_ArwL_on", 28.0f},
        {"mn_SdcardMenu_b_ArwR_in", 11.0f},
        {"mn_SdcardMenu_b_ArwR_out", 11.0f},
        {"mn_SdcardMenu_b_ArwR_rollover", 9.0f},
        {"mn_SdcardMenu_b_ArwR_rollout", 13.0f},
        {"mn_SdcardMenu_b_ArwR_on", 28.0f}
    };
    for (size_t index = 0; index < sizeof(intervals) / sizeof(intervals[0]);
         index++) {
        WmLayoutAnimationInfo info;
        assert(wm_layout_animation_info(footer, intervals[index].name, &info));
        assert(info.frames == intervals[index].frames);
        assert(!info.loop);
    }
    wm_layout_destroy(footer);
}

static void test_page_and_media(WmSdScene *scene) {
    assert(!wm_sd_scene_open(scene, 20, true, WM_SD_MEDIA_READY));
    assert(wm_sd_scene_open(scene, 0, true, WM_SD_MEDIA_READY));
    assert(wm_sd_scene_is_locked(scene));
    wm_sd_scene_advance(scene, 32.0f, false);
    assert(wm_sd_scene_is_locked(scene));
    wm_sd_scene_advance(scene, 1.0f, false);
    assert(wm_sd_scene_is_locked(scene));
    wm_sd_scene_advance(scene, 15.0f, false);
    assert(wm_sd_scene_is_locked(scene));
    wm_sd_scene_advance(scene, 1.0f, false);
    assert(!wm_sd_scene_is_locked(scene));
    assert(wm_sd_scene_hit(scene, 70, 395).control == WM_SD_CONTROL_BACK);
    assert(wm_sd_scene_hit(scene, 570, 395).control == WM_SD_CONTROL_HELP);
    assert(wm_sd_scene_hit(scene, 615, 175).control == WM_SD_CONTROL_NEXT);
    assert(wm_sd_scene_hit(scene, 20, 175).control == WM_SD_CONTROL_NONE);
    assert(!wm_sd_scene_activate(scene,
           (WmSdHit){WM_SD_CONTROL_PREVIOUS, 0}));
    assert(wm_sd_scene_activate(scene, (WmSdHit){WM_SD_CONTROL_NEXT, 0}));
    assert(wm_sd_scene_phase(scene) == WM_SD_SCROLL);
    wm_sd_scene_advance(scene, 19.0f, false);
    assert(wm_sd_scene_page(scene) == 0);
    wm_sd_scene_advance(scene, 1.0f, false);
    assert(wm_sd_scene_page(scene) == 1);
    wm_sd_scene_advance(scene, 11.0f, false);
    assert(wm_sd_scene_hit(scene, 20, 175).control ==
           WM_SD_CONTROL_PREVIOUS);
    assert(find_event(scene, WM_SD_EVENT_PAGE_CHANGED));
    assert(wm_sd_scene_activate(scene,
           (WmSdHit){WM_SD_CONTROL_PREVIOUS, 0}));
    wm_sd_scene_advance(scene, 20.0f, false);
    assert(wm_sd_scene_page(scene) == 0);
    assert(wm_sd_scene_back(scene));
    assert(wm_sd_scene_phase(scene) == WM_SD_LEAVING);
    assert(find_event(scene, WM_SD_EVENT_EXIT));

    assert(wm_sd_scene_open(scene, 5, true, WM_SD_MEDIA_ABSENT));
    wm_sd_scene_advance(scene, 49.0f, false);
    assert(!wm_sd_scene_is_locked(scene));
    assert(!wm_sd_scene_activate(scene,
           (WmSdHit){WM_SD_CONTROL_NEXT, 0}));
    assert(wm_sd_scene_activate(scene, (WmSdHit){WM_SD_CONTROL_HELP, 0}));
    assert(wm_sd_scene_help_open(scene));

    wm_sd_scene_set_card_ready(scene, false);
    assert(wm_sd_scene_open(scene, 0, true, WM_SD_MEDIA_READY));
    wm_sd_scene_advance(scene, 100.0f, false);
    assert(wm_sd_scene_is_locked(scene));
    wm_sd_scene_set_card_ready(scene, true);
    wm_sd_scene_advance(scene, 1.0f, false);
    assert(wm_sd_scene_is_locked(scene));
    wm_sd_scene_advance(scene, 15.0f, false);
    assert(wm_sd_scene_is_locked(scene));
    wm_sd_scene_advance(scene, 1.0f, false);
    assert(!wm_sd_scene_is_locked(scene));
}

static void test_welcome_and_help(WmSdScene *scene) {
    assert(wm_sd_scene_open(scene, 0, false, WM_SD_MEDIA_READY));
    wm_sd_scene_advance(scene, 30.0f, true);
    assert(!wm_sd_scene_help_open(scene));
    assert(wm_sd_scene_is_locked(scene));
    wm_sd_scene_advance(scene, 0.0f, false);
    assert(wm_sd_scene_help_open(scene));
    assert(find_event(scene, WM_SD_EVENT_HELP_OPEN));
    wm_sd_scene_advance(scene, 25.0f, false);
    assert(!wm_sd_scene_is_locked(scene));
    assert(!wm_sd_scene_activate(scene,
           (WmSdHit){WM_SD_CONTROL_HELP_BACK, 0}));
    for (unsigned page = 0; page < 4; page++) {
        assert(wm_sd_scene_help_page(scene) == page);
        assert(wm_sd_scene_activate(scene,
               (WmSdHit){WM_SD_CONTROL_HELP_NEXT, 0}));
        wm_sd_scene_advance(scene, page == 3 ? 42.0f : 41.0f, false);
    }
    assert(!wm_sd_scene_help_open(scene));
    assert(wm_sd_scene_help_seen(scene));
    assert(find_event(scene, WM_SD_EVENT_HELP_CLOSE));
    assert(wm_sd_scene_is_locked(scene));
    wm_sd_scene_advance(scene, 49.0f, false);
    assert(!wm_sd_scene_is_locked(scene));
}

static void test_channel_mapping(WmSdScene *scene) {
    WmSdChannel invalid = {0, "../../invalid"};
    assert(!wm_sd_scene_set_channels(scene, &invalid, 1));
    assert(wm_sd_scene_set_channels(scene, NULL, 0));
    WmSdChannel channel = {0, "0001000148414a45"};
    assert(wm_sd_scene_set_channels(scene, &channel, 1));
    assert(wm_sd_scene_channel_id(scene, 0));
    assert(!wm_sd_scene_set_channels(scene,
           (WmSdChannel[]){{0, channel.title_id}, {0, channel.title_id}}, 2));
    assert(!wm_sd_scene_set_channels(scene,
           (WmSdChannel[]){{0, channel.title_id}, {1, channel.title_id}}, 2));
    assert(wm_sd_scene_open(scene, 0, true, WM_SD_MEDIA_READY));
    wm_sd_scene_advance(scene, 33.0f, false);
    assert(wm_sd_scene_is_locked(scene));
    wm_sd_scene_advance(scene, 59.0f, false);
    assert(wm_sd_scene_is_locked(scene));
    wm_sd_scene_advance(scene, 17.0f, false);
    assert(!wm_sd_scene_is_locked(scene));
    assert(wm_sd_scene_activate(scene,
           (WmSdHit){WM_SD_CONTROL_CHANNEL, 0}));
    assert(find_event(scene, WM_SD_EVENT_CHANNEL_SELECTED));
}

static void test_restart_reset(WmSdScene *scene) {
    assert(wm_sd_scene_open(scene, 4, false, WM_SD_MEDIA_READY));
    wm_sd_scene_reset(scene);
    assert(wm_sd_scene_phase(scene) == WM_SD_CLOSED);
    assert(wm_sd_scene_page(scene) == 4);
    assert(!wm_sd_scene_help_seen(scene));
    assert(wm_sd_scene_channel_id(scene, 0));
    WmSdEvent event;
    assert(!wm_sd_scene_take_event(scene, &event));
    assert(wm_sd_scene_open(scene, wm_sd_scene_page(scene),
                            wm_sd_scene_help_seen(scene),
                            WM_SD_MEDIA_READY));
    assert(wm_sd_scene_phase(scene) == WM_SD_ACTIVE);
}

static void test_source_draw_transitions(int argc, char **argv) {
    const char *assets = argc > 1 ? argv[1] : ".local/native-assets";
    WmPlatform *platform = (WmPlatform *)1;
    capture_render = true;
    WmTextureCache *textures = wm_texture_cache_create(
        platform, assets, 128u * 1024u * 1024u);
    WmFontCache *fonts = wm_font_cache_create(
        platform, assets, 16u * 1024u * 1024u);
    assert(textures && fonts);
    WmSdScene *scene = wm_sd_scene_create(platform, assets, textures, fonts);
    assert(scene);

    assert(wm_sd_scene_open(scene, 0, true, WM_SD_MEDIA_READY));
    wm_sd_scene_advance(scene, 49.0f, false);
    reset_render_probe();
    wm_sd_scene_draw(scene);
    assert(render_probe.page_label_glyphs >= 4);
    assert(render_probe.footer_title_glyphs >= 8);
    RenderProbe footer_reference = render_probe;
    assert(wm_sd_scene_activate(scene, (WmSdHit){WM_SD_CONTROL_NEXT, 0}));
    for (unsigned frame = 0; frame <= 20; frame++) {
        if (frame) wm_sd_scene_advance(scene, 1.0f, false);
        reset_render_probe();
        wm_sd_scene_draw(scene);
        assert_footer_title_unchanged(&footer_reference);
        assert(render_probe.visible_page_label_glyphs >= 4);
        assert(render_probe.visible_page_label_alpha > 0.99f);
        if (frame == 19) {
            /* The incoming /20 label uses the hidden N_Clock2 anchor. */
            assert(render_probe.page_label_glyphs >= 4);
        }
    }
    assert(wm_sd_scene_page(scene) == 1);
    assert(render_probe.page_label_glyphs >= 4);

    assert(wm_sd_scene_activate(scene,
                                (WmSdHit){WM_SD_CONTROL_PREVIOUS, 0}));
    for (unsigned frame = 0; frame <= 20; frame++) {
        if (frame) wm_sd_scene_advance(scene, 1.0f, false);
        reset_render_probe();
        wm_sd_scene_draw(scene);
        assert_footer_title_unchanged(&footer_reference);
        assert(render_probe.visible_page_label_glyphs >= 4);
        assert(render_probe.visible_page_label_alpha > 0.99f);
    }
    assert(wm_sd_scene_page(scene) == 0);
    assert(render_probe.page_label_glyphs >= 4);

    assert(wm_sd_scene_open(scene, 0, false, WM_SD_MEDIA_READY));
    wm_sd_scene_advance(scene, 30.0f, true);
    wm_sd_scene_advance(scene, 0.0f, false);
    wm_sd_scene_advance(scene, 25.0f, false);
    wm_sd_scene_hover(scene, (WmSdHit){WM_SD_CONTROL_HELP_NEXT, 0});
    wm_sd_scene_advance(scene, 6.0f, false);
    assert(wm_sd_scene_activate(scene,
                                (WmSdHit){WM_SD_CONTROL_HELP_NEXT, 0}));
    wm_sd_scene_advance(scene, 21.0f, false);
    reset_render_probe();
    wm_sd_scene_draw(scene);
    /* Source Help restarts the pointed Next hover at click completion. */
    assert(render_probe.next_button_left > 325.0f &&
           render_probe.next_button_left < 332.0f);
    assert(render_probe.back_button_alpha < 0.0f);

    wm_sd_scene_advance(scene, 6.0f, false);
    reset_render_probe();
    wm_sd_scene_draw(scene);
    assert(render_probe.next_button_left > 315.0f &&
           render_probe.next_button_left < 323.0f);

    wm_sd_scene_advance(scene, 4.0f, false);
    assert(wm_sd_scene_help_page(scene) == 1);
    reset_render_probe();
    wm_sd_scene_draw(scene);
    assert(render_probe.back_button_alpha < 0.0f);
    for (unsigned frame = 1; frame <= 10; frame++) {
        wm_sd_scene_advance(scene, 1.0f, false);
        reset_render_probe();
        wm_sd_scene_draw(scene);
        float expected = fminf(255.0f, frame * 26.0f) / 255.0f;
        assert_back_visuals(expected);
    }

    /* Back must stay visible through every later select/text phase boundary.
     * Sample each rendered frame to catch a one-frame reset or blink. */
    for (unsigned destination = 2; destination <= 3; destination++) {
        assert(wm_sd_scene_help_page(scene) == destination - 1);
        assert(wm_sd_scene_activate(scene,
                                    (WmSdHit){WM_SD_CONTROL_HELP_NEXT, 0}));
        for (unsigned frame = 0; frame <= 41; frame++) {
            if (frame) wm_sd_scene_advance(scene, 1.0f, false);
            reset_render_probe();
            wm_sd_scene_draw(scene);
            assert_back_visuals(1.0f);
        }
        assert(wm_sd_scene_help_page(scene) == destination);
    }

    for (unsigned step = 0; step < 2; step++) {
        assert(wm_sd_scene_activate(scene,
                                    (WmSdHit){WM_SD_CONTROL_HELP_BACK, 0}));
        for (unsigned frame = 0; frame <= 41; frame++) {
            if (frame) wm_sd_scene_advance(scene, 1.0f, false);
            reset_render_probe();
            wm_sd_scene_draw(scene);
            assert_back_visuals(1.0f);
        }
        assert(wm_sd_scene_help_page(scene) == 2 - step);
    }

    wm_sd_scene_destroy(scene);
    wm_texture_cache_destroy(textures);
    wm_font_cache_destroy(fonts);
    capture_render = false;
}

static void test_footer_balloon_handoff(int argc, char **argv) {
    const char *assets = argc > 1 ? argv[1] : ".local/native-assets";
    WmPlatform *platform = (WmPlatform *)1;
    capture_render = true;
    WmTextureCache *textures = wm_texture_cache_create(
        platform, assets, 128u * 1024u * 1024u);
    WmFontCache *fonts = wm_font_cache_create(
        platform, assets, 16u * 1024u * 1024u);
    assert(textures && fonts);
    WmSdScene *scene = wm_sd_scene_create(platform, assets, textures, fonts);
    assert(scene);
    assert(wm_sd_scene_open(scene, 0, true, WM_SD_MEDIA_READY));
    wm_sd_scene_advance(scene, 49.0f, false);

    reset_render_probe();
    wm_sd_scene_draw(scene);
    unsigned without_balloon = render_probe.visible_glyphs;
    wm_sd_scene_hover(scene, (WmSdHit){WM_SD_CONTROL_BACK, 0});
    wm_sd_scene_advance(scene, 23.0f, false);
    assert(find_event(scene, WM_SD_EVENT_BALLOON_SOUND));
    reset_render_probe();
    wm_sd_scene_draw(scene);
    unsigned with_back = render_probe.visible_glyphs;
    assert(with_back > without_balloon);

    /* HTML retains the old bubble during its six-frame leave while Help
     * starts a fresh 17-frame wait. Neither button's label may pop. */
    wm_sd_scene_hover(scene, (WmSdHit){WM_SD_CONTROL_HELP, 0});
    for (unsigned frame = 1; frame <= 6; frame++) {
        wm_sd_scene_advance(scene, 1.0f, false);
        reset_render_probe();
        wm_sd_scene_draw(scene);
        if (frame < 6)
            assert(render_probe.visible_glyphs == with_back);
        else
            assert(render_probe.visible_glyphs == without_balloon);
    }
    assert(!find_event(scene, WM_SD_EVENT_BALLOON_SOUND));
    wm_sd_scene_advance(scene, 11.0f, false);
    assert(find_event(scene, WM_SD_EVENT_BALLOON_SOUND));
    wm_sd_scene_advance(scene, 1.0f, false);
    reset_render_probe();
    wm_sd_scene_draw(scene);
    assert(render_probe.visible_glyphs > without_balloon);

    wm_sd_scene_destroy(scene);
    wm_texture_cache_destroy(textures);
    wm_font_cache_destroy(fonts);
    capture_render = false;
}

int main(int argc, char **argv) {
    test_page_arithmetic();
    WmSdScene *scene = load_scene(argc, argv);
    if (!scene) return 0;
    test_arrow_source_intervals(argc, argv);
    test_dialog_alpha(argc, argv);
    test_grid_sd_button_anchor(argc, argv);
    test_page_and_media(scene);
    test_welcome_and_help(scene);
    test_channel_mapping(scene);
    test_restart_reset(scene);
    wm_sd_scene_destroy(scene);
    test_source_draw_transitions(argc, argv);
    test_footer_balloon_handoff(argc, argv);
    puts("SD Card page, loading, Help, and fixture tests passed.");
    return 0;
}
