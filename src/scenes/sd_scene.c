#include "sd_scene_internal.h"

#include "wii_menu/animation/channel_animation.h"
#include "wii_menu/input/source_hit.h"
#include "wii_menu/render/material_prepare.h"

#include <ctype.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

float wm_sd_frame_clamp(float value, float end) {
    return fminf(fmaxf(value, 0.0f), end);
}

bool wm_sd_page_can_move(unsigned page, int direction) {
    if (page >= WM_SD_PAGE_COUNT)
        return false;
    return (direction == -1 && page > 0) ||
           (direction == 1 && page + 1 < WM_SD_PAGE_COUNT);
}

float wm_sd_scroll_animation_frame(int direction, float elapsed_frames) {
    if (direction != -1 && direction != 1)
        return 0.0f;
    return (direction > 0 ? 40.0f : 0.0f) + wm_sd_frame_clamp(elapsed_frames, 20.0f);
}

static WmSdEvent *enqueue(WmSdScene *scene, WmSdEventType type, unsigned slot) {
    if (scene->event_count >= SD_EVENT_CAPACITY)
        return NULL;
    unsigned index = (scene->event_first + scene->event_count) % SD_EVENT_CAPACITY;
    scene->events[index] =
        (WmSdEvent){.type = type, .slot = slot, .control = WM_SD_CONTROL_NONE};
    scene->event_count++;
    return &scene->events[index];
}

bool wm_sd_scene_take_event(WmSdScene *scene, WmSdEvent *event) {
    if (!scene || !event || !scene->event_count)
        return false;
    *event = scene->events[scene->event_first];
    scene->event_first = (scene->event_first + 1) % SD_EVENT_CAPACITY;
    scene->event_count--;
    return true;
}

static WmLayout *load_layout(const char *root, const char *relative) {
    return wm_layout_load_asset(root, relative, "SD");
}

static bool prepare_wide_grid(WmLayout *grid) {
    static const char *const picture_targets[] = {
        "Picture_00", "Picture_01", "Picture_02", "Picture_03", "Picture_04"};
    static const char *const edge_targets[] = {"Edge0", "Edge1", "Edge2", "Edge3",
                                               "Edge4"};
    for (size_t index = 0; index < 5; index++) {
        if (!wm_layout_copy_texture_map(grid, "ChangeTex16x9", picture_targets[index],
                                        0) ||
            !wm_layout_copy_texture_map(grid, "Picture_16", edge_targets[index], 0))
            return false;
    }
    return true;
}

WmSdScene *wm_sd_scene_create(WmPlatform *platform, const char *assets_directory,
                              WmTextureCache *textures, WmFontCache *fonts) {
    if (!platform || !assets_directory || !assets_directory[0] || !textures || !fonts)
        return NULL;
    WmSdScene *scene = calloc(1, sizeof(*scene));
    if (!scene)
        return NULL;
    scene->platform = platform;
    scene->textures = textures;
    scene->fonts = fonts;
    int root_length = snprintf(scene->assets_directory, sizeof(scene->assets_directory),
                               "%s", assets_directory);
    if (root_length < 0 || root_length >= (int)sizeof(scene->assets_directory)) {
        free(scene);
        return NULL;
    }
    scene->grid =
        load_layout(assets_directory, "layouts/sdChanSel/mn_SdcardMenu_a.json");
    scene->footer =
        load_layout(assets_directory, "layouts/sdButton/mn_SdcardMenu_b.json");
    scene->empty_tile =
        load_layout(assets_directory, "layouts/sdChanSel/mn_SdcardMenu_d.json");
    for (size_t index = 0; index < 3; index++) {
        scene->page_labels[index] =
            load_layout(assets_directory, "layouts/sdChanSel/mn_SdcardMenu_Page.json");
    }
    scene->focus_layout =
        load_layout(assets_directory, "layouts/sdChanSel/my_IplTop_d.json");
    scene->balloon =
        load_layout(assets_directory, "layouts/sdChanSel/my_IplTopBalloon_a.json");
    scene->dialog =
        load_layout(assets_directory, "layouts/dlgWdw/my_DialogWindow_a2.json");
    scene->wait_icon =
        load_layout(assets_directory, "layouts/sdChanSel/wait_icon.json");
    scene->help_button =
        load_layout(assets_directory, "layouts/sdChanSel/help_Btn.json");
    scene->loading_panel =
        load_layout(assets_directory, "layouts/sdChanSel/mn_Nocard.json");
    scene->error_panel =
        load_layout(assets_directory, "layouts/sdChanSel/mn_Nocard.json");
    if (!scene->grid || !scene->footer || !scene->empty_tile || !scene->balloon ||
        !scene->dialog || !scene->wait_icon || !scene->help_button ||
        !scene->loading_panel || !scene->error_panel ||
        !prepare_wide_grid(scene->grid)) {
        wm_sd_scene_destroy(scene);
        return NULL;
    }
    for (size_t index = 0; index < 3; index++) {
        if (!scene->page_labels[index]) {
            wm_sd_scene_destroy(scene);
            return NULL;
        }
    }
    if (!scene->focus_layout) {
        wm_sd_scene_destroy(scene);
        return NULL;
    }
    WmLayout *layouts[] = {
        scene->grid,           scene->footer,         scene->empty_tile,
        scene->page_labels[0], scene->page_labels[1], scene->page_labels[2],
        scene->focus_layout,   scene->balloon,        scene->dialog,
        scene->wait_icon,      scene->help_button,    scene->loading_panel,
        scene->error_panel};
    for (size_t index = 0; index < sizeof(layouts) / sizeof(layouts[0]); index++) {
        wm_layout_prepare_materials(platform, layouts[index]);
    }
    scene->phase = WM_SD_CLOSED;
    scene->card_ready = true;
    wm_arrow_interaction_init(&scene->arrows,
                              (WmArrowInteractionConfig){.focus_in_frames = 13.0f,
                                                         .focus_out_frames = 13.0f,
                                                         .press_frames = 28.0f,
                                                         .visibility_frames = 11.0f,
                                                         .clear_focus_out_at_end = true,
                                                         .clear_press_at_end = true});
    scene->help_press = -1.0f;
    return scene;
}

