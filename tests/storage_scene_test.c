#include "wii_menu/storage_scene.h"
#include "wii_menu/layout_runtime.h"
#include "wii_menu/menu.h"

#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <string.h>

typedef struct IconCapture {
    bool active;
    bool clipped;
    unsigned clip_entries;
    WmClipRect clips[2];
    float largest_quad_width;
    unsigned visible_quads[2];
    unsigned textured_visible_quads[2];
    bool detail_window_seen;
    float detail_window_center_x;
} IconCapture;

static IconCapture icon_capture;
static uint32_t next_texture = 1;
static bool capture_back_button;
static int capture_arrow_side;
static float captured_back_alpha;
static float captured_arrow_x;
static unsigned captured_arrow_count;
static bool probe_detail_window;
static bool probe_operation_dialog;
static bool probe_error_text;
static float visible_error_alpha;
static unsigned visible_error_glyphs;
static uint32_t operation_button_texture;
static unsigned visible_operation_buttons;
static unsigned visible_prompt_glyphs;

void wm_platform_begin(WmPlatform *platform, WmColor clear_color) {
    (void)platform;
    (void)clear_color;
    assert(false);
}

void wm_platform_end(WmPlatform *platform) {
    (void)platform;
    assert(false);
}

void wm_platform_set_clip(WmPlatform *platform, const WmClipRect *rect) {
    (void)platform;
    if (!icon_capture.active) return;
    icon_capture.clipped = rect != NULL;
    if (rect) {
        if (icon_capture.clip_entries < 2)
            icon_capture.clips[icon_capture.clip_entries] = *rect;
        icon_capture.clip_entries++;
    }
}

void wm_platform_prepare_material(WmPlatform *platform,
                                  const WmMaterialQuad *quad) {
    (void)platform;
    (void)quad;
}

void wm_platform_draw_quad(WmPlatform *platform, const WmQuad *quad) {
    (void)platform;
    (void)quad;
    assert(false);
}

void wm_platform_draw_vertices(WmPlatform *platform,
                               const WmDrawVertex vertices[4],
                               uint32_t texture) {
    (void)platform;
    (void)texture;
    if (probe_error_text && vertices[0].x > 50.0f &&
        vertices[0].x < 600.0f && vertices[0].y > 160.0f &&
        vertices[0].y < 280.0f) {
        if (vertices[0].color.a > visible_error_alpha)
            visible_error_alpha = vertices[0].color.a;
        if (vertices[0].color.a > 0.0f) visible_error_glyphs++;
    }
    if (probe_operation_dialog && vertices[0].color.a > 0.01f &&
        vertices[0].x >= 100.0f && vertices[0].x < 540.0f &&
        vertices[0].y >= 250.0f && vertices[0].y < 325.0f)
        visible_prompt_glyphs++;
}

void wm_platform_draw_material_quad(WmPlatform *platform,
                                    const WmMaterialQuad *quad) {
    (void)platform;
    if (probe_operation_dialog && quad->texture_count &&
        quad->textures[0] == operation_button_texture &&
        quad->vertices[0].color.a > 0.01f)
        visible_operation_buttons++;
    float width = fabsf(quad->vertices[1].x - quad->vertices[0].x);
    float height = fabsf(quad->vertices[2].y - quad->vertices[0].y);
    if (probe_detail_window && width > 150.0f && height > 70.0f &&
        fabsf(quad->vertices[0].color.a - 0.431f) < 0.005f) {
        icon_capture.detail_window_seen = true;
        icon_capture.detail_window_center_x =
            quad->vertices[0].x + width * 0.5f;
    }
    if (capture_back_button && width > 118.0f && width < 120.0f &&
        quad->vertices[0].y > 367.0f && quad->vertices[0].y < 369.0f) {
        captured_back_alpha = quad->vertices[0].color.a;
    }
    if (capture_arrow_side && width > 48.0f && width < 50.0f &&
        height > 63.0f && height < 65.0f &&
        quad->vertices[0].y > 170.0f && quad->vertices[0].y < 190.0f &&
        ((capture_arrow_side > 0 && quad->vertices[0].x > 320.0f) ||
         (capture_arrow_side < 0 && quad->vertices[0].x < 320.0f))) {
        captured_arrow_x = quad->vertices[0].x;
        captured_arrow_count++;
    }
    if (!icon_capture.active || !icon_capture.clipped) return;
    if (icon_capture.clip_entries >= 1 && icon_capture.clip_entries <= 2 &&
        quad->vertices[0].color.a > 0.0f) {
        icon_capture.visible_quads[icon_capture.clip_entries - 1]++;
        if (quad->texture_count > 0 && quad->textures[0] != 0)
            icon_capture.textured_visible_quads[icon_capture.clip_entries - 1]++;
    }
    if (width > icon_capture.largest_quad_width) {
        icon_capture.largest_quad_width = width;
    }
}

typedef struct DialogLayers {
    unsigned operation_buttons;
    unsigned prompt_glyphs;
} DialogLayers;

static DialogLayers sample_dialog_layers(WmStorageScene *scene,
                                         WmTextureCache *textures,
                                         WmFontCache *fonts,
                                         uint32_t button_texture) {
    operation_button_texture = button_texture;
    visible_operation_buttons = 0;
    visible_prompt_glyphs = 0;
    probe_operation_dialog = true;
    wm_texture_cache_begin_frame(textures);
    wm_font_cache_begin_frame(fonts);
    assert(wm_storage_scene_draw_content(scene));
    probe_operation_dialog = false;
    return (DialogLayers){visible_operation_buttons, visible_prompt_glyphs};
}

uint32_t wm_platform_create_texture(WmPlatform *platform, int width,
                                    int height, const uint8_t *rgba) {
    (void)platform;
    (void)width;
    (void)height;
    (void)rgba;
    return next_texture++;
}

void wm_platform_destroy_texture(WmPlatform *platform, uint32_t texture) {
    (void)platform;
    (void)texture;
}

