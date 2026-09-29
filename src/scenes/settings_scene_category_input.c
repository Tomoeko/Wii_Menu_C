#include "settings_scene_internal.h"

#include <string.h>

static unsigned days_in_month(unsigned year, unsigned month) {
    static const unsigned lengths[12] = {31, 28, 31, 30, 31, 30,
                                         31, 31, 30, 31, 30, 31};
    if (month < 1 || month > 12)
        return 31;
    if (month != 2)
        return lengths[month - 1];
    unsigned full_year = 2000 + year;
    bool leap = full_year % 4 == 0 && (full_year % 100 != 0 || full_year % 400 == 0);
    return leap ? 29 : 28;
}

static void clamp_edit_day(WmSettingsScene *scene) {
    unsigned maximum = days_in_month(scene->edit_year, scene->edit_month);
    if (scene->edit_day > maximum)
        scene->edit_day = maximum;
}

bool settings_scene_back_control(WmSettingsScene *scene) {
    if (!scene || scene->phase != WM_SETTINGS_READY || scene->exit_pending)
        return false;
    scene->held_control = WM_SETTINGS_CONTROL_NONE;
    if (scene->detail) {
        if (scene->active_category == SETTINGS_PARENTAL && scene->detail == 2)
            scene->detail = 1;
        else if (scene->active_category == SETTINGS_INTERNET &&
                 (scene->detail == INTERNET_WIRELESS_CHOICES ||
                  scene->detail == INTERNET_WIRED_PROMPT))
            scene->detail = INTERNET_CONNECTION_SELECT;
        else if (scene->active_category == SETTINGS_INTERNET &&
                 (scene->detail == INTERNET_ACCESS_POINT_SEARCH ||
                  scene->detail == INTERNET_NO_ACCESS_POINT))
            scene->detail = INTERNET_WIRELESS_CHOICES;
        else if (scene->active_category == SETTINGS_INTERNET &&
                 scene->detail == INTERNET_USB_INSTRUCTIONS)
            scene->detail = INTERNET_WIRELESS_CHOICES;
        else if (scene->active_category == SETTINGS_INTERNET &&
                 (scene->detail == INTERNET_USB_REGISTRATION ||
                  scene->detail == INTERNET_USB_EXISTING_CONNECTOR))
            scene->detail = INTERNET_USB_INSTRUCTIONS;
        else if (scene->active_category == SETTINGS_INTERNET &&
                 scene->detail == INTERNET_CONNECTION_SELECT)
            scene->detail = 1;
        else if (scene->active_category == SETTINGS_FORMAT) {
            scene->detail = 0;
            scene->active_category = 0;
        } else
            scene->detail = 0;
        scene->sensitivity_instructions = false;
        scene->hover = WM_SETTINGS_CONTROL_NONE;
        return true;
    }
    if (scene->active_category) {
        if (scene->direct_entry) {
            scene->exit_pending = true;
            return true;
        }
        scene->active_category = 0;
        scene->hover = WM_SETTINGS_CONTROL_NONE;
        return true;
    }
    scene->exit_pending = true;
    scene->hover = WM_SETTINGS_CONTROL_NONE;
    return true;
}

