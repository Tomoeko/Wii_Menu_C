#include "settings_scene_internal.h"

#include <math.h>
#include <string.h>

enum {
    NICKNAME_CARET_FONT_SIZE = 36,
    NICKNAME_KEYBOARD_DELAY = 6,
    /* Match the existing software keyboard's thirty-frame travel. */
    NICKNAME_KEYBOARD_MOTION = 30
};

static float text_width(const WmSettingsScene *scene, const char *text) {
    if (scene->outline_font)
        return wm_outline_font_text_width(scene->outline_font, text,
                                          NICKNAME_CARET_FONT_SIZE);
    return 19.0f * strlen(text);
}

float settings_scene_nickname_caret_x(const WmSettingsScene *scene) {
    if (!scene)
        return 320.0f;
    size_t length = strlen(scene->edit_nickname);
    size_t caret = scene->nickname_caret < length ? scene->nickname_caret : length;
    char prefix[SETTINGS_NICKNAME_LIMIT + 1];
    memcpy(prefix, scene->edit_nickname, caret);
    prefix[caret] = '\0';
    return 320.0f - text_width(scene, scene->edit_nickname) * 0.5f +
           text_width(scene, prefix);
}

bool wm_settings_scene_nickname_keyboard_visible(const WmSettingsScene *scene) {
    return scene && scene->nickname_keyboard_phase != SETTINGS_NICKNAME_KEYBOARD_CLOSED;
}

bool wm_settings_scene_editing_nickname(const WmSettingsScene *scene) {
    return scene && scene->phase == WM_SETTINGS_READY &&
           scene->active_category == SETTINGS_NICKNAME &&
           scene->nickname_keyboard_phase != SETTINGS_NICKNAME_KEYBOARD_CLOSED &&
           scene->nickname_keyboard_phase != SETTINGS_NICKNAME_KEYBOARD_CLOSING;
}

bool wm_settings_scene_place_nickname_caret(WmSettingsScene *scene, int x) {
    if (!scene || scene->phase != WM_SETTINGS_READY ||
        scene->active_category != SETTINGS_NICKNAME ||
        wm_settings_scene_nickname_keyboard_visible(scene))
        return false;
    float source_x = scene->wide
                         ? ((float)x + 0.5f) * SETTINGS_WIDE_WIDTH / WM_FRAME_WIDTH -
                               SETTINGS_SIDE_WIDTH + 16.0f
                         : (float)x + 0.5f;
    size_t length = strlen(scene->edit_nickname);
    float left = 320.0f - text_width(scene, scene->edit_nickname) * 0.5f;
    float nearest = fabsf(source_x - left);
    unsigned caret = 0;
    char prefix[SETTINGS_NICKNAME_LIMIT + 1];
    for (size_t index = 1; index <= length; index++) {
        memcpy(prefix, scene->edit_nickname, index);
        prefix[index] = '\0';
        float distance = fabsf(source_x - left - text_width(scene, prefix));
        if (distance < nearest) {
            nearest = distance;
            caret = (unsigned)index;
        }
    }
    scene->nickname_caret = caret;
    return true;
}

bool settings_scene_open_nickname_keyboard(WmSettingsScene *scene) {
    if (!scene || scene->active_category != SETTINGS_NICKNAME ||
        scene->nickname_keyboard_phase != SETTINGS_NICKNAME_KEYBOARD_CLOSED)
        return false;
    if (!scene->nickname_keyboard) {
        scene->nickname_keyboard = wm_board_keyboard_create(
            scene->platform, scene->assets_directory, scene->textures, scene->fonts);
        if (!scene->nickname_keyboard)
            return false;
    }
    wm_board_keyboard_set_profile(scene->nickname_keyboard,
                                  WM_BOARD_KEYBOARD_CONSOLE_NICKNAME);
    memcpy(scene->nickname_keyboard_display, scene->edit_nickname,
           sizeof(scene->nickname_keyboard_display));
    wm_board_keyboard_set_text_context(scene->nickname_keyboard,
                                       scene->nickname_keyboard_display);
    memcpy(scene->nickname_before_keyboard, scene->edit_nickname,
           sizeof(scene->nickname_before_keyboard));
    size_t length = strlen(scene->edit_nickname);
    if (scene->nickname_caret > length)
        scene->nickname_caret = (unsigned)length;
    scene->nickname_keyboard_phase = SETTINGS_NICKNAME_KEYBOARD_PENDING;
    scene->nickname_keyboard_frame = 0.0f;
    scene->hover = WM_SETTINGS_CONTROL_NONE;
    return true;
}

