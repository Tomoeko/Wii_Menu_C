#include "wii_menu/options_scene.h"

#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <string.h>

static bool capture_render;
static uint32_t next_texture = 1;
static float back_button_width;
static float left_tile_alpha;
static float right_tile_alpha;

/* Render spies inspect source-layout geometry without opening a GPU window. */
void wm_platform_begin(WmPlatform *platform, WmColor clear_color) {
    (void)platform;
    (void)clear_color;
    assert(false);
}

void wm_platform_end(WmPlatform *platform) {
    (void)platform;
    assert(false);
}

void wm_platform_set_fade_alpha(WmPlatform *platform, float alpha) {
    (void)platform;
    (void)alpha;
    assert(false);
}

void wm_platform_set_clip(WmPlatform *platform, const WmClipRect *rect) {
    (void)platform;
    (void)rect;
    assert(capture_render);
}

void wm_platform_draw_quad(WmPlatform *platform, const WmQuad *quad) {
    (void)platform;
    (void)quad;
    assert(false);
}

void wm_platform_prepare_material(WmPlatform *platform,
                                  const WmMaterialQuad *quad) {
    (void)platform;
    (void)quad;
}

void wm_platform_draw_vertices(WmPlatform *platform,
                               const WmDrawVertex vertices[4],
                               uint32_t texture) {
    (void)platform;
    (void)vertices;
    (void)texture;
    assert(capture_render);
}

