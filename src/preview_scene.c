#define _POSIX_C_SOURCE 200809L

#include "wii_menu/preview_scene.h"

#include "wii_menu/arrow_interaction.h"
#include "wii_menu/layout_present.h"
#include "wii_menu/channel_animation.h"
#include "wii_menu/layout_runtime.h"
#include "wii_menu/material_prepare.h"
#include "wii_menu/font_cache.h"
#include "wii_menu/source_hit.h"
#include "wii_menu/texture_cache.h"
#include "wii_menu/ui.h"

#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

enum { WM_PREVIEW_PATH_CAPACITY = 4096 };

typedef struct PreviewFocus {
    float frame;
    bool active;
    bool entering;
} PreviewFocus;

struct WmPreviewScene {
    WmPlatform *platform;
    WmTextureCache *textures;
    WmFontCache *fonts;
    WmLayout *background;
    WmLayout *banner;
    WmLayout *title;
    WmLayout *arrows;
    WmLayout *channel_banners[WM_SLOT_COUNT];
    int date_year;
    int date_month;
    int date_day;
    WmHitType hovered_button;
    PreviewFocus focus[2];
    WmArrowInteraction arrows_interaction;
    bool preview_change_seen;
    int preview_change_from;
    int preview_change_to;
    bool preview_return_seen;
    float preview_return_frame;
    float arrow_entry_frames;
    float arrow_loop_frames;
    bool arrow_clock_started;
    float last_draw_seconds;
    float module_lead_frames;
    int prepared_slot;
};

typedef struct ArrowVisibility {
    bool previous;
    bool next;
} ArrowVisibility;

static WmLayout *load_layout(const char *directory, const char *relative_path)
{
    char path[WM_PREVIEW_PATH_CAPACITY];
    int length = snprintf(path, sizeof(path), "%s/%s", directory, relative_path);
    if (length < 0 || length >= (int)sizeof(path)) return NULL;
    char error[160] = {0};
    WmLayout *layout = wm_layout_load_json(path, error, sizeof(error));
    if (layout == NULL) {
        fprintf(stderr, "Could not load Disc preview layout %s: %s\n",
                relative_path, error);
    }
    return layout;
}

static bool pose_frame(WmLayout *layout, const char *animation, float frame)
{
    WmLayoutClip clip = {
        .animation = animation,
        .frame = frame,
        .loop_override = 0
    };
    return wm_layout_pose(layout, &clip, 1);
}

static void preload_banner_textures(WmPreviewScene *scene,
                                    const WmLayout *layout)
{
    if (!scene || !layout) return;
    /* The HTML renderer loads every channel texture before starting the menu.
     * A picture that first becomes visible mid-fade must not stall that frame
     * for image decoding or flash a fallback texture. */
    for (size_t index = 0; index < wm_layout_texture_count(layout); index++) {
        const WmLayoutTexture *resource = wm_layout_texture_at(layout, index);
        if (!resource || resource->missing || !resource->url[0]) continue;
        uint32_t handle = 0;
        (void)wm_texture_cache_resolve(scene->textures, resource->url, &handle);
    }
}

static bool selected_channel(const WmMenu *menu)
{
    return menu != NULL && menu->screen == WM_SCREEN_PREVIEW &&
           menu->selected >= 0 && menu->selected < WM_SLOT_COUNT &&
           menu->slots[menu->selected].occupied;
}

static bool selected_disc(const WmMenu *menu)
{
    return selected_channel(menu) &&
           strcmp(menu->slots[menu->selected].id, "disc") == 0;
}

static WmLayout *selected_banner(const WmPreviewScene *scene,
                                 const WmMenu *menu)
{
    if (!selected_channel(menu)) return NULL;
    return selected_disc(menu) ? scene->banner
                               : scene->channel_banners[menu->selected];
}

static WmLayout *banner_for_slot(const WmPreviewScene *scene,
                                 const WmMenu *menu, int slot)
{
    if (!scene || !menu || slot < 0 || slot >= WM_SLOT_COUNT ||
        !menu->slots[slot].occupied) return NULL;
    return strcmp(menu->slots[slot].id, "disc") == 0
               ? scene->banner : scene->channel_banners[slot];
}

bool wm_preview_scene_available(const WmPreviewScene *scene,
                                const WmMenu *menu)
{
    return scene && selected_banner(scene, menu) != NULL;
}

