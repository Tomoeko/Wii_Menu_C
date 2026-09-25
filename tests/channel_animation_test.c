#include "wii_menu/channel_animation.h"

#include <assert.h>
#include <float.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* These are synthetic layouts. The expected timing follows the HTML source
 * schedules, not a claim about independently verified console behavior. */
#define PHOTO_ID "0001000148415941"
#define FORECAST_ID "0001000148414645"
#define NEWS_ID "0001000148414745"
#define SHOP_ID "0001000148414241"
#define SEAT_ID "0001000148414445"
#define CONNECTION_ID "0001000148434745"
#define GENERIC_ID "0001000148414341"

static bool near(float actual, float expected) {
    return fabsf(actual - expected) < 0.001f;
}

static WmLayout *load_fixture(void) {
    char error[256];
    WmLayout *layout = wm_layout_load_json("tests/channel_animation_fixture.json",
                                           error, sizeof(error));
    if (!layout) fprintf(stderr, "channel fixture load failed: %s\n", error);
    assert(layout);
    return layout;
}

static WmChannelAnimationPlan plan_for(
    const WmLayout *layout, const char *title_id, WmChannelAnimationKind kind,
    float frame, const WmChannelAnimationOptions *options) {
    WmChannelAnimationPlan plan = {0};
    assert(wm_channel_animation_plan(layout, title_id, kind, frame,
                                     options, &plan));
    assert(plan.count <= WM_CHANNEL_ANIMATION_MAX_CLIPS);
    return plan;
}

static void expect_clip(const WmChannelAnimationPlan *plan, size_t index,
                        const char *name, float frame, const char *group) {
    assert(index < plan->count);
    assert(strcmp(plan->clips[index].animation, name) == 0);
    assert(near(plan->clips[index].frame, frame));
    assert(strcmp(plan->clips[index].group, group) == 0);
}

static WmLayoutPaneState pane_state(const WmLayout *layout, const char *name) {
    WmLayoutPaneState state;
    assert(wm_layout_pane_state(layout, name, &state));
    return state;
}

static bool visible(const WmLayout *layout, const char *name) {
    return (pane_state(layout, name).flags & 1u) != 0;
}

static void test_frame_controller(void) {
    assert(near(wm_channel_animation_frame(0, 40, 190, 0, 1, true), 0));
    assert(near(wm_channel_animation_frame(189, 40, 190, 0, 1, true), 189));
    assert(near(wm_channel_animation_frame(190, 40, 190, 0, 1, true), 40));
    assert(near(wm_channel_animation_frame(340, 40, 190, 0, 1, true), 40));
    assert(near(wm_channel_animation_frame(0, 0, 470, 530, 1, true), 60));
    assert(near(wm_channel_animation_frame(-10, 0, 16, 0, 1, false), 0));
}

static void test_base_intro_and_loop(const WmLayout *layout) {
    WmChannelAnimationPlan plan = plan_for(layout, GENERIC_ID, WM_CHANNEL_BANNER,
                                            59, NULL);
    assert(plan.count == 1);
    expect_clip(&plan, 0, "banner_Start", 59, "");

    plan = plan_for(layout, GENERIC_ID, WM_CHANNEL_BANNER, 65, NULL);
    assert(plan.count == 2);
    expect_clip(&plan, 0, "banner_Start", 60, "");
    expect_clip(&plan, 1, "banner_Loop", 5, "");

    plan = plan_for(layout, GENERIC_ID, WM_CHANNEL_BANNER, 205, NULL);
    expect_clip(&plan, 0, "banner_Start", 60, "");
    expect_clip(&plan, 1, "banner_Loop", 5, "");

    WmChannelAnimationOptions custom = {.custom_banner = true};
    plan = plan_for(layout, GENERIC_ID, WM_CHANNEL_BANNER, 0, &custom);
    assert(plan.count == 2);
    expect_clip(&plan, 0, "banner_Start", 0, "");
    expect_clip(&plan, 1, "banner_Loop", 0, "");

    plan = plan_for(layout, GENERIC_ID, WM_CHANNEL_ICON, 50, NULL);
    assert(plan.count == 1);
    expect_clip(&plan, 0, "icon_Start", 9, "");
}