void wm_sd_scene_destroy(WmSdScene *scene) {
    if (!scene)
        return;
    for (size_t index = 0; index < WM_SD_SLOT_COUNT; index++) {
        wm_layout_destroy(scene->channels[index].icon);
    }
    wm_layout_destroy(scene->grid);
    wm_layout_destroy(scene->footer);
    wm_layout_destroy(scene->empty_tile);
    for (size_t index = 0; index < 3; index++) {
        wm_layout_destroy(scene->page_labels[index]);
    }
    wm_layout_destroy(scene->focus_layout);
    wm_layout_destroy(scene->balloon);
    wm_layout_destroy(scene->dialog);
    wm_layout_destroy(scene->wait_icon);
    wm_layout_destroy(scene->help_button);
    wm_layout_destroy(scene->loading_panel);
    wm_layout_destroy(scene->error_panel);
    free(scene);
}

static bool valid_title_id(const char *id) {
    if (!id || strlen(id) != 16)
        return false;
    for (size_t index = 0; index < 16; index++) {
        char digit = id[index];
        if (!((digit >= '0' && digit <= '9') || (digit >= 'a' && digit <= 'f') ||
              (digit >= 'A' && digit <= 'F')))
            return false;
    }
    return true;
}

bool wm_sd_scene_set_channels(WmSdScene *scene, const WmSdChannel *channels,
                              size_t count) {
    if (!scene || count > WM_SD_SLOT_COUNT || (count && !channels))
        return false;
    SdChannel *next = calloc(WM_SD_SLOT_COUNT, sizeof(*next));
    if (!next)
        return false;
    bool valid = true;
    for (size_t index = 0; index < count; index++) {
        unsigned slot = channels[index].slot;
        if (slot >= WM_SD_SLOT_COUNT || !valid_title_id(channels[index].title_id) ||
            next[slot].icon) {
            valid = false;
            break;
        }
        char normalized[17];
        for (size_t digit = 0; digit < 16; digit++) {
            normalized[digit] =
                (char)tolower((unsigned char)channels[index].title_id[digit]);
        }
        normalized[16] = '\0';
        for (size_t previous = 0; previous < WM_SD_SLOT_COUNT; previous++) {
            if (next[previous].icon && strcmp(next[previous].id, normalized) == 0) {
                valid = false;
                break;
            }
        }
        if (!valid)
            break;
        char relative[128];
        snprintf(relative, sizeof(relative), "channel-layouts/%s/icon/icon.json",
                 normalized);
        next[slot].icon = load_layout(scene->assets_directory, relative);
        if (!next[slot].icon) {
            valid = false;
            break;
        }
        memcpy(next[slot].id, normalized, sizeof(next[slot].id));
    }
    if (valid) {
        for (size_t index = 0; index < WM_SD_SLOT_COUNT; index++) {
            wm_layout_destroy(scene->channels[index].icon);
            scene->channels[index] = next[index];
            if (scene->channels[index].icon) {
                wm_layout_prepare_materials(scene->platform,
                                            scene->channels[index].icon);
            }
            next[index].icon = NULL;
        }
        scene->populated_count = (unsigned)count;
    }
    for (size_t index = 0; index < WM_SD_SLOT_COUNT; index++) {
        wm_layout_destroy(next[index].icon);
    }
    free(next);
    return valid;
}

