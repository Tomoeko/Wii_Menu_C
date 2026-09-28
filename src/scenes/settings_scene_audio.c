#include "settings_scene_internal.h"

/* These paired footers accept on the left and decline on the right. The
 * Format confirmation uses the same sound IDs for Format and No. */
static bool left_decides_right_cancels(unsigned category, unsigned detail) {
    return (category == SETTINGS_PARENTAL && detail == 0) ||
           (category == SETTINGS_INTERNET &&
            (detail == 3 || detail == INTERNET_USB_EXISTING_CONNECTOR)) ||
           (category == SETTINGS_UPDATE && detail == 0) ||
           (category == SETTINGS_FORMAT && detail == 2);
}

const char *wm_settings_scene_click_cue(const WmSettingsScene *scene,
                                         WmSettingsControl control) {
    if (!scene || scene->phase != WM_SETTINGS_READY ||
        control <= WM_SETTINGS_CONTROL_NONE ||
        control > WM_SETTINGS_CONTROL_NICKNAME_FIELD) return NULL;

    if (control == WM_SETTINGS_CONTROL_NICKNAME_FIELD)
        return "WIPL_SE_SK_OPEN";

    if (wm_settings_scene_directional_control(scene, control)) {
        if (scene->active_category == SETTINGS_CALENDAR)
            return "WIPL_SE_CHOICE_CHG";
        if (scene->active_category == SETTINGS_SCREEN && scene->detail == 1) {
            bool at_limit = control == WM_SETTINGS_CONTROL_ITEM_1
                ? scene->selection >= 32 : scene->selection == 0;
            return at_limit ? "WIPL_SE_CHAR_DELETE_ERROR"
                            : "WIPL_SE_CHOICE_CHG";
        }
        /* Settings index and Country-list scrolling both request sound ID 1. */
        return "WIPL_SE_BT_PUSH";
    }

    if (control == WM_SETTINGS_CONTROL_BACK)
        return left_decides_right_cancels(scene->active_category, scene->detail)
            ? "WIPL_SE_DECIDE" : "WIPL_SE_CANCEL";
    if (control == WM_SETTINGS_CONTROL_NEXT)
        return left_decides_right_cancels(scene->active_category, scene->detail)
            ? "WIPL_SE_CANCEL" : "WIPL_SE_DECIDE";

    if (control >= WM_SETTINGS_CONTROL_ITEM_1 &&
        control <= WM_SETTINGS_CONTROL_ITEM_6) {
        if (scene->active_category == SETTINGS_SOUND && scene->detail == 0)
            return "WIPL_SE_OUTPUT_MODE_SELECT";
        if (scene->active_category == SETTINGS_COUNTRY ||
            scene->active_category == SETTINGS_LANGUAGE ||
            (scene->active_category == SETTINGS_SCREEN && scene->detail >= 2) ||
            (scene->active_category == SETTINGS_SENSOR && scene->detail == 1) ||
            (scene->active_category == SETTINGS_CONNECT24 && scene->detail))
            return "WIPL_SE_CHOICE_CHG";
        return "WIPL_SE_DECIDE";
    }
    return NULL;
}