typedef struct PositionCapture {
    bool selected_seen;
    bool unrelated_seen;
    float selected_x;
    float unrelated_x;
} PositionCapture;

static bool capture_position(void *context, const WmLayoutPaneView *pane) {
    PositionCapture *capture = context;
    if (strcmp(pane->name, "selected") == 0) {
        capture->selected_seen = true;
        capture->selected_x = pane->matrix[3];
    } else if (strcmp(pane->name, "unrelated") == 0) {
        capture->unrelated_seen = true;
        capture->unrelated_x = pane->matrix[3];
    }
    return true;
}

static void test_photo_and_group_binding(WmLayout *layout) {
    WmChannelAnimationPlan plan = plan_for(layout, PHOTO_ID, WM_CHANNEL_ICON,
                                            765, NULL);
    assert(plan.count == 2);
    expect_clip(&plan, 1, "icon_Rso0", 5, "Rso0");

    plan = plan_for(layout, PHOTO_ID, WM_CHANNEL_BANNER, 1245, NULL);
    assert(plan.count == 3);
    expect_clip(&plan, 2, "banner_Rso0", 45, "Rso0");

    WmChannelAnimationOptions held_intro = {
        .has_base_frame = true,
        .base_frame = 3
    };
    plan = plan_for(layout, PHOTO_ID, WM_CHANNEL_BANNER, 13, &held_intro);
    assert(plan.count == 2);
    expect_clip(&plan, 0, "banner_Start", 3, "");
    expect_clip(&plan, 1, "banner_Rso0", 13, "Rso0");

    assert(wm_channel_animation_pose(layout, PHOTO_ID, WM_CHANNEL_ICON, 0, NULL));
    PositionCapture capture = {0};
    WmLayoutDrawOptions draw = {
        .alpha = 1,
        .on_pane = capture_position,
        .context = &capture
    };
    wm_layout_draw(layout, &draw);
    assert(capture.selected_seen && capture.unrelated_seen);
    assert(near(capture.selected_x, 99));
    assert(near(capture.unrelated_x, 0));
}

static void test_forecast_and_news(WmLayout *layout) {
    WmChannelAnimationPlan plan = plan_for(layout, FORECAST_ID,
                                            WM_CHANNEL_ICON, 100, NULL);
    assert(plan.count == 3);
    expect_clip(&plan, 1, "icon_Rso0", 100, "Rso0");
    expect_clip(&plan, 2, "icon_Rso1", 0, "");
    assert(wm_channel_animation_pose(layout, FORECAST_ID, WM_CHANNEL_ICON,
                                      100, NULL));
    assert(!visible(layout, "code"));

    assert(wm_channel_animation_pose(layout, FORECAST_ID, WM_CHANNEL_BANNER,
                                      100, NULL));
    assert(visible(layout, "all"));
    assert(!visible(layout, "weather"));
    assert(visible(layout, "textB0"));
    assert(visible(layout, "textT0"));
    assert(!visible(layout, "textT1"));

    plan = plan_for(layout, NEWS_ID, WM_CHANNEL_ICON, 100, NULL);
    assert(plan.count == 3);
    expect_clip(&plan, 1, "icon_Rso1", 0, "");
    expect_clip(&plan, 2, "icon_Rso2", 0, "");
    assert(wm_channel_animation_pose(layout, NEWS_ID, WM_CHANNEL_ICON, 0, NULL));
    assert(strcmp(pane_state(layout, "send_id").text, "") == 0);
    assert(wm_channel_animation_pose(layout, GENERIC_ID, WM_CHANNEL_ICON,
                                      0, NULL));
    assert(strcmp(pane_state(layout, "send_id").text, "placeholder") == 0);

    plan = plan_for(layout, NEWS_ID, WM_CHANNEL_BANNER, 1240, NULL);
    assert(plan.count == 3);
    expect_clip(&plan, 2, "banner_Rso0", 1239, "Rso0");
    assert(wm_channel_animation_pose(layout, NEWS_ID, WM_CHANNEL_BANNER,
                                      1240, NULL));
    assert(visible(layout, "textT0"));
    assert(!visible(layout, "textT1"));
}

