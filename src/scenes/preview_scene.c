#include "preview_scene_internal.h"

#include "wii_menu/layout/layout_assets.h"
#include "wii_menu/layout/layout_runtime.h"
#include "wii_menu/render/material_prepare.h"
#include "wii_menu/render/texture_cache.h"

#include <math.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

static WmLayout *load_layout(const char *directory, const char *relative_path) {
    return wm_layout_load_asset(directory, relative_path, "Disc preview");
}

void preview_scene_preload_banner_textures(WmPreviewScene *scene,
                                    const WmLayout *layout)
{
    if (!scene || !layout) return;
    /* Preload channel textures before entry. A picture that first becomes
     * visible mid-fade must not stall for decoding or flash a fallback. */
    for (size_t index = 0; index < wm_layout_texture_count(layout); index++) {
        const WmLayoutTexture *resource = wm_layout_texture_at(layout, index);
        if (!resource || resource->missing || !resource->url[0]) continue;
        uint32_t handle = 0;
        (void)wm_texture_cache_resolve(scene->textures, resource->url, &handle);
    }
}

WmPreviewScene *wm_preview_scene_create(WmPlatform *platform,
                                         const char *assets_directory,
                                         const WmMenu *menu,
                                         WmTextureCache *textures,
                                         WmFontCache *fonts)
{
    if (platform == NULL || assets_directory == NULL ||
        assets_directory[0] == '\0' || textures == NULL || fonts == NULL) {
        return NULL;
    }
    WmPreviewScene *scene = calloc(1, sizeof(*scene));
    if (scene == NULL) return NULL;
    scene->platform = platform;
    scene->textures = textures;
    scene->fonts = fonts;
    scene->date_year = -1;
    scene->date_month = -1;
    scene->date_day = -1;
    scene->prepared_slot = -1;
    wm_arrow_interaction_init(&scene->arrows_interaction,
        (WmArrowInteractionConfig){
            .focus_in_frames = 15.0f,
            .focus_out_frames = 15.0f,
            .press_frames = 30.0f,
            .visibility_frames = 10.0f
        });
    scene->background = load_layout(assets_directory,
                                    "layouts/board/my_IplTop_c.json");
    scene->banner = load_layout(assets_directory,
                                "layouts/diskBann/my_DiskCh_a.json");
    scene->title = load_layout(assets_directory,
                               "layouts/chanTtl/my_ChTop_a.json");
    scene->arrows = load_layout(assets_directory,
                                "layouts/cmnBtn/my_IplTop_e.json");
    if (scene->background == NULL || scene->banner == NULL ||
        scene->title == NULL || scene->arrows == NULL ||
        !preview_scene_pose_frame(scene->background, "my_IplTop_c", 0.0f)) {
        wm_preview_scene_destroy(scene);
        return NULL;
    }
    wm_layout_set_text(scene->banner, "T_Bar", "Disc Channel");
    wm_layout_set_text(scene->banner, "T_Comment0", "Please insert a disc.");
    wm_layout_set_text(scene->banner, "T_Comment1", "");
    wm_layout_set_text(scene->title, "T_BtnA", "Wii Menu");
    wm_layout_set_text(scene->title, "T_BtnB", "Start");
    static const char *const inactive_footer_text[] = {
        "T_BbsMark1", "T_CalAdd_R", "T_CalExit", "T_Add", "T_Dust"
    };
    for (size_t index = 0; index <
         sizeof(inactive_footer_text) / sizeof(inactive_footer_text[0]); index++) {
        wm_layout_set_text(scene->arrows, inactive_footer_text[index], "");
    }
    wm_layout_prepare_materials(platform, scene->background);
    wm_layout_prepare_materials(platform, scene->banner);
    preview_scene_preload_banner_textures(scene, scene->banner);
    wm_texture_cache_begin_frame(scene->textures);
    wm_layout_prepare_materials(platform, scene->title);
    wm_layout_prepare_materials(platform, scene->arrows);
    if (menu) {
        for (int slot = 1; slot < WM_SLOT_COUNT; slot++) {
            const char *path = menu->slots[slot].banner_layout;
            if (!menu->slots[slot].occupied || !path[0]) continue;
            scene->channel_banners[slot] = load_layout(assets_directory, path);
            if (scene->channel_banners[slot]) {
                wm_layout_prepare_materials(platform,
                                             scene->channel_banners[slot]);
                preview_scene_preload_banner_textures(scene, scene->channel_banners[slot]);
                wm_texture_cache_begin_frame(scene->textures);
            }
        }
    }
    return scene;
}

void wm_preview_scene_destroy(WmPreviewScene *scene)
{
    if (scene == NULL) return;
    wm_layout_destroy(scene->background);
    wm_layout_destroy(scene->banner);
    wm_layout_destroy(scene->title);
    wm_layout_destroy(scene->arrows);
    for (int slot = 1; slot < WM_SLOT_COUNT; slot++) {
        wm_layout_destroy(scene->channel_banners[slot]);
    }
    free(scene);
}

void wm_preview_scene_move_channel(WmPreviewScene *scene, int from, int to)
{
    if (!scene || from < 0 || from >= WM_SLOT_COUNT ||
        to < 0 || to >= WM_SLOT_COUNT || from == to) return;
    WmLayout *layout = scene->channel_banners[from];
    scene->channel_banners[from] = scene->channel_banners[to];
    scene->channel_banners[to] = layout;
}

void wm_preview_scene_set_module_lead(WmPreviewScene *scene, float frames)
{
    if (scene && isfinite(frames) && frames >= 0.0f) {
        scene->module_lead_frames = frames;
        /* The caller assigns this at each settled preview entry, even when
         * no zoom frame was rendered or the same channel was reopened. */
        scene->shop_clock_started = false;
    }
}

