#include "board_keyboard_internal.h"

#include "wii_menu/layout/layout_present.h"
#include "wii_menu/input/source_hit.h"

#include <math.h>
#include <stdio.h>
#include <string.h>

enum {
    KEYBOARD_CLIP_CAPACITY = 2 * WM_KEYBOARD_CONTROL_LAST + 12
};

static const char *const phone_modes[4] = {"Abc", "abc", "ABC", "123"};

static const char *const language_names[3] = {
    "English", "Français", "Español"
};

static const char *const language_short[3] = {"Eng", "Fra", "Esp"};

static float candidate_offset(const WmBoardKeyboard *keyboard) {
    if (keyboard->candidate_count == 0) return 0.0f;
    float from = keyboard->candidate_positions[keyboard->candidate_first];
    if (!keyboard->candidate_scrolling) return from;
    float to = keyboard->candidate_positions[keyboard->candidate_target];
    float t = fminf(keyboard->candidate_scroll_frame, 15.0f) / 15.0f;
    t = t * t * (3.0f - 2.0f * t);
    return from + (to - from) * t;
}

static const char *picture_name(WmBoardKeyboardControl control,
                                char name[24], bool phone_layout) {
    if (control >= WM_KEYBOARD_CHARACTER_FIRST &&
        control <= WM_KEYBOARD_CHARACTER_LAST) {
        snprintf(name, 24, "P_key_%02u",
                 (unsigned)control - WM_KEYBOARD_CHARACTER_FIRST);
        return name;
    }
    if (control >= WM_KEYBOARD_SYMBOL_FIRST &&
        control <= WM_KEYBOARD_SYMBOL_LAST) {
        snprintf(name, 24, "P_SGNkey_%02u",
                 (unsigned)control - WM_KEYBOARD_SYMBOL_FIRST);
        return name;
    }
    if (control >= WM_KEYBOARD_PHONE_FIRST &&
        control <= WM_KEYBOARD_PHONE_LAST) {
        snprintf(name, 24, "W_CPkey_%02u",
                 (unsigned)control - WM_KEYBOARD_PHONE_FIRST);
        return name;
    }
    if (control >= WM_KEYBOARD_PHONE_MODE_FIRST &&
        control <= WM_KEYBOARD_PHONE_MODE_LAST) {
        snprintf(name, 24, "W_ChngTag_%02u",
                 (unsigned)control - WM_KEYBOARD_PHONE_MODE_FIRST);
        return name;
    }
    if (is_language_choice(control)) {
        static const char *const names[3] = {
            "P_PRDC_US_US", "P_PRDC_US_Fre", "P_PRDC_US_Spa"
        };
        return names[control - WM_KEYBOARD_LANGUAGE_ENGLISH];
    }
    if (control >= WM_KEYBOARD_CANDIDATE_FIRST &&
        control <= WM_KEYBOARD_CANDIDATE_LAST) {
        snprintf(name, 24, "T_prdc_Text_%02u",
                 (unsigned)control - WM_KEYBOARD_CANDIDATE_FIRST);
        return name;
    }
    if (control == WM_KEYBOARD_CANDIDATE_PREVIOUS)
        return "P_prdc_scrl_Left";
    if (control == WM_KEYBOARD_CANDIDATE_NEXT)
        return "P_prdc_scrl_Rght";
    switch (control) {
        case WM_KEYBOARD_DELETE:
            return phone_layout ? "W_CPkey_DELETE" : "P_key_DELETE";
        case WM_KEYBOARD_RETURN:
            return phone_layout ? "W_CPkey_LF" : "P_key_LF";
        case WM_KEYBOARD_CAPS: return "P_key_CAPS";
        case WM_KEYBOARD_SHIFT: return "P_key_SHIFT";
        case WM_KEYBOARD_SPACE: return "P_key_SPACE";
        case WM_KEYBOARD_BACK: return "P_BT_cancel";
        case WM_KEYBOARD_OK: return "P_BT_confirm";
        case WM_KEYBOARD_MORE:
            return phone_layout ? "W_othersBT_EU" :
                                  "W_USEU_Chng_sign";
        case WM_KEYBOARD_QWERTY: return "P_kyChng_QWERTY";
        case WM_KEYBOARD_PHONE: return "P_kyChng_CP";
        case WM_KEYBOARD_LANGUAGE:
            return phone_layout ? "W_prdcModeBT_EU" : "W_USEU_prdc_lang";
        case WM_KEYBOARD_PREDICTION: return "P_OffBtn";
        case WM_KEYBOARD_SYMBOL_CLOSE: return "P_SGNkey_close";
        case WM_KEYBOARD_SYMBOL_PREV: return "P_SGNkey_prev";
        case WM_KEYBOARD_SYMBOL_NEXT: return "P_SGNkey_next";
        case WM_KEYBOARD_NONE:
        case WM_KEYBOARD_CHARACTER_FIRST:
        case WM_KEYBOARD_CHARACTER_LAST:
        case WM_KEYBOARD_SYMBOL_FIRST:
        case WM_KEYBOARD_SYMBOL_LAST:
        case WM_KEYBOARD_PHONE_FIRST:
        case WM_KEYBOARD_PHONE_LAST:
        case WM_KEYBOARD_PHONE_MODE_FIRST:
        case WM_KEYBOARD_PHONE_MODE_LAST:
        case WM_KEYBOARD_LANGUAGE_ENGLISH:
        case WM_KEYBOARD_LANGUAGE_FRENCH:
        case WM_KEYBOARD_LANGUAGE_SPANISH:
        case WM_KEYBOARD_CANDIDATE_FIRST:
        case WM_KEYBOARD_CANDIDATE_LAST:
        case WM_KEYBOARD_CANDIDATE_PREVIOUS:
        case WM_KEYBOARD_CANDIDATE_NEXT:
            break;
    }
    return NULL;
}

static void append_clip(WmLayoutClip clips[KEYBOARD_CLIP_CAPACITY],
                        size_t *count, const char *animation,
                        const char *target, float frame) {
    if (*count >= KEYBOARD_CLIP_CAPACITY) return;
    clips[(*count)++] = (WmLayoutClip){
        .animation = animation,
        .target_name = target,
        .frame = frame,
        .loop_override = 0
    };
}

static void append_rebound_clip(WmLayoutClip clips[KEYBOARD_CLIP_CAPACITY],
                                size_t *count, const char *animation,
                                const char *prototype,
                                const char *destination, float frame) {
    if (*count >= KEYBOARD_CLIP_CAPACITY) return;
    clips[(*count)++] = (WmLayoutClip){
        .animation = animation,
        .target_name = prototype,
        .rebind_name = destination,
        .frame = frame,
        .loop_override = 0
    };
}

static void append_press_exit(WmLayoutClip clips[KEYBOARD_CLIP_CAPACITY],
                               size_t *count, const WmBoardKeyboard *keyboard,
                               WmBoardKeyboardControl control,
                               const char *animation, const char *prototype,
                               const char *target) {
    const KeyboardFocus *focus = &keyboard->focus[control];
    if (!focus->active || !focus->from_press || focus->resting ||
        *count >= KEYBOARD_CLIP_CAPACITY) return;
    float duration = is_prediction_control(control) ? 7.0f : 8.0f;
    /* The native state handler starts OUT immediately on pointer leave.
     * Follow its authored curve, blending from the retained click sample to
     * avoid a discontinuity at OUT's enlarged first frame. */
    append_rebound_clip(clips, count, animation, prototype, target, focus->frame);
    float progress = fminf(1.0f, focus->frame / duration);
    clips[*count - 1].blend_from_current = true;
    clips[*count - 1].weight = progress * progress * (3.0f - 2.0f * progress);
}

static const char *focus_prototype(WmBoardKeyboardControl control,
                                   const char *picture, bool phone_layout) {
    if (control >= WM_KEYBOARD_CHARACTER_FIRST &&
        control <= WM_KEYBOARD_CHARACTER_LAST) return "P_key_00";
    if (control == WM_KEYBOARD_OK) return "P_BT_cancel";
    if (control == WM_KEYBOARD_PHONE) return "P_kyChng_QWERTY";
    if (control >= WM_KEYBOARD_PHONE_FIRST &&
        control <= WM_KEYBOARD_PHONE_LAST) return "W_CPkey_00";
    if (control >= WM_KEYBOARD_PHONE_MODE_FIRST &&
        control <= WM_KEYBOARD_PHONE_MODE_LAST) return "W_ChngTag_00";
    if (phone_layout && (control == WM_KEYBOARD_DELETE ||
                         control == WM_KEYBOARD_RETURN)) return "W_CPkey_00";
    if (control >= WM_KEYBOARD_SYMBOL_FIRST &&
        control <= WM_KEYBOARD_SYMBOL_LAST) return "P_SGNkey_00";
    if (is_language_choice(control)) return "P_PRDC_US_US";
    if (control == WM_KEYBOARD_CANDIDATE_NEXT)
        return "P_prdc_scrl_Left";
    if (control >= WM_KEYBOARD_CANDIDATE_FIRST &&
        control <= WM_KEYBOARD_CANDIDATE_LAST) return "T_prdc_Text_00";
    if (control == WM_KEYBOARD_SYMBOL_PREV ||
        control == WM_KEYBOARD_SYMBOL_NEXT) return "P_SGNkey_close";
    return picture;
}

