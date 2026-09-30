#include "settings_scene_internal.h"

#include "wii_menu/layout/layout_assets.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

const unsigned wm_settings_country_page_start[10] = {0,  4,  9,  14, 19,
                                                     24, 29, 34, 39, 44};

static float settings_source_x(const WmSettingsScene *scene, float frame_x) {
    return scene->wide ? frame_x * ((float)SETTINGS_WIDE_WIDTH / WM_FRAME_WIDTH) -
                             SETTINGS_SIDE_WIDTH + 16.0f
                       : frame_x;
}

static void clear_hover_presentation(WmSettingsScene *scene) {
    scene->hover = WM_SETTINGS_CONTROL_NONE;
}

static SettingsPageSnapshot page_snapshot(const WmSettingsScene *scene) {
    SettingsPageSnapshot snapshot = {
        .phase = scene->phase,
        .hover = scene->hover,
        .page = scene->page,
        .previous_page = scene->previous_page,
        .active_category = scene->active_category,
        .detail = scene->detail,
        .selection = scene->selection,
        .language_choice = scene->language_choice,
        .edit_year = scene->edit_year,
        .edit_month = scene->edit_month,
        .edit_day = scene->edit_day,
        .edit_hour = scene->edit_hour,
        .edit_minute = scene->edit_minute,
        .edit_sensitivity = scene->edit_sensitivity,
        .connection_slot = scene->connection_slot,
        .country_page = scene->country_page,
        .edit_country_choice = scene->edit_country_choice,
        .connect24_enabled = scene->connect24_enabled,
        .sensitivity_instructions = scene->sensitivity_instructions,
        .wide = scene->wide,
        .direction = scene->direction,
        .phase_frame = scene->phase_frame,
        .page_frame = scene->page_frame,
        .nickname_keyboard_phase = scene->nickname_keyboard_phase,
        .nickname_caret = scene->nickname_caret};
    memcpy(snapshot.edit_nickname, scene->edit_nickname,
           sizeof(snapshot.edit_nickname));
    return snapshot;
}

static void start_page_crossfade(WmSettingsScene *scene,
                                 const SettingsPageSnapshot *before) {
    scene->prior_page = *before;
    scene->page_frame = 0.0f;
    scene->page_crossfade = true;
}

static bool within(int x, int y, int left, int top, int width, int height) {
    return x >= left && x < left + width && y >= top && y < top + height;
}

void settings_scene_reset_local_values(WmSettingsScene *scene) {
    scene->sound_choice = 1;    /* Stereo */
    scene->language_choice = 0; /* English */
    scene->sensor_position = 0;
    scene->screen_position = 16;
    scene->widescreen_choice = 1; /* 16:9 */
    /* The local display defaults to 480p, the first resolution row. */
    scene->resolution_choice = 0; /* EDTV or HDTV (480p) */
    scene->burn_in_choice = 0;
    scene->sensitivity = 3;
    scene->parental_enabled = false;
    scene->connect24_enabled = false;
    scene->standby_enabled = false;
    scene->slot_light = 0;
    scene->internet_agreement = false;
    /* Country code 49 is entry 41 of the first list, which begins at 8. */
    scene->country_choice = 41;
    memset(scene->nickname, 0, sizeof(scene->nickname));
    memcpy(scene->nickname, "Wii", 4);

    time_t now = time(NULL);
    struct tm *clock_time = localtime(&now);
    if (clock_time) {
        int year = clock_time->tm_year + 1900;
        scene->year = (unsigned)(year < 2000 ? 0 : year > 2035 ? 35 : year - 2000);
        scene->month = (unsigned)(clock_time->tm_mon + 1);
        scene->day = (unsigned)clock_time->tm_mday;
        scene->hour = (unsigned)clock_time->tm_hour;
        scene->minute = (unsigned)clock_time->tm_min;
    } else {
        scene->year = 0;
        scene->month = 1;
        scene->day = 1;
        scene->hour = 0;
        scene->minute = 0;
    }
    if (scene->year == 35 && clock_time && clock_time->tm_year + 1900 > 2035) {
        scene->month = 12;
        scene->day = 31;
    }
}

