#include "board_compose_internal.h"
#include "board_compose_presentation.h"

#include "wii_menu/input/source_hit.h"
#include "wii_menu/layout/layout_present.h"

#include <math.h>
#include <stdint.h>
#include <string.h>

enum { COMPOSE_CLIP_CAPACITY = 20 };

static void append_clip(WmLayoutClip clips[COMPOSE_CLIP_CAPACITY], size_t *count,
                        const char *animation, const char *group, float frame) {
    if (*count >= COMPOSE_CLIP_CAPACITY) {
        return;
    }
    clips[*count] = (WmLayoutClip){
        .animation = animation,
        .group = group,
        .frame = frame,
        .loop_override = 0
    };
    (*count)++;
}

static void append_target_clip(WmLayoutClip clips[COMPOSE_CLIP_CAPACITY], size_t *count,
                               const char *animation, const char *target_name,
                               float frame) {
    if (*count >= COMPOSE_CLIP_CAPACITY) {
        return;
    }
    clips[*count] = (WmLayoutClip){
        .animation = animation,
        .target_name = target_name,
        .frame = frame,
        .loop_override = 0
    };
    (*count)++;
}

void board_compose_pose_selector(WmBoardCompose *compose) {
    WmLayoutClip clips[COMPOSE_CLIP_CAPACITY];
    size_t count = 0;
    float entry = compose->phase == WM_COMPOSE_ENTER_SELECTOR
                      ? clamp_frame(compose->frame, 30.0f)
                      : 30.0f;
    append_clip(clips, &count, "my_Mail_a_SelectIn", "G_SelectInOut", entry);
    if (compose->phase == WM_COMPOSE_ADDRESS) {
        WmBoardAddressPhase phase = wm_board_address_phase(compose->address);
        float frame = wm_board_address_phase_frame(compose->address);
        append_clip(clips, &count,
                    phase == WM_BOARD_ADDRESS_EXIT ? "my_Mail_a_AdressOut"
                                                   : "my_Mail_a_AdressIn",
                    NULL,
                    phase == WM_BOARD_ADDRESS_EXIT    ? clamp_frame(frame - 1.0f, 16.0f)
                    : phase == WM_BOARD_ADDRESS_ENTER ? clamp_frame(frame, 16.0f)
                                                      : 16.0f);
    } else if (compose->phase == WM_COMPOSE_BACK_SELECTOR) {
        append_clip(clips, &count, "my_Mail_a_SelectOut", "G_SelectInOut",
                    clamp_frame(compose->frame, 20.0f));
    } else if (compose->phase == WM_COMPOSE_EXIT_AFTER_POST) {
        /* SelectOut starts from MailIn's hidden choice cards. Restoring the
         * source pose here makes Memo, Letter, and Address flash after Post. */
        append_clip(clips, &count, "my_Mail_a_MailIn", NULL, 16.0f);
        append_clip(clips, &count, "my_Mail_a_SelectOut", "G_SelectInOut",
                    clamp_frame(compose->frame, 20.0f));
    } else if (compose->phase == WM_COMPOSE_ENTER_MEMO ||
               compose->phase == WM_COMPOSE_MEMO ||
               compose->phase == WM_COMPOSE_ENTER_EDIT ||
               compose->phase == WM_COMPOSE_EDIT ||
               compose->phase == WM_COMPOSE_LEAVE_EDIT ||
               compose->phase == WM_COMPOSE_POST_PRESS ||
               compose->phase == WM_COMPOSE_SEND) {
        append_clip(clips, &count, "my_Mail_a_MailIn", NULL,
                    compose->phase == WM_COMPOSE_ENTER_MEMO
                        ? clamp_frame(compose->frame, 16.0f)
                        : 16.0f);
    } else if (compose->phase == WM_COMPOSE_BACK_MEMO) {
        if (compose->frame < 46.0f) {
            append_clip(clips, &count, "my_Mail_a_MailIn", NULL, 16.0f);
        } else {
            append_clip(clips, &count, "my_Mail_a_MailOut", NULL,
                        clamp_frame(compose->frame - 46.0f, 16.0f));
        }
    }
    static const struct {
        WmBoardComposeControl control;
        const char *group;
        const char *enter;
        const char *leave;
    } buttons[] = {
        {
            WM_COMPOSE_CONTROL_MEMO, "G_MailFoucus",
            "my_Mail_a_MailFoucusIn", "my_Mail_a_MailFoucusOut"
        },
        {
            WM_COMPOSE_CONTROL_LETTER, "G_LetterFoucus",
            "my_Mail_a_LetterFoucusIn", "my_Mail_a_LetterFoucusOut"
        },
        {
            WM_COMPOSE_CONTROL_ADDRESS, "G_AdressFoucus",
            "my_Mail_a_AdressFoucusIn", "my_Mail_a_AdressFoucusOut"
        }
    };
    /* Selector focus has a constant card alpha track. After selection it
     * would override the full MailIn/AdressIn clip's card exit fade. */
    if (compose->phase == WM_COMPOSE_ENTER_SELECTOR ||
        compose->phase == WM_COMPOSE_SELECTOR ||
        compose->phase == WM_COMPOSE_BACK_SELECTOR) {
        for (size_t index = 0; index < sizeof(buttons) / sizeof(buttons[0]); index++) {
            ComposeFocus focus = compose->focus[buttons[index].control];
            if (!focus.active)
                continue;
            append_clip(clips, &count,
                        focus.entering ? buttons[index].enter : buttons[index].leave,
                        buttons[index].group, clamp_frame(focus.frame, 6.0f));
        }
    }
    wm_layout_pose(compose->selector, clips, count);
    wm_layout_set_pose_text(compose->selector, "T_Mail", "Memo");
    wm_layout_set_pose_text(compose->selector, "T_Adress", "Address Book");
}

