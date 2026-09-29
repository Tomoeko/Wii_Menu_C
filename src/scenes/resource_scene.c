#include "resource_scene_internal.h"
#include "wii_menu/layout/layout_assets.h"

#include "wii_menu/input/source_hit.h"
#include "wii_menu/render/material_prepare.h"

#include <math.h>
#include <stdlib.h>
#include <string.h>

static WmLayout *load_layout(const char *root, const char *relative) {
    return wm_layout_load_asset(root, relative, NULL);
}

static bool prepare_widescreen_grid(WmLayout *grid) {
    static const char *const pictures[] = {"Picture_00", "Picture_01", "Picture_02",
                                           "Picture_03", "Picture_04"};
    static const char *const edges[] = {"Edge0", "Edge1", "Edge2", "Edge3", "Edge4"};
    for (size_t index = 0; index < 5; index++) {
        if (!wm_layout_copy_texture_map(grid, "ChangeTex16x9", pictures[index], 0) ||
            !wm_layout_copy_texture_map(grid, "Picture_16", edges[index], 0)) {
            return false;
        }
    }
    return true;
}

WmResourceScene *wm_resource_scene_create(WmPlatform *platform,
                                          const char *assets_directory,
                                          const WmMenu *menu, WmTextureCache *textures,
                                          WmFontCache *fonts) {
    if (!platform || !assets_directory || !assets_directory[0] || !textures || !fonts)
        return NULL;
    WmResourceScene *scene = calloc(1, sizeof(*scene));
    if (!scene)
        return NULL;
    scene->platform = platform;
    scene->textures = textures;
    scene->fonts = fonts;
    scene->background = load_layout(assets_directory, "layouts/board/my_IplTop_c.json");
    scene->grid = load_layout(assets_directory, "layouts/chanSel/my_IplTop_a.json");
    scene->empty_channel =
        load_layout(assets_directory, "layouts/chanSel/my_IplTop_b.json");
    scene->disc_channel =
        load_layout(assets_directory, "layouts/diskThum/my_DiskCh_b.json");
    scene->footer = load_layout(assets_directory, "layouts/cmnBtn/my_IplTop_e.json");
    scene->sd_button =
        load_layout(assets_directory, "layouts/cmnBtn/mn_Sdcard_Btn.json");
    scene->clock = load_layout(assets_directory, "layouts/chanSel/my_Clock_a.json");
    scene->focus = load_layout(assets_directory, "layouts/chanSel/my_IplTop_d.json");
    scene->balloon =
        load_layout(assets_directory, "layouts/balloon/my_IplTopBalloon_a.json");
    scene->hovered_slot = -1;
    scene->hovered_footer = -1;
    scene->balloon_target = -1;
    scene->dismissed_balloon = -1;
    if (scene->balloon) {
        WmLayoutAnimationInfo info;
        if (wm_layout_animation_info(scene->balloon, "my_IplTopBalloon_a_BalloonInOut",
                                     &info)) {
            scene->balloon_end = fmaxf(0.0f, info.frames - 1.0f);
        }
    }
    scene->capture_slot = -1;
    scene->capture_date = -1;
    scene->capture_hover_slot = -1;
    scene->previous_page = menu ? menu->page : 0;
    scene->arrow_visible[0] = scene->previous_page > 0;
    scene->arrow_visible[1] = scene->previous_page < WM_PAGE_COUNT - 1;
    scene->arrow_age[0] = 10.0f;
    scene->arrow_age[1] = 10.0f;
    scene->arrow_press_age[0] = -1.0f;
    scene->arrow_press_age[1] = -1.0f;
    scene->sd_hover_changed_at = -INFINITY;
    scene->clock_hour = -1;
    scene->clock_minute = -1;
    scene->clock_change_start = -1.0f;
    scene->date_year = -1;
    scene->date_month = -1;
    scene->date_day = -1;
    uint32_t sample_texture = 0;
    if (!scene->background || !scene->grid || !scene->empty_channel ||
        !scene->disc_channel || !scene->footer || !scene->sd_button || !scene->clock ||
        !scene->focus || !scene->balloon ||
        !wm_texture_cache_resolve(scene->textures, "textures/chanSel/my_TVSheet_b.png",
                                  &sample_texture) ||
        !prepare_widescreen_grid(scene->grid)) {
        wm_resource_scene_destroy(scene);
        return NULL;
    }
    WmLayoutAnimationInfo clock_animation;
    if (wm_layout_animation_info(scene->clock, "my_Clock_a_Change", &clock_animation)) {
        scene->clock_change_frames = clock_animation.frames;
    }
    if (wm_layout_animation_info(scene->clock, "my_Clock_a_Min", &clock_animation)) {
        scene->clock_blink_frames = clock_animation.frames;
    }
    static const char *const inactive_footer_text[] = {"T_BbsMark1", "T_CalAdd_R",
                                                       "T_CalExit", "T_Add", "T_Dust"};
    for (size_t index = 0;
         index < sizeof(inactive_footer_text) / sizeof(inactive_footer_text[0]);
         index++) {
        wm_layout_set_text(scene->footer, inactive_footer_text[index], "");
    }
    wm_layout_set_text(scene->clock, "T_WiiMenu", "Wii Menu");
    wm_layout_prepare_materials(platform, scene->background);
    wm_layout_prepare_materials(platform, scene->grid);
    wm_layout_prepare_materials(platform, scene->empty_channel);
    wm_layout_prepare_materials(platform, scene->disc_channel);
    wm_layout_prepare_materials(platform, scene->footer);
    wm_layout_prepare_materials(platform, scene->sd_button);
    wm_layout_prepare_materials(platform, scene->clock);
    wm_layout_prepare_materials(platform, scene->focus);
    wm_layout_prepare_materials(platform, scene->balloon);
    if (menu) {
        for (int slot = 1; slot < WM_SLOT_COUNT; slot++) {
            const char *path = menu->slots[slot].icon_layout;
            if (!menu->slots[slot].occupied || !path[0])
                continue;
            WmLayout *icon = load_layout(assets_directory, path);
            if (!icon)
                continue;
            scene->channel_icons[slot] = icon;
            const char *leaf = strrchr(path, '/');
            leaf = leaf ? leaf + 1 : path;
            size_t length = strlen(leaf);
            if (length >= 5 && strcmp(leaf + length - 5, ".json") == 0) {
                length -= 5;
            }
            if (length >= sizeof(scene->channel_animations[slot])) {
                length = sizeof(scene->channel_animations[slot]) - 1;
            }
            memcpy(scene->channel_animations[slot], leaf, length);
            scene->channel_animations[slot][length] = '\0';
            wm_layout_prepare_materials(platform, icon);
        }
    }
    return scene;
}