const char *wm_sd_scene_channel_id(const WmSdScene *scene, unsigned slot) {
    if (!scene || slot >= WM_SD_SLOT_COUNT || !scene->channels[slot].icon)
        return NULL;
    return scene->channels[slot].id;
}

void wm_sd_scene_set_messages(WmSdScene *scene, WmSdMessageProvider provider,
                              void *context) {
    if (!scene)
        return;
    scene->message_provider = provider;
    scene->message_context = context;
}

void wm_sd_scene_set_card_ready(WmSdScene *scene, bool ready) {
    if (scene)
        scene->card_ready = ready;
}

static void start_loader(WmSdScene *scene) {
    scene->loader_phase = SD_LOADER_ENTER;
    scene->loader_frame = 0.0f;
}

static void start_help(WmSdScene *scene, bool first_visit) {
    scene->dialog_phase = SD_DIALOG_ENTER;
    scene->dialog_frame = 0.0f;
    scene->dialog_page = 0;
    scene->dialog_previous_page = 0;
    scene->dialog_destination = 0;
    scene->dialog_selected = WM_SD_CONTROL_NONE;
    scene->icon_age = 0.0f;
    scene->welcome_active = first_visit;
    enqueue(scene, WM_SD_EVENT_HELP_OPEN, 0);
    enqueue(scene, WM_SD_EVENT_INFO_SOUND, 0);
}

bool wm_sd_scene_open(WmSdScene *scene, unsigned page, bool help_seen,
                      WmSdMediaStatus media_status) {
    if (!scene || page >= WM_SD_PAGE_COUNT || media_status < WM_SD_MEDIA_READY ||
        media_status > WM_SD_MEDIA_UNSUPPORTED)
        return false;
    scene->page = page;
    scene->help_seen = help_seen;
    scene->media_status = media_status;
    scene->phase = WM_SD_ACTIVE;
    scene->age = 0.0f;
    scene->scroll_frame = 0.0f;
    scene->scroll_direction = 0;
    scene->welcome_pending = !help_seen;
    scene->welcome_active = false;
    scene->dialog_phase = SD_DIALOG_CLOSED;
    scene->dialog_frame = 0.0f;
    scene->loader_phase = help_seen ? SD_LOADER_ENTER : SD_LOADER_CLOSED;
    scene->loader_frame = 0.0f;
    scene->hover = (WmSdHit){WM_SD_CONTROL_NONE, 0};
    scene->balloon_hover = WM_SD_CONTROL_NONE;
    memset(scene->balloons, 0, sizeof(scene->balloons));
    memset(scene->button_focus, 0, sizeof(scene->button_focus));
    memset(scene->tile_focus, 0, sizeof(scene->tile_focus));
    wm_arrow_interaction_reset(
        &scene->arrows, media_status == WM_SD_MEDIA_READY && page > 0,
        media_status == WM_SD_MEDIA_READY && page + 1 < WM_SD_PAGE_COUNT, 11.0f);
    scene->help_press = -1.0f;
    scene->event_first = scene->event_count = 0;
    return true;
}

void wm_sd_scene_reset(WmSdScene *scene) {
    if (!scene)
        return;
    unsigned page = scene->page < WM_SD_PAGE_COUNT ? scene->page : 0;
    WmSdMediaStatus status = scene->media_status;
    if (status < WM_SD_MEDIA_READY || status > WM_SD_MEDIA_UNSUPPORTED)
        status = WM_SD_MEDIA_READY;
    wm_sd_scene_open(scene, page, scene->help_seen, status);
    scene->phase = WM_SD_CLOSED;
    scene->welcome_pending = false;
    scene->welcome_active = false;
    scene->loader_phase = SD_LOADER_CLOSED;
    scene->loader_frame = 0.0f;
    scene->dialog_phase = SD_DIALOG_CLOSED;
    scene->dialog_frame = 0.0f;
    scene->dialog_page = 0;
    scene->dialog_previous_page = 0;
    scene->dialog_destination = 0;
    scene->dialog_selected = WM_SD_CONTROL_NONE;
    scene->icon_age = 0.0f;
    scene->event_first = 0;
    scene->event_count = 0;
}

WmSdPhase wm_sd_scene_phase(const WmSdScene *scene) {
    return scene ? scene->phase : WM_SD_CLOSED;
}