static bool body_visible(const WmBoardCompose *compose) {
    return compose->phase == WM_COMPOSE_ENTER_MEMO ||
           compose->phase == WM_COMPOSE_MEMO ||
           compose->phase == WM_COMPOSE_ENTER_EDIT ||
           compose->phase == WM_COMPOSE_EDIT ||
           compose->phase == WM_COMPOSE_LEAVE_EDIT ||
           compose->phase == WM_COMPOSE_POST_PRESS ||
           compose->phase == WM_COMPOSE_SEND ||
           (compose->phase == WM_COMPOSE_BACK_MEMO && compose->frame < 46.0f);
}

void board_compose_pose_body(WmBoardCompose *compose) {
    WmLayoutClip clips[COMPOSE_CLIP_CAPACITY];
    size_t count = 0;
    if (compose->phase == WM_COMPOSE_ENTER_MEMO) {
        append_clip(clips, &count, "my_Memo_a_MailIn", NULL,
                    clamp_frame(compose->frame, 16.0f));
    } else if (compose->phase == WM_COMPOSE_SEND) {
        append_clip(clips, &count, "my_Memo_a_MailIn", NULL, 16.0f);
        append_clip(clips, &count, "my_Memo_a_SendOut", NULL,
                    clamp_frame(compose->frame, 50.0f));
    } else if (compose->phase == WM_COMPOSE_BACK_MEMO && compose->frame >= 20.0f) {
        append_clip(clips, &count, "my_Memo_a_MailOut", NULL,
                    clamp_frame(compose->frame - 20.0f, 16.0f));
    } else {
        append_clip(clips, &count, "my_Memo_a_MailIn", NULL, 16.0f);
    }
    if (compose->phase == WM_COMPOSE_ENTER_EDIT || compose->phase == WM_COMPOSE_EDIT ||
        compose->phase == WM_COMPOSE_LEAVE_EDIT) {
        float hint_frame = compose->phase == WM_COMPOSE_ENTER_EDIT
                               ? clamp_frame(compose->frame, 9.0f)
                               : 9.0f;
        if (compose->phase == WM_COMPOSE_LEAVE_EDIT && compose->draft.text_bytes == 0) {
            /* The source prompt returns during the final ten keyboard-exit
             * updates by reversing its nine-frame TouchLetter alpha curve. */
            hint_frame =
                9.0f * (1.0f - clamp_frame(compose->frame - 20.0f, 10.0f) / 10.0f);
        }
        append_clip(clips, &count, "my_Memo_a_TouchLetter", NULL, hint_frame);
    }
    static const char *const display_end_groups[COMPOSE_SCROLL_DIRECTIONS] = {
        "G_ArwR_End", "G_ArwL_End"};
    static const char *const display_focus_groups[COMPOSE_SCROLL_DIRECTIONS] = {
        "G_ArwR_Focus", "G_ArwL_Focus"};
    static const char *const display_press_groups[COMPOSE_SCROLL_DIRECTIONS] = {
        "G_ArwR_Ac", "G_ArwL_Ac"};
    static const char *const editor_panes[COMPOSE_SCROLL_DIRECTIONS] = {
        "P_txtScrll_UP", "P_txtScrll_DOWN"};
    append_clip(clips, &count, "my_Memo_a_Loop", "G_ArwRoop",
                fmodf(compose->age, 55.0f));
    ComposeFocus mii_focus = compose->focus[WM_COMPOSE_CONTROL_MII];
    bool body_exiting =
        compose->phase == WM_COMPOSE_SEND ||
        (compose->phase == WM_COMPOSE_BACK_MEMO && compose->frame >= 20.0f);
    if (mii_focus.active && !body_exiting) {
        /* The focus clip has a constant 255-alpha track for Nigaoe. During
         * MailOut or SendOut it would override the source icon fade. */
        append_target_clip(clips, &count,
                           mii_focus.entering ? "my_Memo_a_NigaoeFoucusIn"
                                              : "my_Memo_a_NigaoeFoucusOut",
                           "Nigaoe", clamp_frame(mii_focus.frame, 6.0f));
    }
    for (size_t direction = 0; direction < COMPOSE_SCROLL_DIRECTIONS; direction++) {
        const BoardComposeScrollArrow *display =
            &compose->scroll.arrows[COMPOSE_SCROLL_DISPLAY][direction];
        append_clip(clips, &count,
                    display->visible ? "my_Memo_a_Appear" : "my_Memo_a_Lost",
                    display_end_groups[direction],
                    clamp_frame(display->appearance_frame, 10.0f));
        if (display->visible && display->appearance_frame >= 10.0f &&
            display->focus.active) {
            append_clip(clips, &count,
                        display->focus.entering ? "my_Memo_a_FocusOn"
                                                : "my_Memo_a_FocusOff",
                        display_focus_groups[direction],
                        clamp_frame(display->focus.frame, 15.0f));
        }
        if (display->visible && display->press_active) {
            append_clip(clips, &count, "my_Memo_a_Select",
                        display_press_groups[direction],
                        clamp_frame(display->press_frame, 7.0f));
        }

        const BoardComposeScrollArrow *editor =
            &compose->scroll.arrows[COMPOSE_SCROLL_EDITOR][direction];
        if (!editor->appeared)
            continue;
        append_target_clip(
            clips, &count, "my_Memo_a_Fade_IN", editor_panes[direction],
            editor->visible ? clamp_frame(editor->appearance_frame, 11.0f) : 11.0f);
        if (!editor->visible) {
            append_target_clip(clips, &count, "my_Memo_a_Fade_OUT",
                               editor_panes[direction],
                               clamp_frame(editor->appearance_frame, 10.0f));
        } else if (editor->appearance_frame >= 11.0f) {
            if (editor->focus.active) {
                append_target_clip(clips, &count,
                                   editor->focus.entering ? "my_Memo_a_Foucus_IN"
                                                          : "my_Memo_a_Focus-OUT",
                                   editor_panes[direction],
                                   editor->focus.entering
                                       ? 1.0f + clamp_frame(editor->focus.frame, 5.0f)
                                       : clamp_frame(editor->focus.frame, 8.0f));
            }
            if (editor->press_active) {
                append_target_clip(clips, &count, "my_Memo_a_Pushed",
                                   editor_panes[direction],
                                   clamp_frame(editor->press_frame, 7.0f));
            }
        }
    }
    wm_layout_pose(compose->body, clips, count);
    for (size_t direction = 0; direction < COMPOSE_SCROLL_DIRECTIONS; direction++) {
        if (!compose->scroll.arrows[COMPOSE_SCROLL_EDITOR][direction].appeared) {
            wm_layout_set_pane_visible(compose->body, editor_panes[direction], false);
        }
    }
    if (compose->phase == WM_COMPOSE_LEAVE_EDIT) {
        float alpha = 255.0f * (1.0f - clamp_frame(compose->frame, 30.0f) / 30.0f);
        for (size_t direction = 0; direction < COMPOSE_SCROLL_DIRECTIONS; direction++) {
            wm_layout_set_pane_alpha(compose->body, editor_panes[direction], alpha);
        }
    }
    float keyboard_progress = compose->phase == WM_COMPOSE_ENTER_EDIT
                                  ? clamp_frame(compose->frame, 30.0f) / 30.0f
                              : compose->phase == WM_COMPOSE_LEAVE_EDIT
                                  ? 1.0f - clamp_frame(compose->frame, 30.0f) / 30.0f
                              : compose->phase == WM_COMPOSE_EDIT ? 1.0f
                                                                  : 0.0f;
    float keyboard_smooth =
        keyboard_progress * keyboard_progress * (3.0f - 2.0f * keyboard_progress);
    wm_layout_set_pane_translation(compose->body, "N_Memo", 0.0f,
                                   compose->scroll.offset + 145.0f * keyboard_smooth,
                                   0.0f);
    size_t lines = compose->scroll.lines > 4 ? compose->scroll.lines : 4;
    WmLayoutPaneState footer;
    if (wm_layout_pane_state(compose->body, "N_Footer", &footer)) {
        wm_layout_set_pane_translation(
            compose->body, "N_Footer", footer.translation[0],
            footer.translation[1] - (float)(lines - 1) * compose->scroll.line_height,
            footer.translation[2]);
    }
    WmLayoutPaneState text_hit;
    if (wm_layout_pane_state(compose->body, "B_2l_TextBox", &text_hit)) {
        float height = (float)lines * compose->scroll.line_height;
        wm_layout_set_pane_size(compose->body, "B_2l_TextBox", text_hit.size[0],
                                height);
        wm_layout_set_pane_translation(
            compose->body, "B_2l_TextBox", text_hit.translation[0],
            text_hit.translation[1] - (height - text_hit.size[1]) * 0.5f,
            text_hit.translation[2]);
    }
    if (keyboard_progress > 0.0f) {
        WmLayoutPaneState viewport;
        if (wm_layout_pane_state(compose->body, "T_2l_TextBox", &viewport)) {
            float height = 2.0f * compose->scroll.line_height;
            wm_layout_set_pane_size(compose->body, "T_2l_TextBox", viewport.size[0],
                                    height);
            wm_layout_set_pane_translation(
                compose->body, "T_2l_TextBox", viewport.translation[0],
                viewport.translation[1] - (height - viewport.size[1]) * 0.5f -
                    compose->scroll.offset,
                viewport.translation[2]);
        }
    }
    wm_layout_set_pose_text(compose->body, "T_Header", "Memo");
    wm_layout_set_pose_text(compose->body, "T_TouchLetter",
                            compose->draft.text_bytes ? "" : "Write a memo");
    wm_layout_set_pose_text(compose->body, "T_Nigaoe", "\342\206\220Add a Mii");
    wm_layout_set_pose_text(compose->body, "T_Letter",
                            memo_display_text(compose, NULL));
    if (compose->phase == WM_COMPOSE_EDIT) {
        WmBoardKeyboardComposition composition;
        if (wm_board_keyboard_composition(compose->keyboard, &composition) &&
            composition.prefix_bytes <= compose->draft.caret_bytes) {
            size_t start = compose->draft.caret_bytes - composition.prefix_bytes;
            size_t preview_bytes =
                composition.preview_candidate &&
                        strcmp(composition.preview_candidate, ">") != 0
                    ? strlen(composition.preview_candidate)
                    : 0;
            size_t colored_end =
                preview_bytes && start + preview_bytes < compose->draft.caret_bytes
                    ? start + preview_bytes
                    : compose->draft.caret_bytes;
            WmLayoutTextColorRange colors[2] = {
                {start, colored_end, {255, 50, 50, 255}},
                {compose->draft.caret_bytes,
                 start + preview_bytes,
                 {192, 192, 192, 255}}};
            size_t count = 1;
            if (preview_bytes > composition.prefix_bytes) {
                if (composition.preview_hovered) {
                    colors[1].rgba[0] = 50;
                    colors[1].rgba[1] = 100;
                    colors[1].rgba[2] = 50;
                }
                count = 2;
            }
            (void)wm_layout_set_pose_text_colors(compose->body, "T_Letter", colors,
                                                 count);
        } else if (wm_board_keyboard_phone_pending(compose->keyboard) &&
                   compose->draft.caret_bytes > 0) {
            size_t pending = wm_board_keyboard_phone_pending_bytes(compose->keyboard);
            if (pending <= compose->draft.caret_bytes) {
                size_t displayed_end = compose->draft.caret_bytes;
                if (compose->draft.text[compose->draft.caret_bytes - 1] == ' ' &&
                    wm_board_keyboard_phone_space_pending(compose->keyboard))
                    displayed_end += 2; /* U+2423 replaces one byte with three. */
                WmLayoutTextColorRange color = {
                    .first_byte = compose->draft.caret_bytes - pending,
                    .end_byte = displayed_end,
                    .rgba = {255, 50, 50, 255}};
                (void)wm_layout_set_pose_text_colors(compose->body, "T_Letter", &color,
                                                     1);
            }
        }
    }
}

