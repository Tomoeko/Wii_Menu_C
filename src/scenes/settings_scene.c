#include "settings_scene_internal.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

enum {
    SETTINGS_PATH_CAPACITY = 4096
};

const unsigned wm_settings_country_page_start[10] = {
    0, 4, 9, 14, 19, 24, 29, 34, 39, 44
};

static float settings_source_x(const WmSettingsScene *scene,
                               float frame_x) {
    return scene->wide
        ? frame_x * ((float)SETTINGS_WIDE_WIDTH / WM_FRAME_WIDTH) -
          SETTINGS_SIDE_WIDTH + 16.0f
        : frame_x;
}

static void clear_hover_presentation(WmSettingsScene *scene) {
    scene->hover = WM_SETTINGS_CONTROL_NONE;
}

static void start_page_crossfade(WmSettingsScene *scene,
                                 const WmSettingsScene *before) {
    if (!scene->prior_page) return;
    *scene->prior_page = *before;
    scene->prior_page->prior_page = NULL;
    scene->prior_page->page_crossfade = false;
    scene->prior_page->draw_opacity = 1.0f;
    scene->page_frame = 0.0f;
    scene->page_crossfade = true;
}

static bool within(int x, int y, int left, int top, int width, int height) {
    return x >= left && x < left + width &&
           y >= top && y < top + height;
}

static unsigned days_in_month(unsigned year, unsigned month) {
    static const unsigned lengths[12] = {
        31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31
    };
    if (month < 1 || month > 12) return 31;
    if (month != 2) return lengths[month - 1];
    unsigned full_year = 2000 + year;
    bool leap = full_year % 4 == 0 &&
                (full_year % 100 != 0 || full_year % 400 == 0);
    return leap ? 29 : 28;
}

static void clamp_edit_day(WmSettingsScene *scene) {
    unsigned maximum = days_in_month(scene->edit_year, scene->edit_month);
    if (scene->edit_day > maximum) scene->edit_day = maximum;
}

