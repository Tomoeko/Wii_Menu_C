#define _POSIX_C_SOURCE 200809L
#if defined(__APPLE__)
#define _DARWIN_C_SOURCE 1
#endif

#include "wii_menu/board_keyboard.h"
#include "wii_menu/layout_runtime.h"
#include "wii_menu/source_hit.h"

#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

static uint32_t next_texture = 1;
static WmSourceRect language_button;
static WmSourceRect language_hit;
static WmSourceRect prediction_off_icon;
static WmSourceRect language_label;
static WmSourceRect candidate_previous_hit;
static WmSourceRect candidate_next_hit;
static WmSourceRect candidate_area;
static WmSourceRect candidate_window;
static WmSourceRect candidate_next_picture;
static WmClipRect candidate_clip;
static unsigned clip_sets;
static unsigned candidate_next_normal_draws;
static unsigned button_draws;
static unsigned off_icon_draws;
static unsigned label_glyphs;
static float maximum_quad_alpha;
static double material_geometry_sum;
static unsigned selected_text_after_keytop;
static float selected_text_min_x;
static float normal_text_min_x;
static bool keytop_seen;
static WmSourceRect keytop_picture;

static bool contains_center(const WmSourceRect *rect, float x, float y) {
    return x >= rect->x && x <= rect->x + rect->width &&
           y >= rect->y && y <= rect->y + rect->height;
}

static bool matches_rect(const WmMaterialQuad *quad,
                         const WmSourceRect *rect) {
    float left = INFINITY;
    float right = -INFINITY;
    float top = INFINITY;
    float bottom = -INFINITY;
    for (unsigned index = 0; index < 4; index++) {
        left = fminf(left, quad->vertices[index].x);
        right = fmaxf(right, quad->vertices[index].x);
        top = fminf(top, quad->vertices[index].y);
        bottom = fmaxf(bottom, quad->vertices[index].y);
    }
    return fabsf(left - rect->x) < 0.02f &&
           fabsf(top - rect->y) < 0.02f &&
           fabsf(right - left - rect->width) < 0.02f &&
           fabsf(bottom - top - rect->height) < 0.02f;
}

void wm_platform_begin(WmPlatform *platform, WmColor clear_color) {
    (void)platform;
    (void)clear_color;
}

void wm_platform_end(WmPlatform *platform) {
    (void)platform;
}

void wm_platform_set_clip(WmPlatform *platform, const WmClipRect *clip) {
    (void)platform;
    if (clip) {
        candidate_clip = *clip;
        clip_sets++;
    }
}

void wm_platform_draw_quad(WmPlatform *platform, const WmQuad *quad) {
    (void)platform;
    (void)quad;
}

void wm_platform_draw_vertices(WmPlatform *platform,
                               const WmDrawVertex vertices[4],
                               uint32_t texture) {
    (void)platform;
    if (texture == 0) return;
    float x = (vertices[0].x + vertices[3].x) * 0.5f;
    float y = (vertices[0].y + vertices[3].y) * 0.5f;
    if (contains_center(&language_label, x, y)) label_glyphs++;
    if (!keytop_seen && contains_center(&candidate_area, x, y)) {
        for (unsigned index = 0; index < 4; index++)
            normal_text_min_x = fminf(normal_text_min_x,
                                      vertices[index].x);
    }
    if (keytop_seen) {
        selected_text_after_keytop++;
        for (unsigned index = 0; index < 4; index++)
            selected_text_min_x = fminf(selected_text_min_x,
                                         vertices[index].x);
    }
}

void wm_platform_draw_material_quad(WmPlatform *platform,
                                    const WmMaterialQuad *quad) {
    (void)platform;
    for (unsigned index = 0; index < 4; index++) {
        maximum_quad_alpha = fmaxf(maximum_quad_alpha,
                                    quad->vertices[index].color.a);
        material_geometry_sum +=
            (double)quad->vertices[index].x * (index + 1) +
            (double)quad->vertices[index].y * (index + 3) +
            (double)quad->vertices[index].color.a * (index + 5);
    }
    if (matches_rect(quad, &keytop_picture)) keytop_seen = true;
    if (matches_rect(quad, &language_button)) button_draws++;
    if (matches_rect(quad, &prediction_off_icon)) off_icon_draws++;
    if (matches_rect(quad, &candidate_next_picture))
        candidate_next_normal_draws++;
}