unsigned wm_sd_scene_page(const WmSdScene *scene) {
    return scene ? scene->page : 0;
}

bool wm_sd_scene_help_seen(const WmSdScene *scene) {
    return scene && scene->help_seen;
}

bool wm_sd_scene_help_open(const WmSdScene *scene) {
    return scene && scene->dialog_phase != SD_DIALOG_CLOSED;
}

unsigned wm_sd_scene_help_page(const WmSdScene *scene) {
    return scene ? scene->dialog_page : 0;
}

bool wm_sd_scene_is_locked(const WmSdScene *scene) {
    return !scene || scene->phase != WM_SD_ACTIVE || scene->welcome_pending ||
           scene->loader_phase != SD_LOADER_CLOSED ||
           (scene->dialog_phase != SD_DIALOG_CLOSED &&
            scene->dialog_phase != SD_DIALOG_IDLE);
}

static void advance_loader(WmSdScene *scene, float frames) {
    if (scene->loader_phase == SD_LOADER_CLOSED)
        return;
    bool was_waiting = scene->loader_phase == SD_LOADER_WAIT;
    float previous_wait = was_waiting ? scene->loader_frame : 0.0f;
    scene->loader_frame += frames;
    if (scene->loader_phase == SD_LOADER_ENTER && scene->loader_frame >= 33.0f) {
        scene->loader_frame -= 33.0f;
        scene->loader_phase = SD_LOADER_WAIT;
    }
    float minimum = scene->media_status == WM_SD_MEDIA_READY && scene->populated_count
                        ? 60.0f
                        : 0.0f;
    if (scene->loader_phase == SD_LOADER_WAIT && scene->loader_frame >= minimum &&
        scene->card_ready) {
        scene->loader_frame = was_waiting && previous_wait >= minimum
                                  ? 0.0f
                                  : scene->loader_frame - minimum;
        scene->loader_phase = SD_LOADER_EXIT;
    }
    if (scene->loader_phase == SD_LOADER_EXIT && scene->loader_frame >= 16.0f) {
        scene->loader_phase = SD_LOADER_CLOSED;
        scene->loader_frame = 0.0f;
    }
}

static float dialog_duration(SdDialogPhase phase) {
    switch (phase) {
        case SD_DIALOG_ENTER:
            return 25.0f;
        case SD_DIALOG_SELECT:
            return 21.0f;
        case SD_DIALOG_TEXT_OUT:
        case SD_DIALOG_TEXT_IN:
            return 10.0f;
        case SD_DIALOG_EXIT:
            return 21.0f;
        default:
            return 0.0f;
    }
}

static void complete_dialog_selection(WmSdScene *scene) {
    unsigned count = scene->welcome_active ? 4 : 3;
    if (scene->dialog_destination < 0 || scene->dialog_destination >= (int)count) {
        scene->dialog_phase = SD_DIALOG_EXIT;
        return;
    }
    scene->dialog_phase = SD_DIALOG_TEXT_OUT;
    /* Resume the selected button's hover entrance while the page text fades. */
    if (scene->hover.control == scene->dialog_selected) {
        scene->button_focus[scene->dialog_selected] =
            (SdFocus){.active = true, .entering = true, .frame = scene->dialog_frame};
    }
    scene->dialog_selected = WM_SD_CONTROL_NONE;
}

static void close_dialog(WmSdScene *scene) {
    scene->dialog_phase = SD_DIALOG_CLOSED;
    scene->hover = (WmSdHit){WM_SD_CONTROL_NONE, 0};
    scene->button_focus[WM_SD_CONTROL_HELP_BACK].active = false;
    scene->button_focus[WM_SD_CONTROL_HELP_NEXT].active = false;
    enqueue(scene, WM_SD_EVENT_HELP_CLOSE, 0);
    if (scene->welcome_active) {
        scene->help_seen = true;
        scene->welcome_active = false;
        start_loader(scene);
    }
}

static void complete_dialog_phase(WmSdScene *scene) {
    switch (scene->dialog_phase) {
        case SD_DIALOG_ENTER:
        case SD_DIALOG_TEXT_IN:
            scene->dialog_phase = SD_DIALOG_IDLE;
            break;
        case SD_DIALOG_SELECT:
            complete_dialog_selection(scene);
            break;
        case SD_DIALOG_TEXT_OUT:
            scene->dialog_page = (unsigned)scene->dialog_destination;
            scene->icon_age = 0.0f;
            scene->dialog_phase = SD_DIALOG_TEXT_IN;
            break;
        case SD_DIALOG_EXIT:
            close_dialog(scene);
            break;
        default:
            break;
    }
}