static bool body_row_pane(void *context, const WmLayoutPaneView *pane) {
    (void)context;
    static const char *const names[] = {
        "RootPane",   "N_Memo",     "N_MemoRoot", "N_Body",   "Body_s", "Body3",
        "Picture_11", "Picture_12", "Picture_13", "Body3_04", "B_Body"};
    for (size_t index = 0; index < sizeof(names) / sizeof(names[0]); index++) {
        if (strcmp(pane->name, names[index]) == 0)
            return true;
    }
    return false;
}

static bool body_header_pane(void *context, const WmLayoutPaneView *pane) {
    (void)context;
    static const char *const names[] = {
        "RootPane",   "N_Memo",     "N_MemoRoot", "N_Header",   "Header_s0",
        "Header_s2",  "Picture_07", "Picture_04", "Picture_08", "Picture_09",
        "Picture_05", "Picture_00", "Picture_22", "Picture_10", "Picture_15",
        "Picture_01", "Picture_02", "T_Header"};
    for (size_t index = 0; index < sizeof(names) / sizeof(names[0]); index++) {
        if (strcmp(pane->name, names[index]) == 0)
            return true;
    }
    return false;
}

static bool draw_body_rows(WmBoardCompose *compose) {
    size_t lines = compose->scroll.lines > 4 ? compose->scroll.lines : 4;
    if (lines <= 1)
        return false;
    WmSourceRect rect;
    WmLayoutPaneState body;
    if (!wm_source_pane_rect(compose->body, "N_Body", true, WM_LAYOUT_IPL, NULL,
                             &rect) ||
        !wm_layout_pane_state(compose->body, "N_Body", &body) || rect.height <= 0.0f)
        return false;
    /* The WAD orders its header, original body strip, repeated strips, then
     * footer and text. Drawing the repeated strips first exposes a ruled
     * strip's gray edge, while drawing the header last exposes its shadow. */
    wm_layout_present_filtered_with_fonts(compose->platform, compose->textures,
                                          compose->fonts, compose->body, true,
                                          WM_LAYOUT_IPL, NULL, body_header_pane, NULL);
    wm_layout_present_filtered_with_fonts(compose->platform, compose->textures,
                                          compose->fonts, compose->body, true,
                                          WM_LAYOUT_IPL, NULL, body_row_pane, NULL);
    float first_float = floorf((-rect.y - rect.height) / rect.height) - 1.0f;
    float last_float = ceilf((456.0f - rect.y) / rect.height) + 1.0f;
    if (last_float < 1.0f || first_float > (float)(lines - 1))
        return true;
    size_t first = first_float > 1.0f ? (size_t)first_float : 1;
    size_t last = last_float < (float)(lines - 1) ? (size_t)last_float : lines - 1;
    for (size_t row = first; row <= last; row++) {
        wm_layout_set_pane_translation(compose->body, "N_Body", body.translation[0],
                                       body.translation[1] -
                                           (float)row * compose->scroll.line_height,
                                       body.translation[2]);
        wm_layout_present_filtered_with_fonts(compose->platform, compose->textures,
                                              compose->fonts, compose->body, true,
                                              WM_LAYOUT_IPL, NULL, body_row_pane, NULL);
    }
    wm_layout_set_pane_translation(compose->body, "N_Body", body.translation[0],
                                   body.translation[1], body.translation[2]);
    return true;
}

