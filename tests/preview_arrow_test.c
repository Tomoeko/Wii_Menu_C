#include "wii_menu/preview_scene.h"
#include "wii_menu/resource_scene.h"
#include "wii_menu/source_hit.h"
#include "wii_menu/channel_animation.h"
#include "wii_menu/layout_present.h"

#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <string.h>

typedef struct ArrowTrace {
    unsigned arrow_quads;
    unsigned arrows_in_target;
    unsigned outside_rects;
    unsigned last_outside_event;
    unsigned arrows_after_border;
    unsigned event;
    bool in_target;
} ArrowTrace;

typedef struct MessageTrace {
    bool active;
    unsigned window_quads;
    unsigned glyph_quads;
    float window_left;
    float window_top;
    float window_right;
    float window_bottom;
    float glyph_left;
    float glyph_top;
    float glyph_right;
    float glyph_bottom;
} MessageTrace;

static ArrowTrace trace;
static MessageTrace message_trace;
static uint32_t next_texture = 1;
static uint32_t arrow_textures[2];

typedef struct ArrowFeedbackTrace {
    bool active;
    uint32_t texture;
    float material_alpha[4];
    unsigned quads;
} ArrowFeedbackTrace;

static ArrowFeedbackTrace arrow_feedback;

typedef struct PreviewButtonTrace {
    bool active;
    uint32_t center_texture;
    float back_width;
    float start_width;
} PreviewButtonTrace;

static PreviewButtonTrace preview_button;

typedef struct BannerFadeTrace {
    bool active;
    bool photo;
    uint32_t texture;
    unsigned quads;
    float alpha;
} BannerFadeTrace;

static BannerFadeTrace banner_fade;

typedef struct LabelColorTrace {
    bool active;
    int current_label;
    unsigned glyphs[3];
    float color[3][3];
} LabelColorTrace;

static LabelColorTrace label_colors;

void wm_platform_begin(WmPlatform *platform, WmColor clear_color) {
    (void)platform;
    (void)clear_color;
    trace.in_target = false;
}

void wm_platform_end(WmPlatform *platform) {
    (void)platform;
    trace.in_target = false;
}

void wm_platform_set_clip(WmPlatform *platform, const WmClipRect *clip) {
    (void)platform;
    (void)clip;
}

void wm_platform_draw_quad(WmPlatform *platform, const WmQuad *quad) {
    (void)platform;
    trace.event++;
    if (!trace.in_target && quad->texture == 0 &&
        quad->color.r == 0.0f && quad->color.g == 0.0f &&
        quad->color.b == 0.0f && quad->color.a > 0.0f) {
        trace.outside_rects++;
        trace.last_outside_event = trace.event;
    }
}

void wm_platform_draw_vertices(WmPlatform *platform,
                               const WmDrawVertex vertices[4],
                               uint32_t texture) {
    (void)platform;
    (void)texture;
    trace.event++;
    if (label_colors.active && label_colors.current_label >= 0) {
        const int label = label_colors.current_label;
        label_colors.glyphs[label]++;
        label_colors.color[label][0] = vertices[0].color.r;
        label_colors.color[label][1] = vertices[0].color.g;
        label_colors.color[label][2] = vertices[0].color.b;
    }
    if (!message_trace.active) return;
    message_trace.glyph_quads++;
    for (size_t index = 0; index < 4; index++) {
        message_trace.glyph_left = fminf(message_trace.glyph_left,
                                          vertices[index].x);
        message_trace.glyph_top = fminf(message_trace.glyph_top,
                                         vertices[index].y);
        message_trace.glyph_right = fmaxf(message_trace.glyph_right,
                                           vertices[index].x);
        message_trace.glyph_bottom = fmaxf(message_trace.glyph_bottom,
                                            vertices[index].y);
    }
}

void wm_platform_prepare_material(WmPlatform *platform,
                                  const WmMaterialQuad *quad) {
    (void)platform;
    (void)quad;
}

void wm_platform_draw_material_quad(WmPlatform *platform,
                                    const WmMaterialQuad *quad) {
    (void)platform;
    trace.event++;
    if (preview_button.active && quad->texture_count > 0 &&
        quad->textures[0] == preview_button.center_texture) {
        float middle = (quad->vertices[0].x + quad->vertices[1].x) * 0.5f;
        float width = fabsf(quad->vertices[1].x - quad->vertices[0].x);
        float *largest = middle < WM_FRAME_WIDTH * 0.5f
                             ? &preview_button.back_width
                             : &preview_button.start_width;
        *largest = fmaxf(*largest, width);
    }
    if (arrow_feedback.active && quad->texture_count > 0 &&
        quad->textures[0] == arrow_feedback.texture) {
        if (arrow_feedback.quads < 4)
            arrow_feedback.material_alpha[arrow_feedback.quads] =
                quad->registers[1][3];
        arrow_feedback.quads++;
    }
    if (banner_fade.active) {
        for (size_t index = 0; index < quad->texture_count; index++) {
            if (quad->textures[index] != banner_fade.texture) continue;
            float left = quad->vertices[0].x;
            float top = quad->vertices[0].y;
            float width = quad->vertices[1].x - left;
            float height = quad->vertices[2].y - top;
            bool target = banner_fade.photo
                ? fabsf(left) < 0.01f && fabsf(top - 26.0f) < 0.01f &&
                  fabsf(width - 640.0f) < 0.01f &&
                  fabsf(height - 76.0f) < 0.01f
                : left > 300.0f && left < 315.0f &&
                  top > 65.0f && top < 70.0f &&
                  width > 65.0f && width < 73.0f &&
                  height > 88.0f && height < 95.0f;
            if (target) {
                banner_fade.quads++;
                banner_fade.alpha = quad->vertices[0].color.a;
            }
        }
    }
    if (message_trace.active) {
        message_trace.window_quads++;
        for (size_t index = 0; index < 4; index++) {
            message_trace.window_left = fminf(message_trace.window_left,
                                               quad->vertices[index].x);
            message_trace.window_top = fminf(message_trace.window_top,
                                              quad->vertices[index].y);
            message_trace.window_right = fmaxf(message_trace.window_right,
                                                quad->vertices[index].x);
            message_trace.window_bottom = fmaxf(message_trace.window_bottom,
                                                 quad->vertices[index].y);
        }
    }
    if (quad->texture_count == 0 ||
        (quad->textures[0] != arrow_textures[0] &&
         quad->textures[0] != arrow_textures[1])) return;
    trace.arrow_quads++;
    if (trace.in_target) trace.arrows_in_target++;
    if (trace.last_outside_event && trace.event > trace.last_outside_event)
        trace.arrows_after_border++;
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
    trace.in_target = true;
    return true;
}