static void raise_control(WmLayout *layout, WmBoardKeyboardControl control,
                           bool toolbar, bool phone_layout) {
    char name[24];
    const char *picture = picture_name(control, name, phone_layout);
    if (!picture) return;
    if (toolbar) {
        /* Raising N_toolBar itself would also move its wide background over
         * the sibling layout selectors. Keep both control groups in place. */
        const char *scope = control == WM_KEYBOARD_BACK ||
                            control == WM_KEYBOARD_OK
            ? "N_toolBar" : "N_keyboardChange";
        (void)wm_layout_raise_pane_within(layout, scope, picture);
    } else {
        (void)wm_layout_raise_pane(layout, picture);
    }
}

static void raise_focused_key(WmBoardKeyboard *keyboard, WmLayout *layout,
                              bool phone_layout) {
    bool toolbar = layout == keyboard->toolbar;
    /* Posed layouts restore source order every time. Keep every enlarged
     * branch above idle neighbors throughout its press and focus exit. */
    for (unsigned index = 1; index <= WM_KEYBOARD_CONTROL_LAST; index++) {
        WmBoardKeyboardControl control = (WmBoardKeyboardControl)index;
        bool belongs = toolbar ? is_toolbar(control) :
            layout == keyboard->symbols ? is_symbol(control) :
            layout == keyboard->language ? is_language_choice(control) :
            layout == keyboard->prediction ? is_prediction_control(control) :
            phone_layout ? is_phone_control(keyboard, control) : is_keytop(control);
        bool physical_focus =
            (control == WM_KEYBOARD_SHIFT &&
             keyboard->physical_shift_focus_frame > 0.0f) ||
            (control == WM_KEYBOARD_CAPS &&
             keyboard->physical_caps_focus_frame > 0.0f);
        if (!belongs || (!keyboard->focus[index].active &&
                         !wm_board_keyboard_press_pose_active(keyboard, index) &&
                         !physical_focus))
            continue;
        raise_control(layout, control, toolbar, phone_layout);
    }
    /* The most recent click has priority over a new pointer hover. */
    const WmBoardKeyboardControl top[] = {keyboard->hovered, keyboard->pressed};
    for (size_t index = 0; index < sizeof(top) / sizeof(top[0]); index++) {
        raise_control(layout, top[index], toolbar, phone_layout);
    }
}

static void pose_controls(WmBoardKeyboard *keyboard, bool toolbar) {
    WmLayout *layout = toolbar ? keyboard->toolbar : keyboard->keytop;
    const char *stem = toolbar ? "fs_VK_toolbar_a_" :
                                 "fs_VK_ascii_keytop_a_";
    char name[80];
    WmLayoutClip clips[KEYBOARD_CLIP_CAPACITY];
    size_t count = 0;
    snprintf(name, sizeof(name), "%snormal", stem);
    append_clip(clips, &count, name, NULL, 0.0f);
    /* Names in clips must outlive wm_layout_pose; use stable constants below. */
    static const char *const keytop_motion[3] = {
        "fs_VK_ascii_keytop_a_Focus-IN",
        "fs_VK_ascii_keytop_a_Focus-OUT",
        "fs_VK_ascii_keytop_a_Pushed"
    };
    static const char *const selected_motion[4] = {
        "fs_VK_ascii_keytop_a_normal_toggle-ON",
        "fs_VK_ascii_keytop_a_toggleON_Focus-IN",
        "fs_VK_ascii_keytop_a_toggleON_Focus-OUT",
        "fs_VK_ascii_keytop_a_toggleON_Pushed"
    };
    static const char *const toolbar_motion[3] = {
        "fs_VK_toolbar_a_Focus-IN",
        "fs_VK_toolbar_a_Focus-OUT",
        "fs_VK_toolbar_a_Pushed"
    };
    const char *const *motion = toolbar ? toolbar_motion : keytop_motion;
    char target_names[WM_KEYBOARD_CONTROL_LAST + 1][24];
    if (toolbar) {
        append_rebound_clip(clips, &count,
                            "fs_VK_toolbar_a_toggle-ON",
                            "P_kyChng_QWERTY",
                            keyboard->phone_layout ? "P_kyChng_CP" :
                                                     "P_kyChng_QWERTY", 0.0f);
    } else {
        if (wm_board_keyboard_caps_active(keyboard)) {
            append_rebound_clip(clips, &count, selected_motion[0],
                                "P_key_CAPS", "P_key_CAPS", 0.0f);
        }
        if (wm_board_keyboard_shift_active(keyboard)) {
            append_rebound_clip(clips, &count, selected_motion[0],
                                "P_key_SHIFT", "P_key_SHIFT", 0.0f);
        }
    }
    for (unsigned index = 1; index <= WM_KEYBOARD_CONTROL_LAST; index++) {
        KeyboardFocus focus = keyboard->focus[index];
        if (!focus.active || wm_board_keyboard_press_pose_active(keyboard, index) ||
            (toolbar ? !is_toolbar((WmBoardKeyboardControl)index) :
                       !is_keytop((WmBoardKeyboardControl)index)) ||
            selected_tab(keyboard, (WmBoardKeyboardControl)index))
            continue;
        const char *target = picture_name((WmBoardKeyboardControl)index,
                                           target_names[index],
                                           keyboard->phone_layout);
        if (!target) continue;
        bool selected = !toolbar &&
            ((index == WM_KEYBOARD_CAPS &&
              wm_board_keyboard_caps_active(keyboard)) ||
             (index == WM_KEYBOARD_SHIFT &&
              wm_board_keyboard_shift_active(keyboard)));
        bool selected_exit = selected && !focus.entering && !focus.resting;
        if (selected_exit) {
            /* The toggle OUT clip starts just past the hover scale and
             * changes its color immediately. Retain the hovered endpoint
             * while blending those source-track discontinuities. */
            append_rebound_clip(clips, &count,
                selected_motion[1], target, target, 5.0f);
        }
        append_rebound_clip(clips, &count,
                    focus.resting
                        ? selected ? selected_motion[1] :
                          toolbar ? "fs_VK_toolbar_a_Roll_over" :
                                    "fs_VK_ascii_keytop_a_Roll_over"
                        : selected ? selected_motion[focus.entering ? 1 : 2] :
                                     motion[focus.entering ? 0 : 1],
                    focus_prototype((WmBoardKeyboardControl)index, target,
                                    keyboard->phone_layout),
                    target, focus.resting && selected ? 5.0f : focus.frame);
        if (selected_exit) {
            float progress = fminf(1.0f, focus.frame / 8.0f);
            clips[count - 1].blend_from_current = true;
            clips[count - 1].weight = progress * progress * (3.0f - 2.0f * progress);
        }
    }
    /* Physical focus progresses through the same clips as pointer focus,
     * then reverses after release without changing pointer ownership. */
    if (!toolbar && keyboard->physical_shift_focus_frame > 0.0f &&
        keyboard->hovered != WM_KEYBOARD_SHIFT) {
        append_rebound_clip(clips, &count,
                            wm_board_keyboard_shift_active(keyboard)
                                ? selected_motion[1] : keytop_motion[0],
                            "P_key_SHIFT", "P_key_SHIFT",
                            keyboard->physical_shift_focus_frame);
    }
    if (!toolbar && keyboard->physical_caps_focus_frame > 0.0f &&
        keyboard->hovered != WM_KEYBOARD_CAPS) {
        append_rebound_clip(clips, &count,
                            wm_board_keyboard_caps_active(keyboard)
                                ? selected_motion[1] : keytop_motion[0],
                            "P_key_CAPS", "P_key_CAPS",
                            keyboard->physical_caps_focus_frame);
    }
    for (unsigned index = 1; index <= WM_KEYBOARD_CONTROL_LAST; index++) {
        WmBoardKeyboardControl control = (WmBoardKeyboardControl)index;
        if (!wm_board_keyboard_press_pose_active(keyboard, index) ||
            (toolbar ? !is_toolbar(control) : !is_keytop(control))) continue;
        const char *target = picture_name(control,
                                           target_names[index],
                                           keyboard->phone_layout);
        bool selected = !toolbar &&
            ((index == WM_KEYBOARD_CAPS &&
              wm_board_keyboard_caps_active(keyboard)) ||
             (index == WM_KEYBOARD_SHIFT &&
              wm_board_keyboard_shift_active(keyboard)));
        if (target) {
            append_rebound_clip(clips, &count,
                                selected ? selected_motion[3] : motion[2],
                                focus_prototype(control, target,
                                                keyboard->phone_layout),
                                target, keyboard->press[index].frame);
            append_press_exit(clips, &count, keyboard, control,
                selected ? selected_motion[2] : motion[1],
                focus_prototype(control, target, keyboard->phone_layout), target);
        }
    }
    wm_layout_pose(layout, clips, count);
}

