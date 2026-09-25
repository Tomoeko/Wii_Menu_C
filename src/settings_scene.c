#include "wii_menu/settings_scene.h"

#include "wii_menu/layout_runtime.h"
#include "wii_menu/outline_font.h"
#include "wii_menu/resource_font.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

enum {
    SETTINGS_PATH_CAPACITY = 4096,
    SETTINGS_INDEX_PAGES = 3,
    SETTINGS_ITEMS_PER_PAGE = 4,
    SETTINGS_NICKNAME_LIMIT = 10,
    SETTINGS_WIDE_WIDTH = 832,
    SETTINGS_SIDE_WIDTH = 112,
    /* Calibrated against the source HTML's rendered title, footer and badge
     * ink bounds using the WAD's proportional outline font. */
    SETTINGS_TITLE_TEXT_TOP = 28,
    SETTINGS_FOOTER_TEXT_TOP = 391,
    SETTINGS_PAGE_BADGE_TEXT_TOP = 376
};

enum SettingsCategory {
    SETTINGS_NICKNAME = 1,
    SETTINGS_CALENDAR = 2,
    SETTINGS_SCREEN = 3,
    SETTINGS_SOUND = 4,
    SETTINGS_PARENTAL = 5,
    SETTINGS_SENSOR = 6,
    SETTINGS_INTERNET = 7,
    SETTINGS_CONNECT24 = 8,
    SETTINGS_LANGUAGE = 9,
    SETTINGS_COUNTRY = 10,
    SETTINGS_UPDATE = 11,
    SETTINGS_FORMAT = 12
};

enum InternetDetail {
    INTERNET_CONNECTION_SELECT = 4,
    INTERNET_WIRELESS_CHOICES = 5,
    INTERNET_WIRED_PROMPT = 6,
    INTERNET_ACCESS_POINT_SEARCH = 7,
    INTERNET_NO_ACCESS_POINT = 8,
    INTERNET_USB_INSTRUCTIONS = 9,
    INTERNET_USB_REGISTRATION = 10,
    INTERNET_USB_EXISTING_CONNECTOR = 11
};

static const char *const index_labels[3][SETTINGS_INDEX_PAGES]
                                     [SETTINGS_ITEMS_PER_PAGE] = {
    {
        {"Console Nickname", "Calendar", "Screen", "Sound"},
        {"Parental Controls", "Sensor Bar", "Internet", "WiiConnect24"},
        {"Language", "Country", "Wii System Update",
         "Format Wii System Memory"}
    },
    {
        {"Surnom de la console", "Calendrier", "Ecran", "Son"},
        {"Contrôle parental", "Capteur", "Internet", "WiiConnect24"},
        {"Langue", "Pays", "Mise à jour de la Wii",
         "Formater la console Wii"}
    },
    {
        {"Apodo de la consola Wii", "Fecha y hora", "Pantalla", "Sonido"},
        {"Control parental", "Barra de sensores", "Internet",
         "WiiConnect24"},
        {"Idioma", "País", "Actualización de Wii",
         "Formatear la consola Wii"}
    }
};

static const char *const index_headings[3] = {
    "Wii System Settings", "Paramètres Wii", "Configuración de Wii"
};

static const char *const back_labels[3] = {"Back", "Retour", "Atrás"};

static const char *const confirm_labels[3] = {
    "Confirm", "Valider", "Confirmar"
};

/* Text from the USA Settings archive's ENG/FRA/SPA Display pages. The
 * category index is localized above; its child pages must use the same
 * selected document language. */
static const char *const screen_labels[3][4] = {
    {
        "Screen Position", "Widescreen Settings", "TV Resolution",
        "Screen Burn-in Reduction"
    },
    {
        "Position de l'écran", "Format", "Résolution du téléviseur",
        "Economiseur d'écran"
    },
    {
        "Posición de la pantalla", "Formato", "Resolución",
        "Protector de pantalla"
    }
};

static const char *const widescreen_labels[3][2] = {
    {"Standard (4:3)", "Widescreen (16:9)"},
    {"4:3", "16:9"},
    {"Pantalla normal (4:3)", "Pantalla panorámica (16:9)"}
};

static const char *const resolution_labels[3][2] = {
    {"EDTV or HDTV (480p)", "Standard TV (480i)"},
    {"EDTV ou HDTV (480p)", "Téléviseur standard (480i)"},
    {"EDTV o HDTV (480p)", "Normal (480i)"}
};

static const char *const burn_labels[3][2] = {
    {"On", "Off"},
    {"Oui", "Non"},
    {"Sí", "No"}
};

static const char *const button_images[2][3] = {
    {
        "textures/setting/SetBtn_a0.png",
        "textures/setting/SetBtn_a1.png",
        "textures/setting/SetBtn_a2.png"
    },
    {
        "textures/setting/SetBtn_Focus_a0.png",
        "textures/setting/SetBtn_Focus_a1.png",
        "textures/setting/SetBtn_Focus_a2.png"
    }
};

static const char *const index_arrow_images[2][2] = {
    {
        "textures/settings_html/arrow-left.png",
        "textures/settings_html/arrow-right.png"
    },
    {
        "textures/settings_html/arrow-left-focus.png",
        "textures/settings_html/arrow-right-focus.png"
    }
};

static const char *const country_names[48] = {
    "Anguilla", "Antigua and Barbuda", "Argentina", "Aruba",
    "Bahamas", "Barbados", "Belize", "Bolivia", "Brazil",
    "British Virgin Islands", "Canada", "Cayman Islands", "Chile",
    "Colombia", "Costa Rica", "Dominica", "Dominican Republic",
    "Ecuador", "El Salvador", "French Guiana", "Grenada",
    "Guadeloupe", "Guatemala", "Guyana", "Haiti", "Honduras",
    "Jamaica", "Martinique", "Mexico", "Montserrat",
    "Netherlands Antilles", "Nicaragua", "Panama", "Paraguay",
    "Peru", "Saudi Arabia", "Singapore", "St. Kitts and Nevis",
    "St. Lucia", "St. Vincent and the Grenadines", "Suriname",
    "Trinidad and Tobago", "Turks and Caicos Islands", "U.A.E",
    "United States", "Uruguay", "US Virgin Islands", "Venezuela"
};

static const unsigned country_page_start[10] = {
    0, 4, 9, 14, 19, 24, 29, 34, 39, 44
};

typedef struct TextContext {
    WmPlatform *platform;
    WmCachedFont *face;
    WmSettingsScene *scene;
} TextContext;

typedef struct SlideSample {
    float progress;
    float incoming_alpha;
    const char *incoming_pane;
    bool translation_found;
    bool alpha_found;
} SlideSample;

struct WmSettingsScene {
    WmPlatform *platform;
    WmTextureCache *textures;
    WmFontCache *fonts;
    WmCachedFont *font;
    WmOutlineFont *outline_font;
    WmLayout *scroll_layout;
    WmSettingsPhase phase;
    WmSettingsControl hover;
    unsigned page;
    unsigned previous_page;
    unsigned pending_category;
    unsigned active_category;
    unsigned detail;
    unsigned selection;
    unsigned sound_choice;
    unsigned language_choice;
    unsigned sensor_position;
    unsigned screen_position;
    unsigned widescreen_choice;
    unsigned resolution_choice;
    unsigned burn_in_choice;
    unsigned year;
    unsigned month;
    unsigned day;
    unsigned hour;
    unsigned minute;
    unsigned edit_year;
    unsigned edit_month;
    unsigned edit_day;
    unsigned edit_hour;
    unsigned edit_minute;
    unsigned sensitivity;
    unsigned edit_sensitivity;
    bool sensitivity_instructions;
    char nickname[SETTINGS_NICKNAME_LIMIT + 1];
    char edit_nickname[SETTINGS_NICKNAME_LIMIT + 1];
    bool parental_enabled;
    bool connect24_enabled;
    bool standby_enabled;
    unsigned slot_light;
    bool internet_agreement;
    unsigned connection_slot;
    float connection_search_frames;
    unsigned country_page;
    unsigned country_choice;
    unsigned edit_country_choice;
    bool local_format_complete;
    WmSettingsControl held_control;
    float hold_elapsed;
    float next_repeat;
    int direction;
    float phase_frame;
    float page_frame;
    float draw_opacity;
    bool page_crossfade;
    bool wide;
    WmSettingsScene *prior_page;
    bool exit_pending;
    bool direct_entry;
};

static float clampf(float value, float low, float high) {
    return fminf(high, fmaxf(low, value));
}

static float settings_x(const WmSettingsScene *scene, float source_x) {
    return scene->wide
        ? (source_x - 16.0f + SETTINGS_SIDE_WIDTH) *
          ((float)WM_FRAME_WIDTH / SETTINGS_WIDE_WIDTH)
        : source_x;
}

static float settings_width(const WmSettingsScene *scene,
                            float source_width) {
    return scene->wide
        ? source_width * ((float)WM_FRAME_WIDTH / SETTINGS_WIDE_WIDTH)
        : source_width;
}

static float settings_source_x(const WmSettingsScene *scene,
                               float frame_x) {
    return scene->wide
        ? frame_x * ((float)SETTINGS_WIDE_WIDTH / WM_FRAME_WIDTH) -
          SETTINGS_SIDE_WIDTH + 16.0f
        : frame_x;
}

void wm_settings_scene_set_wide(WmSettingsScene *scene, bool wide) {
    if (scene) scene->wide = wide;
}