void wm_platform_destroy_texture(WmPlatform *platform, uint32_t texture) {
    (void)platform;
    (void)texture;
}

static bool near(float left, float right) {
    return fabsf(left - right) < 0.01f;
}

static WmSourceRect rectangle(const WmLayout *arrows, const char *pane,
                              bool wide) {
    WmSourceRect result;
    assert(wm_source_pane_rect(arrows, pane, wide, WM_LAYOUT_IPL,
                               NULL, &result));
    return result;
}

static void check_source_geometry(const char *assets) {
    char path[1024];
    int length = snprintf(path, sizeof(path),
                          "%s/layouts/cmnBtn/my_IplTop_e.json", assets);
    assert(length > 0 && length < (int)sizeof(path));
    char error[160] = {0};
    WmLayout *arrows = wm_layout_load_json(path, error, sizeof(error));
    if (!arrows) fprintf(stderr, "Arrow layout: %s\n", error);
    assert(arrows);
    static const char *const panes[] = {
        "B_ArwL", "B_ArwR", "ArwL", "ArwR"
    };
    for (unsigned aspect = 0; aspect < 2; aspect++) {
        bool wide = aspect != 0;
        WmSourceRect home[4], start[4], middle[4], end[4];
        WmSourceRect enter_start[4], enter_middle[4];
        assert(wm_preview_scene_pose_arrows(arrows, 10.0f, 0.0f, false));
        for (size_t pane = 0; pane < 4; pane++)
            home[pane] = rectangle(arrows, panes[pane], wide);
        assert(wm_preview_scene_pose_arrows(arrows, 0.0f, 0.0f, false));
        for (size_t pane = 0; pane < 4; pane++)
            enter_start[pane] = rectangle(arrows, panes[pane], wide);
        assert(wm_preview_scene_pose_arrows(arrows, 5.0f, 0.0f, false));
        for (size_t pane = 0; pane < 4; pane++)
            enter_middle[pane] = rectangle(arrows, panes[pane], wide);
        assert(wm_preview_scene_pose_arrows(arrows, 0.0f, 0.0f, true));
        for (size_t pane = 0; pane < 4; pane++)
            start[pane] = rectangle(arrows, panes[pane], wide);
        assert(wm_preview_scene_pose_arrows(arrows, 5.0f, 0.0f, true));
        for (size_t pane = 0; pane < 4; pane++)
            middle[pane] = rectangle(arrows, panes[pane], wide);
        assert(wm_preview_scene_pose_arrows(arrows, 10.0f, 0.0f, true));
        for (size_t pane = 0; pane < 4; pane++)
            end[pane] = rectangle(arrows, panes[pane], wide);
        for (size_t pane = 0; pane < 4; pane++) {
            assert(near(start[pane].width, home[pane].width));
            assert(near(start[pane].height, home[pane].height));
            assert(near(middle[pane].width, home[pane].width));
            assert(near(middle[pane].height, home[pane].height));
            assert(near(end[pane].width, home[pane].width));
            assert(near(end[pane].height, home[pane].height));
            assert(near(start[pane].x, home[pane].x));
            assert(near(enter_start[pane].width, home[pane].width));
            assert(near(enter_start[pane].height, home[pane].height));
            assert(near(enter_middle[pane].width, home[pane].width));
            assert(near(enter_middle[pane].height, home[pane].height));
        }
        assert(enter_start[0].x < enter_middle[0].x);
        assert(enter_middle[0].x < home[0].x);
        assert(enter_start[1].x > enter_middle[1].x);
        assert(enter_middle[1].x > home[1].x);
        assert(middle[0].x + middle[0].width < 0.0f);
        assert(middle[1].x > WM_FRAME_WIDTH);
        assert(end[0].x < middle[0].x);
        assert(end[1].x > middle[1].x);
    }
    wm_layout_destroy(arrows);
}

static void reset_trace(void) {
    trace = (ArrowTrace){0};
}