static void pose_keytop(WmBoardKeyboard *keyboard) {
    if (!keyboard->keytop_dirty) return;
    pose_controls(keyboard, false);
    keyboard->keytop_dirty = false;
    static const char *const hidden[] = {
        "N_KeyChange_JP", "N_VK_grid", "N_modeSelect_all",
        "N_modeSelect_kr", "N_VK_grd_Bnd_ALL", "P_SHIFTMark",
        "P_CAPSMark", "P_key_HENKAN"
    };
    for (size_t index = 0; index < sizeof(hidden) / sizeof(hidden[0]);
         index++) wm_layout_set_pane_visible(keyboard->keytop, hidden[index],
                                              false);
    for (unsigned index = 0; index < 50; index++) {
        char pane[24];
        char value[2] = {wm_board_keyboard_key_character(keyboard, index), '\0'};
        snprintf(pane, sizeof(pane), "T_key_%02u", index);
        wm_layout_set_pose_text(keyboard->keytop, pane, value);
        if (!value[0]) {
            snprintf(pane, sizeof(pane), "P_key_%02u", index);
            wm_layout_set_pane_visible(keyboard->keytop, pane, false);
        }
    }
    wm_layout_set_pose_text(keyboard->keytop, "T_key_CAPS", "Caps");
    wm_layout_set_pose_text(keyboard->keytop, "T_key_SHIFT", "Shift");
    wm_layout_set_pose_text(keyboard->keytop, "T_key_SPACE", "Space");
    wm_layout_set_pose_text(keyboard->keytop, "T_USEU_Chng_sign", "More");
    wm_layout_set_pose_text(keyboard->keytop, "T_USEU_prdc_lang",
                             language_short[keyboard->dictionary_language]);
    wm_layout_set_pane_visible(keyboard->keytop, "P_prdc_ON",
                                keyboard->profile !=
                                    WM_BOARD_KEYBOARD_CONSOLE_NICKNAME &&
                                (keyboard->prediction_animating
                                    ? keyboard->prediction_from :
                                      keyboard->prediction_enabled));
    wm_layout_set_pane_visible(keyboard->keytop, "P_prdc_OFF",
                                keyboard->profile !=
                                    WM_BOARD_KEYBOARD_CONSOLE_NICKNAME &&
                                (keyboard->prediction_animating
                                    ? !keyboard->prediction_from :
                                      !keyboard->prediction_enabled));
    wm_layout_set_pane_visible(keyboard->keytop, "W_USEU_prdc_lang",
                                keyboard->profile == WM_BOARD_KEYBOARD_MEMO);
    if (keyboard->profile == WM_BOARD_KEYBOARD_ADDRESS_WII ||
        keyboard->profile == WM_BOARD_KEYBOARD_ADDRESS_EMAIL ||
        keyboard->profile == WM_BOARD_KEYBOARD_CONSOLE_NICKNAME) {
        wm_layout_set_pane_visible(keyboard->keytop,
                                    "W_USEU_Chng_sign", false);
    }
    if (keyboard->profile != WM_BOARD_KEYBOARD_MEMO) {
        wm_layout_set_pane_visible(keyboard->keytop, "P_key_LF", false);
    }
    raise_focused_key(keyboard, keyboard->keytop, false);
}

static void pose_phone(WmBoardKeyboard *keyboard) {
    if (!keyboard->phone_dirty) return;
    WmLayoutClip clips[KEYBOARD_CLIP_CAPACITY];
    size_t count = 0;
    append_clip(clips, &count, "fs_VK_cellPhone_a_normal", NULL, 0.0f);
    char selected[24];
    snprintf(selected, sizeof(selected), "W_ChngTag_%02u",
             keyboard->phone_mode);
    append_rebound_clip(clips, &count, "fs_VK_cellPhone_a_toggle-ON",
                        "W_ChngTag_00", selected, 0.0f);
    char targets[WM_KEYBOARD_CONTROL_LAST + 1][24];
    for (unsigned index = 1; index <= WM_KEYBOARD_CONTROL_LAST; index++) {
        WmBoardKeyboardControl control = (WmBoardKeyboardControl)index;
        KeyboardFocus focus = keyboard->focus[index];
        if (!is_phone_control(keyboard, control) || !focus.active ||
            wm_board_keyboard_press_pose_active(keyboard, index) ||
            selected_tab(keyboard, control)) continue;
        const char *target = picture_name(control, targets[index], true);
        if (!target) continue;
        append_rebound_clip(clips, &count,
                    focus.resting ? "fs_VK_cellPhone_a_Roll_over" :
                    focus.entering ? "fs_VK_cellPhone_a_Focus-IN" :
                                     "fs_VK_cellPhone_a_Focus-OUT",
                    focus_prototype(control, target, true), target,
                    focus.frame);
    }
    for (unsigned index = 1; index <= WM_KEYBOARD_CONTROL_LAST; index++) {
        WmBoardKeyboardControl control = (WmBoardKeyboardControl)index;
        if (!wm_board_keyboard_press_pose_active(keyboard, index) ||
            !is_phone_control(keyboard, control)) continue;
        const char *target = picture_name(control, targets[index],
                                           true);
        if (target) {
            append_rebound_clip(clips, &count,
                                "fs_VK_cellPhone_a_Pushed",
                                focus_prototype(control, target,
                                                true), target,
                                keyboard->press[index].frame);
            append_press_exit(clips, &count, keyboard, control,
                "fs_VK_cellPhone_a_Focus-OUT",
                focus_prototype(control, target, true), target);
        }
    }
    wm_layout_pose(keyboard->phone, clips, count);
    keyboard->phone_dirty = false;
    static const char *const hidden[] = {
        "N_CP_onlyJP", "W_smlCptChngeBT", "P_CPkey_dakuten",
        "N_prdc_EU_ON"
    };
    for (size_t index = 0; index < sizeof(hidden) / sizeof(hidden[0]);
         index++) wm_layout_set_pane_visible(keyboard->phone, hidden[index],
                                              false);
    wm_layout_set_pane_visible(keyboard->phone, "N_CP_onlyEU",
                               keyboard->phone_mode != 3);
    wm_layout_set_pane_visible(keyboard->phone, "W_prdcModeBT_EU",
                               keyboard->profile == WM_BOARD_KEYBOARD_MEMO &&
                               keyboard->phone_mode != 3);
    wm_layout_set_pane_visible(keyboard->phone, "N_prdc_EU_OFF",
                                keyboard->profile !=
                                    WM_BOARD_KEYBOARD_CONSOLE_NICKNAME &&
                                (keyboard->prediction_animating
                                    ? !keyboard->prediction_from :
                                      !keyboard->prediction_enabled));
    wm_layout_set_pane_visible(keyboard->phone, "N_prdc_EU_ON",
                                keyboard->profile !=
                                    WM_BOARD_KEYBOARD_CONSOLE_NICKNAME &&
                                (keyboard->prediction_animating
                                    ? keyboard->prediction_from :
                                      keyboard->prediction_enabled));
    wm_layout_set_pane_visible(keyboard->phone, "W_othersBT_EU",
                               keyboard->profile != WM_BOARD_KEYBOARD_ADDRESS_WII &&
                               keyboard->profile != WM_BOARD_KEYBOARD_ADDRESS_EMAIL &&
                               keyboard->profile != WM_BOARD_KEYBOARD_CONSOLE_NICKNAME &&
                               keyboard->phone_mode != 3);
    if (keyboard->profile != WM_BOARD_KEYBOARD_MEMO) {
        wm_layout_set_pane_visible(keyboard->phone, "W_CPkey_LF", false);
    }
    if (keyboard->profile == WM_BOARD_KEYBOARD_ADDRESS_WII) {
        for (unsigned index = 0; index < 4; index++) {
            char pane[24];
            snprintf(pane, sizeof(pane), "W_ChngTag_%02u", index);
            wm_layout_set_pane_visible(keyboard->phone, pane, false);
        }
    }
    wm_layout_set_pose_text(keyboard->phone, "T_othersBT_EU", "More");
    wm_layout_set_pose_text(keyboard->phone, "N_prdc_EU_lang",
                             language_short[keyboard->dictionary_language]);
    for (unsigned index = 0; index < 12; index++) {
        char pane[24];
        char label_buffer[16];
        const char *label = wm_board_keyboard_phone_label(keyboard, index, label_buffer);
        snprintf(pane, sizeof(pane), "T_CPkey_%02u", index);
        wm_layout_set_pose_text(keyboard->phone, pane, label);
        snprintf(pane, sizeof(pane), "W_CPkey_%02u", index);
        wm_layout_set_pane_visible(keyboard->phone, pane, label[0] != '\0');
    }
    for (unsigned index = 0; index < 4; index++) {
        char pane[24];
        snprintf(pane, sizeof(pane), "T_ChngTag_%02u", index);
        wm_layout_set_pose_text(keyboard->phone, pane, phone_modes[index]);
    }
    raise_focused_key(keyboard, keyboard->phone, true);
}