static IconCapture capture_icons(WmStorageScene *scene,
                                 WmTextureCache *textures,
                                 WmFontCache *fonts) {
    icon_capture = (IconCapture){.active = true};
    wm_texture_cache_begin_frame(textures);
    wm_font_cache_begin_frame(fonts);
    assert(wm_storage_scene_draw(scene));
    IconCapture result = icon_capture;
    icon_capture.active = false;
    return result;
}

static float sample_error_alpha(WmStorageScene *scene,
                                WmTextureCache *textures,
                                WmFontCache *fonts) {
    visible_error_alpha = 0.0f;
    visible_error_glyphs = 0;
    probe_error_text = true;
    wm_texture_cache_begin_frame(textures);
    wm_font_cache_begin_frame(fonts);
    assert(wm_storage_scene_draw_content(scene));
    probe_error_text = false;
    return visible_error_alpha;
}

static void test_channel_icon_presentation(const char *assets) {
    WmPlatform *platform = (WmPlatform *)1;
    WmTextureCache *textures = wm_texture_cache_create(
        platform, assets, 32u * 1024u * 1024u);
    WmFontCache *fonts = wm_font_cache_create(
        platform, assets, 8u * 1024u * 1024u);
    assert(textures && fonts);
    WmStorageScene *scene = wm_storage_scene_create(
        platform, assets, textures, fonts, WM_STORAGE_CHANNELS);
    assert(scene);
    WmStorageRecord channel = {
        .id = "local-channel",
        .title = "Local Channel",
        .icon_layout = "layouts/chanSel/my_IplTop_b.json"
    };
    assert(wm_storage_scene_set_medium(scene, WM_STORAGE_WII,
                                       WM_STORAGE_READY, &channel, 1, 905));
    assert(wm_storage_scene_open(scene, WM_STORAGE_WII));
    wm_storage_scene_advance(scene, 68.0f);
    IconCapture idle = capture_icons(scene, textures, fonts);
    assert(idle.clip_entries == 1);
    assert(idle.largest_quad_width > 0.0f);
    assert(fabsf(idle.clips[0].width -
                 102.0f * WM_FRAME_WIDTH / 832.0f) < 0.01f);
    assert(fabsf(idle.clips[0].height - 57.6f) < 0.01f);

    assert(wm_storage_scene_hover(scene,
        (WmStorageHit){WM_STORAGE_CONTROL_SLOT, 0}));
    wm_storage_scene_advance(scene, 7.0f);
    IconCapture focused = capture_icons(scene, textures, fonts);
    assert(focused.clip_entries == 1);
    assert(fabsf(focused.clips[0].x - idle.clips[0].x) < 0.01f);
    assert(fabsf(focused.clips[0].y - idle.clips[0].y) < 0.01f);
    assert(fabsf(focused.clips[0].width - idle.clips[0].width) < 0.01f);
    assert(fabsf(focused.clips[0].height - idle.clips[0].height) < 0.01f);
    assert(focused.largest_quad_width > idle.largest_quad_width * 1.05f);

    assert(wm_storage_scene_hover(scene,
        (WmStorageHit){WM_STORAGE_CONTROL_NONE, -1}));
    wm_storage_scene_advance(scene, 7.0f);
    IconCapture released = capture_icons(scene, textures, fonts);
    assert(fabsf(released.largest_quad_width - idle.largest_quad_width) < 0.5f);

    assert(wm_storage_scene_activate(scene,
        (WmStorageHit){WM_STORAGE_CONTROL_SLOT, 0}));
    IconCapture beginning = capture_icons(scene, textures, fonts);
    assert(beginning.clip_entries == 1);
    float origin_x = idle.clips[0].x + idle.clips[0].width * 0.5f;
    static const int window_frames[] = {7, 10, 14};
    int previous_frame = 0;
    for (size_t index = 0;
         index < sizeof(window_frames) / sizeof(window_frames[0]); index++) {
        int frame = window_frames[index];
        wm_storage_scene_advance(scene, (float)(frame - previous_frame));
        probe_detail_window = true;
        IconCapture window = capture_icons(scene, textures, fonts);
        probe_detail_window = false;
        assert(window.detail_window_seen);
        float progress = fminf(1.0f, (float)(frame - 2) / 12.0f);
        float expected_x = WM_FRAME_WIDTH * 0.5f +
            (origin_x - WM_FRAME_WIDTH * 0.5f) * (1.0f - progress);
        assert(fabsf(window.detail_window_center_x - expected_x) < 0.5f);
        previous_frame = frame;
    }
    wm_storage_scene_advance(scene, 2.0f);
    IconCapture detail = capture_icons(scene, textures, fonts);
    assert(detail.clip_entries == 2);
    assert(detail.visible_quads[1] > 0);
    wm_storage_scene_advance(scene, 19.0f);
    IconCapture settled = capture_icons(scene, textures, fonts);
    assert(settled.clip_entries == 2);
    assert(settled.visible_quads[1] > 0);
    wm_storage_scene_destroy(scene);
    wm_font_cache_destroy(fonts);
    wm_texture_cache_destroy(textures);
}