void settings_scene_advance_nickname_keyboard(WmSettingsScene *scene, float frames) {
    if (!wm_settings_scene_nickname_keyboard_visible(scene) || !isfinite(frames) ||
        frames <= 0.0f)
        return;
    if (scene->nickname_keyboard)
        wm_board_keyboard_advance(scene->nickname_keyboard, frames);
    while (frames > 0.0f) {
        SettingsNicknameKeyboardPhase phase = scene->nickname_keyboard_phase;
        if (phase == SETTINGS_NICKNAME_KEYBOARD_OPEN ||
            phase == SETTINGS_NICKNAME_KEYBOARD_CLOSED)
            break;
        float duration = phase == SETTINGS_NICKNAME_KEYBOARD_PENDING
                             ? NICKNAME_KEYBOARD_DELAY
                             : NICKNAME_KEYBOARD_MOTION;
        float step = fminf(frames, duration - scene->nickname_keyboard_frame);
        scene->nickname_keyboard_frame += step;
        frames -= step;
        if (scene->nickname_keyboard_frame < duration)
            break;
        scene->nickname_keyboard_phase = phase == SETTINGS_NICKNAME_KEYBOARD_PENDING
                                             ? SETTINGS_NICKNAME_KEYBOARD_OPENING
                                         : phase == SETTINGS_NICKNAME_KEYBOARD_OPENING
                                             ? SETTINGS_NICKNAME_KEYBOARD_OPEN
                                             : SETTINGS_NICKNAME_KEYBOARD_CLOSED;
        scene->nickname_keyboard_frame =
            scene->nickname_keyboard_phase == SETTINGS_NICKNAME_KEYBOARD_OPEN
                ? NICKNAME_KEYBOARD_MOTION
                : 0.0f;
    }
}

void settings_scene_draw_nickname_keyboard(WmSettingsScene *scene) {
    if (!wm_settings_scene_nickname_keyboard_visible(scene) ||
        scene->nickname_keyboard_phase == SETTINGS_NICKNAME_KEYBOARD_PENDING ||
        !scene->nickname_keyboard)
        return;
    float progress =
        scene->nickname_keyboard_phase == SETTINGS_NICKNAME_KEYBOARD_OPEN ? 1.0f
        : scene->nickname_keyboard_phase == SETTINGS_NICKNAME_KEYBOARD_CLOSING
            ? 1.0f - scene->nickname_keyboard_frame / NICKNAME_KEYBOARD_MOTION
            : scene->nickname_keyboard_frame / NICKNAME_KEYBOARD_MOTION;
    wm_board_keyboard_set_caret(scene->nickname_keyboard, scene->nickname_caret,
                                scene->nickname_keyboard_phase !=
                                    SETTINGS_NICKNAME_KEYBOARD_CLOSING);
    wm_board_keyboard_draw(scene->nickname_keyboard, progress,
                           scene->nickname_keyboard_phase ==
                               SETTINGS_NICKNAME_KEYBOARD_OPENING);
}

bool wm_settings_scene_type_ascii(WmSettingsScene *scene, char character) {
    if (!wm_settings_scene_editing_nickname(scene) || character < 32 || character > 126)
        return false;
    size_t length = strlen(scene->edit_nickname);
    if (length >= SETTINGS_NICKNAME_LIMIT)
        return false;
    size_t caret = scene->nickname_caret;
    if (caret > length)
        caret = length;
    memmove(scene->edit_nickname + caret + 1, scene->edit_nickname + caret,
            length - caret + 1);
    scene->edit_nickname[caret] = character;
    scene->nickname_caret = (unsigned)(caret + 1);
    memcpy(scene->nickname_keyboard_display, scene->edit_nickname,
           sizeof(scene->nickname_keyboard_display));
    wm_board_keyboard_text_changed(scene->nickname_keyboard, false);
    return true;
}

bool wm_settings_scene_backspace(WmSettingsScene *scene) {
    if (!wm_settings_scene_editing_nickname(scene) || !scene->nickname_caret)
        return false;
    size_t length = strlen(scene->edit_nickname);
    size_t caret = scene->nickname_caret;
    if (caret > length)
        return false;
    memmove(scene->edit_nickname + caret - 1, scene->edit_nickname + caret,
            length - caret + 1);
    scene->nickname_caret--;
    memcpy(scene->nickname_keyboard_display, scene->edit_nickname,
           sizeof(scene->nickname_keyboard_display));
    wm_board_keyboard_text_changed(scene->nickname_keyboard, false);
    return true;
}

bool wm_settings_scene_move_nickname_caret(WmSettingsScene *scene, int direction) {
    if (!wm_settings_scene_editing_nickname(scene))
        return false;
    unsigned length = (unsigned)strlen(scene->edit_nickname);
    if (direction < 0 && scene->nickname_caret > 0)
        scene->nickname_caret--;
    else if (direction > 0 && scene->nickname_caret < length)
        scene->nickname_caret++;
    else
        return false;
    wm_board_keyboard_finish_composition(scene->nickname_keyboard);
    return true;
}

void wm_settings_scene_keyboard_modifiers(WmSettingsScene *scene, bool shift_down,
                                          bool caps_lock_on) {
    if (wm_settings_scene_editing_nickname(scene))
        wm_board_keyboard_set_physical_modifiers(scene->nickname_keyboard, shift_down,
                                                 caps_lock_on);
}

WmBoardKeyboardControl wm_settings_scene_keyboard_hit(WmSettingsScene *scene, int x,
                                                      int y) {
    if (!scene || scene->nickname_keyboard_phase != SETTINGS_NICKNAME_KEYBOARD_OPEN)
        return WM_KEYBOARD_NONE;
    return wm_board_keyboard_hit(scene->nickname_keyboard, x, y);
}