static void advance_dialog(WmSdScene *scene, float frames) {
    if (scene->dialog_phase == SD_DIALOG_CLOSED)
        return;
    if (scene->dialog_phase == SD_DIALOG_IDLE) {
        scene->icon_age += frames;
        return;
    }
    scene->dialog_frame += frames;
    while (scene->dialog_phase != SD_DIALOG_CLOSED &&
           scene->dialog_phase != SD_DIALOG_IDLE &&
           scene->dialog_frame >= dialog_duration(scene->dialog_phase)) {
        scene->dialog_frame -= dialog_duration(scene->dialog_phase);
        complete_dialog_phase(scene);
    }
}

static void advance_focus(SdFocus *focus, float frames, float end) {
    if (!focus->active)
        return;
    focus->frame = wm_sd_frame_clamp(focus->frame + frames, end);
    if (!focus->entering && focus->frame >= end)
        focus->active = false;
}

static void advance_tile_focus(WmSdScene *scene, float frames) {
    for (size_t index = 0; index < WM_SD_SLOT_COUNT; index++) {
        SdFocus *focus = &scene->tile_focus[index];
        if (!focus->active)
            continue;
        focus->frame += frames;
        if (focus->entering && focus->frame >= 5.0f && focus->pending_leave) {
            focus->entering = false;
            focus->frame = 0.0f;
        }
        if (!focus->entering && focus->frame >= 30.0f) {
            focus->active = false;
        } else if (focus->entering) {
            focus->frame = wm_sd_frame_clamp(focus->frame, 5.0f);
        }
    }
}

static void advance_balloons(WmSdScene *scene, float frames) {
    for (size_t index = 0; index < 2; index++) {
        SdBalloon *balloon = &scene->balloons[index];
        if (balloon->phase == SD_BALLOON_WAIT) {
            balloon->wait += frames;
            if (balloon->wait >= 17.0f) {
                balloon->phase = SD_BALLOON_ENTER;
                balloon->frame = fminf(6.0f, balloon->wait - 17.0f);
                enqueue(scene, WM_SD_EVENT_BALLOON_SOUND, 0);
            }
        } else if (balloon->phase == SD_BALLOON_ENTER) {
            balloon->frame = wm_sd_frame_clamp(balloon->frame + frames, 6.0f);
        } else if (balloon->phase == SD_BALLOON_LEAVE) {
            balloon->frame = fmaxf(0.0f, balloon->frame - frames);
            if (balloon->frame == 0.0f)
                balloon->phase = SD_BALLOON_CLOSED;
        }
    }
}

static void advance_scroll(WmSdScene *scene, float frames) {
    if (scene->phase != WM_SD_SCROLL)
        return;
    scene->scroll_frame += frames;
    if (scene->scroll_frame < 20.0f)
        return;
    scene->page = (unsigned)((int)scene->page + scene->scroll_direction);
    scene->phase = WM_SD_ACTIVE;
    scene->scroll_frame = 0.0f;
    scene->scroll_direction = 0;
    enqueue(scene, WM_SD_EVENT_PAGE_CHANGED, scene->page);
    bool arrows[2] = {scene->media_status == WM_SD_MEDIA_READY && scene->page > 0,
                      scene->media_status == WM_SD_MEDIA_READY &&
                          scene->page + 1 < WM_SD_PAGE_COUNT};
    for (size_t index = 0; index < 2; index++) {
        WmSdControl control = index == 0 ? WM_SD_CONTROL_PREVIOUS : WM_SD_CONTROL_NEXT;
        bool changed =
            wm_arrow_interaction_set_visible(&scene->arrows, (int)index, arrows[index]);
        if (changed && !arrows[index] && scene->hover.control == control)
            wm_sd_scene_hover(scene, (WmSdHit){WM_SD_CONTROL_NONE, 0});
    }
}

void wm_sd_scene_advance(WmSdScene *scene, float frames, bool revealing) {
    if (!scene || scene->phase == WM_SD_CLOSED || !isfinite(frames) || frames < 0.0f)
        return;
    scene->age += frames;
    advance_loader(scene, frames);
    for (size_t index = 0;
         index < sizeof(scene->button_focus) / sizeof(scene->button_focus[0]);
         index++) {
        if (index == WM_SD_CONTROL_PREVIOUS || index == WM_SD_CONTROL_NEXT)
            continue;
        advance_focus(&scene->button_focus[index], frames, 9.0f);
    }
    advance_dialog(scene, frames);
    if (scene->welcome_pending && !revealing) {
        scene->welcome_pending = false;
        start_help(scene, true);
    }
    advance_tile_focus(scene, frames);
    if (scene->help_press >= 0.0f) {
        scene->help_press += frames;
        if (scene->help_press >= 21.0f)
            scene->help_press = -1.0f;
    }
    wm_arrow_interaction_advance(&scene->arrows, frames);
    advance_balloons(scene, frames);
    advance_scroll(scene, frames);
}