typedef struct MemoCaretPane {
    bool visible;
    float matrix[12];
    float alpha;
    float marker_alpha;
    float marker_tint[3];
    WmFontPane font;
    const char *font_name;
} MemoCaretPane;

static bool capture_memo_caret_pane(void *context, const WmLayoutPaneView *pane) {
    MemoCaretPane *caret = context;
    if (strcmp(pane->name, "T_Letter") == 0 && pane->text) {
        caret->visible = pane->alpha > 0.0f;
        memcpy(caret->matrix, pane->matrix, sizeof(caret->matrix));
        caret->alpha = pane->alpha;
        caret->font = pane->text->pane;
        caret->font_name = pane->text->font_name;
        const float *foreground =
            pane->text->material ? pane->text->material->registers[1] : NULL;
        caret->marker_alpha =
            pane->alpha * fminf(1.0f, fmaxf(0.0f, pane->text->colors[0][3] / 255.0f)) *
            (foreground ? fminf(1.0f, fmaxf(0.0f, foreground[3])) : 1.0f);
        for (size_t channel = 0; channel < 3; channel++) {
            caret->marker_tint[channel] =
                foreground ? fminf(1.0f, fmaxf(0.0f, foreground[channel])) : 1.0f;
        }
    }
    return true;
}

static bool capture_memo_caret_without_sheet(void *context,
                                             const WmLayoutPaneView *pane) {
    if (strcmp(pane->name, "N_Header") == 0 || strcmp(pane->name, "N_Body") == 0)
        return false;
    return capture_memo_caret_pane(context, pane);
}