WmSettingsControl wm_settings_index_hit(unsigned page, int x, int y) {
    if (page < 1 || page > SETTINGS_INDEX_PAGES || x < 0 || y < 0 ||
        x >= WM_FRAME_WIDTH || y >= WM_FRAME_HEIGHT)
        return WM_SETTINGS_CONTROL_NONE;

    /* The original documents are 608 pixels wide and centered by Setting's
     * framebuffer presentation in the 640-pixel logical viewport. */
    int local_x = x - 16;
    if (within(local_x, y, 28, 371, 272, 72))
        return WM_SETTINGS_CONTROL_BACK;
    if (page > 1 && within(local_x, y, 22, 180, 72, 72))
        return WM_SETTINGS_CONTROL_PREVIOUS;
    if (page < SETTINGS_INDEX_PAGES && within(local_x, y, 514, 180, 72, 72))
        return WM_SETTINGS_CONTROL_NEXT;
    for (int index = 0; index < SETTINGS_ITEMS_PER_PAGE; index++) {
        if (within(local_x, y, 104, 78 + index * 72, 400, 60))
            return (WmSettingsControl)(WM_SETTINGS_CONTROL_ITEM_1 + index);
    }
    return WM_SETTINGS_CONTROL_NONE;
}

static bool implemented_category(unsigned category) {
    return category >= SETTINGS_NICKNAME && category <= SETTINGS_FORMAT;
}

static WmSettingsControl choice_at(int local_x, int y, int first_y, int count) {
    for (int index = 0; index < count; index++) {
        if (within(local_x, y, 107, first_y + index * 96, 394, 70))
            return (WmSettingsControl)(WM_SETTINGS_CONTROL_ITEM_1 + index);
    }
    return WM_SETTINGS_CONTROL_NONE;
}

static bool category_has_next_action(const WmSettingsScene *scene) {
    return scene->active_category == SETTINGS_SOUND ||
           scene->active_category == SETTINGS_LANGUAGE ||
           (scene->detail != 0 && scene->active_category != SETTINGS_INTERNET) ||
           (scene->active_category == SETTINGS_INTERNET &&
            (scene->detail == 3 || scene->detail == INTERNET_WIRED_PROMPT ||
             scene->detail == INTERNET_NO_ACCESS_POINT ||
             scene->detail == INTERNET_USB_INSTRUCTIONS ||
             scene->detail == INTERNET_USB_EXISTING_CONNECTOR)) ||
           scene->active_category == SETTINGS_NICKNAME ||
           scene->active_category == SETTINGS_PARENTAL ||
           scene->active_category == SETTINGS_COUNTRY ||
           scene->active_category == SETTINGS_UPDATE ||
           scene->active_category == SETTINGS_FORMAT;
}

static WmSettingsControl category_overview_hit(const WmSettingsScene *scene,
                                               int local_x, int y) {
    if (scene->active_category == SETTINGS_SCREEN) {
        for (int item = 0; item < 4; item++) {
            if (within(local_x, y, 104, 78 + item * 72, 400, 60))
                return (WmSettingsControl)(WM_SETTINGS_CONTROL_ITEM_1 + item);
        }
    } else if (scene->active_category == SETTINGS_SOUND ||
               scene->active_category == SETTINGS_LANGUAGE) {
        return choice_at(local_x, y, 85, 3);
    } else if (scene->active_category == SETTINGS_CALENDAR ||
               scene->active_category == SETTINGS_SENSOR) {
        return choice_at(local_x, y, 133, 2);
    } else if (scene->active_category == SETTINGS_INTERNET ||
               scene->active_category == SETTINGS_CONNECT24) {
        if (scene->active_category == SETTINGS_CONNECT24 && !scene->connect24_enabled)
            return choice_at(local_x, y, 85, 1);
        return choice_at(local_x, y, 85, 3);
    } else if (scene->active_category == SETTINGS_COUNTRY) {
        if (scene->country_page > 0 && within(local_x, y, 528, 76, 72, 72))
            return WM_SETTINGS_CONTROL_PREVIOUS;
        if (scene->country_page < 9 && within(local_x, y, 528, 282, 72, 72))
            return WM_SETTINGS_CONTROL_ITEM_6;
        unsigned count = scene->country_page == 0 || scene->country_page == 9 ? 4 : 5;
        int first_y = scene->country_page == 0 ? 132 : 76;
        for (unsigned item = 0; item < count; item++) {
            if (within(local_x, y, 88, first_y + (int)item * 56, 432, 56))
                return (WmSettingsControl)(WM_SETTINGS_CONTROL_ITEM_1 + item);
        }
    }
    return WM_SETTINGS_CONTROL_NONE;
}