static bool in_rectangle(WmSourceRect rectangle, int x, int y, float margin) {
    return x >= rectangle.x - margin && x <= rectangle.x + rectangle.width + margin &&
           y >= rectangle.y - margin && y <= rectangle.y + rectangle.height + margin;
}

static bool footer_hit(WmSdScene *scene, const char *pane_name, int x, int y,
                       float margin) {
    WmSourceRect rectangle;
    return wm_source_pane_rect(scene->footer, pane_name, true, WM_LAYOUT_IPL, NULL,
                               &rectangle) &&
           in_rectangle(rectangle, x, y, margin);
}

static bool help_hit(WmSdScene *scene, const char *pane_name, int x, int y) {
    WmSourceRect rectangle;
    return wm_source_pane_rect(scene->dialog, pane_name, true, WM_LAYOUT_IPL, NULL,
                               &rectangle) &&
           in_rectangle(rectangle, x, y, 0.0f);
}

WmSdHit wm_sd_scene_hit(WmSdScene *scene, int x, int y) {
    WmSdHit none = {WM_SD_CONTROL_NONE, 0};
    if (!scene || scene->phase == WM_SD_CLOSED)
        return none;
    if (scene->dialog_phase != SD_DIALOG_CLOSED) {
        if (scene->dialog_phase != SD_DIALOG_IDLE)
            return none;
        if (!(scene->welcome_active && scene->dialog_page == 0) &&
            help_hit(scene, "B_BtnA", x, y)) {
            return (WmSdHit){WM_SD_CONTROL_HELP_BACK, 0};
        }
        if (help_hit(scene, "B_BtnB", x, y)) {
            return (WmSdHit){WM_SD_CONTROL_HELP_NEXT, 0};
        }
        return none;
    }
    if (scene->loader_phase != SD_LOADER_CLOSED || scene->welcome_pending ||
        scene->phase == WM_SD_LEAVING)
        return none;
    wm_sd_pose_footer(scene);
    /* G_ArwRoop shifts the source hit pane by nearly three pixels. Retain
     * an expanded, already-held arrow before testing overlapping tiles. */
    if (scene->hover.control == WM_SD_CONTROL_PREVIOUS &&
        footer_hit(scene, "B_ArwL", x, y, 4.0f))
        return scene->hover;
    if (scene->hover.control == WM_SD_CONTROL_NEXT &&
        footer_hit(scene, "B_ArwR", x, y, 4.0f))
        return scene->hover;
    if (wm_arrow_interaction_side(&scene->arrows, WM_ARROW_PREVIOUS)->visible &&
        footer_hit(scene, "B_ArwL", x, y, 0.0f)) {
        return (WmSdHit){WM_SD_CONTROL_PREVIOUS, 0};
    }
    if (wm_arrow_interaction_side(&scene->arrows, WM_ARROW_NEXT)->visible &&
        footer_hit(scene, "B_ArwR", x, y, 0.0f)) {
        return (WmSdHit){WM_SD_CONTROL_NEXT, 0};
    }
    if (scene->phase == WM_SD_SCROLL)
        return none;
    if (footer_hit(scene, "B_Wiimenu", x, y, 0.0f)) {
        return (WmSdHit){WM_SD_CONTROL_BACK, 0};
    }
    if (footer_hit(scene, "B_Help", x, y, 0.0f)) {
        return (WmSdHit){WM_SD_CONTROL_HELP, 0};
    }
    if (scene->media_status != WM_SD_MEDIA_READY)
        return none;
    WmLayoutClip grid_clip = {
        .animation = "mn_SdcardMenu_a", .frame = 0.0f, .loop_override = 0};
    wm_layout_pose(scene->grid, &grid_clip, 1);
    for (unsigned index = 0; index < WM_SD_SLOTS_PER_PAGE; index++) {
        unsigned absolute = scene->page * WM_SD_SLOTS_PER_PAGE + index;
        if (!scene->channels[absolute].icon)
            continue;
        char name[16];
        snprintf(name, sizeof(name), "N_Ch_c%02u", index + 1);
        WmSourceRect rectangle;
        if (wm_source_pane_rect(scene->grid, name, true, WM_LAYOUT_IPL, NULL,
                                &rectangle) &&
            in_rectangle(rectangle, x, y, 0.0f)) {
            return (WmSdHit){WM_SD_CONTROL_CHANNEL, absolute};
        }
    }
    return none;
}