static void pose_toolbar(WmBoardKeyboard *keyboard) {
    if (!keyboard->toolbar_dirty) return;
    pose_controls(keyboard, true);
    keyboard->toolbar_dirty = false;
    wm_layout_set_pose_text(keyboard->toolbar, "T_BT_cancel",
                              keyboard->profile == WM_BOARD_KEYBOARD_MEMO
                                  ? "Back" : "Quit");
    wm_layout_set_pose_text(keyboard->toolbar, "T_BT_confirm", "OK");
    wm_layout_set_pane_visible(keyboard->toolbar, "N_keyboardChange",
        keyboard->profile == WM_BOARD_KEYBOARD_MEMO ||
        keyboard->profile == WM_BOARD_KEYBOARD_ADDRESS_NICKNAME ||
        keyboard->profile == WM_BOARD_KEYBOARD_CONSOLE_NICKNAME);
    raise_focused_key(keyboard, keyboard->toolbar, false);
}

void wm_board_keyboard_pose_prediction(WmBoardKeyboard *keyboard) {
    if (!keyboard->prediction_dirty) return;
    WmLayoutClip clips[KEYBOARD_CLIP_CAPACITY];
    size_t count = 0;
    append_clip(clips, &count, "fs_VK_predictInput_a_normal", NULL, 1.0f);
    append_rebound_clip(clips, &count,
        keyboard->prediction_enabled ? "fs_VK_predictInput_a_predict_ON" :
                                       "fs_VK_predictInput_a_Predict_OFF",
        "W_predictWindow", "W_predictWindow",
        keyboard->prediction_animating ? keyboard->prediction_frame : 12.0f);
    KeyboardFocus focus = keyboard->focus[WM_KEYBOARD_PREDICTION];
    const char *button = keyboard->prediction_enabled ? "P_OnBtn" :
                                                         "P_OffBtn";
    if (focus.active &&
        !wm_board_keyboard_press_pose_active(keyboard, WM_KEYBOARD_PREDICTION))
        append_rebound_clip(clips, &count,
        focus.resting ? "fs_VK_predictInput_a_Roll_over" :
        focus.entering ? "fs_VK_predictInput_a_Foucus_IN" :
                         "fs_VK_predictInput_a_Focus_OUT",
        button, button, focus.frame);
    if (wm_board_keyboard_press_pose_active(keyboard, WM_KEYBOARD_PREDICTION)) {
        append_rebound_clip(clips, &count,
            "fs_VK_predictInput_a_OnOffButton_Pushed", button, button,
            keyboard->press[WM_KEYBOARD_PREDICTION].frame);
        append_press_exit(clips, &count, keyboard, WM_KEYBOARD_PREDICTION,
            "fs_VK_predictInput_a_Focus_OUT", button, button);
    }
    /* Clip target names must remain alive until wm_layout_pose consumes them. */
    char candidate_names[CANDIDATE_PANE_COUNT][24];
    for (unsigned index = 0; index < CANDIDATE_PANE_COUNT; index++) {
        WmBoardKeyboardControl control = (WmBoardKeyboardControl)(
            WM_KEYBOARD_CANDIDATE_FIRST + index);
        KeyboardFocus candidate_focus = keyboard->focus[control];
        if (!candidate_focus.active || keyboard->candidate_scrolling ||
            wm_board_keyboard_press_pose_active(keyboard, control)) continue;
        snprintf(candidate_names[index], sizeof(candidate_names[index]),
                 "T_prdc_Text_%02u", index);
        append_rebound_clip(clips, &count,
            candidate_focus.resting ? "fs_VK_predictInput_a_Roll_over" :
            candidate_focus.entering ? "fs_VK_predictInput_a_Foucus_IN" :
                                       "fs_VK_predictInput_a_Focus_OUT",
            "T_prdc_Text_00", candidate_names[index],
            candidate_focus.frame);
    }
    for (unsigned index = 0; index < CANDIDATE_PANE_COUNT; index++) {
        unsigned control = WM_KEYBOARD_CANDIDATE_FIRST + index;
        if (!wm_board_keyboard_press_pose_active(keyboard, control)) continue;
        if (keyboard->candidate_pane_indices[index] <
            keyboard->candidate_count) {
            snprintf(candidate_names[index], sizeof(candidate_names[index]),
                     "T_prdc_Text_%02u", index);
            append_rebound_clip(clips, &count,
                "fs_VK_predictInput_a_Pushed", "T_prdc_Text_00",
                candidate_names[index], keyboard->press[control].frame);
            append_press_exit(clips, &count, keyboard,
                (WmBoardKeyboardControl)control,
                "fs_VK_predictInput_a_Focus_OUT", "T_prdc_Text_00",
                candidate_names[index]);
        }
    }
    static const WmBoardKeyboardControl arrows[2] = {
        WM_KEYBOARD_CANDIDATE_PREVIOUS,
        WM_KEYBOARD_CANDIDATE_NEXT
    };
    for (unsigned index = 0; index < 2; index++) {
        WmBoardKeyboardControl control = arrows[index];
        const char *picture = index == 0 ? "P_prdc_scrl_Left" :
                                           "P_prdc_scrl_Rght";
        KeyboardFocus arrow_focus = keyboard->focus[control];
        if (arrow_focus.active && !wm_board_keyboard_press_pose_active(keyboard, control))
            append_rebound_clip(clips, &count,
            arrow_focus.resting ? "fs_VK_predictInput_a_Roll_over" :
            arrow_focus.entering ? "fs_VK_predictInput_a_Foucus_IN" :
                                   "fs_VK_predictInput_a_Focus_OUT",
            "P_prdc_scrl_Left", picture, arrow_focus.frame);
        if (wm_board_keyboard_press_pose_active(keyboard, control)) {
            append_rebound_clip(clips, &count,
                "fs_VK_predictInput_a_Pushed", "P_prdc_scrl_Left",
                picture, keyboard->press[control].frame);
            append_press_exit(clips, &count, keyboard, control,
                "fs_VK_predictInput_a_Focus_OUT", "P_prdc_scrl_Left", picture);
        }
    }
    wm_layout_pose(keyboard->prediction, clips, count);
    static const char *const hidden[] = {
        "P_JPOffBtn", "P_CNOffBtn", "P_CNOnBtn"
    };
    for (size_t index = 0; index < sizeof(hidden) / sizeof(hidden[0]);
         index++) wm_layout_set_pane_visible(keyboard->prediction,
                                              hidden[index], false);
    wm_layout_set_pane_visible(keyboard->prediction, "P_OnBtn",
                                keyboard->prediction_animating
                                    ? keyboard->prediction_from :
                                      keyboard->prediction_enabled);
    wm_layout_set_pane_visible(keyboard->prediction, "P_OffBtn",
                                keyboard->prediction_animating
                                    ? !keyboard->prediction_from :
                                      !keyboard->prediction_enabled);
    wm_layout_set_pane_visible(keyboard->prediction, "N_prdc_Texts",
                                keyboard->candidate_count > 0);
    wm_layout_set_pane_visible(keyboard->prediction, "P_prdc_scrl_Left",
        keyboard->candidate_first > 0);
    wm_layout_set_pane_visible(keyboard->prediction, "P_prdc_scrl_Rght",
        wm_board_keyboard_candidate_next_index(keyboard) > keyboard->candidate_first);
    float offset = candidate_offset(keyboard);
    const float area_width = 390.0f;
    for (unsigned index = 0; index < CANDIDATE_PANE_COUNT; index++) {
        char text_name[24];
        char bounds_name[24];
        snprintf(text_name, sizeof(text_name), "T_prdc_Text_%02u", index);
        snprintf(bounds_name, sizeof(bounds_name), "B_prdc_Text_%02u", index);
        keyboard->candidate_pane_indices[index] = CANDIDATE_COUNT;
        wm_layout_set_pane_visible(keyboard->prediction, text_name, false);
        wm_layout_set_pane_size(keyboard->prediction, bounds_name, 0.0f,
                                 38.0f);
    }
    for (unsigned index = 0; index < keyboard->candidate_count; index++) {
        float position = keyboard->candidate_positions[index] - offset;
        float screen_width = keyboard->candidate_screen_widths[index];
        float left = fmaxf(0.0f, position);
        float right = fminf(area_width, position + screen_width);
        if (right <= left) continue;
        unsigned slot = index % CANDIDATE_PANE_COUNT;
        char text_name[24];
        char bounds_name[24];
        snprintf(text_name, sizeof(text_name), "T_prdc_Text_%02u", slot);
        snprintf(bounds_name, sizeof(bounds_name), "B_prdc_Text_%02u", slot);
        keyboard->candidate_pane_indices[slot] = index;
        wm_layout_set_pane_visible(keyboard->prediction, text_name, true);
        wm_layout_set_pose_text(keyboard->prediction, text_name,
                                keyboard->candidates[index]);
        wm_layout_set_pane_size(keyboard->prediction, text_name,
                                 keyboard->candidate_widths[index],
                                 38.0f);
        wm_layout_set_pane_translation(keyboard->prediction, text_name,
                                        -477.0f + position +
                                            screen_width * 0.5f, 0.0f, 0.0f);
        wm_layout_set_pane_size(keyboard->prediction, bounds_name,
                                 right - left, 38.0f);
        wm_layout_set_pane_translation(keyboard->prediction, bounds_name,
            -477.0f + (left + right) * 0.5f, 0.0f, 0.0f);
    }
    raise_focused_key(keyboard, keyboard->prediction, false);
    keyboard->prediction_dirty = false;
}