static void test_native_channel_detail_icon(const char *assets) {
    /* The channel export is an ignored local input. Keep the synthetic
     * geometry test above mandatory even when this source is unavailable. */
    const char *icon_path =
        "channel-layouts/0001000148434c45/icon/icon.json";
    char full_path[4096];
    int length = snprintf(full_path, sizeof(full_path), "%s/%s",
                          assets, icon_path);
    assert(length > 0 && length < (int)sizeof(full_path));
    FILE *file = fopen(full_path, "rb");
    if (!file) return;
    fclose(file);

    WmPlatform *platform = (WmPlatform *)1;
    WmTextureCache *textures = wm_texture_cache_create(
        platform, assets, 32u * 1024u * 1024u);
    WmFontCache *fonts = wm_font_cache_create(
        platform, assets, 8u * 1024u * 1024u);
    assert(textures && fonts);
    WmStorageScene *scene = wm_storage_scene_create(
        platform, assets, textures, fonts, WM_STORAGE_CHANNELS);
    assert(scene);
    WmStorageRecord channel = {
        .id = "0001000148434c45",
        .title = "Netflix",
        .icon_layout = "channel-layouts/0001000148434c45/icon/icon.json"
    };
    assert(wm_storage_scene_set_medium(scene, WM_STORAGE_WII,
                                       WM_STORAGE_READY, &channel, 1, 905));
    assert(wm_storage_scene_open(scene, WM_STORAGE_WII));
    wm_storage_scene_advance(scene, 68.0f);
    IconCapture grid = capture_icons(scene, textures, fonts);
    assert(grid.clip_entries == 1);
    assert(grid.textured_visible_quads[0] > 0);
    assert(wm_storage_scene_activate(scene,
        (WmStorageHit){WM_STORAGE_CONTROL_SLOT, 0}));
    wm_storage_scene_advance(scene, 16.0f);
    IconCapture detail = capture_icons(scene, textures, fonts);
    assert(detail.clip_entries == 2);
    assert(detail.textured_visible_quads[1] > 0);
    wm_storage_scene_advance(scene, 19.0f);
    IconCapture settled = capture_icons(scene, textures, fonts);
    assert(settled.clip_entries == 2);
    assert(settled.textured_visible_quads[1] > 0);
    wm_storage_scene_destroy(scene);
    wm_font_cache_destroy(fonts);
    wm_texture_cache_destroy(textures);
}

static void test_manageable(void) {
    assert(!wm_storage_manageable_channel("disc", true));
    assert(!wm_storage_manageable_channel("0001000148434745", false));
    assert(wm_storage_manageable_channel("0001000148434745", true));
    assert(!wm_storage_manageable_channel("0001000248414241", true));
    assert(!wm_storage_manageable_channel("0000000148414241", true));
    assert(wm_storage_manageable_channel("local-channel", true));
}

static void test_wii_saves(const char *assets) {
    WmStorageScene *scene = wm_storage_scene_create(
        (WmPlatform *)1, assets, (WmTextureCache *)1,
        (WmFontCache *)1, WM_STORAGE_WII_SAVES);
    assert(scene);
    assert(wm_storage_scene_open(scene, WM_STORAGE_WII));
    assert(!wm_storage_scene_activate(scene,
        (WmStorageHit){WM_STORAGE_CONTROL_SLOT, 0}));
    wm_storage_scene_advance(scene, 68.0f);
    WmStorageSnapshot state = wm_storage_scene_snapshot(scene);
    assert(state.phase == WM_STORAGE_READY_PHASE);
    assert(state.record_count == 1);
    WmStorageHit first_slot = wm_storage_scene_hit(scene, 118, 120);
    assert(first_slot.control == WM_STORAGE_CONTROL_SLOT &&
           first_slot.slot == 0);
    assert(wm_storage_scene_hover(scene,
        (WmStorageHit){WM_STORAGE_CONTROL_SLOT, 0}));
    wm_storage_scene_advance(scene, 16.0f);
    assert(!wm_storage_scene_take_balloon_cue(scene));
    wm_storage_scene_advance(scene, 1.0f);
    assert(wm_storage_scene_take_balloon_cue(scene));
    assert(!wm_storage_scene_take_balloon_cue(scene));
    assert(wm_storage_scene_activate(scene,
        (WmStorageHit){WM_STORAGE_CONTROL_SLOT, 0}));
    wm_storage_scene_advance(scene, 36.0f);
    assert(wm_storage_scene_snapshot(scene).view == WM_STORAGE_VIEW_DETAIL);
    assert(wm_storage_scene_snapshot(scene).phase == WM_STORAGE_READY_PHASE);
    assert(wm_storage_scene_activate(scene,
        (WmStorageHit){WM_STORAGE_CONTROL_ERASE, -1}));
    wm_storage_scene_advance(scene, 19.0f + 46.0f + 26.0f);
    assert(wm_storage_scene_snapshot(scene).view == WM_STORAGE_VIEW_DIALOG);
    assert(wm_storage_scene_snapshot(scene).phase == WM_STORAGE_READY_PHASE);
    assert(wm_storage_scene_activate(scene,
        (WmStorageHit){WM_STORAGE_CONTROL_NO, -1}));
    wm_storage_scene_advance(scene, 21.0f + 26.0f + 46.0f);
    assert(wm_storage_scene_snapshot(scene).view == WM_STORAGE_VIEW_DETAIL);
    assert(wm_storage_scene_take_action(scene, NULL, NULL) ==
           WM_STORAGE_ACTION_NONE);
    assert(wm_storage_scene_activate(scene,
        (WmStorageHit){WM_STORAGE_CONTROL_COPY, -1}));
    wm_storage_scene_advance(scene, 19.0f + 46.0f + 26.0f);
    assert(wm_storage_scene_activate(scene,
        (WmStorageHit){WM_STORAGE_CONTROL_YES, -1}));
    wm_storage_scene_advance(scene, 21.0f + 26.0f);
    WmStorageOperation operation = WM_STORAGE_OPERATION_NONE;
    WmStorageRecord record = {0};
    assert(wm_storage_scene_take_action(scene, &operation, &record) ==
           WM_STORAGE_ACTION_CONFIRMED);
    assert(operation == WM_STORAGE_OPERATION_COPY);
    assert(strcmp(record.id, "dummy-save") == 0);
    wm_storage_scene_advance(scene, 46.0f);
    assert(wm_storage_scene_back(scene));
    wm_storage_scene_advance(scene, 19.0f + 11.0f);
    assert(wm_storage_scene_snapshot(scene).view == WM_STORAGE_VIEW_GRID);
    assert(wm_storage_scene_activate(scene,
        (WmStorageHit){WM_STORAGE_CONTROL_SD_TAB, -1}));
    wm_storage_scene_advance(scene, 22.0f + 26.0f);
    assert(wm_storage_scene_snapshot(scene).tab == WM_STORAGE_SD);
    assert(wm_storage_scene_snapshot(scene).record_count == 0);
    assert(wm_storage_scene_back(scene));
    wm_storage_scene_advance(scene, 19.0f + 26.0f);
    assert(wm_storage_scene_take_action(scene, NULL, NULL) ==
           WM_STORAGE_ACTION_EXITED);
    wm_storage_scene_destroy(scene);
}