static WmSettingsControl category_detail_hit(const WmSettingsScene *scene, int local_x,
                                             int y) {
    if (scene->active_category == SETTINGS_INTERNET) {
        if (scene->detail == 1)
            return choice_at(local_x, y, 85, 3);
        if (scene->detail == INTERNET_CONNECTION_SELECT)
            return choice_at(local_x, y, 133, 2);
        if (scene->detail == INTERNET_WIRELESS_CHOICES) {
            WmSettingsControl full_row = choice_at(local_x, y, 85, 2);
            if (full_row != WM_SETTINGS_CONTROL_NONE)
                return full_row;
            if (within(local_x, y, 104, 274, 168, 76))
                return WM_SETTINGS_CONTROL_ITEM_3;
            if (within(local_x, y, 339, 277, 168, 70))
                return WM_SETTINGS_CONTROL_ITEM_4;
        }
        return WM_SETTINGS_CONTROL_NONE;
    }
    if (scene->active_category == SETTINGS_CONNECT24) {
        return choice_at(local_x, y, scene->detail == 3 ? 85 : 133,
                         scene->detail == 3 ? 3 : 2);
    }
    if (scene->active_category == SETTINGS_CALENDAR && scene->detail == 1) {
        /* The USA English stylesheet moves Year to the right of Month/Day.
         * The common stylesheet's positions are different. */
        const int arrow_x[6] = {400, 400, 88, 88, 224, 224};
        const int arrow_y[6] = {108, 253, 108, 253, 108, 253};
        for (unsigned index = 0; index < 6; index++) {
            if (within(local_x, y, arrow_x[index], arrow_y[index], 72, 72))
                return (WmSettingsControl)(WM_SETTINGS_CONTROL_ITEM_1 + index);
        }
    } else if (scene->active_category == SETTINGS_CALENDAR && scene->detail == 2) {
        const int arrow_x[4] = {200, 200, 336, 336};
        const int arrow_y[4] = {108, 253, 108, 253};
        for (unsigned index = 0; index < 4; index++) {
            if (within(local_x, y, arrow_x[index], arrow_y[index], 64, 64))
                return (WmSettingsControl)(WM_SETTINGS_CONTROL_ITEM_1 + index);
        }
    } else if (scene->active_category == SETTINGS_SENSOR && scene->detail == 2) {
        if (scene->sensitivity_instructions)
            return WM_SETTINGS_CONTROL_NONE;
        if (within(local_x, y, 24, 296, 132, 48))
            return WM_SETTINGS_CONTROL_ITEM_1;
        if (within(local_x, y, 452, 296, 132, 48))
            return WM_SETTINGS_CONTROL_ITEM_2;
    } else if (scene->active_category == SETTINGS_SCREEN && scene->detail == 1) {
        if (within(local_x, y, 160, 180, 64, 64))
            return WM_SETTINGS_CONTROL_ITEM_1;
        if (within(local_x, y, 376, 180, 64, 64))
            return WM_SETTINGS_CONTROL_ITEM_2;
    } else if (scene->active_category == SETTINGS_SCREEN && scene->detail == 2) {
        if (within(local_x, y, 48, 146, 200, 140))
            return WM_SETTINGS_CONTROL_ITEM_1;
        if (within(local_x, y, 296, 146, 264, 140))
            return WM_SETTINGS_CONTROL_ITEM_2;
    } else {
        return choice_at(local_x, y, 133, 2);
    }
    return WM_SETTINGS_CONTROL_NONE;
}