static void test_shop(WmLayout *layout) {
    WmChannelAnimationPlan plan = plan_for(layout, SHOP_ID, WM_CHANNEL_ICON,
                                            635, NULL);
    assert(plan.count == 5);
    expect_clip(&plan, 1, "icon_Rso0", 5, "Rso0");
    expect_clip(&plan, 2, "icon_Rso1", 0, "");
    expect_clip(&plan, 3, "icon_Rso2", 0, "");
    expect_clip(&plan, 4, "icon_Rso3", 0, "");

    assert(wm_channel_animation_pose(layout, SHOP_ID, WM_CHANNEL_ICON, 0, NULL));
    assert(visible(layout, "N_SuperParent"));
    assert(visible(layout, "P_title_E_00"));
    assert(!visible(layout, "P_title_S_00"));

    WmChannelAnimationOptions spanish = {.language = "SPA"};
    assert(wm_channel_animation_pose(layout, SHOP_ID, WM_CHANNEL_ICON, 0,
                                      &spanish));
    assert(!visible(layout, "P_title_E_00"));
    assert(visible(layout, "P_title_S_00"));

    assert(wm_channel_animation_pose(layout, SHOP_ID, WM_CHANNEL_BANNER, 0,
                                      NULL));
    assert(visible(layout, "font_e"));
    assert(!visible(layout, "font_s"));
    assert(wm_channel_animation_pose(layout, SHOP_ID, WM_CHANNEL_BANNER, 0,
                                      &spanish));
    assert(!visible(layout, "font_e"));
    assert(visible(layout, "font_s"));
}

static float fixed_text_width(void *context, const WmLayout *layout,
                              const WmLayoutPaneState *pane,
                              const char *utf8, size_t length) {
    (void)context;
    (void)layout;
    (void)utf8;
    (void)length;
    return strcmp(pane->name, "T_messageE_00") == 0 ? 200 : 100;
}

static void test_seat_holder(WmLayout *layout) {
    WmChannelAnimationPlan plan = plan_for(layout, SEAT_ID, WM_CHANNEL_ICON,
                                            100, NULL);
    assert(plan.count == 5);
    expect_clip(&plan, 1, "icon_Rso0", 280, "Rso0");
    expect_clip(&plan, 2, "icon_Rso1", 160, "");
    expect_clip(&plan, 3, "icon_Rso2", 340, "");
    expect_clip(&plan, 4, "icon_Rso3", 100.0f * 1024.0f / 290.0f, "");

    WmChannelAnimationOptions english = {
        .language = "ENG",
        .measure_text = fixed_text_width
    };
    plan = plan_for(layout, SEAT_ID, WM_CHANNEL_BANNER, 100, &english);
    assert(plan.count == 5);
    expect_clip(&plan, 2, "banner_Rso0", 40, "Rso0");
    expect_clip(&plan, 3, "banner_Rso2", 100, "");
    expect_clip(&plan, 4, "banner_Rso1", 60, "");

    assert(wm_channel_animation_pose(layout, SEAT_ID, WM_CHANNEL_BANNER,
                                      100, &english));
    assert(!visible(layout, "N_base_00"));
    assert(visible(layout, "P_logoE_00"));
    assert(!visible(layout, "P_logoSp_00"));
    assert(visible(layout, "T_titleE_00"));
    assert(!visible(layout, "T_titleSp_00"));
    assert(visible(layout, "T_telopE_00"));
    assert(!visible(layout, "T_telopSp_00"));
    assert(visible(layout, "N_messageE_00"));
    assert(!visible(layout, "N_messageSp_00"));
    assert(visible(layout, "T_messageE_00"));
    assert(!visible(layout, "T_messageE_01"));
    assert(!visible(layout, "T_messageSp_00"));
    assert(visible(layout, "N_title_00"));
    assert(near(pane_state(layout, "W_messWindow_00").size[1], 120));
    assert(near(pane_state(layout, "T_messageE_00").font_size[0], 11.88f));
    assert(near(pane_state(layout, "T_messageE_00").char_space, 0.594f));

    WmChannelAnimationOptions spanish = {.language = "SPA"};
    assert(wm_channel_animation_pose(layout, SEAT_ID, WM_CHANNEL_ICON, 0,
                                      &spanish));
    assert(!visible(layout, "N_base_00"));
    assert(!visible(layout, "P_logoE_00"));
    assert(visible(layout, "P_logoSp_00"));
    assert(visible(layout, "P_BG_00"));
    assert(visible(layout, "P_BG_01"));
}