static int focus_button(WmHitType hit)
{
    if (hit == WM_HIT_BACK) return 0;
    if (hit == WM_HIT_START) return 1;
    return -1;
}

static int focus_arrow(WmHitType hit)
{
    if (hit == WM_HIT_PREVIEW_PREVIOUS) return 0;
    if (hit == WM_HIT_PREVIEW_NEXT) return 1;
    return -1;
}

void preview_scene_reset_button_focus(WmPreviewScene *scene)
{
    scene->hovered_button = WM_HIT_NONE;
    memset(scene->focus, 0, sizeof(scene->focus));
}

static void reset_preview_feedback(WmPreviewScene *scene)
{
    preview_scene_reset_button_focus(scene);
    wm_arrow_interaction_reset_feedback(&scene->arrows_interaction);
    scene->preview_change_seen = false;
    scene->preview_change_from = -1;
    scene->preview_change_to = -1;
    scene->arrow_entry_frames = 0.0f;
    scene->arrow_loop_frames = 0.0f;
    scene->arrow_clock_started = false;
}

static bool new_preview_change(const WmPreviewScene *scene, const WmMenu *menu)
{
    return menu->transition == WM_TRANSITION_PREVIEW &&
           (!scene->preview_change_seen ||
            scene->preview_change_from != menu->transition_from_selected ||
            scene->preview_change_to != menu->selected);
}

void preview_scene_advance_arrow_clocks(WmPreviewScene *scene, float delta)
{
    wm_arrow_interaction_advance(&scene->arrows_interaction, delta);
}

static void advance_arrow_feedback(WmPreviewScene *scene, const WmMenu *menu,
                                   WmHit hover)
{
    int hovered = focus_arrow(hover.type);
    wm_arrow_interaction_hover(&scene->arrows_interaction, hovered);

    if (menu->transition == WM_TRANSITION_PREVIEW) {
        float transition_frame = wm_menu_transition_frame(menu);
        if (new_preview_change(scene, menu)) {
            int pressed = menu->transition_direction < 0 ? 0 : 1;
            wm_arrow_interaction_press(&scene->arrows_interaction,
                                       pressed, transition_frame);
            scene->preview_change_from = menu->transition_from_selected;
            scene->preview_change_to = menu->selected;
        } else {
            int pressed = menu->transition_direction < 0 ? 0 : 1;
            wm_arrow_interaction_min_press_age(&scene->arrows_interaction,
                                                pressed, transition_frame);
        }
        scene->preview_change_seen = true;
    } else {
        scene->preview_change_seen = false;
    }
}

void preview_scene_advance_focus(WmPreviewScene *scene, const WmMenu *menu,
                          WmHit hover, float seconds)
{
    if (scene->preview_return_seen) {
        reset_preview_feedback(scene);
        scene->preview_return_seen = false;
        scene->last_draw_seconds = seconds;
    }
    /* A channel change restarts the banner clock, but the common arrow's
     * 30-frame press continues across its 20-frame preview transition. */
    bool finishing_change = scene->preview_change_seen &&
                            (menu->transition == WM_TRANSITION_NONE ||
                             new_preview_change(scene, menu));
    if (seconds < scene->last_draw_seconds && !finishing_change)
        reset_preview_feedback(scene);
    float delta = fmaxf(0.0f, seconds - scene->last_draw_seconds) * 60.0f;
    scene->last_draw_seconds = seconds;
    /* Channel banners restart their clock after a preview change. The common
     * arrows enter only once and retain their separate looping motion. */
    if (scene->arrow_clock_started) {
        scene->arrow_entry_frames = fminf(10.0f,
                                          scene->arrow_entry_frames + delta);
        scene->arrow_loop_frames = fmodf(scene->arrow_loop_frames + delta,
                                         55.0f);
    } else {
        scene->arrow_entry_frames = fminf(10.0f, seconds * 60.0f);
        scene->arrow_loop_frames = fmodf(seconds * 60.0f, 55.0f);
        scene->arrow_clock_started = true;
    }
    preview_scene_advance_arrow_clocks(scene, delta);
    if (finishing_change) {
        /* A second click can begin in the same event batch as the previous
         * change's completion, with no settled preview draw in between. */
        for (size_t index = 0; index < 2; index++) {
            wm_arrow_interaction_min_press_age(&scene->arrows_interaction,
                                                (int)index, 20.0f);
        }
    }
    advance_arrow_feedback(scene, menu, hover);
    int old_button = focus_button(scene->hovered_button);
    int new_button = focus_button(hover.type);
    if (old_button != new_button) {
        if (old_button >= 0) {
            PreviewFocus *old = &scene->focus[old_button];
            float frame = old->active && old->entering
                              ? 8.0f - fminf(5.0f, old->frame) * 8.0f / 5.0f
                              : 8.0f;
            *old = (PreviewFocus){ .frame = frame, .active = true };
        }
        if (new_button >= 0) {
            PreviewFocus *next = &scene->focus[new_button];
            float frame = next->active && !next->entering
                              ? 5.0f * (1.0f - fminf(8.0f, next->frame) / 8.0f)
                              : 0.0f;
            *next = (PreviewFocus){
                .frame = frame, .active = true, .entering = true
            };
        }
        scene->hovered_button = hover.type;
    }
    for (size_t index = 0; index < 2; index++) {
        PreviewFocus *focus = &scene->focus[index];
        if (!focus->active) continue;
        focus->frame += delta;
        if (focus->entering) focus->frame = fminf(10.0f, focus->frame);
        else if (focus->frame >= 10.0f) focus->active = false;
    }
}
