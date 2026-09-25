#include "wii_menu/health_scene.h"

#include "wii_menu/layout_present.h"
#include "wii_menu/material_prepare.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

enum {
    HEALTH_PATH_CAPACITY = 4096,
    HEALTH_LOCALE_CAPACITY = 32
};

struct WmHealthScene {
    WmPlatform *platform;
    WmTextureCache *textures;
    WmFontCache *fonts;
    WmLayout *layout;
    WmHealthPhase phase;
    float elapsed;
    float exit_start;
    float enter_frames;
    float exit_frames;
    char locale[HEALTH_LOCALE_CAPACITY];
};

static bool locale_available(const WmLayout *layout, const char *locale) {
    if (!locale || !locale[0]) return false;
    size_t length = strlen(locale);
    if (length > HEALTH_LOCALE_CAPACITY - 6) return false;
    char pane[HEALTH_LOCALE_CAPACITY + 5];
    int printed = snprintf(pane, sizeof(pane), "Has_%s", locale);
    if (printed < 0 || printed >= (int)sizeof(pane)) return false;
    WmLayoutPaneState state;
    if (!wm_layout_pane_state(layout, pane, &state)) return false;
    printed = snprintf(pane, sizeof(pane), "Push_%s", locale);
    return printed >= 0 && printed < (int)sizeof(pane) &&
           wm_layout_pane_state(layout, pane, &state);
}

WmHealthScene *wm_health_scene_create(WmPlatform *platform,
                                        const char *assets_directory,
                                        WmTextureCache *textures,
                                        WmFontCache *fonts,
                                        bool enabled,
                                        const char *locale) {
    if (!platform || !assets_directory || !assets_directory[0] ||
        !textures || !fonts) return NULL;
    char path[HEALTH_PATH_CAPACITY];
    int length = snprintf(path, sizeof(path), "%s/layouts/health/it_Has_a.json",
                          assets_directory);
    if (length < 0 || length >= (int)sizeof(path)) return NULL;
    char error[160] = {0};
    WmLayout *layout = wm_layout_load_json(path, error, sizeof(error));
    if (!layout) {
        fprintf(stderr, "Could not load Health and Safety layout: %s\n", error);
        return NULL;
    }
    WmLayoutAnimationInfo enter;
    WmLayoutAnimationInfo exit;
    WmLayoutAnimationInfo push;
    if (!wm_layout_animation_info(layout, "it_Has_a_SeenIn", &enter) ||
        !wm_layout_animation_info(layout, "it_Has_a_SeenOut", &exit) ||
        !wm_layout_animation_info(layout, "it_Has_a_Push", &push) ||
        enter.frames <= 0.0f || exit.frames <= 0.0f || push.frames <= 0.0f) {
        wm_layout_destroy(layout);
        return NULL;
    }
    WmHealthScene *health = calloc(1, sizeof(*health));
    if (!health) {
        wm_layout_destroy(layout);
        return NULL;
    }
    health->platform = platform;
    health->textures = textures;
    health->fonts = fonts;
    health->layout = layout;
    health->enter_frames = enter.frames;
    health->exit_frames = exit.frames;
    const char *selected = locale_available(layout, locale) ? locale : "US_ENG";
    if (!locale_available(layout, selected)) {
        wm_health_scene_destroy(health);
        return NULL;
    }
    snprintf(health->locale, sizeof(health->locale), "%s", selected);
    wm_layout_prepare_materials(platform, layout);
    wm_health_scene_reset(health, enabled);
    return health;
}

void wm_health_scene_destroy(WmHealthScene *health) {
    if (!health) return;
    wm_layout_destroy(health->layout);
    free(health);
}

void wm_health_scene_reset(WmHealthScene *health, bool enabled) {
    if (!health) return;
    health->phase = enabled ? WM_HEALTH_ENTER : WM_HEALTH_DONE;
    health->elapsed = 0.0f;
    health->exit_start = 0.0f;
}