static WmSettingsControl category_hit(const WmSettingsScene *scene, int x, int y) {
    if (x < 16 || y < 0 || x >= 624 || y >= WM_FRAME_HEIGHT)
        return WM_SETTINGS_CONTROL_NONE;
    int local_x = x - 16;
    if (scene->active_category == SETTINGS_NICKNAME &&
        within(x, y, SETTINGS_NICKNAME_FIELD_X, SETTINGS_NICKNAME_FIELD_Y,
               SETTINGS_NICKNAME_FIELD_WIDTH, SETTINGS_NICKNAME_FIELD_HEIGHT))
        return WM_SETTINGS_CONTROL_NICKNAME_FIELD;
    bool sensitivity_meter = scene->active_category == SETTINGS_SENSOR &&
                             scene->detail == 2 && !scene->sensitivity_instructions;
    /* The meter has no footer; its instruction line accepts a pointer click
     * in place of the Wii Remote A press. */
    if (sensitivity_meter && within(local_x, y, 24, 344, 560, 95))
        return WM_SETTINGS_CONTROL_NEXT;
    if (within(local_x, y, 28, 371, 272, 72) && !sensitivity_meter &&
        !(scene->active_category == SETTINGS_INTERNET &&
          (scene->detail == INTERNET_WIRED_PROMPT ||
           scene->detail == INTERNET_ACCESS_POINT_SEARCH ||
           scene->detail == INTERNET_NO_ACCESS_POINT)) &&
        !(scene->active_category == SETTINGS_FORMAT && scene->detail == 3))
        return WM_SETTINGS_CONTROL_BACK;
    if (category_has_next_action(scene) && !sensitivity_meter &&
        within(local_x, y, 308, 371, 272, 72))
        return WM_SETTINGS_CONTROL_NEXT; /* Confirm or OK. */
    return scene->detail ? category_detail_hit(scene, local_x, y)
                         : category_overview_hit(scene, local_x, y);
}

WmSettingsScene *wm_settings_scene_create(WmPlatform *platform,
                                          const char *assets_directory,
                                          WmTextureCache *textures,
                                          WmFontCache *fonts) {
    if (!platform || !assets_directory || !assets_directory[0] || !textures || !fonts)
        return NULL;
    WmSettingsScene *scene = calloc(1, sizeof(*scene));
    if (!scene)
        return NULL;
    scene->platform = platform;
    scene->textures = textures;
    scene->fonts = fonts;
    size_t assets_length = strlen(assets_directory) + 1;
    scene->assets_directory = malloc(assets_length);
    if (!scene->assets_directory) {
        free(scene);
        return NULL;
    }
    memcpy(scene->assets_directory, assets_directory, assets_length);
    scene->phase = WM_SETTINGS_CLOSED;
    settings_scene_reset_local_values(scene);
    scene->scroll_layout =
        wm_layout_load_asset(assets_directory, "layouts/setting/SceenChange_b.json",
                             "Wii Settings page transition");
    if (!scene->scroll_layout) {
        wm_settings_scene_destroy(scene);
        return NULL;
    }
    WmLayoutAnimationInfo left, right;
    if (!wm_layout_animation_info(scene->scroll_layout, "SceenChange_b_Left", &left) ||
        !wm_layout_animation_info(scene->scroll_layout, "SceenChange_b_Right",
                                  &right) ||
        left.frames != 41.0f || right.frames != 41.0f) {
        wm_settings_scene_destroy(scene);
        return NULL;
    }
    scene->draw_opacity = 1.0f;
    char path[WM_LAYOUT_ASSET_PATH_CAPACITY];
    if (wm_layout_asset_path(path, sizeof(path), assets_directory,
                             "fonts/settings-latin.ttc"))
        scene->outline_font = wm_outline_font_load(path, 1);
    return scene;
}