static void reset_local_values(WmSettingsScene *scene) {
    scene->sound_choice = 1; /* Stereo */
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
        scene->year = (unsigned)(year < 2000 ? 0 : year > 2035 ? 35
                                                       : year - 2000);
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
    if (page < 1 || page > SETTINGS_INDEX_PAGES ||
        x < 0 || y < 0 || x >= WM_FRAME_WIDTH || y >= WM_FRAME_HEIGHT)
        return WM_SETTINGS_CONTROL_NONE;

    /* The original documents are 608 pixels wide and centered by Setting's
     * framebuffer presentation in the 640-pixel logical viewport. */
    int local_x = x - 16;
    if (within(local_x, y, 28, 371, 272, 72))
        return WM_SETTINGS_CONTROL_BACK;
    if (page > 1 && within(local_x, y, 22, 180, 72, 72))
        return WM_SETTINGS_CONTROL_PREVIOUS;
    if (page < SETTINGS_INDEX_PAGES &&
        within(local_x, y, 514, 180, 72, 72))
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

static WmSettingsControl choice_at(int local_x, int y,
                                   int first_y, int count) {
    for (int index = 0; index < count; index++) {
        if (within(local_x, y, 107, first_y + index * 96, 394, 70))
            return (WmSettingsControl)(WM_SETTINGS_CONTROL_ITEM_1 + index);
    }
    return WM_SETTINGS_CONTROL_NONE;
}

static WmSettingsControl category_hit(const WmSettingsScene *scene,
                                      int x, int y) {
    if (x < 16 || y < 0 || x >= 624 || y >= WM_FRAME_HEIGHT)
        return WM_SETTINGS_CONTROL_NONE;
    int local_x = x - 16;
    bool sensitivity_meter = scene->active_category == 6 &&
                             scene->detail == 2 &&
                             !scene->sensitivity_instructions;
    /* The meter has no footer; its instruction line accepts a pointer click
     * in place of the Wii Remote A press. */
    if (sensitivity_meter && within(local_x, y, 24, 344, 560, 95))
        return WM_SETTINGS_CONTROL_NEXT;
    if (within(local_x, y, 28, 371, 272, 72) &&
        !sensitivity_meter &&
        !(scene->active_category == SETTINGS_INTERNET &&
          (scene->detail == INTERNET_WIRED_PROMPT ||
           scene->detail == INTERNET_ACCESS_POINT_SEARCH ||
           scene->detail == INTERNET_NO_ACCESS_POINT)) &&
        !(scene->active_category == SETTINGS_FORMAT && scene->detail == 3))
        return WM_SETTINGS_CONTROL_BACK;
    bool choices = scene->active_category == 4 ||
                   scene->active_category == 9 ||
                   (scene->detail != 0 &&
                    scene->active_category != SETTINGS_INTERNET) ||
                   (scene->active_category == SETTINGS_INTERNET &&
                    (scene->detail == 3 ||
                     scene->detail == INTERNET_WIRED_PROMPT ||
                     scene->detail == INTERNET_NO_ACCESS_POINT ||
                     scene->detail == INTERNET_USB_INSTRUCTIONS ||
                     scene->detail == INTERNET_USB_EXISTING_CONNECTOR)) ||
                   scene->active_category == SETTINGS_NICKNAME ||
                   scene->active_category == SETTINGS_PARENTAL ||
                   scene->active_category == SETTINGS_COUNTRY ||
                   scene->active_category == SETTINGS_UPDATE ||
                   scene->active_category == SETTINGS_FORMAT;
    if (choices && !sensitivity_meter &&
        within(local_x, y, 308, 371, 272, 72))
        return WM_SETTINGS_CONTROL_NEXT; /* Confirm or OK. */

    if (!scene->detail) {
        if (scene->active_category == 3) {
            for (int item = 0; item < 4; item++) {
                if (within(local_x, y, 104, 78 + item * 72, 400, 60))
                    return (WmSettingsControl)(WM_SETTINGS_CONTROL_ITEM_1 +
                                               item);
            }
        } else if (scene->active_category == 4 ||
                   scene->active_category == 9) {
            return choice_at(local_x, y, 85, 3);
        } else if (scene->active_category == 2 ||
                   scene->active_category == 6) {
            return choice_at(local_x, y, 133, 2);
        } else if (scene->active_category == SETTINGS_INTERNET ||
                   scene->active_category == SETTINGS_CONNECT24) {
            if (scene->active_category == SETTINGS_CONNECT24 &&
                !scene->connect24_enabled)
                return choice_at(local_x, y, 85, 1);
            return choice_at(local_x, y, 85, 3);
        } else if (scene->active_category == SETTINGS_COUNTRY) {
            if (scene->country_page > 0 &&
                within(local_x, y, 528, 76, 72, 72))
                return WM_SETTINGS_CONTROL_PREVIOUS;
            if (scene->country_page < 9 &&
                within(local_x, y, 528, 282, 72, 72))
                return WM_SETTINGS_CONTROL_ITEM_6;
            unsigned count = scene->country_page == 0 ||
                             scene->country_page == 9 ? 4 : 5;
            int first_y = scene->country_page == 0 ? 132 : 76;
            for (unsigned item = 0; item < count; item++) {
                if (within(local_x, y, 88, first_y + (int)item * 56,
                           432, 56))
                    return (WmSettingsControl)(
                        WM_SETTINGS_CONTROL_ITEM_1 + item);
            }
        }
        return WM_SETTINGS_CONTROL_NONE;
    }
    if (scene->active_category == SETTINGS_INTERNET) {
        if (scene->detail == 1)
            return choice_at(local_x, y, 85, 3);
        if (scene->detail == INTERNET_CONNECTION_SELECT)
            return choice_at(local_x, y, 133, 2);
        if (scene->detail == INTERNET_WIRELESS_CHOICES) {
            WmSettingsControl full_row = choice_at(local_x, y, 85, 2);
            if (full_row != WM_SETTINGS_CONTROL_NONE) return full_row;
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
    if (scene->active_category == 2 && scene->detail == 1) {
        /* The USA English stylesheet moves Year to the right of Month/Day.
         * The common stylesheet's positions are different. */
        const int arrow_x[6] = {400, 400, 88, 88, 224, 224};
        const int arrow_y[6] = {108, 253, 108, 253, 108, 253};
        for (unsigned index = 0; index < 6; index++) {
            if (within(local_x, y, arrow_x[index], arrow_y[index], 72, 72))
                return (WmSettingsControl)(WM_SETTINGS_CONTROL_ITEM_1 +
                                           index);
        }
    } else if (scene->active_category == 2 && scene->detail == 2) {
        const int arrow_x[4] = {200, 200, 336, 336};
        const int arrow_y[4] = {108, 253, 108, 253};
        for (unsigned index = 0; index < 4; index++) {
            if (within(local_x, y, arrow_x[index], arrow_y[index], 64, 64))
                return (WmSettingsControl)(WM_SETTINGS_CONTROL_ITEM_1 +
                                           index);
        }
    } else if (scene->active_category == 6 && scene->detail == 2) {
        if (scene->sensitivity_instructions)
            return WM_SETTINGS_CONTROL_NONE;
        if (within(local_x, y, 24, 296, 132, 48))
            return WM_SETTINGS_CONTROL_ITEM_1;
        if (within(local_x, y, 452, 296, 132, 48))
            return WM_SETTINGS_CONTROL_ITEM_2;
    } else if (scene->active_category == 3 && scene->detail == 1) {
        if (within(local_x, y, 160, 180, 64, 64))
            return WM_SETTINGS_CONTROL_ITEM_1;
        if (within(local_x, y, 376, 180, 64, 64))
            return WM_SETTINGS_CONTROL_ITEM_2;
    } else if (scene->active_category == 3 && scene->detail == 2) {
        if (within(local_x, y, 48, 146, 200, 140))
            return WM_SETTINGS_CONTROL_ITEM_1;
        if (within(local_x, y, 296, 146, 264, 140))
            return WM_SETTINGS_CONTROL_ITEM_2;
    } else {
        return choice_at(local_x, y, 133, 2);
    }
    return WM_SETTINGS_CONTROL_NONE;
}

WmSettingsScene *wm_settings_scene_create(WmPlatform *platform,
                                           const char *assets_directory,
                                           WmTextureCache *textures,
                                           WmFontCache *fonts) {
    if (!platform || !assets_directory || !assets_directory[0] ||
        !textures || !fonts) return NULL;
    WmSettingsScene *scene = calloc(1, sizeof(*scene));
    if (!scene) return NULL;
    scene->platform = platform;
    scene->textures = textures;
    scene->fonts = fonts;
    scene->phase = WM_SETTINGS_CLOSED;
    reset_local_values(scene);
    char path[SETTINGS_PATH_CAPACITY];
    int length = snprintf(path, sizeof(path),
                          "%s/layouts/setting/SceenChange_b.json",
                          assets_directory);
    if (length < 0 || length >= (int)sizeof(path)) {
        free(scene);
        return NULL;
    }
    char error[160] = {0};
    scene->scroll_layout = wm_layout_load_json(path, error, sizeof(error));
    if (!scene->scroll_layout) {
        fprintf(stderr, "Could not load Wii Settings page transition: %s\n",
                error);
        free(scene);
        return NULL;
    }
    WmLayoutAnimationInfo left, right;
    if (!wm_layout_animation_info(scene->scroll_layout,
                                  "SceenChange_b_Left", &left) ||
        !wm_layout_animation_info(scene->scroll_layout,
                                  "SceenChange_b_Right", &right) ||
        left.frames != 41.0f || right.frames != 41.0f) {
        wm_settings_scene_destroy(scene);
        return NULL;
    }
    scene->prior_page = calloc(1, sizeof(*scene));
    if (!scene->prior_page) {
        wm_settings_scene_destroy(scene);
        return NULL;
    }
    scene->draw_opacity = 1.0f;
    length = snprintf(path, sizeof(path), "%s/fonts/settings-latin.ttc",
                      assets_directory);
    if (length > 0 && length < (int)sizeof(path))
        scene->outline_font = wm_outline_font_load(path, 1);
    return scene;
}

void wm_settings_scene_destroy(WmSettingsScene *scene) {
    if (!scene) return;
    free(scene->prior_page);
    wm_outline_font_destroy(scene->outline_font, scene->platform);
    wm_layout_destroy(scene->scroll_layout);
    free(scene);
}

bool wm_settings_scene_open(WmSettingsScene *scene) {
    if (!scene) return false;
    scene->page = 1;
    scene->previous_page = 1;
    scene->phase = WM_SETTINGS_APPEAR;
    scene->phase_frame = -1.0f;
    scene->hover = WM_SETTINGS_CONTROL_NONE;
    clear_hover_presentation(scene);
    scene->pending_category = 0;
    scene->active_category = 0;
    scene->detail = 0;
    scene->connection_search_frames = 0.0f;
    scene->selection = 0;
    scene->sensitivity_instructions = false;
    scene->held_control = WM_SETTINGS_CONTROL_NONE;
    scene->exit_pending = false;
    scene->direction = 0;
    scene->page_crossfade = false;
    scene->page_frame = 20.0f;
    scene->draw_opacity = 1.0f;
    scene->direct_entry = false;
    return true;
}

static bool open_direct_category(WmSettingsScene *scene, unsigned category) {
    if (!wm_settings_scene_open(scene)) return false;
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
    if (!scene) return;
    scene->page = 1;
    scene->previous_page = 1;
    scene->phase = WM_SETTINGS_CLOSED;
    scene->phase_frame = 0.0f;
    scene->hover = WM_SETTINGS_CONTROL_NONE;
    clear_hover_presentation(scene);
    scene->pending_category = 0;
    scene->active_category = 0;
    scene->detail = 0;
    scene->connection_search_frames = 0.0f;
    scene->selection = 0;
    scene->sensitivity_instructions = false;
    scene->held_control = WM_SETTINGS_CONTROL_NONE;
    scene->direction = 0;
    scene->page_crossfade = false;
    scene->page_frame = 20.0f;
    scene->draw_opacity = 1.0f;
    scene->exit_pending = false;
    scene->direct_entry = false;
}

void wm_settings_scene_advance(WmSettingsScene *scene, float frames) {
    if (!scene || !isfinite(frames) || frames < 0.0f) return;
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
        for (unsigned repeats = 0; repeats < 128 &&
             scene->hold_elapsed >= scene->next_repeat; repeats++) {
            if (!wm_settings_scene_activate(scene, scene->held_control)) {
                scene->held_control = WM_SETTINGS_CONTROL_NONE;
                break;
            }
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
                WmSettingsScene before = *scene;
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
                WmSettingsScene before = *scene;
                scene->detail = INTERNET_USB_EXISTING_CONNECTOR;
                scene->hover = WM_SETTINGS_CONTROL_NONE;
                clear_hover_presentation(scene);
                start_page_crossfade(scene, &before);
            }
        }
    }
}

WmSettingsSnapshot wm_settings_scene_snapshot(const WmSettingsScene *scene) {
    if (!scene) return (WmSettingsSnapshot){.phase = WM_SETTINGS_CLOSED};
    WmSettingsSnapshot snapshot = {
        .page = scene->page,
        .category = scene->active_category,
        .detail = scene->detail,
        .selection = scene->selection,
        .year = scene->detail == 1 && scene->active_category == 2
            ? scene->edit_year : scene->year,
        .month = scene->detail == 1 && scene->active_category == 2
            ? scene->edit_month : scene->month,
        .day = scene->detail == 1 && scene->active_category == 2
            ? scene->edit_day : scene->day,
        .hour = scene->detail == 2 && scene->active_category == 2
            ? scene->edit_hour : scene->hour,
        .minute = scene->detail == 2 && scene->active_category == 2
            ? scene->edit_minute : scene->minute,
        .sensitivity = scene->detail == 2 && scene->active_category == 6
            ? scene->edit_sensitivity : scene->sensitivity,
        .parental_enabled = scene->parental_enabled,
        .connect24_enabled = scene->connect24_enabled,
        .standby_enabled = scene->standby_enabled,
        .slot_light = scene->slot_light,
        .internet_agreement = scene->internet_agreement,
        .connection_slot = scene->connection_slot,
        .country_page = scene->country_page,
        .country_choice = scene->active_category == SETTINGS_COUNTRY
            ? scene->edit_country_choice : scene->country_choice,
        .local_format_complete = scene->local_format_complete,
        .phase = scene->phase,
        .phase_frame = scene->phase_frame,
        .hover = scene->hover,
        .hover_opacity = wm_settings_focus_opacity(scene, scene->hover),
        .page_opacity = wm_settings_page_opacity(scene)
    };
    snprintf(snapshot.nickname, sizeof(snapshot.nickname), "%s",
             scene->active_category == SETTINGS_NICKNAME
                 ? scene->edit_nickname : scene->nickname);
    return snapshot;
}

bool wm_settings_scene_update_question(const WmSettingsScene *scene) {
    return scene && scene->phase == WM_SETTINGS_READY &&
           scene->active_category == SETTINGS_UPDATE && scene->detail == 0;
}

static bool back_control(WmSettingsScene *scene) {
    if (!scene || scene->phase != WM_SETTINGS_READY ||
        scene->exit_pending) return false;
    scene->held_control = WM_SETTINGS_CONTROL_NONE;
    if (scene->detail) {
        if (scene->active_category == SETTINGS_PARENTAL &&
            scene->detail == 2)
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
        clear_hover_presentation(scene);
        return true;
    }
    if (scene->active_category) {
        if (scene->direct_entry) {
            scene->exit_pending = true;
            return true;
        }
        scene->active_category = 0;
        scene->hover = WM_SETTINGS_CONTROL_NONE;
        clear_hover_presentation(scene);
        return true;
    }
    scene->exit_pending = true;
    scene->hover = WM_SETTINGS_CONTROL_NONE;
    clear_hover_presentation(scene);
    return true;
}

bool wm_settings_scene_back(WmSettingsScene *scene) {
    if (!scene) return false;
    WmSettingsScene before = *scene;
    bool backed = back_control(scene);
    if (backed && (before.active_category != scene->active_category ||
                   before.detail != scene->detail))
        start_page_crossfade(scene, &before);
    return backed;
}

bool wm_settings_scene_take_exit(WmSettingsScene *scene) {
    if (!scene) return false;
    bool exit_pending = scene->exit_pending;
    scene->exit_pending = false;
    return exit_pending;
}

unsigned wm_settings_scene_take_category(WmSettingsScene *scene) {
    if (!scene) return 0;
    unsigned category = scene->pending_category;
    scene->pending_category = 0;
    return category;
}

WmSettingsControl wm_settings_scene_hit(const WmSettingsScene *scene,
                                        int x, int y) {
    if (!scene) return WM_SETTINGS_CONTROL_NONE;
    int source_x = (int)floorf(settings_source_x(scene, x + 0.5f));
    return scene && scene->phase == WM_SETTINGS_READY &&
           !scene->exit_pending
        ? (scene->active_category
            ? category_hit(scene, source_x, y)
            : wm_settings_index_hit(scene->page, source_x, y))
        : WM_SETTINGS_CONTROL_NONE;
}

bool wm_settings_scene_hover(WmSettingsScene *scene,
                             WmSettingsControl control) {
    if (!scene || scene->phase != WM_SETTINGS_READY ||
        scene->exit_pending || control > WM_SETTINGS_CONTROL_ITEM_6 ||
        scene->hover == control) return false;
    scene->hover = control;
    return true;
}

static bool activate_extended_category(WmSettingsScene *scene,
                                       WmSettingsControl control) {
    unsigned category = scene->active_category;
    unsigned item = control >= WM_SETTINGS_CONTROL_ITEM_1 &&
                    control <= WM_SETTINGS_CONTROL_ITEM_6
        ? (unsigned)(control - WM_SETTINGS_CONTROL_ITEM_1) : 6;
    if (category == SETTINGS_NICKNAME) {
        if (control == WM_SETTINGS_CONTROL_BACK)
            return back_control(scene);
        if (control != WM_SETTINGS_CONTROL_NEXT) return false;
        memcpy(scene->nickname, scene->edit_nickname,
               sizeof(scene->nickname));
        scene->active_category = 0;
    } else if (category == SETTINGS_PARENTAL) {
        if (control == WM_SETTINGS_CONTROL_BACK) {
            if (!scene->detail) scene->detail = 1; /* Yes. */
            else return back_control(scene);
        } else if (control == WM_SETTINGS_CONTROL_NEXT) {
            if (!scene->detail) scene->active_category = 0; /* No. */
            else if (scene->detail == 1) scene->detail = 2;
            else {
                /* A local guard only. Rating, PIN, and title restriction
                 * services are separate unported console functions. */
                scene->parental_enabled = true;
                scene->active_category = 0;
                scene->detail = 0;
            }
        } else return false;
    } else if (category == SETTINGS_INTERNET) {
        if (scene->detail == 3 &&
            (control == WM_SETTINGS_CONTROL_BACK ||
             control == WM_SETTINGS_CONTROL_NEXT)) {
            /* The left action accepts; the right action declines. */
            scene->internet_agreement =
                control == WM_SETTINGS_CONTROL_BACK;
            return back_control(scene);
        }
        if (scene->detail == INTERNET_USB_EXISTING_CONNECTOR &&
            control == WM_SETTINGS_CONTROL_BACK) {
            /* The visible left Yes retries registration. The footer pane
             * names are swapped, so use their displayed positions. */
            scene->detail = INTERNET_USB_REGISTRATION;
            scene->connection_search_frames = 0.0f;
            scene->hover = WM_SETTINGS_CONTROL_NONE;
            clear_hover_presentation(scene);
            return true;
        }
        if (control == WM_SETTINGS_CONTROL_BACK)
            return back_control(scene);
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
            scene->detail = item == 0 ? INTERNET_WIRELESS_CHOICES
                                       : INTERNET_WIRED_PROMPT;
        } else if (scene->detail == INTERNET_WIRELESS_CHOICES && item == 0) {
            scene->detail = INTERNET_ACCESS_POINT_SEARCH;
            scene->connection_search_frames = 0.0f;
        } else if (scene->detail == INTERNET_WIRELESS_CHOICES && item == 1) {
            scene->detail = INTERNET_USB_INSTRUCTIONS;
        } else return false;
    } else if (category == SETTINGS_CONNECT24) {
        if (control == WM_SETTINGS_CONTROL_BACK)
            return back_control(scene);
        if (!scene->detail && item < 3 &&
            (scene->connect24_enabled || item == 0)) {
            scene->detail = item + 1;
            scene->selection = scene->detail == 1
                ? (scene->connect24_enabled ? 0 : 1)
                : scene->detail == 2 ? (scene->standby_enabled ? 0 : 1)
                                     : scene->slot_light;
        } else if (scene->detail && item <
                   (scene->detail == 3 ? 3u : 2u)) {
            scene->selection = item;
            if (scene->detail == 1) {
                /* Row selection commits the value; Back and Confirm keep it. */
                scene->connect24_enabled = item == 0;
            }
        } else if (scene->detail && control == WM_SETTINGS_CONTROL_NEXT) {
            switch (scene->detail) {
                case 1: scene->connect24_enabled = scene->selection == 0; break;
                case 2: scene->standby_enabled = scene->selection == 0; break;
                case 3: scene->slot_light = scene->selection; break;
            }
            scene->detail = 0;
        } else return false;
    } else if (category == SETTINGS_COUNTRY) {
        if (control == WM_SETTINGS_CONTROL_BACK)
            return back_control(scene);
        if (control == WM_SETTINGS_CONTROL_PREVIOUS &&
            scene->country_page > 0) {
            scene->country_page--;
        } else if (control == WM_SETTINGS_CONTROL_ITEM_6 &&
                   scene->country_page < 9) {
            scene->country_page++;
        } else if (item < (scene->country_page == 0 ||
                           scene->country_page == 9 ? 4u : 5u)) {
            scene->edit_country_choice =
                wm_settings_country_page_start[scene->country_page] + item;
        } else if (control == WM_SETTINGS_CONTROL_NEXT) {
            scene->country_choice = scene->edit_country_choice;
            scene->active_category = 0;
        } else return false;
    } else if (category == SETTINGS_UPDATE) {
        if (!scene->detail) {
            /* The left action opens the offline explanation; the right exits. */
            if (control == WM_SETTINGS_CONTROL_BACK)
                scene->detail = 1;
            else if (control == WM_SETTINGS_CONTROL_NEXT)
                return back_control(scene);
            else return false;
        } else if (control == WM_SETTINGS_CONTROL_BACK) {
            return back_control(scene);
        } else if (control == WM_SETTINGS_CONTROL_NEXT) {
            scene->detail = 0;
            scene->active_category = 0;
        } else return false;
    } else if (category == SETTINGS_FORMAT) {
        if (scene->detail == 2 && control == WM_SETTINGS_CONTROL_BACK) {
            /* The final left action resets local volatile settings only;
             * it never modifies NAND or channel files. */
            reset_local_values(scene);
            scene->local_format_complete = true;
            scene->detail = 3;
        } else if (control == WM_SETTINGS_CONTROL_BACK) {
            return back_control(scene);
        } else if (control == WM_SETTINGS_CONTROL_NEXT) {
            if (scene->detail < 2) scene->detail++;
            else {
                scene->detail = 0;
                scene->active_category = 0;
            }
        } else return false;
    } else {
        return false;
    }
    scene->hover = WM_SETTINGS_CONTROL_NONE;
    return true;
}