static void test_connection_video(WmLayout *layout) {
    WmChannelAnimationPlan plan = plan_for(layout, CONNECTION_ID,
                                            WM_CHANNEL_ICON, 0, NULL);
    assert(plan.count == 2);
    expect_clip(&plan, 1, "icon_Rso0", 1000, "Rso0");
    assert(wm_channel_animation_pose(layout, CONNECTION_ID, WM_CHANNEL_ICON,
                                      0, NULL));
    assert(!visible(layout, "fade"));
    assert(!visible(layout, "txt_3"));
    assert(visible(layout, "bg170_96"));
    assert(visible(layout, "color"));
    assert(visible(layout, "wii"));
    assert(strcmp(pane_state(layout, "txt_green").text,
                  "Get more\nchannels") == 0);
    assert(near(pane_state(layout, "txt_orange").font_size[0], 18));

    WmChannelAnimationOptions japanese = {.language = "JPN"};
    assert(wm_channel_animation_pose(layout, CONNECTION_ID, WM_CHANNEL_ICON,
                                      0, &japanese));
    assert(strcmp(pane_state(layout, "txt_green").text, "") == 0);

    WmChannelAnimationOptions configured = {.network_configured = true};
    plan = plan_for(layout, CONNECTION_ID, WM_CHANNEL_ICON, 0, &configured);
    expect_clip(&plan, 1, "icon_Rso0", 1, "Rso0");
    assert(wm_channel_animation_pose(layout, CONNECTION_ID, WM_CHANNEL_ICON,
                                      0, &configured));
    assert(visible(layout, "fade"));
    assert(visible(layout, "txt_3"));

    plan = plan_for(layout, CONNECTION_ID, WM_CHANNEL_BANNER, 2000, NULL);
    assert(plan.count == 3);
    expect_clip(&plan, 2, "banner_Rso0", 1240, "Rso0");
}

static void test_language_group_mask(WmLayout *layout) {
    assert(wm_channel_animation_pose(layout, GENERIC_ID, WM_CHANNEL_ICON,
                                      0, NULL));
    assert(!visible(layout, "LanguageJapanese"));
    assert(visible(layout, "LanguageEnglish"));
    assert(!visible(layout, "LanguageSpanish"));
    assert(visible(layout, "LanguageShared"));
    assert(visible(layout, "LanguageUnassigned"));

    WmChannelAnimationOptions spanish = {.language = "SPA"};
    assert(wm_channel_animation_pose(layout, GENERIC_ID, WM_CHANNEL_ICON,
                                      0, &spanish));
    assert(!visible(layout, "LanguageJapanese"));
    assert(!visible(layout, "LanguageEnglish"));
    assert(visible(layout, "LanguageSpanish"));
    assert(visible(layout, "LanguageShared"));
    assert(visible(layout, "LanguageUnassigned"));

    WmChannelAnimationOptions japanese = {.language = "JPN"};
    assert(wm_channel_animation_pose(layout, GENERIC_ID, WM_CHANNEL_ICON,
                                      0, &japanese));
    assert(visible(layout, "LanguageJapanese"));
    assert(!visible(layout, "LanguageEnglish"));
    assert(!visible(layout, "LanguageSpanish"));
    assert(visible(layout, "LanguageShared"));

    /* A new pose must restore authored visibility before applying the mask. */
    assert(wm_channel_animation_pose(layout, GENERIC_ID, WM_CHANNEL_ICON,
                                      0, NULL));
    assert(!visible(layout, "LanguageJapanese"));
    assert(visible(layout, "LanguageEnglish"));
    assert(!visible(layout, "LanguageSpanish"));
    assert(visible(layout, "LanguageShared"));
}