bool wm_settings_scene_keyboard_place_caret(WmSettingsScene *scene, int x, int y) {
    if (!scene || scene->nickname_keyboard_phase != SETTINGS_NICKNAME_KEYBOARD_OPEN)
        return false;
    size_t selected;
    if (!wm_board_keyboard_hit_text_caret(scene->nickname_keyboard, x, y, &selected))
        return false;
    scene->nickname_caret = (unsigned)selected;
    wm_board_keyboard_finish_composition(scene->nickname_keyboard);
    wm_board_keyboard_set_caret(scene->nickname_keyboard, selected, true);
    return true;
}

void wm_settings_scene_keyboard_hover(WmSettingsScene *scene,
                                      WmBoardKeyboardControl control) {
    if (scene && scene->nickname_keyboard_phase == SETTINGS_NICKNAME_KEYBOARD_OPEN)
        wm_board_keyboard_hover(scene->nickname_keyboard, control);
}

const char *wm_settings_scene_keyboard_close(WmSettingsScene *scene, bool accept) {
    if (!wm_settings_scene_editing_nickname(scene))
        return NULL;
    if (!accept) {
        memcpy(scene->edit_nickname, scene->nickname_before_keyboard,
               sizeof(scene->edit_nickname));
    }
    wm_board_keyboard_release_hold(scene->nickname_keyboard);
    wm_board_keyboard_hover(scene->nickname_keyboard, WM_KEYBOARD_NONE);
    bool pending = scene->nickname_keyboard_phase == SETTINGS_NICKNAME_KEYBOARD_PENDING;
    scene->nickname_keyboard_phase = pending ? SETTINGS_NICKNAME_KEYBOARD_CLOSED
                                             : SETTINGS_NICKNAME_KEYBOARD_CLOSING;
    scene->nickname_keyboard_frame = 0.0f;
    return "WIPL_SE_CHAR_DECIDE";
}

const char *wm_settings_scene_keyboard_activate(WmSettingsScene *scene,
                                                WmBoardKeyboardControl control) {
    if (!scene || scene->nickname_keyboard_phase != SETTINGS_NICKNAME_KEYBOARD_OPEN ||
        control == WM_KEYBOARD_NONE)
        return NULL;
    char utf8[5] = {0};
    WmBoardKeyboardAction action =
        wm_board_keyboard_activate(scene->nickname_keyboard, control, false, utf8);
    switch (action) {
        case WM_KEYBOARD_ACTION_INSERT:
            if (utf8[0] >= 32 && utf8[0] <= 126 && !utf8[1] &&
                wm_settings_scene_type_ascii(scene, utf8[0]))
                return control == WM_KEYBOARD_SPACE ? "WIPL_SE_CHAR_DECIDE"
                                                    : "WIPL_SE_CHAR_INPUT";
            return "WIPL_SE_CHAR_DELETE_ERROR";
        case WM_KEYBOARD_ACTION_REPLACE_LAST:
            if (scene->nickname_caret && utf8[0] >= 32 && utf8[0] <= 126 && !utf8[1]) {
                scene->edit_nickname[scene->nickname_caret - 1] = utf8[0];
                memcpy(scene->nickname_keyboard_display, scene->edit_nickname,
                       sizeof(scene->nickname_keyboard_display));
                wm_board_keyboard_text_changed(scene->nickname_keyboard, true);
                return "WIPL_SE_CHAR_INPUT";
            }
            return "WIPL_SE_CHAR_DELETE_ERROR";
        case WM_KEYBOARD_ACTION_DELETE:
            return wm_settings_scene_backspace(scene) ? "WIPL_SE_CHAR_DELETE"
                                                      : "WIPL_SE_CHAR_DELETE_ERROR";
        case WM_KEYBOARD_ACTION_CLOSE_BACK:
            return wm_settings_scene_keyboard_close(scene, false);
        case WM_KEYBOARD_ACTION_CLOSE_OK:
            return wm_settings_scene_keyboard_close(scene, true);
        case WM_KEYBOARD_ACTION_SYMBOL_OPEN:
            return "WIPL_SE_SYMBOL_PAGE_OPEN";
        case WM_KEYBOARD_ACTION_SYMBOL_CLOSE:
            return "WIPL_SE_CHAR_DECIDE";
        case WM_KEYBOARD_ACTION_SYMBOL_PAGE:
            return "WSD_SELECT";
        case WM_KEYBOARD_ACTION_LAYOUT_QWERTY:
            return "WIPL_SE_SK_SWITCHING_01";
        case WM_KEYBOARD_ACTION_LAYOUT_PHONE:
            return "WIPL_SE_SK_SWITCH_TO_KETAI";
        case WM_KEYBOARD_ACTION_PHONE_MODE:
        case WM_KEYBOARD_ACTION_HANDLED:
            return "WIPL_SE_SK_SWITCHING_02";
        default:
            return NULL;
    }
}