static void test_channels(const char *assets) {
    WmStorageScene *scene = wm_storage_scene_create(
        (WmPlatform *)1, assets, (WmTextureCache *)1,
        (WmFontCache *)1, WM_STORAGE_CHANNELS);
    assert(scene);
    WmStorageRecord records[16] = {0};
    for (int index = 0; index < 16; index++) {
        snprintf(records[index].id, sizeof(records[index].id),
                 "local-channel-%d", index);
        snprintf(records[index].title, sizeof(records[index].title),
                 "Channel %d", index);
        snprintf(records[index].icon_layout,
                 sizeof(records[index].icon_layout),
                 "layouts/chanSel/my_IplTop_b.json");
        records[index].blocks = 1;
    }
    assert(wm_storage_scene_set_medium(scene, WM_STORAGE_WII,
                                        WM_STORAGE_READY, records, 16, 905));
    assert(wm_storage_scene_open(scene, WM_STORAGE_WII));
    wm_storage_scene_advance(scene, 68.0f);
    /* In 16:9, source column centers at -192 and +192 enter each box's
     * local N_All and are projected by that box's IPL root. */
    WmStorageHit left = wm_storage_scene_hit(scene, 118, 120);
    WmStorageHit right = wm_storage_scene_hit(scene, 522, 120);
    assert(left.control == WM_STORAGE_CONTROL_SLOT && left.slot == 0);
    assert(right.control == WM_STORAGE_CONTROL_SLOT && right.slot == 4);
    assert(wm_storage_scene_activate(scene,
        (WmStorageHit){WM_STORAGE_CONTROL_NEXT, -1}));
    wm_storage_scene_advance(scene, 20.0f);
    assert(wm_storage_scene_snapshot(scene).page == 1);
    assert(wm_storage_scene_activate(scene,
        (WmStorageHit){WM_STORAGE_CONTROL_PREVIOUS, -1}));
    wm_storage_scene_advance(scene, 20.0f);
    assert(wm_storage_scene_snapshot(scene).page == 0);
    wm_storage_scene_destroy(scene);
}

static void assert_unselected_tab_hover(WmStorageScene *scene,
                                        WmStorageControl control) {
    int left = WM_FRAME_WIDTH;
    int right = -1;
    int top = WM_FRAME_HEIGHT;
    int bottom = -1;
    for (int y = 0; y < WM_FRAME_HEIGHT; y += 4) {
        for (int x = 0; x < WM_FRAME_WIDTH; x += 8) {
            WmStorageHit hit = wm_storage_scene_hit(scene, x, y);
            if (hit.control != control) continue;
            if (x < left) left = x;
            if (x > right) right = x;
            if (y < top) top = y;
            if (y > bottom) bottom = y;
        }
    }
    assert(right > left && bottom > top);
    const int center_x = (left + right) / 2;
    const int center_y = (top + bottom) / 2;
    const int points[5][2] = {
        {center_x, center_y},
        {left + 8, center_y},
        {right - 8, center_y},
        {center_x, top + 4},
        {center_x, bottom - 4}
    };
    WmStorageHit tab = {control, -1};
    for (size_t point = 0; point < 5; point++) {
        assert(wm_storage_scene_hit(scene, points[point][0],
                                         points[point][1]).control == control);
        assert(wm_storage_scene_hover(scene, tab));
        for (int frame = 0; frame < 20; frame++) {
            wm_storage_scene_advance(scene, 1.0f);
            WmStorageHit hit = wm_storage_scene_hit(
                scene, points[point][0], points[point][1]);
            assert(hit.control == control);
            assert(!wm_storage_scene_hover(scene, hit));
        }
        assert(wm_storage_scene_hover(
            scene, (WmStorageHit){WM_STORAGE_CONTROL_NONE, -1}));
        wm_storage_scene_advance(scene, 8.0f);
    }
}

static void test_unselected_tab_hover(const char *assets) {
    WmStorageScene *scene = wm_storage_scene_create(
        (WmPlatform *)1, assets, (WmTextureCache *)1,
        (WmFontCache *)1, WM_STORAGE_CHANNELS);
    assert(scene);
    assert(wm_storage_scene_open(scene, WM_STORAGE_WII));
    wm_storage_scene_advance(scene, 68.0f);
    assert_unselected_tab_hover(scene, WM_STORAGE_CONTROL_SD_TAB);
    assert(wm_storage_scene_activate(scene,
        (WmStorageHit){WM_STORAGE_CONTROL_SD_TAB, -1}));
    wm_storage_scene_advance(scene, 48.0f);
    assert(wm_storage_scene_snapshot(scene).tab == WM_STORAGE_SD);
    assert(wm_storage_scene_snapshot(scene).phase == WM_STORAGE_READY_PHASE);
    assert_unselected_tab_hover(scene, WM_STORAGE_CONTROL_WII_TAB);
    wm_storage_scene_destroy(scene);
}