static bool message_panes_only(void *context, const WmLayoutPaneView *pane) {
    (void)context;
    if (strcmp(pane->type, "txt1") == 0)
        return strcmp(pane->name, "T_messageE_00") == 0;
    if (strcmp(pane->type, "wnd1") == 0)
        return strcmp(pane->name, "W_messWindow_00") == 0;
    return strcmp(pane->type, "pic1") != 0;
}

static void check_rendered_message_windows(const char *assets) {
    static const char *const channels[] = {
        "0001000148414a45", /* Everybody Votes Channel */
        "0001000148434c45"  /* Netflix */
    };
    WmPlatform *platform = (WmPlatform *)1;
    WmTextureCache *textures = wm_texture_cache_create(
        platform, assets, 32u * 1024u * 1024u);
    WmFontCache *fonts = wm_font_cache_create(
        platform, assets, 16u * 1024u * 1024u);
    assert(textures && fonts);
    unsigned checked = 0;
    for (size_t index = 0; index <
         sizeof(channels) / sizeof(channels[0]); index++) {
        char path[1024];
        int length = snprintf(path, sizeof(path),
                              "%s/channel-layouts/%s/banner/banner.json",
                              assets, channels[index]);
        assert(length > 0 && length < (int)sizeof(path));
        FILE *resource = fopen(path, "rb");
        if (!resource) continue;
        fclose(resource);
        checked++;
        char error[160] = {0};
        WmLayout *banner = wm_layout_load_json(path, error, sizeof(error));
        if (!banner) fprintf(stderr, "Channel banner: %s\n", error);
        assert(banner);
        const WmChannelAnimationOptions options = {
            .language = "ENG",
            .has_base_frame = true,
            .base_frame = 100.0f,
            .measure_text = wm_font_cache_measure_text,
            .measure_context = fonts
        };
        assert(wm_channel_animation_pose(banner, channels[index],
                                          WM_CHANNEL_BANNER, 100.0f, &options));
        for (unsigned aspect = 0; aspect < 2; aspect++) {
            message_trace = (MessageTrace){
                .active = true,
                .window_left = INFINITY,
                .window_top = INFINITY,
                .window_right = -INFINITY,
                .window_bottom = -INFINITY,
                .glyph_left = INFINITY,
                .glyph_top = INFINITY,
                .glyph_right = -INFINITY,
                .glyph_bottom = -INFINITY
            };
            wm_layout_present_filtered_with_fonts(
                platform, textures, fonts, banner, aspect != 0, WM_LAYOUT_IPL,
                NULL, message_panes_only, NULL);
            assert(message_trace.window_quads > 0);
            assert(message_trace.glyph_quads > 0);
            assert(message_trace.glyph_left >= message_trace.window_left - 0.01f);
            assert(message_trace.glyph_right <= message_trace.window_right + 0.01f);
            assert(message_trace.glyph_top >= message_trace.window_top - 0.01f);
            assert(message_trace.glyph_bottom <= message_trace.window_bottom + 0.01f);
        }
        message_trace.active = false;
        wm_layout_destroy(banner);
    }
    wm_font_cache_destroy(fonts);
    wm_texture_cache_destroy(textures);
    printf("Checked %u imported preview message windows in both projections.\n",
           checked);
}

static void check_capture_and_overlay(const char *assets) {
    WmPlatform *platform = (WmPlatform *)1;
    WmMenu menu;
    wm_menu_init(&menu);
    menu.slots[13].occupied = true;
    strcpy(menu.slots[13].id, "disc");
    strcpy(menu.slots[13].title, "Disc Channel");
    menu.page = 1;
    WmTextureCache *textures = wm_texture_cache_create(
        platform, assets, 128u * 1024u * 1024u);
    WmFontCache *fonts = wm_font_cache_create(
        platform, assets, 16u * 1024u * 1024u);
    assert(textures && fonts);
    assert(wm_texture_cache_resolve(textures,
        "textures/cmnBtn/my_arw_a.png", &arrow_textures[0]));
    assert(wm_texture_cache_resolve(textures,
        "textures/cmnBtn/my_arw_b.png", &arrow_textures[1]));
    WmPreviewScene *preview = wm_preview_scene_create(
        platform, assets, &menu, textures, fonts);
    WmResourceScene *grid = wm_resource_scene_create(
        platform, assets, &menu, textures, fonts);
    assert(preview && grid);

    assert(wm_menu_select(&menu, 13));
    wm_menu_tick(&menu, 28.0f / 60.0f);
    assert(menu.screen == WM_SCREEN_PREVIEW);
    reset_trace();
    assert(wm_preview_scene_draw_layers(preview, &menu, 1.0f, NULL));
    assert(trace.arrow_quads > 0);
    reset_trace();
    assert(wm_preview_scene_draw_capture(preview, &menu, 1.0f));
    assert(trace.arrow_quads == 0);

    assert(wm_menu_back(&menu));
    assert(menu.screen == WM_SCREEN_GRID && menu.selected == -1);
    assert(menu.transition_from_selected == 13);
    wm_menu_tick(&menu, 5.0f / 60.0f);
    reset_trace();
    const WmResourceSceneFrame frame = {
        .elapsed_seconds = 2.0f,
        .preview_elapsed_seconds = 1.0f,
        .preview_scene = preview
    };
    wm_resource_scene_draw(grid, &menu, &frame);
    assert(trace.outside_rects > 0);
    assert(trace.arrow_quads > 0);
    assert(trace.arrows_in_target == 0);
    assert(trace.arrows_after_border > 0);

    WmMenu alone;
    wm_menu_init(&alone);
    assert(wm_menu_select(&alone, 0));
    wm_menu_tick(&alone, 28.0f / 60.0f);
    assert(wm_menu_back(&alone));
    reset_trace();
    assert(wm_preview_scene_draw_return_arrows(preview, &alone, 2.0f, true));
    assert(trace.arrow_quads == 0);

    wm_resource_scene_destroy(grid);
    wm_preview_scene_destroy(preview);
    wm_font_cache_destroy(fonts);
    wm_texture_cache_destroy(textures);
}