void wm_settings_scene_destroy(WmSettingsScene *scene) {
    if (!scene)
        return;
    wm_board_keyboard_destroy(scene->nickname_keyboard);
    free(scene->assets_directory);
    wm_outline_font_destroy(scene->outline_font, scene->platform);
    wm_layout_destroy(scene->scroll_layout);
    free(scene);
}

static void reset_settings_presentation(WmSettingsScene *scene, WmSettingsPhase phase,
                                        float frame) {
    scene->page = 1;
    scene->previous_page = 1;
    scene->phase = phase;
    scene->phase_frame = frame;
    scene->hover = WM_SETTINGS_CONTROL_NONE;
    scene->nickname_keyboard_phase = SETTINGS_NICKNAME_KEYBOARD_CLOSED;
    scene->nickname_keyboard_frame = 0.0f;
    clear_hover_presentation(scene);
    scene->pending_category = 0;
    scene->active_category = 0;
    scene->detail = 0;
    scene->connection_search_frames = 0.0f;
    scene->selection = 0;
    scene->sensitivity_instructions = false;
    scene->held_control = WM_SETTINGS_CONTROL_NONE;
    scene->repeat_cue_pending = false;
    scene->exit_pending = false;
    scene->direction = 0;
    scene->page_crossfade = false;
    scene->page_frame = 20.0f;
    scene->draw_opacity = 1.0f;
    scene->direct_entry = false;
}

bool wm_settings_scene_open(WmSettingsScene *scene) {
    if (!scene)
        return false;
    reset_settings_presentation(scene, WM_SETTINGS_APPEAR, -1.0f);
    return true;
}

static bool open_direct_category(WmSettingsScene *scene, unsigned category) {
    if (!wm_settings_scene_open(scene))
        return false;
    scene->page = 2;
    scene->previous_page = 2;
    scene->active_category = category;
    scene->direct_entry = true;
    return true;
}

bool wm_settings_scene_open_internet(WmSettingsScene *scene) {
    return open_direct_category(scene, SETTINGS_INTERNET);
}

bool wm_settings_scene_open_connect24(WmSettingsScene *scene) {
    return open_direct_category(scene, SETTINGS_CONNECT24);
}

void wm_settings_scene_reset(WmSettingsScene *scene) {
    if (!scene)
        return;
    reset_settings_presentation(scene, WM_SETTINGS_CLOSED, 0.0f);
}