static void pose_language(WmBoardKeyboard *keyboard) {
    if (!keyboard->language_dirty) return;
    WmLayoutClip clips[KEYBOARD_CLIP_CAPACITY];
    size_t count = 0;
    append_clip(clips, &count, "fs_prdicSelWidw_a_PRDC_normal", NULL,
                0.0f);
    append_clip(clips, &count, "fs_prdicSelWidw_a_PRDC_FADE-IN", NULL,
                18.0f);
    if (keyboard->language_phase == LANGUAGE_ENTERING ||
        keyboard->language_phase == LANGUAGE_LEAVING)
        append_clip(clips, &count,
            keyboard->language_phase == LANGUAGE_ENTERING
                ? "fs_prdicSelWidw_a_PRDC_FADE-IN" :
                  "fs_prdicSelWidw_a_PRDC_FADE-OUT",
            NULL, keyboard->language_frame);
    char names[WM_KEYBOARD_CONTROL_LAST + 1][24];
    for (unsigned index = WM_KEYBOARD_LANGUAGE_ENGLISH;
         index <= WM_KEYBOARD_LANGUAGE_SPANISH; index++) {
        KeyboardFocus focus = keyboard->focus[index];
        if (!focus.active || wm_board_keyboard_press_pose_active(keyboard, index))
            continue;
        WmBoardKeyboardControl control = (WmBoardKeyboardControl)index;
        const char *target = picture_name(control, names[index], false);
        append_rebound_clip(clips, &count,
            focus.resting ? "fs_prdicSelWidw_a_PRDC_Roll_over" :
            focus.entering ? "fs_prdicSelWidw_a_PRDC_Focus-IN" :
                             "fs_prdicSelWidw_a_PRDC_Focus-OUT",
            "P_PRDC_US_US", target, focus.frame);
    }
    for (unsigned index = WM_KEYBOARD_LANGUAGE_ENGLISH;
         index <= WM_KEYBOARD_LANGUAGE_SPANISH; index++) {
        if (!wm_board_keyboard_press_pose_active(keyboard, index)) continue;
        const char *target = picture_name((WmBoardKeyboardControl)index,
                                           names[index], false);
        append_rebound_clip(clips, &count,
            "fs_prdicSelWidw_a_PRDC_Pushed", "P_PRDC_US_US", target,
            keyboard->press[index].frame);
        append_press_exit(clips, &count, keyboard, (WmBoardKeyboardControl)index,
            "fs_prdicSelWidw_a_PRDC_Focus-OUT", "P_PRDC_US_US", target);
    }
    wm_layout_pose(keyboard->language, clips, count);
    keyboard->language_dirty = false;
    wm_layout_set_pane_visible(keyboard->language, "N_PRDCkey_EU", false);
    wm_layout_set_pose_text(keyboard->language, "T_PRDC_title", "Dictionary");
    static const char *const text_panes[3] = {
        "T_PRDC_US_US", "T_PRDC_US_Fre", "T_PRDC_US_Spa"
    };
    for (unsigned index = 0; index < 3; index++)
        wm_layout_set_pose_text(keyboard->language, text_panes[index],
                                 language_names[index]);
    raise_focused_key(keyboard, keyboard->language, false);
}

static void pose_symbols(WmBoardKeyboard *keyboard) {
    if (!keyboard->symbols_dirty) return;
    WmLayoutClip clips[KEYBOARD_CLIP_CAPACITY];
    size_t count = 0;
    const char *fade_in = "fs_signWindow_a_SGN_FADE-IN";
    append_clip(clips, &count, fade_in, NULL, 18.0f);
    append_rebound_clip(clips, &count, fade_in, "P_SGNkey_close",
                        "P_SGNkey_prev", 18.0f);
    append_rebound_clip(clips, &count, fade_in, "P_SGNkey_close",
                        "P_SGNkey_next", 18.0f);
    const char *transition = NULL;
    switch (keyboard->symbol_phase) {
        case SYMBOL_ENTERING: transition = fade_in; break;
        case SYMBOL_SCROLL_PREV:
            transition = "fs_signWindow_a_SGN_scroll_next";
            break;
        case SYMBOL_SCROLL_NEXT:
            transition = "fs_signWindow_a_SGN_scroll_prev";
            break;
        case SYMBOL_LEAVING:
            transition = "fs_signWindow_a_Scroll_FADE-OUT";
            break;
        case SYMBOL_CLOSED:
        case SYMBOL_OPEN: break;
    }
    if (transition) {
        append_clip(clips, &count, transition, NULL,
                    keyboard->symbol_frame);
        append_rebound_clip(clips, &count, transition, "P_SGNkey_close",
                            "P_SGNkey_prev", keyboard->symbol_frame);
        append_rebound_clip(clips, &count, transition, "P_SGNkey_close",
                            "P_SGNkey_next", keyboard->symbol_frame);
    }
    char target_names[WM_KEYBOARD_CONTROL_LAST + 1][24];
    for (unsigned index = WM_KEYBOARD_SYMBOL_FIRST;
         index <= WM_KEYBOARD_SYMBOL_NEXT; index++) {
        KeyboardFocus focus = keyboard->focus[index];
        if (!focus.active || wm_board_keyboard_press_pose_active(keyboard, index))
            continue;
        WmBoardKeyboardControl control = (WmBoardKeyboardControl)index;
        const char *target = picture_name(control, target_names[index],
                                          false);
        append_rebound_clip(clips, &count,
            focus.resting ? "fs_signWindow_a_SGN_Roll_over" :
            focus.entering ? "fs_signWindow_a_SGN_Focus-IN" :
                             "fs_signWindow_a_SGN_Focus-OUT",
            focus_prototype(control, target, false), target, focus.frame);
    }
    for (unsigned index = WM_KEYBOARD_SYMBOL_FIRST;
         index <= WM_KEYBOARD_SYMBOL_NEXT; index++) {
        if (!wm_board_keyboard_press_pose_active(keyboard, index)) continue;
        WmBoardKeyboardControl control = (WmBoardKeyboardControl)index;
        const char *target = picture_name(control,
                                           target_names[index],
                                           false);
        append_rebound_clip(clips, &count, "fs_signWindow_a_SGN_Pushed",
                            focus_prototype(control, target, false),
                            target, keyboard->press[index].frame);
        append_press_exit(clips, &count, keyboard, control,
            "fs_signWindow_a_SGN_Focus-OUT",
            focus_prototype(control, target, false), target);
    }
    unsigned second_page = keyboard->symbol_phase == SYMBOL_SCROLL_PREV ||
                           keyboard->symbol_phase == SYMBOL_SCROLL_NEXT
                               ? keyboard->symbol_target_page :
                                 keyboard->symbol_page;
    for (unsigned index = 0; index < SYMBOLS_PER_PAGE * 2; index++) {
        char pane[24];
        snprintf(pane, sizeof(pane), "T_SGNkey_%02u", index);
        unsigned page = index < SYMBOLS_PER_PAGE ? keyboard->symbol_page :
                                                  second_page;
        wm_layout_set_text(keyboard->symbols, pane,
                           wm_board_keyboard_symbols[page][index % SYMBOLS_PER_PAGE]);
    }
    char number[16];
    snprintf(number, sizeof(number), "%u/%u", keyboard->symbol_page + 1,
             SYMBOL_PAGE_COUNT);
    wm_layout_set_text(keyboard->symbols, "T_SGN_pageNumber", number);
    wm_layout_set_text(keyboard->symbols, "T_SGNkey_close", "Close");
    wm_layout_set_text(keyboard->symbols, "T_SGNkey_prev", "←");
    wm_layout_set_text(keyboard->symbols, "T_SGNkey_next", "→");
    wm_layout_pose(keyboard->symbols, clips, count);
    raise_focused_key(keyboard, keyboard->symbols, false);
    keyboard->symbols_dirty = false;
}