static ArrowFeedbackTrace draw_arrow_feedback(WmPreviewScene *scene,
                                              const WmMenu *menu,
                                              float seconds, WmHitType hover,
                                              uint32_t texture)
{
    arrow_feedback = (ArrowFeedbackTrace){
        .active = true,
        .texture = texture
    };
    assert(wm_preview_scene_draw(scene, menu, seconds,
                                  (WmHit){hover, -1}, NULL));
    arrow_feedback.active = false;
    assert(arrow_feedback.quads == 2);
    return arrow_feedback;
}

static void check_preview_arrow_feedback(const char *assets)
{
    WmPlatform *platform = (WmPlatform *)1;
    WmMenu menu;
    wm_menu_init(&menu);
    menu.slots[1].occupied = true;
    strcpy(menu.slots[1].id, "disc");
    strcpy(menu.slots[1].title, "Disc Channel");
    WmTextureCache *textures = wm_texture_cache_create(
        platform, assets, 128u * 1024u * 1024u);
    WmFontCache *fonts = wm_font_cache_create(
        platform, assets, 16u * 1024u * 1024u);
    assert(textures && fonts);
    uint32_t button_texture;
    assert(wm_texture_cache_resolve(textures,
        "textures/cmnBtn/my_ComBtn_b.png", &button_texture));
    WmPreviewScene *scene = wm_preview_scene_create(
        platform, assets, &menu, textures, fonts);
    assert(scene);
    assert(wm_menu_select(&menu, 0));
    wm_menu_tick(&menu, 28.0f / 60.0f);

    /* The maintained HTML resolver retains a hovered common arrow for four
     * source units beyond its moving hit pane. A click still needs the exact
     * pane, so a nearby press cannot activate a page change. */
    char arrow_path[1024];
    int arrow_length = snprintf(arrow_path, sizeof(arrow_path),
                                "%s/layouts/cmnBtn/my_IplTop_e.json", assets);
    assert(arrow_length > 0 && arrow_length < (int)sizeof(arrow_path));
    char arrow_error[128];
    WmLayout *arrow_layout = wm_layout_load_json(
        arrow_path, arrow_error, sizeof(arrow_error));
    assert(arrow_layout);
    WmSourceRect right_arrow;
    assert(wm_source_pane_rect(arrow_layout, "B_ArwR", true,
                               WM_LAYOUT_IPL, NULL, &right_arrow));
    int near_x = (int)floorf(right_arrow.x) - 2;
    int far_x = (int)floorf(right_arrow.x) - 8;
    int arrow_y = (int)floorf(right_arrow.y + right_arrow.height * 0.5f);
    WmHit held = {WM_HIT_PREVIEW_NEXT, -1};
    assert(wm_preview_scene_hit(scene, &menu, near_x, arrow_y).type !=
           WM_HIT_PREVIEW_NEXT);
    assert(wm_preview_scene_hover_hit(scene, &menu, near_x, arrow_y,
                                       held).type == WM_HIT_PREVIEW_NEXT);
    assert(wm_preview_scene_hover_hit(scene, &menu, far_x, arrow_y,
                                       held).type != WM_HIT_PREVIEW_NEXT);
    wm_layout_destroy(arrow_layout);

    ArrowFeedbackTrace sampled = draw_arrow_feedback(
        scene, &menu, 0.0f, WM_HIT_NONE, button_texture);
    assert(sampled.material_alpha[0] == 0.0f);
    assert(sampled.material_alpha[1] == 0.0f);
    sampled = draw_arrow_feedback(scene, &menu, 1.0f / 60.0f,
                                  WM_HIT_PREVIEW_PREVIOUS, button_texture);
    assert(sampled.material_alpha[0] == 0.0f);
    sampled = draw_arrow_feedback(scene, &menu, 8.0f / 60.0f,
                                  WM_HIT_PREVIEW_PREVIOUS, button_texture);
    assert(sampled.material_alpha[0] > 0.99f);

    /* HTML's commonArrowDefinitions plays source 10700–10730 on click,
     * independently of the held 10600–10615 hover clip. The banner swaps
     * after 20 frames; the pressed art continues for ten more frames. */
    assert(wm_menu_change_preview(&menu, -1));
    sampled = draw_arrow_feedback(scene, &menu, 8.0f / 60.0f,
                                  WM_HIT_PREVIEW_PREVIOUS, button_texture);
    assert(sampled.material_alpha[1] == 0.0f);
    for (int frame = 1; frame < 20; frame++) {
        wm_menu_tick(&menu, 1.0f / 60.0f);
        sampled = draw_arrow_feedback(scene, &menu, (8.0f + frame) / 60.0f,
                                      WM_HIT_PREVIEW_PREVIOUS, button_texture);
        if (frame == 5)
            assert(sampled.material_alpha[1] > 0.9f);
    }
    wm_menu_tick(&menu, 1.0f / 60.0f);
    assert(menu.transition == WM_TRANSITION_NONE);
    sampled = draw_arrow_feedback(scene, &menu, 0.0f,
                                  WM_HIT_PREVIEW_PREVIOUS, button_texture);
    assert(sampled.material_alpha[0] > 0.99f);
    sampled = draw_arrow_feedback(scene, &menu, 10.0f / 60.0f,
                                  WM_HIT_PREVIEW_PREVIOUS, button_texture);
    assert(sampled.material_alpha[1] == 0.0f);
    sampled = draw_arrow_feedback(scene, &menu, 11.0f / 60.0f,
                                  WM_HIT_NONE, button_texture);
    assert(sampled.material_alpha[1] == 0.0f);
    sampled = draw_arrow_feedback(scene, &menu, 16.0f / 60.0f,
                                  WM_HIT_NONE, button_texture);
    assert(sampled.material_alpha[0] == 0.0f);
    assert(sampled.material_alpha[1] == 0.0f);

    sampled = draw_arrow_feedback(scene, &menu, 17.0f / 60.0f,
                                  WM_HIT_PREVIEW_PREVIOUS, button_texture);
    sampled = draw_arrow_feedback(scene, &menu, 24.0f / 60.0f,
                                  WM_HIT_PREVIEW_PREVIOUS, button_texture);
    assert(sampled.material_alpha[0] > 0.99f);
    assert(wm_menu_back(&menu));
    arrow_feedback = (ArrowFeedbackTrace){
        .active = true,
        .texture = button_texture
    };
    assert(wm_preview_scene_draw_return_arrows(scene, &menu, 1.0f, true));
    assert(arrow_feedback.quads == 2);
    assert(arrow_feedback.material_alpha[0] > 0.99f);
    wm_menu_tick(&menu, 5.0f / 60.0f);
    arrow_feedback = (ArrowFeedbackTrace){
        .active = true,
        .texture = button_texture
    };
    assert(wm_preview_scene_draw_return_arrows(scene, &menu, 1.0f, true));
    arrow_feedback.active = false;
    assert(arrow_feedback.material_alpha[0] == 0.0f);
    wm_menu_tick(&menu, 1.0f);
    assert(wm_menu_select(&menu, 0));
    wm_menu_tick(&menu, 28.0f / 60.0f);
    sampled = draw_arrow_feedback(scene, &menu, 0.0f,
                                  WM_HIT_NONE, button_texture);
    assert(sampled.material_alpha[0] == 0.0f);
    assert(sampled.material_alpha[1] == 0.0f);
    uint32_t right_button_texture;
    assert(wm_texture_cache_resolve(textures,
        "textures/cmnBtn/my_ComBtn_a.png", &right_button_texture));
    sampled = draw_arrow_feedback(scene, &menu, 1.0f / 60.0f,
                                  WM_HIT_PREVIEW_NEXT, right_button_texture);
    sampled = draw_arrow_feedback(scene, &menu, 8.0f / 60.0f,
                                  WM_HIT_PREVIEW_NEXT, right_button_texture);
    assert(sampled.material_alpha[0] > 0.99f);
    assert(wm_menu_change_preview(&menu, 1));
    sampled = draw_arrow_feedback(scene, &menu, 8.0f / 60.0f,
                                  WM_HIT_PREVIEW_NEXT, right_button_texture);
    assert(sampled.material_alpha[1] == 0.0f);
    wm_menu_tick(&menu, 5.0f / 60.0f);
    sampled = draw_arrow_feedback(scene, &menu, 13.0f / 60.0f,
                                  WM_HIT_PREVIEW_NEXT, right_button_texture);
    assert(sampled.material_alpha[1] > 0.9f);
    wm_menu_tick(&menu, 15.0f / 60.0f);
    assert(menu.transition == WM_TRANSITION_NONE);
    /* The next accepted click may arrive before a settled frame renders.
     * HTML starts a fresh press clip for it without restarting held focus. */
    assert(wm_menu_change_preview(&menu, 1));
    sampled = draw_arrow_feedback(scene, &menu, 28.0f / 60.0f,
                                  WM_HIT_PREVIEW_NEXT, right_button_texture);
    assert(sampled.material_alpha[0] > 0.99f);
    assert(sampled.material_alpha[1] == 0.0f);
    wm_menu_tick(&menu, 5.0f / 60.0f);
    sampled = draw_arrow_feedback(scene, &menu, 33.0f / 60.0f,
                                  WM_HIT_PREVIEW_NEXT, right_button_texture);
    assert(sampled.material_alpha[0] > 0.99f);
    assert(sampled.material_alpha[1] > 0.9f);

    wm_preview_scene_destroy(scene);
    wm_font_cache_destroy(fonts);
    wm_texture_cache_destroy(textures);
    puts("Preview arrows follow WAD focus and press clips through change and Back.");
}