static void draw_memo_caret(WmBoardCompose *compose, const MemoCaretPane *pane) {
    if (!pane->visible || !isfinite(compose->keyboard_age))
        return;
    WmCachedFont *face = wm_font_cache_resolve(compose->fonts, pane->font_name);
    size_t display_bytes = 0;
    const char *display = memo_display_text(compose, &display_bytes);
    const WmFontTextLayout *layout =
        face ? wm_font_cache_layout(face, display, &pane->font) : NULL;
    float position_x, position_y;
    if (!layout ||
        !wm_font_text_layout_caret(layout, display_bytes, &position_x, &position_y)) {
        return;
    }
    float height = fmaxf(0.0f, pane->font.font_size[1] - 4.0f);
    if (height <= 0.0f)
        return;

    /* Base::drawCursor uses a centered source-space strip and a 45-update
     * sine pulse. The layout matrix is the same one used for its glyphs. */
    const float width = (float)(14592 / 832) / 6.0f;
    const float radians =
        fmodf(compose->keyboard_age, 45.0f) * (8.0f * 3.14159265358979323846f / 180.0f);
    const float opacity =
        floorf(127.0f * (1.0f + sinf(radians))) / 255.0f * pane->alpha;
    const float xs[4] = {position_x - width * 0.5f, position_x + width * 0.5f,
                         position_x - width * 0.5f, position_x + width * 0.5f};
    const float ys[4] = {position_y - 2.0f, position_y - 2.0f,
                         position_y - 2.0f - height, position_y - 2.0f - height};
    WmDrawVertex vertices[4] = {0};
    for (size_t index = 0; index < 4; index++) {
        float world_x =
            pane->matrix[0] * xs[index] + pane->matrix[1] * ys[index] + pane->matrix[3];
        float world_y =
            pane->matrix[4] * xs[index] + pane->matrix[5] * ys[index] + pane->matrix[7];
        vertices[index].x =
            WM_FRAME_WIDTH * 0.5f + world_x * (float)WM_FRAME_WIDTH / 832.0f;
        vertices[index].y = WM_FRAME_HEIGHT * 0.5f - world_y;
        vertices[index].color =
            (WmColor){1.0f, 50.0f / 255.0f, 50.0f / 255.0f, opacity};
    }
    wm_platform_draw_vertices(compose->platform, vertices, 0);
}

typedef struct MemoLineFeedDraw {
    WmBoardCompose *compose;
    WmCachedFont *face;
    const MemoCaretPane *pane;
} MemoLineFeedDraw;

static bool memo_line_feed_sheet(void *context, size_t sheet, uint32_t *texture) {
    MemoLineFeedDraw *draw = context;
    return wm_font_cache_sheet(draw->face, sheet, texture);
}

static void memo_line_feed_quad(void *context, const WmFontQuad *quad) {
    MemoLineFeedDraw *draw = context;
    if (!quad || !quad->texture)
        return;
    WmDrawVertex vertices[4];
    for (size_t index = 0; index < 4; index++) {
        const WmFontVertex *source = &quad->vertices[index];
        vertices[index] = (WmDrawVertex){
            .x = WM_FRAME_WIDTH * 0.5f +
                 source->position[0] * (float)WM_FRAME_WIDTH / 832.0f,
            .y = WM_FRAME_HEIGHT * 0.5f - source->position[1],
            .u = source->uv[0],
            .v = source->uv[1],
            .color = {source->color[0] * draw->pane->marker_tint[0],
                      source->color[1] * draw->pane->marker_tint[1],
                      source->color[2] * draw->pane->marker_tint[2], source->color[3]}};
    }
    wm_platform_draw_vertices(draw->compose->platform, vertices, quad->texture);
}

static void draw_memo_line_feeds(WmBoardCompose *compose, const MemoCaretPane *pane) {
    if (!pane->visible || pane->marker_alpha <= 0.0f ||
        !strchr(compose->draft.text, '\n'))
        return;
    WmCachedFont *face = wm_font_cache_resolve(compose->fonts, pane->font_name);
    const WmFont *font = face ? wm_cached_font_resource(face) : NULL;
    const WmFontTextLayout *layout =
        font ? wm_font_cache_layout(face, compose->draft.text, &pane->font) : NULL;
    if (!layout || !wm_font_glyph(font, 0xe056))
        return;
    MemoLineFeedDraw draw = {compose, face, pane};
    WmFontDrawOptions options = {
        .size = {pane->font.font_size[0], pane->font.font_size[1]},
        .alpha = pane->marker_alpha,
        .align = WM_FONT_ALIGN_LEFT,
        .matrix = pane->matrix,
        .sheet_provider = memo_line_feed_sheet,
        .on_quad = memo_line_feed_quad,
        .context = &draw};
    for (size_t channel = 0; channel < 4; channel++) {
        options.top_color[channel] = channel == 3 ? 255 : 200;
        options.bottom_color[channel] = options.top_color[channel];
    }
    for (size_t index = 0; index < compose->draft.text_bytes; index++) {
        if (compose->draft.text[index] != '\n' ||
            !wm_font_text_layout_caret(layout, index, &options.x, &options.y))
            continue;
        /* Keep each draft newline's E056 display marker with its text during
         * keyboard motion and after dismissal. It adds no stored character. */
        wm_font_emit_line(font, "\xee\x81\x96", &options);
    }
}