static void position_layout(WmLayout *layout, float y) {
    wm_layout_set_pane_translation(layout, "RootPane", 0.0f, y, 0.0f);
}

typedef struct PredictionDrawPass {
    bool text_pass;
    bool selected_only;
    uint32_t foreground_candidates;
} PredictionDrawPass;

static bool draw_prediction_pass(void *context, const char *pane_name) {
    const PredictionDrawPass *pass = context;
    bool candidate_text = strncmp(pane_name, "T_prdc_Text_", 12) == 0 &&
        pane_name[12] >= '0' && pane_name[12] <= '9' &&
        pane_name[13] >= '0' && pane_name[13] <= '9' && pane_name[14] == '\0';
    if (!pass->text_pass) return !candidate_text;
    if (!candidate_text) return false;
    if (!pass->foreground_candidates) return true;
    unsigned slot = (unsigned)(pane_name[12] - '0') * 10u +
                    (unsigned)(pane_name[13] - '0');
    bool selected = slot < CANDIDATE_PANE_COUNT &&
        (pass->foreground_candidates & (UINT32_C(1) << slot)) != 0;
    return pass->selected_only ? selected : !selected;
}

static void pose_address_text_box(WmBoardKeyboard *keyboard) {
    bool large = keyboard->profile != WM_BOARD_KEYBOARD_ADDRESS_EMAIL;
    bool numeric = keyboard->profile == WM_BOARD_KEYBOARD_ADDRESS_WII;
    WmLayout *box = large ? keyboard->text_box_big :
                              keyboard->text_box_small;
    WmLayoutClip clip = {
        .animation = large ? NULL : "fs_VK_textBox_a_normal",
        .frame = 0.0f,
        .loop_override = 0
    };
    wm_layout_pose(box, large ? NULL : &clip, large ? 0 : 1);
    wm_layout_set_pose_text(box, "T_2l_TextBox",
                              keyboard->text_context ?
                                  keyboard->text_context : "");
    wm_layout_set_pose_text(box, "T_title_text", "");
    wm_layout_set_pane_visible(box, "P_txtScrll_UP", false);
    wm_layout_set_pane_visible(box, "P_txtScrll_DOWN", false);
    if (large) {
        wm_layout_set_pane_visible(box, "N_separateBarAll", numeric);
        wm_layout_set_pane_visible(box, "N_KOR", false);
        wm_layout_set_pane_visible(box, "N_CHN", false);
        wm_layout_set_pane_visible(box, "N_separateBarKOR", false);
        wm_layout_set_pane_visible(box, "N_separateBarCHN", false);
    }
}

typedef struct ConsoleCaretPane {
    bool found;
    float matrix[12];
    float alpha;
    WmFontPane font;
    const char *font_name;
} ConsoleCaretPane;

static bool capture_console_caret_pane(void *context,
                                       const WmLayoutPaneView *pane) {
    if (strcmp(pane->name, "T_2l_TextBox") != 0 || !pane->text)
        return true;
    ConsoleCaretPane *caret = context;
    caret->found = true;
    memcpy(caret->matrix, pane->matrix, sizeof(caret->matrix));
    caret->alpha = pane->alpha;
    caret->font = pane->text->pane;
    caret->font_name = pane->text->font_name;
    return true;
}

static void draw_console_caret(WmBoardKeyboard *keyboard, float opacity) {
    if (!keyboard->caret_visible || !keyboard->text_context || opacity <= 0.0f)
        return;
    ConsoleCaretPane caret = {0};
    WmLayoutDrawOptions options = {
        .wide = true, .mode = WM_LAYOUT_IPL, .alpha = opacity,
        .on_pane = capture_console_caret_pane, .context = &caret
    };
    wm_layout_draw(keyboard->text_box_big, &options);
    if (!caret.found || !caret.font_name) return;
    WmCachedFont *face = wm_font_cache_resolve(keyboard->fonts,
                                               caret.font_name);
    const WmFontTextLayout *layout = face
        ? wm_font_cache_layout(face, keyboard->text_context, &caret.font)
        : NULL;
    float position_x, position_y;
    if (!layout || !wm_font_text_layout_caret(layout, keyboard->caret_bytes,
                                              &position_x, &position_y))
        return;

    /* Match the red, pulsing insertion strip used by the source Memo editor. */
    const float width = (float)(14592 / 832) / 6.0f;
    const float height = fmaxf(0.0f, caret.font.font_size[1] - 4.0f);
    const float radians = fmodf(keyboard->caret_age, 45.0f) *
                          (8.0f * 3.14159265358979323846f / 180.0f);
    const float alpha = floorf(127.0f * (1.0f + sinf(radians))) /
                        255.0f * caret.alpha;
    const float xs[4] = {position_x - width * 0.5f,
                         position_x + width * 0.5f,
                         position_x - width * 0.5f,
                         position_x + width * 0.5f};
    const float ys[4] = {position_y - 2.0f, position_y - 2.0f,
                         position_y - 2.0f - height,
                         position_y - 2.0f - height};
    WmDrawVertex vertices[4] = {0};
    for (size_t index = 0; index < 4; index++) {
        float world_x = caret.matrix[0] * xs[index] +
                        caret.matrix[1] * ys[index] + caret.matrix[3];
        float world_y = caret.matrix[4] * xs[index] +
                        caret.matrix[5] * ys[index] + caret.matrix[7];
        vertices[index].x = WM_FRAME_WIDTH * 0.5f +
                            world_x * (float)WM_FRAME_WIDTH / 832.0f;
        vertices[index].y = WM_FRAME_HEIGHT * 0.5f - world_y;
        vertices[index].color =
            (WmColor){1.0f, 50.0f / 255.0f, 50.0f / 255.0f, alpha};
    }
    wm_platform_draw_vertices(keyboard->platform, vertices, 0);
}

