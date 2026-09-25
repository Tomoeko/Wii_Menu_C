#include "wii_menu/board_compose.h"
#include "wii_menu/layout_runtime.h"
#include "wii_menu/source_hit.h"

#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <string.h>

static WmSourceRect header_rect;
static WmSourceRect first_row_rect;
static WmSourceRect second_row_rect;
static WmSourceRect mii_idle_rect;
static WmSourceRect tracked_mii_rect;
static WmSourceRect mii_half_focus_rect;
static WmSourceRect mii_reentered_rect;
static WmSourceRect hint_rect;
static WmSourceRect selector_mask_rect;
static WmSourceRect notice_panel_rect;
static WmSourceRect back_button_rect;
static WmSourceRect post_button_rect;
static unsigned draw_index;
static unsigned header_index;
static unsigned first_row_index;
static unsigned second_row_index;
static unsigned mii_idle_draws;
static unsigned mii_half_focus_draws;
static unsigned mii_reentered_draws;
static unsigned notice_panel_index;
static unsigned back_button_index;
static unsigned post_button_index;
static unsigned back_button_near_draws;
static unsigned post_button_near_draws;
static bool tracking_notice;
static bool tracking_hint;
static bool tracking_mii_alpha;
static bool tracking_selector_left;
static bool tracking_line_feed;
static bool tracking_back_scale;
static float back_max_width;
static unsigned hint_glyphs;
static unsigned selector_left_draws;
static unsigned selector_mask_draws;
static unsigned line_feed_glyphs;
static float hint_max_alpha;
static float mii_alpha;
static float line_feed_u;
static float line_feed_v;

static bool contains_center(const WmSourceRect *rect, float x, float y) {
    return x >= rect->x && x <= rect->x + rect->width &&
           y >= rect->y && y <= rect->y + rect->height;
}

static bool near_center(const WmMaterialQuad *quad,
                        const WmSourceRect *rect) {
    float x = 0.0f, y = 0.0f;
    for (size_t index = 0; index < 4; index++) {
        x += quad->vertices[index].x * 0.25f;
        y += quad->vertices[index].y * 0.25f;
    }
    return fabsf(x - rect->x - rect->width * 0.5f) < 12.0f &&
           fabsf(y - rect->y - rect->height * 0.5f) < 12.0f &&
           quad->vertices[0].color.a > 0.01f;
}

static WmBoardComposeControl compose_key(WmBoardKeyboardControl key) {
    return (WmBoardComposeControl)(WM_COMPOSE_CONTROL_KEY_FIRST + key - 1);
}
static uint32_t next_texture = 1;

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
    return fabsf(left - rect->x) < 0.03f &&
           fabsf(top - rect->y) < 0.03f &&
           fabsf(right - left - rect->width) < 0.03f &&
           fabsf(bottom - top - rect->height) < 0.03f;
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
    if (tracking_line_feed && texture != 0 &&
        fabsf(vertices[0].u - line_feed_u) < 0.0001f &&
        fabsf(vertices[0].v - line_feed_v) < 0.0001f &&
        vertices[0].color.a > 0.01f) line_feed_glyphs++;
    if (!tracking_hint || texture == 0) return;
    float x = (vertices[0].x + vertices[3].x) * 0.5f;
    float y = (vertices[0].y + vertices[3].y) * 0.5f;
    if (vertices[0].color.a > 0.05f &&
        contains_center(&hint_rect, x, y) &&
        y < hint_rect.y + 50.0f) {
        hint_glyphs++;
        hint_max_alpha = fmaxf(hint_max_alpha, vertices[0].color.a);
    }
}

