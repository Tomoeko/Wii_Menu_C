#define _POSIX_C_SOURCE 200809L

#include "wii_menu/resource_scene.h"

#include "wii_menu/layout_present.h"
#include "wii_menu/channel_animation.h"
#include "wii_menu/channel_drag.h"
#include "wii_menu/layout_runtime.h"
#include "wii_menu/material_prepare.h"
#include "wii_menu/menu_transition.h"
#include "wii_menu/font_cache.h"
#include "wii_menu/pointer.h"
#include "wii_menu/preview_scene.h"
#include "wii_menu/source_hit.h"
#include "wii_menu/texture_cache.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

enum { WM_SCENE_PATH_CAPACITY = 4096 };

typedef struct FocusAnimation {
    bool active;
    bool leaving;
    bool off_phase;
    float frame;
} FocusAnimation;

typedef struct HoverAnimation {
    bool active;
    bool entering;
    float frame;
} HoverAnimation;

typedef enum BalloonPhase {
    BALLOON_NONE,
    BALLOON_WAIT,
    BALLOON_ENTER,
    BALLOON_HOLD,
    BALLOON_LEAVE
} BalloonPhase;

typedef struct BalloonAnimation {
    BalloonPhase phase;
    float frame;
    bool leaving;
} BalloonAnimation;

enum {
    BALLOON_SETTINGS = WM_SLOT_COUNT,
    BALLOON_BOARD,
    BALLOON_SD,
    BALLOON_BOARD_BACK,
    BALLOON_CALENDAR,
    BALLOON_CREATE,
    BALLOON_COUNT
};

struct WmResourceScene {
    WmPlatform *platform;
    WmTextureCache *textures;
    WmFontCache *fonts;
    WmLayout *background;
    WmLayout *grid;
    WmLayout *empty_channel;
    WmLayout *disc_channel;
    WmLayout *footer;
    WmLayout *sd_button;
    WmLayout *clock;
    WmLayout *focus;
    WmLayout *balloon;
    uint32_t preview_capture;
    int capture_slot;
    int capture_date;
    WmChannel capture_channel;
    bool capture_valid;
    int capture_hover_slot;
    float capture_hover_started;
    WmLayout *channel_icons[WM_SLOT_COUNT];
    char channel_animations[WM_SLOT_COUNT][128];
    FocusAnimation focus_states[WM_SLOT_COUNT];
    HoverAnimation footer_states[4];
    int hovered_slot;
    int hovered_footer;
    int previous_page;
    float last_draw_seconds;
    float arrow_age[2];
    bool arrow_visible[2];
    float arrow_press_age[2];
    bool sd_hovered;
    BalloonAnimation balloons[BALLOON_COUNT];
    int balloon_target;
    int dismissed_balloon;
    bool fade_dismissed_balloon;
    float balloon_end;
    bool balloon_sound_pending;
    float sd_hover_changed_at;
    float board_sd_reveal_started_at;
    int clock_hour;
    int clock_minute;
    float clock_change_start;
    float clock_change_frames;
    float clock_blink_frames;
    int date_year;
    int date_month;
    int date_day;
    unsigned message_badge_count;
    bool new_mail_active;
    bool new_mail_sound_pending;
    float new_mail_age;
};

typedef struct GridTraversal {
    WmGridTile tiles[5][WM_CHANNELS_PER_PAGE];
    bool has_tile[5][WM_CHANNELS_PER_PAGE];
    float clock_anchors[3][12];
    bool has_clock_anchor[3];
    int page;
    bool masks_only;
    float zoom_scale_x;
    float zoom_scale_y;
} GridTraversal;

static WmLayout *load_layout(const char *assets_directory,
                             const char *relative_path) {
    char path[WM_SCENE_PATH_CAPACITY];
    int length = snprintf(path, sizeof(path), "%s/%s",
                          assets_directory, relative_path);
    if (length < 0 || length >= (int)sizeof(path)) return NULL;
    char error[160];
    return wm_layout_load_json(path, error, sizeof(error));
}