void wm_platform_prepare_material(WmPlatform *platform,
                                  const WmMaterialQuad *quad) {
    (void)platform;
    (void)quad;
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

static void reset_draw_counts(void) {
    button_draws = 0;
    off_icon_draws = 0;
    label_glyphs = 0;
    maximum_quad_alpha = 0.0f;
    candidate_next_normal_draws = 0;
    material_geometry_sum = 0.0;
    selected_text_after_keytop = 0;
    selected_text_min_x = INFINITY;
    normal_text_min_x = INFINITY;
    keytop_seen = false;
}

static void test_prepared_oem_runtime(const char *assets) {
    char *absolute_assets = realpath(assets, NULL);
    assert(absolute_assets);
    char temporary[] = "/tmp/wm-keyboard-runtime-XXXXXX";
    assert(mkdtemp(temporary));
    static const char *const shared[] = {"layouts", "textures", "fonts"};
    for (unsigned index = 0; index < 3; index++) {
        char source[4096], destination[4096];
        assert(snprintf(source, sizeof(source), "%s/%s", absolute_assets,
                        shared[index]) < (int)sizeof(source));
        assert(snprintf(destination, sizeof(destination), "%s/%s", temporary,
                        shared[index]) < (int)sizeof(destination));
        assert(symlink(source, destination) == 0);
    }
    char dictionary[4096], word_path[4096];
    assert(snprintf(dictionary, sizeof(dictionary),
                    "%s/keyboard-dictionary", temporary) <
           (int)sizeof(dictionary));
    assert(mkdir(dictionary, 0700) == 0);
    assert(snprintf(word_path, sizeof(word_path),
                    "%s/eZTNintendoENAM.znd", dictionary) <
           (int)sizeof(word_path));
    uint8_t words[32] = {0};
    words[3] = 1;
    words[7] = 8;
    static const char oem_word[] = "tessellate";
    for (size_t index = 0; index < sizeof(oem_word) - 1; index++)
        words[8 + index * 2 + 1] = (uint8_t)oem_word[index];
    FILE *file = fopen(word_path, "wb");
    assert(file);
    assert(fwrite(words, 1, 8 + sizeof(oem_word) * 2, file) ==
           8 + sizeof(oem_word) * 2);
    assert(fclose(file) == 0);

    WmTextureCache *textures = wm_texture_cache_create(
        (WmPlatform *)1, temporary, 64u * 1024u * 1024u);
    WmFontCache *fonts = wm_font_cache_create(
        (WmPlatform *)1, temporary, 8u * 1024u * 1024u);
    assert(textures && fonts);
    WmBoardKeyboard *keyboard = wm_board_keyboard_create(
        (WmPlatform *)1, temporary, textures, fonts);
    assert(keyboard);
    char context[64] = "";
    char character[5];
    wm_board_keyboard_set_text_context(keyboard, context);
    assert(wm_board_keyboard_activate(keyboard, WM_KEYBOARD_PREDICTION,
        false, character) == WM_KEYBOARD_ACTION_PREDICTION_TOGGLE);
    strcpy(context, "tes");
    wm_board_keyboard_text_changed(keyboard, false);
    WmBoardKeyboardComposition composition;
    assert(wm_board_keyboard_composition(keyboard, &composition));
    assert(strcmp(composition.preview_candidate, "tessellate") == 0);
    strcpy(context, "zebracorn ");
    wm_board_keyboard_set_text_context(keyboard, context);
    strcat(context, "zeb");
    wm_board_keyboard_text_changed(keyboard, false);
    assert(wm_board_keyboard_composition(keyboard, &composition));
    assert(strcmp(composition.preview_candidate, "zebracorn") == 0);
    wm_board_keyboard_finish_composition(keyboard);
    assert(!wm_board_keyboard_composition(keyboard, &composition));
    strcat(context, "x");
    wm_board_keyboard_text_changed(keyboard, false);
    assert(wm_board_keyboard_composition(keyboard, &composition));
    assert(composition.prefix_bytes == 1);
    assert(strcmp(composition.selected_candidate, "x") == 0);
    wm_board_keyboard_advance(keyboard, 12.0f);
    assert(wm_board_keyboard_activate(keyboard, WM_KEYBOARD_PREDICTION,
        false, character) == WM_KEYBOARD_ACTION_PREDICTION_TOGGLE);
    wm_board_keyboard_advance(keyboard, 12.0f);
    strcpy(context, "already");
    wm_board_keyboard_set_text_context(keyboard, context);
    assert(wm_board_keyboard_activate(keyboard, WM_KEYBOARD_PREDICTION,
        false, character) == WM_KEYBOARD_ACTION_PREDICTION_TOGGLE);
    assert(!wm_board_keyboard_composition(keyboard, &composition));
    strcat(context, "t");
    wm_board_keyboard_text_changed(keyboard, false);
    assert(wm_board_keyboard_composition(keyboard, &composition));
    assert(composition.prefix_bytes == 1);
    wm_board_keyboard_destroy(keyboard);
    wm_font_cache_destroy(fonts);
    wm_texture_cache_destroy(textures);
    assert(unlink(word_path) == 0);
    assert(rmdir(dictionary) == 0);
    for (unsigned index = 0; index < 3; index++) {
        char destination[4096];
        assert(snprintf(destination, sizeof(destination), "%s/%s",
                        temporary, shared[index]) < (int)sizeof(destination));
        assert(unlink(destination) == 0);
    }
    assert(rmdir(temporary) == 0);
    free(absolute_assets);
}

static void test_candidate_corner(const char *assets,
                                  WmTextureCache *textures,
                                  WmFontCache *fonts) {
    WmBoardKeyboard *keyboard = wm_board_keyboard_create(
        (WmPlatform *)1, assets, textures, fonts);
    assert(keyboard);
    char context[32] = "";
    char character[5];
    wm_board_keyboard_set_text_context(keyboard, context);
    assert(wm_board_keyboard_activate(keyboard, WM_KEYBOARD_PREDICTION,
        false, character) == WM_KEYBOARD_ACTION_PREDICTION_TOGGLE);
    wm_board_keyboard_advance(keyboard, 12.0f);
    strcpy(context, "t");
    wm_board_keyboard_text_changed(keyboard, false);
    reset_draw_counts();
    wm_board_keyboard_draw(keyboard, 1.0f, false);
    float resting_left = normal_text_min_x;
    assert(isfinite(resting_left));

    wm_board_keyboard_hover(keyboard, WM_KEYBOARD_CANDIDATE_FIRST);
    wm_board_keyboard_advance(keyboard, 6.0f);
    reset_draw_counts();
    wm_board_keyboard_draw(keyboard, 1.0f, false);
    assert(selected_text_after_keytop > 0);
    assert(selected_text_min_x < resting_left - 4.5f);
    assert(selected_text_min_x >= candidate_clip.x);

    wm_board_keyboard_hover(keyboard, WM_KEYBOARD_NONE);
    wm_board_keyboard_advance(keyboard, 6.0f);
    strcpy(context, "th");
    wm_board_keyboard_text_changed(keyboard, false);
    reset_draw_counts();
    wm_board_keyboard_draw(keyboard, 1.0f, false);
    assert(normal_text_min_x < candidate_area.x);
    assert(candidate_clip.x <= normal_text_min_x);
    assert(candidate_clip.x >= candidate_window.x - 1.1f);

    wm_board_keyboard_hover(keyboard, WM_KEYBOARD_NONE);
    wm_board_keyboard_advance(keyboard, 20.0f);
    strcpy(context, "gibberishnumber42");
    wm_board_keyboard_text_changed(keyboard, false);
    wm_board_keyboard_hover(keyboard, WM_KEYBOARD_CANDIDATE_FIRST);
    wm_board_keyboard_advance(keyboard, 6.0f);
    reset_draw_counts();
    wm_board_keyboard_draw(keyboard, 1.0f, false);
    assert(selected_text_after_keytop > 0);
    assert(selected_text_min_x < candidate_window.x - 1.0f);
    assert(candidate_clip.x == -1.0f);
    assert(candidate_clip.x < selected_text_min_x);
    wm_board_keyboard_destroy(keyboard);
}

int main(int argc, char **argv) {
    const char *assets = argc > 1 ? argv[1] : ".local/native-assets";
    char path[4096];
    int length = snprintf(path, sizeof(path),
                          "%s/layouts/sofkeybd/fs_VK_cellPhone_a.json",
                          assets);
    assert(length > 0 && length < (int)sizeof(path));
    FILE *check = fopen(path, "rb");
    if (!check) {
        puts("Phone keyboard artwork test skipped: local WAD export absent.");
        return 0;
    }
    fclose(check);

    char error[160] = {0};
    WmLayout *source = wm_layout_load_json(path, error, sizeof(error));
    assert(source);
    WmLayoutClip normal = {
        .animation = "fs_VK_cellPhone_a_normal",
        .frame = 0.0f
    };
    assert(wm_layout_pose(source, &normal, 1));
    assert(wm_source_pane_rect(source, "W_prdcModeBT_EU", true,
                               WM_LAYOUT_IPL, NULL, &language_button));
    assert(wm_source_pane_rect(source, "B_prdcModeBT_EU", true,
                               WM_LAYOUT_IPL, NULL, &language_hit));
    assert(wm_source_pane_rect(source, "P_prdc_EU_OFF", true,
                               WM_LAYOUT_IPL, NULL, &prediction_off_icon));
    assert(wm_source_pane_rect(source, "N_prdc_EU_lang", true,
                               WM_LAYOUT_IPL, NULL, &language_label));
    wm_layout_destroy(source);

    length = snprintf(path, sizeof(path),
                      "%s/layouts/sofkeybd/fs_VK_predictInput_a.json",
                      assets);
    assert(length > 0 && length < (int)sizeof(path));
    source = wm_layout_load_json(path, error, sizeof(error));
    assert(source);
    WmLayoutClip prediction_normal = {
        .animation = "fs_VK_predictInput_a_normal",
        .frame = 1.0f
    };
    assert(wm_layout_pose(source, &prediction_normal, 1));
    assert(wm_source_pane_rect(source, "B_prdc_scrl_Left", true,
        WM_LAYOUT_IPL, NULL, &candidate_previous_hit));
    assert(wm_source_pane_rect(source, "B_prdc_scrl_Rght", true,
        WM_LAYOUT_IPL, NULL, &candidate_next_hit));
    assert(wm_source_pane_rect(source, "N_prdcTextArea", true,
        WM_LAYOUT_IPL, NULL, &candidate_area));
    assert(wm_source_pane_rect(source, "W_predictWindow", true,
        WM_LAYOUT_IPL, NULL, &candidate_window));
    assert(wm_source_pane_rect(source, "P_prdc_scrl_Rght", true,
        WM_LAYOUT_IPL, NULL, &candidate_next_picture));
    wm_layout_destroy(source);

    length = snprintf(path, sizeof(path),
                      "%s/layouts/sofkeybd/fs_VK_ascii_keytop_a.json",
                      assets);
    assert(length > 0 && length < (int)sizeof(path));
    source = wm_layout_load_json(path, error, sizeof(error));
    assert(source);
    WmLayoutClip keytop_normal = {
        .animation = "fs_VK_ascii_keytop_a_normal", .frame = 0.0f
    };
    assert(wm_layout_pose(source, &keytop_normal, 1));
    assert(wm_source_pane_rect(source, "P_key_00", true,
        WM_LAYOUT_IPL, NULL, &keytop_picture));
    wm_layout_destroy(source);

    WmTextureCache *textures = wm_texture_cache_create(
        (WmPlatform *)1, assets, 64u * 1024u * 1024u);
    WmFontCache *fonts = wm_font_cache_create(
        (WmPlatform *)1, assets, 8u * 1024u * 1024u);
    assert(textures && fonts);
    WmCachedFont *source_font = wm_font_cache_resolve(fonts,
        "RevoIpl_RodinNTLGPro_DB_32_I4.brfnt");
    assert(source_font);
    const WmFont *glyphs = wm_cached_font_resource(source_font);
    assert(wm_font_glyph(glyphs, 0x2423u) ==
           wm_font_glyph(glyphs, 0xe057u));
    assert(wm_font_glyph(glyphs, 0x2423u) !=
           wm_font_glyph(glyphs, ' '));
    WmBoardKeyboard *keyboard = wm_board_keyboard_create(
        (WmPlatform *)1, assets, textures, fonts);
    assert(keyboard);

    reset_draw_counts();
    wm_board_keyboard_draw(keyboard, 1.0f, false);
    double normal_geometry = material_geometry_sum;
    assert(wm_board_keyboard_press_physical(keyboard, "a") ==
           (WmBoardKeyboardControl)(WM_KEYBOARD_CHARACTER_FIRST + 21));
    wm_board_keyboard_advance(keyboard, 3.0f);
    reset_draw_counts();
    wm_board_keyboard_draw(keyboard, 1.0f, false);
    assert(fabs(material_geometry_sum - normal_geometry) > 0.01);
    wm_board_keyboard_advance(keyboard, 20.0f);
    reset_draw_counts();
    wm_board_keyboard_draw(keyboard, 1.0f, false);
    assert(fabs(material_geometry_sum - normal_geometry) < 0.01);
    assert(wm_board_keyboard_press_physical(keyboard, "A") ==
           (WmBoardKeyboardControl)(WM_KEYBOARD_CHARACTER_FIRST + 21));
    assert(wm_board_keyboard_press_physical(keyboard, "\b") ==
           WM_KEYBOARD_DELETE);
    assert(wm_board_keyboard_press_physical(keyboard, "\n") ==
           WM_KEYBOARD_RETURN);

    char character[5];
    assert(wm_board_keyboard_activate(keyboard, WM_KEYBOARD_PHONE, false,
                                       character) ==
           WM_KEYBOARD_ACTION_LAYOUT_PHONE);
    wm_board_keyboard_advance(keyboard, 20.0f);
    reset_draw_counts();
    wm_board_keyboard_draw(keyboard, 1.0f, false);
    double neutral_phone_geometry = material_geometry_sum;
    wm_board_keyboard_hover(keyboard, WM_KEYBOARD_QWERTY);
    wm_board_keyboard_advance(keyboard, 6.0f);
    assert(wm_board_keyboard_activate(keyboard, WM_KEYBOARD_QWERTY,
        false, character) == WM_KEYBOARD_ACTION_LAYOUT_QWERTY);
    wm_board_keyboard_advance(keyboard, 20.0f);
    assert(wm_board_keyboard_activate(keyboard, WM_KEYBOARD_PHONE,
        false, character) == WM_KEYBOARD_ACTION_LAYOUT_PHONE);
    wm_board_keyboard_advance(keyboard, 20.0f);
    reset_draw_counts();
    wm_board_keyboard_draw(keyboard, 1.0f, false);
    assert(fabs(material_geometry_sum - neutral_phone_geometry) < 0.01);
    assert(wm_board_keyboard_press_physical(keyboard, "a") ==
           (WmBoardKeyboardControl)(WM_KEYBOARD_PHONE_FIRST + 1));
    assert(wm_board_keyboard_press_physical(keyboard, " ") ==
           (WmBoardKeyboardControl)(WM_KEYBOARD_PHONE_FIRST + 10));
    reset_draw_counts();
    wm_board_keyboard_draw(keyboard, 1.0f, false);
    assert(button_draws == 1);
    assert(off_icon_draws == 1);
    assert(label_glyphs >= 3);
    float fully_visible_alpha = maximum_quad_alpha;
    assert(fully_visible_alpha > 0.9f);

    reset_draw_counts();
    wm_board_keyboard_draw(keyboard, 0.5f, true);
    assert(maximum_quad_alpha > 0.4f * fully_visible_alpha);
    assert(maximum_quad_alpha < 0.6f * fully_visible_alpha);

    reset_draw_counts();
    wm_board_keyboard_draw(keyboard, 0.0f, true);
    assert(maximum_quad_alpha == 0.0f);

    assert(wm_board_keyboard_activate(keyboard,
        (WmBoardKeyboardControl)(WM_KEYBOARD_PHONE_MODE_FIRST + 3),
        false, character) == WM_KEYBOARD_ACTION_PHONE_MODE);
    reset_draw_counts();
    wm_board_keyboard_draw(keyboard, 1.0f, false);
    assert(off_icon_draws == 0);

    assert(wm_board_keyboard_activate(keyboard,
        (WmBoardKeyboardControl)(WM_KEYBOARD_PHONE_MODE_FIRST + 1),
        false, character) == WM_KEYBOARD_ACTION_PHONE_MODE);
    int language_x = (int)(language_hit.x + language_hit.width * 0.5f);
    int language_y = (int)(language_hit.y + language_hit.height * 0.5f);
    assert(wm_board_keyboard_hit(keyboard, language_x, language_y) ==
           WM_KEYBOARD_LANGUAGE);
    wm_board_keyboard_hover(keyboard, WM_KEYBOARD_LANGUAGE);
    wm_board_keyboard_advance(keyboard, 5.0f);
    reset_draw_counts();
    wm_board_keyboard_draw(keyboard, 1.0f, false);
    assert(button_draws == 0);
    wm_board_keyboard_hover(keyboard, WM_KEYBOARD_NONE);
    wm_board_keyboard_advance(keyboard, 8.0f);
    reset_draw_counts();
    wm_board_keyboard_draw(keyboard, 1.0f, false);
    assert(button_draws == 1);

    WmBoardKeyboardControl phone_letters =
        (WmBoardKeyboardControl)(WM_KEYBOARD_PHONE_FIRST + 1);
    wm_board_keyboard_hover(keyboard, phone_letters);
    assert(wm_board_keyboard_activate(keyboard, phone_letters, true,
                                       character) == WM_KEYBOARD_ACTION_INSERT);
    assert(strcmp(character, "2") == 0);
    assert(wm_board_keyboard_activate(keyboard, phone_letters, true,
                                       character) ==
           WM_KEYBOARD_ACTION_REPLACE_LAST);
    assert(strcmp(character, "c") == 0);
    wm_board_keyboard_hover(keyboard, WM_KEYBOARD_NONE);
    assert(wm_board_keyboard_take_phone_commit(keyboard));
    assert(!wm_board_keyboard_take_phone_commit(keyboard));
    wm_board_keyboard_hover(keyboard, phone_letters);
    assert(wm_board_keyboard_activate(keyboard, phone_letters, false,
                                       character) == WM_KEYBOARD_ACTION_INSERT);
    assert(strcmp(character, "a") == 0);

    WmBoardKeyboardControl phone_space =
        (WmBoardKeyboardControl)(WM_KEYBOARD_PHONE_FIRST + 10);
    wm_board_keyboard_hover(keyboard, phone_space);
    assert(wm_board_keyboard_activate(keyboard, phone_space, false,
                                       character) == WM_KEYBOARD_ACTION_INSERT);
    assert(strcmp(character, " ") == 0);
    assert(wm_board_keyboard_phone_space_pending(keyboard));
    wm_board_keyboard_hover(keyboard, WM_KEYBOARD_NONE);
    assert(!wm_board_keyboard_phone_space_pending(keyboard));
    assert(wm_board_keyboard_take_phone_commit(keyboard));

    assert(wm_board_keyboard_activate(keyboard, WM_KEYBOARD_PREDICTION,
        false, character) == WM_KEYBOARD_ACTION_PREDICTION_TOGGLE);
    assert(wm_board_keyboard_prediction_enabled(keyboard));
    wm_board_keyboard_advance(keyboard, 12.0f);
    wm_board_keyboard_hover(keyboard, phone_letters);
    assert(wm_board_keyboard_activate(keyboard, phone_letters, false,
        character) == WM_KEYBOARD_ACTION_PREDICT_PHONE);
    assert(wm_board_keyboard_candidate_prefix_bytes(keyboard) == 0);
    assert(strlen(wm_board_keyboard_candidate_text(keyboard)) == 1);

    assert(wm_board_keyboard_activate(keyboard, WM_KEYBOARD_LANGUAGE,
        false, character) == WM_KEYBOARD_ACTION_DICTIONARY_OPEN);
    wm_board_keyboard_advance(keyboard, 18.0f);
    assert(wm_board_keyboard_activate(keyboard,
        WM_KEYBOARD_LANGUAGE_FRENCH, false, character) ==
           WM_KEYBOARD_ACTION_DICTIONARY_LANGUAGE);
    wm_board_keyboard_advance(keyboard, 13.0f);
    char memo_context[64] = "";
    wm_board_keyboard_finish_composition(keyboard);
    wm_board_keyboard_set_text_context(keyboard, memo_context);
    static const unsigned accent_digits[4] = {8, 7, 3, 7};
    for (unsigned index = 0; index < 4; index++) {
        WmBoardKeyboardControl key = (WmBoardKeyboardControl)(
            WM_KEYBOARD_PHONE_FIRST + accent_digits[index] - 1);
        assert(wm_board_keyboard_activate(keyboard, key, false, character) ==
               WM_KEYBOARD_ACTION_PREDICT_PHONE);
        size_t replace = wm_board_keyboard_candidate_prefix_bytes(keyboard);
        size_t used = strlen(memo_context);
        const char *value = wm_board_keyboard_candidate_text(keyboard);
        assert(replace <= used && used - replace + strlen(value) <
               sizeof(memo_context));
        strcpy(memo_context + used - replace, value);
        wm_board_keyboard_text_changed(keyboard, true);
    }
    assert(strcmp(memo_context, "très") == 0);
    assert(wm_board_keyboard_activate(keyboard, WM_KEYBOARD_QWERTY,
        false, character) == WM_KEYBOARD_ACTION_LAYOUT_QWERTY);
    memo_context[0] = '\0';
    wm_board_keyboard_set_text_context(keyboard, memo_context);
    strcpy(memo_context, "bon");
    wm_board_keyboard_text_changed(keyboard, false);
    assert(wm_board_keyboard_activate(keyboard,
        WM_KEYBOARD_CANDIDATE_FIRST,
        false, character) == WM_KEYBOARD_ACTION_ACCEPT_CANDIDATE);
    assert(strcmp(wm_board_keyboard_candidate_text(keyboard), "bonjour") == 0);
    assert(wm_board_keyboard_candidate_prefix_bytes(keyboard) == 3);

    assert(wm_board_keyboard_activate(keyboard,
        WM_KEYBOARD_LANGUAGE, false, character) ==
        WM_KEYBOARD_ACTION_DICTIONARY_OPEN);
    wm_board_keyboard_advance(keyboard, 18.0f);
    assert(wm_board_keyboard_activate(keyboard,
        WM_KEYBOARD_LANGUAGE_ENGLISH, false, character) ==
        WM_KEYBOARD_ACTION_DICTIONARY_LANGUAGE);
    wm_board_keyboard_advance(keyboard, 13.0f);
    memo_context[0] = '\0';
    wm_board_keyboard_set_text_context(keyboard, memo_context);
    strcpy(memo_context, "t");
    wm_board_keyboard_text_changed(keyboard, false);
    clip_sets = 0;
    wm_board_keyboard_draw(keyboard, 1.0f, false);
    assert(clip_sets == 1);
    assert(fabsf(candidate_clip.x - (candidate_window.x - 1.0f)) < 0.02f);
    assert(fabsf(candidate_clip.x + candidate_clip.width -
                 (candidate_area.x + candidate_area.width)) < 0.02f);
    assert(candidate_clip.y == 0.0f);
    assert(candidate_clip.height == 456.0f);
    reset_draw_counts();
    wm_board_keyboard_draw(keyboard, 1.0f, false);
    assert(candidate_next_normal_draws == 1);
    WmBoardKeyboardComposition composition;
    assert(wm_board_keyboard_composition(keyboard, &composition));
    assert(composition.prefix_bytes == 1);
    assert(strcmp(composition.preview_candidate, "thank") == 0);
    wm_board_keyboard_hover(keyboard,
        (WmBoardKeyboardControl)(WM_KEYBOARD_CANDIDATE_FIRST + 1));
    assert(wm_board_keyboard_composition(keyboard, &composition));
    assert(composition.preview_hovered);
    assert(strcmp(composition.preview_candidate, "thanks") == 0);
    wm_board_keyboard_hover(keyboard, WM_KEYBOARD_NONE);
    assert(wm_board_keyboard_composition(keyboard, &composition));
    assert(!composition.preview_hovered);
    assert(strcmp(composition.selected_candidate, "thanks") == 0);
    assert(strcmp(composition.preview_candidate, "thank") == 0);
    assert(!wm_board_keyboard_take_phone_commit(keyboard));
    clip_sets = 0;
    wm_board_keyboard_hover(keyboard, WM_KEYBOARD_CANDIDATE_FIRST);
    wm_board_keyboard_advance(keyboard, 6.0f);
    reset_draw_counts();
    wm_board_keyboard_draw(keyboard, 1.0f, false);
    assert(clip_sets == 2);
    assert(candidate_clip.x == -1.0f);
    assert(selected_text_after_keytop > 0);
    assert(selected_text_min_x >= candidate_clip.x);
    wm_board_keyboard_hover(keyboard, WM_KEYBOARD_NONE);
    int next_x = (int)(candidate_next_hit.x +
                       candidate_next_hit.width * 0.5f);
    int next_y = (int)(candidate_next_hit.y +
                       candidate_next_hit.height * 0.5f);
    int previous_x = (int)(candidate_previous_hit.x +
                           candidate_previous_hit.width * 0.5f);
    int previous_y = (int)(candidate_previous_hit.y +
                           candidate_previous_hit.height * 0.5f);
    assert(wm_board_keyboard_hit(keyboard, previous_x, previous_y) !=
           WM_KEYBOARD_CANDIDATE_PREVIOUS);
    assert(wm_board_keyboard_hit(keyboard, next_x, next_y) ==
           WM_KEYBOARD_CANDIDATE_NEXT);
    wm_board_keyboard_hover(keyboard, WM_KEYBOARD_CANDIDATE_NEXT);
    wm_board_keyboard_advance(keyboard, 6.0f);
    reset_draw_counts();
    wm_board_keyboard_draw(keyboard, 1.0f, false);
    assert(candidate_next_normal_draws == 0);
    wm_board_keyboard_hover(keyboard, WM_KEYBOARD_NONE);
    wm_board_keyboard_advance(keyboard, 7.0f);
    reset_draw_counts();
    wm_board_keyboard_draw(keyboard, 1.0f, false);
    assert(candidate_next_normal_draws == 1);
    wm_board_keyboard_hover(keyboard, WM_KEYBOARD_CANDIDATE_NEXT);
    wm_board_keyboard_advance(keyboard, 6.0f);
    assert(wm_board_keyboard_activate(keyboard,
        WM_KEYBOARD_CANDIDATE_NEXT, false, character) ==
        WM_KEYBOARD_ACTION_CANDIDATE_PAGE);
    assert(wm_board_keyboard_begin_hold(keyboard,
        WM_KEYBOARD_CANDIDATE_NEXT));
    assert(wm_board_keyboard_advance(keyboard, 15.0f) == 15);
    assert(wm_board_keyboard_hit(keyboard, next_x, next_y) ==
           WM_KEYBOARD_CANDIDATE_NEXT);
    assert(wm_board_keyboard_held_control(keyboard) ==
           WM_KEYBOARD_CANDIDATE_NEXT);
    assert(wm_board_keyboard_activate(keyboard,
        WM_KEYBOARD_CANDIDATE_NEXT, false, character) ==
        WM_KEYBOARD_ACTION_NONE);
    unsigned repeat_on_unlocked_frame =
        wm_board_keyboard_advance(keyboard, 1.0f);
    assert(wm_board_keyboard_hit(keyboard, previous_x, previous_y) ==
           WM_KEYBOARD_CANDIDATE_PREVIOUS);
    if (wm_board_keyboard_hit(keyboard, next_x, next_y) ==
        WM_KEYBOARD_CANDIDATE_NEXT) {
        assert(repeat_on_unlocked_frame == 1);
        assert(wm_board_keyboard_held_control(keyboard) ==
               WM_KEYBOARD_CANDIDATE_NEXT);
    } else {
        assert(repeat_on_unlocked_frame == 0);
        assert(wm_board_keyboard_held_control(keyboard) ==
               WM_KEYBOARD_NONE);
    }
    WmBoardKeyboardControl page_candidate = wm_board_keyboard_hit(keyboard,
        (int)(candidate_area.x + 5.0f),
        (int)(candidate_area.y + candidate_area.height * 0.5f));
    assert(page_candidate >= WM_KEYBOARD_CANDIDATE_FIRST &&
           page_candidate <= WM_KEYBOARD_CANDIDATE_LAST);
    assert(wm_board_keyboard_activate(keyboard, page_candidate, false,
        character) == WM_KEYBOARD_ACTION_ACCEPT_CANDIDATE);
    assert(strcmp(wm_board_keyboard_candidate_text(keyboard), "thank") != 0);
    assert(wm_board_keyboard_activate(keyboard,
        WM_KEYBOARD_CANDIDATE_PREVIOUS, false, character) ==
        WM_KEYBOARD_ACTION_CANDIDATE_PAGE);
    wm_board_keyboard_advance(keyboard, 16.0f);
    for (unsigned page = 0; page < 20; page++) {
        if (wm_board_keyboard_activate(keyboard,
            WM_KEYBOARD_CANDIDATE_PREVIOUS, false, character) ==
            WM_KEYBOARD_ACTION_NONE) break;
        wm_board_keyboard_advance(keyboard, 16.0f);
    }
    assert(wm_board_keyboard_activate(keyboard,
        WM_KEYBOARD_CANDIDATE_PREVIOUS, false, character) ==
        WM_KEYBOARD_ACTION_NONE);
    assert(wm_board_keyboard_hit(keyboard, next_x, next_y) ==
           WM_KEYBOARD_CANDIDATE_NEXT);

    memo_context[0] = '\0';
    wm_board_keyboard_set_text_context(keyboard, memo_context);
    strcpy(memo_context, "123?!");
    wm_board_keyboard_text_changed(keyboard, false);
    assert(wm_board_keyboard_composition(keyboard, &composition));
    assert(composition.prefix_bytes == 5);
    assert(strcmp(composition.selected_candidate, "123?!") == 0);
    assert(strcmp(composition.preview_candidate, "123?!") == 0);

    wm_board_keyboard_reset(keyboard);
    char predictive_context[64] = "";
    wm_board_keyboard_set_text_context(keyboard, predictive_context);
    if (!wm_board_keyboard_phone_mode(keyboard)) {
        assert(wm_board_keyboard_activate(keyboard, WM_KEYBOARD_PHONE,
            false, character) == WM_KEYBOARD_ACTION_LAYOUT_PHONE);
    }
    if (!wm_board_keyboard_prediction_enabled(keyboard)) {
        assert(wm_board_keyboard_activate(keyboard, WM_KEYBOARD_PREDICTION,
            false, character) == WM_KEYBOARD_ACTION_PREDICTION_TOGGLE);
    }
    wm_board_keyboard_advance(keyboard, 12.0f);
    wm_board_keyboard_hover(keyboard,
        (WmBoardKeyboardControl)(WM_KEYBOARD_PHONE_FIRST + 1));
    assert(wm_board_keyboard_activate(keyboard,
        (WmBoardKeyboardControl)(WM_KEYBOARD_PHONE_FIRST + 1),
        false, character) == WM_KEYBOARD_ACTION_PREDICT_PHONE);
    snprintf(predictive_context, sizeof(predictive_context), "%s",
             wm_board_keyboard_candidate_text(keyboard));
    wm_board_keyboard_text_changed(keyboard, true);
    assert(wm_board_keyboard_composition(keyboard, &composition));
    wm_board_keyboard_hover(keyboard, WM_KEYBOARD_CANDIDATE_FIRST);
    wm_board_keyboard_hover(keyboard,
        (WmBoardKeyboardControl)(WM_KEYBOARD_PHONE_FIRST + 2));
    wm_board_keyboard_hover(keyboard, WM_KEYBOARD_NONE);
    assert(!wm_board_keyboard_take_phone_commit(keyboard));
    assert(wm_board_keyboard_composition(keyboard, &composition));
    wm_board_keyboard_hover(keyboard,
        (WmBoardKeyboardControl)(WM_KEYBOARD_PHONE_FIRST + 1));
    assert(wm_board_keyboard_activate(keyboard,
        (WmBoardKeyboardControl)(WM_KEYBOARD_PHONE_FIRST + 1),
        false, character) == WM_KEYBOARD_ACTION_PREDICT_PHONE);
    snprintf(predictive_context, sizeof(predictive_context), "%s",
             wm_board_keyboard_candidate_text(keyboard));
    wm_board_keyboard_text_changed(keyboard, true);
    wm_board_keyboard_hover(keyboard, WM_KEYBOARD_NONE);
    assert(wm_board_keyboard_take_phone_commit(keyboard));
    assert(!wm_board_keyboard_composition(keyboard, &composition));

    wm_board_keyboard_destroy(keyboard);
    test_candidate_corner(assets, textures, fonts);
    wm_font_cache_destroy(fonts);
    wm_texture_cache_destroy(textures);
    test_prepared_oem_runtime(assets);
    puts("Phone keyboard artwork render commands passed.");
    return 0;
}