void wm_platform_draw_material_quad(WmPlatform *platform,
                                    const WmMaterialQuad *quad) {
    (void)platform;
    draw_index++;
    if (tracking_back_scale && near_center(quad, &back_button_rect)) {
        float width = fabsf(quad->vertices[1].x - quad->vertices[0].x);
        back_max_width = fmaxf(back_max_width, width);
    }
    if (tracking_selector_left) {
        float center_x = (quad->vertices[0].x + quad->vertices[1].x +
                          quad->vertices[2].x + quad->vertices[3].x) * 0.25f;
        float center_y = (quad->vertices[0].y + quad->vertices[1].y +
                          quad->vertices[2].y + quad->vertices[3].y) * 0.25f;
        float width = fabsf(quad->vertices[1].x - quad->vertices[0].x);
        if (center_x > 0.0f && center_x < 150.0f &&
            center_y > 120.0f && center_y < 300.0f &&
            width > 100.0f && quad->vertices[0].color.a > 0.0f)
            selector_left_draws++;
        if (matches_rect(quad, &selector_mask_rect))
            selector_mask_draws++;
    }
    if (matches_rect(quad, &header_rect)) header_index = draw_index;
    if (matches_rect(quad, &first_row_rect)) first_row_index = draw_index;
    if (matches_rect(quad, &second_row_rect)) second_row_index = draw_index;
    if (matches_rect(quad, &mii_idle_rect)) mii_idle_draws++;
    if (matches_rect(quad, &mii_half_focus_rect)) mii_half_focus_draws++;
    if (matches_rect(quad, &mii_reentered_rect)) mii_reentered_draws++;
    if (tracking_mii_alpha && matches_rect(quad, &tracked_mii_rect)) {
        mii_alpha = fmaxf(mii_alpha, quad->vertices[0].color.a);
    }
    if (tracking_notice) {
        if (near_center(quad, &back_button_rect)) back_button_near_draws++;
        if (near_center(quad, &post_button_rect)) post_button_near_draws++;
        if (matches_rect(quad, &notice_panel_rect))
            notice_panel_index = draw_index;
        if (matches_rect(quad, &back_button_rect))
            back_button_index = draw_index;
        if (matches_rect(quad, &post_button_rect))
            post_button_index = draw_index;
    }
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

int main(int argc, char **argv) {
    const char *assets = argc > 1 ? argv[1] : ".local/native-assets";
    char path[4096];
    int length = snprintf(path, sizeof(path),
                          "%s/layouts/sofkeybd/my_Memo_a.json", assets);
    assert(length > 0 && length < (int)sizeof(path));
    FILE *check = fopen(path, "rb");
    if (!check) {
        puts("Memo draw-order test skipped: local WAD export absent.");
        return 0;
    }
    fclose(check);

    char error[160] = {0};
    WmLayout *source = wm_layout_load_json(path, error, sizeof(error));
    assert(source);
    WmLayoutClip entry = {
        .animation = "my_Memo_a_MailIn", .frame = 16.0f
    };
    assert(wm_layout_pose(source, &entry, 1));
    assert(wm_source_pane_rect(source, "Header_s2", true, WM_LAYOUT_IPL,
                               NULL, &header_rect));
    assert(wm_source_pane_rect(source, "Picture_12", true, WM_LAYOUT_IPL,
                               NULL, &first_row_rect));
    assert(wm_source_pane_rect(source, "Nigaoe", true, WM_LAYOUT_IPL,
                               NULL, &mii_idle_rect));
    assert(wm_source_pane_rect(source, "T_TouchLetter", true,
                               WM_LAYOUT_IPL, NULL, &hint_rect));
    second_row_rect = first_row_rect;
    second_row_rect.y += 42.0f;
    WmLayoutClip half_focus[] = {
        entry,
        { .animation = "my_Memo_a_NigaoeFoucusIn",
          .target_name = "Nigaoe", .frame = 3.0f }
    };
    assert(wm_layout_pose(source, half_focus, 2));
    assert(wm_source_pane_rect(source, "Nigaoe", true, WM_LAYOUT_IPL,
                               NULL, &mii_half_focus_rect));
    half_focus[1].frame = 2.0f;
    assert(wm_layout_pose(source, half_focus, 2));
    assert(wm_source_pane_rect(source, "Nigaoe", true, WM_LAYOUT_IPL,
                               NULL, &mii_reentered_rect));

    length = snprintf(path, sizeof(path),
                      "%s/layouts/mlAdSel/my_Mail_a.json", assets);
    assert(length > 0 && length < (int)sizeof(path));
    WmLayout *selector = wm_layout_load_json(path, error, sizeof(error));
    assert(selector);
    WmLayoutClip selector_entry = {
        .animation = "my_Mail_a_SelectIn",
        .group = "G_SelectInOut", .frame = 30.0f
    };
    assert(wm_layout_pose(selector, &selector_entry, 1));
    assert(wm_source_pane_rect(selector, "mask", true,
                               WM_LAYOUT_IPL, NULL, &selector_mask_rect));

    length = snprintf(path, sizeof(path),
                      "%s/layouts/cmnBtn/my_IplTop_e.json", assets);
    assert(length > 0 && length < (int)sizeof(path));
    WmLayout *footer = wm_layout_load_json(path, error, sizeof(error));
    assert(footer);
    WmLayoutClip footer_clips[] = {
        { .animation = "my_IplTop_e", .group = "G_SeenChange",
          .frame = 3326.0f },
        { .animation = "my_IplTop_e", .group = "G_ArwL_End",
          .frame = 10110.0f },
        { .animation = "my_IplTop_e", .group = "G_ArwR_End",
          .frame = 10110.0f }
    };
    assert(wm_layout_pose(footer, footer_clips, 3));
    assert(wm_source_pane_rect(footer, "CalExitBase1", true,
                               WM_LAYOUT_IPL, NULL, &back_button_rect));
    assert(wm_source_pane_rect(footer, "Add_R_Base1", true,
                               WM_LAYOUT_IPL, NULL, &post_button_rect));

    length = snprintf(path, sizeof(path),
                      "%s/layouts/dlgWdw/my_DialogWindow_a2.json", assets);
    assert(length > 0 && length < (int)sizeof(path));
    WmLayout *dialog = wm_layout_load_json(path, error, sizeof(error));
    assert(dialog);
    WmLayoutClip dialog_entry = {
        .animation = "my_DialogWindow_a1_DialogIn",
        .group = "G_InOut", .frame = 24.0f
    };
    assert(wm_layout_pose(dialog, &dialog_entry, 1));
    assert(wm_source_pane_rect(dialog, "Picture_03", true,
                               WM_LAYOUT_IPL, NULL, &notice_panel_rect));

    WmTextureCache *textures = wm_texture_cache_create(
        (WmPlatform *)1, assets, 64u * 1024u * 1024u);
    WmFontCache *fonts = wm_font_cache_create(
        (WmPlatform *)1, assets, 8u * 1024u * 1024u);
    assert(textures && fonts);
    WmFontPane letter_font;
    const char *letter_font_name = NULL;
    assert(wm_layout_pane_font(source, "T_Letter", &letter_font,
                               &letter_font_name));
    WmCachedFont *letter_face = wm_font_cache_resolve(fonts,
                                                      letter_font_name);
    const WmFont *letter_resource = wm_cached_font_resource(letter_face);
    const WmFontGlyph *line_feed = wm_font_glyph(letter_resource, 0xe056);
    assert(line_feed);
    const WmFontSheetInfo *line_feed_sheet = wm_font_sheet_info(
        letter_resource, line_feed->sheet);
    assert(line_feed_sheet);
    line_feed_u = (float)line_feed->x / line_feed_sheet->width;
    line_feed_v = (float)line_feed->y / line_feed_sheet->height;
    WmBoardCompose *compose = wm_board_compose_create(
        (WmPlatform *)1, assets, textures, fonts);
    assert(compose && wm_board_compose_open(compose));
    wm_board_compose_advance(compose, 39.0f);
    wm_board_compose_hover(compose, WM_COMPOSE_CONTROL_MEMO);
    wm_board_compose_advance(compose, 6.0f);
    tracking_selector_left = true;
    wm_board_compose_draw(compose);
    assert(selector_left_draws > 0);
    assert(selector_mask_draws > 0);
    assert(wm_board_compose_activate(compose, WM_COMPOSE_CONTROL_MEMO));
    wm_board_compose_advance(compose, 26.0f);
    selector_left_draws = 0;
    selector_mask_draws = 0;
    wm_board_compose_draw(compose);
    assert(selector_left_draws == 0);
    assert(selector_mask_draws == 0);
    tracking_selector_left = false;

    assert(header_index > 0);
    assert(first_row_index > header_index);
    assert(second_row_index > first_row_index);
    assert(mii_idle_draws > 0);
    mii_half_focus_draws = 0;
    wm_board_compose_hover(compose, WM_COMPOSE_CONTROL_MII);
    wm_board_compose_advance(compose, 3.0f);
    wm_board_compose_draw(compose);
    assert(mii_half_focus_draws > 0);
    mii_half_focus_draws = 0;
    wm_board_compose_hover(compose, WM_COMPOSE_CONTROL_NONE);
    wm_board_compose_draw(compose);
    assert(mii_half_focus_draws > 0);
    mii_reentered_draws = 0;
    wm_board_compose_advance(compose, 1.0f);
    wm_board_compose_hover(compose, WM_COMPOSE_CONTROL_MII);
    wm_board_compose_draw(compose);
    assert(mii_reentered_draws > 0);
    assert(wm_board_compose_activate(compose, WM_COMPOSE_CONTROL_MII));
    wm_board_compose_advance(compose, 25.0f);
    tracking_notice = true;
    wm_board_compose_draw(compose);
    tracking_notice = false;
    assert(notice_panel_index > 0);
    assert(back_button_index > 0 &&
           back_button_index < notice_panel_index);
    assert(post_button_index > 0 &&
           post_button_index < notice_panel_index);
    assert(wm_board_compose_activate(compose,
                                     WM_COMPOSE_CONTROL_ADDRESS_DIALOG_OK));
    wm_board_compose_advance(compose, 38.0f);
    assert(wm_board_compose_phase(compose) == WM_COMPOSE_MEMO);
    tracking_hint = true;
    hint_glyphs = 0;
    wm_board_compose_draw(compose);
    assert(hint_glyphs > 0);
    assert(wm_board_compose_activate(compose, WM_COMPOSE_CONTROL_EDIT));
    wm_board_compose_advance(compose, 30.0f);
    assert(wm_board_compose_phase(compose) == WM_COMPOSE_EDIT);
    assert(wm_board_compose_finish_edit(compose));
    assert(strcmp(wm_board_compose_take_key_cue(compose),
                  "WIPL_SE_SK_CANCEL_CLOSE") == 0);
    assert(wm_board_compose_take_key_cue(compose) == NULL);
    wm_board_compose_advance(compose, 20.0f);
    hint_glyphs = 0;
    hint_max_alpha = 0.0f;
    wm_board_compose_draw(compose);
    assert(hint_glyphs == 0);
    wm_board_compose_advance(compose, 5.0f);
    hint_glyphs = 0;
    hint_max_alpha = 0.0f;
    wm_board_compose_draw(compose);
    assert(hint_glyphs > 0);
    float half_hint_alpha = hint_max_alpha;
    wm_board_compose_advance(compose, 5.0f);
    assert(wm_board_compose_phase(compose) == WM_COMPOSE_MEMO);
    hint_glyphs = 0;
    hint_max_alpha = 0.0f;
    wm_board_compose_draw(compose);
    assert(hint_glyphs > 0);
    assert(hint_max_alpha > half_hint_alpha);
    assert(wm_board_compose_activate(compose, WM_COMPOSE_CONTROL_EDIT));
    wm_board_compose_advance(compose, 30.0f);
    assert(wm_board_compose_finish_edit(compose));
    wm_board_compose_advance(compose, 30.0f);
    hint_glyphs = 0;
    wm_board_compose_draw(compose);
    assert(hint_glyphs > 0);
    tracking_hint = false;

    assert(wm_board_compose_back(compose));
    assert(strcmp(wm_board_compose_take_key_cue(compose),
                  "WIPL_SE_CANCEL") == 0);
    assert(wm_board_compose_take_key_cue(compose) == NULL);
    wm_board_compose_advance(compose, 20.0f);
    WmLayoutClip exit_clip = {
        .animation = "my_Memo_a_MailOut", .frame = 0.0f
    };
    assert(wm_layout_pose(source, &exit_clip, 1));
    assert(wm_source_pane_rect(source, "Nigaoe", true, WM_LAYOUT_IPL,
                               NULL, &tracked_mii_rect));
    tracking_mii_alpha = true;
    mii_alpha = 0.0f;
    wm_board_compose_draw(compose);
    float mii_exit_start_alpha = mii_alpha;
    wm_board_compose_advance(compose, 8.0f);
    exit_clip.frame = 8.0f;
    assert(wm_layout_pose(source, &exit_clip, 1));
    assert(wm_source_pane_rect(source, "Nigaoe", true, WM_LAYOUT_IPL,
                               NULL, &tracked_mii_rect));
    mii_alpha = 0.0f;
    wm_board_compose_draw(compose);
    float mii_exit_middle_alpha = mii_alpha;
    wm_board_compose_advance(compose, 8.0f);
    exit_clip.frame = 16.0f;
    assert(wm_layout_pose(source, &exit_clip, 1));
    assert(wm_source_pane_rect(source, "Nigaoe", true, WM_LAYOUT_IPL,
                               NULL, &tracked_mii_rect));
    mii_alpha = 0.0f;
    wm_board_compose_draw(compose);
    assert(mii_exit_start_alpha > mii_exit_middle_alpha);
    assert(mii_exit_middle_alpha > mii_alpha);
    tracking_mii_alpha = false;
    wm_board_compose_destroy(compose);

    compose = wm_board_compose_create((WmPlatform *)1, assets,
                                      textures, fonts);
    assert(compose && wm_board_compose_open(compose));
    wm_board_compose_advance(compose, 39.0f);
    assert(wm_board_compose_activate(compose, WM_COMPOSE_CONTROL_MEMO));
    wm_board_compose_advance(compose, 26.0f);
    assert(wm_board_compose_activate(compose, WM_COMPOSE_CONTROL_EDIT));
    wm_board_compose_advance(compose, 30.0f);
    while (wm_board_compose_take_key_cue(compose)) {}
    assert(wm_board_compose_activate(compose,
                                     compose_key(WM_KEYBOARD_PHONE)));
    while (wm_board_compose_take_key_cue(compose)) {}
    WmBoardComposeControl phone_space = compose_key(
        (WmBoardKeyboardControl)(WM_KEYBOARD_PHONE_FIRST + 10));
    WmBoardComposeControl phone_letters = compose_key(
        (WmBoardKeyboardControl)(WM_KEYBOARD_PHONE_FIRST + 1));
    wm_board_compose_hover(compose, phone_space);
    assert(wm_board_compose_activate(compose, phone_space));
    assert(strcmp(wm_board_compose_text(compose), " ") == 0);
    assert(strcmp(wm_board_compose_display_text(compose),
                  "\342\220\243") == 0);
    while (wm_board_compose_take_key_cue(compose)) {}
    wm_board_compose_hover(compose, phone_letters);
    assert(strcmp(wm_board_compose_display_text(compose), " ") == 0);
    assert(strcmp(wm_board_compose_take_key_cue(compose),
                  "WIPL_SE_CHAR_DECIDE") == 0);
    assert(wm_board_compose_take_key_cue(compose) == NULL);
    assert(wm_board_compose_activate(compose, phone_letters));
    assert(strlen(wm_board_compose_text(compose)) == 2);
    char first_letter = wm_board_compose_text(compose)[1];
    assert(wm_board_compose_activate(compose, phone_letters));
    assert(strlen(wm_board_compose_text(compose)) == 2);
    assert(wm_board_compose_text(compose)[1] != first_letter);
    wm_board_compose_destroy(compose);

    compose = wm_board_compose_create((WmPlatform *)1, assets,
                                      textures, fonts);
    assert(compose && wm_board_compose_open(compose));
    wm_board_compose_advance(compose, 39.0f);
    assert(wm_board_compose_activate(compose, WM_COMPOSE_CONTROL_MEMO));
    wm_board_compose_advance(compose, 26.0f);
    assert(wm_board_compose_activate(compose, WM_COMPOSE_CONTROL_EDIT));
    wm_board_compose_advance(compose, 30.0f);
    while (wm_board_compose_take_key_cue(compose)) {}
    assert(wm_board_compose_activate(compose,
                                     compose_key(WM_KEYBOARD_PREDICTION)));
    assert(strcmp(wm_board_compose_take_key_cue(compose),
                  "WIPL_SE_SK_PREDICT_ON") == 0);
    assert(wm_board_compose_insert_text(compose, "hel"));
    assert(strcmp(wm_board_compose_text(compose), "hel") == 0);
    assert(strcmp(wm_board_compose_display_text(compose), "hello") == 0);
    assert(wm_board_compose_activate(compose, compose_key(
        WM_KEYBOARD_CANDIDATE_FIRST)));
    assert(strcmp(wm_board_compose_text(compose), "hello") == 0);
    assert(strcmp(wm_board_compose_take_key_cue(compose),
                  "WIPL_SE_CHAR_DECIDE") == 0);
    assert(wm_board_compose_insert_text(compose, " xz1!"));
    assert(strcmp(wm_board_compose_display_text(compose),
                  "hello xz1!") == 0);
    assert(wm_board_compose_insert_text(compose, " "));
    assert(strcmp(wm_board_compose_text(compose), "hello xz1! ") == 0);
    char boundary_run[33];
    memset(boundary_run, 'a', 32);
    boundary_run[32] = '\0';
    assert(wm_board_compose_insert_text(compose, boundary_run));
    while (wm_board_compose_take_key_cue(compose)) {}
    assert(wm_board_compose_insert_text(compose, "b"));
    assert(strcmp(wm_board_compose_take_key_cue(compose),
                  "WIPL_SE_CHAR_DECIDE") == 0);
    assert(strncmp(wm_board_compose_text(compose), "hello xz1! ", 11) == 0);
    assert(strncmp(wm_board_compose_text(compose) + 11,
                   boundary_run, 32) == 0);
    assert(strcmp(wm_board_compose_text(compose) + 43, "b") == 0);
    wm_board_compose_destroy(compose);

    compose = wm_board_compose_create((WmPlatform *)1, assets,
                                      textures, fonts);
    assert(compose && wm_board_compose_open(compose));
    wm_board_compose_advance(compose, 39.0f);
    assert(wm_board_compose_activate(compose, WM_COMPOSE_CONTROL_MEMO));
    wm_board_compose_advance(compose, 26.0f);
    assert(wm_board_compose_activate(compose, WM_COMPOSE_CONTROL_EDIT));
    wm_board_compose_advance(compose, 30.0f);
    assert(wm_board_compose_activate(compose,
                                     compose_key(WM_KEYBOARD_PREDICTION)));
    assert(wm_board_compose_activate(compose,
                                     compose_key(WM_KEYBOARD_PHONE)));
    for (unsigned index = 0; index < 32; index++)
        assert(wm_board_compose_activate(compose, phone_letters));
    assert(strlen(wm_board_compose_text(compose)) == 32);
    while (wm_board_compose_take_key_cue(compose)) {}
    assert(wm_board_compose_activate(compose, phone_letters));
    assert(strlen(wm_board_compose_text(compose)) == 33);
    assert(strcmp(wm_board_compose_take_key_cue(compose),
                  "WIPL_SE_CHAR_DECIDE") == 0);
    assert(wm_board_compose_take_key_cue(compose) == NULL);
    wm_board_compose_destroy(compose);

    compose = wm_board_compose_create((WmPlatform *)1, assets,
                                      textures, fonts);
    assert(compose && wm_board_compose_open(compose));
    wm_board_compose_advance(compose, 39.0f);
    assert(wm_board_compose_activate(compose, WM_COMPOSE_CONTROL_MEMO));
    wm_board_compose_advance(compose, 26.0f);
    assert(wm_board_compose_activate(compose, WM_COMPOSE_CONTROL_EDIT));
    wm_board_compose_advance(compose, 30.0f);
    assert(wm_board_compose_insert_text(compose, "First\nSecond"));
    tracking_line_feed = true;
    line_feed_glyphs = 0;
    wm_board_compose_draw(compose);
    assert(line_feed_glyphs == 1);
    assert(strcmp(wm_board_compose_text(compose), "First\nSecond") == 0);
    assert(wm_board_compose_finish_edit(compose));
    wm_board_compose_advance(compose, 30.0f);
    line_feed_glyphs = 0;
    wm_board_compose_draw(compose);
    assert(line_feed_glyphs == 0);
    tracking_line_feed = false;

    tracking_notice = true;
    back_button_index = 0;
    post_button_index = 0;
    back_button_near_draws = 0;
    post_button_near_draws = 0;
    wm_board_compose_draw(compose);
    assert(back_button_index > 0 && post_button_index > 0);
    assert(wm_board_compose_back(compose));
    back_button_near_draws = 0;
    post_button_near_draws = 0;
    wm_board_compose_draw(compose);
    assert(back_button_near_draws > 0 && post_button_near_draws > 0);
    tracking_notice = false;
    wm_board_compose_advance(compose, 63.0f);
    assert(wm_board_compose_phase(compose) == WM_COMPOSE_SELECTOR);
    wm_board_compose_destroy(compose);

    compose = wm_board_compose_create((WmPlatform *)1, assets,
                                      textures, fonts);
    assert(compose && wm_board_compose_open(compose));
    wm_board_compose_advance(compose, 39.0f);
    assert(wm_board_compose_activate(compose, WM_COMPOSE_CONTROL_MEMO));
    wm_board_compose_advance(compose, 26.0f);
    assert(wm_board_compose_insert_text(compose, "Posted text"));
    wm_board_compose_advance(compose, 30.0f);
    assert(wm_board_compose_finish_edit(compose));
    wm_board_compose_advance(compose, 30.0f);
    assert(wm_board_compose_activate(compose, WM_COMPOSE_CONTROL_POST));
    wm_board_compose_advance(compose, 20.0f + 51.0f);
    assert(wm_board_compose_phase(compose) ==
           WM_COMPOSE_EXIT_AFTER_POST);
    tracking_selector_left = true;
    for (int frame = 0; frame < 20; frame += 10) {
        selector_left_draws = 0;
        wm_board_compose_draw(compose);
        assert(selector_left_draws == 0);
        wm_board_compose_advance(compose, 10.0f);
    }
    tracking_selector_left = false;
    wm_board_compose_advance(compose, 1.0f);
    assert(wm_board_compose_phase(compose) == WM_COMPOSE_CLOSED);
    wm_board_compose_destroy(compose);

    /* A click on Memo Back at its source button coordinates must retire the
     * press scale before the returning selector's Back becomes available. */
    compose = wm_board_compose_create((WmPlatform *)1, assets,
                                      textures, fonts);
    assert(compose && wm_board_compose_open(compose));
    wm_board_compose_advance(compose, 39.0f);
    assert(wm_board_compose_activate(compose, WM_COMPOSE_CONTROL_MEMO));
    wm_board_compose_advance(compose, 26.0f);
    int back_x = (int)(back_button_rect.x + back_button_rect.width * 0.5f);
    int back_y = (int)(back_button_rect.y + back_button_rect.height * 0.5f);
    assert(wm_board_compose_hit(compose, back_x, back_y) ==
           WM_COMPOSE_CONTROL_BACK);
    tracking_back_scale = true;
    back_max_width = 0.0f;
    wm_board_compose_draw(compose);
    float neutral_width = back_max_width;
    assert(neutral_width > 0.0f);
    wm_board_compose_hover(compose, WM_COMPOSE_CONTROL_BACK);
    wm_board_compose_advance(compose, 6.0f);
    back_max_width = 0.0f;
    wm_board_compose_draw(compose);
    assert(back_max_width > neutral_width * 1.05f);
    assert(wm_board_compose_activate(compose,
        wm_board_compose_hit(compose, back_x, back_y)));
    wm_board_compose_advance(compose, 20.0f);
    back_max_width = 0.0f;
    wm_board_compose_draw(compose);
    assert(fabsf(back_max_width - neutral_width) < 0.5f);
    wm_board_compose_advance(compose, 43.0f);
    assert(wm_board_compose_phase(compose) == WM_COMPOSE_SELECTOR);
    back_max_width = 0.0f;
    wm_board_compose_draw(compose);
    assert(fabsf(back_max_width - neutral_width) < 0.5f);
    tracking_back_scale = false;
    wm_board_compose_destroy(compose);

    wm_font_cache_destroy(fonts);
    wm_texture_cache_destroy(textures);
    wm_layout_destroy(dialog);
    wm_layout_destroy(footer);
    wm_layout_destroy(selector);
    wm_layout_destroy(source);
    puts("Memo selector, sheet, hover, notice, keyboard, and exit fades passed.");
    return 0;
}