static void test_catalog_channel_population(const char *assets) {
    WmMenu menu;
    wm_menu_init(&menu);
    assert(wm_catalog_load(&menu, assets));
    WmStorageRecord records[WM_SLOT_COUNT] = {0};
    size_t count = 0;
    size_t manageable = 0;
    for (int slot = 1; slot < WM_SLOT_COUNT; slot++) {
        const WmChannel *channel = &menu.slots[slot];
        if (!channel->occupied || !channel->icon_layout[0]) continue;
        if (wm_storage_manageable_channel(channel->id, true)) manageable++;
        WmStorageRecord *record = &records[count++];
        snprintf(record->id, sizeof(record->id), "%s", channel->id);
        snprintf(record->title, sizeof(record->title), "%s",
                 channel->title);
        snprintf(record->icon_layout, sizeof(record->icon_layout),
                 "%s", channel->icon_layout);
        record->blocks = 1;
    }
    assert(manageable > 0);
    WmStorageScene *scene = wm_storage_scene_create(
        (WmPlatform *)1, assets, (WmTextureCache *)1,
        (WmFontCache *)1, WM_STORAGE_CHANNELS);
    assert(scene);
    assert(wm_storage_scene_set_medium(scene, WM_STORAGE_WII,
                                       WM_STORAGE_READY, records, count, 0));
    assert(wm_storage_scene_open(scene, WM_STORAGE_WII));
    wm_storage_scene_advance(scene, 68.0f);
    assert(wm_storage_scene_snapshot(scene).record_count == manageable);
    wm_storage_scene_destroy(scene);
}

static float sample_arrow_x(WmStorageScene *scene, WmTextureCache *textures,
                            WmFontCache *fonts, int side) {
    captured_arrow_x = NAN;
    captured_arrow_count = 0;
    capture_arrow_side = side;
    wm_texture_cache_begin_frame(textures);
    wm_font_cache_begin_frame(fonts);
    assert(wm_storage_scene_draw_content(scene));
    capture_arrow_side = 0;
    assert(isfinite(captured_arrow_x));
    assert(captured_arrow_count == 2);
    return captured_arrow_x;
}

static float sample_back_alpha(WmStorageScene *scene,
                               WmTextureCache *textures,
                               WmFontCache *fonts) {
    captured_back_alpha = 0.0f;
    capture_back_button = true;
    wm_texture_cache_begin_frame(textures);
    wm_font_cache_begin_frame(fonts);
    assert(wm_storage_scene_draw_back(scene));
    capture_back_button = false;
    return captured_back_alpha;
}

typedef struct SourceAlpha {
    float value;
} SourceAlpha;

static bool source_back_button(void *context, const WmLayoutPaneView *pane) {
    SourceAlpha *sample = context;
    if (strcmp(pane->name, "N_Button_00") == 0) {
        sample->value = pane->alpha;
    }
    return true;
}

static float source_back_alpha(WmLayout *source, const char *animation,
                               float frame) {
    char name[64];
    snprintf(name, sizeof(name), "it_Button_a_%s", animation);
    WmLayoutClip clip = {
        .animation = name,
        .group = "G_FocusBtnA",
        .frame = frame,
        .loop_override = 0
    };
    assert(wm_layout_pose(source, &clip, 1));
    SourceAlpha sample = {0};
    WmLayoutDrawOptions draw = {
        .wide = true,
        .mode = WM_LAYOUT_IPL,
        .alpha = 1.0f,
        .on_pane = source_back_button,
        .context = &sample
    };
    wm_layout_draw(source, &draw);
    return sample.value;
}

static void test_channel_arrow_exit(const char *assets) {
    WmPlatform *platform = (WmPlatform *)1;
    WmTextureCache *textures = wm_texture_cache_create(platform, assets,
                                                       32u * 1024u * 1024u);
    WmFontCache *fonts = wm_font_cache_create(platform, assets,
                                             8u * 1024u * 1024u);
    assert(textures && fonts);
    WmStorageScene *scene = wm_storage_scene_create(platform, assets, textures,
                                                    fonts, WM_STORAGE_CHANNELS);
    assert(scene);
    WmStorageRecord records[16] = {0};
    for (int i = 0; i < 16; i++) {
        snprintf(records[i].id, sizeof(records[i].id), "channel-%d", i);
        snprintf(records[i].title, sizeof(records[i].title), "Channel %d", i);
        snprintf(records[i].icon_layout, sizeof(records[i].icon_layout),
                 "layouts/chanSel/my_IplTop_b.json");
    }
    assert(wm_storage_scene_set_medium(scene, WM_STORAGE_WII,
                                       WM_STORAGE_READY, records, 16, 0));
    for (int side = 1; side >= -1; side -= 2) {
        assert(wm_storage_scene_open(scene, WM_STORAGE_WII));
        wm_storage_scene_advance(scene, 68.0f);
        if (side < 0) {
            assert(wm_storage_scene_activate(scene,
                (WmStorageHit){WM_STORAGE_CONTROL_NEXT, -1}));
            wm_storage_scene_advance(scene, 20.0f);
            assert(wm_storage_scene_snapshot(scene).page == 1);
        }
        float resting = sample_arrow_x(scene, textures, fonts, side);
        assert(wm_storage_scene_back(scene));
        wm_storage_scene_advance(scene, 19.0f);
        assert(wm_storage_scene_snapshot(scene).phase == WM_STORAGE_DATA_OUT);
        float leaving = sample_arrow_x(scene, textures, fonts, side);
        assert(fabsf(leaving - resting) < 1.0f);
        wm_storage_scene_advance(scene, 5.0f);
        float midpoint = sample_arrow_x(scene, textures, fonts, side);
        wm_storage_scene_advance(scene, 5.0f);
        float departed = sample_arrow_x(scene, textures, fonts, side);
        assert((midpoint - leaving) * (float)side > 20.0f);
        assert((departed - midpoint) * (float)side > 20.0f);
        assert(side > 0 ? departed > WM_FRAME_WIDTH : departed < 0.0f);
        wm_storage_scene_advance(scene, 16.0f);
        assert(wm_storage_scene_take_action(scene, NULL, NULL) ==
               WM_STORAGE_ACTION_EXITED);
    }
    wm_storage_scene_destroy(scene);
    wm_font_cache_destroy(fonts);
    wm_texture_cache_destroy(textures);
}