void wm_resource_scene_destroy(WmResourceScene *scene) {
    if (!scene)
        return;
    if (scene->preview_capture) {
        wm_platform_destroy_texture(scene->platform, scene->preview_capture);
    }
    wm_layout_destroy(scene->background);
    wm_layout_destroy(scene->grid);
    wm_layout_destroy(scene->empty_channel);
    wm_layout_destroy(scene->disc_channel);
    wm_layout_destroy(scene->footer);
    wm_layout_destroy(scene->sd_button);
    wm_layout_destroy(scene->clock);
    wm_layout_destroy(scene->focus);
    wm_layout_destroy(scene->balloon);
    for (int slot = 1; slot < WM_SLOT_COUNT; slot++) {
        wm_layout_destroy(scene->channel_icons[slot]);
    }
    free(scene);
}

static bool hit_layout_pane(const WmLayout *layout, const char *pane_name,
                            const float parent_matrix[12], int x, int y) {
    WmSourceRect rect;
    return wm_source_pane_rect(layout, pane_name, true, WM_LAYOUT_IPL, parent_matrix,
                               &rect) &&
           (float)x >= rect.x && (float)x < rect.x + rect.width && (float)y >= rect.y &&
           (float)y < rect.y + rect.height;
}

static bool hit_layout_pane_margin(const WmLayout *layout, const char *pane_name, int x,
                                   int y, float margin) {
    WmSourceRect rect;
    return wm_source_pane_rect(layout, pane_name, true, WM_LAYOUT_IPL, NULL, &rect) &&
           (float)x >= rect.x - margin && (float)x <= rect.x + rect.width + margin &&
           (float)y >= rect.y - margin && (float)y <= rect.y + rect.height + margin;
}