void wm_platform_draw_material_quad(WmPlatform *platform,
                                    const WmMaterialQuad *quad) {
    (void)platform;
    assert(capture_render);
    const float left = quad->vertices[0].x;
    const float top = quad->vertices[0].y;
    const float width = quad->vertices[1].x - left;
    if (left > 70.0f && left < 100.0f && top > 350.0f &&
        top < 380.0f && width > 100.0f && width < 170.0f) {
        back_button_width = fmaxf(back_button_width, width);
    }
    if (top > 119.0f && top < 125.0f &&
        width > 190.0f && width < 205.0f) {
        if (left > 70.0f && left < 90.0f) {
            left_tile_alpha = fmaxf(left_tile_alpha,
                                    quad->vertices[0].color.a);
        } else if (left > 355.0f && left < 380.0f) {
            right_tile_alpha = fmaxf(right_tile_alpha,
                                     quad->vertices[0].color.a);
        }
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

static bool has_hit(WmOptionsScene *scene, WmOptionsControl control) {
    for (int y = 0; y < WM_FRAME_HEIGHT; y += 8) {
        for (int x = 0; x < WM_FRAME_WIDTH; x += 8) {
            if (wm_options_scene_hit(scene, x, y) == control) return true;
        }
    }
    return false;
}

static void test_options_hierarchy(WmOptionsScene *scene) {
    assert(wm_options_scene_open(scene));
    assert(wm_options_scene_snapshot(scene).phase == WM_OPTIONS_ENTER_BACK);
    assert(!wm_options_scene_activate(scene, WM_OPTIONS_CONTROL_DATA));
    wm_options_scene_advance(scene, 31.0f);
    assert(wm_options_scene_snapshot(scene).locked);
    wm_options_scene_advance(scene, 1.0f);
    assert(wm_options_scene_snapshot(scene).phase == WM_OPTIONS_READY);
    assert(has_hit(scene, WM_OPTIONS_CONTROL_DATA));
    assert(has_hit(scene, WM_OPTIONS_CONTROL_SYSTEM));
    assert(has_hit(scene, WM_OPTIONS_CONTROL_BACK));
    assert(strcmp(wm_options_scene_click_cue(scene, WM_OPTIONS_CONTROL_BACK),
                  "WIPL_SE_CANCEL") == 0);
    assert(wm_options_scene_hover(scene, WM_OPTIONS_CONTROL_DATA));
    wm_options_scene_advance(scene, 3.0f);
    assert(wm_options_scene_hover(scene, WM_OPTIONS_CONTROL_SYSTEM));
    wm_options_scene_advance(scene, 4.0f);
    assert(wm_options_scene_activate(scene, WM_OPTIONS_CONTROL_DATA));
    wm_options_scene_advance(scene, 39.0f);
    assert(wm_options_scene_snapshot(scene).page == WM_OPTIONS_PAGE_OPTIONS);
    wm_options_scene_advance(scene, 1.0f);
    assert(wm_options_scene_snapshot(scene).page == WM_OPTIONS_PAGE_DATA);
    assert(wm_options_scene_snapshot(scene).phase == WM_OPTIONS_ENTER_LEVEL);
    wm_options_scene_advance(scene, 16.0f);
    assert(wm_options_scene_snapshot(scene).phase == WM_OPTIONS_READY);
    assert(has_hit(scene, WM_OPTIONS_CONTROL_SAVE));
    assert(has_hit(scene, WM_OPTIONS_CONTROL_CHANNELS));
    assert(strcmp(wm_options_scene_click_cue(scene, WM_OPTIONS_CONTROL_BACK),
                  "WIPL_SE_CANCEL") == 0);
    assert(wm_options_scene_activate(scene, WM_OPTIONS_CONTROL_SAVE));
    wm_options_scene_advance(scene, 56.0f);
    assert(wm_options_scene_snapshot(scene).page == WM_OPTIONS_PAGE_SAVE);
    assert(wm_options_scene_snapshot(scene).phase == WM_OPTIONS_READY);
    assert(has_hit(scene, WM_OPTIONS_CONTROL_WII));
    assert(has_hit(scene, WM_OPTIONS_CONTROL_GAMECUBE));
    assert(strcmp(wm_options_scene_click_cue(scene, WM_OPTIONS_CONTROL_BACK),
                  "WIPL_SE_CANCEL") == 0);

    assert(wm_options_scene_back(scene));
    wm_options_scene_advance(scene, 39.0f);
    assert(wm_options_scene_snapshot(scene).page == WM_OPTIONS_PAGE_DATA);
    assert(wm_options_scene_snapshot(scene).phase == WM_OPTIONS_READY);
    assert(wm_options_scene_back(scene));
    wm_options_scene_advance(scene, 39.0f);
    assert(wm_options_scene_snapshot(scene).page == WM_OPTIONS_PAGE_OPTIONS);
    assert(wm_options_scene_snapshot(scene).phase == WM_OPTIONS_READY);
    assert(wm_options_scene_back(scene));
    wm_options_scene_advance(scene, 19.0f);
    assert(wm_options_scene_snapshot(scene).phase == WM_OPTIONS_EXIT_HOLD);
    assert(wm_options_scene_take_action(scene) == WM_OPTIONS_ACTION_EXITED);
    assert(wm_options_scene_take_action(scene) == WM_OPTIONS_ACTION_NONE);
}

static void test_save_data_leaf_handoffs(WmOptionsScene *scene) {
    wm_options_scene_reset(scene);
    assert(wm_options_scene_open(scene));
    wm_options_scene_advance(scene, 32.0f);
    assert(wm_options_scene_activate(scene, WM_OPTIONS_CONTROL_DATA));
    wm_options_scene_advance(scene, 56.0f);
    assert(wm_options_scene_activate(scene, WM_OPTIONS_CONTROL_SAVE));
    wm_options_scene_advance(scene, 56.0f);

    /* Options > Data Management > Save Data is already two levels deep.
     * Both leaves need one more history entry for their storage handoff. */
    assert(wm_options_scene_snapshot(scene).page == WM_OPTIONS_PAGE_SAVE);
    assert(wm_options_scene_activate(scene, WM_OPTIONS_CONTROL_WII));
    wm_options_scene_advance(scene, 40.0f);
    assert(wm_options_scene_snapshot(scene).page ==
           WM_OPTIONS_PAGE_WII_STORAGE);
    assert(wm_options_scene_take_action(scene) ==
           WM_OPTIONS_ACTION_WII_STORAGE);
    assert(wm_options_scene_take_action(scene) == WM_OPTIONS_ACTION_NONE);
    assert(wm_options_scene_back(scene));
    wm_options_scene_advance(scene, 20.0f);
    assert(wm_options_scene_snapshot(scene).page == WM_OPTIONS_PAGE_SAVE);

    assert(wm_options_scene_activate(scene, WM_OPTIONS_CONTROL_GAMECUBE));
    wm_options_scene_advance(scene, 40.0f);
    assert(wm_options_scene_snapshot(scene).page ==
           WM_OPTIONS_PAGE_GAMECUBE_STORAGE);
    assert(wm_options_scene_take_action(scene) ==
           WM_OPTIONS_ACTION_GAMECUBE_STORAGE);
    assert(wm_options_scene_back(scene));
    wm_options_scene_advance(scene, 20.0f);
    assert(wm_options_scene_snapshot(scene).page == WM_OPTIONS_PAGE_SAVE);
    assert(wm_options_scene_back(scene));
    wm_options_scene_advance(scene, 39.0f);
    assert(wm_options_scene_snapshot(scene).page == WM_OPTIONS_PAGE_DATA);
    assert(wm_options_scene_back(scene));
    wm_options_scene_advance(scene, 39.0f);
    assert(wm_options_scene_snapshot(scene).page == WM_OPTIONS_PAGE_OPTIONS);
}

static void test_leaf_handoffs(WmOptionsScene *scene) {
    assert(wm_options_scene_open(scene));
    wm_options_scene_advance(scene, 32.0f);
    assert(wm_options_scene_activate(scene, WM_OPTIONS_CONTROL_SYSTEM));
    wm_options_scene_advance(scene, 40.0f);
    assert(wm_options_scene_snapshot(scene).page ==
           WM_OPTIONS_PAGE_SYSTEM_SETTINGS);
    assert(wm_options_scene_take_action(scene) ==
           WM_OPTIONS_ACTION_NONE);
    assert(wm_options_scene_hit(scene, 320, 228) == WM_OPTIONS_CONTROL_NONE);
    wm_options_scene_advance(scene, 21.0f);
    assert(wm_options_scene_hit(scene, 320, 228) ==
           WM_OPTIONS_CONTROL_SETTINGS_ITEM_3);
    assert(wm_options_scene_activate(
        scene, WM_OPTIONS_CONTROL_SETTINGS_NEXT));
    wm_options_scene_advance(scene, 40.0f);
    assert(wm_options_scene_hit(scene, 320, 228) ==
           WM_OPTIONS_CONTROL_SETTINGS_ITEM_3);
    assert(wm_options_scene_activate(
        scene, WM_OPTIONS_CONTROL_SETTINGS_ITEM_2));
    assert(wm_options_scene_take_action(scene) ==
           WM_OPTIONS_ACTION_NONE);
    assert(wm_options_scene_take_settings_category(scene) == 0);
    /* Sensor Bar has a native C category screen. Back first returns to the
     * Settings index, then leaves the Settings stack. */
    assert(wm_options_scene_back(scene));
    assert(wm_options_scene_snapshot(scene).page ==
           WM_OPTIONS_PAGE_SYSTEM_SETTINGS);
    assert(wm_options_scene_back(scene));
    wm_options_scene_advance(scene, 20.0f);
    assert(wm_options_scene_snapshot(scene).page ==
           WM_OPTIONS_PAGE_SYSTEM_SETTINGS);
    assert(wm_options_scene_snapshot(scene).locked);
    wm_options_scene_advance(scene, 3.0f);
    assert(wm_options_scene_snapshot(scene).page == WM_OPTIONS_PAGE_OPTIONS);
    assert(wm_options_scene_snapshot(scene).phase == WM_OPTIONS_ENTER_BACK);
    wm_options_scene_advance(scene, 22.0f);
    assert(wm_options_scene_snapshot(scene).locked);
    wm_options_scene_advance(scene, 10.0f);
    assert(wm_options_scene_snapshot(scene).phase == WM_OPTIONS_READY);

    assert(wm_options_scene_activate(scene, WM_OPTIONS_CONTROL_DATA));
    wm_options_scene_advance(scene, 56.0f);
    assert(wm_options_scene_activate(scene, WM_OPTIONS_CONTROL_CHANNELS));
    wm_options_scene_advance(scene, 40.0f);
    assert(wm_options_scene_take_action(scene) ==
           WM_OPTIONS_ACTION_CHANNEL_STORAGE);
    assert(wm_options_scene_back(scene));
    wm_options_scene_advance(scene, 20.0f);
    assert(wm_options_scene_snapshot(scene).page == WM_OPTIONS_PAGE_DATA);
    wm_options_scene_reset(scene);
    assert(wm_options_scene_snapshot(scene).phase == WM_OPTIONS_CLOSED);
    assert(wm_options_scene_take_action(scene) == WM_OPTIONS_ACTION_NONE);
    assert(wm_options_scene_take_settings_category(scene) == 0);
    assert(wm_options_scene_open(scene));
    wm_options_scene_advance(scene, 32.0f);
    assert(wm_options_scene_snapshot(scene).phase == WM_OPTIONS_READY);
}

static void test_nickname_keyboard(WmOptionsScene *scene) {
    wm_options_scene_reset(scene);
    assert(wm_options_scene_open(scene));
    wm_options_scene_advance(scene, 32.0f);
    assert(wm_options_scene_activate(scene, WM_OPTIONS_CONTROL_SYSTEM));
    wm_options_scene_advance(scene, 40.0f);
    wm_options_scene_advance(scene, 21.0f);
    assert(wm_options_scene_snapshot(scene).page ==
           WM_OPTIONS_PAGE_SYSTEM_SETTINGS);
    assert(!wm_options_scene_text_editing(scene));
    assert(wm_options_scene_activate(
        scene, WM_OPTIONS_CONTROL_SETTINGS_ITEM_1));
    assert(wm_options_scene_text_editing(scene));
    assert(wm_options_scene_type_ascii(scene, 'h'));
    assert(wm_options_scene_backspace(scene));
    assert(wm_options_scene_activate(
        scene, WM_OPTIONS_CONTROL_SETTINGS_NEXT));
    assert(!wm_options_scene_text_editing(scene));
    assert(!wm_options_scene_type_ascii(scene, 'h'));
}

static float rendered_back_button_width(WmOptionsScene *scene) {
    back_button_width = 0.0f;
    left_tile_alpha = 0.0f;
    right_tile_alpha = 0.0f;
    capture_render = true;
    assert(wm_options_scene_draw(scene));
    capture_render = false;
    return back_button_width;
}

static void test_entry_and_selection_samples(WmOptionsScene *scene) {
    wm_options_scene_reset(scene);
    assert(wm_options_scene_open(scene));

    /* The HTML controller first plays the 16-frame Back-bar clip, then the
     * two 16-frame option clips. The exported USA 4.3 tile art must not flash
     * in while the footer is still entering. */
    rendered_back_button_width(scene);
    assert(back_button_width == 0.0f);
    assert(left_tile_alpha == 0.0f && right_tile_alpha == 0.0f);
    wm_options_scene_advance(scene, 15.0f);
    rendered_back_button_width(scene);
    assert(back_button_width > 118.0f);
    assert(left_tile_alpha == 0.0f && right_tile_alpha == 0.0f);
    wm_options_scene_advance(scene, 9.0f);
    rendered_back_button_width(scene);
    assert(left_tile_alpha > 0.5f && left_tile_alpha < 0.6f);
    assert(right_tile_alpha > 0.5f && right_tile_alpha < 0.6f);
    wm_options_scene_advance(scene, 8.0f);
    rendered_back_button_width(scene);
    assert(left_tile_alpha > 0.99f && right_tile_alpha > 0.99f);

    /* The unselected sibling follows its own Out curve during the selected
     * tile's longer flash; it must not disappear on the click frame. */
    assert(wm_options_scene_activate(scene, WM_OPTIONS_CONTROL_DATA));
    rendered_back_button_width(scene);
    assert(right_tile_alpha > 0.99f);
    wm_options_scene_advance(scene, 8.0f);
    rendered_back_button_width(scene);
    assert(right_tile_alpha > 0.4f && right_tile_alpha < 0.6f);
    wm_options_scene_advance(scene, 7.0f);
    rendered_back_button_width(scene);
    assert(right_tile_alpha == 0.0f);
    wm_options_scene_advance(scene, 25.0f);
    rendered_back_button_width(scene);
    assert(left_tile_alpha == 0.0f && right_tile_alpha == 0.0f);
    wm_options_scene_advance(scene, 8.0f);
    rendered_back_button_width(scene);
    assert(left_tile_alpha > 0.5f && right_tile_alpha > 0.5f);
    wm_options_scene_advance(scene, 8.0f);
    assert(wm_options_scene_back(scene));
    rendered_back_button_width(scene);
    float back_start = back_button_width;
    assert(back_start > 118.0f);
    assert(left_tile_alpha > 0.99f && right_tile_alpha > 0.99f);
    wm_options_scene_advance(scene, 9.0f);
    rendered_back_button_width(scene);
    assert(back_button_width > back_start);
    assert(left_tile_alpha > 0.99f && right_tile_alpha > 0.99f);
    wm_options_scene_advance(scene, 10.0f);
    rendered_back_button_width(scene);
    assert(fabsf(back_button_width - back_start) < 0.02f);
    assert(left_tile_alpha > 0.99f && right_tile_alpha > 0.99f);
    wm_options_scene_advance(scene, 10.0f);
    rendered_back_button_width(scene);
    assert(left_tile_alpha > 0.2f && right_tile_alpha > 0.7f);
    wm_options_scene_advance(scene, 10.0f);
    rendered_back_button_width(scene);
    assert(left_tile_alpha > 0.99f && right_tile_alpha > 0.99f);

    /* Root Back completes its flash before the global fade replaces the
     * retained Options layout. The back bar remains drawn under that fade. */
    assert(wm_options_scene_back(scene));
    wm_options_scene_advance(scene, 19.0f);
    assert(wm_options_scene_snapshot(scene).phase == WM_OPTIONS_EXIT_HOLD);
    rendered_back_button_width(scene);
    assert(back_button_width > 118.0f);
    assert(wm_options_scene_take_action(scene) == WM_OPTIONS_ACTION_EXITED);
}

static void test_back_focus_during_selection(WmOptionsScene *scene) {
    wm_options_scene_reset(scene);
    assert(wm_options_scene_open(scene));
    wm_options_scene_advance(scene, 32.0f);
    float neutral = rendered_back_button_width(scene);
    assert(wm_options_scene_hover(scene, WM_OPTIONS_CONTROL_BACK));
    wm_options_scene_advance(scene, 7.0f);
    assert(wm_options_scene_hover(scene, WM_OPTIONS_CONTROL_DATA));
    wm_options_scene_advance(scene, 2.0f);
    float before = rendered_back_button_width(scene);
    /* The maintained HTML controller commits each active focus sample before
     * FoucusFlash. The USA 4.3 Back rollout keeps N_Button_00 enlarged here. */
    assert(before > neutral * 1.05f);
    assert(wm_options_scene_activate(scene, WM_OPTIONS_CONTROL_DATA));
    float after = rendered_back_button_width(scene);
    assert(fabsf(after - before) < 0.02f);
    wm_options_scene_advance(scene, 1.0f);
    assert(fabsf(rendered_back_button_width(scene) - before) < 0.02f);

    wm_options_scene_advance(scene, 55.0f);
    assert(wm_options_scene_snapshot(scene).page == WM_OPTIONS_PAGE_DATA);
    assert(wm_options_scene_back(scene));
    wm_options_scene_advance(scene, 39.0f);
    assert(wm_options_scene_snapshot(scene).page == WM_OPTIONS_PAGE_OPTIONS);
    assert(wm_options_scene_snapshot(scene).phase == WM_OPTIONS_READY);

    /* A completed Back flash must yield to a newly sampled focus pose. */
    neutral = rendered_back_button_width(scene);
    assert(wm_options_scene_hover(scene, WM_OPTIONS_CONTROL_BACK));
    wm_options_scene_advance(scene, 7.0f);
    assert(wm_options_scene_hover(scene, WM_OPTIONS_CONTROL_SYSTEM));
    wm_options_scene_advance(scene, 2.0f);
    before = rendered_back_button_width(scene);
    assert(before > neutral * 1.05f);
    assert(wm_options_scene_activate(scene, WM_OPTIONS_CONTROL_SYSTEM));
    assert(fabsf(rendered_back_button_width(scene) - before) < 0.02f);
}

static void test_update_question_cue_context(WmOptionsScene *scene) {
    wm_options_scene_reset(scene);
    assert(wm_options_scene_open(scene));
    wm_options_scene_advance(scene, 32.0f);
    assert(!wm_options_scene_update_question(scene));
    assert(wm_options_scene_activate(scene, WM_OPTIONS_CONTROL_SYSTEM));
    wm_options_scene_advance(scene, 40.0f);
    wm_options_scene_advance(scene, 21.0f);
    assert(wm_options_scene_snapshot(scene).page ==
           WM_OPTIONS_PAGE_SYSTEM_SETTINGS);
    assert(strcmp(wm_options_scene_click_cue(
        scene, WM_OPTIONS_CONTROL_BACK), "WIPL_SE_CANCEL") == 0);
    for (unsigned page = 1; page < 3; page++) {
        assert(wm_options_scene_activate(
            scene, WM_OPTIONS_CONTROL_SETTINGS_NEXT));
        wm_options_scene_advance(scene, 40.0f);
    }
    assert(wm_options_scene_activate(
        scene, WM_OPTIONS_CONTROL_SETTINGS_ITEM_3));
    wm_options_scene_advance(scene, 20.0f);
    assert(wm_options_scene_update_question(scene));
    assert(strcmp(wm_options_scene_click_cue(
        scene, WM_OPTIONS_CONTROL_BACK), "WIPL_SE_DECIDE") == 0);
    assert(strcmp(wm_options_scene_click_cue(
        scene, WM_OPTIONS_CONTROL_SETTINGS_NEXT), "WIPL_SE_CANCEL") == 0);
    assert(wm_options_scene_activate(scene, WM_OPTIONS_CONTROL_BACK));
    wm_options_scene_advance(scene, 20.0f);
    assert(!wm_options_scene_update_question(scene));
}

static void test_user_agreements_click_cues(WmOptionsScene *scene) {
    wm_options_scene_reset(scene);
    assert(wm_options_scene_open(scene));
    wm_options_scene_advance(scene, 32.0f);
    assert(wm_options_scene_activate(scene, WM_OPTIONS_CONTROL_SYSTEM));
    wm_options_scene_advance(scene, 40.0f);
    wm_options_scene_advance(scene, 21.0f);
    assert(wm_options_scene_activate(
        scene, WM_OPTIONS_CONTROL_SETTINGS_NEXT));
    wm_options_scene_advance(scene, 40.0f);
    assert(wm_options_scene_activate(
        scene, WM_OPTIONS_CONTROL_SETTINGS_ITEM_3));
    wm_options_scene_advance(scene, 20.0f);
    assert(wm_options_scene_activate(
        scene, WM_OPTIONS_CONTROL_SETTINGS_ITEM_3));
    wm_options_scene_advance(scene, 20.0f);

    /* Source EULA_index.html: left Yes sets se 3; right No sets se 4. */
    assert(strcmp(wm_options_scene_click_cue(
        scene, WM_OPTIONS_CONTROL_BACK), "WIPL_SE_DECIDE") == 0);
    assert(strcmp(wm_options_scene_click_cue(
        scene, WM_OPTIONS_CONTROL_SETTINGS_NEXT), "WIPL_SE_CANCEL") == 0);
    assert(wm_options_scene_activate(
        scene, WM_OPTIONS_CONTROL_SETTINGS_NEXT));
    wm_options_scene_advance(scene, 20.0f);
    assert(wm_options_scene_activate(
        scene, WM_OPTIONS_CONTROL_SETTINGS_ITEM_3));
    wm_options_scene_advance(scene, 20.0f);
    assert(strcmp(wm_options_scene_click_cue(
        scene, WM_OPTIONS_CONTROL_BACK), "WIPL_SE_DECIDE") == 0);
    assert(wm_options_scene_activate(scene, WM_OPTIONS_CONTROL_BACK));
}

static void test_internet_connection_click_cues(WmOptionsScene *scene) {
    wm_options_scene_reset(scene);
    assert(wm_options_scene_open(scene));
    wm_options_scene_advance(scene, 32.0f);
    assert(wm_options_scene_activate(scene, WM_OPTIONS_CONTROL_SYSTEM));
    wm_options_scene_advance(scene, 40.0f);
    wm_options_scene_advance(scene, 21.0f);
    assert(wm_options_scene_activate(
        scene, WM_OPTIONS_CONTROL_SETTINGS_NEXT));
    wm_options_scene_advance(scene, 40.0f);
    assert(wm_options_scene_activate(
        scene, WM_OPTIONS_CONTROL_SETTINGS_ITEM_3));
    assert(wm_options_scene_activate(
        scene, WM_OPTIONS_CONTROL_SETTINGS_ITEM_1));

    /* Connect_set_top.html uses Decide on a slot and Cancel on Back. */
    assert(strcmp(wm_options_scene_click_cue(
        scene, WM_OPTIONS_CONTROL_SETTINGS_ITEM_1),
        "WIPL_SE_DECIDE") == 0);
    assert(strcmp(wm_options_scene_click_cue(
        scene, WM_OPTIONS_CONTROL_BACK), "WIPL_SE_CANCEL") == 0);
    assert(wm_options_scene_activate(
        scene, WM_OPTIONS_CONTROL_SETTINGS_ITEM_1));

    /* Connect_select.html uses the same cues for Wireless and Wired. */
    assert(strcmp(wm_options_scene_click_cue(
        scene, WM_OPTIONS_CONTROL_SETTINGS_ITEM_1),
        "WIPL_SE_DECIDE") == 0);
    assert(strcmp(wm_options_scene_click_cue(
        scene, WM_OPTIONS_CONTROL_SETTINGS_ITEM_2),
        "WIPL_SE_DECIDE") == 0);
    assert(strcmp(wm_options_scene_click_cue(
        scene, WM_OPTIONS_CONTROL_BACK), "WIPL_SE_CANCEL") == 0);
    assert(wm_options_scene_activate(
        scene, WM_OPTIONS_CONTROL_SETTINGS_ITEM_1));

    /* Wi_Fi_set_top.html uses Decide on Search and Cancel on Back. */
    assert(strcmp(wm_options_scene_click_cue(
        scene, WM_OPTIONS_CONTROL_SETTINGS_ITEM_1),
        "WIPL_SE_DECIDE") == 0);
    assert(wm_options_scene_activate(
        scene, WM_OPTIONS_CONTROL_SETTINGS_ITEM_1));
    wm_options_scene_advance(scene, 60.0f);
    /* Common0103.html's No Access Point OK uses source se 3. */
    assert(strcmp(wm_options_scene_click_cue(
        scene, WM_OPTIONS_CONTROL_SETTINGS_NEXT),
        "WIPL_SE_DECIDE") == 0);
    assert(wm_options_scene_activate(
        scene, WM_OPTIONS_CONTROL_SETTINGS_NEXT));

    /* Wi_Fi_set_top.html opens the USB Connector instructions with Decide. */
    assert(strcmp(wm_options_scene_click_cue(
        scene, WM_OPTIONS_CONTROL_SETTINGS_ITEM_2),
        "WIPL_SE_DECIDE") == 0);
    assert(wm_options_scene_activate(
        scene, WM_OPTIONS_CONTROL_SETTINGS_ITEM_2));
    assert(strcmp(wm_options_scene_click_cue(
        scene, WM_OPTIONS_CONTROL_BACK), "WIPL_SE_CANCEL") == 0);
    assert(strcmp(wm_options_scene_click_cue(
        scene, WM_OPTIONS_CONTROL_SETTINGS_NEXT),
        "WIPL_SE_DECIDE") == 0);
    assert(wm_options_scene_activate(
        scene, WM_OPTIONS_CONTROL_SETTINGS_NEXT));
    assert(strcmp(wm_options_scene_click_cue(
        scene, WM_OPTIONS_CONTROL_BACK), "WIPL_SE_CANCEL") == 0);
    wm_options_scene_advance(scene, 60.0f);

    /* Common0204.html uses Decide for Yes and Cancel for No. */
    assert(strcmp(wm_options_scene_click_cue(
        scene, WM_OPTIONS_CONTROL_BACK), "WIPL_SE_DECIDE") == 0);
    assert(strcmp(wm_options_scene_click_cue(
        scene, WM_OPTIONS_CONTROL_SETTINGS_NEXT),
        "WIPL_SE_CANCEL") == 0);
    assert(wm_options_scene_activate(
        scene, WM_OPTIONS_CONTROL_SETTINGS_NEXT));
    assert(strcmp(wm_options_scene_click_cue(
        scene, WM_OPTIONS_CONTROL_BACK), "WIPL_SE_CANCEL") == 0);
    assert(wm_options_scene_activate(scene, WM_OPTIONS_CONTROL_BACK));
    assert(strcmp(wm_options_scene_click_cue(
        scene, WM_OPTIONS_CONTROL_SETTINGS_ITEM_2),
        "WIPL_SE_DECIDE") == 0);
    assert(wm_options_scene_activate(
        scene, WM_OPTIONS_CONTROL_SETTINGS_ITEM_2));

    /* Common0101.html exposes one OK footer, assigned source se 3. */
    assert(strcmp(wm_options_scene_click_cue(
        scene, WM_OPTIONS_CONTROL_SETTINGS_NEXT),
        "WIPL_SE_DECIDE") == 0);
    assert(wm_options_scene_activate(
        scene, WM_OPTIONS_CONTROL_SETTINGS_NEXT));
}

static void test_sensor_position_click_cues(WmOptionsScene *scene) {
    wm_options_scene_reset(scene);
    assert(wm_options_scene_open(scene));
    wm_options_scene_advance(scene, 32.0f);
    assert(wm_options_scene_activate(scene, WM_OPTIONS_CONTROL_SYSTEM));
    wm_options_scene_advance(scene, 40.0f);
    wm_options_scene_advance(scene, 21.0f);
    assert(wm_options_scene_activate(
        scene, WM_OPTIONS_CONTROL_SETTINGS_NEXT));
    wm_options_scene_advance(scene, 40.0f);
    assert(wm_options_scene_activate(
        scene, WM_OPTIONS_CONTROL_SETTINGS_ITEM_2));
    assert(wm_options_scene_activate(
        scene, WM_OPTIONS_CONTROL_SETTINGS_ITEM_1));

    assert(strcmp(wm_options_scene_click_cue(
        scene, WM_OPTIONS_CONTROL_SETTINGS_ITEM_1),
        "WIPL_SE_CHOICE_CHG") == 0);
    assert(strcmp(wm_options_scene_click_cue(
        scene, WM_OPTIONS_CONTROL_SETTINGS_ITEM_2),
        "WIPL_SE_CHOICE_CHG") == 0);
    assert(strcmp(wm_options_scene_click_cue(
        scene, WM_OPTIONS_CONTROL_BACK), "WIPL_SE_CANCEL") == 0);
    assert(strcmp(wm_options_scene_click_cue(
        scene, WM_OPTIONS_CONTROL_SETTINGS_NEXT), "WIPL_SE_DECIDE") == 0);
}

static void test_screen_position_click_cues(WmOptionsScene *scene) {
    wm_options_scene_reset(scene);
    assert(wm_options_scene_open(scene));
    wm_options_scene_advance(scene, 32.0f);
    assert(wm_options_scene_activate(scene, WM_OPTIONS_CONTROL_SYSTEM));
    wm_options_scene_advance(scene, 40.0f);
    wm_options_scene_advance(scene, 21.0f);
    assert(wm_options_scene_activate(
        scene, WM_OPTIONS_CONTROL_SETTINGS_ITEM_3));
    assert(wm_options_scene_activate(
        scene, WM_OPTIONS_CONTROL_SETTINGS_ITEM_1));
    assert(strcmp(wm_options_scene_click_cue(
        scene, WM_OPTIONS_CONTROL_SETTINGS_ITEM_1),
        "WIPL_SE_CHOICE_CHG") == 0);
    assert(strcmp(wm_options_scene_click_cue(
        scene, WM_OPTIONS_CONTROL_SETTINGS_ITEM_2),
        "WIPL_SE_CHOICE_CHG") == 0);
    for (unsigned step = 0; step < 8; step++) {
        assert(wm_options_scene_activate(
            scene, WM_OPTIONS_CONTROL_SETTINGS_ITEM_1));
    }
    assert(strcmp(wm_options_scene_click_cue(
        scene, WM_OPTIONS_CONTROL_SETTINGS_ITEM_1),
        "WIPL_SE_CHAR_DELETE_ERROR") == 0);
    assert(strcmp(wm_options_scene_click_cue(
        scene, WM_OPTIONS_CONTROL_BACK), "WIPL_SE_CANCEL") == 0);
    assert(strcmp(wm_options_scene_click_cue(
        scene, WM_OPTIONS_CONTROL_SETTINGS_NEXT), "WIPL_SE_DECIDE") == 0);
}

static void test_sound_mode_click_cues(WmOptionsScene *scene) {
    wm_options_scene_reset(scene);
    assert(wm_options_scene_open(scene));
    wm_options_scene_advance(scene, 32.0f);
    assert(wm_options_scene_activate(scene, WM_OPTIONS_CONTROL_SYSTEM));
    wm_options_scene_advance(scene, 40.0f);
    wm_options_scene_advance(scene, 21.0f);
    assert(wm_options_scene_activate(
        scene, WM_OPTIONS_CONTROL_SETTINGS_ITEM_4));
    for (WmOptionsControl control = WM_OPTIONS_CONTROL_SETTINGS_ITEM_1;
         control <= WM_OPTIONS_CONTROL_SETTINGS_ITEM_3; control++) {
        assert(strcmp(wm_options_scene_click_cue(scene, control),
                      "WIPL_SE_OUTPUT_MODE_SELECT") == 0);
        assert(wm_options_scene_activate(scene, control));
    }
    assert(strcmp(wm_options_scene_click_cue(
        scene, WM_OPTIONS_CONTROL_BACK), "WIPL_SE_CANCEL") == 0);
    assert(strcmp(wm_options_scene_click_cue(
        scene, WM_OPTIONS_CONTROL_SETTINGS_NEXT), "WIPL_SE_DECIDE") == 0);
}

static void test_display_choice_click_cues(WmOptionsScene *scene) {
    wm_options_scene_reset(scene);
    assert(wm_options_scene_open(scene));
    wm_options_scene_advance(scene, 32.0f);
    assert(wm_options_scene_activate(scene, WM_OPTIONS_CONTROL_SYSTEM));
    wm_options_scene_advance(scene, 40.0f);
    wm_options_scene_advance(scene, 21.0f);
    assert(wm_options_scene_activate(
        scene, WM_OPTIONS_CONTROL_SETTINGS_ITEM_3));
    assert(wm_options_scene_activate(
        scene, WM_OPTIONS_CONTROL_SETTINGS_ITEM_2));
    assert(strcmp(wm_options_scene_click_cue(
        scene, WM_OPTIONS_CONTROL_SETTINGS_ITEM_1),
        "WIPL_SE_CHOICE_CHG") == 0);
    assert(strcmp(wm_options_scene_click_cue(
        scene, WM_OPTIONS_CONTROL_SETTINGS_ITEM_2),
        "WIPL_SE_CHOICE_CHG") == 0);
    assert(strcmp(wm_options_scene_click_cue(
        scene, WM_OPTIONS_CONTROL_BACK), "WIPL_SE_CANCEL") == 0);
    assert(strcmp(wm_options_scene_click_cue(
        scene, WM_OPTIONS_CONTROL_SETTINGS_NEXT), "WIPL_SE_DECIDE") == 0);
}

static void test_message_board_internet_route(WmOptionsScene *scene) {
    assert(wm_options_scene_open_internet(scene));
    WmOptionsSnapshot entry = wm_options_scene_snapshot(scene);
    assert(entry.page == WM_OPTIONS_PAGE_SYSTEM_SETTINGS);
    assert(entry.locked);
    wm_options_scene_advance(scene, 21.0f);
    assert(!wm_options_scene_snapshot(scene).locked);
    assert(wm_options_scene_back(scene));
    wm_options_scene_advance(scene, 1.0f);
    assert(wm_options_scene_take_action(scene) == WM_OPTIONS_ACTION_EXITED);
    assert(wm_options_scene_snapshot(scene).page ==
           WM_OPTIONS_PAGE_SYSTEM_SETTINGS);
    wm_options_scene_reset(scene);
    assert(wm_options_scene_open(scene));
    assert(wm_options_scene_snapshot(scene).page == WM_OPTIONS_PAGE_OPTIONS);
}

int main(int argc, char **argv) {
    const char *assets = argc > 1 ? argv[1] : ".local/native-assets";
    char source[4096];
    int length = snprintf(source, sizeof(source),
                          "%s/layouts/setupSel/it_ObjSetUp_a.json", assets);
    assert(length > 0 && length < (int)sizeof(source));
    FILE *check = fopen(source, "rb");
    if (!check) {
        puts("Wii Options source test skipped: local WAD export absent.");
        return 0;
    }
    fclose(check);
    WmPlatform *platform = (WmPlatform *)1;
    WmTextureCache *textures = wm_texture_cache_create(platform, assets,
                                                       128u * 1024u * 1024u);
    WmFontCache *fonts = wm_font_cache_create(platform, assets,
                                              16u * 1024u * 1024u);
    assert(textures && fonts);
    WmOptionsScene *scene = wm_options_scene_create(
        platform, assets, textures, fonts);
    assert(scene);
    test_options_hierarchy(scene);
    test_save_data_leaf_handoffs(scene);
    test_leaf_handoffs(scene);
    test_nickname_keyboard(scene);
    test_back_focus_during_selection(scene);
    test_entry_and_selection_samples(scene);
    test_update_question_cue_context(scene);
    test_user_agreements_click_cues(scene);
    test_internet_connection_click_cues(scene);
    test_sensor_position_click_cues(scene);
    test_screen_position_click_cues(scene);
    test_sound_mode_click_cues(scene);
    test_display_choice_click_cues(scene);
    test_message_board_internet_route(scene);
    wm_options_scene_destroy(scene);
    wm_texture_cache_destroy(textures);
    wm_font_cache_destroy(fonts);
    puts("Wii Options source transitions and controls passed.");
    return 0;
}