static void check_preview_arrow_entry_and_idle(const char *assets)
{
    WmPlatform *platform = (WmPlatform *)1;
    WmMenu menu;
    wm_menu_init(&menu);
    menu.slots[1].occupied = true;
    strcpy(menu.slots[1].id, "disc");
    strcpy(menu.slots[1].title, "Disc Channel");
    WmTextureCache *textures = wm_texture_cache_create(
        platform, assets, 128u * 1024u * 1024u);
    WmFontCache *fonts = wm_font_cache_create(
        platform, assets, 16u * 1024u * 1024u);
    assert(textures && fonts);
    uint32_t button_textures[2];
    assert(wm_texture_cache_resolve(textures,
        "textures/cmnBtn/my_ComBtn_b.png", &button_textures[0]));
    assert(wm_texture_cache_resolve(textures,
        "textures/cmnBtn/my_ComBtn_a.png", &button_textures[1]));
    WmPreviewScene *scene = wm_preview_scene_create(
        platform, assets, &menu, textures, fonts);
    assert(scene);
    assert(wm_menu_select(&menu, 0));
    wm_menu_tick(&menu, 28.0f / 60.0f);

    char path[1024];
    int length = snprintf(path, sizeof path,
                          "%s/layouts/cmnBtn/my_IplTop_e.json", assets);
    assert(length > 0 && length < (int)sizeof path);
    char error[160] = {0};
    WmLayout *arrows = wm_layout_load_json(path, error, sizeof error);
    assert(arrows);
    assert(wm_preview_scene_pose_arrows(arrows, 10.0f, 0.0f, false));
    WmSourceRect left = rectangle(arrows, "B_ArwL", true);
    int x = (int)(left.x + left.width * 0.5f);
    int y = (int)(left.y + left.height * 0.5f);
    assert(wm_preview_scene_draw(scene, &menu, 0.0f,
                                  (WmHit){WM_HIT_NONE, -1}, NULL));
    assert(wm_preview_scene_hit(scene, &menu, x, y).type !=
           WM_HIT_PREVIEW_PREVIOUS);
    for (size_t side = 0; side < 2; side++) {
        ArrowFeedbackTrace idle = draw_arrow_feedback(
            scene, &menu, 10.0f / 60.0f, WM_HIT_NONE,
            button_textures[side]);
        assert(idle.material_alpha[0] == 0.0f);
        assert(idle.material_alpha[1] == 0.0f);
    }
    assert(wm_preview_scene_hit(scene, &menu, x, y).type ==
           WM_HIT_PREVIEW_PREVIOUS);

    /* HTML's arrow entry clock is independent of its per-banner clock.
     * A channel change must not move settled arrows back offscreen. */
    assert(wm_menu_change_preview(&menu, -1));
    assert(wm_preview_scene_draw(scene, &menu, 10.0f / 60.0f,
                                  (WmHit){WM_HIT_NONE, -1}, NULL));
    wm_menu_tick(&menu, 20.0f / 60.0f);
    assert(menu.transition == WM_TRANSITION_NONE);
    assert(wm_preview_scene_draw(scene, &menu, 0.0f,
                                  (WmHit){WM_HIT_NONE, -1}, NULL));
    assert(wm_preview_scene_hit(scene, &menu, x, y).type ==
           WM_HIT_PREVIEW_PREVIOUS);
    for (size_t side = 0; side < 2; side++) {
        ArrowFeedbackTrace swapped = draw_arrow_feedback(
            scene, &menu, 0.0f, WM_HIT_NONE, button_textures[side]);
        assert(swapped.material_alpha[0] == 0.0f);
    }
    for (size_t side = 0; side < 2; side++) {
        ArrowFeedbackTrace settled = draw_arrow_feedback(
            scene, &menu, 11.0f / 60.0f, WM_HIT_NONE,
            button_textures[side]);
        assert(settled.material_alpha[0] == 0.0f);
        assert(settled.material_alpha[1] == 0.0f);
    }

    wm_layout_destroy(arrows);
    wm_preview_scene_destroy(scene);
    wm_font_cache_destroy(fonts);
    wm_texture_cache_destroy(textures);
    puts("Idle preview arrows stay neutral and do not re-enter after clicks.");
}