static float footer_scene_frame(const WmBoardCompose *compose) {
    switch (compose->phase) {
        case WM_COMPOSE_ENTER_SELECTOR:
            return compose->frame < 26.0f
                       ? 4000.0f + compose->frame
                       : 3113.0f + clamp_frame(compose->frame - 26.0f, 13.0f);
        case WM_COMPOSE_SELECTOR:
            return 3126.0f;
        case WM_COMPOSE_ENTER_MEMO:
            return compose->frame < 13.0f
                       ? 3213.0f + compose->frame
                       : 3313.0f + clamp_frame(compose->frame - 13.0f, 13.0f);
        case WM_COMPOSE_MEMO:
        case WM_COMPOSE_POST_PRESS:
            return 3326.0f;
        case WM_COMPOSE_ENTER_EDIT:
            return 3413.0f + clamp_frame(compose->frame, 13.0f);
        case WM_COMPOSE_EDIT:
            return 3426.0f;
        case WM_COMPOSE_LEAVE_EDIT:
            return 3313.0f + clamp_frame(compose->frame, 13.0f);
        case WM_COMPOSE_SEND:
            return 3413.0f + clamp_frame(compose->frame, 13.0f);
        case WM_COMPOSE_EXIT_AFTER_POST:
            return 3426.0f + clamp_frame(compose->frame, 13.0f);
        case WM_COMPOSE_BACK_MEMO:
            if (compose->frame < 20.0f)
                return 3326.0f;
            if (compose->frame < 33.0f)
                return 3413.0f + compose->frame - 20.0f;
            return 3113.0f + clamp_frame(compose->frame - 33.0f, 13.0f);
        case WM_COMPOSE_BACK_SELECTOR:
            return compose->frame < 20.0f
                       ? 3213.0f + clamp_frame(compose->frame, 13.0f)
                       : 3426.0f + clamp_frame(compose->frame - 33.0f, 13.0f);
        case WM_COMPOSE_ADDRESS: {
            WmBoardAddressPhase phase = wm_board_address_phase(compose->address);
            float frame = wm_board_address_phase_frame(compose->address);
            if (phase == WM_BOARD_ADDRESS_ENTER) {
                return frame < 13.0f ? 3213.0f + frame
                                     : 3313.0f + clamp_frame(frame - 13.0f, 13.0f);
            }
            if (phase == WM_BOARD_ADDRESS_EXIT) {
                if (frame < 21.0f)
                    return 3326.0f;
                if (frame < 35.0f)
                    return 3413.0f + clamp_frame(frame - 21.0f, 13.0f);
                return 3113.0f + clamp_frame(frame - 35.0f, 13.0f);
            }
            return 3326.0f;
        }
        case WM_COMPOSE_CLOSED:
            return 1040.0f;
    }
    return 1040.0f;
}

void board_compose_pose_footer(WmBoardCompose *compose) {
    WmLayoutClip clips[COMPOSE_CLIP_CAPACITY];
    size_t count = 0;
    append_clip(clips, &count, "my_IplTop_e", "G_SeenChange",
                footer_scene_frame(compose));
    append_clip(clips, &count, "my_IplTop_e", "G_ArwRoop",
                10000.0f + fmodf(compose->age, 55.0f));
    if (compose->phase == WM_COMPOSE_ENTER_SELECTOR) {
        float frame = clamp_frame(compose->frame, 10.0f);
        append_clip(clips, &count, "my_IplTop_e", "G_ArwL_End", 10100.0f + frame);
        append_clip(clips, &count, "my_IplTop_e", "G_ArwR_End", 10100.0f + frame);
    } else if (compose->phase == WM_COMPOSE_ADDRESS) {
        WmBoardAddressPhase phase = wm_board_address_phase(compose->address);
        float frame = wm_board_address_phase_frame(compose->address);
        float arrow_frame =
            phase == WM_BOARD_ADDRESS_ENTER
                ? 10150.0f + clamp_frame(frame - 1.0f, 10.0f)
            : phase == WM_BOARD_ADDRESS_EXIT || phase == WM_BOARD_ADDRESS_BOOK_TO_KIND
                ? 10100.0f + clamp_frame(frame, 10.0f)
            : phase == WM_BOARD_ADDRESS_BOOK_RETURN
                ? 10150.0f + clamp_frame(frame, 10.0f)
            : wm_board_address_step(compose->address) == WM_BOARD_ADDRESS_STEP_BOOK
                ? 10160.0f
                : 10110.0f;
        static const char *const ends[] = {"G_ArwL_End", "G_ArwR_End"};
        static const char *const focus_groups[] = {"G_ArwL_Focus", "G_ArwR_Focus"};
        static const char *const press_groups[] = {"G_ArwL_Ac", "G_ArwR_Ac"};
        for (size_t side = 0; side < 2; side++) {
            append_clip(clips, &count, "my_IplTop_e", ends[side], arrow_frame);
            ComposeFocus focus =
                compose->focus[side ? WM_COMPOSE_CONTROL_ADDRESS_NEXT
                                    : WM_COMPOSE_CONTROL_ADDRESS_PREVIOUS];
            append_clip(clips, &count, "my_IplTop_e", focus_groups[side],
                        (focus.active && !focus.entering ? 10800.0f : 10600.0f) +
                            clamp_frame(focus.frame, 15.0f));
            if (compose->address_arrow_press[side] >= 0.0f) {
                append_clip(clips, &count, "my_IplTop_e", press_groups[side],
                            10700.0f +
                                clamp_frame(compose->address_arrow_press[side], 30.0f));
            }
        }
    } else {
        append_clip(clips, &count, "my_IplTop_e", "G_ArwL_End", 10110.0f);
        append_clip(clips, &count, "my_IplTop_e", "G_ArwR_End", 10110.0f);
    }
    const WmBoardComposeControl controls[2] = {WM_COMPOSE_CONTROL_BACK,
                                               WM_COMPOSE_CONTROL_POST};
    const char *const groups[2] = {"G_CalExit", "G_Cmn_R"};
    for (size_t index = 0; index < 2; index++) {
        ComposeFocus focus = compose->focus[controls[index]];
        if (!focus.active)
            continue;
        append_clip(clips, &count, "my_IplTop_e", groups[index],
                    (focus.entering ? 2900.0f : 2930.0f) +
                        clamp_frame(focus.frame, focus.entering ? 6.0f : 8.0f));
    }
    if (compose->phase == WM_COMPOSE_POST_PRESS) {
        append_clip(clips, &count, "my_IplTop_e", "G_Cmn_R",
                    3000.0f + clamp_frame(compose->frame, 20.0f));
    }
    if (compose->phase == WM_COMPOSE_ADDRESS &&
        wm_board_address_phase(compose->address) == WM_BOARD_ADDRESS_REGISTER_PRESS) {
        append_clip(
            clips, &count, "my_IplTop_e", "G_Cmn_R",
            3000.0f +
                clamp_frame(wm_board_address_phase_frame(compose->address), 20.0f));
    }
    if (compose->phase == WM_COMPOSE_ADDRESS &&
        (wm_board_address_phase(compose->address) == WM_BOARD_ADDRESS_FORM_OK_PRESS ||
         wm_board_address_phase(compose->address) ==
             WM_BOARD_ADDRESS_NICKNAME_OK_PRESS ||
         wm_board_address_phase(compose->address) == WM_BOARD_ADDRESS_MII_OK_PRESS)) {
        append_clip(
            clips, &count, "my_IplTop_e", "G_Cmn_R",
            3000.0f +
                clamp_frame(wm_board_address_phase_frame(compose->address), 20.0f));
    }
    if ((compose->phase == WM_COMPOSE_BACK_MEMO && compose->frame < 20.0f) ||
        compose->phase == WM_COMPOSE_BACK_SELECTOR) {
        /* HTML's Memo Back press is committed when its 20-frame first
         * stage ends. The following MailOut stage restores the neutral
         * button pose while the selector returns. Holding frame 3020
         * retains its 1.1-scale press track until the entire exit ends. */
        append_clip(clips, &count, "my_IplTop_e", "G_CalExit",
                    3000.0f + clamp_frame(compose->frame, 20.0f));
    }
    if (compose->phase == WM_COMPOSE_ADDRESS &&
        wm_board_address_phase(compose->address) == WM_BOARD_ADDRESS_EXIT) {
        append_clip(
            clips, &count, "my_IplTop_e", "G_CalExit",
            3000.0f +
                clamp_frame(wm_board_address_phase_frame(compose->address), 20.0f));
    }
    wm_layout_pose(compose->footer, clips, count);
    wm_layout_set_pose_text(compose->footer, "T_BbsMark1", "");
    wm_layout_set_pose_text(compose->footer, "T_CalExit", "Back");
    wm_layout_set_pose_text(compose->footer, "T_Add", "Back");
    const char *right_label = "Post";
    if (compose->phase == WM_COMPOSE_ADDRESS) {
        WmBoardAddressStep step = wm_board_address_step(compose->address);
        right_label = step == WM_BOARD_ADDRESS_STEP_BOOK ? "Register"
                      : step == WM_BOARD_ADDRESS_STEP_FORM ||
                              step == WM_BOARD_ADDRESS_STEP_NICKNAME ||
                              step == WM_BOARD_ADDRESS_STEP_MII ||
                              step == WM_BOARD_ADDRESS_STEP_REVIEW
                          ? "OK"
                          : "";
    }
    wm_layout_set_pose_text(compose->footer, "T_CalAdd_R", right_label);
    wm_layout_set_pose_text(compose->footer, "T_Dust", "");
}