static bool same_hit(WmSdHit first, WmSdHit second) {
    return first.control == second.control &&
           (first.control != WM_SD_CONTROL_CHANNEL || first.slot == second.slot);
}

static void clear_tile_focus(WmSdScene *scene) {
    for (size_t index = 0; index < WM_SD_SLOT_COUNT; index++) {
        if (!scene->tile_focus[index].active)
            continue;
        if (scene->tile_focus[index].entering) {
            scene->tile_focus[index].pending_leave = true;
        }
    }
}

static void target_balloon(WmSdScene *scene, WmSdControl control) {
    if (control != WM_SD_CONTROL_BACK && control != WM_SD_CONTROL_HELP)
        control = WM_SD_CONTROL_NONE;
    if (control == scene->balloon_hover)
        return;
    if (scene->balloon_hover != WM_SD_CONTROL_NONE) {
        size_t old_index = scene->balloon_hover == WM_SD_CONTROL_BACK ? 0 : 1;
        SdBalloon *old = &scene->balloons[old_index];
        old->phase =
            old->phase == SD_BALLOON_WAIT ? SD_BALLOON_CLOSED : SD_BALLOON_LEAVE;
    }
    scene->balloon_hover = control;
    if (control != WM_SD_CONTROL_NONE) {
        size_t index = control == WM_SD_CONTROL_BACK ? 0 : 1;
        scene->balloons[index] = (SdBalloon){.phase = SD_BALLOON_WAIT};
    }
}

void wm_sd_scene_hover(WmSdScene *scene, WmSdHit hit) {
    if (!scene || scene->phase == WM_SD_CLOSED)
        return;
    if (scene->dialog_phase != SD_DIALOG_CLOSED) {
        if (scene->dialog_phase != SD_DIALOG_IDLE ||
            (hit.control != WM_SD_CONTROL_HELP_BACK &&
             hit.control != WM_SD_CONTROL_HELP_NEXT) ||
            (scene->welcome_active && scene->dialog_page == 0 &&
             hit.control == WM_SD_CONTROL_HELP_BACK)) {
            hit = (WmSdHit){WM_SD_CONTROL_NONE, 0};
        }
    } else if (scene->loader_phase != SD_LOADER_CLOSED || scene->welcome_pending ||
               scene->phase == WM_SD_LEAVING ||
               (scene->phase == WM_SD_SCROLL && hit.control != WM_SD_CONTROL_PREVIOUS &&
                hit.control != WM_SD_CONTROL_NEXT)) {
        hit = (WmSdHit){WM_SD_CONTROL_NONE, 0};
    }
    if (hit.control == WM_SD_CONTROL_CHANNEL &&
        (hit.slot >= WM_SD_SLOT_COUNT || scene->media_status != WM_SD_MEDIA_READY ||
         hit.slot / WM_SD_SLOTS_PER_PAGE != scene->page ||
         !scene->channels[hit.slot].icon)) {
        hit = (WmSdHit){WM_SD_CONTROL_NONE, 0};
    }
    if ((hit.control == WM_SD_CONTROL_PREVIOUS &&
         !wm_arrow_interaction_side(&scene->arrows, WM_ARROW_PREVIOUS)->visible) ||
        (hit.control == WM_SD_CONTROL_NEXT &&
         !wm_arrow_interaction_side(&scene->arrows, WM_ARROW_NEXT)->visible)) {
        hit = (WmSdHit){WM_SD_CONTROL_NONE, 0};
    }
    if (same_hit(scene->hover, hit))
        return;
    if (scene->hover.control == WM_SD_CONTROL_CHANNEL)
        clear_tile_focus(scene);
    else if (scene->hover.control > WM_SD_CONTROL_NONE &&
             scene->hover.control <= WM_SD_CONTROL_HELP_NEXT) {
        if (scene->hover.control != WM_SD_CONTROL_PREVIOUS &&
            scene->hover.control != WM_SD_CONTROL_NEXT) {
            SdFocus *previous = &scene->button_focus[scene->hover.control];
            previous->active = true;
            previous->entering = false;
            previous->frame = 0.0f;
        }
    }
    wm_arrow_interaction_hover(&scene->arrows,
                               hit.control == WM_SD_CONTROL_PREVIOUS ? WM_ARROW_PREVIOUS
                               : hit.control == WM_SD_CONTROL_NEXT   ? WM_ARROW_NEXT
                                                                     : -1);
    scene->hover = hit;
    target_balloon(scene, hit.control);
    if (hit.control == WM_SD_CONTROL_NONE)
        return;
    WmSdEvent *event = enqueue(scene, WM_SD_EVENT_HOVER_SOUND, 0);
    if (event)
        event->control = hit.control;
    if (hit.control == WM_SD_CONTROL_CHANNEL) {
        SdFocus *focus = &scene->tile_focus[hit.slot];
        if (focus->active && focus->entering) {
            focus->pending_leave = false;
        } else {
            *focus = (SdFocus){.active = true, .entering = true};
        }
    } else if (hit.control != WM_SD_CONTROL_PREVIOUS &&
               hit.control != WM_SD_CONTROL_NEXT) {
        scene->button_focus[hit.control] = (SdFocus){.active = true, .entering = true};
    }
}