static float preview_back_button_width(WmPreviewScene *scene,
                                       const WmMenu *menu, float seconds,
                                       WmHitType hover, uint32_t texture,
                                       bool capture)
{
    preview_button = (PreviewButtonTrace){
        .active = true,
        .center_texture = texture
    };
    if (capture)
        assert(wm_preview_scene_draw_capture(scene, menu, seconds));
    else
        assert(wm_preview_scene_draw(scene, menu, seconds,
                                     (WmHit){hover, -1}, NULL));
    preview_button.active = false;
    assert(preview_button.back_width > 0.0f);
    return preview_button.back_width;
}

static void check_preview_back_focus_on_click(const char *assets)
{
    WmPlatform *platform = (WmPlatform *)1;
    WmMenu menu;
    wm_menu_init(&menu);
    WmTextureCache *textures = wm_texture_cache_create(
        platform, assets, 128u * 1024u * 1024u);
    WmFontCache *fonts = wm_font_cache_create(
        platform, assets, 16u * 1024u * 1024u);
    assert(textures && fonts);
    uint32_t center_texture;
    assert(wm_texture_cache_resolve(textures,
        "textures/chanTtl/my_ComBtn_c1.png", &center_texture));
    WmPreviewScene *scene = wm_preview_scene_create(
        platform, assets, &menu, textures, fonts);
    assert(scene);
    assert(wm_menu_select(&menu, 0));
    wm_menu_tick(&menu, 28.0f / 60.0f);
    float idle = preview_back_button_width(
        scene, &menu, 0.0f, WM_HIT_NONE, center_texture, false);
    (void)preview_back_button_width(
        scene, &menu, 1.0f / 60.0f, WM_HIT_BACK, center_texture, false);
    float focused = preview_back_button_width(
        scene, &menu, 8.0f / 60.0f, WM_HIT_BACK, center_texture, false);
    assert(focused > idle * 1.09f);
    assert(wm_menu_back(&menu));
    float returning = preview_back_button_width(
        scene, &menu, 8.0f / 60.0f, WM_HIT_NONE, center_texture, true);
    assert(fabsf(returning - idle) < 0.01f);

    wm_preview_scene_destroy(scene);
    wm_font_cache_destroy(fonts);
    wm_texture_cache_destroy(textures);
    puts("Preview Back returns to the WAD neutral button pose on click.");
}

