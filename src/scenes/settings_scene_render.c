#include "settings_scene_internal.h"

#include "wii_menu/resources/resource_font.h"

#include <math.h>
#include <stdio.h>
#include <string.h>

enum {
    /* Text origins place the proportional outline font inside the title,
     * footer and page badge bounds. */
    SETTINGS_TITLE_TEXT_TOP = 28,
    SETTINGS_FOOTER_TEXT_TOP = 391,
    SETTINGS_PAGE_BADGE_TEXT_TOP = 376
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

float wm_settings_focus_opacity(const WmSettingsScene *scene,
                                WmSettingsControl control) {
    if (control <= WM_SETTINGS_CONTROL_NONE ||
        control > WM_SETTINGS_CONTROL_ITEM_6) return 0.0f;
    return scene->hover == control ? 1.0f : 0.0f;
}

float wm_settings_page_opacity(const WmSettingsScene *scene) {
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
    /* Map the pane's 477-unit travel by frame 25 to the full 608-pixel
     * page shift in this projection. */
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

static void draw_nickname_text(WmSettingsScene *scene) {
    const WmColor ink = {0.2f, 0.2f, 0.2f, scene->draw_opacity};
    if (scene->outline_font) {
        float scale = scene->wide
            ? (float)WM_FRAME_WIDTH / SETTINGS_WIDE_WIDTH : 1.0f;
        float offset = scene->wide
            ? (SETTINGS_SIDE_WIDTH - 16.0f) * scale : 0.0f;
        if (wm_outline_font_draw_line(scene->outline_font, scene->platform,
                                      scene->edit_nickname, 36, 320.0f,
                                      197.0f, WM_FONT_ALIGN_CENTER, scale,
                                      offset, ink, true)) return;
    }
    draw_text(scene, scene->edit_nickname, 320.0f, 197.0f, 36.0f,
              (WmColor){0.2f, 0.2f, 0.2f, 1.0f},
              1.0f, WM_FONT_ALIGN_CENTER);
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

static void draw_surface_background(WmSettingsScene *scene, float alpha) {
    /* BG_16x9's rows have no horizontal variation. Extend its RGB565 artwork
     * beneath the whole viewport so the gradient and rules remain continuous
     * while foreground pages move. The document's projection stays separate. */
    if (draw_image_raw(scene, "textures/settings_html/side-panel.png",
                       0.0f, 0.0f, WM_FRAME_WIDTH, WM_FRAME_HEIGHT,
                       (WmColor){1, 1, 1, alpha})) return;
    /* Retain the prepared GIF as a fallback for older local data. Its
     * rows also have no horizontal variation, before RGB565 quantization. */
    if (draw_image_raw(scene, "textures/settings_html/background.png",
                       0.0f, 0.0f, WM_FRAME_WIDTH, WM_FRAME_HEIGHT,
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
            WmQuad quad = {
                .x = 0.0f, .y = start + (end - start) * fraction,
                .width = WM_FRAME_WIDTH,
                .height = (end - start) / bands + 0.5f,
                .u1 = 1.0f, .v1 = 1.0f,
                .color = {shade, shade, shade, alpha * scene->draw_opacity}
            };
            wm_platform_draw_quad(scene->platform, &quad);
        }
    }
}

static void draw_background(WmSettingsScene *scene) {
    draw_black_background(scene);
    draw_surface_background(scene, appearance_opacity(scene));
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
        float focus = hover_enabled ? wm_settings_focus_opacity(scene, control) : 0.0f;
        bool format_row = page == 3 && item == 3;
        const char *focus_image = format_row
            ? "textures/settings_html/index-row-format-focus.png"
            : "textures/settings_html/index-row-focus.png";
        if (draw_image(scene, "textures/settings_html/index-row.png",
                       origin + 104.0f, y - 2.0f, 400.0f, 64.0f,
                       (WmColor){1, 1, 1, alpha})) {
            if (focus > 0.0f &&
                !draw_image(scene, focus_image,
                            origin + 104.0f, y, 400.0f, 60.0f,
                            (WmColor){1, 1, 1, alpha * focus})) {
                if (format_row)
                    draw_rectangle(scene, origin + 107.0f, y + 3.0f,
                                   394.0f, 54.0f,
                                   (WmColor){1, 242.0f / 255.0f, 0,
                                             alpha * focus * 77.0f / 255.0f});
                else
                    draw_button_focus(scene, origin + 104.0f, y,
                                      400.0f, 60.0f, alpha * focus);
            }
        } else {
            draw_button(scene, origin + 104.0f, y, 400.0f, 60.0f,
                        focus > 0.0f && !format_row, alpha);
            if (format_row && focus > 0.0f)
                draw_rectangle(scene, origin + 107.0f, y + 3.0f,
                               394.0f, 54.0f,
                               (WmColor){1, 242.0f / 255.0f, 0,
                                         alpha * focus * 77.0f / 255.0f});
        }
        draw_text(scene, index_labels[scene->language_choice][page - 1][item],
                  origin + 304.0f, y + 14.0f, 24.0f, dark, alpha,
                  WM_FONT_ALIGN_CENTER);
    }
    float back_focus = hover_enabled
        ? wm_settings_focus_opacity(scene, WM_SETTINGS_CONTROL_BACK) : 0.0f;
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
                       ? wm_settings_focus_opacity(scene, WM_SETTINGS_CONTROL_PREVIOUS)
                       : 0.0f,
                   alpha);
    if (page < SETTINGS_INDEX_PAGES)
        draw_arrow(scene, origin + 514.0f, 180.0f, true,
                   hover_enabled
                       ? wm_settings_focus_opacity(scene, WM_SETTINGS_CONTROL_NEXT)
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
    /* The action that advances Format uses red focus: right on the first
     * two pages, left on the final confirmation page. */
    bool format_action = scene->active_category == SETTINGS_FORMAT &&
                         scene->detail <= 2;
    const char *left_focus = format_action && scene->detail == 2
        ? red_focus : normal_focus;
    const char *right_focus = format_action && scene->detail < 2
        ? red_focus : normal_focus;
    float origin = 16.0f;
    if (left_label) {
        float focus = wm_settings_focus_opacity(scene, WM_SETTINGS_CONTROL_BACK);
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
        float focus = wm_settings_focus_opacity(scene, WM_SETTINGS_CONTROL_NEXT);
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
    float focus = wm_settings_focus_opacity(scene, control);
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

static void draw_centered_lines_sized(WmSettingsScene *scene,
                                      const char *const *lines,
                                      unsigned count, float first_y,
                                      float line_height, float font_height) {
    const WmColor white = {1, 1, 1, 1};
    for (unsigned line = 0; line < count; line++)
        draw_text(scene, lines[line], 320.0f,
                  first_y + line_height * line, font_height,
                  white, 1.0f, WM_FONT_ALIGN_CENTER);
}

static void draw_centered_lines(WmSettingsScene *scene,
                                const char *const *lines,
                                unsigned count, float first_y,
                                float line_height) {
    draw_centered_lines_sized(scene, lines, count, first_y, line_height,
                              23.0f);
}

static void draw_format_lines(WmSettingsScene *scene,
                              const char *const *lines, unsigned count) {
    const float line_height = 33.0f;
    const float middle_y = 198.0f;
    float first_y = middle_y - (float)(count - 1) * line_height * 0.5f;
    draw_centered_lines_sized(scene, lines, count, first_y,
                              line_height, 24.0f);
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
    unsigned first = wm_settings_country_page_start[page];
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
        float focus = wm_settings_focus_opacity(scene, control);
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
    /* Show local placeholders instead of host or console identifiers, and
     * dim the LAN address while no adapter service is available. */
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
            draw_rectangle(scene, SETTINGS_NICKNAME_FIELD_X,
                           SETTINGS_NICKNAME_FIELD_Y,
                           SETTINGS_NICKNAME_FIELD_WIDTH,
                           SETTINGS_NICKNAME_FIELD_HEIGHT,
                           (WmColor){1.0f, 1.0f, 1.0f, 1.0f});
            draw_nickname_text(scene);
            if (wm_settings_scene_editing_nickname(scene)) {
                draw_rectangle(scene, settings_scene_nickname_caret_x(scene),
                               SETTINGS_NICKNAME_FIELD_Y + 3.0f,
                               1.5f, SETTINGS_NICKNAME_FIELD_HEIGHT - 6.0f,
                               (WmColor){0.0f, 0.0f, 0.0f, 1.0f});
            }
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
                draw_format_lines(scene, format_intro, 7);
            else if (scene->detail == 1)
                draw_format_lines(scene, format_shop, 7);
            else if (scene->detail == 2)
                draw_format_lines(scene, format_final, 5);
            else
                draw_format_lines(scene, format_local, 2);
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
        float focus = wm_settings_focus_opacity(scene, control);
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
        float focus = wm_settings_focus_opacity(scene, control);
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
        float focus = wm_settings_focus_opacity(
            scene, (WmSettingsControl)(WM_SETTINGS_CONTROL_ITEM_1 + item));
        /* Split each 400×76 row into Connection and type fields. Local
         * profiles start unconfigured, so the type field reads None. */
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
        float focus = wm_settings_focus_opacity(scene,
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
               wm_settings_focus_opacity(scene, WM_SETTINGS_CONTROL_ITEM_1), 1.0f);
    draw_arrow(scene, 16.0f + 376.0f, 180.0f, true,
               wm_settings_focus_opacity(scene, WM_SETTINGS_CONTROL_ITEM_2), 1.0f);
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
        float focus = wm_settings_focus_opacity(scene, (WmSettingsControl)(
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
    /* The USA Format warning documents all place BnrWhite at x=56 and
     * BnrGray at x=24; advancing a warning does not create a nested tab. */
    bool nested = scene->detail != 0 &&
                  scene->active_category != SETTINGS_FORMAT;
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

static WmSettingsScene prior_page_view(const WmSettingsScene *scene) {
    const SettingsPageSnapshot *snapshot = &scene->prior_page;
    WmSettingsScene prior = {
        .platform = scene->platform,
        .textures = scene->textures,
        .fonts = scene->fonts,
        .font = scene->font,
        .outline_font = scene->outline_font,
        .scroll_layout = scene->scroll_layout,
        .phase = snapshot->phase,
        .hover = snapshot->hover,
        .page = snapshot->page,
        .previous_page = snapshot->previous_page,
        .active_category = snapshot->active_category,
        .detail = snapshot->detail,
        .selection = snapshot->selection,
        .language_choice = snapshot->language_choice,
        .edit_year = snapshot->edit_year,
        .edit_month = snapshot->edit_month,
        .edit_day = snapshot->edit_day,
        .edit_hour = snapshot->edit_hour,
        .edit_minute = snapshot->edit_minute,
        .edit_sensitivity = snapshot->edit_sensitivity,
        .connection_slot = snapshot->connection_slot,
        .country_page = snapshot->country_page,
        .edit_country_choice = snapshot->edit_country_choice,
        .connect24_enabled = snapshot->connect24_enabled,
        .sensitivity_instructions = snapshot->sensitivity_instructions,
        .wide = snapshot->wide,
        .direction = snapshot->direction,
        .phase_frame = snapshot->phase_frame,
        .page_frame = snapshot->page_frame,
        .nickname_keyboard_phase = snapshot->nickname_keyboard_phase,
        .nickname_caret = snapshot->nickname_caret,
        .draw_opacity = 1.0f
    };
    memcpy(prior.edit_nickname, snapshot->edit_nickname,
           sizeof(prior.edit_nickname));
    return prior;
}

bool wm_settings_scene_draw(WmSettingsScene *scene) {
    if (!scene || scene->phase == WM_SETTINGS_CLOSED) return false;
    WmSettingsProjection projection = wm_settings_scene_projection(scene);
    WmClipRect clip = {
        projection.document_x, 0.0f,
        projection.document_width, 456.0f
    };
    if (scene->page_crossfade && scene->phase == WM_SETTINGS_READY) {
        WmSettingsScene prior = prior_page_view(scene);
        draw_background(&prior);
        draw_page_content(&prior, &clip);
        scene->draw_opacity = wm_settings_page_opacity(scene);
        if (scene->draw_opacity > 0.0f) {
            /* Cover the previous foreground with the incoming page's fade,
             * keeping the continuous background at the same position. */
            draw_surface_background(scene, 1.0f);
            draw_page_content(scene, &clip);
        }
        scene->draw_opacity = 1.0f;
        settings_scene_draw_nickname_keyboard(scene);
        return true;
    }
    scene->draw_opacity = 1.0f;
    bool scrolling = scene->phase == WM_SETTINGS_SCROLL;
    draw_background(scene);
    WmClipRect frame_clip = {0.0f, 0.0f, WM_FRAME_WIDTH, WM_FRAME_HEIGHT};
    wm_platform_set_clip(scene->platform, scrolling ? &frame_clip : &clip);
    if (scene->active_category) {
        draw_category_page(scene);
    } else if (scrolling) {
        /* Only foreground content travels; the full-width backdrop stays
         * fixed beneath both pages throughout the scroll. */
        float page_width = scene->wide ? (float)SETTINGS_WIDE_WIDTH : 608.0f;
        SlideSample sample = scroll_sample(scene);
        float movement = sample.progress * page_width;
        float prior_offset = -scene->direction * movement;
        float next_offset = prior_offset + scene->direction * page_width;
        draw_index_page(scene, scene->previous_page, prior_offset, 1.0f,
                        false);
        draw_index_page(scene, scene->page, next_offset,
                        sample.incoming_alpha, false);
    } else {
        float alpha = appearance_opacity(scene);
        draw_index_page(scene, scene->page, 0.0f, alpha,
                        scene->phase == WM_SETTINGS_READY);
    }
    wm_platform_set_clip(scene->platform, NULL);
    settings_scene_draw_nickname_keyboard(scene);
    return true;
}