static void test_invalid_id(const WmLayout *layout) {
    WmChannelAnimationPlan plan = {0};
    assert(!wm_channel_animation_plan(layout, "HAYA", WM_CHANNEL_ICON,
                                      0, NULL, &plan));
    assert(!wm_channel_animation_plan(layout, "000100014841594Z",
                                      WM_CHANNEL_ICON, 0, NULL, &plan));
    assert(!wm_channel_animation_plan(layout, NULL, WM_CHANNEL_ICON,
                                      0, NULL, &plan));
}

typedef struct BannerAlphaCapture {
    const char *pane_name;
    bool pane_seen;
    bool quad_seen;
    float pane_alpha;
    float quad_alpha;
} BannerAlphaCapture;

static bool capture_banner_alpha(void *context, const WmLayoutPaneView *pane) {
    BannerAlphaCapture *capture = context;
    if (strcmp(pane->name, capture->pane_name) == 0) {
        capture->pane_seen = true;
        capture->pane_alpha = pane->alpha;
    }
    return true;
}

static void capture_banner_quad(void *context, const WmLayoutQuad *quad) {
    BannerAlphaCapture *capture = context;
    if (strcmp(quad->pane_name, capture->pane_name) == 0) {
        capture->quad_seen = true;
        capture->quad_alpha = quad->vertices[0].color[3];
    }
}

static void expect_banner_alpha(WmLayout *layout, const char *title_id,
                                const char *pane_name, float base_frame,
                                float module_frame, float expected_alpha) {
    WmChannelAnimationOptions options = {
        .language = "ENG",
        .has_base_frame = true,
        .base_frame = base_frame
    };
    assert(wm_channel_animation_pose(layout, title_id, WM_CHANNEL_BANNER,
                                      module_frame, &options));
    BannerAlphaCapture capture = {.pane_name = pane_name};
    WmLayoutDrawOptions draw = {
        .wide = true,
        .mode = WM_LAYOUT_IPL,
        .alpha = 1,
        .on_pane = capture_banner_alpha,
        .on_quad = capture_banner_quad,
        .context = &capture
    };
    wm_layout_draw(layout, &draw);
    assert(capture.pane_seen);
    if (!near(capture.pane_alpha, expected_alpha)) {
        fprintf(stderr, "%s %s frame %.1f: alpha %.6f, expected %.6f\n",
                title_id, pane_name, base_frame, capture.pane_alpha,
                expected_alpha);
    }
    assert(near(capture.pane_alpha, expected_alpha));
    /* Fully transparent panes are culled; visible picture quads carry the
     * same animation opacity into both graphics backends. */
    assert(capture.quad_seen == (expected_alpha > 0));
    if (capture.quad_seen) assert(near(capture.quad_alpha, expected_alpha));
}