static void test_channel_back_dialog_fade(const char *assets) {
    WmPlatform *platform = (WmPlatform *)1;
    WmTextureCache *textures = wm_texture_cache_create(platform, assets,
                                                       32u * 1024u * 1024u);
    WmFontCache *fonts = wm_font_cache_create(platform, assets,
                                             8u * 1024u * 1024u);
    assert(textures && fonts);
    char path[4096];
    snprintf(path, sizeof(path), "%s/layouts/setupBtn/it_Button_a.json",
             assets);
    char error[160] = {0};
    WmLayout *source = wm_layout_load_json(path, error, sizeof(error));
    assert(source);
    WmStorageControl operations[] = {
        WM_STORAGE_CONTROL_MOVE,
        WM_STORAGE_CONTROL_COPY,
        WM_STORAGE_CONTROL_ERASE
    };
    for (size_t operation = 0;
         operation < sizeof(operations) / sizeof(operations[0]); operation++) {
        WmStorageScene *scene = wm_storage_scene_create(platform, assets,
                                                        textures, fonts,
                                                        WM_STORAGE_CHANNELS);
        assert(scene);
        WmStorageRecord record = {
            .id = "local-channel",
            .title = "Local Channel",
            .icon_layout = "layouts/chanSel/my_IplTop_b.json"
        };
        assert(wm_storage_scene_set_medium(scene, WM_STORAGE_WII,
                                           WM_STORAGE_READY, &record, 1, 0));
        assert(wm_storage_scene_open(scene, WM_STORAGE_WII));
        wm_storage_scene_advance(scene, 68.0f);
        assert(wm_storage_scene_activate(scene,
            (WmStorageHit){WM_STORAGE_CONTROL_SLOT, 0}));
        wm_storage_scene_advance(scene, 36.0f);
        assert(wm_storage_scene_activate(scene,
            (WmStorageHit){operations[operation], -1}));
        assert(sample_back_alpha(scene, textures, fonts) > 0.99f);
        wm_storage_scene_advance(scene, 19.0f + 46.0f);
        assert(wm_storage_scene_snapshot(scene).phase == WM_STORAGE_DIALOG_IN);
        for (int frame = 0; frame <= 11; frame++) {
            float actual = sample_back_alpha(scene, textures, fonts);
            float expected = source_back_alpha(source, "AlphOut",
                                                fminf((float)frame, 10.0f));
            assert(fabsf(actual - expected) < 0.02f);
            wm_storage_scene_advance(scene, 1.0f);
        }
        wm_storage_scene_advance(scene, 14.0f);
        assert(wm_storage_scene_snapshot(scene).view == WM_STORAGE_VIEW_DIALOG);
        assert(sample_back_alpha(scene, textures, fonts) == 0.0f);
        assert(wm_storage_scene_activate(scene,
            (WmStorageHit){WM_STORAGE_CONTROL_NO, -1}));
        assert(sample_back_alpha(scene, textures, fonts) == 0.0f);
        wm_storage_scene_advance(scene, 21.0f);
        assert(sample_back_alpha(scene, textures, fonts) == 0.0f);
        wm_storage_scene_advance(scene, 26.0f);
        assert(wm_storage_scene_snapshot(scene).phase ==
               WM_STORAGE_DETAIL_BUTTONS_IN);
        for (int frame = 0; frame <= 11; frame++) {
            float actual = sample_back_alpha(scene, textures, fonts);
            float expected = source_back_alpha(source, "AlphIn",
                                                fminf((float)frame, 10.0f));
            assert(fabsf(actual - expected) < 0.02f);
            wm_storage_scene_advance(scene, 1.0f);
        }
        wm_storage_scene_advance(scene, 34.0f);
        assert(wm_storage_scene_snapshot(scene).phase ==
               WM_STORAGE_READY_PHASE);
        assert(sample_back_alpha(scene, textures, fonts) > 0.99f);
        wm_storage_scene_destroy(scene);
    }
    wm_layout_destroy(source);
    wm_font_cache_destroy(fonts);
    wm_texture_cache_destroy(textures);
}