bool wm_sd_scene_activate(WmSdScene *scene, WmSdHit hit) {
    if (!scene || scene->phase == WM_SD_CLOSED)
        return false;
    if (scene->dialog_phase != SD_DIALOG_CLOSED) {
        if (scene->dialog_phase != SD_DIALOG_IDLE ||
            (hit.control != WM_SD_CONTROL_HELP_BACK &&
             hit.control != WM_SD_CONTROL_HELP_NEXT) ||
            (scene->welcome_active && scene->dialog_page == 0 &&
             hit.control == WM_SD_CONTROL_HELP_BACK))
            return false;
        scene->dialog_previous_page = scene->dialog_page;
        scene->dialog_destination =
            (int)scene->dialog_page + (hit.control == WM_SD_CONTROL_HELP_BACK ? -1 : 1);
        scene->dialog_selected = hit.control;
        scene->dialog_phase = SD_DIALOG_SELECT;
        scene->dialog_frame = 0.0f;
        enqueue(scene,
                hit.control == WM_SD_CONTROL_HELP_BACK ? WM_SD_EVENT_CANCEL_SOUND
                                                       : WM_SD_EVENT_CONFIRM_SOUND,
                0);
        return true;
    }
    if (wm_sd_scene_is_locked(scene))
        return false;
    if (hit.control == WM_SD_CONTROL_CHANNEL) {
        if (scene->media_status != WM_SD_MEDIA_READY || hit.slot >= WM_SD_SLOT_COUNT ||
            hit.slot / WM_SD_SLOTS_PER_PAGE != scene->page ||
            !scene->channels[hit.slot].icon)
            return false;
        enqueue(scene, WM_SD_EVENT_CONFIRM_SOUND, 0);
        enqueue(scene, WM_SD_EVENT_CHANNEL_SELECTED, hit.slot);
        return true;
    }
    if (hit.control == WM_SD_CONTROL_BACK) {
        scene->phase = WM_SD_LEAVING;
        wm_sd_scene_hover(scene, (WmSdHit){WM_SD_CONTROL_NONE, 0});
        enqueue(scene, WM_SD_EVENT_CONFIRM_SOUND, 0);
        enqueue(scene, WM_SD_EVENT_EXIT, 0);
        return true;
    }
    if (hit.control == WM_SD_CONTROL_HELP) {
        scene->help_press = 0.0f;
        wm_sd_scene_hover(scene, (WmSdHit){WM_SD_CONTROL_NONE, 0});
        enqueue(scene, WM_SD_EVENT_CONFIRM_SOUND, 0);
        start_help(scene, false);
        return true;
    }
    int direction = hit.control == WM_SD_CONTROL_PREVIOUS ? -1
                    : hit.control == WM_SD_CONTROL_NEXT   ? 1
                                                          : 0;
    if (scene->media_status != WM_SD_MEDIA_READY ||
        !wm_sd_page_can_move(scene->page, direction))
        return false;
    scene->phase = WM_SD_SCROLL;
    scene->scroll_direction = direction;
    scene->scroll_frame = 0.0f;
    wm_arrow_interaction_press(&scene->arrows,
                               direction < 0 ? WM_ARROW_PREVIOUS : WM_ARROW_NEXT, 0.0f);
    clear_tile_focus(scene);
    enqueue(scene, WM_SD_EVENT_PAGE_SOUND, 0);
    return true;
}

bool wm_sd_scene_back(WmSdScene *scene) {
    if (!scene)
        return false;
    if (scene->dialog_phase != SD_DIALOG_CLOSED) {
        return wm_sd_scene_activate(scene, (WmSdHit){WM_SD_CONTROL_HELP_BACK, 0});
    }
    return wm_sd_scene_activate(scene, (WmSdHit){WM_SD_CONTROL_BACK, 0});
}