static void test_native_banner_fades(void) {
    /* Extracted layouts are intentionally ignored by Git. Check them when
     * present without making the source build depend on private WAD data. */
    static const struct {
        const char *path;
        const char *title_id;
        const char *pane_name;
        float alpha[4];
    } banners[] = {
        {
            ".local/native-assets/channel-layouts/0001000248415941/banner/banner.json",
            "0001000248415941", "belt_a", {0, 0.15625f, 0.5f, 1}
        },
        {
            ".local/native-assets/channel-layouts/0001000248414241/banner/banner.json",
            "0001000248414241", "logo_03", {0.5f, 2.0f / 3.0f,
                                               5.0f / 6.0f, 1}
        }
    };
    static const float frames[] = {0, 10, 20, 40};
    for (size_t index = 0; index < sizeof banners / sizeof banners[0]; index++) {
        FILE *file = fopen(banners[index].path, "rb");
        if (!file) continue;
        fclose(file);
        char error[256];
        WmLayout *layout = wm_layout_load_json(banners[index].path,
                                                error, sizeof error);
        if (!layout) fprintf(stderr, "native banner load failed: %s\n", error);
        assert(layout);
        for (size_t frame = 0; frame < sizeof frames / sizeof frames[0]; frame++) {
            expect_banner_alpha(layout, banners[index].title_id,
                                banners[index].pane_name, frames[frame],
                                frames[frame], banners[index].alpha[frame]);
        }
        if (strcmp(banners[index].pane_name, "logo_03") == 0) {
            /* A destination module may lead by ten frames while its authored
             * banner_Start fade still begins at base frame zero. */
            expect_banner_alpha(layout, banners[index].title_id, "logo_03",
                                0, 10, 0.5f);
            /* The English title remains absent during the logo's opening,
             * then fades in under the same one-shot Start clock. Its final
             * opacity must survive the handoff to banner_Loop. */
            expect_banner_alpha(layout, banners[index].title_id, "Picture_76",
                                0, 0, 0);
            expect_banner_alpha(layout, banners[index].title_id, "Picture_76",
                                500, 500, 0.1203598f);
            expect_banner_alpha(layout, banners[index].title_id, "Picture_76",
                                526, 526, 1);
            expect_banner_alpha(layout, banners[index].title_id, "Picture_76",
                                790, 790, 1);
        } else {
            /* Photo's authored Rso0 fades several layers at different rates.
             * Once it loops from frame 1239 to 40, it must stay opaque. */
            expect_banner_alpha(layout, banners[index].title_id, "frame3",
                                0, 0, 0);
            expect_banner_alpha(layout, banners[index].title_id, "frame3",
                                10, 10, 0.5f);
            expect_banner_alpha(layout, banners[index].title_id, "frame3",
                                20, 20, 1);
            expect_banner_alpha(layout, banners[index].title_id, "logoENG",
                                0, 0, 0);
            expect_banner_alpha(layout, banners[index].title_id, "logoENG",
                                20, 20, 0.625f);
            expect_banner_alpha(layout, banners[index].title_id, "belt_a",
                                1240, 1240, 1);
        }
        wm_layout_destroy(layout);
    }
}

typedef struct MessageGeometry {
    bool window_seen;
    bool text_seen;
    float window_corners[4][3];
    float text_matrix[12];
    float minimum_x;
    float minimum_y;
    float maximum_x;
    float maximum_y;
} MessageGeometry;

static bool capture_message_panes(void *context, const WmLayoutPaneView *pane) {
    MessageGeometry *geometry = context;
    if (strcmp(pane->name, "W_messWindow_00") == 0) {
        geometry->window_seen = true;
        memcpy(geometry->window_corners, pane->corners,
               sizeof(geometry->window_corners));
    } else if (strcmp(pane->name, "T_messageE_00") == 0) {
        geometry->text_seen = true;
        memcpy(geometry->text_matrix, pane->matrix,
               sizeof(geometry->text_matrix));
    }
    return true;
}

static bool accept_font_sheet(void *context, size_t sheet, uint32_t *texture) {
    (void)context;
    (void)sheet;
    *texture = 1;
    return true;
}

static void capture_glyph_bounds(void *context, const WmFontQuad *quad) {
    MessageGeometry *geometry = context;
    for (size_t corner = 0; corner < 4; corner++) {
        const float *position = quad->vertices[corner].position;
        geometry->minimum_x = fminf(geometry->minimum_x, position[0]);
        geometry->minimum_y = fminf(geometry->minimum_y, position[1]);
        geometry->maximum_x = fmaxf(geometry->maximum_x, position[0]);
        geometry->maximum_y = fmaxf(geometry->maximum_y, position[1]);
    }
}

static float measure_native_message(void *context, const WmLayout *layout,
                                    const WmLayoutPaneState *pane,
                                    const char *text, size_t length) {
    (void)layout;
    return wm_font_text_width_n(context, text, length, pane->font_size,
                                pane->char_space);
}