bool wm_health_scene_accept(WmHealthScene *health) {
    if (!health || health->phase != WM_HEALTH_WAIT) return false;
    health->phase = WM_HEALTH_LEAVE;
    health->exit_start = health->elapsed;
    return true;
}

void wm_health_scene_advance(WmHealthScene *health, float frames) {
    if (!health || !isfinite(frames) || frames <= 0.0f) return;
    health->elapsed += frames;
    if (health->phase == WM_HEALTH_ENTER &&
        health->elapsed >= health->enter_frames + 60.0f) {
        health->phase = WM_HEALTH_WAIT;
    }
    if (health->phase == WM_HEALTH_WAIT &&
        health->elapsed >= health->enter_frames + 60.0f + 3600.0f) {
        wm_health_scene_accept(health);
    }
    if (health->phase == WM_HEALTH_LEAVE &&
        health->elapsed - health->exit_start >= health->exit_frames) {
        health->phase = WM_HEALTH_DONE;
    }
}

bool wm_health_scene_active(const WmHealthScene *health) {
    return health && health->phase != WM_HEALTH_DONE;
}

bool wm_health_scene_ready(const WmHealthScene *health) {
    return health && health->phase == WM_HEALTH_WAIT;
}

WmHealthPhase wm_health_scene_phase(const WmHealthScene *health) {
    return health ? health->phase : WM_HEALTH_DONE;
}

float wm_health_scene_elapsed(const WmHealthScene *health) {
    return health ? health->elapsed : 0.0f;
}

typedef struct LocaleContext {
    WmHealthScene *health;
    bool show_push;
} LocaleContext;

static bool select_locale_pane(void *opaque, const WmLayoutPaneView *pane) {
    LocaleContext *context = opaque;
    const char *prefix = NULL;
    bool show = false;
    if (strncmp(pane->name, "Has_", 4) == 0) {
        prefix = "Has_";
        show = true;
    } else if (strncmp(pane->name, "Push_", 5) == 0) {
        prefix = "Push_";
        show = context->show_push;
    }
    if (prefix) {
        show = show && strcmp(pane->name + strlen(prefix),
                              context->health->locale) == 0;
        wm_layout_set_pane_visible(context->health->layout, pane->name, show);
    }
    return true;
}

bool wm_health_scene_pose(WmHealthScene *health) {
    if (!health) return false;
    WmLayoutClip clips[3] = {
        {
            .animation = "it_Has_a_SeenIn",
            .group = "G_All",
            .frame = health->elapsed,
            .loop_override = 0
        }
    };
    size_t count = 1;
    if (health->phase == WM_HEALTH_WAIT) {
        clips[count++] = (WmLayoutClip){
            .animation = "it_Has_a_Push",
            .group = "G_Push",
            .frame = health->elapsed - health->enter_frames - 60.0f,
            .loop_override = 1
        };
    }
    if (health->phase == WM_HEALTH_LEAVE ||
        (health->phase == WM_HEALTH_DONE && health->exit_start > 0.0f)) {
        clips[count++] = (WmLayoutClip){
            .animation = "it_Has_a_SeenOut",
            .group = "G_All",
            .frame = health->elapsed - health->exit_start,
            .loop_override = 0
        };
    }
    if (!wm_layout_pose(health->layout, clips, count)) return false;
    LocaleContext context = {
        .health = health,
        .show_push = health->phase == WM_HEALTH_WAIT ||
                     health->phase == WM_HEALTH_LEAVE
    };
    wm_layout_visit_all_transforms(health->layout, true, WM_LAYOUT_IPL,
                                    NULL, select_locale_pane, &context);
    return true;
}

void wm_health_scene_draw(WmHealthScene *health) {
    if (!wm_health_scene_pose(health)) return;
    wm_layout_present_with_fonts(health->platform, health->textures,
                                 health->fonts, health->layout, true,
                                 WM_LAYOUT_IPL, NULL);
}

const WmLayout *wm_health_scene_layout(const WmHealthScene *health) {
    return health ? health->layout : NULL;
}