static void draw_network_dialog(WmBoardCompose *compose) {
    if (compose->network_phase == COMPOSE_NETWORK_CLOSED)
        return;
    WmLayoutClip clips[COMPOSE_CLIP_CAPACITY];
    size_t count = 0;
    append_clip(clips, &count, "my_DialogWindow_a2_DialogIn", "G_InOut",
                compose->network_phase == COMPOSE_NETWORK_ENTER ? compose->network_frame
                                                                : 25.0f);
    static const WmBoardComposeControl controls[2] = {
        WM_COMPOSE_CONTROL_NETWORK_QUIT, WM_COMPOSE_CONTROL_NETWORK_SETTINGS};
    static const char *const focus_groups[2] = {"G_FocusBtnA", "G_FocusBtnB"};
    for (size_t index = 0; index < 2; index++) {
        ComposeFocus focus = compose->focus[controls[index]];
        if (!focus.active)
            continue;
        append_clip(clips, &count,
                    focus.entering ? "my_DialogWindow_a2_FocusBtn_on"
                                   : "my_DialogWindow_a2_FocusBtn_off",
                    focus_groups[index], clamp_frame(focus.frame, 6.0f));
    }
    if (compose->network_selected != WM_COMPOSE_CONTROL_NONE) {
        append_clip(clips, &count, "my_DialogWindow_a2_SelectBtn_Ac",
                    compose->network_selected == WM_COMPOSE_CONTROL_NETWORK_QUIT
                        ? "G_SelectBtnA"
                        : "G_SelectBtnB",
                    compose->network_phase == COMPOSE_NETWORK_SELECT
                        ? compose->network_frame
                        : 20.0f);
    }
    if (compose->network_phase == COMPOSE_NETWORK_EXIT)
        append_clip(clips, &count, "my_DialogWindow_a2_DialogOut", "G_InOut",
                    compose->network_frame);
    wm_layout_pose(compose->network_dialog, clips, count);
    wm_layout_set_pose_text(compose->network_dialog, "T_Dialog",
                            wm_board_compose_network_message(compose));
    wm_layout_set_pose_text(compose->network_dialog, "T_BtnA", "Quit");
    wm_layout_set_pose_text(compose->network_dialog, "T_BtnB",
                            compose->network_wii_connect24 ? "Enter Settings"
                                                           : "Settings");
    wm_layout_present_with_fonts(compose->platform, compose->textures, compose->fonts,
                                 compose->network_dialog, true, WM_LAYOUT_IPL, NULL);
}