static bool activate_extended_category(WmSettingsScene *scene,
                                       WmSettingsControl control) {
    unsigned category = scene->active_category;
    unsigned item =
        control >= WM_SETTINGS_CONTROL_ITEM_1 && control <= WM_SETTINGS_CONTROL_ITEM_6
            ? (unsigned)(control - WM_SETTINGS_CONTROL_ITEM_1)
            : 6;
    if (category == SETTINGS_NICKNAME) {
        if (control == WM_SETTINGS_CONTROL_BACK)
            return settings_scene_back_control(scene);
        if (control != WM_SETTINGS_CONTROL_NEXT)
            return false;
        memcpy(scene->nickname, scene->edit_nickname, sizeof(scene->nickname));
        scene->active_category = 0;
    } else if (category == SETTINGS_PARENTAL) {
        if (control == WM_SETTINGS_CONTROL_BACK) {
            if (!scene->detail)
                scene->detail = 1; /* Yes. */
            else
                return settings_scene_back_control(scene);
        } else if (control == WM_SETTINGS_CONTROL_NEXT) {
            if (!scene->detail)
                scene->active_category = 0; /* No. */
            else if (scene->detail == 1)
                scene->detail = 2;
            else {
                /* A local guard only. Rating, PIN, and title restriction
                 * services are separate unported console functions. */
                scene->parental_enabled = true;
                scene->active_category = 0;
                scene->detail = 0;
            }
        } else
            return false;
    } else if (category == SETTINGS_INTERNET) {
        if (scene->detail == 3 && (control == WM_SETTINGS_CONTROL_BACK ||
                                   control == WM_SETTINGS_CONTROL_NEXT)) {
            /* The left action accepts; the right action declines. */
            scene->internet_agreement = control == WM_SETTINGS_CONTROL_BACK;
            return settings_scene_back_control(scene);
        }
        if (scene->detail == INTERNET_USB_EXISTING_CONNECTOR &&
            control == WM_SETTINGS_CONTROL_BACK) {
            /* The visible left Yes retries registration. The footer pane
             * names are swapped, so use their displayed positions. */
            scene->detail = INTERNET_USB_REGISTRATION;
            scene->connection_search_frames = 0.0f;
            scene->hover = WM_SETTINGS_CONTROL_NONE;
            return true;
        }
        if (control == WM_SETTINGS_CONTROL_BACK)
            return settings_scene_back_control(scene);
        if (scene->detail == INTERNET_WIRED_PROMPT &&
            control == WM_SETTINGS_CONTROL_NEXT) {
            /* No network test service is available; return to mode choices. */
            scene->detail = INTERNET_CONNECTION_SELECT;
        } else if (scene->detail == INTERNET_NO_ACCESS_POINT &&
                   control == WM_SETTINGS_CONTROL_NEXT) {
            scene->detail = INTERNET_WIRELESS_CHOICES;
        } else if (scene->detail == INTERNET_USB_INSTRUCTIONS &&
                   control == WM_SETTINGS_CONTROL_NEXT) {
            scene->detail = INTERNET_USB_REGISTRATION;
            scene->connection_search_frames = 0.0f;
        } else if (scene->detail == INTERNET_USB_EXISTING_CONNECTOR &&
                   control == WM_SETTINGS_CONTROL_NEXT) {
            /* No connector exists locally; decline and return to wireless
             * choices instead of claiming setup completed. */
            scene->detail = INTERNET_WIRELESS_CHOICES;
        } else if (!scene->detail && item < 3) {
            scene->detail = item + 1;
        } else if (scene->detail == 1 && item < 3) {
            scene->connection_slot = item + 1;
            scene->detail = INTERNET_CONNECTION_SELECT;
        } else if (scene->detail == INTERNET_CONNECTION_SELECT && item < 2) {
            scene->detail =
                item == 0 ? INTERNET_WIRELESS_CHOICES : INTERNET_WIRED_PROMPT;
        } else if (scene->detail == INTERNET_WIRELESS_CHOICES && item == 0) {
            scene->detail = INTERNET_ACCESS_POINT_SEARCH;
            scene->connection_search_frames = 0.0f;
        } else if (scene->detail == INTERNET_WIRELESS_CHOICES && item == 1) {
            scene->detail = INTERNET_USB_INSTRUCTIONS;
        } else
            return false;
    } else if (category == SETTINGS_CONNECT24) {
        if (control == WM_SETTINGS_CONTROL_BACK)
            return settings_scene_back_control(scene);
        if (!scene->detail && item < 3 && (scene->connect24_enabled || item == 0)) {
            scene->detail = item + 1;
            scene->selection = scene->detail == 1   ? (scene->connect24_enabled ? 0 : 1)
                               : scene->detail == 2 ? (scene->standby_enabled ? 0 : 1)
                                                    : scene->slot_light;
        } else if (scene->detail && item < (scene->detail == 3 ? 3u : 2u)) {
            scene->selection = item;
            if (scene->detail == 1) {
                /* Row selection commits the value; Back and Confirm keep it. */
                scene->connect24_enabled = item == 0;
            }
        } else if (scene->detail && control == WM_SETTINGS_CONTROL_NEXT) {
            switch (scene->detail) {
                case 1:
                    scene->connect24_enabled = scene->selection == 0;
                    break;
                case 2:
                    scene->standby_enabled = scene->selection == 0;
                    break;
                case 3:
                    scene->slot_light = scene->selection;
                    break;
            }
            scene->detail = 0;
        } else
            return false;
    } else if (category == SETTINGS_COUNTRY) {
        if (control == WM_SETTINGS_CONTROL_BACK)
            return settings_scene_back_control(scene);
        if (control == WM_SETTINGS_CONTROL_PREVIOUS && scene->country_page > 0) {
            scene->country_page--;
        } else if (control == WM_SETTINGS_CONTROL_ITEM_6 && scene->country_page < 9) {
            scene->country_page++;
        } else if (item <
                   (scene->country_page == 0 || scene->country_page == 9 ? 4u : 5u)) {
            scene->edit_country_choice =
                wm_settings_country_page_start[scene->country_page] + item;
        } else if (control == WM_SETTINGS_CONTROL_NEXT) {
            scene->country_choice = scene->edit_country_choice;
            scene->active_category = 0;
        } else
            return false;
    } else if (category == SETTINGS_UPDATE) {
        if (!scene->detail) {
            /* The left action opens the offline explanation; the right exits. */
            if (control == WM_SETTINGS_CONTROL_BACK)
                scene->detail = 1;
            else if (control == WM_SETTINGS_CONTROL_NEXT)
                return settings_scene_back_control(scene);
            else
                return false;
        } else if (control == WM_SETTINGS_CONTROL_BACK) {
            return settings_scene_back_control(scene);
        } else if (control == WM_SETTINGS_CONTROL_NEXT) {
            scene->detail = 0;
            scene->active_category = 0;
        } else
            return false;
    } else if (category == SETTINGS_FORMAT) {
        if (scene->detail == 2 && control == WM_SETTINGS_CONTROL_BACK) {
            /* The final left action resets local volatile settings only;
             * it never modifies NAND or channel files. */
            settings_scene_reset_local_values(scene);
            scene->local_format_complete = true;
            scene->detail = 3;
        } else if (control == WM_SETTINGS_CONTROL_BACK) {
            return settings_scene_back_control(scene);
        } else if (control == WM_SETTINGS_CONTROL_NEXT) {
            if (scene->detail < 2)
                scene->detail++;
            else {
                scene->detail = 0;
                scene->active_category = 0;
            }
        } else
            return false;
    } else {
        return false;
    }
    scene->hover = WM_SETTINGS_CONTROL_NONE;
    return true;
}