static bool activate_control(WmSettingsScene *scene,
                             WmSettingsControl control) {
    if (!scene || scene->phase != WM_SETTINGS_READY ||
        scene->exit_pending || control <= WM_SETTINGS_CONTROL_NONE ||
        control > WM_SETTINGS_CONTROL_ITEM_6) return false;
    if (scene->active_category == SETTINGS_NICKNAME ||
        scene->active_category == SETTINGS_PARENTAL ||
        scene->active_category == SETTINGS_INTERNET ||
        scene->active_category == SETTINGS_CONNECT24 ||
        scene->active_category == SETTINGS_COUNTRY ||
        scene->active_category == SETTINGS_UPDATE ||
        scene->active_category == SETTINGS_FORMAT)
        return activate_extended_category(scene, control);
    if (control == WM_SETTINGS_CONTROL_BACK)
        return back_control(scene);
    if (scene->active_category) {
        if (control == WM_SETTINGS_CONTROL_NEXT) {
            if (scene->active_category == 2 && scene->detail == 1) {
                scene->year = scene->edit_year;
                scene->month = scene->edit_month;
                scene->day = scene->edit_day;
                scene->detail = 0;
            } else if (scene->active_category == 2 && scene->detail == 2) {
                scene->hour = scene->edit_hour;
                scene->minute = scene->edit_minute;
                scene->detail = 0;
            } else if (scene->active_category == 4) {
                scene->sound_choice = scene->selection;
                scene->active_category = 0;
            } else if (scene->active_category == 9) {
                scene->language_choice = scene->selection;
                scene->active_category = 0;
            } else if (scene->active_category == 3 && scene->detail) {
                switch (scene->detail) {
                    case 1: scene->screen_position = scene->selection; break;
                    case 2: scene->widescreen_choice = scene->selection; break;
                    case 3: scene->resolution_choice = scene->selection; break;
                    case 4: scene->burn_in_choice = scene->selection; break;
                }
                scene->detail = 0;
            } else if (scene->active_category == 6 && scene->detail == 1) {
                scene->sensor_position = scene->selection == 0 ? 1 : 0;
                scene->detail = 0;
            } else if (scene->active_category == 6 && scene->detail == 2) {
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
        if (control < WM_SETTINGS_CONTROL_ITEM_1 ||
            control > WM_SETTINGS_CONTROL_ITEM_6) return false;
        unsigned item = (unsigned)(control - WM_SETTINGS_CONTROL_ITEM_1);
        if (scene->active_category == 2 && !scene->detail && item < 2) {
            scene->detail = item + 1;
            scene->edit_year = scene->year;
            scene->edit_month = scene->month;
            scene->edit_day = scene->day;
            scene->edit_hour = scene->hour;
            scene->edit_minute = scene->minute;
            scene->hover = WM_SETTINGS_CONTROL_NONE;
            return true;
        }
        if (scene->active_category == 2 && scene->detail == 1) {
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
                    scene->edit_month = scene->edit_month == 12
                        ? 1 : scene->edit_month + 1;
                    clamp_edit_day(scene);
                    break;
                case 3:
                    scene->edit_month = scene->edit_month == 1
                        ? 12 : scene->edit_month - 1;
                    clamp_edit_day(scene);
                    break;
                case 4:
                    scene->edit_day = scene->edit_day ==
                        days_in_month(scene->edit_year, scene->edit_month)
                        ? 1 : scene->edit_day + 1;
                    break;
                case 5:
                    scene->edit_day = scene->edit_day == 1
                        ? days_in_month(scene->edit_year, scene->edit_month)
                        : scene->edit_day - 1;
                    break;
            }
            return true;
        }
        if (scene->active_category == 2 && scene->detail == 2) {
            switch (item) {
                case 0: scene->edit_hour = (scene->edit_hour + 1) % 24; break;
                case 1: scene->edit_hour = (scene->edit_hour + 23) % 24; break;
                case 2:
                    scene->edit_minute = (scene->edit_minute + 1) % 60;
                    break;
                case 3:
                    scene->edit_minute = (scene->edit_minute + 59) % 60;
                    break;
                default: return false;
            }
            return true;
        }
        if (scene->active_category == 4 || scene->active_category == 9) {
            if (item >= 3) return false;
            scene->selection = item;
            return true;
        }
        if (!scene->detail && scene->active_category == 3) {
            scene->detail = item + 1;
            switch (scene->detail) {
                case 1: scene->selection = scene->screen_position; break;
                case 2: scene->selection = scene->widescreen_choice; break;
                case 3: scene->selection = scene->resolution_choice; break;
                case 4: scene->selection = scene->burn_in_choice; break;
            }
            scene->hover = WM_SETTINGS_CONTROL_NONE;
            return true;
        }
        if (!scene->detail && scene->active_category == 6 && item < 2) {
            scene->detail = item + 1;
            scene->selection = scene->sensor_position == 1 ? 0 : 1;
            scene->edit_sensitivity = scene->sensitivity;
            scene->sensitivity_instructions = item == 1;
            scene->hover = WM_SETTINGS_CONTROL_NONE;
            return true;
        }
        if (scene->active_category == 6 && scene->detail == 1) {
            if (item >= 2) return false;
            /* Above TV maps to 1, Below TV to 0; selection commits the value. */
            scene->selection = item;
            scene->sensor_position = item == 0 ? 1 : 0;
            return true;
        }
        if (scene->active_category == 6 && scene->detail == 2) {
            if (scene->sensitivity_instructions || item >= 2) return false;
            if (item == 0 && scene->edit_sensitivity > 1)
                scene->edit_sensitivity--;
            else if (item == 1 && scene->edit_sensitivity < 5)
                scene->edit_sensitivity++;
            return true;
        }
        if (scene->active_category == 3 && scene->detail == 1) {
            if (item >= 2) return false;
            /* Left moves the position by +2; right moves it by -2. */
            int position = (int)scene->selection + (item ? -2 : 2);
            if (position < 0) position = 0;
            if (position > 32) position = 32;
            scene->selection = (unsigned)position;
            /* Each arrow press commits the position; Back retains it. */
            scene->screen_position = scene->selection;
            return true;
        }
        if (scene->active_category == 3 && scene->detail == 2) {
            if (item >= 2) return false;
            /* Selection commits the local widescreen choice; it does not
             * change the host window's output projection. */
            scene->selection = item;
            scene->widescreen_choice = item;
            return true;
        }
        if (scene->active_category == 3 &&
            (scene->detail == 3 || scene->detail == 4)) {
            if (item >= 2) return false;
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
    if (control == WM_SETTINGS_CONTROL_PREVIOUS ||
        control == WM_SETTINGS_CONTROL_NEXT) {
        int direction = control == WM_SETTINGS_CONTROL_NEXT ? 1 : -1;
        int next = (int)scene->page + direction;
        if (next < 1 || next > SETTINGS_INDEX_PAGES) return false;
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
    scene->selection = category == 4 ? scene->sound_choice
                     : category == 9 ? scene->language_choice : 0;
    if (category == SETTINGS_NICKNAME)
        memcpy(scene->edit_nickname, scene->nickname,
               sizeof(scene->edit_nickname));
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

bool wm_settings_scene_activate(WmSettingsScene *scene,
                                WmSettingsControl control) {
    if (!scene) return false;
    WmSettingsScene before = *scene;
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

bool wm_settings_scene_pointer_down(WmSettingsScene *scene,
                                    WmSettingsControl control) {
    if (!scene || scene->phase != WM_SETTINGS_READY ||
        scene->active_category != 2) return false;
    bool date_arrow = scene->detail == 1 &&
        control >= WM_SETTINGS_CONTROL_ITEM_1 &&
        control <= WM_SETTINGS_CONTROL_ITEM_6;
    bool time_arrow = scene->detail == 2 &&
        control >= WM_SETTINGS_CONTROL_ITEM_1 &&
        control <= WM_SETTINGS_CONTROL_ITEM_4;
    if (!date_arrow && !time_arrow) return false;
    if (!wm_settings_scene_activate(scene, control)) return false;
    scene->held_control = control;
    scene->hold_elapsed = 0.0f;
    scene->next_repeat = 24.0f;
    return true;
}

void wm_settings_scene_pointer_up(WmSettingsScene *scene) {
    if (!scene) return;
    scene->held_control = WM_SETTINGS_CONTROL_NONE;
    scene->hold_elapsed = 0.0f;
    scene->next_repeat = 0.0f;
}

bool wm_settings_scene_directional_control(const WmSettingsScene *scene,
                                            WmSettingsControl control) {
    if (!scene || scene->phase != WM_SETTINGS_READY) return false;
    if (scene->active_category == 0) {
        return (control == WM_SETTINGS_CONTROL_PREVIOUS && scene->page > 1) ||
               (control == WM_SETTINGS_CONTROL_NEXT && scene->page < 3);
    }
    if (scene->active_category == 2) {
        WmSettingsControl last = scene->detail == 1
            ? WM_SETTINGS_CONTROL_ITEM_6 : WM_SETTINGS_CONTROL_ITEM_4;
        return (scene->detail == 1 || scene->detail == 2) &&
               control >= WM_SETTINGS_CONTROL_ITEM_1 && control <= last;
    }
    if (scene->active_category == 3 && scene->detail == 1) {
        return control == WM_SETTINGS_CONTROL_ITEM_1 ||
               control == WM_SETTINGS_CONTROL_ITEM_2;
    }
    if (scene->active_category == 10) {
        return (control == WM_SETTINGS_CONTROL_PREVIOUS &&
                scene->country_page > 0) ||
               (control == WM_SETTINGS_CONTROL_ITEM_6 &&
                scene->country_page < 9);
    }
    return false;
}

bool wm_settings_scene_type_ascii(WmSettingsScene *scene, char character) {
    if (!wm_settings_scene_editing_nickname(scene) ||
        character < 32 || character > 126) return false;
    size_t length = strlen(scene->edit_nickname);
    if (length >= SETTINGS_NICKNAME_LIMIT) return false;
    scene->edit_nickname[length] = character;
    scene->edit_nickname[length + 1] = '\0';
    return true;
}

bool wm_settings_scene_backspace(WmSettingsScene *scene) {
    if (!wm_settings_scene_editing_nickname(scene)) return false;
    size_t length = strlen(scene->edit_nickname);
    if (!length) return false;
    scene->edit_nickname[length - 1] = '\0';
    return true;
}

bool wm_settings_scene_editing_nickname(const WmSettingsScene *scene) {
    return scene && scene->phase == WM_SETTINGS_READY &&
           scene->active_category == SETTINGS_NICKNAME;
}