static bool has_neighbor_from(const WmMenu *menu, int slot, int direction)
{
    if (!menu || slot < 0 || slot >= WM_SLOT_COUNT) return false;
    for (int distance = 1; distance < WM_SLOT_COUNT; distance++) {
        int index = (slot + direction * distance +
                     WM_SLOT_COUNT * 2) % WM_SLOT_COUNT;
        if (menu->slots[index].occupied) return true;
    }
    return false;
}

static bool has_neighbor(const WmMenu *menu, int direction)
{
    return has_neighbor_from(menu, menu->selected, direction);
}

static bool hit_pane(const WmLayout *layout, const char *name, int x, int y) {
    WmSourceRect rect;
    return wm_source_pane_rect(layout, name, true, WM_LAYOUT_IPL,
                               NULL, &rect) &&
           (float)x >= rect.x && (float)x < rect.x + rect.width &&
           (float)y >= rect.y && (float)y < rect.y + rect.height;
}

static bool held_arrow_contains(const WmLayout *layout, const char *name,
                                 int x, int y) {
    WmSourceRect rect;
    const float margin = 4.0f;
    return wm_source_pane_rect(layout, name, true, WM_LAYOUT_IPL,
                               NULL, &rect) &&
           (float)x >= rect.x - margin &&
           (float)x < rect.x + rect.width + margin &&
           (float)y >= rect.y - margin &&
           (float)y < rect.y + rect.height + margin;
}

WmHit wm_preview_scene_hit(const WmPreviewScene *scene, const WmMenu *menu,
                            int x, int y) {
    const WmHit none = {WM_HIT_NONE, -1};
    if (!scene || !selected_banner(scene, menu)) return none;
    if (hit_pane(scene->title, "B_BtnA", x, y)) {
        return (WmHit){WM_HIT_BACK, -1};
    }
    if (!selected_disc(menu) && hit_pane(scene->title, "B_BtnB", x, y)) {
        return (WmHit){WM_HIT_START, -1};
    }
    if (has_neighbor(menu, -1) &&
        hit_pane(scene->arrows, "B_ArwL", x, y)) {
        return (WmHit){WM_HIT_PREVIEW_PREVIOUS, -1};
    }
    if (has_neighbor(menu, 1) &&
        hit_pane(scene->arrows, "B_ArwR", x, y)) {
        return (WmHit){WM_HIT_PREVIEW_NEXT, -1};
    }
    return none;
}

WmHit wm_preview_scene_hover_hit(const WmPreviewScene *scene,
                                  const WmMenu *menu, int x, int y,
                                  WmHit held) {
    if (scene && selected_banner(scene, menu)) {
        if (held.type == WM_HIT_PREVIEW_PREVIOUS &&
            held_arrow_contains(scene->arrows, "B_ArwL", x, y))
            return held;
        if (held.type == WM_HIT_PREVIEW_NEXT &&
            held_arrow_contains(scene->arrows, "B_ArwR", x, y))
            return held;
    }
    return wm_preview_scene_hit(scene, menu, x, y);
}

/* The preview uses the source's common footer arrow pose but excludes the
 * channel-grid buttons and dust. Absent neighboring channels hide arrow panes. */