static float rendered_banner_alpha(WmPreviewScene *scene, const WmMenu *menu,
                                   bool photo, uint32_t texture, int frame,
                                   bool capture) {
    banner_fade = (BannerFadeTrace){
        .active = true,
        .photo = photo,
        .texture = texture
    };
    if (capture)
        assert(wm_preview_scene_draw_capture(scene, menu, frame / 60.0f));
    else
        assert(wm_preview_scene_draw_layers(scene, menu, frame / 60.0f,
                                             NULL));
    banner_fade.active = false;
    assert(banner_fade.quads <= 1);
    return banner_fade.alpha;
}

static void check_preview_banner_fades(const char *assets) {
    WmPlatform *platform = (WmPlatform *)1;
    WmMenu menu;
    wm_menu_init(&menu);
    static const char *const ids[] = {
        "0001000248415941", "0001000248414241"
    };
    static const char *const textures_to_probe[] = {
        "channel-layouts/0001000248415941/banner/textures/plate1.png",
        "channel-layouts/0001000248414241/banner/textures/logo_pic02.png"
    };
    static const float expected_alpha[2][5] = {
        {0.0f, 0.15625f, 0.5f, 0.84375f, 1.0f},
        {0.5f, 2.0f / 3.0f, 5.0f / 6.0f, 1.0f, 1.0f}
    };
    for (int index = 0; index < 2; index++) {
        char path[1024];
        int length = snprintf(path, sizeof path,
                              "%s/channel-layouts/%s/banner/banner.json",
                              assets, ids[index]);
        assert(length > 0 && length < (int)sizeof path);
        FILE *resource = fopen(path, "rb");
        if (!resource) {
            puts("Preview banner fade comparison skipped: channel layouts unavailable.");
            return;
        }
        fclose(resource);
        WmChannel *channel = &menu.slots[index + 1];
        channel->occupied = true;
        snprintf(channel->id, sizeof channel->id, "%s", ids[index]);
        snprintf(channel->banner_layout, sizeof channel->banner_layout,
                 "channel-layouts/%s/banner/banner.json", ids[index]);
    }
    WmTextureCache *textures = wm_texture_cache_create(
        platform, assets, 128u * 1024u * 1024u);
    WmFontCache *fonts = wm_font_cache_create(
        platform, assets, 16u * 1024u * 1024u);
    assert(textures && fonts);
    WmPreviewScene *scene = wm_preview_scene_create(
        platform, assets, &menu, textures, fonts);
    assert(scene);
    for (int index = 0; index < 2; index++) {
        uint32_t texture;
        assert(wm_texture_cache_resolve(textures, textures_to_probe[index],
                                        &texture));
        menu.screen = WM_SCREEN_PREVIEW;
        menu.selected = index + 1;
        float previous = -1.0f;
        for (int sample = 0; sample < 5; sample++) {
            float alpha = rendered_banner_alpha(scene, &menu, index == 0,
                                                 texture, sample * 10, false);
            assert(fabsf(alpha - expected_alpha[index][sample]) < 0.015f);
            assert(alpha >= previous);
            previous = alpha;
        }
    }
    /* On a preview swap the module clock leads by ten frames while Shop's
     * base Start clock still begins at zero. Photo's Rso0 fade follows the
     * module clock, so its first swapped frame is already partly opaque. */
    wm_preview_scene_set_module_lead(scene, 10.0f);
    menu.selected = 1;
    uint32_t photo_texture;
    assert(wm_texture_cache_resolve(textures, textures_to_probe[0],
                                    &photo_texture));
    assert(fabsf(rendered_banner_alpha(scene, &menu, true, photo_texture, 0,
                                       false) -
                 0.15625f) < 0.015f);
    /* A preview swap retains a ten-frame module lead. Leaving that preview
     * and selecting another channel must capture the new banner at frame
     * zero; otherwise its zoom image jumps backward when the zoom completes. */
    menu.screen = WM_SCREEN_GRID;
    menu.selected = -1;
    assert(wm_menu_select(&menu, 1));
    float zoom_start = rendered_banner_alpha(scene, &menu, true,
                                              photo_texture, 0, true);
    float zoom_middle = rendered_banner_alpha(scene, &menu, true,
                                               photo_texture, 14, true);
    assert(fabsf(zoom_start) < 0.015f);
    assert(fabsf(zoom_middle - zoom_start) < 0.015f);
    wm_menu_tick(&menu, 28.0f / 60.0f);
    wm_preview_scene_set_module_lead(scene, 0.0f);
    assert(fabsf(rendered_banner_alpha(scene, &menu, true, photo_texture,
                                       0, false) - zoom_start) < 0.015f);
    wm_preview_scene_destroy(scene);
    wm_font_cache_destroy(fonts);
    wm_texture_cache_destroy(textures);
    puts("Photo and Shop preview fades match source opacity at five frames.");
}