bool wm_board_keyboard_hit_text_caret(WmBoardKeyboard *keyboard,
                                      int x, int y, size_t *byte_index) {
    if (!keyboard || !byte_index || !keyboard->text_context ||
        keyboard->profile != WM_BOARD_KEYBOARD_CONSOLE_NICKNAME)
        return false;
    pose_address_text_box(keyboard);
    position_layout(keyboard->text_box_big, 0.0f);
    WmSourceRect bounds;
    if (!wm_source_pane_rect(keyboard->text_box_big, "T_2l_TextBox",
                              true, WM_LAYOUT_IPL, NULL, &bounds) ||
        x < bounds.x || x >= bounds.x + bounds.width ||
        y < bounds.y || y >= bounds.y + bounds.height)
        return false;

    ConsoleCaretPane caret = {0};
    WmLayoutDrawOptions options = {
        .wide = true, .mode = WM_LAYOUT_IPL, .alpha = 1.0f,
        .on_pane = capture_console_caret_pane, .context = &caret
    };
    wm_layout_draw(keyboard->text_box_big, &options);
    if (!caret.found || !caret.font_name) return false;
    float determinant = caret.matrix[0] * caret.matrix[5] -
                        caret.matrix[1] * caret.matrix[4];
    if (!isfinite(determinant) || fabsf(determinant) < 0.000001f)
        return false;
    float projected_x = ((float)x - WM_FRAME_WIDTH * 0.5f) *
                        832.0f / WM_FRAME_WIDTH - caret.matrix[3];
    float projected_y = WM_FRAME_HEIGHT * 0.5f - (float)y - caret.matrix[7];
    float local_x = (caret.matrix[5] * projected_x -
                     caret.matrix[1] * projected_y) / determinant;
    float local_y = (caret.matrix[0] * projected_y -
                     caret.matrix[4] * projected_x) / determinant;
    WmCachedFont *face = wm_font_cache_resolve(keyboard->fonts,
                                               caret.font_name);
    const WmFontTextLayout *layout = face
        ? wm_font_cache_layout(face, keyboard->text_context, &caret.font)
        : NULL;
    size_t selected;
    if (!layout || !wm_font_text_layout_hit_caret(layout, local_x, local_y,
                                                  &selected)) return false;
    size_t length = strlen(keyboard->text_context);
    *byte_index = selected < length ? selected : length;
    return true;
}

void wm_board_keyboard_draw(WmBoardKeyboard *keyboard, float progress,
                            bool entering) {
    (void)entering;
    if (!keyboard) return;
    if (keyboard->phone_layout) pose_phone(keyboard);
    else pose_keytop(keyboard);
    pose_toolbar(keyboard);
    wm_board_keyboard_pose_prediction(keyboard);
    if (keyboard->language_phase != LANGUAGE_CLOSED)
        pose_language(keyboard);
    progress = fminf(fmaxf(progress, 0.0f), 1.0f);
    float smooth = progress * progress * (3.0f - 2.0f * progress);
    float offset = -200.0f * (1.0f - smooth);
    float opacity = floorf(255.0f * smooth) / 255.0f;
    position_layout(keyboard->prediction, offset);
    WmLayout *active = keyboard->phone_layout ? keyboard->phone :
                                                 keyboard->keytop;
    position_layout(active, offset);
    if (keyboard->profile != WM_BOARD_KEYBOARD_MEMO) {
        WmLayoutClip clip = {
            .animation = "fs_VK_bg_a_normal",
            .frame = 0.0f,
            .loop_override = 0
        };
        wm_layout_pose(keyboard->background, &clip, 1);
        wm_layout_present_with_fonts_opacity(
            keyboard->platform, keyboard->textures, keyboard->fonts,
            keyboard->background, true, WM_LAYOUT_IPL, NULL, opacity);
        pose_address_text_box(keyboard);
        WmLayout *box = keyboard->profile != WM_BOARD_KEYBOARD_ADDRESS_EMAIL
            ? keyboard->text_box_big : keyboard->text_box_small;
        if (keyboard->profile == WM_BOARD_KEYBOARD_CONSOLE_NICKNAME)
            position_layout(box, offset);
        wm_layout_present_with_fonts_opacity(
            keyboard->platform, keyboard->textures, keyboard->fonts,
            box, true, WM_LAYOUT_IPL, NULL, opacity);
        if (keyboard->profile == WM_BOARD_KEYBOARD_CONSOLE_NICKNAME)
            draw_console_caret(keyboard, opacity);
    }
    /* Native toolbar halves move in opposite directions during entrance. */
    position_layout(keyboard->toolbar, 0.0f);
    wm_layout_set_pane_translation(keyboard->toolbar, "N_UP", 0.0f,
                                     -offset / 3.0f, 0.0f);
    wm_layout_set_pane_translation(keyboard->toolbar, "N_DOWN", 0.0f,
                                      offset / 3.0f, 0.0f);
    wm_layout_set_pane_visible(keyboard->toolbar, "N_UP", true);
    wm_layout_present_with_fonts_opacity(
        keyboard->platform, keyboard->textures, keyboard->fonts,
        keyboard->toolbar, true, WM_LAYOUT_IPL, NULL, opacity);
    uint32_t foreground_candidates = 0;
    WmSourceRect area;
    WmSourceRect window;
    bool candidate_area_visible = keyboard->profile == WM_BOARD_KEYBOARD_MEMO &&
        keyboard->candidate_count > 0 &&
        wm_source_pane_rect(keyboard->prediction, "N_prdcTextArea",
            true, WM_LAYOUT_IPL, NULL, &area) &&
        wm_source_pane_rect(keyboard->prediction, "W_predictWindow",
            true, WM_LAYOUT_IPL, NULL, &window);
    if (candidate_area_visible && !keyboard->candidate_scrolling) {
        for (unsigned slot = 0; slot < CANDIDATE_PANE_COUNT; slot++) {
            unsigned control = WM_KEYBOARD_CANDIDATE_FIRST + slot;
            if (keyboard->candidate_pane_indices[slot] < keyboard->candidate_count &&
                (keyboard->hovered == control || keyboard->focus[control].active ||
                 wm_board_keyboard_press_pose_active(keyboard, control))) {
                foreground_candidates |= UINT32_C(1) << slot;
            }
        }
    }
    if (keyboard->profile == WM_BOARD_KEYBOARD_MEMO) {
        PredictionDrawPass pass = {.foreground_candidates = foreground_candidates};
        wm_layout_present_with_fonts_opacity_masked(
            keyboard->platform, keyboard->textures, keyboard->fonts,
            keyboard->prediction, true, WM_LAYOUT_IPL, NULL, opacity,
            draw_prediction_pass, &pass);
        if (candidate_area_visible) {
            /* Keep the strip's right edge at the authored text area. Its
             * left glyphs can extend over the rounded window corner. */
            float left = fmaxf(0.0f, window.x - 1.0f);
            WmClipRect clip = {
                .x = left, .y = 0.0f,
                .width = area.x + area.width - left,
                .height = (float)WM_FRAME_HEIGHT
            };
            wm_platform_set_clip(keyboard->platform, &clip);
            pass.text_pass = true;
            wm_layout_present_with_fonts_opacity_masked(
                keyboard->platform, keyboard->textures, keyboard->fonts,
                keyboard->prediction, true, WM_LAYOUT_IPL, NULL, opacity,
                draw_prediction_pass, &pass);
            wm_platform_set_clip(keyboard->platform, NULL);
        }
    }
    wm_layout_present_with_fonts_opacity(
        keyboard->platform, keyboard->textures, keyboard->fonts,
        active, true, WM_LAYOUT_IPL, NULL, opacity);
    if (foreground_candidates) {
        /* Focus scales the first word across the rounded left edge. Draw
         * every glyph above the keytops and window at its authored position. */
        WmClipRect clip = {
            .x = -1.0f, .y = 0.0f,
            .width = area.x + area.width + 1.0f,
            .height = (float)WM_FRAME_HEIGHT
        };
        PredictionDrawPass pass = {
            .text_pass = true, .selected_only = true,
            .foreground_candidates = foreground_candidates
        };
        wm_platform_set_clip(keyboard->platform, &clip);
        wm_layout_present_with_fonts_opacity_masked(
            keyboard->platform, keyboard->textures, keyboard->fonts,
            keyboard->prediction, true, WM_LAYOUT_IPL, NULL, opacity,
            draw_prediction_pass, &pass);
        wm_platform_set_clip(keyboard->platform, NULL);
    }
    if (keyboard->symbol_phase != SYMBOL_CLOSED) {
        pose_symbols(keyboard);
        position_layout(keyboard->symbols, offset);
        wm_layout_present_with_fonts_opacity(
            keyboard->platform, keyboard->textures, keyboard->fonts,
            keyboard->symbols, true, WM_LAYOUT_IPL, NULL, opacity);
    }
    if (keyboard->language_phase != LANGUAGE_CLOSED) {
        position_layout(keyboard->language, offset);
        wm_layout_present_with_fonts_opacity(
            keyboard->platform, keyboard->textures, keyboard->fonts,
            keyboard->language, true, WM_LAYOUT_IPL, NULL, opacity);
    }
}

static bool hit_pane(WmLayout *layout, const char *name, int x, int y) {
    WmSourceRect rect;
    return wm_source_pane_rect(layout, name, true, WM_LAYOUT_IPL,
                               NULL, &rect) &&
           (float)x >= rect.x && (float)x < rect.x + rect.width &&
           (float)y >= rect.y && (float)y < rect.y + rect.height;
}