void wm_settings_scene_advance(WmSettingsScene *scene, float frames) {
    if (!scene || !isfinite(frames) || frames < 0.0f)
        return;
    settings_scene_advance_nickname_keyboard(scene, frames);
    if (scene->phase == WM_SETTINGS_APPEAR) {
        scene->phase_frame += frames;
        if (scene->phase_frame >= 20.0f) {
            scene->phase = WM_SETTINGS_READY;
            scene->phase_frame = 20.0f;
        }
    } else if (scene->phase == WM_SETTINGS_SCROLL) {
        scene->phase_frame += frames;
        if (scene->phase_frame >= 40.0f) {
            scene->phase = WM_SETTINGS_READY;
            scene->phase_frame = 40.0f;
            scene->direction = 0;
        }
    } else if (scene->phase == WM_SETTINGS_READY &&
               scene->held_control != WM_SETTINGS_CONTROL_NONE) {
        scene->hold_elapsed = fminf(scene->hold_elapsed + frames, 100000.0f);
        for (unsigned repeats = 0;
             repeats < 128 && scene->hold_elapsed >= scene->next_repeat; repeats++) {
            if (!wm_settings_scene_activate(scene, scene->held_control)) {
                scene->held_control = WM_SETTINGS_CONTROL_NONE;
                break;
            }
            scene->repeat_cue_pending = true;
            scene->next_repeat += 9.0f;
        }
        if (scene->next_repeat <= scene->hold_elapsed)
            scene->next_repeat = scene->hold_elapsed + 9.0f;
    }
    if (scene->phase == WM_SETTINGS_READY) {
        if (scene->page_crossfade) {
            scene->page_frame += frames;
            if (scene->page_frame >= 20.0f) {
                scene->page_frame = 20.0f;
                scene->page_crossfade = false;
            }
        }
        if (scene->active_category == SETTINGS_INTERNET &&
            scene->detail == INTERNET_ACCESS_POINT_SEARCH) {
            /* The local search reports no access point after 60 frames. */
            scene->connection_search_frames += frames;
            if (scene->connection_search_frames >= 60.0f) {
                SettingsPageSnapshot before = page_snapshot(scene);
                scene->detail = INTERNET_NO_ACCESS_POINT;
                scene->hover = WM_SETTINGS_CONTROL_NONE;
                clear_hover_presentation(scene);
                start_page_crossfade(scene, &before);
            }
        }
        if (scene->active_category == SETTINGS_INTERNET &&
            scene->detail == INTERNET_USB_REGISTRATION) {
            /* The local USB registration attempt reports failure after
             * 60 frames; no connector service is available. */
            scene->connection_search_frames += frames;
            if (scene->connection_search_frames >= 60.0f) {
                SettingsPageSnapshot before = page_snapshot(scene);
                scene->detail = INTERNET_USB_EXISTING_CONNECTOR;
                scene->hover = WM_SETTINGS_CONTROL_NONE;
                clear_hover_presentation(scene);
                start_page_crossfade(scene, &before);
            }
        }
    }
}

bool wm_settings_scene_take_repeat_cue(WmSettingsScene *scene) {
    if (!scene)
        return false;
    bool pending = scene->repeat_cue_pending;
    scene->repeat_cue_pending = false;
    return pending;
}

WmSettingsSnapshot wm_settings_scene_snapshot(const WmSettingsScene *scene) {
    if (!scene)
        return (WmSettingsSnapshot){.phase = WM_SETTINGS_CLOSED};
    WmSettingsSnapshot snapshot = {
        .page = scene->page,
        .category = scene->active_category,
        .detail = scene->detail,
        .selection = scene->selection,
        .year = scene->detail == 1 && scene->active_category == 2 ? scene->edit_year
                                                                  : scene->year,
        .month = scene->detail == 1 && scene->active_category == 2 ? scene->edit_month
                                                                   : scene->month,
        .day = scene->detail == 1 && scene->active_category == 2 ? scene->edit_day
                                                                 : scene->day,
        .hour = scene->detail == 2 && scene->active_category == 2 ? scene->edit_hour
                                                                  : scene->hour,
        .minute = scene->detail == 2 && scene->active_category == 2 ? scene->edit_minute
                                                                    : scene->minute,
        .sensitivity = scene->detail == 2 && scene->active_category == 6
                           ? scene->edit_sensitivity
                           : scene->sensitivity,
        .parental_enabled = scene->parental_enabled,
        .connect24_enabled = scene->connect24_enabled,
        .standby_enabled = scene->standby_enabled,
        .slot_light = scene->slot_light,
        .internet_agreement = scene->internet_agreement,
        .connection_slot = scene->connection_slot,
        .country_page = scene->country_page,
        .country_choice = scene->active_category == SETTINGS_COUNTRY
                              ? scene->edit_country_choice
                              : scene->country_choice,
        .local_format_complete = scene->local_format_complete,
        .phase = scene->phase,
        .phase_frame = scene->phase_frame,
        .hover = scene->hover,
        .hover_opacity = wm_settings_focus_opacity(scene, scene->hover),
        .page_opacity = wm_settings_page_opacity(scene)};
    snprintf(snapshot.nickname, sizeof(snapshot.nickname), "%s",
             scene->active_category == SETTINGS_NICKNAME ? scene->edit_nickname
                                                         : scene->nickname);
    return snapshot;
}