static void test_channel_operation_dialog_layers(const char *assets) {
    WmPlatform *platform = (WmPlatform *)1;
    WmTextureCache *textures = wm_texture_cache_create(platform, assets,
                                                       32u * 1024u * 1024u);
    WmFontCache *fonts = wm_font_cache_create(platform, assets,
                                             8u * 1024u * 1024u);
    assert(textures && fonts);
    uint32_t operation_art = 0;
    assert(wm_texture_cache_resolve(
        textures, "textures/chanEdit/it_Button_a.png", &operation_art));
    WmStorageControl operations[] = {
        WM_STORAGE_CONTROL_MOVE,
        WM_STORAGE_CONTROL_COPY,
        WM_STORAGE_CONTROL_ERASE
    };
    for (size_t operation = 0;
         operation < sizeof(operations) / sizeof(operations[0]); operation++) {
        WmStorageScene *scene = wm_storage_scene_create(platform, assets,
                                                        textures, fonts,
                                                        WM_STORAGE_CHANNELS);
        assert(scene);
        WmStorageRecord record = {
            .id = "local-channel",
            .title = "Local Channel",
            .icon_layout = "layouts/chanSel/my_IplTop_b.json"
        };
        assert(wm_storage_scene_set_medium(scene, WM_STORAGE_WII,
                                           WM_STORAGE_READY, &record, 1, 0));
        assert(wm_storage_scene_open(scene, WM_STORAGE_WII));
        wm_storage_scene_advance(scene, 68.0f);
        assert(wm_storage_scene_activate(scene,
            (WmStorageHit){WM_STORAGE_CONTROL_SLOT, 0}));
        wm_storage_scene_advance(scene, 36.0f);
        DialogLayers layers = sample_dialog_layers(scene, textures, fonts,
                                                   operation_art);
        assert(layers.operation_buttons == 3);
        assert(wm_storage_scene_activate(scene,
            (WmStorageHit){operations[operation], -1}));
        wm_storage_scene_advance(scene, 19.0f);
        assert(wm_storage_scene_snapshot(scene).phase ==
               WM_STORAGE_DETAIL_BUTTONS_OUT);
        layers = sample_dialog_layers(scene, textures, fonts, operation_art);
        assert(layers.operation_buttons == 3);
        wm_storage_scene_advance(scene, 10.0f);
        layers = sample_dialog_layers(scene, textures, fonts, operation_art);
        assert(layers.operation_buttons == 0);
        /* SelectOut hides N_Select by frame 10 and reveals T_Message_00
         * across frames 25–45 of the same WAD clip. */
        wm_storage_scene_advance(scene, 35.0f);
        layers = sample_dialog_layers(scene, textures, fonts, operation_art);
        assert(layers.operation_buttons == 0);
        assert(layers.prompt_glyphs > 0);
        wm_storage_scene_advance(scene, 1.0f);
        wm_storage_scene_advance(scene, 26.0f);
        assert(wm_storage_scene_snapshot(scene).view ==
               WM_STORAGE_VIEW_DIALOG);
        assert(wm_storage_scene_snapshot(scene).phase ==
               WM_STORAGE_READY_PHASE);
        layers = sample_dialog_layers(scene, textures, fonts, operation_art);
        assert(layers.operation_buttons == 0);
        assert(layers.prompt_glyphs > 0);
        assert(wm_storage_scene_activate(scene,
            (WmStorageHit){WM_STORAGE_CONTROL_NO, -1}));
        wm_storage_scene_advance(scene, 21.0f + 26.0f + 46.0f);
        assert(wm_storage_scene_snapshot(scene).view ==
               WM_STORAGE_VIEW_DETAIL);
        layers = sample_dialog_layers(scene, textures, fonts, operation_art);
        assert(layers.operation_buttons == 3);

        wm_storage_scene_destroy(scene);
    }
    wm_font_cache_destroy(fonts);
    wm_texture_cache_destroy(textures);
}

static void test_mixed_channel_catalog(const char *assets) {
    WmStorageScene *scene = wm_storage_scene_create(
        (WmPlatform *)1, assets, (WmTextureCache *)1,
        (WmFontCache *)1, WM_STORAGE_CHANNELS);
    assert(scene);
    WmStorageRecord records[] = {
        {
            .id = "0001000248414341", .title = "System Channel",
            .icon_layout = "layouts/chanSel/my_IplTop_b.json"
        },
        {
            .id = "0001000148434c45", .title = "Native B",
            .icon_layout = "layouts/chanSel/my_IplTop_b.json"
        },
        {
            .id = "local-channel", .title = "Local Channel",
            .icon_layout = "layouts/chanSel/my_IplTop_b.json"
        },
        {
            .id = "0001000148414445", .title = "Native A",
            .icon_layout = "layouts/chanSel/my_IplTop_b.json"
        },
        {.id = "disc", .title = "Disc Channel"}
    };
    assert(wm_storage_scene_set_medium(
        scene, WM_STORAGE_WII, WM_STORAGE_READY, records,
        sizeof(records) / sizeof(records[0]), 905));
    assert(wm_storage_scene_open(scene, WM_STORAGE_WII));
    wm_storage_scene_advance(scene, 68.0f);
    assert(wm_storage_scene_snapshot(scene).record_count == 3);
    assert(wm_storage_scene_activate(
        scene, (WmStorageHit){WM_STORAGE_CONTROL_SLOT, 0}));
    wm_storage_scene_advance(scene, 36.0f);
    assert(wm_storage_scene_activate(
        scene, (WmStorageHit){WM_STORAGE_CONTROL_COPY, -1}));
    wm_storage_scene_advance(scene, 19.0f + 46.0f + 26.0f);
    assert(wm_storage_scene_activate(
        scene, (WmStorageHit){WM_STORAGE_CONTROL_YES, -1}));
    wm_storage_scene_advance(scene, 21.0f + 26.0f);
    WmStorageRecord selected = {0};
    assert(wm_storage_scene_take_action(scene, NULL, &selected) ==
           WM_STORAGE_ACTION_CONFIRMED);
    assert(strcmp(selected.id, "0001000148414445") == 0);
    wm_storage_scene_destroy(scene);
}