static bool label_pane(void *context, const WmLayoutPaneView *pane) {
    (void)context;
    label_colors.current_label = -1;
    if (strcmp(pane->name, "T_BtnA") == 0) label_colors.current_label = 0;
    if (strcmp(pane->name, "T_BtnB") == 0) label_colors.current_label = 1;
    if (strcmp(pane->name, "T_WiiMenu") == 0) label_colors.current_label = 2;
    return true;
}

static void check_preview_label_colors(const char *assets) {
    WmPlatform *platform = (WmPlatform *)1;
    char path[1024];
    int length = snprintf(path, sizeof(path),
                          "%s/layouts/chanTtl/my_ChTop_a.json", assets);
    assert(length > 0 && length < (int)sizeof path);
    char error[256];
    WmLayout *title = wm_layout_load_json(path, error, sizeof error);
    if (!title) fprintf(stderr, "Channel title: %s\n", error);
    assert(title);
    WmTextureCache *textures = wm_texture_cache_create(
        platform, assets, 8u * 1024u * 1024u);
    WmFontCache *fonts = wm_font_cache_create(
        platform, assets, 8u * 1024u * 1024u);
    assert(textures && fonts);
    assert(wm_layout_set_text(title, "T_BtnA", "Wii Menu"));
    assert(wm_layout_set_text(title, "T_BtnB", "Start"));
    const WmLayoutClip clips[] = {
        {.animation = "my_ChTop_a_OnBtn", .frame = 10.0f,
         .group = "G_OnOffBtnA", .loop_override = 0},
        {.animation = "my_ChTop_a_OnBtn", .frame = 10.0f,
         .group = "G_OnOffBtnB", .loop_override = 0}
    };
    assert(wm_layout_pose(title, clips, sizeof clips / sizeof clips[0]));
    label_colors = (LabelColorTrace){.active = true, .current_label = -1};
    wm_layout_present_filtered_with_fonts(
        platform, textures, fonts, title, true, WM_LAYOUT_IPL, NULL,
        label_pane, NULL);
    label_colors.active = false;
    assert(label_colors.glyphs[0] > 0 && label_colors.glyphs[1] > 0);
    const float expected[] = {70.0f / 255.0f, 48.0f / 255.0f};
    for (size_t label = 0; label < 2; label++) {
        for (size_t channel = 0; channel < 3; channel++) {
            assert(fabsf(label_colors.color[label][channel] - expected[label])
                   < 0.005f);
        }
    }
    wm_layout_destroy(title);

    length = snprintf(path, sizeof(path),
                      "%s/layouts/chanSel/my_Clock_a.json", assets);
    assert(length > 0 && length < (int)sizeof path);
    WmLayout *clock = wm_layout_load_json(path, error, sizeof error);
    if (!clock) fprintf(stderr, "Clock: %s\n", error);
    assert(clock);
    assert(wm_layout_set_text(clock, "T_WiiMenu", "Wii Menu"));
    const WmLayoutClip clock_intro = {
        .animation = "my_Clock_a_Change", .frame = 0.0f,
        .loop_override = 0
    };
    assert(wm_layout_pose(clock, &clock_intro, 1));
    label_colors = (LabelColorTrace){.active = true, .current_label = -1};
    wm_layout_present_filtered_with_fonts(
        platform, textures, fonts, clock, true, WM_LAYOUT_IPL, NULL,
        label_pane, NULL);
    label_colors.active = false;
    assert(label_colors.glyphs[2] > 0);
    const float clock_color[] = {52.0f / 255.0f, 192.0f / 255.0f,
                                 237.0f / 255.0f};
    for (size_t channel = 0; channel < 3; channel++) {
        assert(fabsf(label_colors.color[2][channel] - clock_color[channel])
               < 0.005f);
    }
    wm_font_cache_destroy(fonts);
    wm_texture_cache_destroy(textures);
    wm_layout_destroy(clock);
    puts("Channel and clock labels retain WAD colors in C draw commands.");
}

int main(int argc, char **argv) {
    const char *assets = argc > 1 ? argv[1] : ".local/native-assets";
    char path[1024];
    int length = snprintf(path, sizeof(path),
                          "%s/layouts/cmnBtn/my_IplTop_e.json", assets);
    assert(length > 0 && length < (int)sizeof(path));
    FILE *resource = fopen(path, "rb");
    if (!resource) {
        puts("Preview arrow comparison skipped: prepared assets unavailable.");
        return 0;
    }
    fclose(resource);
    check_source_geometry(assets);
    check_capture_and_overlay(assets);
    check_preview_arrow_feedback(assets);
    check_preview_arrow_entry_and_idle(assets);
    check_preview_back_focus_on_click(assets);
    check_preview_banner_fades(assets);
    check_preview_label_colors(assets);
    check_rendered_message_windows(assets);
    puts("Preview arrows retain source size and draw after the zoom border.");
    return 0;
}