bool wm_settings_scene_update_question(const WmSettingsScene *scene) {
    return scene && scene->phase == WM_SETTINGS_READY &&
           scene->active_category == SETTINGS_UPDATE && scene->detail == 0;
}

bool wm_settings_scene_back(WmSettingsScene *scene) {
    if (!scene)
        return false;
    if (wm_settings_scene_nickname_keyboard_visible(scene))
        return wm_settings_scene_keyboard_close(scene, false) != NULL;
    SettingsPageSnapshot before = page_snapshot(scene);
    bool backed = settings_scene_back_control(scene);
    if (backed && (before.active_category != scene->active_category ||
                   before.detail != scene->detail))
        start_page_crossfade(scene, &before);
    return backed;
}

bool wm_settings_scene_take_exit(WmSettingsScene *scene) {
    if (!scene)
        return false;
    bool exit_pending = scene->exit_pending;
    scene->exit_pending = false;
    return exit_pending;
}

unsigned wm_settings_scene_take_category(WmSettingsScene *scene) {
    if (!scene)
        return 0;
    unsigned category = scene->pending_category;
    scene->pending_category = 0;
    return category;
}

WmSettingsControl wm_settings_scene_hit(const WmSettingsScene *scene, int x, int y) {
    if (!scene)
        return WM_SETTINGS_CONTROL_NONE;
    if (wm_settings_scene_nickname_keyboard_visible(scene))
        return WM_SETTINGS_CONTROL_NONE;
    int source_x = (int)floorf(settings_source_x(scene, x + 0.5f));
    return scene && scene->phase == WM_SETTINGS_READY && !scene->exit_pending
               ? (scene->active_category
                      ? category_hit(scene, source_x, y)
                      : wm_settings_index_hit(scene->page, source_x, y))
               : WM_SETTINGS_CONTROL_NONE;
}

bool wm_settings_scene_hover(WmSettingsScene *scene, WmSettingsControl control) {
    if (!scene || scene->phase != WM_SETTINGS_READY || scene->exit_pending ||
        wm_settings_scene_nickname_keyboard_visible(scene) ||
        control > WM_SETTINGS_CONTROL_NICKNAME_FIELD || scene->hover == control)
        return false;
    scene->hover = control;
    return true;
}

static bool activate_control(WmSettingsScene *scene, WmSettingsControl control) {
    if (!scene || scene->phase != WM_SETTINGS_READY || scene->exit_pending ||
        control <= WM_SETTINGS_CONTROL_NONE ||
        control > WM_SETTINGS_CONTROL_NICKNAME_FIELD)
        return false;
    if (wm_settings_scene_nickname_keyboard_visible(scene))
        return false;
    if (control == WM_SETTINGS_CONTROL_NICKNAME_FIELD)
        return settings_scene_open_nickname_keyboard(scene);
    if (scene->active_category)
        return settings_scene_activate_category(scene, control);
    if (control == WM_SETTINGS_CONTROL_BACK)
        return settings_scene_back_control(scene);
    if (control == WM_SETTINGS_CONTROL_PREVIOUS ||
        control == WM_SETTINGS_CONTROL_NEXT) {
        int direction = control == WM_SETTINGS_CONTROL_NEXT ? 1 : -1;
        int next = (int)scene->page + direction;
        if (next < 1 || next > SETTINGS_INDEX_PAGES)
            return false;
        scene->previous_page = scene->page;
        scene->page = (unsigned)next;
        scene->direction = direction;
        scene->phase = WM_SETTINGS_SCROLL;
        scene->phase_frame = 0.0f;
        scene->hover = WM_SETTINGS_CONTROL_NONE;
        return true;
    }
    unsigned category = (scene->page - 1) * SETTINGS_ITEMS_PER_PAGE +
                        (unsigned)(control - WM_SETTINGS_CONTROL_ITEM_1) + 1;
    if (!implemented_category(category)) {
        scene->pending_category = category;
        return true;
    }
    scene->active_category = category;
    scene->detail = 0;
    scene->selection = category == 4   ? scene->sound_choice
                       : category == 9 ? scene->language_choice
                                       : 0;
    if (category == SETTINGS_NICKNAME) {
        memcpy(scene->edit_nickname, scene->nickname, sizeof(scene->edit_nickname));
        scene->nickname_caret = (unsigned)strlen(scene->edit_nickname);
    }
    if (category == SETTINGS_COUNTRY) {
        scene->edit_country_choice = scene->country_choice;
        /* Always open the first country list page, even if the saved choice
         * belongs to a later page. */
        scene->country_page = 0;
    }
    if (category == SETTINGS_FORMAT)
        scene->local_format_complete = false;
    scene->hover = WM_SETTINGS_CONTROL_NONE;
    return true;
}