static void test_gamecube_entry(const char *assets) {
    WmStorageScene *scene = wm_storage_scene_create(
        (WmPlatform *)1, assets, (WmTextureCache *)1,
        (WmFontCache *)1, WM_STORAGE_GAMECUBE_SAVES);
    assert(scene);
    WmStorageRecord card_record = {
        .id = "local-card-save",
        .title = "Local GameCube Save",
        .blocks = 2
    };
    assert(wm_storage_scene_set_medium(scene, WM_STORAGE_WII,
                                        WM_STORAGE_READY, &card_record, 1, 0));
    assert(wm_storage_scene_open(scene, WM_STORAGE_WII));
    wm_storage_scene_advance(scene, 26.0f);
    assert(wm_storage_scene_snapshot(scene).phase == WM_STORAGE_TABS_IN);
    assert(wm_storage_scene_snapshot(scene).phase_duration == 26.0f);
    wm_storage_scene_advance(scene, 26.0f);
    assert(wm_storage_scene_snapshot(scene).phase == WM_STORAGE_READY_PHASE);
    assert(wm_storage_scene_snapshot(scene).record_count == 1);
    WmStorageHit first_slot = wm_storage_scene_hit(scene, 118, 120);
    assert(first_slot.control == WM_STORAGE_CONTROL_SLOT &&
           first_slot.slot == 0);
    assert(wm_storage_scene_activate(scene, first_slot));
    wm_storage_scene_advance(scene, 36.0f);
    assert(wm_storage_scene_snapshot(scene).view == WM_STORAGE_VIEW_DETAIL);
    assert(wm_storage_scene_activate(scene,
        (WmStorageHit){WM_STORAGE_CONTROL_MOVE, -1}));
    wm_storage_scene_advance(scene, 19.0f + 46.0f + 26.0f);
    assert(wm_storage_scene_snapshot(scene).view == WM_STORAGE_VIEW_DIALOG);
    assert(wm_storage_scene_activate(scene,
        (WmStorageHit){WM_STORAGE_CONTROL_YES, -1}));
    wm_storage_scene_advance(scene, 21.0f + 26.0f);
    WmStorageOperation operation = WM_STORAGE_OPERATION_NONE;
    WmStorageRecord selected = {0};
    assert(wm_storage_scene_take_action(scene, &operation, &selected) ==
           WM_STORAGE_ACTION_CONFIRMED);
    assert(operation == WM_STORAGE_OPERATION_MOVE);
    assert(strcmp(selected.id, card_record.id) == 0);
    wm_storage_scene_advance(scene, 46.0f);
    assert(wm_storage_scene_back(scene));
    wm_storage_scene_advance(scene, 19.0f + 11.0f);
    assert(wm_storage_scene_snapshot(scene).view == WM_STORAGE_VIEW_GRID);
    assert_unselected_tab_hover(scene, WM_STORAGE_CONTROL_SD_TAB);
    assert(wm_storage_scene_activate(scene,
        (WmStorageHit){WM_STORAGE_CONTROL_SD_TAB, -1}));
    wm_storage_scene_advance(scene, 22.0f);
    assert(wm_storage_scene_snapshot(scene).medium_status == WM_STORAGE_ABSENT);
    assert(wm_storage_scene_snapshot(scene).phase == WM_STORAGE_ERROR_IN);
    assert(wm_storage_scene_snapshot(scene).phase_duration == 16.0f);
    wm_storage_scene_advance(scene, 16.0f);
    assert(wm_storage_scene_snapshot(scene).phase == WM_STORAGE_READY_PHASE);
    assert_unselected_tab_hover(scene, WM_STORAGE_CONTROL_WII_TAB);
    assert(wm_storage_scene_back(scene));
    wm_storage_scene_advance(scene, 19.0f);
    assert(wm_storage_scene_snapshot(scene).phase == WM_STORAGE_DATA_OUT);
    wm_storage_scene_advance(scene, 26.0f);
    assert(wm_storage_scene_snapshot(scene).phase == WM_STORAGE_CLOSED);
    assert(wm_storage_scene_open(scene, WM_STORAGE_SD));
    wm_storage_scene_advance(scene, 42.0f);
    assert(wm_storage_scene_snapshot(scene).phase == WM_STORAGE_ERROR_IN);
    wm_storage_scene_advance(scene, 16.0f);
    assert(wm_storage_scene_snapshot(scene).phase == WM_STORAGE_READY_PHASE);
    wm_storage_scene_destroy(scene);
}

static void test_gamecube_error_fade(const char *assets) {
    WmPlatform *platform = (WmPlatform *)1;
    WmTextureCache *textures = wm_texture_cache_create(
        platform, assets, 32u * 1024u * 1024u);
    WmFontCache *fonts = wm_font_cache_create(
        platform, assets, 8u * 1024u * 1024u);
    assert(textures && fonts);
    WmStorageScene *scene = wm_storage_scene_create(
        platform, assets, textures, fonts, WM_STORAGE_GAMECUBE_SAVES);
    assert(scene && wm_storage_scene_open(scene, WM_STORAGE_SD));
    wm_storage_scene_advance(scene, 42.0f);
    assert(wm_storage_scene_snapshot(scene).phase == WM_STORAGE_ERROR_IN);
    assert(sample_error_alpha(scene, textures, fonts) == 0.0f);
    wm_storage_scene_advance(scene, 8.0f);
    float middle = sample_error_alpha(scene, textures, fonts);
    assert(middle > 0.0f && middle < 1.0f);
    wm_storage_scene_advance(scene, 8.0f);
    assert(wm_storage_scene_snapshot(scene).phase == WM_STORAGE_READY_PHASE);
    assert(sample_error_alpha(scene, textures, fonts) > 0.99f);
    assert(wm_storage_scene_back(scene));
    wm_storage_scene_advance(scene, 19.0f);
    assert(wm_storage_scene_snapshot(scene).phase == WM_STORAGE_DATA_OUT);
    assert(sample_error_alpha(scene, textures, fonts) > 0.99f);
    wm_storage_scene_advance(scene, 8.0f);
    middle = sample_error_alpha(scene, textures, fonts);
    assert(middle > 0.0f && middle < 1.0f);
    wm_storage_scene_advance(scene, 8.0f);
    assert(sample_error_alpha(scene, textures, fonts) == 0.0f);
    wm_storage_scene_destroy(scene);
    wm_font_cache_destroy(fonts);
    wm_texture_cache_destroy(textures);
}

int main(int argc, char **argv) {
    test_manageable();
    const char *assets = argc > 1 ? argv[1] : ".local/native-assets";
    char path[4096];
    int length = snprintf(path, sizeof(path),
                          "%s/layouts/chanEdit/it_ObjChannelEdit_a.json",
                          assets);
    assert(length > 0 && length < (int)sizeof(path));
    FILE *check = fopen(path, "rb");
    if (!check) {
        puts("Storage scene source test skipped: local WAD export absent.");
        return 0;
    }
    fclose(check);
    test_wii_saves(assets);
    test_channels(assets);
    test_unselected_tab_hover(assets);
    test_catalog_channel_population(assets);
    test_channel_arrow_exit(assets);
    test_channel_back_dialog_fade(assets);
    test_channel_operation_dialog_layers(assets);
    test_channel_icon_presentation(assets);
    test_native_channel_detail_icon(assets);
    test_mixed_channel_catalog(assets);
    test_gamecube_entry(assets);
    test_gamecube_error_fade(assets);
    puts("Data Management source navigation and dialogs passed.");
    return 0;
}