void wm_board_compose_draw(WmBoardCompose *compose) {
    if (!compose || compose->phase == WM_COMPOSE_CLOSED)
        return;
    bool dialog_active = wm_board_address_dialog_active(compose->address);
    bool memo_notice = dialog_active && compose->phase != WM_COMPOSE_ADDRESS;
    board_compose_pose_selector(compose);
    wm_layout_present_with_fonts(compose->platform, compose->textures, compose->fonts,
                                 compose->selector, true, WM_LAYOUT_IPL, NULL);
    if (compose->phase == WM_COMPOSE_ADDRESS)
        wm_board_address_draw(compose->address);
    if (body_visible(compose)) {
        board_compose_pose_body(compose);
        bool body_drawn = draw_body_rows(compose);
        MemoCaretPane caret = {0};
        wm_layout_present_filtered_with_fonts(
            compose->platform, compose->textures, compose->fonts, compose->body, true,
            WM_LAYOUT_IPL, NULL,
            body_drawn ? capture_memo_caret_without_sheet : capture_memo_caret_pane,
            &caret);
        draw_memo_line_feeds(compose, &caret);
        if (compose->phase == WM_COMPOSE_ENTER_EDIT ||
            compose->phase == WM_COMPOSE_EDIT) {
            draw_memo_caret(compose, &caret);
        }
    }
    if (!dialog_active || memo_notice) {
        /* The source modal shade dims the Memo footer, but Back and Post
         * remain drawn under it. Input still belongs only to the notice. */
        board_compose_pose_footer(compose);
        wm_layout_present_with_fonts(compose->platform, compose->textures,
                                     compose->fonts, compose->footer, true,
                                     WM_LAYOUT_IPL, NULL);
    }
    if (compose->phase == WM_COMPOSE_ENTER_EDIT || compose->phase == WM_COMPOSE_EDIT ||
        compose->phase == WM_COMPOSE_LEAVE_EDIT) {
        float progress = compose->phase == WM_COMPOSE_ENTER_EDIT
                             ? clamp_frame(compose->frame, 30.0f) / 30.0f
                         : compose->phase == WM_COMPOSE_LEAVE_EDIT
                             ? 1.0f - clamp_frame(compose->frame, 30.0f) / 30.0f
                             : 1.0f;
        wm_board_keyboard_draw(compose->keyboard, progress,
                               compose->phase == WM_COMPOSE_ENTER_EDIT);
    }
    if (compose->address_keyboard_open)
        wm_board_keyboard_draw(compose->keyboard, 1.0f, false);
    if (dialog_active)
        wm_board_address_draw_dialog(compose->address);
    draw_network_dialog(compose);
}

bool board_compose_hit_pane(const WmLayout *layout, const char *pane, int x, int y) {
    WmSourceRect rect;
    return wm_source_pane_rect(layout, pane, true, WM_LAYOUT_IPL, NULL, &rect) &&
           (float)x >= rect.x && (float)x < rect.x + rect.width && (float)y >= rect.y &&
           (float)y < rect.y + rect.height;
}

bool board_compose_hit_memo_caret(WmBoardCompose *compose, int x, int y) {
    const char *hit_name =
        compose->phase == WM_COMPOSE_EDIT ? "T_2l_TextBox" : "B_2l_TextBox";
    if (y < 0 || y >= 456 || !board_compose_hit_pane(compose->body, hit_name, x, y))
        return false;
    MemoCaretPane pane = {0};
    WmLayoutDrawOptions options = {.wide = true,
                                   .mode = WM_LAYOUT_IPL,
                                   .alpha = 1.0f,
                                   .on_pane = capture_memo_caret_pane,
                                   .context = &pane};
    wm_layout_draw(compose->body, &options);
    if (!pane.visible)
        return false;
    float determinant =
        pane.matrix[0] * pane.matrix[5] - pane.matrix[1] * pane.matrix[4];
    if (!isfinite(determinant) || fabsf(determinant) < 0.000001f)
        return false;
    float projected_x =
        ((float)x - WM_FRAME_WIDTH * 0.5f) * 832.0f / WM_FRAME_WIDTH - pane.matrix[3];
    float projected_y = WM_FRAME_HEIGHT * 0.5f - (float)y - pane.matrix[7];
    float local_x =
        (pane.matrix[5] * projected_x - pane.matrix[1] * projected_y) / determinant;
    float local_y =
        (pane.matrix[0] * projected_y - pane.matrix[4] * projected_x) / determinant;
    WmCachedFont *face = wm_font_cache_resolve(compose->fonts, pane.font_name);
    const char *display = memo_display_text(compose, NULL);
    const WmFontTextLayout *layout =
        face ? wm_font_cache_layout(face, display, &pane.font) : NULL;
    size_t selected;
    if (!wm_font_text_layout_hit_caret(layout, local_x, local_y, &selected))
        return false;
    WmBoardKeyboardComposition composition;
    if (compose->phase == WM_COMPOSE_EDIT &&
        wm_board_keyboard_composition(compose->keyboard, &composition) &&
        composition.prefix_bytes <= compose->draft.caret_bytes &&
        composition.preview_candidate &&
        strcmp(composition.preview_candidate, ">") != 0) {
        size_t start = compose->draft.caret_bytes - composition.prefix_bytes;
        size_t preview_end = start + strlen(composition.preview_candidate);
        if (selected >= preview_end) {
            selected = compose->draft.caret_bytes + selected - preview_end;
        } else if (selected > start) {
            selected = compose->draft.caret_bytes;
        }
    } else if (compose->phase == WM_COMPOSE_EDIT && compose->draft.caret_bytes > 0 &&
               wm_board_keyboard_phone_space_pending(compose->keyboard)) {
        size_t marker_end = compose->draft.caret_bytes + 2;
        if (selected >= marker_end)
            selected -= 2;
        else if (selected >= compose->draft.caret_bytes) {
            selected = compose->draft.caret_bytes;
        }
    }
    compose->draft.pointer_caret_bytes =
        selected < compose->draft.text_bytes ? selected : compose->draft.text_bytes;
    return true;
}