bool wm_settings_scene_activate(WmSettingsScene *scene, WmSettingsControl control) {
    if (!scene)
        return false;
    SettingsPageSnapshot before = page_snapshot(scene);
    bool activated = activate_control(scene, control);
    /* Changing Language selects its localized settings page. */
    bool page_changed = before.page != scene->page ||
                        before.active_category != scene->active_category ||
                        before.detail != scene->detail ||
                        (before.active_category == SETTINGS_LANGUAGE &&
                         scene->active_category == SETTINGS_LANGUAGE &&
                         before.selection != scene->selection);
    if (activated && page_changed)
        clear_hover_presentation(scene);
    if (activated && page_changed && scene->phase == WM_SETTINGS_READY)
        start_page_crossfade(scene, &before);
    return activated;
}

bool wm_settings_scene_pointer_down(WmSettingsScene *scene, WmSettingsControl control) {
    if (!scene || scene->phase != WM_SETTINGS_READY || scene->active_category != 2)
        return false;
    bool date_arrow = scene->detail == 1 && control >= WM_SETTINGS_CONTROL_ITEM_1 &&
                      control <= WM_SETTINGS_CONTROL_ITEM_6;
    bool time_arrow = scene->detail == 2 && control >= WM_SETTINGS_CONTROL_ITEM_1 &&
                      control <= WM_SETTINGS_CONTROL_ITEM_4;
    if (!date_arrow && !time_arrow)
        return false;
    if (!wm_settings_scene_activate(scene, control))
        return false;
    scene->held_control = control;
    scene->repeat_cue_pending = false;
    scene->hold_elapsed = 0.0f;
    scene->next_repeat = 24.0f;
    return true;
}

void wm_settings_scene_pointer_up(WmSettingsScene *scene) {
    if (!scene)
        return;
    scene->held_control = WM_SETTINGS_CONTROL_NONE;
    scene->hold_elapsed = 0.0f;
    scene->next_repeat = 0.0f;
}

bool wm_settings_scene_directional_control(const WmSettingsScene *scene,
                                           WmSettingsControl control) {
    if (!scene || scene->phase != WM_SETTINGS_READY)
        return false;
    if (scene->active_category == 0) {
        return (control == WM_SETTINGS_CONTROL_PREVIOUS && scene->page > 1) ||
               (control == WM_SETTINGS_CONTROL_NEXT && scene->page < 3);
    }
    if (scene->active_category == 2) {
        WmSettingsControl last = scene->detail == 1 ? WM_SETTINGS_CONTROL_ITEM_6
                                                    : WM_SETTINGS_CONTROL_ITEM_4;
        return (scene->detail == 1 || scene->detail == 2) &&
               control >= WM_SETTINGS_CONTROL_ITEM_1 && control <= last;
    }
    if (scene->active_category == 3 && scene->detail == 1) {
        return control == WM_SETTINGS_CONTROL_ITEM_1 ||
               control == WM_SETTINGS_CONTROL_ITEM_2;
    }
    if (scene->active_category == 10) {
        return (control == WM_SETTINGS_CONTROL_PREVIOUS && scene->country_page > 0) ||
               (control == WM_SETTINGS_CONTROL_ITEM_6 && scene->country_page < 9);
    }
    return false;
}