bool settings_scene_activate_category(WmSettingsScene *scene,
                                      WmSettingsControl control) {
    if (!scene || !scene->active_category || scene->phase != WM_SETTINGS_READY ||
        scene->exit_pending || control <= WM_SETTINGS_CONTROL_NONE ||
        control > WM_SETTINGS_CONTROL_ITEM_6)
        return false;
    if (scene->active_category == SETTINGS_NICKNAME ||
        scene->active_category == SETTINGS_PARENTAL ||
        scene->active_category == SETTINGS_INTERNET ||
        scene->active_category == SETTINGS_CONNECT24 ||
        scene->active_category == SETTINGS_COUNTRY ||
        scene->active_category == SETTINGS_UPDATE ||
        scene->active_category == SETTINGS_FORMAT)
        return activate_extended_category(scene, control);
    if (control == WM_SETTINGS_CONTROL_BACK)
        return settings_scene_back_control(scene);
    if (control == WM_SETTINGS_CONTROL_NEXT) {
        if (scene->active_category == SETTINGS_CALENDAR && scene->detail == 1) {
            scene->year = scene->edit_year;
            scene->month = scene->edit_month;
            scene->day = scene->edit_day;
            scene->detail = 0;
        } else if (scene->active_category == SETTINGS_CALENDAR && scene->detail == 2) {
            scene->hour = scene->edit_hour;
            scene->minute = scene->edit_minute;
            scene->detail = 0;
        } else if (scene->active_category == SETTINGS_SOUND) {
            scene->sound_choice = scene->selection;
            scene->active_category = 0;
        } else if (scene->active_category == SETTINGS_LANGUAGE) {
            scene->language_choice = scene->selection;
            scene->active_category = 0;
        } else if (scene->active_category == SETTINGS_SCREEN && scene->detail) {
            switch (scene->detail) {
                case 1:
                    scene->screen_position = scene->selection;
                    break;
                case 2:
                    scene->widescreen_choice = scene->selection;
                    break;
                case 3:
                    scene->resolution_choice = scene->selection;
                    break;
                case 4:
                    scene->burn_in_choice = scene->selection;
                    break;
            }
            scene->detail = 0;
        } else if (scene->active_category == SETTINGS_SENSOR && scene->detail == 1) {
            scene->sensor_position = scene->selection == 0 ? 1 : 0;
            scene->detail = 0;
        } else if (scene->active_category == SETTINGS_SENSOR && scene->detail == 2) {
            if (scene->sensitivity_instructions)
                scene->sensitivity_instructions = false;
            else {
                scene->sensitivity = scene->edit_sensitivity;
                scene->detail = 0;
            }
        } else {
            return false;
        }
        scene->hover = WM_SETTINGS_CONTROL_NONE;
        return true;
    }
    if (control < WM_SETTINGS_CONTROL_ITEM_1 || control > WM_SETTINGS_CONTROL_ITEM_6)
        return false;
    unsigned item = (unsigned)(control - WM_SETTINGS_CONTROL_ITEM_1);
    if (scene->active_category == SETTINGS_CALENDAR && !scene->detail && item < 2) {
        scene->detail = item + 1;
        scene->edit_year = scene->year;
        scene->edit_month = scene->month;
        scene->edit_day = scene->day;
        scene->edit_hour = scene->hour;
        scene->edit_minute = scene->minute;
        scene->hover = WM_SETTINGS_CONTROL_NONE;
        return true;
    }
    if (scene->active_category == SETTINGS_CALENDAR && scene->detail == 1) {
        switch (item) {
            case 0:
                scene->edit_year = (scene->edit_year + 1) % 36;
                clamp_edit_day(scene);
                break;
            case 1:
                scene->edit_year = (scene->edit_year + 35) % 36;
                clamp_edit_day(scene);
                break;
            case 2:
                scene->edit_month = scene->edit_month == 12 ? 1 : scene->edit_month + 1;
                clamp_edit_day(scene);
                break;
            case 3:
                scene->edit_month = scene->edit_month == 1 ? 12 : scene->edit_month - 1;
                clamp_edit_day(scene);
                break;
            case 4:
                scene->edit_day = scene->edit_day == days_in_month(scene->edit_year,
                                                                   scene->edit_month)
                                      ? 1
                                      : scene->edit_day + 1;
                break;
            case 5:
                scene->edit_day =
                    scene->edit_day == 1
                        ? days_in_month(scene->edit_year, scene->edit_month)
                        : scene->edit_day - 1;
                break;
        }
        return true;
    }
    if (scene->active_category == SETTINGS_CALENDAR && scene->detail == 2) {
        switch (item) {
            case 0:
                scene->edit_hour = (scene->edit_hour + 1) % 24;
                break;
            case 1:
                scene->edit_hour = (scene->edit_hour + 23) % 24;
                break;
            case 2:
                scene->edit_minute = (scene->edit_minute + 1) % 60;
                break;
            case 3:
                scene->edit_minute = (scene->edit_minute + 59) % 60;
                break;
            default:
                return false;
        }
        return true;
    }
    if (scene->active_category == SETTINGS_SOUND ||
        scene->active_category == SETTINGS_LANGUAGE) {
        if (item >= 3)
            return false;
        scene->selection = item;
        return true;
    }
    if (!scene->detail && scene->active_category == SETTINGS_SCREEN) {
        scene->detail = item + 1;
        switch (scene->detail) {
            case 1:
                scene->selection = scene->screen_position;
                break;
            case 2:
                scene->selection = scene->widescreen_choice;
                break;
            case 3:
                scene->selection = scene->resolution_choice;
                break;
            case 4:
                scene->selection = scene->burn_in_choice;
                break;
        }
        scene->hover = WM_SETTINGS_CONTROL_NONE;
        return true;
    }
    if (!scene->detail && scene->active_category == SETTINGS_SENSOR && item < 2) {
        scene->detail = item + 1;
        scene->selection = scene->sensor_position == 1 ? 0 : 1;
        scene->edit_sensitivity = scene->sensitivity;
        scene->sensitivity_instructions = item == 1;
        scene->hover = WM_SETTINGS_CONTROL_NONE;
        return true;
    }
    if (scene->active_category == SETTINGS_SENSOR && scene->detail == 1) {
        if (item >= 2)
            return false;
        /* Above TV maps to 1, Below TV to 0; selection commits the value. */
        scene->selection = item;
        scene->sensor_position = item == 0 ? 1 : 0;
        return true;
    }
    if (scene->active_category == SETTINGS_SENSOR && scene->detail == 2) {
        if (scene->sensitivity_instructions || item >= 2)
            return false;
        if (item == 0 && scene->edit_sensitivity > 1)
            scene->edit_sensitivity--;
        else if (item == 1 && scene->edit_sensitivity < 5)
            scene->edit_sensitivity++;
        return true;
    }
    if (scene->active_category == SETTINGS_SCREEN && scene->detail == 1) {
        if (item >= 2)
            return false;
        /* Left moves the position by +2; right moves it by -2. */
        int position = (int)scene->selection + (item ? -2 : 2);
        if (position < 0)
            position = 0;
        if (position > 32)
            position = 32;
        scene->selection = (unsigned)position;
        /* Each arrow press commits the position; Back retains it. */
        scene->screen_position = scene->selection;
        return true;
    }
    if (scene->active_category == SETTINGS_SCREEN && scene->detail == 2) {
        if (item >= 2)
            return false;
        /* Selection commits the local widescreen choice; it does not
         * change the host window's output projection. */
        scene->selection = item;
        scene->widescreen_choice = item;
        return true;
    }
    if (scene->active_category == SETTINGS_SCREEN &&
        (scene->detail == 3 || scene->detail == 4)) {
        if (item >= 2)
            return false;
        /* Selection commits either local value, so Back retains the row. */
        scene->selection = item;
        if (scene->detail == 3)
            scene->resolution_choice = item;
        else
            scene->burn_in_choice = item;
        return true;
    }
    if (scene->detail && item < 2) {
        scene->selection = item;
        return true;
    }
    return false;
}
