#include "board_keyboard_internal.h"

#include "wii_menu/layout/layout_present.h"
#include "wii_menu/input/source_hit.h"

#include <math.h>
#include <stdio.h>
#include <string.h>

enum {
    KEYBOARD_CLIP_CAPACITY = WM_KEYBOARD_CONTROL_LAST + 12
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

static void raise_focused_key(WmBoardKeyboard *keyboard, WmLayout *layout,
                              bool phone_layout) {
    WmBoardKeyboardControl control = keyboard->hovered != WM_KEYBOARD_NONE
        ? keyboard->hovered : keyboard->pressed;
    char name[24];
    const char *picture = picture_name(control, name, phone_layout);
    if (picture) (void)wm_layout_raise_pane(layout, picture);
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
        if (keyboard->caps) {
            append_rebound_clip(clips, &count, selected_motion[0],
                                "P_key_CAPS", "P_key_CAPS", 0.0f);
        }
        if (keyboard->shift) {
            append_rebound_clip(clips, &count, selected_motion[0],
                                "P_key_SHIFT", "P_key_SHIFT", 0.0f);
        }
    }
    for (unsigned index = 1; index <= WM_KEYBOARD_CONTROL_LAST; index++) {
        KeyboardFocus focus = keyboard->focus[index];
        if (!focus.active ||
            (toolbar ? !is_toolbar((WmBoardKeyboardControl)index) :
                       !is_keytop((WmBoardKeyboardControl)index)) ||
            selected_tab(keyboard, (WmBoardKeyboardControl)index))
            continue;
        const char *target = picture_name((WmBoardKeyboardControl)index,
                                           target_names[index],
                                           keyboard->phone_layout);
        if (!target) continue;
        bool selected = !toolbar &&
            ((index == WM_KEYBOARD_CAPS && keyboard->caps) ||
             (index == WM_KEYBOARD_SHIFT && keyboard->shift));
        append_rebound_clip(clips, &count,
                    focus.resting
                        ? selected ? "fs_VK_ascii_keytop_a_toggle-ON" :
                          toolbar ? "fs_VK_toolbar_a_Roll_over" :
                                    "fs_VK_ascii_keytop_a_Roll_over"
                        : selected ? selected_motion[focus.entering ? 1 : 2] :
                                     motion[focus.entering ? 0 : 1],
                    focus_prototype((WmBoardKeyboardControl)index, target,
                                    keyboard->phone_layout),
                    target, focus.frame);
    }
    if (keyboard->pressed != WM_KEYBOARD_NONE &&
        is_toolbar(keyboard->pressed) == toolbar) {
        unsigned index = (unsigned)keyboard->pressed;
        const char *target = picture_name(keyboard->pressed,
                                           target_names[index],
                                           keyboard->phone_layout);
        bool selected = !toolbar &&
            ((index == WM_KEYBOARD_CAPS && keyboard->caps) ||
             (index == WM_KEYBOARD_SHIFT && keyboard->shift));
        if (target && (toolbar ? is_toolbar(keyboard->pressed) :
                                is_keytop(keyboard->pressed))) {
            append_rebound_clip(clips, &count,
                                selected ? selected_motion[3] : motion[2],
                                focus_prototype(keyboard->pressed, target,
                                                keyboard->phone_layout),
                                target, keyboard->press_frame);
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
                                keyboard->prediction_animating
                                    ? keyboard->prediction_from :
                                      keyboard->prediction_enabled);
    wm_layout_set_pane_visible(keyboard->keytop, "P_prdc_OFF",
                                keyboard->prediction_animating
                                    ? !keyboard->prediction_from :
                                      !keyboard->prediction_enabled);
    wm_layout_set_pane_visible(keyboard->keytop, "W_USEU_prdc_lang",
                                keyboard->profile == WM_BOARD_KEYBOARD_MEMO);
    if (keyboard->profile == WM_BOARD_KEYBOARD_ADDRESS_WII ||
        keyboard->profile == WM_BOARD_KEYBOARD_ADDRESS_EMAIL) {
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
    if (is_phone_control(keyboard, keyboard->pressed)) {
        unsigned index = keyboard->pressed;
        const char *target = picture_name(keyboard->pressed, targets[index],
                                           true);
        if (target) {
            append_rebound_clip(clips, &count,
                                "fs_VK_cellPhone_a_Pushed",
                                focus_prototype(keyboard->pressed, target,
                                                true), target,
                                keyboard->press_frame);
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
                                keyboard->prediction_animating
                                    ? !keyboard->prediction_from :
                                      !keyboard->prediction_enabled);
    wm_layout_set_pane_visible(keyboard->phone, "N_prdc_EU_ON",
                                keyboard->prediction_animating
                                    ? keyboard->prediction_from :
                                      keyboard->prediction_enabled);
    wm_layout_set_pane_visible(keyboard->phone, "W_othersBT_EU",
                               keyboard->profile != WM_BOARD_KEYBOARD_ADDRESS_WII &&
                               keyboard->profile != WM_BOARD_KEYBOARD_ADDRESS_EMAIL &&
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
        keyboard->profile == WM_BOARD_KEYBOARD_ADDRESS_NICKNAME);
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
    if (focus.active) append_rebound_clip(clips, &count,
        focus.resting ? "fs_VK_predictInput_a_Roll_over" :
        focus.entering ? "fs_VK_predictInput_a_Foucus_IN" :
                         "fs_VK_predictInput_a_Focus_OUT",
        button, button, focus.frame);
    if (keyboard->pressed == WM_KEYBOARD_PREDICTION)
        append_rebound_clip(clips, &count,
            "fs_VK_predictInput_a_OnOffButton_Pushed", button, button,
            keyboard->press_frame);
    /* Clip target names must remain alive until wm_layout_pose consumes them. */
    char candidate_names[CANDIDATE_PANE_COUNT][24];
    for (unsigned index = 0; index < CANDIDATE_PANE_COUNT; index++) {
        WmBoardKeyboardControl control = (WmBoardKeyboardControl)(
            WM_KEYBOARD_CANDIDATE_FIRST + index);
        KeyboardFocus candidate_focus = keyboard->focus[control];
        if (!candidate_focus.active || keyboard->candidate_scrolling) continue;
        snprintf(candidate_names[index], sizeof(candidate_names[index]),
                 "T_prdc_Text_%02u", index);
        append_rebound_clip(clips, &count,
            candidate_focus.resting ? "fs_VK_predictInput_a_Roll_over" :
            candidate_focus.entering ? "fs_VK_predictInput_a_Foucus_IN" :
                                       "fs_VK_predictInput_a_Focus_OUT",
            "T_prdc_Text_00", candidate_names[index],
            candidate_focus.frame);
    }
    if (keyboard->pressed >= WM_KEYBOARD_CANDIDATE_FIRST &&
        keyboard->pressed <= WM_KEYBOARD_CANDIDATE_LAST) {
        unsigned index = keyboard->pressed - WM_KEYBOARD_CANDIDATE_FIRST;
        if (keyboard->candidate_pane_indices[index] <
            keyboard->candidate_count) {
            snprintf(candidate_names[index], sizeof(candidate_names[index]),
                     "T_prdc_Text_%02u", index);
            append_rebound_clip(clips, &count,
                "fs_VK_predictInput_a_Pushed", "T_prdc_Text_00",
                candidate_names[index], keyboard->press_frame);
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
        if (arrow_focus.active) append_rebound_clip(clips, &count,
            arrow_focus.resting ? "fs_VK_predictInput_a_Roll_over" :
            arrow_focus.entering ? "fs_VK_predictInput_a_Foucus_IN" :
                                   "fs_VK_predictInput_a_Focus_OUT",
            "P_prdc_scrl_Left", picture, arrow_focus.frame);
        if (keyboard->pressed == control)
            append_rebound_clip(clips, &count,
                "fs_VK_predictInput_a_Pushed", "P_prdc_scrl_Left",
                picture, keyboard->press_frame);
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
    if (keyboard->hovered >= WM_KEYBOARD_CANDIDATE_FIRST &&
        keyboard->hovered <= WM_KEYBOARD_CANDIDATE_LAST) {
        unsigned index = keyboard->hovered - WM_KEYBOARD_CANDIDATE_FIRST;
        if (keyboard->candidate_pane_indices[index] <
            keyboard->candidate_count && !keyboard->candidate_scrolling) {
            char name[24];
            snprintf(name, sizeof(name), "T_prdc_Text_%02u", index);
            (void)wm_layout_raise_pane(keyboard->prediction, name);
        }
    }
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
        if (!focus.active) continue;
        WmBoardKeyboardControl control = (WmBoardKeyboardControl)index;
        const char *target = picture_name(control, names[index], false);
        append_rebound_clip(clips, &count,
            focus.resting ? "fs_prdicSelWidw_a_PRDC_Roll_over" :
            focus.entering ? "fs_prdicSelWidw_a_PRDC_Focus-IN" :
                             "fs_prdicSelWidw_a_PRDC_Focus-OUT",
            "P_PRDC_US_US", target, focus.frame);
    }
    if (is_language_choice(keyboard->pressed)) {
        const char *target = picture_name(keyboard->pressed,
                                           names[keyboard->pressed], false);
        append_rebound_clip(clips, &count,
            "fs_prdicSelWidw_a_PRDC_Pushed", "P_PRDC_US_US", target,
            keyboard->press_frame);
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
    if (is_language_choice(keyboard->hovered)) {
        const char *target = picture_name(keyboard->hovered,
                                           names[keyboard->hovered], false);
        (void)wm_layout_raise_pane(keyboard->language, target);
    }
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
        if (!focus.active) continue;
        WmBoardKeyboardControl control = (WmBoardKeyboardControl)index;
        const char *target = picture_name(control, target_names[index],
                                          false);
        append_rebound_clip(clips, &count,
            focus.resting ? "fs_signWindow_a_SGN_Roll_over" :
            focus.entering ? "fs_signWindow_a_SGN_Focus-IN" :
                             "fs_signWindow_a_SGN_Focus-OUT",
            focus_prototype(control, target, false), target, focus.frame);
    }
    if (is_symbol(keyboard->pressed)) {
        const char *target = picture_name(keyboard->pressed,
                                           target_names[keyboard->pressed],
                                           false);
        append_rebound_clip(clips, &count, "fs_signWindow_a_SGN_Pushed",
                            focus_prototype(keyboard->pressed, target, false),
                            target, keyboard->press_frame);
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
    keyboard->symbols_dirty = false;
}

static void position_layout(WmLayout *layout, float y) {
    wm_layout_set_pane_translation(layout, "RootPane", 0.0f, y, 0.0f);
}

typedef struct PredictionDrawPass {
    bool text_pass;
    bool selected_only;
    const char *selected_name;
} PredictionDrawPass;

static bool draw_prediction_pass(void *context, const char *pane_name) {
    const PredictionDrawPass *pass = context;
    bool candidate_text = strncmp(pane_name, "T_prdc_Text_", 12) == 0;
    if (!pass->text_pass) return !candidate_text;
    if (!candidate_text) return false;
    if (!pass->selected_name) return true;
    bool selected = strcmp(pane_name, pass->selected_name) == 0;
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
        wm_layout_present_with_fonts(keyboard->platform, keyboard->textures,
                                     keyboard->fonts, keyboard->background,
                                     true, WM_LAYOUT_IPL, NULL);
        pose_address_text_box(keyboard);
        WmLayout *box = keyboard->profile != WM_BOARD_KEYBOARD_ADDRESS_EMAIL
            ? keyboard->text_box_big : keyboard->text_box_small;
        wm_layout_present_with_fonts(keyboard->platform, keyboard->textures,
                                     keyboard->fonts, box, true,
                                     WM_LAYOUT_IPL, NULL);
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
    char selected_name[24];
    const char *selected = NULL;
    WmSourceRect area;
    WmSourceRect window;
    bool candidate_area_visible = keyboard->profile == WM_BOARD_KEYBOARD_MEMO &&
        keyboard->candidate_count > 0 &&
        wm_source_pane_rect(keyboard->prediction, "N_prdcTextArea",
            true, WM_LAYOUT_IPL, NULL, &area) &&
        wm_source_pane_rect(keyboard->prediction, "W_predictWindow",
            true, WM_LAYOUT_IPL, NULL, &window);
    if (candidate_area_visible && !keyboard->candidate_scrolling &&
        keyboard->hovered >= WM_KEYBOARD_CANDIDATE_FIRST &&
        keyboard->hovered <= WM_KEYBOARD_CANDIDATE_LAST) {
        unsigned slot = keyboard->hovered - WM_KEYBOARD_CANDIDATE_FIRST;
        if (keyboard->candidate_pane_indices[slot] < keyboard->candidate_count) {
            snprintf(selected_name, sizeof(selected_name),
                     "T_prdc_Text_%02u", slot);
            selected = selected_name;
        }
    }
    if (keyboard->profile == WM_BOARD_KEYBOARD_MEMO) {
        PredictionDrawPass pass = {.selected_name = selected};
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
    if (selected) {
        /* Focus scales the first word across the rounded left edge. Draw
         * every glyph above the keytops and window at its authored position. */
        WmClipRect clip = {
            .x = -1.0f, .y = 0.0f,
            .width = area.x + area.width + 1.0f,
            .height = (float)WM_FRAME_HEIGHT
        };
        PredictionDrawPass pass = {
            .text_pass = true, .selected_only = true,
            .selected_name = selected
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