WmBoardKeyboardControl wm_board_keyboard_hit_unfiltered(WmBoardKeyboard *keyboard,
                                              int x, int y) {
    if (!keyboard) return WM_KEYBOARD_NONE;
    if (keyboard->language_phase != LANGUAGE_CLOSED) {
        pose_language(keyboard);
        position_layout(keyboard->language, 0.0f);
        if (keyboard->language_phase != LANGUAGE_OPEN)
            return WM_KEYBOARD_NONE;
        static const char *const panes[3] = {
            "B_PRDC_US_US", "B_PRDC_US_Fre", "B_PRDC_US_Spa"
        };
        for (unsigned index = 0; index < 3; index++)
            if (hit_pane(keyboard->language, panes[index], x, y))
                return (WmBoardKeyboardControl)(
                    WM_KEYBOARD_LANGUAGE_ENGLISH + index);
        return WM_KEYBOARD_NONE;
    }
    if (keyboard->symbol_phase != SYMBOL_CLOSED) {
        pose_symbols(keyboard);
        position_layout(keyboard->symbols, 0.0f);
        if (keyboard->symbol_phase != SYMBOL_OPEN) {
            if ((keyboard->symbol_phase == SYMBOL_SCROLL_PREV ||
                 keyboard->symbol_phase == SYMBOL_SCROLL_NEXT) &&
                (keyboard->hovered == WM_KEYBOARD_SYMBOL_PREV ||
                 keyboard->hovered == WM_KEYBOARD_SYMBOL_NEXT)) {
                const char *pane = keyboard->hovered == WM_KEYBOARD_SYMBOL_PREV
                    ? "B_SGNkey_prev" : "B_SGNkey_next";
                return hit_pane(keyboard->symbols, pane, x, y)
                    ? keyboard->hovered : WM_KEYBOARD_NONE;
            }
            return WM_KEYBOARD_NONE;
        }
        static const struct {
            WmBoardKeyboardControl control;
            const char *pane;
        } actions[] = {
            {WM_KEYBOARD_SYMBOL_CLOSE, "B_SGNkey_close"},
            {WM_KEYBOARD_SYMBOL_PREV, "B_SGNkey_prev"},
            {WM_KEYBOARD_SYMBOL_NEXT, "B_SGNkey_next"}
        };
        for (size_t index = 0; index < sizeof(actions) / sizeof(actions[0]);
             index++) {
            if (hit_pane(keyboard->symbols, actions[index].pane, x, y)) {
                return actions[index].control;
            }
        }
        for (unsigned index = 0; index < SYMBOLS_PER_PAGE; index++) {
            char pane[24];
            snprintf(pane, sizeof(pane), "B_SGNkey_%02u", index);
            if (hit_pane(keyboard->symbols, pane, x, y)) {
                return (WmBoardKeyboardControl)(WM_KEYBOARD_SYMBOL_FIRST +
                                                index);
            }
        }
        return WM_KEYBOARD_NONE;
    }
    if (keyboard->phone_layout) pose_phone(keyboard);
    else pose_keytop(keyboard);
    pose_toolbar(keyboard);
    WmLayout *active = keyboard->phone_layout ? keyboard->phone :
                                                 keyboard->keytop;
    position_layout(active, 0.0f);
    position_layout(keyboard->toolbar, 0.0f);
    wm_layout_set_pane_translation(keyboard->toolbar, "N_UP", 0, 0, 0);
    wm_layout_set_pane_translation(keyboard->toolbar, "N_DOWN", 0, 0, 0);
    if (hit_pane(keyboard->toolbar, "B_BT_cancel", x, y))
        return WM_KEYBOARD_BACK;
    if (hit_pane(keyboard->toolbar, "B_BT_confirm", x, y))
        return WM_KEYBOARD_OK;
    if (hit_pane(keyboard->toolbar, "B_kyChng_QWERTY", x, y))
        return WM_KEYBOARD_QWERTY;
    if (hit_pane(keyboard->toolbar, "B_kyChng_CP", x, y))
        return WM_KEYBOARD_PHONE;
    if (keyboard->profile == WM_BOARD_KEYBOARD_MEMO) {
        wm_board_keyboard_pose_prediction(keyboard);
        position_layout(keyboard->prediction, 0.0f);
        if (hit_pane(keyboard->prediction,
            keyboard->prediction_enabled ? "B_OnBtn" : "B_OffBtn", x, y))
            return WM_KEYBOARD_PREDICTION;
        if (keyboard->candidate_first > 0 &&
            hit_pane(keyboard->prediction, "B_prdc_scrl_Left", x, y))
            return WM_KEYBOARD_CANDIDATE_PREVIOUS;
        if (wm_board_keyboard_candidate_next_index(keyboard) > keyboard->candidate_first &&
            hit_pane(keyboard->prediction, "B_prdc_scrl_Rght", x, y))
            return WM_KEYBOARD_CANDIDATE_NEXT;
        for (unsigned index = 0; index < CANDIDATE_PANE_COUNT;
             index++) {
            if (keyboard->candidate_pane_indices[index] >=
                keyboard->candidate_count) continue;
            char pane[24];
            snprintf(pane, sizeof(pane), "B_prdc_Text_%02u", index);
            if (hit_pane(keyboard->prediction, pane, x, y))
                return (WmBoardKeyboardControl)(
                    WM_KEYBOARD_CANDIDATE_FIRST + index);
        }
    }
    if (keyboard->phone_layout) {
        if (keyboard->profile == WM_BOARD_KEYBOARD_MEMO &&
            keyboard->phone_mode != 3 &&
            hit_pane(keyboard->phone, "B_prdcModeBT_EU", x, y))
            return WM_KEYBOARD_LANGUAGE;
        if (keyboard->phone_mode != 3 &&
            hit_pane(keyboard->phone, "B_othersBT_EU", x, y))
            return WM_KEYBOARD_MORE;
        if (hit_pane(keyboard->phone, "B_CPkey_DELETE", x, y))
            return WM_KEYBOARD_DELETE;
        if (hit_pane(keyboard->phone, "B_CPkey_LF", x, y))
            return WM_KEYBOARD_RETURN;
        for (unsigned index = 0; index < 4; index++) {
            char pane[24];
            snprintf(pane, sizeof(pane), "B_ChngTag_%02u", index);
            if (hit_pane(keyboard->phone, pane, x, y)) {
                return (WmBoardKeyboardControl)(
                    WM_KEYBOARD_PHONE_MODE_FIRST + index);
            }
        }
        for (unsigned index = 0; index < 12; index++) {
            char pane[24];
            char label_buffer[16];
            if (!wm_board_keyboard_phone_label(keyboard, index, label_buffer)[0]) continue;
            snprintf(pane, sizeof(pane), "B_CPkey_%02u", index);
            if (hit_pane(keyboard->phone, pane, x, y)) {
                return (WmBoardKeyboardControl)(WM_KEYBOARD_PHONE_FIRST +
                                                index);
            }
        }
        return WM_KEYBOARD_NONE;
    }
    if (keyboard->profile == WM_BOARD_KEYBOARD_MEMO &&
        hit_pane(keyboard->keytop, "B_USEU_prdc_lang", x, y))
        return WM_KEYBOARD_LANGUAGE;
    if (hit_pane(keyboard->keytop, "B_USEU_Chng_sign", x, y))
        return WM_KEYBOARD_MORE;
    static const struct {
        WmBoardKeyboardControl control;
        const char *pane;
    } special[] = {
        {WM_KEYBOARD_DELETE, "B_key_DELETE"},
        {WM_KEYBOARD_RETURN, "B_key_LF"},
        {WM_KEYBOARD_CAPS, "B_key_CAPS"},
        {WM_KEYBOARD_SHIFT, "B_key_SHIFT"},
        {WM_KEYBOARD_SPACE, "B_key_SPACE"}
    };
    for (size_t index = 0; index < sizeof(special) / sizeof(special[0]);
         index++) {
        if (hit_pane(keyboard->keytop, special[index].pane, x, y))
            return special[index].control;
    }
    for (unsigned index = 0; index < 50; index++) {
        if (!wm_board_keyboard_key_character(keyboard, index)) continue;
        char name[24];
        snprintf(name, sizeof(name), "B_key_%02u", index);
        if (hit_pane(keyboard->keytop, name, x, y)) {
            return (WmBoardKeyboardControl)(WM_KEYBOARD_CHARACTER_FIRST +
                                            index);
        }
    }
    return WM_KEYBOARD_NONE;
}