WmSettingsProjection wm_settings_scene_projection(
    const WmSettingsScene *scene) {
    if (!scene) return (WmSettingsProjection){0};
    return (WmSettingsProjection){
        .document_x = settings_x(scene, 16.0f),
        .document_width = settings_width(scene, 608.0f),
        .side_width = scene->wide
            ? settings_width(scene, SETTINGS_SIDE_WIDTH) : 0.0f
    };
}

static void clear_hover_presentation(WmSettingsScene *scene) {
    scene->hover = WM_SETTINGS_CONTROL_NONE;
}

static float focus_opacity(const WmSettingsScene *scene,
                           WmSettingsControl control) {
    if (control <= WM_SETTINGS_CONTROL_NONE ||
        control > WM_SETTINGS_CONTROL_ITEM_6) return 0.0f;
    return scene->hover == control ? 1.0f : 0.0f;
}

static float page_opacity(const WmSettingsScene *scene) {
    return scene->page_crossfade
        ? floorf(clampf(scene->page_frame, 0.0f, 20.0f) * 255.0f /
                 20.0f) / 255.0f
        : 1.0f;
}

static float appearance_opacity(const WmSettingsScene *scene) {
    return scene->phase == WM_SETTINGS_APPEAR
        ? floorf(clampf(scene->phase_frame, 0.0f, 20.0f) * 255.0f /
                 20.0f) / 255.0f
        : 1.0f;
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
    /* The maintained HTML Settings dummy starts with DTV and progressive
     * output enabled, so Progressive_set selects its first (480p) row. */
    scene->resolution_choice = 0; /* EDTV or HDTV (480p) */
    scene->burn_in_choice = 0;
    scene->sensitivity = 3;
    scene->parental_enabled = false;
    scene->connect24_enabled = false;
    scene->standby_enabled = false;
    scene->slot_light = 0;
    scene->internet_agreement = false;
    /* The HTML bridge starts at country code 49. The first US list begins
     * at code 8, so code 49 is entry 41 (Trinidad and Tobago). */
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

const char *wm_settings_category_label(unsigned category) {
    if (category < 1 || category > 12) return NULL;
    unsigned index = category - 1;
    return index_labels[0][index / SETTINGS_ITEMS_PER_PAGE]
                       [index % SETTINGS_ITEMS_PER_PAGE];
}

static const char *localized_category_label(unsigned language,
                                            unsigned category) {
    if (category < 1 || category > 12) return NULL;
    if (language > 2) language = 0;
    unsigned index = category - 1;
    return index_labels[language][index / SETTINGS_ITEMS_PER_PAGE]
                                 [index % SETTINGS_ITEMS_PER_PAGE];
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
    /* The source meter page has no footer buttons. Clicking its instruction
     * line substitutes for the Wii Remote A press on a pointer-only host. */
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

bool wm_settings_scene_open_internet(WmSettingsScene *scene) {
    if (!wm_settings_scene_open(scene)) return false;
    scene->page = 2;
    scene->previous_page = 2;
    scene->active_category = SETTINGS_INTERNET;
    scene->direct_entry = true;
    return true;
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
            /* The maintained local bridge returns funcResult 2 for the
             * source's unavailable AP scan after its 1000 ms poll. */
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
            /* The maintained local bridge gives USB registration function
             * 30 a dummy result of 2 on the source page's one-second poll. */
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
        .hover_opacity = focus_opacity(scene, scene->hover),
        .page_opacity = page_opacity(scene)
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
            /* EULA_index.html places Yes on the left and No on the right. */
            scene->internet_agreement =
                control == WM_SETTINGS_CONTROL_BACK;
            return back_control(scene);
        }
        if (scene->detail == INTERNET_USB_EXISTING_CONNECTOR &&
            control == WM_SETTINGS_CONTROL_BACK) {
            /* Common0204's left Yes retries registration. Its stylesheet
             * swaps UnderL/UnderR names; the visible footer is decisive. */
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
            /* The source starts a network test here. This local menu has no
             * network service, so return to mode choices without success. */
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
            /* Common0204's No would open Common0203 and claim setup
             * complete. No connector exists in this local C runtime. */
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
                /* ONOFF_set.html writes nwc24 on row selection. Its Back and
                 * Confirm links both return to the index for that value. */
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
                country_page_start[scene->country_page] + item;
        } else if (control == WM_SETTINGS_CONTROL_NEXT) {
            scene->country_choice = scene->edit_country_choice;
            scene->active_category = 0;
        } else return false;
    } else if (category == SETTINGS_UPDATE) {
        if (!scene->detail) {
            /* Update_index.html puts Yes on the left and No on the right. */
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
            /* The source's last left button is Format. Only local volatile
             * settings are reset here; no NAND or channel files are touched. */
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
            /* The extracted page uses 1 for Above TV (first row), 0 for
             * Below TV (second row), and writes that value on selection. */
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
            /* HTML's left arrow adds 2 to dis_pos; right subtracts 2. */
            int position = (int)scene->selection + (item ? -2 : 2);
            if (position < 0) position = 0;
            if (position > 32) position = 32;
            scene->selection = (unsigned)position;
            /* Position_set.html writes dis_pos on every arrow press. Back
             * retains that local value in the maintained Settings bridge. */
            scene->screen_position = scene->selection;
            return true;
        }
        if (scene->active_category == 3 && scene->detail == 2) {
            if (item >= 2) return false;
            /* Wide_set.html writes dis_wide on row selection. This local
             * choice does not change the host window's output projection. */
            scene->selection = item;
            scene->widescreen_choice = item;
            return true;
        }
        if (scene->active_category == 3 &&
            (scene->detail == 3 || scene->detail == 4)) {
            if (item >= 2) return false;
            /* Progressive_set.html and Yakituki_set.html write their local
             * values on row selection, so Back retains the selected row. */
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
        /* US_Country_flame.html always loads US_Country_select01.html, even
         * when the saved choice belongs to a later list page. */
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
    /* Each Language choice navigates to its localized source document. */
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

static bool slide_pane(void *context, const WmLayoutPaneView *pane) {
    SlideSample *sample = context;
    if (strcmp(pane->name, "N_Tra0") == 0) {
        sample->progress = pane->matrix[3];
        sample->translation_found = true;
    } else if (strcmp(pane->name, sample->incoming_pane) == 0) {
        sample->incoming_alpha = pane->alpha;
        sample->alpha_found = true;
    }
    return true;
}

static SlideSample scroll_sample(WmSettingsScene *scene) {
    SlideSample sample = {
        .incoming_alpha = 1.0f,
        .incoming_pane = scene->direction > 0 ? "Tex1" : "Tex2"
    };
    if (scene->phase != WM_SETTINGS_SCROLL || !scene->direction)
        return sample;
    WmLayoutClip clip = {
        .animation = scene->direction > 0 ? "SceenChange_b_Right"
                                          : "SceenChange_b_Left",
        .frame = floorf(clampf(scene->phase_frame, 0.0f, 40.0f)),
        .loop_override = 0
    };
    if (!wm_layout_pose(scene->scroll_layout, &clip, 1)) return sample;
    wm_layout_visit_all_transforms(scene->scroll_layout, false,
                                    WM_LAYOUT_LOCAL, NULL,
                                    slide_pane, &sample);
    /* The authored pane travels 477 layout units by frame 25. Its movement
     * controls a full 608-pixel HTML-raster page shift in this C projection. */
    sample.progress = sample.translation_found
        ? clampf(fabsf(sample.progress) / 477.0f, 0.0f, 1.0f)
        : 0.0f;
    if (!sample.alpha_found) sample.incoming_alpha = 1.0f;
    return sample;
}

static void draw_rectangle(WmSettingsScene *scene, float x, float y,
                           float width, float height, WmColor color) {
    if (width <= 0.0f || height <= 0.0f || color.a <= 0.0f) return;
    color.a *= scene->draw_opacity;
    WmQuad quad = {
        .x = settings_x(scene, x), .y = y,
        .width = settings_width(scene, width), .height = height,
        .u1 = 1.0f, .v1 = 1.0f, .color = color
    };
    wm_platform_draw_quad(scene->platform, &quad);
}

static bool draw_image_raw(WmSettingsScene *scene, const char *url,
                           float x, float y, float width, float height,
                           WmColor tint) {
    uint32_t texture = 0;
    if (!wm_texture_cache_resolve(scene->textures, url, &texture) ||
        !texture) return false;
    tint.a *= scene->draw_opacity;
    WmQuad quad = {
        .x = x, .y = y, .width = width, .height = height,
        .u1 = 1.0f, .v1 = 1.0f, .color = tint, .texture = texture
    };
    wm_platform_draw_quad(scene->platform, &quad);
    return true;
}

static bool draw_image(WmSettingsScene *scene, const char *url,
                       float x, float y, float width, float height,
                       WmColor tint) {
    return draw_image_raw(scene, url, settings_x(scene, x), y,
                          settings_width(scene, width), height, tint);
}

static void draw_button(WmSettingsScene *scene, float x, float y,
                        float width, float height, bool focused,
                        float alpha) {
    const float edge = 32.0f;
    const WmColor normal = {1.0f, 1.0f, 1.0f, alpha};
    const WmColor focus = {0.62f, 0.62f, 0.88f, alpha * 0.88f};
    for (int slice = 0; slice < 3; slice++) {
        float left = slice == 0 ? x : slice == 1 ? x + edge
                                                  : x + width - edge;
        float slice_width = slice == 1 ? width - edge * 2.0f : edge;
        draw_image(scene, button_images[0][slice], left, y,
                   slice_width, height, normal);
        if (focused)
            draw_image(scene, button_images[1][slice], left, y,
                       slice_width, height, focus);
    }
}

static void draw_button_focus(WmSettingsScene *scene, float x, float y,
                              float width, float height, float alpha) {
    const float edge = 32.0f;
    const WmColor tint = {0.62f, 0.62f, 0.88f, alpha * 0.88f};
    for (int slice = 0; slice < 3; slice++) {
        float left = slice == 0 ? x : slice == 1 ? x + edge
                                                  : x + width - edge;
        float slice_width = slice == 1 ? width - edge * 2.0f : edge;
        draw_image(scene, button_images[1][slice], left, y,
                   slice_width, height, tint);
    }
}

static void draw_arrow(WmSettingsScene *scene, float x, float y,
                       bool right, float focus, float alpha) {
    const char *normal = index_arrow_images[0][right ? 1 : 0];
    const char *highlight = index_arrow_images[1][right ? 1 : 0];
    bool base_drawn = focus >= 1.0f ||
        draw_image(scene, normal, x, y, 72.0f, 72.0f,
                   (WmColor){1, 1, 1, alpha * (1.0f - focus)});
    if (base_drawn) {
        if (focus <= 0.0f ||
            draw_image(scene, highlight, x, y, 72.0f, 72.0f,
                       (WmColor){1, 1, 1, alpha * focus})) return;
        draw_image(scene, normal, x, y, 72.0f, 72.0f,
                   (WmColor){1, 1, 1, alpha * focus});
        return;
    }
    uint32_t texture = 0;
    if (!wm_texture_cache_resolve(scene->textures,
                                  "textures/setting/my_ArwSetUp_a.png",
                                  &texture) || !texture) return;
    if (focus > 0.0f)
        draw_rectangle(scene, x + 7.0f, y + 7.0f, 58.0f, 58.0f,
                       (WmColor){0.25f, 0.7f, 0.95f,
                                 alpha * focus * 0.35f});
    static const float right_uv[4][2] = {
        {0, 1}, {0, 0}, {1, 1}, {1, 0}
    };
    static const float left_uv[4][2] = {
        {1, 0}, {1, 1}, {0, 0}, {0, 1}
    };
    const float (*uv)[2] = right ? right_uv : left_uv;
    WmDrawVertex vertices[4];
    for (int index = 0; index < 4; index++) {
        vertices[index] = (WmDrawVertex){
            .x = settings_x(scene, x + (index % 2 ? 72.0f : 0.0f)),
            .y = y + (index >= 2 ? 72.0f : 0.0f),
            .u = uv[index][0], .v = uv[index][1],
            .color = {1.0f, 1.0f, 1.0f,
                      alpha * scene->draw_opacity}
        };
    }
    wm_platform_draw_vertices(scene->platform, vertices, texture);
}

static bool font_sheet(void *context, size_t sheet, uint32_t *texture) {
    TextContext *draw = context;
    return wm_font_cache_sheet(draw->face, sheet, texture);
}

static void font_quad(void *context, const WmFontQuad *quad) {
    TextContext *draw = context;
    if (!quad || !quad->texture) return;
    WmDrawVertex vertices[4];
    for (int index = 0; index < 4; index++) {
        const WmFontVertex *source = &quad->vertices[index];
        vertices[index] = (WmDrawVertex){
            .x = settings_x(draw->scene,
                            WM_FRAME_WIDTH * 0.5f + source->position[0]),
            .y = WM_FRAME_HEIGHT * 0.5f - source->position[1],
            .u = source->uv[0], .v = source->uv[1],
            .color = {
                source->color[0], source->color[1],
                source->color[2], source->color[3]
            }
        };
    }
    wm_platform_draw_vertices(draw->platform, vertices, quad->texture);
}

static void draw_text(WmSettingsScene *scene, const char *value,
                      float x, float top, float height,
                      WmColor color, float alpha, WmFontAlign align) {
    float opacity = alpha * scene->draw_opacity;
    if (scene->outline_font && height >= 8.0f && height <= 72.0f) {
        float scale = scene->wide
            ? (float)WM_FRAME_WIDTH / SETTINGS_WIDE_WIDTH : 1.0f;
        float offset = scene->wide
            ? (SETTINGS_SIDE_WIDTH - 16.0f) * scale : 0.0f;
        WmColor ink = color;
        ink.a *= opacity;
        if (wm_outline_font_draw_line(scene->outline_font, scene->platform,
                                      value, (unsigned)lroundf(height), x,
                                      top, align, scale, offset, ink,
                                      height == 24.0f || height == 26.0f))
            return;
    }
    if (!scene->font)
        scene->font = wm_font_cache_resolve(
            scene->fonts, "RevoIpl_RodinNTLGPro_DB_32_I4.brfnt");
    const WmFont *font = wm_cached_font_resource(scene->font);
    const WmFontMetrics *metrics = wm_font_metrics(font);
    if (!font || !metrics || !metrics->height) return;
    TextContext context = {
        .platform = scene->platform, .face = scene->font, .scene = scene
    };
    unsigned char red = (unsigned char)lroundf(clampf(color.r, 0, 1) * 255);
    unsigned char green = (unsigned char)lroundf(clampf(color.g, 0, 1) * 255);
    unsigned char blue = (unsigned char)lroundf(clampf(color.b, 0, 1) * 255);
    WmFontDrawOptions options = {
        .x = x - WM_FRAME_WIDTH * 0.5f,
        .y = WM_FRAME_HEIGHT * 0.5f - top,
        .size = {height * metrics->width / metrics->height, height},
        .alpha = opacity,
        .top_color = {red, green, blue, 255},
        .bottom_color = {red, green, blue, 255},
        .align = align,
        .sheet_provider = font_sheet,
        .on_quad = font_quad,
        .context = &context
    };
    wm_font_emit_line(font, value, &options);
}

static void draw_black_background(WmSettingsScene *scene) {
    WmQuad black = {
        .x = 0.0f, .y = 0.0f,
        .width = WM_FRAME_WIDTH, .height = WM_FRAME_HEIGHT,
        .u1 = 1.0f, .v1 = 1.0f,
        .color = {0, 0, 0, scene->draw_opacity}
    };
    wm_platform_draw_quad(scene->platform, &black);
}

static void draw_side_panels(WmSettingsScene *scene, float offset,
                             float alpha) {
    if (!scene->wide) return;
    WmSettingsProjection projection = wm_settings_scene_projection(scene);
    float translation = settings_width(scene, offset);
    draw_image_raw(scene, "textures/settings_html/side-panel.png",
                   translation, 0.0f, projection.side_width, 456.0f,
                   (WmColor){1, 1, 1, alpha});
    draw_image_raw(scene, "textures/settings_html/side-panel.png",
                   translation + projection.document_x +
                       projection.document_width,
                   0.0f, projection.side_width, 456.0f,
                   (WmColor){1, 1, 1, alpha});
}

static void draw_background_shell(WmSettingsScene *scene) {
    draw_black_background(scene);
    draw_side_panels(scene, 0.0f, appearance_opacity(scene));
}

static void draw_document_background(WmSettingsScene *scene, float offset,
                                     float alpha) {
    /* The source BG_common.gif repeats in an eight-pixel horizontal tile.
     * Preparation expands that tile to one 608×456 texture to save draw calls. */
    if (draw_image(scene, "textures/settings_html/background.png",
                   16.0f + offset, 0.0f, 608.0f, 456.0f,
                   (WmColor){1, 1, 1, alpha})) return;
    /* Older local preparations can still open Settings until re-exported. */
    static const struct {
        float y;
        float value;
    } stops[] = {
        {0.0f, 0.12f}, {48.0f, 0.035f}, {100.0f, 0.0f},
        {340.0f, 0.0f}, {410.0f, 0.05f}, {456.0f, 0.16f}
    };
    for (size_t index = 0; index + 1 < sizeof(stops) / sizeof(stops[0]);
         index++) {
        float start = stops[index].y;
        float end = stops[index + 1].y;
        const int bands = 8;
        for (int band = 0; band < bands; band++) {
            float fraction = (float)band / bands;
            float shade = stops[index].value +
                (stops[index + 1].value - stops[index].value) * fraction;
            draw_rectangle(scene, 16.0f + offset,
                           start + (end - start) * fraction,
                           608.0f, (end - start) / bands + 0.5f,
                           (WmColor){shade, shade, shade, alpha});
        }
    }
}

static void draw_background(WmSettingsScene *scene) {
    draw_background_shell(scene);
    draw_document_background(scene, 0.0f, appearance_opacity(scene));
}

static void draw_index_page(WmSettingsScene *scene, unsigned page,
                            float offset, float alpha, bool hover_enabled) {
    float origin = 16.0f + offset;
    const WmColor dark = {0.2f, 0.2f, 0.2f, 1.0f};
    if (!draw_image(scene, "textures/settings_html/title-tab.png",
                    origin + 24.0f, 27.0f, 408.0f, 36.0f,
                    (WmColor){1, 1, 1, alpha}))
        draw_rectangle(scene, origin + 24.0f, 27.0f, 408.0f, 36.0f,
                       (WmColor){1, 1, 1, alpha});
    char heading[40];
    snprintf(heading, sizeof(heading), "%s %u",
             index_headings[scene->language_choice], page);
    draw_text(scene, heading, origin + 32.0f,
              SETTINGS_TITLE_TEXT_TOP, 24.0f,
              dark, alpha, WM_FONT_ALIGN_LEFT);
    if (page == 1)
        draw_text(scene, "Ver. 4.3U", origin + 575.0f, 34.0f,
                  18.0f, (WmColor){1, 1, 1, 1}, alpha,
                  WM_FONT_ALIGN_RIGHT);

    for (int item = 0; item < SETTINGS_ITEMS_PER_PAGE; item++) {
        WmSettingsControl control =
            (WmSettingsControl)(WM_SETTINGS_CONTROL_ITEM_1 + item);
        float y = 78.0f + item * 72.0f;
        float focus = hover_enabled ? focus_opacity(scene, control) : 0.0f;
        if (draw_image(scene, "textures/settings_html/index-row.png",
                       origin + 104.0f, y - 2.0f, 400.0f, 64.0f,
                       (WmColor){1, 1, 1, alpha})) {
            if (focus > 0.0f &&
                !draw_image(scene,
                            "textures/settings_html/index-row-focus.png",
                            origin + 104.0f, y, 400.0f, 60.0f,
                            (WmColor){1, 1, 1, alpha * focus}))
                draw_button_focus(scene, origin + 104.0f, y,
                                  400.0f, 60.0f, alpha * focus);
        } else {
            draw_button(scene, origin + 104.0f, y, 400.0f, 60.0f,
                        focus > 0.0f, alpha);
        }
        draw_text(scene, index_labels[scene->language_choice][page - 1][item],
                  origin + 304.0f, y + 14.0f, 24.0f, dark, alpha,
                  WM_FONT_ALIGN_CENTER);
    }
    float back_focus = hover_enabled
        ? focus_opacity(scene, WM_SETTINGS_CONTROL_BACK) : 0.0f;
    if (draw_image(scene, "textures/settings_html/footer-button.png",
                   origin + 28.0f, 371.0f, 272.0f, 72.0f,
                   (WmColor){1, 1, 1, alpha})) {
        if (back_focus > 0.0f &&
            !draw_image(scene,
                        "textures/settings_html/footer-button-focus.png",
                        origin + 28.0f, 371.0f, 272.0f, 72.0f,
                        (WmColor){1, 1, 1, alpha * back_focus}))
            draw_button_focus(scene, origin + 28.0f, 371.0f,
                              272.0f, 72.0f, alpha * back_focus);
    } else {
        draw_button(scene, origin + 28.0f, 371.0f, 272.0f, 72.0f,
                    back_focus > 0.0f, alpha);
    }
    draw_text(scene, back_labels[scene->language_choice],
              origin + 164.0f, SETTINGS_FOOTER_TEXT_TOP,
              24.0f, dark, alpha, WM_FONT_ALIGN_CENTER);

    if (page > 1)
        draw_arrow(scene, origin + 22.0f, 180.0f, false,
                   hover_enabled
                       ? focus_opacity(scene, WM_SETTINGS_CONTROL_PREVIOUS)
                       : 0.0f,
                   alpha);
    if (page < SETTINGS_INDEX_PAGES)
        draw_arrow(scene, origin + 514.0f, 180.0f, true,
                   hover_enabled
                       ? focus_opacity(scene, WM_SETTINGS_CONTROL_NEXT)
                       : 0.0f,
                   alpha);
    for (int icon = 0; icon < SETTINGS_INDEX_PAGES; icon++) {
        float x = origin + 440.0f + 48.0f * icon;
        bool selected = page == (unsigned)(icon + 1);
        const char *image = selected
            ? "textures/settings_html/page-on.png"
            : "textures/settings_html/page-off.png";
        if (!draw_image(scene, image, x, 376.0f, 40.0f, 32.0f,
                        (WmColor){1, 1, 1, alpha}))
            draw_rectangle(scene, x, 376.0f, 40.0f, 32.0f,
                           selected ? (WmColor){1, 1, 1, alpha}
                                    : (WmColor){0.5f, 0.5f, 0.5f, alpha});
        char number[2] = {(char)('1' + icon), '\0'};
        draw_text(scene, number, x + 20.0f,
                  SETTINGS_PAGE_BADGE_TEXT_TOP, 24.0f,
                  (WmColor){0.2f, 0.2f, 0.2f, 1}, alpha,
                  WM_FONT_ALIGN_CENTER);
    }
}

static void draw_choice_outline(WmSettingsScene *scene, float x, float y,
                                float width, float height) {
    const WmColor border = {0.35f, 0.68f, 0.82f, 1.0f};
    draw_rectangle(scene, x, y, width, 3.0f, border);
    draw_rectangle(scene, x, y + height - 3.0f, width, 3.0f, border);
    draw_rectangle(scene, x, y, 3.0f, height, border);
    draw_rectangle(scene, x + width - 3.0f, y, 3.0f, height, border);
}

static void draw_category_header(WmSettingsScene *scene, const char *title,
                                 bool nested) {
    const WmColor dark = {0.2f, 0.2f, 0.2f, 1.0f};
    float origin = 16.0f;
    bool connection_flow = scene->active_category == SETTINGS_INTERNET &&
                           scene->detail >= INTERNET_CONNECTION_SELECT &&
                           scene->detail <= INTERNET_USB_EXISTING_CONNECTOR;
    const char *first_tab = connection_flow
        ? "textures/settings_html/tab-dark-gray.png" : nested
        ? "textures/settings_html/tab-middle-gray.png"
        : "textures/settings_html/tab-gray.png";
    if (!draw_image(scene, first_tab,
                    origin + 24.0f, 35.0f, 32.0f, 26.0f,
                    (WmColor){1, 1, 1, 1}))
        draw_rectangle(scene, origin + 24.0f, 35.0f, 32.0f, 26.0f,
                       (WmColor){0.49f, 0.49f, 0.49f, 1.0f});
    if (connection_flow &&
        !draw_image(scene,
                    "textures/settings_html/tab-middle-gray-nested.png",
                    origin + 56.0f, 35.0f, 32.0f, 26.0f,
                    (WmColor){1, 1, 1, 1}))
        draw_rectangle(scene, origin + 56.0f, 35.0f, 32.0f, 26.0f,
                       (WmColor){0.49f, 0.49f, 0.49f, 1.0f});
    float nested_tab_x = connection_flow ? 88.0f :
        scene->active_category == SETTINGS_CALENDAR
        ? 56.0f : 54.0f;
    float nested_tab_width = scene->active_category == SETTINGS_CALENDAR &&
                             scene->detail == 1 ? 40.0f : 32.0f;
    if (nested &&
        !draw_image(scene, "textures/settings_html/tab-gray-nested.png",
                    origin + nested_tab_x, 35.0f,
                    nested_tab_width, 26.0f,
                    (WmColor){1, 1, 1, 1}))
        draw_rectangle(scene, origin + nested_tab_x, 35.0f,
                       nested_tab_width, 26.0f,
                       (WmColor){0.49f, 0.49f, 0.49f, 1.0f});
    float banner_x = origin + (connection_flow ? 120.0f : nested
        ? (scene->active_category == SETTINGS_CALENDAR ? 88.0f : 86.0f)
        : 56.0f);
    if (!draw_image(scene, "textures/settings_html/title-tab.png",
                    banner_x, 27.0f, 408.0f, 36.0f,
                    (WmColor){1, 1, 1, 1}))
        draw_rectangle(scene, banner_x, 27.0f, 408.0f, 36.0f,
                       (WmColor){1, 1, 1, 1});
    draw_text(scene, title, banner_x + 8.0f,
              SETTINGS_TITLE_TEXT_TOP, 24.0f,
              dark, 1.0f, WM_FONT_ALIGN_LEFT);
}

static void draw_category_footer(WmSettingsScene *scene,
                                 const char *left_label,
                                 const char *right_label) {
    const WmColor dark = {0.2f, 0.2f, 0.2f, 1.0f};
    const char *normal_focus =
        "textures/settings_html/footer-button-focus.png";
    const char *red_focus =
        "textures/settings_html/footer-button-red-focus.png";
    /* The extracted Format pages use a red rollover on the action that
     * advances formatting: right on the first two pages, left on the last
     * confirmation page. Their other footer uses the ordinary rollover. */
    bool format_action = scene->active_category == SETTINGS_FORMAT &&
                         scene->detail <= 2;
    const char *left_focus = format_action && scene->detail == 2
        ? red_focus : normal_focus;
    const char *right_focus = format_action && scene->detail < 2
        ? red_focus : normal_focus;
    float origin = 16.0f;
    if (left_label) {
        float focus = focus_opacity(scene, WM_SETTINGS_CONTROL_BACK);
        if (draw_image(scene, "textures/settings_html/footer-button.png",
                       origin + 28.0f, 371.0f, 272.0f, 72.0f,
                       (WmColor){1, 1, 1, 1})) {
            if (focus > 0.0f &&
                !draw_image(scene, left_focus,
                            origin + 28.0f, 371.0f, 272.0f, 72.0f,
                            (WmColor){1, 1, 1, focus}))
                draw_button_focus(scene, origin + 28.0f, 371.0f,
                                  272.0f, 72.0f, focus);
        } else {
            draw_button(scene, origin + 28.0f, 371.0f, 272.0f, 72.0f,
                        focus > 0.0f, 1.0f);
        }
        draw_text(scene, left_label, origin + 164.0f,
                  SETTINGS_FOOTER_TEXT_TOP, 24.0f,
                  dark, 1.0f, WM_FONT_ALIGN_CENTER);
    }
    if (right_label) {
        float focus = focus_opacity(scene, WM_SETTINGS_CONTROL_NEXT);
        if (draw_image(scene, "textures/settings_html/footer-button.png",
                       origin + 308.0f, 371.0f, 272.0f, 72.0f,
                       (WmColor){1, 1, 1, 1})) {
            if (focus > 0.0f &&
                !draw_image(scene, right_focus,
                            origin + 308.0f, 371.0f, 272.0f, 72.0f,
                            (WmColor){1, 1, 1, focus}))
                draw_button_focus(scene, origin + 308.0f, 371.0f,
                                  272.0f, 72.0f, focus);
        } else {
            draw_button(scene, origin + 308.0f, 371.0f, 272.0f, 72.0f,
                        focus > 0.0f, 1.0f);
        }
        draw_text(scene, right_label, origin + 444.0f,
                  SETTINGS_FOOTER_TEXT_TOP, 24.0f,
                  dark, 1.0f, WM_FONT_ALIGN_CENTER);
    }
}

static void draw_adjust_arrow(WmSettingsScene *scene, float x, float y,
                              bool up, WmSettingsControl control) {
    const char *normal = up
        ? "textures/settings_html/arrow-up.png"
        : "textures/settings_html/arrow-down.png";
    const char *focused = up
        ? "textures/settings_html/arrow-up-focus.png"
        : "textures/settings_html/arrow-down-focus.png";
    float focus = focus_opacity(scene, control);
    bool base_drawn = focus >= 1.0f ||
        draw_image(scene, normal, 16.0f + x, y, 72.0f, 72.0f,
                   (WmColor){1, 1, 1, 1.0f - focus});
    if (base_drawn) {
        if (focus <= 0.0f ||
            draw_image(scene, focused, 16.0f + x, y, 72.0f, 72.0f,
                       (WmColor){1, 1, 1, focus})) return;
        draw_image(scene, normal, 16.0f + x, y, 72.0f, 72.0f,
                   (WmColor){1, 1, 1, focus});
        return;
    }
    draw_button(scene, 16.0f + x, y, 64.0f, 64.0f,
                focus > 0.0f, 1.0f);
    draw_text(scene, up ? "^" : "v", 16.0f + x + 32.0f, y + 15.0f,
              30.0f, (WmColor){0.2f, 0.2f, 0.2f, 1.0f},
              1.0f, WM_FONT_ALIGN_CENTER);
}

static void draw_calendar_edit(WmSettingsScene *scene) {
    const WmColor white = {1, 1, 1, 1};
    char value[32];
    if (scene->detail == 1) {
        const float arrow_x[6] = {400, 400, 88, 88, 224, 224};
        const float arrow_y[6] = {108, 253, 108, 253, 108, 253};
        for (unsigned item = 0; item < 6; item++)
            draw_adjust_arrow(scene, arrow_x[item], arrow_y[item],
                              item % 2 == 0,
                              (WmSettingsControl)(WM_SETTINGS_CONTROL_ITEM_1 +
                                                  item));
        snprintf(value, sizeof(value), "%02u", scene->edit_month);
        draw_text(scene, value, 16.0f + 124.0f, 183.0f, 60.0f,
                  white, 1.0f, WM_FONT_ALIGN_CENTER);
        draw_text(scene, "/", 16.0f + 192.0f, 183.0f, 60.0f,
                  white, 1.0f, WM_FONT_ALIGN_CENTER);
        snprintf(value, sizeof(value), "%02u", scene->edit_day);
        draw_text(scene, value, 16.0f + 260.0f, 183.0f, 60.0f,
                  white, 1.0f, WM_FONT_ALIGN_CENTER);
        draw_text(scene, "/", 16.0f + 328.0f, 183.0f, 60.0f,
                  white, 1.0f, WM_FONT_ALIGN_CENTER);
        snprintf(value, sizeof(value), "%04u", 2000 + scene->edit_year);
        draw_text(scene, value, 16.0f + 440.0f, 183.0f, 60.0f,
                  white, 1.0f, WM_FONT_ALIGN_CENTER);
    } else {
        const float arrow_x[4] = {200, 200, 336, 336};
        const float arrow_y[4] = {108, 253, 108, 253};
        for (unsigned item = 0; item < 4; item++)
            draw_adjust_arrow(scene, arrow_x[item], arrow_y[item],
                              item % 2 == 0,
                              (WmSettingsControl)(WM_SETTINGS_CONTROL_ITEM_1 +
                                                  item));
        snprintf(value, sizeof(value), "%02u", scene->edit_hour);
        draw_text(scene, value, 16.0f + 236.0f, 183.0f, 60.0f,
                  white, 1.0f, WM_FONT_ALIGN_CENTER);
        draw_text(scene, ":", 16.0f + 304.0f, 183.0f, 60.0f,
                  white, 1.0f, WM_FONT_ALIGN_CENTER);
        snprintf(value, sizeof(value), "%02u", scene->edit_minute);
        draw_text(scene, value, 16.0f + 372.0f, 183.0f, 60.0f,
                  white, 1.0f, WM_FONT_ALIGN_CENTER);
    }
}

static void draw_sensitivity_screen(WmSettingsScene *scene) {
    const WmColor white = {1, 1, 1, 1};
    if (scene->sensitivity_instructions) {
        static const char *const lines[] = {
            "The pointing device can't be used",
            "to navigate until sensitivity is set.",
            "Press Left and Right on the Wii",
            "Remote to set sensitivity,",
            "then press A. Check the Wii",
            "Operations Manual for details."
        };
        for (unsigned line = 0; line < 6; line++)
            draw_text(scene, lines[line], 320.0f, 129.0f + 29.0f * line,
                      24.0f, white, 1.0f, WM_FONT_ALIGN_CENTER);
        return;
    }
    static const char *const rank_art[] = {
        "textures/settings_html/sensitivity-rank-1.png",
        "textures/settings_html/sensitivity-rank-2.png",
        "textures/settings_html/sensitivity-rank-3.png",
        "textures/settings_html/sensitivity-rank-4.png",
        "textures/settings_html/sensitivity-rank-5.png"
    };
    draw_image(scene, "textures/settings_html/sensitivity-minus.png",
               16.0f + 74.0f, 304.0f, 32.0f, 32.0f, white);
    draw_image(scene, "textures/settings_html/sensitivity-gauge.png",
               16.0f + 156.0f, 296.0f, 296.0f, 48.0f, white);
    draw_image(scene, "textures/settings_html/sensitivity-plus.png",
               16.0f + 502.0f, 304.0f, 32.0f, 32.0f, white);
    unsigned rank = scene->edit_sensitivity;
    if (rank >= 1 && rank <= 5)
        draw_image(scene, rank_art[rank - 1],
                   16.0f + 132.0f + (rank - 1) * 72.0f,
                   300.0f, 56.0f, 56.0f, white);
    draw_text(scene, "Press A when done.", 320.0f, 381.0f,
              24.0f, white, 1.0f, WM_FONT_ALIGN_CENTER);
}

static void draw_centered_lines(WmSettingsScene *scene,
                                const char *const *lines,
                                unsigned count, float first_y,
                                float line_height) {
    const WmColor white = {1, 1, 1, 1};
    for (unsigned line = 0; line < count; line++)
        draw_text(scene, lines[line], 320.0f,
                  first_y + line_height * line, 23.0f,
                  white, 1.0f, WM_FONT_ALIGN_CENTER);
}

static void draw_usb_prompt_lines(WmSettingsScene *scene,
                                  const char *const *lines,
                                  unsigned count, float first_y) {
    /* Common0201/0202/0204 use List.css's 24 px centered text within
     * the 288 px message cell below the connection tab. */
    const WmColor white = {1, 1, 1, 1};
    for (unsigned line = 0; line < count; line++)
        draw_text(scene, lines[line], 320.0f,
                  first_y + 30.0f * line, 24.0f,
                  white, 1.0f, WM_FONT_ALIGN_CENTER);
}

static void draw_large_rows(WmSettingsScene *scene,
                            const char *const *labels, unsigned count,
                            int first_y, bool selectable);
static void draw_large_rows_with_disabled(WmSettingsScene *scene,
                                          const char *const *labels,
                                          unsigned count, int first_y,
                                          unsigned enabled_count);
static void draw_connection_rows(WmSettingsScene *scene);
static void draw_wireless_choices(WmSettingsScene *scene);

static void draw_country_screen(WmSettingsScene *scene) {
    unsigned page = scene->country_page;
    unsigned first = country_page_start[page];
    unsigned count = page == 0 || page == 9 ? 4 : 5;
    float first_y = page == 0 ? 132.0f : 76.0f;
    const WmColor dark = {0.2f, 0.2f, 0.2f, 1.0f};
    if (page == 0)
        draw_text(scene, "Select your country of residence.",
                  320.0f, 92.0f, 24.0f,
                  (WmColor){1, 1, 1, 1}, 1.0f,
                  WM_FONT_ALIGN_CENTER);
    for (unsigned item = 0; item < count; item++) {
        float y = first_y + item * 56.0f;
        WmSettingsControl control =
            (WmSettingsControl)(WM_SETTINGS_CONTROL_ITEM_1 + item);
        if (!draw_image(scene, "textures/settings_html/country-row.png",
                        16.0f + 88.0f, y, 432.0f, 56.0f,
                        (WmColor){1, 1, 1, 1}))
            draw_button(scene, 16.0f + 88.0f, y, 432.0f, 56.0f,
                        false, 1.0f);
        if (scene->edit_country_choice == first + item) {
            bool left = draw_image(
                scene, "textures/settings_html/country-choice-left.png",
                16.0f + 79.0f, y - 4.0f, 24.0f, 64.0f,
                (WmColor){1, 1, 1, 1});
            bool right = draw_image(
                scene, "textures/settings_html/country-choice-right.png",
                16.0f + 503.0f, y - 4.0f, 24.0f, 64.0f,
                (WmColor){1, 1, 1, 1});
            if (!left || !right)
                draw_choice_outline(scene, 16.0f + 79.0f, y - 4.0f,
                                    448.0f, 64.0f);
        }
        float focus = focus_opacity(scene, control);
        if (focus > 0.0f &&
            !draw_image(scene, "textures/settings_html/country-row-focus.png",
                        16.0f + 88.0f, y, 432.0f, 56.0f,
                        (WmColor){1, 1, 1, focus}))
            draw_button_focus(scene, 16.0f + 88.0f, y,
                              432.0f, 56.0f, focus);
        draw_text(scene, country_names[first + item],
                  16.0f + 304.0f, y + 16.0f, 24.0f,
                  dark, 1.0f, WM_FONT_ALIGN_CENTER);
    }
    if (page > 0)
        draw_adjust_arrow(scene, 528.0f, 76.0f, true,
                          WM_SETTINGS_CONTROL_PREVIOUS);
    if (page < 9)
        draw_adjust_arrow(scene, 528.0f, 282.0f, false,
                          WM_SETTINGS_CONTROL_ITEM_6);
}

static void draw_console_information(WmSettingsScene *scene) {
    /* The maintained HTML bridge supplies local dummy MAC values. The
     * extracted MAC_address.html page dims the LAN address when no adapter
     * is available; displaying a host or console identifier here would be
     * both inaccurate and unnecessary. */
    const WmColor available = {1.0f, 1.0f, 1.0f, 1.0f};
    const WmColor unavailable = {0.2f, 0.2f, 0.2f, 1.0f};
    const char *const placeholder = "00-00-00-00-00-00";
    draw_text(scene, "MAC Address", 320.0f, 115.0f, 24.0f,
              available, 1.0f, WM_FONT_ALIGN_CENTER);
    draw_text(scene, placeholder, 320.0f, 139.0f, 36.0f,
              available, 1.0f, WM_FONT_ALIGN_CENTER);
    draw_text(scene, "LAN Adapter MAC Address", 320.0f, 257.0f, 24.0f,
              unavailable, 1.0f, WM_FONT_ALIGN_CENTER);
    draw_text(scene, placeholder, 320.0f, 281.0f, 36.0f,
              unavailable, 1.0f, WM_FONT_ALIGN_CENTER);
}

static void draw_extended_category(WmSettingsScene *scene,
                                   const char **left_label,
                                   const char **right_label) {
    static const char *const internet_rows[3] = {
        "Connection Settings", "Console Information", "User Agreements"
    };
    static const char *const connection_type_rows[2] = {
        "Wireless Connection", "Wired Connection"
    };
    static const char *const usb_instructions[6] = {
        "Install the Nintendo Wi-Fi USB",
        "Connector software on your PC",
        "and then insert the Nintendo",
        "Wi-Fi USB Connector into your",
        "PC's USB port. Choose Next to",
        "continue."
    };
    static const char *const usb_registration[4] = {
        "Use the Nintendo Wi-Fi",
        "USB Connector registration",
        "tool on your PC to grant this",
        "Wii permission to connect."
    };
    static const char *const usb_existing_connector[6] = {
        "A Nintendo Wi-Fi USB Connector",
        "that has already granted",
        "connection access has been",
        "found. Wait for a different",
        "Nintendo Wi-Fi USB Connector",
        "to grant access?"
    };
    static const char *const connect24_rows[3] = {
        "WiiConnect24", "Standby Connection", "Slot Illumination"
    };
    static const char *const on_off_rows[2] = {"On", "Off"};
    static const char *const light_rows[3] = {"Bright", "Dim", "Off"};
    static const char *const parental_intro[1] = {
        "Use Parental Controls?"
    };
    static const char *const parental_description1[4] = {
        "The Parental Controls feature allows you",
        "to control access to certain software",
        "and Internet content. For details, please",
        "refer to the Wii Operations Manual."
    };
    static const char *const parental_description2[3] = {
        "This setting should be implemented by",
        "parents or guardians. This function does",
        "not work with Nintendo GameCube games."
    };
    static const char *const update_question[2] = {
        "Connect to the Internet and",
        "perform a Wii system update?"
    };
    static const char *const update_offline[2] = {
        "An update cannot be performed",
        "in this local menu."
    };
    static const char *const format_intro[7] = {
        "All added channels and save",
        "data will be erased. Once",
        "erased, it can never be",
        "recovered. Even channels that",
        "have been copied to an",
        "SD Card can't be restored.",
        "Do you still want to format?"
    };
    static const char *const format_shop[7] = {
        "If you use the Wii Shop",
        "Channel, it is recommended",
        "that you remove your",
        "Wii Shop Channel Account",
        "before reformatting your",
        "Wii System Memory. Do you",
        "still want to reformat?"
    };
    static const char *const format_final[5] = {
        "Remember, all of your",
        "current data will be",
        "permanently lost. Are you",
        "sure you want to format",
        "the Wii System Memory?"
    };
    static const char *const format_local[2] = {
        "Local menu settings have been reset.",
        "No console storage was changed."
    };
    switch (scene->active_category) {
        case SETTINGS_NICKNAME:
            draw_rectangle(scene, 16.0f + 80.0f, 185.0f, 448.0f, 70.0f,
                           (WmColor){0.83f, 0.86f, 0.89f, 1.0f});
            draw_text(scene, scene->edit_nickname, 320.0f, 202.0f, 30.0f,
                      (WmColor){0.15f, 0.18f, 0.22f, 1.0f},
                      1.0f, WM_FONT_ALIGN_CENTER);
            *right_label = "Confirm";
            break;
        case SETTINGS_PARENTAL:
            if (!scene->detail) {
                draw_centered_lines(scene, parental_intro, 1, 194.0f, 30.0f);
                *left_label = "Yes";
                *right_label = "No";
            } else {
                if (scene->detail == 1)
                    draw_centered_lines(scene, parental_description1,
                                        4, 152.0f, 32.0f);
                else
                    draw_centered_lines(scene, parental_description2,
                                        3, 168.0f, 32.0f);
                *right_label = "OK";
            }
            break;
        case SETTINGS_INTERNET:
            if (!scene->detail)
                draw_large_rows(scene, internet_rows, 3, 85, false);
            else if (scene->detail == 1)
                draw_connection_rows(scene);
            else if (scene->detail == 2)
                draw_console_information(scene);
            else if (scene->detail == 3) {
                static const char *const agreement[2] = {
                    "Would you like to use the Wii Shop Channel",
                    "and WiiConnect24?"
                };
                draw_centered_lines(scene, agreement, 2, 177.0f, 34.0f);
                *left_label = "Yes";
                *right_label = "No";
            } else if (scene->detail == INTERNET_CONNECTION_SELECT)
                draw_large_rows(scene, connection_type_rows, 2, 133, false);
            else if (scene->detail == INTERNET_WIRELESS_CHOICES)
                draw_wireless_choices(scene);
            else if (scene->detail == INTERNET_WIRED_PROMPT) {
                draw_text(scene, "Initiating connection test.",
                          320.0f, 204.0f, 24.0f,
                          (WmColor){1, 1, 1, 1}, 1.0f,
                          WM_FONT_ALIGN_CENTER);
                *left_label = NULL;
                *right_label = "OK";
            } else if (scene->detail == INTERNET_ACCESS_POINT_SEARCH ||
                       scene->detail == INTERNET_NO_ACCESS_POINT) {
                const char *message = scene->detail ==
                    INTERNET_ACCESS_POINT_SEARCH
                    ? "Searching for an access point..."
                    : "No access point was found.";
                draw_text(scene, message, 320.0f, 204.0f, 24.0f,
                          (WmColor){1, 1, 1, 1}, 1.0f,
                          WM_FONT_ALIGN_CENTER);
                *left_label = NULL;
                *right_label = scene->detail == INTERNET_NO_ACCESS_POINT
                    ? "OK" : NULL;
            } else if (scene->detail == INTERNET_USB_INSTRUCTIONS) {
                draw_usb_prompt_lines(scene, usb_instructions, 6, 127.0f);
                *left_label = "Cancel";
                *right_label = "Next";
            } else if (scene->detail == INTERNET_USB_REGISTRATION) {
                draw_usb_prompt_lines(scene, usb_registration, 4, 157.0f);
                *left_label = "Cancel";
                *right_label = NULL;
            } else if (scene->detail == INTERNET_USB_EXISTING_CONNECTOR) {
                draw_usb_prompt_lines(scene, usb_existing_connector,
                                      6, 127.0f);
                *left_label = "Yes";
                *right_label = "No";
            }
            break;
        case SETTINGS_CONNECT24:
            if (!scene->detail) {
                draw_large_rows_with_disabled(
                    scene, connect24_rows, 3, 85,
                    scene->connect24_enabled ? 3 : 1);
            } else {
                draw_large_rows(scene,
                                scene->detail == 3 ? light_rows : on_off_rows,
                                scene->detail == 3 ? 3 : 2,
                                scene->detail == 3 ? 85 : 133, true);
                *right_label = "Confirm";
            }
            break;
        case SETTINGS_COUNTRY:
            draw_country_screen(scene);
            *right_label = "OK";
            break;
        case SETTINGS_UPDATE:
            draw_centered_lines(scene,
                                scene->detail ? update_offline
                                              : update_question,
                                2, 177.0f, 34.0f);
            *left_label = scene->detail ? "Back" : "Yes";
            *right_label = scene->detail ? "OK" : "No";
            break;
        case SETTINGS_FORMAT:
            if (!scene->detail)
                draw_centered_lines(scene, format_intro,
                                    7, 84.0f, 34.0f);
            else if (scene->detail == 1)
                draw_centered_lines(scene, format_shop,
                                    7, 84.0f, 34.0f);
            else if (scene->detail == 2)
                draw_centered_lines(scene, format_final,
                                    5, 120.0f, 34.0f);
            else
                draw_centered_lines(scene, format_local,
                                    2, 177.0f, 34.0f);
            *left_label = scene->detail == 2 ? "Format" :
                          scene->detail == 3 ? NULL : "Cancel";
            *right_label = scene->detail == 2 ? "No" :
                           scene->detail == 3 ? "OK" : "Format";
            if (scene->detail != 3)
                draw_text(scene,
                          "Local preview: no NAND or channel data will change.",
                          320.0f, 339.0f, 17.0f,
                          (WmColor){0.8f, 0.87f, 0.92f, 1.0f},
                          1.0f, WM_FONT_ALIGN_CENTER);
            break;
    }
}

static void draw_compact_rows(WmSettingsScene *scene,
                              const char *const *labels, unsigned count) {
    const WmColor dark = {0.2f, 0.2f, 0.2f, 1.0f};
    for (unsigned item = 0; item < count; item++) {
        WmSettingsControl control =
            (WmSettingsControl)(WM_SETTINGS_CONTROL_ITEM_1 + item);
        float y = 78.0f + item * 72.0f;
        float focus = focus_opacity(scene, control);
        if (draw_image(scene, "textures/settings_html/index-row.png",
                       16.0f + 104.0f, y - 2.0f, 400.0f, 64.0f,
                       (WmColor){1, 1, 1, 1})) {
            if (focus > 0.0f &&
                !draw_image(scene,
                            "textures/settings_html/index-row-focus.png",
                            16.0f + 104.0f, y, 400.0f, 60.0f,
                            (WmColor){1, 1, 1, focus}))
                draw_button_focus(scene, 16.0f + 104.0f, y,
                                  400.0f, 60.0f, focus);
        } else {
            draw_button(scene, 16.0f + 104.0f, y, 400.0f, 60.0f,
                        focus > 0.0f, 1.0f);
        }
        draw_text(scene, labels[item], 16.0f + 304.0f, y + 14.0f, 24.0f,
                  dark, 1.0f, WM_FONT_ALIGN_CENTER);
    }
}

static void draw_large_rows_impl(WmSettingsScene *scene,
                                 const char *const *labels, unsigned count,
                                 int first_y, bool selectable,
                                 unsigned enabled_count) {
    const WmColor dark = {0.2f, 0.2f, 0.2f, 1.0f};
    for (unsigned item = 0; item < count; item++) {
        WmSettingsControl control =
            (WmSettingsControl)(WM_SETTINGS_CONTROL_ITEM_1 + item);
        float y = (float)(first_y + (int)item * 96);
        float focus = focus_opacity(scene, control);
        const char *background = item < enabled_count
            ? "textures/settings_html/large-row.png"
            : "textures/settings_html/large-row-disabled.png";
        bool source_drawn = draw_image(scene, background,
                                       16.0f + 104.0f, y - 5.0f,
                                       400.0f, 80.0f,
                                       (WmColor){1, 1, 1, 1});
        if (source_drawn) {
            if (item < enabled_count && focus > 0.0f &&
                !draw_image(scene,
                            "textures/settings_html/large-row-focus.png",
                            16.0f + 107.0f, y, 394.0f, 70.0f,
                            (WmColor){1, 1, 1, focus}))
                draw_button_focus(scene, 16.0f + 107.0f, y,
                                  394.0f, 70.0f, focus);
        } else {
            draw_button(scene, 16.0f + 107.0f, y, 394.0f, 70.0f,
                        item < enabled_count && focus > 0.0f, 1.0f);
            if (item >= enabled_count)
                draw_rectangle(scene, 16.0f + 107.0f, y,
                               394.0f, 70.0f,
                               (WmColor){0, 0, 0, 0.4f});
        }
        draw_text(scene, labels[item], 16.0f + 304.0f, y + 20.0f,
                  24.0f, dark, 1.0f, WM_FONT_ALIGN_CENTER);
        if (item < enabled_count && selectable && scene->selection == item) {
            bool left = draw_image(
                scene, "textures/settings_html/choice-left.png",
                16.0f + 94.0f, y - 13.0f, 24.0f, 96.0f,
                (WmColor){1, 1, 1, 1});
            bool right = draw_image(
                scene, "textures/settings_html/choice-right.png",
                16.0f + 490.0f, y - 13.0f, 24.0f, 96.0f,
                (WmColor){1, 1, 1, 1});
            if (!left || !right)
                draw_choice_outline(scene, 16.0f + 94.0f, y - 13.0f,
                                    420.0f, 96.0f);
        }
    }
}

static void draw_large_rows(WmSettingsScene *scene,
                            const char *const *labels, unsigned count,
                            int first_y, bool selectable) {
    draw_large_rows_impl(scene, labels, count, first_y, selectable, count);
}

static void draw_large_rows_with_disabled(WmSettingsScene *scene,
                                          const char *const *labels,
                                          unsigned count, int first_y,
                                          unsigned enabled_count) {
    draw_large_rows_impl(scene, labels, count, first_y, false,
                         enabled_count);
}

static void draw_connection_rows(WmSettingsScene *scene) {
    const WmColor dark = {0.2f, 0.2f, 0.2f, 1.0f};
    for (unsigned item = 0; item < 3; item++) {
        float top = 72.0f + item * 96.0f;
        float focus = focus_opacity(
            scene, (WmSettingsControl)(WM_SETTINGS_CONTROL_ITEM_1 + item));
        /* Connect_set_top.html uses a split 400×76 background, with separate
         * Connection and type fields. The local bridge starts each profile
         * unconfigured, so its type field reads None. */
        if (!draw_image(scene,
                        "textures/settings_html/connection-split-row.png",
                        16.0f + 104.0f, top + 10.0f, 400.0f, 76.0f,
                        (WmColor){1, 1, 1, 1}))
            draw_image(scene, "textures/settings_html/large-row.png",
                       16.0f + 104.0f, top + 8.0f, 400.0f, 80.0f,
                       (WmColor){1, 1, 1, 1});
        char name[24];
        snprintf(name, sizeof(name), "Connection %u", item + 1);
        draw_text(scene, name, 16.0f + 228.0f, top + 32.0f, 24.0f,
                  dark, 1.0f, WM_FONT_ALIGN_CENTER);
        draw_text(scene, "None", 16.0f + 420.0f, top + 32.0f, 24.0f,
                  dark, 1.0f, WM_FONT_ALIGN_CENTER);
        if (focus > 0.0f &&
            !draw_image(scene, "textures/settings_html/large-row-focus.png",
                        16.0f + 107.0f, top + 13.0f, 394.0f, 70.0f,
                        (WmColor){1, 1, 1, focus}))
            draw_button_focus(scene, 16.0f + 107.0f, top + 13.0f,
                              394.0f, 70.0f, focus);
    }
}

static void draw_wireless_choices(WmSettingsScene *scene) {
    static const char *const main_rows[2] = {
        "Search for an Access Point", "Nintendo Wi-Fi USB Connector"
    };
    const WmColor opaque = {1, 1, 1, 1};
    const WmColor dark = {0.2f, 0.2f, 0.2f, 1};
    const float button_x[2] = {104.0f, 339.0f};
    draw_large_rows(scene, main_rows, 2, 85, false);
    for (unsigned item = 0; item < 2; item++) {
        float x = 16.0f + button_x[item];
        if (!draw_image(scene, "textures/settings_html/small-row.png",
                        x, 274.0f, 168.0f, 76.0f, opaque))
            draw_button(scene, x, 274.0f, 168.0f, 76.0f, false, 1.0f);
        float focus = focus_opacity(scene,
            (WmSettingsControl)(WM_SETTINGS_CONTROL_ITEM_3 + item));
        if (focus > 0.0f &&
            !draw_image(scene, "textures/settings_html/small-row-focus.png",
                        x, item == 0 ? 274.0f : 277.0f,
                        168.0f, item == 0 ? 76.0f : 70.0f,
                        (WmColor){1, 1, 1, focus}))
            draw_button_focus(scene, x, item == 0 ? 274.0f : 277.0f,
                              168.0f, item == 0 ? 76.0f : 70.0f, focus);
    }
    draw_image(scene, "textures/settings_html/aoss-icon.png",
               16.0f + 156.5f, 284.0f, 63.0f, 56.0f, opaque);
    draw_text(scene, "Manual Setup", 16.0f + 423.0f, 299.0f, 24.0f,
              dark, 1.0f, WM_FONT_ALIGN_CENTER);
}

static void draw_position_screen(WmSettingsScene *scene) {
    draw_image(scene, "textures/settings_html/position-flame-left.png",
               16.0f + 30.0f, 80.0f, 28.0f, 272.0f,
               (WmColor){1, 1, 1, 1});
    draw_image(scene, "textures/settings_html/position-flame-right.png",
               16.0f + 550.0f, 80.0f, 28.0f, 272.0f,
               (WmColor){1, 1, 1, 1});
    draw_arrow(scene, 16.0f + 160.0f, 180.0f, false,
               focus_opacity(scene, WM_SETTINGS_CONTROL_ITEM_1), 1.0f);
    draw_arrow(scene, 16.0f + 376.0f, 180.0f, true,
               focus_opacity(scene, WM_SETTINGS_CONTROL_ITEM_2), 1.0f);
    char position[16];
    int delta = ((int)scene->selection - 16) / 2;
    if (delta > 0)
        snprintf(position, sizeof(position), "-%d", delta);
    else if (delta < 0)
        snprintf(position, sizeof(position), "+%d", -delta);
    else
        snprintf(position, sizeof(position), "0");
    draw_text(scene, position, 16.0f + 304.0f, 173.0f, 60.0f,
              (WmColor){1, 1, 1, 1}, 1.0f, WM_FONT_ALIGN_CENTER);
}

static void draw_widescreen_screen(WmSettingsScene *scene) {
    const WmColor dark = {0.2f, 0.2f, 0.2f, 1.0f};
    const float xs[2] = {48.0f, 296.0f};
    const float widths[2] = {200.0f, 264.0f};
    unsigned language = scene->language_choice;
    const char *backgrounds[2] = {
        "textures/settings_html/widescreen-standard.png",
        "textures/settings_html/widescreen-wide.png"
    };
    const char *focus_images[2] = {
        "textures/settings_html/widescreen-standard-focus.png",
        "textures/settings_html/widescreen-wide-focus.png"
    };
    for (unsigned item = 0; item < 2; item++) {
        float x = 16.0f + xs[item];
        float focus = focus_opacity(scene, (WmSettingsControl)(
            WM_SETTINGS_CONTROL_ITEM_1 + item));
        if (draw_image(scene, backgrounds[item], x, 146.0f,
                       widths[item], 140.0f,
                       (WmColor){1, 1, 1, 1})) {
            if (focus > 0.0f &&
                !draw_image(scene, focus_images[item], x, 146.0f,
                            widths[item], 140.0f,
                            (WmColor){1, 1, 1, focus}))
                draw_button_focus(scene, x, 146.0f, widths[item],
                                  140.0f, focus);
        } else {
            draw_button(scene, x, 146.0f, widths[item], 140.0f,
                        focus > 0.0f, 1.0f);
        }
        draw_text(scene, widescreen_labels[language][item],
                  x + widths[item] * 0.5f,
                  204.0f, 24.0f, dark, 1.0f, WM_FONT_ALIGN_CENTER);
        if (scene->selection == item) {
            float flame_x = 16.0f + (item ? 286.0f : 38.0f);
            float flame_right_x = flame_x + widths[item] - 4.0f;
            bool left = draw_image(
                scene, "textures/settings_html/tv-choice-left.png",
                flame_x, 136.0f, 24.0f, 160.0f,
                (WmColor){1, 1, 1, 1});
            bool right = draw_image(
                scene, "textures/settings_html/tv-choice-right.png",
                flame_right_x, 136.0f, 24.0f, 160.0f,
                (WmColor){1, 1, 1, 1});
            if (!left || !right)
                draw_choice_outline(scene, flame_x, 136.0f,
                                    widths[item] + 20.0f, 160.0f);
        }
    }
}

static void draw_category_page(WmSettingsScene *scene) {
    static const char *const sound_labels[3] = {
        "Mono", "Stereo", "Surround"
    };
    static const char *const language_labels[3] = {
        "English", "Français", "Español"
    };
    static const char *const calendar_labels[2] = {"Date", "Time"};
    static const char *const sensor_labels[2] = {
        "Sensor Bar Position", "Sensitivity"
    };
    static const char *const sensor_position_labels[2] = {
        "Above TV", "Below TV"
    };
    unsigned language = scene->active_category == SETTINGS_LANGUAGE
        ? scene->selection : scene->language_choice;
    const char *title = localized_category_label(language,
                                                 scene->active_category);
    const char *left_label = back_labels[language];
    const char *right_label =
        scene->active_category == 4 || scene->active_category == 9
            ? confirm_labels[language] : NULL;
    bool nested = scene->detail != 0;
    if (scene->active_category == 2 && nested)
        title = calendar_labels[scene->detail - 1];
    if (scene->active_category == 3 && nested)
        title = screen_labels[language][scene->detail - 1];
    if (scene->active_category == 6 && nested)
        title = sensor_labels[scene->detail - 1];
    char connection_title[24];
    if (scene->active_category == SETTINGS_INTERNET && nested) {
        static const char *const internet_titles[3] = {
            "Connection Settings", "Console Information",
            "User Agreements"
        };
        if (scene->detail >= INTERNET_CONNECTION_SELECT &&
            scene->detail <= INTERNET_USB_EXISTING_CONNECTOR) {
            snprintf(connection_title, sizeof(connection_title),
                     "Connection %u", scene->connection_slot);
            title = connection_title;
        } else
            title = internet_titles[scene->detail - 1];
    }
    if (scene->active_category == SETTINGS_CONNECT24 && nested) {
        static const char *const connect24_titles[3] = {
            "WiiConnect24", "Standby Connection", "Slot Illumination"
        };
        title = connect24_titles[scene->detail - 1];
    }
    draw_category_header(scene, title, nested);
    if (scene->active_category == SETTINGS_NICKNAME ||
        scene->active_category == SETTINGS_PARENTAL ||
        scene->active_category == SETTINGS_INTERNET ||
        scene->active_category == SETTINGS_CONNECT24 ||
        scene->active_category == SETTINGS_COUNTRY ||
        scene->active_category == SETTINGS_UPDATE ||
        scene->active_category == SETTINGS_FORMAT) {
        draw_extended_category(scene, &left_label, &right_label);
        draw_category_footer(scene, left_label, right_label);
        return;
    }
    if (!nested) {
        switch (scene->active_category) {
            case 2:
                draw_large_rows(scene, calendar_labels, 2, 133, false);
                break;
            case 3:
                draw_compact_rows(scene, screen_labels[language], 4);
                break;
            case 4:
                draw_large_rows(scene, sound_labels, 3, 85, true);
                break;
            case 6:
                draw_large_rows(scene, sensor_labels, 2, 133, false);
                break;
            case 9:
                draw_large_rows(scene, language_labels, 3, 85, true);
                break;
        }
    } else if (scene->active_category == 2) {
        right_label = "Confirm";
        draw_calendar_edit(scene);
    } else if (scene->active_category == 3) {
        right_label = confirm_labels[language];
        switch (scene->detail) {
            case 1: draw_position_screen(scene); break;
            case 2: draw_widescreen_screen(scene); break;
            case 3:
                draw_large_rows(scene, resolution_labels[language],
                                2, 133, true);
                break;
            case 4:
                draw_large_rows(scene, burn_labels[language],
                                2, 133, true);
                break;
        }
    } else if (scene->active_category == 6 && scene->detail == 1) {
        right_label = "Confirm";
        draw_large_rows(scene, sensor_position_labels, 2, 133, true);
    } else if (scene->active_category == 6 && scene->detail == 2) {
        if (scene->sensitivity_instructions)
            right_label = "OK";
        draw_sensitivity_screen(scene);
        if (!scene->sensitivity_instructions) return;
    }
    draw_category_footer(scene, left_label, right_label);
}

static void draw_page_content(WmSettingsScene *scene,
                              const WmClipRect *clip) {
    wm_platform_set_clip(scene->platform, clip);
    if (scene->active_category)
        draw_category_page(scene);
    else
        draw_index_page(scene, scene->page, 0.0f, 1.0f, true);
    wm_platform_set_clip(scene->platform, NULL);
}

bool wm_settings_scene_draw(WmSettingsScene *scene) {
    if (!scene || scene->phase == WM_SETTINGS_CLOSED) return false;
    WmSettingsProjection projection = wm_settings_scene_projection(scene);
    WmClipRect clip = {
        projection.document_x, 0.0f,
        projection.document_width, 456.0f
    };
    if (scene->page_crossfade && scene->phase == WM_SETTINGS_READY &&
        scene->prior_page) {
        WmSettingsScene *prior = scene->prior_page;
        prior->draw_opacity = 1.0f;
        draw_background(prior);
        draw_page_content(prior, &clip);
        scene->draw_opacity = page_opacity(scene);
        if (scene->draw_opacity > 0.0f) {
            /* The retained page already supplies the black shell and the
             * widescreen panels. Only the new 608-pixel document is faded
             * over it, as the source raster texture bank does. */
            draw_document_background(scene, 0.0f, 1.0f);
            draw_page_content(scene, &clip);
        }
        scene->draw_opacity = 1.0f;
        return true;
    }
    scene->draw_opacity = 1.0f;
    bool scrolling = scene->phase == WM_SETTINGS_SCROLL;
    if (scrolling)
        draw_black_background(scene);
    else
        draw_background(scene);
    WmClipRect frame_clip = {0.0f, 0.0f, WM_FRAME_WIDTH, WM_FRAME_HEIGHT};
    wm_platform_set_clip(scene->platform, scrolling ? &frame_clip : &clip);
    if (scene->active_category) {
        draw_category_page(scene);
    } else if (scrolling) {
        /* A widescreen page carries its two 112-unit side panels with the
         * 608-unit document, so adjacent compositions span all 832 units. */
        float page_width = scene->wide ? (float)SETTINGS_WIDE_WIDTH : 608.0f;
        SlideSample sample = scroll_sample(scene);
        float movement = sample.progress * page_width;
        float prior_offset = -scene->direction * movement;
        float next_offset = prior_offset + scene->direction * page_width;
        draw_side_panels(scene, prior_offset, 1.0f);
        draw_document_background(scene, prior_offset, 1.0f);
        draw_index_page(scene, scene->previous_page, prior_offset, 1.0f,
                        false);
        draw_side_panels(scene, next_offset, sample.incoming_alpha);
        draw_document_background(scene, next_offset,
                                 sample.incoming_alpha);
        draw_index_page(scene, scene->page, next_offset,
                        sample.incoming_alpha, false);
    } else {
        float alpha = appearance_opacity(scene);
        draw_index_page(scene, scene->page, 0.0f, alpha,
                        scene->phase == WM_SETTINGS_READY);
    }
    wm_platform_set_clip(scene->platform, NULL);
    return true;
}