static bool preview_arrow_pane(void *context, const WmLayoutPaneView *pane)
{
    const ArrowVisibility *visibility = context;
    const char *name = pane->name;
    if (strncmp(name, "N_BtnL_a", 8) == 0 ||
        strncmp(name, "N_BtnR_a", 8) == 0 ||
        strcmp(name, "N_Dust") == 0) {
        return false;
    }
    if (!visibility->previous && strcmp(name, "N_ArwL") == 0) return false;
    if (!visibility->next && strcmp(name, "N_ArwR") == 0) return false;
    return true;
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
        !pose_frame(scene->background, "my_IplTop_c", 0.0f)) {
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
    preload_banner_textures(scene, scene->banner);
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
                preload_banner_textures(scene, scene->channel_banners[slot]);
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
    if (scene && isfinite(frames) && frames >= 0.0f)
        scene->module_lead_frames = frames;
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

static void reset_preview_button_focus(WmPreviewScene *scene)
{
    scene->hovered_button = WM_HIT_NONE;
    memset(scene->focus, 0, sizeof(scene->focus));
}

static void reset_preview_feedback(WmPreviewScene *scene)
{
    reset_preview_button_focus(scene);
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

static void advance_arrow_clocks(WmPreviewScene *scene, float delta)
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

static void advance_focus(WmPreviewScene *scene, const WmMenu *menu,
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
    advance_arrow_clocks(scene, delta);
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

static bool pose_title(WmPreviewScene *scene, const WmMenu *menu,
                       WmPreviewPresentation presentation)
{
    bool enabled = strcmp(menu->slots[presentation.slot].id, "disc") != 0;
    int old_slot = menu->transition_from_selected;
    bool old_enabled = old_slot >= 0 && old_slot < WM_SLOT_COUNT &&
                       strcmp(menu->slots[old_slot].id, "disc") != 0;
    bool changing_button = presentation.phase == WM_PREVIEW_PHASE_CHANGE_OUT &&
                           enabled != old_enabled;
    WmLayoutClip clips[5] = {
        {
            .animation = changing_button && !enabled
                             ? "my_ChTop_a_OffBtn" : "my_ChTop_a_OnBtn",
            .frame = changing_button ? presentation.frame : enabled ? 10.0f : 0.0f,
            .group = "G_OnOffBtnB",
            .loop_override = 0
        },
        {
            .animation = "my_ChTop_a_OnBtn",
            .frame = 10.0f,
            .group = "G_OnOffBtnA",
            .loop_override = 0
        }
    };
    size_t count = 2;
    if (presentation.phase != WM_PREVIEW_PHASE_NORMAL) {
        clips[count++] = (WmLayoutClip){
            .animation = presentation.phase == WM_PREVIEW_PHASE_CHANGE_IN
                             ? "my_ChTop_a_ChangeIn" : "my_ChTop_a_ChangeOut",
            .frame = presentation.frame,
            .loop_override = 0,
            .target_name = "Change"
        };
    }
    static const char *const groups[] = {"G_FocusBtnA", "G_FocusBtnB"};
    for (size_t index = 0; index < 2; index++) {
        if (index == 1 && !enabled) continue;
        const PreviewFocus *focus = &scene->focus[index];
        clips[count++] = (WmLayoutClip){
            .animation = focus->active && focus->entering
                             ? "my_ChTop_a_FocusBtn_on"
                             : "my_ChTop_a_FocusBtnA_off",
            .frame = focus->active ? focus->frame : 10.0f,
            .group = groups[index],
            .loop_override = 0
        };
    }
    return wm_layout_pose(scene->title, clips, count);
}

static bool pose_arrows(WmLayout *layout, const WmPreviewScene *scene,
                        float frame, float loop_frame, bool exiting)
{
    if (!layout || !isfinite(frame) || frame < 0.0f ||
        !isfinite(loop_frame) || loop_frame < 0.0f) return false;
    const float loop = 10000.0f + fmodf(loop_frame, 55.0f);
    const float end = (exiting ? 10100.0f : 10150.0f) +
                      fminf(frame, 10.0f);
    WmLayoutClip clips[8] = {
        {.animation = "my_IplTop_e", .frame = 0.0f, .loop_override = 0},
        {.animation = "my_IplTop_e", .frame = loop,
         .group = "G_ArwRoop", .loop_override = 0},
        {.animation = "my_IplTop_e", .frame = end,
         .group = "G_ArwL_End", .loop_override = 0},
        {.animation = "my_IplTop_e", .frame = end,
         .group = "G_ArwR_End", .loop_override = 0}
    };
    size_t count = 4;
    if (scene) {
        static const char *const focus_groups[] = {
            "G_ArwL_Focus", "G_ArwR_Focus"
        };
        static const char *const press_groups[] = {
            "G_ArwL_Ac", "G_ArwR_Ac"
        };
        for (size_t index = 0; index < 2; index++) {
            const WmArrowSideState *feedback = wm_arrow_interaction_side(
                &scene->arrows_interaction, (int)index);
            clips[count++] = (WmLayoutClip){
                .animation = "my_IplTop_e",
                .frame = (feedback->focus_entering ? 10600.0f : 10800.0f) +
                         (feedback->focus_active
                              ? fminf(feedback->focus_age, 15.0f) : 0.0f),
                .group = focus_groups[index],
                .loop_override = 0
            };
        }
        for (size_t index = 0; index < 2; index++) {
            const WmArrowSideState *feedback = wm_arrow_interaction_side(
                &scene->arrows_interaction, (int)index);
            if (!feedback->pressed) continue;
            clips[count++] = (WmLayoutClip){
                .animation = "my_IplTop_e",
                .frame = 10700.0f + feedback->press_age,
                .group = press_groups[index],
                .loop_override = 0
            };
        }
    }
    return wm_layout_pose(layout, clips, count);
}

bool wm_preview_scene_pose_arrows(WmLayout *layout, float frame,
                                  float loop_frame, bool exiting)
{
    return pose_arrows(layout, NULL, frame, loop_frame, exiting);
}

static void update_background_date(WmPreviewScene *scene) {
    time_t now = time(NULL);
    struct tm date;
    if (!localtime_r(&now, &date) || date.tm_wday < 0 ||
        date.tm_wday >= 7) return;
    if (scene->date_year == date.tm_year &&
        scene->date_month == date.tm_mon &&
        scene->date_day == date.tm_mday) return;
    static const char *const weekdays[] = {
        "Sun", "Mon", "Tue", "Wed", "Thu", "Fri", "Sat"
    };
    char label[32];
    snprintf(label, sizeof(label), "%s %d/%d", weekdays[date.tm_wday],
             date.tm_mon + 1, date.tm_mday);
    wm_layout_set_text(scene->background, "T_Day_a", label);
    wm_layout_set_text(scene->background, "T_Day_b", label);
    wm_layout_set_text(scene->background, "T_Day_c", label);
    scene->date_year = date.tm_year;
    scene->date_month = date.tm_mon;
    scene->date_day = date.tm_mday;
}

static bool draw_preview_layers(WmPreviewScene *scene, const WmMenu *menu,
                                float preview_elapsed_seconds,
                                const float parent_matrix[12],
                                bool include_arrows)
{
    if (scene == NULL || menu == NULL ||
        !isfinite(preview_elapsed_seconds)) {
        return false;
    }
    WmPreviewPresentation presentation = wm_menu_preview_presentation(menu);
    if (menu->transition == WM_TRANSITION_BACK) {
        presentation.slot = menu->transition_from_selected;
        presentation.phase = WM_PREVIEW_PHASE_NORMAL;
    }
    WmLayout *banner = banner_for_slot(scene, menu, presentation.slot);
    if (!banner) return false;
    /* A large catalog may evict an earlier channel's startup images. Refresh
     * them in the retained zoom capture or at the start of a preview change,
     * before that banner's first visible fade frame. */
    if (menu->transition == WM_TRANSITION_SELECT ||
        scene->prepared_slot != presentation.slot) {
        preload_banner_textures(scene, banner);
        scene->prepared_slot = presentation.slot;
    }
    float updates = fmaxf(0.0f, preview_elapsed_seconds) * 60.0f;
    bool disc = strcmp(menu->slots[presentation.slot].id, "disc") == 0;
    /* A new selection captures the banner's initial pose for the entire
     * zoom. A lead retained from an earlier preview-channel change belongs
     * to that old preview and must not advance this new capture. */
    bool initial_zoom = menu->transition == WM_TRANSITION_SELECT;
    float banner_frame = updates;
    float module_frame = updates + scene->module_lead_frames;
    if (initial_zoom) {
        banner_frame = 0.0f;
        module_frame = 0.0f;
    } else if (presentation.phase == WM_PREVIEW_PHASE_CHANGE_OUT) {
        banner_frame = 0.0f;
        module_frame = presentation.frame;
    }
    const WmChannelAnimationOptions banner_options = {
        .language = "ENG",
        .has_base_frame = true,
        .base_frame = banner_frame,
        .measure_text = wm_font_cache_measure_text,
        .measure_context = scene->fonts
    };
    /* Empty Disc is both the active channel and the banner. Native
     * ChannelTitle::calcCommon advances its Start animation twice per tick. */
    if (!(disc ? pose_frame(banner, "my_DiskCh_a_Start", banner_frame * 2.0f)
                : wm_channel_animation_pose(
                      banner, menu->slots[presentation.slot].id,
                      WM_CHANNEL_BANNER, module_frame, &banner_options)) ||
        !pose_title(scene, menu, presentation) ||
        (include_arrows && !pose_arrows(
            scene->arrows, scene,
            scene->arrow_clock_started ? scene->arrow_entry_frames : updates,
            scene->arrow_clock_started ? scene->arrow_loop_frames : updates,
            false))) {
        return false;
    }
    update_background_date(scene);
    wm_layout_present_with_fonts(scene->platform, scene->textures, scene->fonts,
                                 scene->background, true, WM_LAYOUT_IPL,
                                 parent_matrix);
    wm_layout_present_with_fonts(scene->platform, scene->textures, scene->fonts,
                                 banner, true, WM_LAYOUT_IPL, parent_matrix);
    wm_layout_present_with_fonts(scene->platform, scene->textures, scene->fonts,
                                 scene->title, true, WM_LAYOUT_IPL, parent_matrix);
    if (include_arrows) {
        wm_arrow_interaction_set_visible(&scene->arrows_interaction,
                                          WM_ARROW_PREVIOUS,
                                          has_neighbor(menu, -1));
        wm_arrow_interaction_set_visible(&scene->arrows_interaction,
                                          WM_ARROW_NEXT,
                                          has_neighbor(menu, 1));
        ArrowVisibility arrows = {
            .previous = wm_arrow_interaction_side(
                &scene->arrows_interaction, WM_ARROW_PREVIOUS)->visible,
            .next = wm_arrow_interaction_side(
                &scene->arrows_interaction, WM_ARROW_NEXT)->visible
        };
        wm_layout_present_filtered_with_fonts(
            scene->platform, scene->textures, scene->fonts, scene->arrows,
            true, WM_LAYOUT_IPL, parent_matrix, preview_arrow_pane, &arrows);
    }
    return true;
}

bool wm_preview_scene_draw_layers(WmPreviewScene *scene, const WmMenu *menu,
                                   float preview_elapsed_seconds,
                                   const float parent_matrix[12])
{
    return draw_preview_layers(scene, menu, preview_elapsed_seconds,
                               parent_matrix, true);
}

bool wm_preview_scene_draw_capture(WmPreviewScene *scene, const WmMenu *menu,
                                   float preview_elapsed_seconds)
{
    /* HTML clears ChannelTitle button hover when Back starts, before its
     * first reverse-zoom capture. The common arrows finish separately. */
    if (scene && menu && menu->transition == WM_TRANSITION_BACK)
        reset_preview_button_focus(scene);
    return draw_preview_layers(scene, menu, preview_elapsed_seconds,
                               NULL, false);
}

bool wm_preview_scene_draw_return_arrows(WmPreviewScene *scene,
                                         const WmMenu *menu,
                                         float loop_elapsed_seconds,
                                         bool wide)
{
    if (!scene || !menu || menu->transition != WM_TRANSITION_BACK ||
        menu->transition_from_screen != WM_SCREEN_PREVIEW ||
        !isfinite(loop_elapsed_seconds) || loop_elapsed_seconds < 0.0f) {
        return false;
    }
    const float frame = wm_menu_transition_frame(menu);
    if (!scene->preview_return_seen) {
        scene->preview_return_seen = true;
        scene->preview_return_frame = 0.0f;
        wm_arrow_interaction_hover(&scene->arrows_interaction, -1);
    }
    advance_arrow_clocks(scene,
                         fmaxf(0.0f, frame - scene->preview_return_frame));
    scene->preview_return_frame = frame;
    if (frame > 10.0f ||
        !pose_arrows(scene->arrows, scene, frame,
                     loop_elapsed_seconds * 60.0f, true)) {
        return false;
    }
    const int slot = menu->transition_from_selected;
    ArrowVisibility arrows = {
        .previous = has_neighbor_from(menu, slot, -1),
        .next = has_neighbor_from(menu, slot, 1)
    };
    wm_layout_present_filtered_with_fonts(
        scene->platform, scene->textures, scene->fonts, scene->arrows,
        wide, WM_LAYOUT_IPL, NULL, preview_arrow_pane, &arrows);
    return true;
}

bool wm_preview_scene_draw(WmPreviewScene *scene, const WmMenu *menu,
                            float preview_elapsed_seconds, WmHit hover,
                            const WmPointer *pointer)
{
    if (!scene || !menu || !isfinite(preview_elapsed_seconds) ||
        !banner_for_slot(scene, menu,
                         wm_menu_preview_presentation(menu).slot)) return false;
    advance_focus(scene, menu, hover, preview_elapsed_seconds);
    wm_texture_cache_begin_frame(scene->textures);
    wm_font_cache_begin_frame(scene->fonts);
    wm_platform_begin(scene->platform, (WmColor){0.92f, 0.92f, 0.92f, 1.0f});
    bool drawn = wm_preview_scene_draw_layers(scene, menu,
                                               preview_elapsed_seconds, NULL);
    wm_ui_draw_notice(scene->platform, menu);
    wm_pointer_draw(pointer);
    wm_platform_end(scene->platform);
    return drawn;
}