WmHit wm_resource_scene_hit(const WmResourceScene *scene, const WmMenu *menu, int x,
                            int y) {
    const WmHit none = {WM_HIT_NONE, -1};
    if (!scene || !menu)
        return none;
    /* The animated arrow hit pane drifts by nearly three logical pixels.
     * Hold an existing focus bubble through that motion and page locking. */
    if (scene->hovered_footer == 2 && menu->page > 0 &&
        hit_layout_pane_margin(scene->footer, "B_ArwL", x, y, 4.0f)) {
        return (WmHit){WM_HIT_PAGE_PREVIOUS, -1};
    }
    if (scene->hovered_footer == 3 && menu->page < WM_PAGE_COUNT - 1 &&
        hit_layout_pane_margin(scene->footer, "B_ArwR", x, y, 4.0f)) {
        return (WmHit){WM_HIT_PAGE_NEXT, -1};
    }
    if (menu->page > 0 && hit_layout_pane(scene->footer, "B_ArwL", NULL, x, y)) {
        return (WmHit){WM_HIT_PAGE_PREVIOUS, -1};
    }
    if (menu->page < WM_PAGE_COUNT - 1 &&
        hit_layout_pane(scene->footer, "B_ArwR", NULL, x, y)) {
        return (WmHit){WM_HIT_PAGE_NEXT, -1};
    }
    WmHit channel = wm_source_menu_hit(scene->grid, menu, x, y);
    if (channel.type == WM_HIT_CHANNEL)
        return channel;
    if (hit_layout_pane(scene->footer, "B_Set", NULL, x, y)) {
        return (WmHit){WM_HIT_SETTINGS, -1};
    }
    if (hit_layout_pane(scene->footer, "B_Bbs", NULL, x, y)) {
        return (WmHit){WM_HIT_BOARD, -1};
    }
    if (hit_layout_pane(scene->sd_button, "Ac", wm_resource_scene_sd_position, x, y)) {
        return (WmHit){WM_HIT_SD, -1};
    }
    return none;
}

int wm_resource_scene_slot_at(const WmResourceScene *scene, const WmMenu *menu, int x,
                              int y) {
    if (!scene)
        return -1;
    return wm_source_menu_slot_at(scene->grid, menu, x, y);
}

void wm_resource_scene_press_arrow(WmResourceScene *scene, int direction) {
    if (!scene || (direction != -1 && direction != 1))
        return;
    scene->arrow_press_age[direction < 0 ? 0 : 1] = 0.0f;
}

void wm_resource_scene_move_channel(WmResourceScene *scene, int from, int to) {
    if (!scene || from < 0 || from >= WM_SLOT_COUNT || to < 0 || to >= WM_SLOT_COUNT ||
        from == to)
        return;
    WmLayout *layout = scene->channel_icons[from];
    scene->channel_icons[from] = scene->channel_icons[to];
    scene->channel_icons[to] = layout;
    char animation[sizeof(scene->channel_animations[0])];
    memcpy(animation, scene->channel_animations[from], sizeof(animation));
    memcpy(scene->channel_animations[from], scene->channel_animations[to],
           sizeof(animation));
    memcpy(scene->channel_animations[to], animation, sizeof(animation));
    scene->capture_valid = false;
    scene->capture_hover_slot = -1;
}

void wm_resource_scene_release_preview_capture(WmResourceScene *scene) {
    if (!scene)
        return;
    if (scene->preview_capture)
        wm_platform_destroy_texture(scene->platform, scene->preview_capture);
    scene->preview_capture = 0;
    scene->capture_valid = false;
    scene->capture_slot = -1;
    scene->capture_date = -1;
    scene->capture_hover_slot = -1;
}

void wm_resource_scene_restart(WmResourceScene *scene) {
    if (!scene)
        return;
    wm_resource_scene_release_preview_capture(scene);
    memset(scene->focus_states, 0, sizeof(scene->focus_states));
    memset(scene->footer_states, 0, sizeof(scene->footer_states));
    memset(scene->balloons, 0, sizeof(scene->balloons));
    scene->hovered_slot = -1;
    scene->hovered_footer = -1;
    scene->balloon_target = -1;
    scene->dismissed_balloon = -1;
    scene->fade_dismissed_balloon = false;
    scene->balloon_sound_pending = false;
    scene->last_draw_seconds = 0.0f;
    scene->previous_page = 0;
    scene->arrow_visible[0] = false;
    scene->arrow_visible[1] = true;
    scene->arrow_age[0] = 10.0f;
    scene->arrow_age[1] = 10.0f;
    scene->arrow_press_age[0] = -1.0f;
    scene->arrow_press_age[1] = -1.0f;
    scene->sd_hovered = false;
    scene->sd_hover_changed_at = -INFINITY;
    scene->clock_hour = -1;
    scene->clock_minute = -1;
    scene->clock_change_start = -1.0f;
    scene->date_year = -1;
    scene->date_month = -1;
    scene->date_day = -1;
    scene->message_badge_count = 0;
    scene->new_mail_active = false;
    scene->new_mail_sound_pending = false;
    scene->new_mail_age = 0.0f;
    wm_layout_set_text(scene->footer, "T_BbsMark1", "");
    wm_layout_set_text(scene->clock, "T_WiiMenu", "Wii Menu");
}