static bool prepare_widescreen_grid(WmLayout *grid) {
    static const char *const pictures[] = {
        "Picture_00", "Picture_01", "Picture_02", "Picture_03", "Picture_04"
    };
    static const char *const edges[] = {
        "Edge0", "Edge1", "Edge2", "Edge3", "Edge4"
    };
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
                                           const WmMenu *menu,
                                           WmTextureCache *textures,
                                           WmFontCache *fonts) {
    if (!platform || !assets_directory || !assets_directory[0] ||
        !textures || !fonts) return NULL;
    WmResourceScene *scene = calloc(1, sizeof(*scene));
    if (!scene) return NULL;
    scene->platform = platform;
    scene->textures = textures;
    scene->fonts = fonts;
    scene->background = load_layout(assets_directory,
                                    "layouts/board/my_IplTop_c.json");
    scene->grid = load_layout(assets_directory,
                              "layouts/chanSel/my_IplTop_a.json");
    scene->empty_channel = load_layout(assets_directory,
                                       "layouts/chanSel/my_IplTop_b.json");
    scene->disc_channel = load_layout(assets_directory,
                                      "layouts/diskThum/my_DiskCh_b.json");
    scene->footer = load_layout(assets_directory,
                                 "layouts/cmnBtn/my_IplTop_e.json");
    scene->sd_button = load_layout(assets_directory,
                                    "layouts/cmnBtn/mn_Sdcard_Btn.json");
    scene->clock = load_layout(assets_directory,
                                "layouts/chanSel/my_Clock_a.json");
    scene->focus = load_layout(assets_directory,
                               "layouts/chanSel/my_IplTop_d.json");
    scene->balloon = load_layout(assets_directory,
                                 "layouts/balloon/my_IplTopBalloon_a.json");
    scene->hovered_slot = -1;
    scene->hovered_footer = -1;
    scene->balloon_target = -1;
    scene->dismissed_balloon = -1;
    if (scene->balloon) {
        WmLayoutAnimationInfo info;
        if (wm_layout_animation_info(scene->balloon,
              "my_IplTopBalloon_a_BalloonInOut", &info)) {
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
    scene->board_sd_reveal_started_at = -1.0f;
    scene->clock_hour = -1;
    scene->clock_minute = -1;
    scene->clock_change_start = -1.0f;
    scene->date_year = -1;
    scene->date_month = -1;
    scene->date_day = -1;
    uint32_t sample_texture = 0;
    if (!scene->background || !scene->grid || !scene->empty_channel ||
        !scene->disc_channel || !scene->footer || !scene->sd_button ||
        !scene->clock || !scene->focus || !scene->balloon ||
        !wm_texture_cache_resolve(scene->textures,
                                  "textures/chanSel/my_TVSheet_b.png",
                                  &sample_texture) ||
        !prepare_widescreen_grid(scene->grid)) {
        wm_resource_scene_destroy(scene);
        return NULL;
    }
    WmLayoutAnimationInfo clock_animation;
    if (wm_layout_animation_info(scene->clock, "my_Clock_a_Change",
                                 &clock_animation)) {
        scene->clock_change_frames = clock_animation.frames;
    }
    if (wm_layout_animation_info(scene->clock, "my_Clock_a_Min",
                                 &clock_animation)) {
        scene->clock_blink_frames = clock_animation.frames;
    }
    static const char *const inactive_footer_text[] = {
        "T_BbsMark1", "T_CalAdd_R", "T_CalExit", "T_Add", "T_Dust"
    };
    for (size_t index = 0; index <
         sizeof(inactive_footer_text) / sizeof(inactive_footer_text[0]); index++) {
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
            if (!menu->slots[slot].occupied || !path[0]) continue;
            WmLayout *icon = load_layout(assets_directory, path);
            if (!icon) continue;
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
    if (!scene) return;
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

static bool collect_clock_anchor(void *context,
                                 const WmLayoutPaneView *pane) {
    GridTraversal *traversal = context;
    const char *name = pane->name;
    if (strncmp(name, "N_Clock", 7) == 0 &&
        name[7] >= '0' && name[7] <= '2' && name[8] == '\0') {
        int index = name[7] - '0';
        memcpy(traversal->clock_anchors[index], pane->matrix,
               sizeof(traversal->clock_anchors[index]));
        traversal->has_clock_anchor[index] = true;
    }
    return true;
}

static bool grid_pane(void *context, const WmLayoutPaneView *pane) {
    GridTraversal *traversal = context;
    const char *name = pane->name;
    if (strncmp(name, "N_Ch_", 5) == 0 &&
        name[5] >= 'a' && name[5] <= 'e' &&
        name[6] >= '0' && name[6] <= '9' &&
        name[7] >= '0' && name[7] <= '9' && name[8] == '\0') {
        int group = name[5] - 'a';
        int slot = (name[6] - '0') * 10 + (name[7] - '0') - 1;
        if (slot >= 0 && slot < WM_CHANNELS_PER_PAGE) {
            /* The source clips icons to 170 x 96 logical pixels at 16:9,
             * centered on the anchor; that is narrower than pane bounds. */
            const float scale = (float)WM_FRAME_WIDTH / 832.0f;
            const float width = 170.0f * scale * traversal->zoom_scale_x;
            const float center_x = WM_FRAME_WIDTH * 0.5f +
                                   pane->matrix[3] * scale;
            const float center_y = WM_FRAME_HEIGHT * 0.5f - pane->matrix[7];
            WmGridTile *tile = &traversal->tiles[group][slot];
            tile->page_offset = group - 2;
            memcpy(tile->matrix, pane->matrix, sizeof(tile->matrix));
            tile->clip = (WmClipRect){
                center_x - width * 0.5f,
                center_y - 48.0f * traversal->zoom_scale_y,
                width, 96.0f * traversal->zoom_scale_y
            };
            traversal->has_tile[group][slot] = true;
        }
    }
    if (strcmp(pane->type, "pic1") != 0) return true;
    bool base_mask = strncmp(name, "BaseMask", 8) == 0;
    if (traversal->masks_only) return base_mask;
    if (base_mask || strcmp(name, "ChMask") == 0) return false;
    if (strncmp(name, "Edge", 4) == 0 &&
        name[4] >= '0' && name[4] <= '4' && name[5] == '\0') {
        int relative = (name[4] - '0') - 2;
        if (traversal->page + relative < 0 ||
            traversal->page + relative >= WM_PAGE_COUNT) return false;
    }
    return true;
}

static size_t visible_grid_tiles(const GridTraversal *traversal,
                                 WmGridTile *tiles, size_t capacity) {
    size_t count = 0;
    for (int group = 0; group < 5; group++) {
        int page = traversal->page + group - 2;
        if (page < 0 || page >= WM_PAGE_COUNT) continue;
        for (int index = 0; index < WM_CHANNELS_PER_PAGE; index++) {
            if (!traversal->has_tile[group][index]) continue;
            WmGridTile tile = traversal->tiles[group][index];
            if (tile.clip.x + tile.clip.width <= 0.0f ||
                tile.clip.x >= WM_FRAME_WIDTH ||
                tile.clip.y + tile.clip.height <= 0.0f ||
                tile.clip.y >= WM_FRAME_HEIGHT) continue;
            tile.slot = page * WM_CHANNELS_PER_PAGE + index;
            if (count < capacity && tiles) tiles[count] = tile;
            count++;
        }
    }
    return count;
}

size_t wm_resource_scene_collect_tiles(const WmLayout *grid, int page,
                                       WmGridTile *tiles, size_t capacity) {
    if (!grid || page < 0 || page >= WM_PAGE_COUNT) return 0;
    GridTraversal traversal = {
        .page = page, .masks_only = true,
        .zoom_scale_x = 1.0f, .zoom_scale_y = 1.0f
    };
    WmLayoutDrawOptions options = {
        .wide = true,
        .mode = WM_LAYOUT_IPL,
        .alpha = 1.0f,
        .on_pane = grid_pane,
        .context = &traversal
    };
    wm_layout_draw(grid, &options);
    return visible_grid_tiles(&traversal, tiles, capacity);
}

static void pose_layout(WmLayout *layout, const char *animation, float frame) {
    WmLayoutClip clip = {
        .animation = animation,
        .frame = frame,
        .loop_override = -1
    };
    wm_layout_pose(layout, &clip, 1);
}

static bool sd_button_pane(void *context, const WmLayoutPaneView *pane) {
    (void)context;
    return strcmp(pane->name, "N_Btn_Off") != 0;
}

/* The 16:9 IPL projection places the SD button at X=-245. The 4:3
 * position is X=-152; applying it here moves the button toward the center. */
static const float sd_position[12] = {
    1, 0, 0, -245,
    0, 1, 0, -172,
    0, 0, 1, 0
};

static bool hit_layout_pane(const WmLayout *layout, const char *pane_name,
                            const float parent_matrix[12], int x, int y) {
    WmSourceRect rect;
    return wm_source_pane_rect(layout, pane_name, true, WM_LAYOUT_IPL,
                               parent_matrix, &rect) &&
           (float)x >= rect.x && (float)x < rect.x + rect.width &&
           (float)y >= rect.y && (float)y < rect.y + rect.height;
}

static bool hit_layout_pane_margin(const WmLayout *layout,
                                   const char *pane_name, int x, int y,
                                   float margin) {
    WmSourceRect rect;
    return wm_source_pane_rect(layout, pane_name, true, WM_LAYOUT_IPL,
                               NULL, &rect) &&
           (float)x >= rect.x - margin &&
           (float)x <= rect.x + rect.width + margin &&
           (float)y >= rect.y - margin &&
           (float)y <= rect.y + rect.height + margin;
}

WmHit wm_resource_scene_hit(const WmResourceScene *scene, const WmMenu *menu,
                            int x, int y) {
    const WmHit none = {WM_HIT_NONE, -1};
    if (!scene || !menu) return none;
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
    if (menu->page > 0 &&
        hit_layout_pane(scene->footer, "B_ArwL", NULL, x, y)) {
        return (WmHit){WM_HIT_PAGE_PREVIOUS, -1};
    }
    if (menu->page < WM_PAGE_COUNT - 1 &&
        hit_layout_pane(scene->footer, "B_ArwR", NULL, x, y)) {
        return (WmHit){WM_HIT_PAGE_NEXT, -1};
    }
    WmHit channel = wm_source_menu_hit(scene->grid, menu, x, y);
    if (channel.type == WM_HIT_CHANNEL) return channel;
    if (hit_layout_pane(scene->footer, "B_Set", NULL, x, y)) {
        return (WmHit){WM_HIT_SETTINGS, -1};
    }
    if (hit_layout_pane(scene->footer, "B_Bbs", NULL, x, y)) {
        return (WmHit){WM_HIT_BOARD, -1};
    }
    if (hit_layout_pane(scene->sd_button, "Ac", sd_position, x, y)) {
        return (WmHit){WM_HIT_SD, -1};
    }
    return none;
}

int wm_resource_scene_slot_at(const WmResourceScene *scene,
                              const WmMenu *menu, int x, int y) {
    if (!scene) return -1;
    return wm_source_menu_slot_at(scene->grid, menu, x, y);
}

void wm_resource_scene_press_arrow(WmResourceScene *scene, int direction) {
    if (!scene || (direction != -1 && direction != 1)) return;
    scene->arrow_press_age[direction < 0 ? 0 : 1] = 0.0f;
}

void wm_resource_scene_move_channel(WmResourceScene *scene, int from, int to) {
    if (!scene || from < 0 || from >= WM_SLOT_COUNT ||
        to < 0 || to >= WM_SLOT_COUNT || from == to) return;
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

static bool clock_pane(void *context, const WmLayoutPaneView *pane) {
    int hour = *(const int *)context;
    if (strncmp(pane->name, "Num", 3) == 0 ||
        strcmp(pane->name, "AM") == 0 ||
        strcmp(pane->name, "PM") == 0 ||
        strcmp(pane->name, "AM_PM") == 0) return false;
    if (strcmp(pane->name, "Clock3") == 0 && hour < 10) return false;
    return true;
}

static void update_clock_textures(WmResourceScene *scene, int hour,
                                  int minute, bool afternoon) {
    if (scene->clock_hour == hour && scene->clock_minute == minute) return;
    static const char *const target[] = {
        "Clock0", "Clock1", "Clock2", "Clock3"
    };
    const int digits[] = {
        minute % 10, minute / 10, hour % 10, hour / 10
    };
    char donor[16];
    for (size_t index = 0; index < 4; index++) {
        snprintf(donor, sizeof(donor), "Num%d", digits[index]);
        wm_layout_copy_texture_map(scene->clock, donor, target[index], 0);
    }
    wm_layout_copy_texture_map(scene->clock, afternoon ? "PM" : "AM",
                               "AM_PM_R", 0);
    scene->clock_hour = hour;
    scene->clock_minute = minute;
}

static void update_background_date(WmResourceScene *scene,
                                   const struct tm *date) {
    if (scene->date_year == date->tm_year &&
        scene->date_month == date->tm_mon &&
        scene->date_day == date->tm_mday) return;
    static const char *const weekdays[] = {
        "Sun", "Mon", "Tue", "Wed", "Thu", "Fri", "Sat"
    };
    if (date->tm_wday < 0 || date->tm_wday >= 7) return;
    char label[32];
    snprintf(label, sizeof(label), "%s %d/%d", weekdays[date->tm_wday],
             date->tm_mon + 1, date->tm_mday);
    wm_layout_set_text(scene->background, "T_Day_a", label);
    wm_layout_set_text(scene->background, "T_Day_b", label);
    wm_layout_set_text(scene->background, "T_Day_c", label);
    scene->date_year = date->tm_year;
    scene->date_month = date->tm_mon;
    scene->date_day = date->tm_mday;
}

static int footer_hover_index(WmHit hover) {
    switch (hover.type) {
        case WM_HIT_SETTINGS: return 0;
        case WM_HIT_BOARD: return 1;
        case WM_HIT_PAGE_PREVIOUS: return 2;
        case WM_HIT_PAGE_NEXT: return 3;
        default: return -1;
    }
}

static void balloon_target(WmResourceScene *scene, int target) {
    if (target == scene->balloon_target) return;
    int old = scene->balloon_target;
    if (old >= 0) {
        BalloonAnimation *state = &scene->balloons[old];
        if (state->phase == BALLOON_WAIT) state->phase = BALLOON_NONE;
        else if (old >= WM_SLOT_COUNT) state->phase = BALLOON_LEAVE;
        else state->leaving = true;
    }
    scene->balloon_target = target;
    if (target >= 0) {
        BalloonAnimation *state = &scene->balloons[target];
        if (target < WM_SLOT_COUNT && state->phase != BALLOON_NONE) {
            /* ChannelObj lets an existing bubble finish its exit when the
             * pointer returns, then starts a fresh wait. Replacing it with
             * WAIT here makes the visible bubble disappear in one frame. */
            state->leaving = false;
        } else {
            *state = (BalloonAnimation){
                .phase = BALLOON_WAIT, .frame = 0.0f
            };
        }
    }
}

void wm_resource_scene_dismiss_balloon(WmResourceScene *scene) {
    if (!scene || scene->balloon_target < 0) return;
    scene->dismissed_balloon = scene->balloon_target;
    scene->fade_dismissed_balloon = false;
    balloon_target(scene, -1);
}

void wm_resource_scene_retire_footer_focus(WmResourceScene *scene) {
    if (!scene) return;
    memset(scene->footer_states, 0, sizeof(scene->footer_states));
    scene->hovered_footer = -1;
}

void wm_resource_scene_pointer_moved(WmResourceScene *scene) {
    if (!scene || !scene->fade_dismissed_balloon) return;
    scene->dismissed_balloon = -1;
    scene->fade_dismissed_balloon = false;
}

static int available_balloon_target(WmResourceScene *scene, int target) {
    if (target == scene->dismissed_balloon) return -1;
    scene->dismissed_balloon = -1;
    scene->fade_dismissed_balloon = false;
    return target;
}

static void advance_balloons(WmResourceScene *scene, float frames) {
    for (int index = 0; index < BALLOON_COUNT; index++) {
        BalloonAnimation *state = &scene->balloons[index];
        if (index < WM_SLOT_COUNT) {
            if (state->phase == BALLOON_NONE) continue;
            state->frame += frames;
            const float duration = scene->balloon_end + 1.0f;
            switch (state->phase) {
                case BALLOON_WAIT:
                    if (state->frame + 0.0001f >= 20.0f) {
                        state->frame = fmaxf(0.0f, state->frame - 20.0f);
                        state->phase = BALLOON_ENTER;
                        scene->balloon_sound_pending = true;
                    }
                    break;
                case BALLOON_ENTER:
                    if (state->frame >= duration) {
                        state->frame = duration;
                        state->phase = BALLOON_HOLD;
                    }
                    break;
                case BALLOON_HOLD:
                    if (state->leaving) {
                        state->frame = 0.0f;
                        state->phase = BALLOON_LEAVE;
                    }
                    break;
                case BALLOON_LEAVE:
                    if (state->frame >= duration) {
                        *state = index == scene->balloon_target
                            ? (BalloonAnimation){.phase = BALLOON_WAIT}
                            : (BalloonAnimation){0};
                    }
                    break;
                case BALLOON_NONE: break;
            }
            continue;
        }
        switch (state->phase) {
            case BALLOON_WAIT:
                state->frame += frames;
                /* Seconds arrive as float from the frame clock. Treat a
                 * tiny rounding shortfall as the authored update boundary
                 * so 17 successive 60 Hz samples cannot become 18. */
                if (state->frame + 0.0001f >= 17.0f) {
                    state->frame = fmaxf(0.0f, state->frame - 17.0f);
                    state->phase = BALLOON_ENTER;
                    scene->balloon_sound_pending = true;
                }
                break;
            case BALLOON_ENTER:
                state->frame = fminf(scene->balloon_end,
                                      state->frame + frames);
                if (state->frame >= scene->balloon_end) {
                    state->phase = state->leaving ? BALLOON_LEAVE : BALLOON_HOLD;
                }
                break;
            case BALLOON_HOLD:
                if (state->leaving) state->phase = BALLOON_LEAVE;
                break;
            case BALLOON_LEAVE:
                state->frame = fmaxf(0.0f, state->frame - frames);
                if (state->frame == 0.0f) state->phase = BALLOON_NONE;
                break;
            case BALLOON_NONE: break;
        }
    }
}

bool wm_resource_scene_take_balloon_sound(WmResourceScene *scene) {
    if (!scene) return false;
    bool pending = scene->balloon_sound_pending;
    scene->balloon_sound_pending = false;
    return pending;
}

void wm_resource_scene_set_message_badge(WmResourceScene *scene,
                                         unsigned today_count,
                                         bool new_mail) {
    if (!scene) return;
    unsigned count = today_count > 99 ? 99 : today_count;
    if (count != scene->message_badge_count) {
        char label[4] = {0};
        if (count) snprintf(label, sizeof(label), "%u", count);
        wm_layout_set_text(scene->footer, "T_BbsMark1", label);
        scene->message_badge_count = count;
    }
    bool active = count > 0 && new_mail;
    if (active != scene->new_mail_active) {
        scene->new_mail_active = active;
        scene->new_mail_age = 0.0f;
        if (active) scene->new_mail_sound_pending = true;
        else scene->new_mail_sound_pending = false;
    }
}

bool wm_resource_scene_take_new_mail_sound(WmResourceScene *scene) {
    if (!scene) return false;
    bool pending = scene->new_mail_sound_pending;
    scene->new_mail_sound_pending = false;
    return pending;
}

static void advance_interactions(WmResourceScene *scene, const WmMenu *menu,
                                 float elapsed_seconds, WmHit hover,
                                 bool suppress_balloons) {
    float frames = fmaxf(0.0f, elapsed_seconds - scene->last_draw_seconds) * 60.0f;
    scene->last_draw_seconds = elapsed_seconds;
    if (scene->new_mail_active && frames > 0.0f) {
        scene->new_mail_age += frames;
        if (scene->new_mail_age >= 180.0f) {
            scene->new_mail_age = fmodf(scene->new_mail_age, 180.0f);
            scene->new_mail_sound_pending = true;
        }
    }

    int slot = hover.type == WM_HIT_CHANNEL ? hover.slot : -1;
    if (slot < 0 || slot >= WM_SLOT_COUNT ||
        menu->transition != WM_TRANSITION_NONE) slot = -1;
    if (slot != scene->hovered_slot) {
        if (scene->hovered_slot >= 0) {
            scene->focus_states[scene->hovered_slot].leaving = true;
        }
        scene->hovered_slot = slot;
        if (slot >= 0) {
            FocusAnimation *focus = &scene->focus_states[slot];
            if (!focus->active || focus->off_phase) {
                *focus = (FocusAnimation){.active = true};
            } else {
                focus->leaving = false;
            }
        }
    }
    int bubble = slot;
    if (bubble < 0 && hover.type == WM_HIT_SETTINGS)
        bubble = BALLOON_SETTINGS;
    if (bubble < 0 && hover.type == WM_HIT_BOARD)
        bubble = BALLOON_BOARD;
    if (bubble < 0 && hover.type == WM_HIT_SD)
        bubble = BALLOON_SD;
    if (suppress_balloons) {
        memset(scene->balloons, 0, sizeof(scene->balloons));
        scene->balloon_target = -1;
        scene->dismissed_balloon = bubble;
        scene->fade_dismissed_balloon = bubble >= 0;
        scene->balloon_sound_pending = false;
    } else {
        balloon_target(scene, available_balloon_target(scene, bubble));
        advance_balloons(scene, frames);
    }
    for (int index = 0; index < WM_SLOT_COUNT; index++) {
        FocusAnimation *focus = &scene->focus_states[index];
        if (!focus->active) continue;
        focus->frame += frames;
        if (!focus->off_phase && focus->frame >= 5.0f && focus->leaving) {
            focus->off_phase = true;
            focus->frame = 0.0f;
        }
        if (focus->off_phase && focus->frame >= 30.0f) {
            focus->active = false;
        }
    }

    int footer = footer_hover_index(hover);
    if (footer != scene->hovered_footer) {
        if (scene->hovered_footer >= 0) {
            scene->footer_states[scene->hovered_footer] =
                (HoverAnimation){.active = true, .entering = false};
        }
        scene->hovered_footer = footer;
        if (footer >= 0) {
            scene->footer_states[footer] =
                (HoverAnimation){.active = true, .entering = true};
        }
    }
    for (size_t index = 0; index < 4; index++) {
        if (scene->footer_states[index].active) {
            scene->footer_states[index].frame += frames;
        }
    }

    bool sd_hovered = hover.type == WM_HIT_SD;
    if (scene->sd_hovered != sd_hovered) {
        scene->sd_hovered = sd_hovered;
        scene->sd_hover_changed_at = elapsed_seconds * 60.0f;
    }

    if (menu->page != scene->previous_page) {
        int direction = menu->page > scene->previous_page ? 1 : -1;
        scene->arrow_press_age[direction > 0 ? 1 : 0] = 0.0f;
        scene->previous_page = menu->page;
    }
    bool zooming = menu->transition == WM_TRANSITION_SELECT ||
                   menu->transition == WM_TRANSITION_BACK;
    bool visible[] = {
        !zooming && menu->page > 0,
        !zooming && menu->page < WM_PAGE_COUNT - 1
    };
    for (size_t index = 0; index < 2; index++) {
        if (scene->arrow_visible[index] != visible[index]) {
            scene->arrow_visible[index] = visible[index];
            scene->arrow_age[index] = 0.0f;
        }
    }
    for (size_t index = 0; index < 2; index++) {
        scene->arrow_age[index] += frames;
        if (scene->arrow_press_age[index] >= 0.0f) {
            scene->arrow_press_age[index] += frames;
            if (scene->arrow_press_age[index] > 30.0f) {
                scene->arrow_press_age[index] = -1.0f;
            }
        }
    }
}

static bool channel_mask_pane(void *context, const WmLayoutPaneView *pane) {
    (void)context;
    return strcmp(pane->name, "RootPane") == 0 ||
           strcmp(pane->name, "ChMask") == 0;
}

static bool footer_without_arrows(void *context,
                                  const WmLayoutPaneView *pane) {
    (void)context;
    return strcmp(pane->name, "N_ArwL") != 0 &&
           strcmp(pane->name, "N_ArwR") != 0;
}

static bool footer_arrows_only(void *context,
                               const WmLayoutPaneView *pane) {
    (void)context;
    const char *name = pane->name;
    return strcmp(name, "RootPane") == 0 ||
           strcmp(name, "N_TopBtn") == 0 ||
           strcmp(name, "N_BtnL") == 0 ||
           strcmp(name, "N_BtnR") == 0 ||
           strncmp(name, "N_Arw", 5) == 0 ||
           strncmp(name, "Arw", 3) == 0 ||
           strncmp(name, "Taba", 4) == 0 ||
           strncmp(name, "B_Arw", 5) == 0;
}

typedef struct BalloonAnchorSearch {
    const char *pane_name;
    float x;
    float y;
    bool found;
} BalloonAnchorSearch;

static bool collect_balloon_anchor(void *context,
                                   const WmLayoutPaneView *pane) {
    BalloonAnchorSearch *search = context;
    if (strcmp(pane->name, search->pane_name) == 0) {
        search->x = pane->matrix[3];
        search->y = pane->matrix[7];
        search->found = true;
    }
    return true;
}

static bool footer_balloon_anchor(const WmLayout *layout,
                                  const char *pane_name,
                                  const float parent_matrix[12],
                                  float *x, float *y) {
    BalloonAnchorSearch search = {.pane_name = pane_name};
    WmLayoutDrawOptions options = {
        .wide = true,
        .mode = WM_LAYOUT_IPL,
        .alpha = 1.0f,
        .parent_matrix = parent_matrix,
        .on_pane = collect_balloon_anchor,
        .context = &search
    };
    wm_layout_draw(layout, &options);
    if (!search.found) return false;
    *x = search.x;
    *y = search.y;
    return true;
}

static void draw_balloons(WmResourceScene *scene, const WmMenu *menu,
                          const WmBoardScene *board) {
    static const char *const titles[] = {
        "Wii Options", "Wii Message Board", "SD Card Menu",
        "Wii Menu", "Calendar", "Create Message"
    };
    static const char *const footer_panes[] = {"B_Set", "B_Bbs", "Ac"};
    for (int index = 0; index < BALLOON_COUNT; index++) {
        if (board ? index < BALLOON_BOARD_BACK :
                    index >= BALLOON_BOARD_BACK) continue;
        BalloonAnimation *state = &scene->balloons[index];
        if (state->phase != BALLOON_ENTER && state->phase != BALLOON_HOLD &&
            state->phase != BALLOON_LEAVE) continue;
        const char *original = index < WM_SLOT_COUNT
            ? menu->slots[index].title : titles[index - WM_SLOT_COUNT];
        if (index < WM_SLOT_COUNT &&
            (!menu || !menu->slots[index].occupied)) continue;
        char label[160];
        snprintf(label, sizeof(label), "%s", original);
        WmLayoutClip clip = {
            .animation = "my_IplTopBalloon_a_BalloonInOut",
            .frame = index < WM_SLOT_COUNT && state->phase == BALLOON_LEAVE
                ? scene->balloon_end + 1.0f - state->frame
                : state->frame,
            .loop_override = 0
        };
        if (!wm_layout_pose(scene->balloon, &clip, 1)) continue;
        WmLayoutPaneState text_pane;
        if (!wm_layout_pane_state(scene->balloon, "T_Balloon", &text_pane))
            continue;
        float text_width = wm_font_cache_measure_text(
            scene->fonts, scene->balloon, &text_pane,
            label, strlen(label));
        while ((text_width > 390.32f ||
                (index >= WM_SLOT_COUNT && strlen(label) > 20)) &&
               strlen(label) > 3) {
            size_t length = strlen(label);
            if (length >= 3 && strcmp(label + length - 3, "...") == 0) {
                label[length - 3] = '\0';
                length -= 3;
            }
            if (length <= 3) break;
            label[length - 1] = '\0';
            strncat(label, "...", sizeof(label) - strlen(label) - 1);
            text_width = wm_font_cache_measure_text(
                scene->fonts, scene->balloon, &text_pane,
                label, strlen(label));
        }
        float width = fmaxf(160.0f * 832.0f / 608.0f,
                             text_width + 40.0f);
        wm_layout_set_pane_size(scene->balloon, "W_Base", width, 48.0f);
        wm_layout_set_pane_size(scene->balloon, "W_Shade", width, 48.0f);
        wm_layout_set_pose_text(scene->balloon, "T_Balloon", label);

        float x, y;
        float margin = index < WM_SLOT_COUNT ? 60.0f : 120.0f;
        if (index < WM_SLOT_COUNT) {
            int local = index % WM_CHANNELS_PER_PAGE;
            x = (-192.0f + (float)(local % 4) * 128.0f) *
                (832.0f / 608.0f);
            y = 75.0f - (float)(local / 4) * 96.0f;
        } else if (index < BALLOON_BOARD_BACK) {
            int footer_index = index - WM_SLOT_COUNT;
            const WmLayout *layout = footer_index == 2
                ? scene->sd_button : scene->footer;
            const float *parent = footer_index == 2 ? sd_position : NULL;
            if (!footer_balloon_anchor(layout, footer_panes[footer_index],
                                       parent, &x, &y))
                continue;
            y += 50.0f;
            if (footer_index == 2) margin = 200.0f;
        } else {
            WmBoardControl control = index == BALLOON_BOARD_BACK
                ? WM_BOARD_CONTROL_BACK
                : index == BALLOON_CALENDAR
                    ? WM_BOARD_CONTROL_CALENDAR : WM_BOARD_CONTROL_CREATE;
            if (!wm_board_scene_footer_button_anchor(board, control,
                                                      &x, &y)) continue;
            y += 50.0f;
        }
        x = fmaxf(-416.0f + margin + width * 0.5f,
                  fminf(416.0f - margin - width * 0.5f, x));
        /* Footer text moves N_Balloon beneath the IPL-scaled root, while
         * channel text moves the root itself. Only the footer translation
         * inherits the root's widescreen X scale. */
        float draw_x = index < WM_SLOT_COUNT ? x : x * (832.0f / 608.0f);
        float position[12] = {
            1, 0, 0, draw_x,
            0, 1, 0, y,
            0, 0, 1, 0
        };
        wm_layout_present_with_fonts(scene->platform, scene->textures,
                                      scene->fonts, scene->balloon,
                                      true, WM_LAYOUT_IPL, position);
    }
}

void wm_resource_scene_draw_board_balloons(WmResourceScene *scene,
                                            const WmBoardScene *board,
                                            WmBoardHit hover,
                                            float elapsed_seconds) {
    if (!scene || !board || !isfinite(elapsed_seconds)) return;
    int target = -1;
    if (wm_board_scene_phase(board) == WM_BOARD_READY &&
        wm_board_scene_child(board) == WM_BOARD_CHILD_NONE &&
        !wm_board_scene_dragging(board)) {
        if (hover.control == WM_BOARD_CONTROL_BACK)
            target = BALLOON_BOARD_BACK;
        else if (hover.control == WM_BOARD_CONTROL_CALENDAR)
            target = BALLOON_CALENDAR;
        else if (hover.control == WM_BOARD_CONTROL_CREATE)
            target = BALLOON_CREATE;
    }
    balloon_target(scene, available_balloon_target(scene, target));
    float frames = fmaxf(0.0f, elapsed_seconds - scene->last_draw_seconds)
                   * 60.0f;
    scene->last_draw_seconds = elapsed_seconds;
    advance_balloons(scene, frames);
    if (wm_board_scene_phase(board) == WM_BOARD_READY &&
        wm_board_scene_child(board) == WM_BOARD_CHILD_NONE)
        draw_balloons(scene, NULL, board);
}

static void draw_captured_preview(WmResourceScene *scene,
                                  const WmChannelZoom *zoom) {
    const float *matrix = zoom->preview_matrix;
    const float half_width = 416.0f;
    const float half_height = 228.0f;
    const float scale_x = (float)WM_FRAME_WIDTH / 832.0f;
    const float positions[4][2] = {
        {-half_width, half_height},
        {half_width, half_height},
        {-half_width, -half_height},
        {half_width, -half_height}
    };
    const float uv[4][2] = {
        {0, 0}, {1, 0}, {0, 1}, {1, 1}
    };
    WmDrawVertex vertices[4];
    for (size_t index = 0; index < 4; index++) {
        float world_x = matrix[0] * positions[index][0] + matrix[3];
        float world_y = matrix[5] * positions[index][1] + matrix[7];
        vertices[index] = (WmDrawVertex){
            .x = WM_FRAME_WIDTH * 0.5f + world_x * scale_x,
            .y = WM_FRAME_HEIGHT * 0.5f - world_y,
            .u = uv[index][0],
            .v = uv[index][1],
            .color = {1, 1, 1, zoom->alpha}
        };
    }
    wm_platform_draw_vertices(scene->platform, vertices,
                              scene->preview_capture);
    for (size_t index = 0; index < zoom->outside_count; index++) {
        const WmTransitionRect *rect = &zoom->outside[index];
        WmQuad shade = {
            .x = rect->x * scale_x,
            .y = rect->y,
            .width = rect->width * scale_x,
            .height = rect->height,
            .u0 = 0, .v0 = 0, .u1 = 1, .v1 = 1,
            .color = {0, 0, 0, zoom->alpha},
            .texture = 0
        };
        wm_platform_draw_quad(scene->platform, &shade);
    }
}

static void draw_grid_thumbnails(WmResourceScene *scene,
                                  const WmMenu *menu,
                                  const GridTraversal *traversal,
                                  float elapsed_seconds,
                                  WmChannelDrag *drag) {
    WmGridTile visible[5 * WM_CHANNELS_PER_PAGE];
    size_t tile_count = visible_grid_tiles(
        traversal, visible, sizeof(visible) / sizeof(visible[0]));
    WmChannelDragState drag_state = wm_channel_drag_state(drag);
    bool dragging = drag_state.phase != WM_CHANNEL_DRAG_NONE;
    for (size_t tile_index = 0; tile_index < tile_count; tile_index++) {
        const WmGridTile *tile = &visible[tile_index];
        int absolute = tile->slot;
        int index = absolute % WM_CHANNELS_PER_PAGE;
        WmLayout *thumbnail = scene->empty_channel;
        const char *animation = "my_IplTop_b";
        bool moving_origin = dragging && absolute == drag_state.source &&
            (drag_state.phase == WM_CHANNEL_DRAG_GRAB ||
             drag_state.phase == WM_CHANNEL_DRAG_MOVING ||
             drag_state.phase == WM_CHANNEL_DRAG_DROP_IN);
        if (!moving_origin && absolute == 0 && menu->slots[0].occupied) {
            thumbnail = scene->disc_channel;
            animation = "my_DiskCh_b";
        } else if (!moving_origin && absolute > 0 &&
                   scene->channel_icons[absolute]) {
            thumbnail = scene->channel_icons[absolute];
            animation = scene->channel_animations[absolute];
        }
        float icon_frame = elapsed_seconds * 60.0f +
            (thumbnail == scene->empty_channel ? index * 83.0f : 0.0f);
        if (thumbnail == scene->channel_icons[absolute] && absolute > 0) {
            const WmChannelAnimationOptions icon_options = {
                .language = "ENG",
                .measure_text = wm_font_cache_measure_text,
                .measure_context = scene->fonts
            };
            wm_channel_animation_pose(thumbnail, menu->slots[absolute].id,
                                       WM_CHANNEL_ICON, icon_frame,
                                       &icon_options);
        } else {
            pose_layout(thumbnail, animation, icon_frame);
        }
        wm_platform_set_clip(scene->platform, &tile->clip);
        wm_layout_present_with_fonts(
            scene->platform, scene->textures, scene->fonts, thumbnail,
            true, WM_LAYOUT_EMBEDDED, tile->matrix);
        if (dragging && menu->slots[absolute].occupied &&
            absolute != drag_state.source) {
            wm_channel_drag_draw_mask(drag, tile->matrix[3],
                                       tile->matrix[7]);
        }
        if (dragging && drag_state.target == absolute) {
            wm_channel_drag_draw_drop(drag, tile->matrix[3],
                                       tile->matrix[7]);
        }
        wm_platform_set_clip(scene->platform, NULL);
    }
}

/* ChannelSelect owns the clock on the grid and during the Board handoff.
 * Both paths use the same state so the intro and colon phase cannot restart
 * when draw ownership moves between scenes. */
static void draw_grid_clock(WmResourceScene *scene,
                            const GridTraversal *traversal,
                            float elapsed_seconds,
                            const float *camera,
                            const struct timespec *wall_time,
                            const struct tm *date) {
    if (!wall_time || !date ||
        !(traversal->has_clock_anchor[0] ||
          traversal->has_clock_anchor[1] ||
          traversal->has_clock_anchor[2])) return;

    int hour = date->tm_hour % 12;
    if (hour == 0) hour = 12;
    update_clock_textures(scene, hour, date->tm_min, date->tm_hour >= 12);
    if (scene->clock_change_start < 0.0f && elapsed_seconds >= 3.0f &&
        (date->tm_sec % 2) != 0) {
        scene->clock_change_start = elapsed_seconds;
    }
    float change_frame = scene->clock_change_start < 0.0f
        ? 0.0f
        : (elapsed_seconds - scene->clock_change_start) * 60.0f;
    if (change_frame > scene->clock_change_frames) {
        change_frame = scene->clock_change_frames;
    }
    WmLayoutClip clock_clips[3] = {
        {
            .animation = "my_Clock_a_Change",
            .frame = change_frame,
            .loop_override = 0,
            .target_name = "T_WiiMenu"
        },
        {
            .animation = "my_Clock_a_Change",
            .frame = change_frame,
            .loop_override = 0,
            .target_name = "N_Clock"
        }
    };
    size_t clock_clip_count = 2;
    if (scene->clock_change_start >= 0.0f) {
        int millisecond = (int)(wall_time->tv_nsec / 1000000L);
        float blink_frame =
            ((float)(date->tm_sec % 2) + (float)millisecond / 1000.0f) * 60.0f;
        if (blink_frame > scene->clock_blink_frames) {
            blink_frame = scene->clock_blink_frames;
        }
        clock_clips[clock_clip_count++] = (WmLayoutClip){
            .animation = "my_Clock_a_Min",
            .frame = blink_frame,
            .loop_override = 0,
            .target_name = "ClockTen"
        };
    }
    wm_layout_pose(scene->clock, clock_clips, clock_clip_count);
    for (size_t index = 0; index < 3; index++) {
        if (!traversal->has_clock_anchor[index]) continue;
        float clock_position[12] = {
            camera ? camera[0] : 1.0f, 0, 0,
            traversal->clock_anchors[index][3],
            0, camera ? camera[5] : 1.0f, 0,
            traversal->clock_anchors[index][7],
            0, 0, 1, 0
        };
        wm_layout_present_filtered_with_fonts(
            scene->platform, scene->textures, scene->fonts,
            scene->clock, true, WM_LAYOUT_IPL,
            clock_position, clock_pane, &hour);
    }
}

void wm_resource_scene_draw_grid_overlay(WmResourceScene *scene,
                                         const WmMenu *menu,
                                         float layout_frame,
                                         float elapsed_seconds) {
    if (!scene || !menu || menu->page < 0 || menu->page >= WM_PAGE_COUNT)
        return;
    pose_layout(scene->grid, "my_IplTop_a", layout_frame);
    GridTraversal traversal = {
        .page = menu->page,
        .masks_only = true,
        .zoom_scale_x = 1.0f,
        .zoom_scale_y = 1.0f
    };
    wm_layout_visit_all_transforms(scene->grid, true, WM_LAYOUT_IPL,
                                   NULL, collect_clock_anchor, &traversal);
    wm_layout_present_filtered_with_fonts(
        scene->platform, scene->textures, scene->fonts, scene->grid,
        true, WM_LAYOUT_IPL, NULL, grid_pane, &traversal);
    draw_grid_thumbnails(scene, menu, &traversal, elapsed_seconds, NULL);
    traversal.masks_only = false;
    wm_layout_present_filtered_with_fonts(
        scene->platform, scene->textures, scene->fonts, scene->grid,
        true, WM_LAYOUT_IPL, NULL, grid_pane, &traversal);
    struct timespec wall_time = {0};
    struct tm date;
    if (clock_gettime(CLOCK_REALTIME, &wall_time) == 0 &&
        localtime_r(&wall_time.tv_sec, &date) != NULL) {
        draw_grid_clock(scene, &traversal, elapsed_seconds, NULL,
                        &wall_time, &date);
    }
}

static void draw_sd_button(WmResourceScene *scene, const WmMenu *menu,
                           float elapsed_seconds, const float *camera,
                           float visibility_frame) {
    bool hovered = menu && scene->sd_hovered;
    float hover_frame = fminf(6.0f, fmaxf(0.0f,
        elapsed_seconds * 60.0f - scene->sd_hover_changed_at));
    WmLayoutClip clips[4] = {
        {
            .animation = "mn_Sdcard_Btn_On_Roop",
            .frame = elapsed_seconds * 60.0f,
            .group = "On_Roop",
            .loop_override = 1
        },
        {
            .animation = "mn_Sdcard_Btn_BtnL_Out",
            .frame = visibility_frame,
            .group = "Btn_L_InOut",
            .loop_override = 0
        },
        {
            .animation = hovered ? "mn_Sdcard_Btn_BtnL_RollOver"
                                 : "mn_Sdcard_Btn_BtnL_RollOut",
            .frame = hover_frame,
            .group = "Btn_L_Roll",
            .loop_override = 0
        }
    };
    size_t clip_count = 3;
    if (menu && menu->transition == WM_TRANSITION_SETTINGS &&
        menu->screen == WM_SCREEN_SD) {
        clips[clip_count++] = (WmLayoutClip){
            .animation = "mn_Sdcard_Btn_BtnL_On",
            .frame = fminf(20.0f, menu->transition_elapsed * 60.0f),
            .group = "Btn_L_On",
            .loop_override = 0
        };
    }
    wm_layout_pose(scene->sd_button, clips, clip_count);
    float zoom_position[12];
    const float *position = sd_position;
    if (camera) {
        memcpy(zoom_position, camera, sizeof(zoom_position));
        zoom_position[3] = camera[0] * sd_position[3] + camera[3];
        zoom_position[7] = camera[5] * sd_position[7] + camera[7];
        position = zoom_position;
    }
    wm_layout_present_filtered_with_fonts(
        scene->platform, scene->textures, scene->fonts,
        scene->sd_button, true, WM_LAYOUT_IPL,
        position, sd_button_pane, NULL);
}

void wm_resource_scene_draw_sd_button(WmResourceScene *scene,
                                      float elapsed_seconds) {
    if (!scene) return;
    if (scene->board_sd_reveal_started_at < 0.0f) {
        scene->board_sd_reveal_started_at = elapsed_seconds;
    }
    float visibility_frame = fmaxf(0.0f,
        15.0f - (elapsed_seconds - scene->board_sd_reveal_started_at) * 60.0f);
    draw_sd_button(scene, NULL, elapsed_seconds, NULL, visibility_frame);
}

void wm_resource_scene_release_preview_capture(WmResourceScene *scene) {
    if (!scene) return;
    if (scene->preview_capture)
        wm_platform_destroy_texture(scene->platform, scene->preview_capture);
    scene->preview_capture = 0;
    scene->capture_valid = false;
    scene->capture_slot = -1;
    scene->capture_date = -1;
    scene->capture_hover_slot = -1;
}

void wm_resource_scene_restart(WmResourceScene *scene) {
    if (!scene) return;
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
    scene->board_sd_reveal_started_at = -1.0f;
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

static bool capture_matches(const WmResourceScene *scene,
                            const WmMenu *menu, int slot, int date) {
    if (!scene->capture_valid || scene->capture_slot != slot ||
        scene->capture_date != date) return false;
    const WmChannel *current = &menu->slots[slot];
    const WmChannel *cached = &scene->capture_channel;
    return current->occupied == cached->occupied &&
           strcmp(current->id, cached->id) == 0 &&
           strcmp(current->title, cached->title) == 0 &&
           strcmp(current->banner_layout, cached->banner_layout) == 0;
}

static bool capture_preview(WmResourceScene *scene, const WmMenu *menu,
                            WmPreviewScene *preview, int slot, int date,
                            float preview_seconds) {
    if (!preview || slot < 0 || slot >= WM_SLOT_COUNT) return false;
    if (!scene->preview_capture)
        scene->preview_capture =
            wm_platform_create_render_texture(scene->platform);
    if (!scene->preview_capture ||
        !wm_platform_begin_target(scene->platform, scene->preview_capture,
                                  (WmColor){0.92f, 0.92f, 0.92f, 1.0f})) {
        scene->capture_valid = false;
        return false;
    }
    scene->capture_valid = wm_preview_scene_draw_capture(
        preview, menu, preview_seconds);
    wm_platform_end(scene->platform);
    if (scene->capture_valid) {
        scene->capture_slot = slot;
        scene->capture_date = date;
        scene->capture_channel = menu->slots[slot];
    }
    return scene->capture_valid;
}

static void draw_resource_scene(WmResourceScene *scene, const WmMenu *menu,
                                const WmResourceSceneFrame *frame,
                                bool owns_frame) {
    if (!scene || !menu || !frame) return;
    float elapsed_seconds = frame->elapsed_seconds;
    scene->board_sd_reveal_started_at = -1.0f;
    WmHit hover = frame->hover;
    advance_interactions(scene, menu, elapsed_seconds, hover,
                         frame->suppress_balloons);
    WmGridPresentation presentation = wm_menu_grid_presentation(menu);
    WmChannelZoom zoom = {0};
    const float *camera = NULL;
    if (presentation.zooming && presentation.zoom_slot >= 0 &&
        presentation.zoom_slot < WM_SLOT_COUNT) {
        pose_layout(scene->grid, "my_IplTop_a", 0.0f);
        GridTraversal anchor = {
            .page = presentation.page, .masks_only = true,
            .zoom_scale_x = 1.0f, .zoom_scale_y = 1.0f
        };
        WmLayoutDrawOptions options = {
            .wide = true, .mode = WM_LAYOUT_IPL, .alpha = 1.0f,
            .on_pane = grid_pane, .context = &anchor
        };
        wm_layout_draw(scene->grid, &options);
        int index = presentation.zoom_slot % WM_CHANNELS_PER_PAGE;
        if (anchor.has_tile[2][index] &&
            wm_channel_zoom(wm_menu_transition_frame(menu),
                            presentation.zoom_out,
                            anchor.tiles[2][index].matrix[3],
                            anchor.tiles[2][index].matrix[7],
                            true, &zoom)) {
            camera = zoom.camera_matrix;
        }
    }
    wm_texture_cache_begin_frame(scene->textures);
    wm_font_cache_begin_frame(scene->fonts);

    struct timespec wall_time = {0};
    struct tm date;
    bool has_date = clock_gettime(CLOCK_REALTIME, &wall_time) == 0 &&
                    localtime_r(&wall_time.tv_sec, &date) != NULL;
    int capture_date = has_date ? date.tm_year * 1000 + date.tm_yday : -1;
    bool can_prepare = !presentation.zooming &&
        menu->screen == WM_SCREEN_GRID &&
        menu->transition == WM_TRANSITION_NONE &&
        !menu->home_open && !menu->notice[0] &&
        !frame->suppress_balloons && has_date && frame->preview_scene &&
        wm_channel_drag_state(frame->drag).phase == WM_CHANNEL_DRAG_NONE &&
        frame->hover.type == WM_HIT_CHANNEL &&
        frame->hover.slot >= 0 && frame->hover.slot < WM_SLOT_COUNT &&
        menu->slots[frame->hover.slot].occupied;
    if (can_prepare) {
        int slot = frame->hover.slot;
        if (scene->capture_hover_slot != slot) {
            scene->capture_hover_slot = slot;
            scene->capture_hover_started = elapsed_seconds;
        }
        if (!capture_matches(scene, menu, slot, capture_date)) {
            scene->capture_valid = false;
            /* One stable pointer update moves the offscreen work out of the
             * first SELECT frame without capturing every tile crossed. */
            if (elapsed_seconds - scene->capture_hover_started >= 1.0f / 60.0f) {
                WmMenu preview_menu = *menu;
                preview_menu.screen = WM_SCREEN_PREVIEW;
                preview_menu.selected = slot;
                preview_menu.transition = WM_TRANSITION_SELECT;
                if (wm_preview_scene_available(frame->preview_scene,
                                               &preview_menu)) {
                    capture_preview(scene, &preview_menu,
                                    frame->preview_scene, slot,
                                    capture_date, 0.0f);
                }
            }
        }
    } else {
        scene->capture_hover_slot = -1;
        if (!presentation.zooming) scene->capture_valid = false;
    }
    bool captured = false;
    if (camera && frame->preview_scene) {
        if (presentation.zoom_out ||
            !capture_matches(scene, menu, presentation.zoom_slot,
                             capture_date))
            capture_preview(scene, menu, frame->preview_scene,
                            presentation.zoom_slot, capture_date,
                            presentation.zoom_out
                                ? frame->preview_elapsed_seconds : 0.0f);
        captured = scene->capture_valid;
    }
    if (owns_frame)
        wm_platform_begin(scene->platform,
                          (WmColor){0.92f, 0.92f, 0.92f, 1.0f});

    if (has_date) update_background_date(scene, &date);

    pose_layout(scene->background, "my_IplTop_c", 0);
    wm_layout_present_with_fonts(scene->platform, scene->textures, scene->fonts,
                                 scene->background, true, WM_LAYOUT_IPL, camera);

    if (frame->board_scene && has_date) {
        WmBoardDate today = {
            .year = date.tm_year + 1900,
            .month = date.tm_mon + 1,
            .day = date.tm_mday
        };
        wm_board_scene_draw_parked_memos(frame->board_scene, today, camera);
    }

    pose_layout(scene->grid, "my_IplTop_a", presentation.layout_frame);
    GridTraversal traversal = {
        .page = presentation.page, .masks_only = true,
        .zoom_scale_x = camera ? camera[0] : 1.0f,
        .zoom_scale_y = camera ? camera[5] : 1.0f
    };
    /* Native clock::draw reads all authored anchor matrices, even when a
     * page animation hides their parent from the visible grid traversal. */
    wm_layout_visit_all_transforms(scene->grid, true, WM_LAYOUT_IPL,
                                   camera, collect_clock_anchor, &traversal);
    wm_layout_present_filtered_with_fonts(
        scene->platform, scene->textures, scene->fonts, scene->grid,
        true, WM_LAYOUT_IPL, camera, grid_pane, &traversal);

    WmChannelDragState drag_state = wm_channel_drag_state(frame->drag);
    bool dragging = drag_state.phase != WM_CHANNEL_DRAG_NONE;
    draw_grid_thumbnails(scene, menu, &traversal, elapsed_seconds,
                         frame->drag);

    traversal.masks_only = false;
    wm_layout_present_filtered_with_fonts(
        scene->platform, scene->textures, scene->fonts, scene->grid,
        true, WM_LAYOUT_IPL, camera, grid_pane, &traversal);

    if (has_date) {
        draw_grid_clock(scene, &traversal, elapsed_seconds, camera,
                        &wall_time, &date);
    }

    if (menu->transition == WM_TRANSITION_SELECT &&
        menu->selected >= 0 && menu->selected < WM_SLOT_COUNT) {
        int index = menu->selected % WM_CHANNELS_PER_PAGE;
        if (traversal.has_tile[2][index]) {
            float frame = menu->transition_elapsed * 60.0f;
            if (frame < 16.0f) {
                WmLayoutClip select_clip = {
                    .animation = "my_IplTop_d_Select",
                    .frame = frame,
                    .loop_override = 0
                };
                wm_layout_pose(scene->focus, &select_clip, 1);
                wm_layout_present_with_fonts(
                    scene->platform, scene->textures, scene->fonts,
                    scene->focus, true, WM_LAYOUT_EMBEDDED,
                    traversal.tiles[2][index].matrix);
            }
        }
    } else {
        for (int index = 0; index < WM_CHANNELS_PER_PAGE; index++) {
            int absolute = presentation.page * WM_CHANNELS_PER_PAGE + index;
            FocusAnimation *focus = &scene->focus_states[absolute];
            if (!focus->active || !traversal.has_tile[2][index]) continue;
            WmLayoutClip focus_clip = {
                .animation = focus->off_phase ? "my_IplTop_d_FocusOff"
                                              : "my_IplTop_d_FocusOn",
                .frame = focus->frame,
                .loop_override = 0
            };
            wm_layout_pose(scene->focus, &focus_clip, 1);
            wm_layout_present_with_fonts(
                scene->platform, scene->textures, scene->fonts,
                scene->focus, true, WM_LAYOUT_EMBEDDED,
                traversal.tiles[2][index].matrix);
        }
    }

    /* Keep the carried channel beneath the common footer arrows. The grab
     * hand itself is the final overlay, like the ordinary pointer. */
    if (dragging) {
        wm_channel_drag_draw_shade(frame->drag);
    }

    WmLayoutClip footer_clips[12] = {
        {
            .animation = "my_IplTop_e",
            .frame = 0,
            .group = "G_SeenChange",
            .loop_override = 0
        },
        {
            .animation = "my_IplTop_e",
            .frame = 10000.0f + (float)((int)(elapsed_seconds * 60.0f) % 55),
            .group = "G_ArwRoop",
            .loop_override = 0
        },
        {
            .animation = "my_IplTop_e",
            .frame = (scene->arrow_visible[0] ? 10150.0f : 10100.0f) +
                     fminf(scene->arrow_age[0], 10.0f),
            .group = "G_ArwL_End",
            .loop_override = 0
        },
        {
            .animation = "my_IplTop_e",
            .frame = (scene->arrow_visible[1] ? 10150.0f : 10100.0f) +
                     fminf(scene->arrow_age[1], 10.0f),
            .group = "G_ArwR_End",
            .loop_override = 0
        }
    };
    size_t footer_clip_count = 4;
    static const char *const footer_groups[] = {
        "G_Set", "G_Bbs", "G_ArwL_Focus", "G_ArwR_Focus"
    };
    for (size_t index = 0; index < 4; index++) {
        HoverAnimation *state = &scene->footer_states[index];
        if (!state->active && index < 2) continue;
        float origin;
        float length;
        if (index == 0) {
            origin = state->entering ? 6900.0f : 6930.0f;
            length = state->entering ? 6.0f : 8.0f;
        } else if (index == 1) {
            origin = state->entering ? 900.0f : 930.0f;
            length = state->entering ? 6.0f : 8.0f;
        } else {
            origin = state->active && !state->entering ? 10800.0f : 10600.0f;
            length = 15.0f;
        }
        footer_clips[footer_clip_count++] = (WmLayoutClip){
            .animation = "my_IplTop_e",
            .frame = origin + fminf(state->frame, length),
            .group = footer_groups[index],
            .loop_override = 0
        };
    }
    for (size_t index = 0; index < 2; index++) {
        if (scene->arrow_press_age[index] < 0.0f) continue;
        footer_clips[footer_clip_count++] = (WmLayoutClip){
            .animation = "my_IplTop_e",
            .frame = 10700.0f + scene->arrow_press_age[index],
            .group = index == 0 ? "G_ArwL_Ac" : "G_ArwR_Ac",
            .loop_override = 0
        };
    }
    /* G_Bbs hover also keys Picture_00. Apply the mail-number group after
     * hover so its frame-zero pose keeps the spare envelope hidden when the
     * Board has no messages, matching messageBadgePose in the HTML port. */
    footer_clips[footer_clip_count++] = (WmLayoutClip){
        .animation = "my_IplTop_e",
        .frame = scene->message_badge_count
                     ? 1.0f + fmodf(elapsed_seconds * 60.0f, 399.0f)
                     : 0.0f,
        .group = "G_BbsSignal",
        .loop_override = 0
    };
    footer_clips[footer_clip_count++] = (WmLayoutClip){
        .animation = "my_IplTop_e",
        .frame = scene->new_mail_active
                     ? 1.0f + fminf(scene->new_mail_age, 159.0f)
                     : 0.0f,
        .group = "G_BbsSignal_new",
        .loop_override = 0
    };
    wm_layout_pose(scene->footer, footer_clips, footer_clip_count);
    if (camera) {
        wm_layout_present_filtered_with_fonts(
            scene->platform, scene->textures, scene->fonts,
            scene->footer, true, WM_LAYOUT_IPL, camera,
            footer_without_arrows, NULL);
    } else {
        wm_layout_present_with_fonts(
            scene->platform, scene->textures, scene->fonts,
            scene->footer, true, WM_LAYOUT_IPL, NULL);
    }

    draw_sd_button(scene, menu, elapsed_seconds, camera, 0.0f);
    if (camera && captured) {
        wm_layout_present_filtered_with_fonts(
            scene->platform, scene->textures, scene->fonts,
            scene->grid, true, WM_LAYOUT_IPL,
            camera, channel_mask_pane, NULL);
        draw_captured_preview(scene, &zoom);
    }
    if (camera) {
        /* The common arrows leave outside the channel camera, retaining
         * their source size while the selected channel zooms beneath them. */
        wm_platform_set_clip(scene->platform, NULL);
        wm_layout_present_filtered_with_fonts(
            scene->platform, scene->textures, scene->fonts,
            scene->footer, true, WM_LAYOUT_IPL, NULL,
            footer_arrows_only, NULL);
    }
    if (menu->transition == WM_TRANSITION_BACK && frame->preview_scene &&
        wm_menu_transition_frame(menu) <= 10.0f) {
        /* ChannelTitle's black outside rectangles have already been drawn.
         * Common arrows leave in the full-screen source projection, outside
         * the zoom camera and with no inherited channel clip. */
        wm_platform_set_clip(scene->platform, NULL);
        wm_preview_scene_draw_return_arrows(frame->preview_scene, menu,
                                            elapsed_seconds, true);
    }
    if (!presentation.zooming && !dragging && !menu->home_open &&
        !frame->suppress_balloons)
        draw_balloons(scene, menu, NULL);
    if (owns_frame) {
        wm_pointer_draw(frame->pointer);
        wm_platform_end(scene->platform);
    }
}

void wm_resource_scene_draw_layers(WmResourceScene *scene,
                                   const WmMenu *menu,
                                   const WmResourceSceneFrame *frame) {
    draw_resource_scene(scene, menu, frame, false);
}

void wm_resource_scene_draw(WmResourceScene *scene, const WmMenu *menu,
                            const WmResourceSceneFrame *frame) {
    draw_resource_scene(scene, menu, frame, true);
}