static WmFont *load_native_message_font(void) {
    FILE *file = fopen(".local/native-assets/fonts/wbf1.brfna", "rb");
    if (!file) return NULL;
    if (fseek(file, 0, SEEK_END) != 0) {
        fclose(file);
        return NULL;
    }
    long length = ftell(file);
    if (length <= 0 || length > 64 * 1024 * 1024 ||
        fseek(file, 0, SEEK_SET) != 0) {
        fclose(file);
        return NULL;
    }
    uint8_t *bytes = malloc((size_t)length);
    if (!bytes) {
        fclose(file);
        return NULL;
    }
    bool read = fread(bytes, 1, (size_t)length, file) == (size_t)length;
    fclose(file);
    char error[256];
    WmFont *font = read ? wm_font_decode(bytes, (size_t)length,
                                         error, sizeof(error)) : NULL;
    free(bytes);
    assert(font);
    return font;
}

static void test_native_message_window_geometry(void) {
    /* The original WAD assets are intentionally absent from Git. When they
     * are prepared locally, check glyph bounds after the same width change
     * that the preview uses against the emitted window pane. */
    WmFont *font = load_native_message_font();
    if (!font) return;
    static const char *const channels[] = {
        "0001000148414a45", /* Everybody Votes Channel */
        "0001000148434c45"  /* Netflix */
    };
    for (size_t index = 0; index < sizeof(channels) / sizeof(channels[0]); index++) {
        char path[256];
        snprintf(path, sizeof(path),
                 ".local/native-assets/channel-layouts/%s/banner/banner.json",
                 channels[index]);
        FILE *file = fopen(path, "rb");
        if (!file) continue;
        fclose(file);
        char error[256];
        WmLayout *layout = wm_layout_load_json(path, error, sizeof(error));
        assert(layout);
        WmChannelAnimationOptions options = {
            .language = "ENG",
            .measure_text = measure_native_message,
            .measure_context = font
        };
        assert(wm_channel_animation_pose(layout, channels[index],
                                          WM_CHANNEL_BANNER, 100, &options));
        WmLayoutPaneState pane = pane_state(layout, "T_messageE_00");
        WmFontPane font_pane;
        const char *font_name;
        assert(wm_layout_pane_font(layout, "T_messageE_00",
                                    &font_pane, &font_name));
        assert(font_name && strcmp(font_name, "wbf1.brfna") == 0);
        WmFontTextLayout *text_layout =
            wm_font_layout_pane(font, pane.text, &font_pane);
        assert(text_layout);
        MessageGeometry geometry = {
            .minimum_x = FLT_MAX, .minimum_y = FLT_MAX,
            .maximum_x = -FLT_MAX, .maximum_y = -FLT_MAX
        };
        WmLayoutDrawOptions draw = {
            .wide = true, .mode = WM_LAYOUT_IPL, .alpha = 1,
            .on_pane = capture_message_panes, .context = &geometry
        };
        wm_layout_draw(layout, &draw);
        assert(geometry.window_seen && geometry.text_seen);
        wm_font_emit_pane(text_layout, geometry.text_matrix, 1,
                          accept_font_sheet, capture_glyph_bounds, &geometry);
        assert(geometry.minimum_x >= geometry.window_corners[0][0]);
        assert(geometry.maximum_x <= geometry.window_corners[1][0]);
        assert(geometry.maximum_y <= geometry.window_corners[0][1]);
        assert(geometry.minimum_y >= geometry.window_corners[2][1]);
        wm_font_text_layout_destroy(text_layout);
        wm_layout_destroy(layout);
    }
    wm_font_destroy(font);
}

int main(void) {
    test_frame_controller();
    WmLayout *layout = load_fixture();
    test_base_intro_and_loop(layout);
    test_photo_and_group_binding(layout);
    test_forecast_and_news(layout);
    test_shop(layout);
    test_seat_holder(layout);
    test_connection_video(layout);
    test_language_group_mask(layout);
    test_invalid_id(layout);
    wm_layout_destroy(layout);
    test_native_banner_fades();
    test_native_message_window_geometry();
    return 0;
}
