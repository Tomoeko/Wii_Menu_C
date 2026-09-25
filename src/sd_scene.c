#include "wii_menu/sd_scene.h"

#include "wii_menu/arrow_interaction.h"
#include "wii_menu/channel_animation.h"
#include "wii_menu/layout_present.h"
#include "wii_menu/layout_runtime.h"
#include "wii_menu/material_prepare.h"
#include "wii_menu/source_hit.h"

#include <ctype.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

enum {
    SD_PATH_CAPACITY = 4096,
    SD_VISIBLE_TILES = 5 * WM_SD_SLOTS_PER_PAGE,
    SD_CLIP_CAPACITY = 18,
    SD_EVENT_CAPACITY = 16
};

typedef struct SdChannel {
    char id[17];
    WmLayout *icon;
} SdChannel;

typedef struct SdFocus {
    bool active;
    bool entering;
    bool pending_leave;
    float frame;
} SdFocus;

typedef struct SdTile {
    bool valid;
    unsigned absolute_slot;
    float matrix[12];
    WmClipRect clip;
} SdTile;

typedef enum SdBalloonPhase {
    SD_BALLOON_CLOSED,
    SD_BALLOON_WAIT,
    SD_BALLOON_ENTER,
    SD_BALLOON_LEAVE
} SdBalloonPhase;

typedef struct SdBalloon {
    SdBalloonPhase phase;
    float wait;
    float frame;
} SdBalloon;

typedef enum SdDialogPhase {
    SD_DIALOG_CLOSED,
    SD_DIALOG_ENTER,
    SD_DIALOG_IDLE,
    SD_DIALOG_SELECT,
    SD_DIALOG_TEXT_OUT,
    SD_DIALOG_TEXT_IN,
    SD_DIALOG_EXIT
} SdDialogPhase;

typedef enum SdLoaderPhase {
    SD_LOADER_CLOSED,
    SD_LOADER_ENTER,
    SD_LOADER_WAIT,
    SD_LOADER_EXIT
} SdLoaderPhase;

struct WmSdScene {
    char assets_directory[SD_PATH_CAPACITY];
    WmPlatform *platform;
    WmTextureCache *textures;
    WmFontCache *fonts;
    WmLayout *grid;
    WmLayout *footer;
    WmLayout *empty_tile;
    WmLayout *page_labels[3];
    WmLayout *focus_layout;
    WmLayout *balloon;
    WmLayout *dialog;
    WmLayout *wait_icon;
    WmLayout *help_button;
    WmLayout *loading_panel;
    WmLayout *error_panel;
    SdChannel channels[WM_SD_SLOT_COUNT];
    unsigned populated_count;
    unsigned page;
    WmSdPhase phase;
    WmSdMediaStatus media_status;
    bool card_ready;
    float age;
    float scroll_frame;
    int scroll_direction;
    bool help_seen;
    bool welcome_pending;
    bool welcome_active;
    SdDialogPhase dialog_phase;
    float dialog_frame;
    unsigned dialog_page;
    unsigned dialog_previous_page;
    int dialog_destination;
    WmSdControl dialog_selected;
    float icon_age;
    SdLoaderPhase loader_phase;
    float loader_frame;
    SdFocus button_focus[WM_SD_CONTROL_HELP_NEXT + 1];
    SdFocus tile_focus[WM_SD_SLOT_COUNT];
    WmSdHit hover;
    WmArrowInteraction arrows;
    float help_press;
    WmSdControl balloon_hover;
    SdBalloon balloons[2];
    WmSdMessageProvider message_provider;
    void *message_context;
    WmSdEvent events[SD_EVENT_CAPACITY];
    unsigned event_first;
    unsigned event_count;
};

static float frame_clamp(float value, float end) {
    return fminf(fmaxf(value, 0.0f), end);
}

bool wm_sd_page_can_move(unsigned page, int direction) {
    if (page >= WM_SD_PAGE_COUNT) return false;
    return (direction == -1 && page > 0) ||
           (direction == 1 && page + 1 < WM_SD_PAGE_COUNT);
}

float wm_sd_scroll_animation_frame(int direction, float elapsed_frames) {
    if (direction != -1 && direction != 1) return 0.0f;
    return (direction > 0 ? 40.0f : 0.0f) +
           frame_clamp(elapsed_frames, 20.0f);
}

static void enqueue(WmSdScene *scene, WmSdEventType type, unsigned slot) {
    if (scene->event_count >= SD_EVENT_CAPACITY) return;
    unsigned index = (scene->event_first + scene->event_count) % SD_EVENT_CAPACITY;
    scene->events[index] = (WmSdEvent){type, slot};
    scene->event_count++;
}

bool wm_sd_scene_take_event(WmSdScene *scene, WmSdEvent *event) {
    if (!scene || !event || !scene->event_count) return false;
    *event = scene->events[scene->event_first];
    scene->event_first = (scene->event_first + 1) % SD_EVENT_CAPACITY;
    scene->event_count--;
    return true;
}

static WmLayout *load_layout(const char *root, const char *relative) {
    char path[SD_PATH_CAPACITY];
    int length = snprintf(path, sizeof(path), "%s/%s", root, relative);
    if (length < 0 || length >= (int)sizeof(path)) return NULL;
    char error[160] = {0};
    WmLayout *layout = wm_layout_load_json(path, error, sizeof(error));
    if (!layout) {
        fprintf(stderr, "Could not load SD layout %s: %s\n", relative, error);
    }
    return layout;
}

static bool prepare_wide_grid(WmLayout *grid) {
    static const char *const picture_targets[] = {
        "Picture_00", "Picture_01", "Picture_02", "Picture_03", "Picture_04"
    };
    static const char *const edge_targets[] = {
        "Edge0", "Edge1", "Edge2", "Edge3", "Edge4"
    };
    for (size_t index = 0; index < 5; index++) {
        if (!wm_layout_copy_texture_map(grid, "ChangeTex16x9",
                                        picture_targets[index], 0) ||
            !wm_layout_copy_texture_map(grid, "Picture_16",
                                        edge_targets[index], 0)) return false;
    }
    return true;
}

WmSdScene *wm_sd_scene_create(WmPlatform *platform,
                               const char *assets_directory,
                               WmTextureCache *textures,
                               WmFontCache *fonts) {
    if (!platform || !assets_directory || !assets_directory[0] ||
        !textures || !fonts) return NULL;
    WmSdScene *scene = calloc(1, sizeof(*scene));
    if (!scene) return NULL;
    scene->platform = platform;
    scene->textures = textures;
    scene->fonts = fonts;
    int root_length = snprintf(scene->assets_directory,
                               sizeof(scene->assets_directory), "%s",
                               assets_directory);
    if (root_length < 0 || root_length >=
        (int)sizeof(scene->assets_directory)) {
        free(scene);
        return NULL;
    }
    scene->grid = load_layout(assets_directory,
                              "layouts/sdChanSel/mn_SdcardMenu_a.json");
    scene->footer = load_layout(assets_directory,
                                "layouts/sdButton/mn_SdcardMenu_b.json");
    scene->empty_tile = load_layout(assets_directory,
                                    "layouts/sdChanSel/mn_SdcardMenu_d.json");
    for (size_t index = 0; index < 3; index++) {
        scene->page_labels[index] = load_layout(assets_directory,
                         "layouts/sdChanSel/mn_SdcardMenu_Page.json");
    }
    scene->focus_layout = load_layout(assets_directory,
                           "layouts/sdChanSel/my_IplTop_d.json");
    scene->balloon = load_layout(assets_directory,
                      "layouts/sdChanSel/my_IplTopBalloon_a.json");
    scene->dialog = load_layout(assets_directory,
                     "layouts/dlgWdw/my_DialogWindow_a2.json");
    scene->wait_icon = load_layout(assets_directory,
                        "layouts/sdChanSel/wait_icon.json");
    scene->help_button = load_layout(assets_directory,
                          "layouts/sdChanSel/help_Btn.json");
    scene->loading_panel = load_layout(assets_directory,
                            "layouts/sdChanSel/mn_Nocard.json");
    scene->error_panel = load_layout(assets_directory,
                          "layouts/sdChanSel/mn_Nocard.json");
    if (!scene->grid || !scene->footer || !scene->empty_tile ||
        !scene->balloon || !scene->dialog || !scene->wait_icon ||
        !scene->help_button || !scene->loading_panel || !scene->error_panel ||
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
        scene->grid, scene->footer, scene->empty_tile,
        scene->page_labels[0], scene->page_labels[1], scene->page_labels[2],
        scene->focus_layout, scene->balloon,
        scene->dialog, scene->wait_icon, scene->help_button,
        scene->loading_panel, scene->error_panel
    };
    for (size_t index = 0; index < sizeof(layouts) / sizeof(layouts[0]); index++) {
        wm_layout_prepare_materials(platform, layouts[index]);
    }
    scene->phase = WM_SD_CLOSED;
    scene->card_ready = true;
    wm_arrow_interaction_init(&scene->arrows,
        (WmArrowInteractionConfig){
            .focus_in_frames = 13.0f,
            .focus_out_frames = 13.0f,
            .press_frames = 28.0f,
            .visibility_frames = 11.0f,
            .clear_focus_out_at_end = true,
            .clear_press_at_end = true
        });
    scene->help_press = -1.0f;
    return scene;
}

void wm_sd_scene_destroy(WmSdScene *scene) {
    if (!scene) return;
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
    if (!id || strlen(id) != 16) return false;
    for (size_t index = 0; index < 16; index++) {
        char digit = id[index];
        if (!((digit >= '0' && digit <= '9') ||
              (digit >= 'a' && digit <= 'f') ||
              (digit >= 'A' && digit <= 'F'))) return false;
    }
    return true;
}

bool wm_sd_scene_set_channels(WmSdScene *scene,
                               const WmSdChannel *channels, size_t count) {
    if (!scene || count > WM_SD_SLOT_COUNT || (count && !channels)) return false;
    SdChannel *next = calloc(WM_SD_SLOT_COUNT, sizeof(*next));
    if (!next) return false;
    bool valid = true;
    for (size_t index = 0; index < count; index++) {
        unsigned slot = channels[index].slot;
        if (slot >= WM_SD_SLOT_COUNT ||
            !valid_title_id(channels[index].title_id) || next[slot].icon) {
            valid = false;
            break;
        }
        char normalized[17];
        for (size_t digit = 0; digit < 16; digit++) {
            normalized[digit] = (char)tolower(
                (unsigned char)channels[index].title_id[digit]);
        }
        normalized[16] = '\0';
        for (size_t previous = 0; previous < WM_SD_SLOT_COUNT; previous++) {
            if (next[previous].icon &&
                strcmp(next[previous].id, normalized) == 0) {
                valid = false;
                break;
            }
        }
        if (!valid) break;
        char relative[128];
        snprintf(relative, sizeof(relative),
                 "channel-layouts/%s/icon/icon.json", normalized);
        next[slot].icon = load_layout(scene->assets_directory, relative);
        if (!next[slot].icon) {
            valid = false;
            break;
        }
        memcpy(next[slot].id, normalized,
               sizeof(next[slot].id));
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
    if (!scene || slot >= WM_SD_SLOT_COUNT ||
        !scene->channels[slot].icon) return NULL;
    return scene->channels[slot].id;
}

void wm_sd_scene_set_messages(WmSdScene *scene, WmSdMessageProvider provider,
                               void *context) {
    if (!scene) return;
    scene->message_provider = provider;
    scene->message_context = context;
}

void wm_sd_scene_set_card_ready(WmSdScene *scene, bool ready) {
    if (scene) scene->card_ready = ready;
}

static const char *fallback_message(unsigned identifier) {
    switch (identifier) {
        case 157: return "Welcome to the SD Card Menu.";
        case 158: return "Channels stored on an SD Card appear here.";
        case 159: return "Select a channel to view it.";
        case 160: return "SD Card Menu";
        case 163: return "Next";
        case 164: return "Close";
        case 165: return "Back";
        case 166: return "SD Card Menu Help";
        case 168: return "Wii Menu";
        case 169: return "Nothing is inserted in the SD Card Slot.";
        case 170: return "Checking the SD Card...";
        case 171: return "The inserted device cannot be used.";
        case 195: return "An SD Card process failed.";
        case 201: return "About the SD Card Menu";
        case 202: return "You can open this guide again with Help.";
        default: return "";
    }
}

static const char *message(WmSdScene *scene, unsigned identifier) {
    const char *provided = scene->message_provider
                               ? scene->message_provider(scene->message_context,
                                                         identifier)
                               : NULL;
    return provided ? provided : fallback_message(identifier);
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
    if (!scene || page >= WM_SD_PAGE_COUNT ||
        media_status < WM_SD_MEDIA_READY ||
        media_status > WM_SD_MEDIA_UNSUPPORTED) return false;
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
        &scene->arrows,
        media_status == WM_SD_MEDIA_READY && page > 0,
        media_status == WM_SD_MEDIA_READY && page + 1 < WM_SD_PAGE_COUNT,
        11.0f);
    scene->help_press = -1.0f;
    scene->event_first = scene->event_count = 0;
    return true;
}

void wm_sd_scene_reset(WmSdScene *scene) {
    if (!scene) return;
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
    return !scene || scene->phase != WM_SD_ACTIVE ||
           scene->welcome_pending ||
           scene->loader_phase != SD_LOADER_CLOSED ||
           (scene->dialog_phase != SD_DIALOG_CLOSED &&
            scene->dialog_phase != SD_DIALOG_IDLE);
}

static void advance_loader(WmSdScene *scene, float frames) {
    if (scene->loader_phase == SD_LOADER_CLOSED) return;
    bool was_waiting = scene->loader_phase == SD_LOADER_WAIT;
    float previous_wait = was_waiting ? scene->loader_frame : 0.0f;
    scene->loader_frame += frames;
    if (scene->loader_phase == SD_LOADER_ENTER &&
        scene->loader_frame >= 33.0f) {
        scene->loader_frame -= 33.0f;
        scene->loader_phase = SD_LOADER_WAIT;
    }
    float minimum = scene->media_status == WM_SD_MEDIA_READY &&
                    scene->populated_count ? 60.0f : 0.0f;
    if (scene->loader_phase == SD_LOADER_WAIT &&
        scene->loader_frame >= minimum && scene->card_ready) {
        scene->loader_frame = was_waiting && previous_wait >= minimum
                                  ? 0.0f : scene->loader_frame - minimum;
        scene->loader_phase = SD_LOADER_EXIT;
    }
    if (scene->loader_phase == SD_LOADER_EXIT &&
        scene->loader_frame >= 16.0f) {
        scene->loader_phase = SD_LOADER_CLOSED;
        scene->loader_frame = 0.0f;
    }
}

static float dialog_duration(SdDialogPhase phase) {
    switch (phase) {
        case SD_DIALOG_ENTER: return 25.0f;
        case SD_DIALOG_SELECT: return 21.0f;
        case SD_DIALOG_TEXT_OUT:
        case SD_DIALOG_TEXT_IN: return 10.0f;
        case SD_DIALOG_EXIT: return 21.0f;
        default: return 0.0f;
    }
}

static void advance_dialog(WmSdScene *scene, float frames) {
    if (scene->dialog_phase == SD_DIALOG_CLOSED) return;
    if (scene->dialog_phase == SD_DIALOG_IDLE) {
        scene->icon_age += frames;
        return;
    }
    scene->dialog_frame += frames;
    while (scene->dialog_phase != SD_DIALOG_CLOSED &&
           scene->dialog_phase != SD_DIALOG_IDLE &&
           scene->dialog_frame >= dialog_duration(scene->dialog_phase)) {
        scene->dialog_frame -= dialog_duration(scene->dialog_phase);
        switch (scene->dialog_phase) {
            case SD_DIALOG_ENTER:
                scene->dialog_phase = SD_DIALOG_IDLE;
                break;
            case SD_DIALOG_SELECT: {
                unsigned count = scene->welcome_active ? 4 : 3;
                if (scene->dialog_destination < 0 ||
                    scene->dialog_destination >= (int)count) {
                    scene->dialog_phase = SD_DIALOG_EXIT;
                } else {
                    scene->dialog_phase = SD_DIALOG_TEXT_OUT;
                    /* The selected button resumes its hover entrance while
                     * the page text fades, as the source help controller does. */
                    if (scene->hover.control == scene->dialog_selected) {
                        scene->button_focus[scene->dialog_selected] =
                            (SdFocus){.active = true, .entering = true,
                                      .frame = scene->dialog_frame};
                    }
                    scene->dialog_selected = WM_SD_CONTROL_NONE;
                }
                break;
            }
            case SD_DIALOG_TEXT_OUT:
                scene->dialog_page = (unsigned)scene->dialog_destination;
                scene->icon_age = 0.0f;
                scene->dialog_phase = SD_DIALOG_TEXT_IN;
                break;
            case SD_DIALOG_TEXT_IN:
                scene->dialog_phase = SD_DIALOG_IDLE;
                break;
            case SD_DIALOG_EXIT:
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
                break;
            default:
                break;
        }
    }
}

static void advance_focus(SdFocus *focus, float frames, float end) {
    if (!focus->active) return;
    focus->frame = frame_clamp(focus->frame + frames, end);
    if (!focus->entering && focus->frame >= end) focus->active = false;
}

void wm_sd_scene_advance(WmSdScene *scene, float frames, bool revealing) {
    if (!scene || scene->phase == WM_SD_CLOSED || !isfinite(frames) ||
        frames < 0.0f) return;
    scene->age += frames;
    advance_loader(scene, frames);
    for (size_t index = 0; index <
         sizeof(scene->button_focus) / sizeof(scene->button_focus[0]); index++) {
        if (index == WM_SD_CONTROL_PREVIOUS ||
            index == WM_SD_CONTROL_NEXT) continue;
        advance_focus(&scene->button_focus[index], frames, 9.0f);
    }
    advance_dialog(scene, frames);
    if (scene->welcome_pending && !revealing) {
        scene->welcome_pending = false;
        start_help(scene, true);
    }
    for (size_t index = 0; index < WM_SD_SLOT_COUNT; index++) {
        SdFocus *focus = &scene->tile_focus[index];
        if (!focus->active) continue;
        focus->frame += frames;
        if (focus->entering && focus->frame >= 5.0f &&
            focus->pending_leave) {
            focus->entering = false;
            focus->frame = 0.0f;
        }
        if (!focus->entering && focus->frame >= 30.0f) {
            focus->active = false;
        } else if (focus->entering) {
            focus->frame = frame_clamp(focus->frame, 5.0f);
        }
    }
    if (scene->help_press >= 0.0f) {
        scene->help_press += frames;
        if (scene->help_press >= 21.0f) scene->help_press = -1.0f;
    }
    wm_arrow_interaction_advance(&scene->arrows, frames);
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
            balloon->frame = frame_clamp(balloon->frame + frames, 6.0f);
        } else if (balloon->phase == SD_BALLOON_LEAVE) {
            balloon->frame = fmaxf(0.0f, balloon->frame - frames);
            if (balloon->frame == 0.0f)
                balloon->phase = SD_BALLOON_CLOSED;
        }
    }
    if (scene->phase == WM_SD_SCROLL) {
        scene->scroll_frame += frames;
        if (scene->scroll_frame >= 20.0f) {
            scene->page = (unsigned)((int)scene->page +
                                     scene->scroll_direction);
            scene->phase = WM_SD_ACTIVE;
            scene->scroll_frame = 0.0f;
            scene->scroll_direction = 0;
            enqueue(scene, WM_SD_EVENT_PAGE_CHANGED, scene->page);
            bool arrows[2] = {
                scene->media_status == WM_SD_MEDIA_READY && scene->page > 0,
                scene->media_status == WM_SD_MEDIA_READY &&
                scene->page + 1 < WM_SD_PAGE_COUNT
            };
            for (size_t index = 0; index < 2; index++) {
                if (wm_arrow_interaction_set_visible(&scene->arrows,
                                                      (int)index,
                                                      arrows[index])) {
                    if (!arrows[index] &&
                        scene->hover.control ==
                            (index == 0 ? WM_SD_CONTROL_PREVIOUS :
                                          WM_SD_CONTROL_NEXT)) {
                        wm_sd_scene_hover(scene,
                            (WmSdHit){WM_SD_CONTROL_NONE, 0});
                    }
                }
            }
        }
    }
}

typedef struct SdTraversal {
    unsigned page;
    SdTile tiles[SD_VISIBLE_TILES];
    float page_anchor[3][12];
    bool has_page_anchor[3];
} SdTraversal;

static bool collect_page_anchor(void *context,
                                const WmLayoutPaneView *pane) {
    SdTraversal *traversal = context;
    const char *name = pane->name;
    if (strncmp(name, "N_Clock", 7) == 0 &&
        name[7] >= '0' && name[7] <= '2' && name[8] == '\0') {
        unsigned index = (unsigned)(name[7] - '0');
        memcpy(traversal->page_anchor[index], pane->matrix,
               sizeof(traversal->page_anchor[index]));
        traversal->has_page_anchor[index] = true;
    }
    return true;
}

static bool collect_grid_pane(void *context, const WmLayoutPaneView *pane) {
    SdTraversal *traversal = context;
    const char *name = pane->name;
    if (strncmp(name, "N_Ch_", 5) != 0 ||
        name[5] < 'a' || name[5] > 'e' ||
        name[6] < '0' || name[6] > '9' ||
        name[7] < '0' || name[7] > '9' || name[8] != '\0') return true;
    unsigned group = (unsigned)(name[5] - 'a');
    unsigned relative_slot = (unsigned)(name[6] - '0') * 10u +
                             (unsigned)(name[7] - '0');
    if (relative_slot == 0 || relative_slot > WM_SD_SLOTS_PER_PAGE) return true;
    int page = (int)traversal->page + (int)group - 2;
    if (page < 0 || page >= WM_SD_PAGE_COUNT) return true;
    unsigned position = group * WM_SD_SLOTS_PER_PAGE + relative_slot - 1;
    SdTile *tile = &traversal->tiles[position];
    const float scale_x = (float)WM_FRAME_WIDTH / 832.0f;
    const float width = 170.0f * scale_x;
    const float center_x = WM_FRAME_WIDTH * 0.5f +
                           pane->matrix[3] * scale_x;
    const float center_y = WM_FRAME_HEIGHT * 0.5f - pane->matrix[7];
    tile->valid = center_x + width * 0.5f > 0.0f &&
                  center_x - width * 0.5f < WM_FRAME_WIDTH &&
                  center_y + 48.0f > 0.0f &&
                  center_y - 48.0f < WM_FRAME_HEIGHT;
    tile->absolute_slot = (unsigned)page * WM_SD_SLOTS_PER_PAGE +
                          relative_slot - 1;
    memcpy(tile->matrix, pane->matrix, sizeof(tile->matrix));
    tile->clip = (WmClipRect){center_x - width * 0.5f,
                              center_y - 48.0f, width, 96.0f};
    return true;
}

static bool grid_base_filter(void *context, const WmLayoutPaneView *pane) {
    (void)context;
    if (strcmp(pane->name, "RootPane") == 0 ||
        strcmp(pane->name, "N_Ch") == 0 ||
        strcmp(pane->name, "N_ChAll") == 0) return true;
    return strncmp(pane->name, "BaseMask", 8) == 0;
}

static bool grid_trim_filter(void *context, const WmLayoutPaneView *pane) {
    const WmSdScene *scene = context;
    if (strncmp(pane->name, "BaseMask", 8) == 0 ||
        strcmp(pane->name, "ChMask") == 0) return false;
    if (strncmp(pane->name, "Edge", 4) == 0 &&
        pane->name[4] >= '0' && pane->name[4] <= '4' &&
        pane->name[5] == '\0') {
        int page = (int)scene->page + pane->name[4] - '2';
        return page >= 0 && page < WM_SD_PAGE_COUNT;
    }
    return true;
}

static bool footer_background_filter(void *context,
                                      const WmLayoutPaneView *pane) {
    (void)context;
    return strcmp(pane->name, "RootPane") == 0 ||
           strcmp(pane->name, "background") == 0;
}

static bool footer_front_filter(void *context,
                                 const WmLayoutPaneView *pane) {
    (void)context;
    return strcmp(pane->name, "background") != 0;
}

static void present(WmSdScene *scene, const WmLayout *layout,
                    WmLayoutMode mode, const float matrix[12]) {
    wm_layout_present_with_fonts(scene->platform, scene->textures,
                                  scene->fonts, layout, true, mode, matrix);
}

static void translation_matrix(float x, float y, float matrix[12]) {
    static const float identity[12] = {
        1, 0, 0, 0,
        0, 1, 0, 0,
        0, 0, 1, 0
    };
    memcpy(matrix, identity, sizeof(identity));
    matrix[3] = x;
    matrix[7] = y;
}

static void add_clip(WmLayoutClip clips[SD_CLIP_CAPACITY], size_t *count,
                     const char *animation, const char *group,
                     float frame, bool loop) {
    if (*count >= SD_CLIP_CAPACITY) return;
    clips[*count] = (WmLayoutClip){
        .animation = animation,
        .frame = frame,
        .group = group,
        .loop_override = loop ? 1 : 0
    };
    (*count)++;
}

static void pose_footer(WmSdScene *scene) {
    WmLayoutClip clips[SD_CLIP_CAPACITY];
    size_t count = 0;
    add_clip(clips, &count,
             "mn_SdcardMenu_b_Arw_wating_roop", "G_ArwRoop",
             fmodf(scene->age, 55.0f), true);
    const struct {
        WmSdControl control;
        const char *name;
        const char *group;
    } buttons[] = {
        {WM_SD_CONTROL_BACK, "Wiimenu", "G_BL"},
        {WM_SD_CONTROL_HELP, "Help", "G_BR"}
    };
    char names[8][64];
    size_t name_count = 0;
    for (size_t index = 0; index < 2; index++) {
        SdFocus *focus = &scene->button_focus[buttons[index].control];
        snprintf(names[name_count], sizeof(names[name_count]),
                 "mn_SdcardMenu_b_Btn_%s_%s", buttons[index].name,
                 focus->active && !focus->entering ? "rollout" : "rollover");
        add_clip(clips, &count, names[name_count++], buttons[index].group,
                 focus->active ? focus->frame : 0.0f, false);
    }
    for (size_t index = 0; index < 2; index++) {
        const char side = index == 0 ? 'L' : 'R';
        snprintf(names[name_count], sizeof(names[name_count]),
                 "mn_SdcardMenu_b_Arw%c_%s", side,
                 wm_arrow_interaction_side(&scene->arrows,
                                            (int)index)->visible ? "in" : "out");
        /* Group names are needed until pose returns, so retain them below. */
        static const char *const end_groups[] = {
            "G_ArwL_End", "G_ArwR_End"
        };
        add_clip(clips, &count, names[name_count++], end_groups[index],
                 wm_arrow_interaction_side(&scene->arrows,
                                            (int)index)->visibility_age, false);
        const WmArrowSideState *focus = wm_arrow_interaction_side(
            &scene->arrows, (int)index);
        static const char *const focus_groups[] = {
            "G_ArwL_Focus", "G_ArwR_Focus"
        };
        snprintf(names[name_count], sizeof(names[name_count]),
                 "mn_SdcardMenu_b_Arw%c_%s", side,
                 focus->focus_active && !focus->focus_entering
                     ? "rollout" : "rollover");
        add_clip(clips, &count, names[name_count++], focus_groups[index],
                 focus->focus_active ? focus->focus_age : 0.0f, false);
        if (focus->pressed) {
            static const char *const press_names[] = {
                "mn_SdcardMenu_b_ArwL_on", "mn_SdcardMenu_b_ArwR_on"
            };
            static const char *const press_groups[] = {
                "G_ArwL_Ac", "G_ArwR_Ac"
            };
            add_clip(clips, &count, press_names[index], press_groups[index],
                     focus->press_age, false);
        }
    }
    if (scene->help_press >= 0.0f) {
        add_clip(clips, &count,
                 "mn_SdcardMenu_b_Btn_Help_on", "G_BR",
                 scene->help_press, false);
    }
    wm_layout_pose(scene->footer, clips, count);
    wm_layout_set_pose_text(scene->footer, "T_page", message(scene, 160));
}

static void draw_tiles(WmSdScene *scene, const SdTraversal *traversal) {
    const WmChannelAnimationOptions animation_options = {
        .language = "ENG",
        .measure_text = wm_font_cache_measure_text,
        .measure_context = scene->fonts
    };
    WmLayoutClip empty_animation = {
        .animation = "mn_SdcardMenu_d",
        .frame = fmodf(scene->age, 1999.0f),
        .loop_override = 1
    };
    wm_layout_pose(scene->empty_tile, &empty_animation, 1);
    for (size_t index = 0; index < SD_VISIBLE_TILES; index++) {
        const SdTile *tile = &traversal->tiles[index];
        if (!tile->valid) continue;
        SdChannel *channel = &scene->channels[tile->absolute_slot];
        WmLayout *layout = channel->icon ? channel->icon : scene->empty_tile;
        if (channel->icon) {
            wm_channel_animation_pose(channel->icon, channel->id,
                                       WM_CHANNEL_ICON, scene->age,
                                       &animation_options);
        }
        wm_platform_set_clip(scene->platform, &tile->clip);
        present(scene, layout, WM_LAYOUT_EMBEDDED, tile->matrix);
        wm_platform_set_clip(scene->platform, NULL);
    }
    for (size_t tile_index = 0; tile_index < SD_VISIBLE_TILES;
         tile_index++) {
        const SdTile *tile = &traversal->tiles[tile_index];
        if (!tile->valid) continue;
        const SdFocus *focus = &scene->tile_focus[tile->absolute_slot];
        if (!focus->active) continue;
        WmLayoutClip clip = {
            .animation = focus->entering ? "my_IplTop_d_FocusOn" :
                                           "my_IplTop_d_FocusOff",
            .frame = focus->frame,
            .loop_override = 0
        };
        wm_layout_pose(scene->focus_layout, &clip, 1);
        wm_platform_set_clip(scene->platform, &tile->clip);
        present(scene, scene->focus_layout, WM_LAYOUT_EMBEDDED,
                tile->matrix);
        wm_platform_set_clip(scene->platform, NULL);
    }
}

static void draw_page_labels(WmSdScene *scene,
                              const SdTraversal *traversal) {
    for (size_t index = 0; index < 3; index++) {
        if (!traversal->has_page_anchor[index]) continue;
        WmLayout *label = scene->page_labels[index];
        wm_layout_pose(label, NULL, 0);
        char page[8];
        snprintf(page, sizeof(page), "%u", scene->page + (unsigned)index);
        wm_layout_set_pose_text(label, "TextBox_00", page);
        wm_layout_set_pose_text(label, "T_Page00", "/20");
        float position[12];
        translation_matrix(traversal->page_anchor[index][3],
                           traversal->page_anchor[index][7], position);
        present(scene, label, WM_LAYOUT_IPL, position);
    }
}

typedef struct SdPaneSearch {
    const char *name;
    bool found;
    float matrix[12];
} SdPaneSearch;

static bool locate_pane(void *context, const WmLayoutPaneView *pane) {
    SdPaneSearch *search = context;
    if (strcmp(pane->name, search->name) == 0) {
        memcpy(search->matrix, pane->matrix, sizeof(search->matrix));
        search->found = true;
    }
    return true;
}

static bool pane_matrix(const WmLayout *layout, const char *name,
                        float matrix[12]) {
    SdPaneSearch search = {.name = name};
    WmLayoutDrawOptions options = {
        .wide = true, .mode = WM_LAYOUT_IPL, .alpha = 1.0f,
        .on_pane = locate_pane, .context = &search
    };
    wm_layout_draw(layout, &options);
    if (search.found) memcpy(matrix, search.matrix, sizeof(search.matrix));
    return search.found;
}

static void draw_balloon(WmSdScene *scene, WmSdControl control,
                         float frame) {
    const char *pane_name = control == WM_SD_CONTROL_BACK
                                ? "B_Wiimenu" : "B_Help";
    float anchor[12];
    if (!pane_matrix(scene->footer, pane_name, anchor)) return;
    unsigned identifier = control == WM_SD_CONTROL_BACK
                              ? 168 : 166;
    const char *title = message(scene, identifier);
    WmLayoutPaneState text_pane;
    float title_width = 0.0f;
    if (wm_layout_pane_state(scene->balloon, "T_Balloon", &text_pane)) {
        title_width = wm_font_cache_measure_text(scene->fonts, scene->balloon,
                                                   &text_pane, title,
                                                   strlen(title));
    }
    float width = fmaxf(160.0f * (832.0f / 608.0f), title_width + 40.0f);
    float x = fmaxf(-416.0f + 120.0f + width * 0.5f,
                    fminf(416.0f - 120.0f - width * 0.5f, anchor[3]));
    float matrix[12];
    translation_matrix(x * (832.0f / 608.0f),
                       anchor[7] + 50.0f, matrix);
    WmLayoutClip clip = {
        .animation = "my_IplTopBalloon_a_BalloonInOut",
        .frame = frame_clamp(frame, 6.0f),
        .loop_override = 0
    };
    wm_layout_pose(scene->balloon, &clip, 1);
    wm_layout_set_pane_size(scene->balloon, "W_Base", width, 48.0f);
    wm_layout_set_pane_size(scene->balloon, "W_Shade", width, 48.0f);
    wm_layout_set_pose_text(scene->balloon, "T_Balloon", title);
    present(scene, scene->balloon, WM_LAYOUT_IPL, matrix);
}

static unsigned help_message_id(const WmSdScene *scene) {
    static const unsigned visited_pages[] = {201, 158, 159};
    static const unsigned welcome_pages[] = {157, 158, 159, 202};
    if (scene->welcome_active) return welcome_pages[scene->dialog_page];
    return visited_pages[scene->dialog_page];
}

static void draw_help(WmSdScene *scene) {
    if (scene->dialog_phase == SD_DIALOG_CLOSED) return;
    WmLayoutClip clips[SD_CLIP_CAPACITY];
    size_t count = 0;
    add_clip(clips, &count, "my_DialogWindow_a2_DialogIn", "G_InOut",
             scene->dialog_phase == SD_DIALOG_ENTER
                 ? scene->dialog_frame : 25.0f, false);
    const WmSdControl controls[2] = {
        WM_SD_CONTROL_HELP_BACK, WM_SD_CONTROL_HELP_NEXT
    };
    for (size_t index = 0; index < 2; index++) {
        SdFocus *focus = &scene->button_focus[controls[index]];
        if (!focus->active) continue;
        add_clip(clips, &count,
                 focus->entering ? "my_DialogWindow_a2_FocusBtn_on" :
                                   "my_DialogWindow_a2_FocusBtn_off",
                 index == 0 ? "G_FocusBtnA" : "G_FocusBtnB",
                 focus->frame, false);
    }
    if (scene->dialog_selected != WM_SD_CONTROL_NONE) {
        add_clip(clips, &count,
                 "my_DialogWindow_a2_SelectBtn_Ac",
                 scene->dialog_selected == WM_SD_CONTROL_HELP_BACK
                     ? "G_SelectBtnA" : "G_SelectBtnB",
                 scene->dialog_phase == SD_DIALOG_SELECT
                     ? scene->dialog_frame : 21.0f, false);
    }
    if (scene->dialog_phase == SD_DIALOG_EXIT) {
        add_clip(clips, &count,
                 "my_DialogWindow_a2_DialogOut", "G_InOut",
                 scene->dialog_frame, false);
    }
    wm_layout_pose(scene->dialog, clips, count);
    float text_alpha = scene->dialog_phase == SD_DIALOG_TEXT_OUT
                           ? fmaxf(0.0f,
                                   255.0f - floorf(scene->dialog_frame) * 26.0f)
                           : scene->dialog_phase == SD_DIALOG_TEXT_IN
                                 ? fminf(255.0f,
                                         floorf(scene->dialog_frame) * 26.0f)
                                 : 255.0f;
    wm_layout_set_pose_text(scene->dialog, "T_Dialog",
                             message(scene, help_message_id(scene)));
    wm_layout_set_pane_alpha(scene->dialog, "T_Dialog", text_alpha);
    wm_layout_set_pose_text(scene->dialog, "T_BtnA", message(scene, 165));
    unsigned last = scene->welcome_active ? 3 : 2;
    wm_layout_set_pose_text(scene->dialog, "T_BtnB",
                             message(scene,
                                     scene->dialog_page == last ? 164 : 163));
    bool changing_text = scene->dialog_phase == SD_DIALOG_TEXT_OUT ||
                         scene->dialog_phase == SD_DIALOG_TEXT_IN;
    if (changing_text &&
        (scene->dialog_previous_page == last) !=
            (scene->dialog_destination == (int)last)) {
        wm_layout_set_pane_alpha(scene->dialog, "T_BtnB", text_alpha);
    }
    if (scene->welcome_active) {
        /* Back fades in on the first boundary. Keep it visible on later
         * pages, including text-in frame zero, so it cannot blink. */
        bool show_back = scene->dialog_page > 0 &&
                         !(scene->dialog_phase == SD_DIALOG_TEXT_IN &&
                           text_alpha == 0.0f &&
                           scene->dialog_previous_page == 0);
        wm_layout_set_pane_visible(scene->dialog, "N_BtnA", show_back);
        if (changing_text &&
            (scene->dialog_previous_page == 0) !=
                (scene->dialog_destination == 0)) {
            wm_layout_set_descendant_alpha(scene->dialog,
                                            "N_BtnA_Pic", text_alpha);
        }
    }
    present(scene, scene->dialog, WM_LAYOUT_IPL, NULL);
    if (scene->dialog_page != 2 &&
        !(scene->welcome_active && scene->dialog_page == 3)) return;
    float dialog_matrix[12];
    if (!pane_matrix(scene->dialog, "N_Dialog", dialog_matrix)) return;
    bool final_welcome = scene->welcome_active && scene->dialog_page == 3;
    float matrix[12];
    translation_matrix(dialog_matrix[3],
                       dialog_matrix[7] + (final_welcome ? 108.0f : 74.0f),
                       matrix);
    WmLayout *icon = final_welcome ? scene->help_button : scene->wait_icon;
    if (final_welcome) wm_layout_pose(icon, NULL, 0);
    else {
        WmLayoutClip clip = {
            .animation = "wait_icon_wait_loop",
            .frame = fmodf(scene->icon_age, 40.0f),
            .loop_override = 1
        };
        wm_layout_pose(icon, &clip, 1);
    }
    if (changing_text) {
        wm_layout_set_descendant_alpha(icon, "RootPane", text_alpha);
    }
    present(scene, icon, WM_LAYOUT_IPL, matrix);
}

static void draw_loading(WmSdScene *scene) {
    if (scene->loader_phase == SD_LOADER_CLOSED) return;
    WmLayoutClip clips[3];
    size_t count = 0;
    add_clip(clips, &count, "mn_Nocard_IN_02", "Group_01",
             scene->loader_phase == SD_LOADER_ENTER
                 ? scene->loader_frame : 33.0f, false);
    if (scene->loader_phase == SD_LOADER_WAIT) {
        add_clip(clips, &count, "mn_Nocard_Wait", "G_Wait",
                 fmaxf(0.0f, scene->loader_frame - 1.0f), true);
    }
    if (scene->loader_phase == SD_LOADER_EXIT) {
        add_clip(clips, &count, "mn_Nocard_OUT_02", "Group_01",
                 scene->loader_frame, false);
    }
    wm_layout_pose(scene->loading_panel, clips, count);
    wm_layout_set_pose_text(scene->loading_panel, "T_TimerMes_01",
                             message(scene, 170));
    present(scene, scene->loading_panel, WM_LAYOUT_IPL, NULL);
}

static void draw_media_error(WmSdScene *scene) {
    if (scene->media_status == WM_SD_MEDIA_READY ||
        scene->loader_phase != SD_LOADER_CLOSED ||
        scene->dialog_phase != SD_DIALOG_CLOSED) return;
    unsigned identifier = scene->media_status == WM_SD_MEDIA_ABSENT ? 169 :
                          scene->media_status == WM_SD_MEDIA_READ_ERROR ? 195 :
                          171;
    WmLayoutClip clip = {
        .animation = "mn_Nocard_IN",
        .frame = 33.0f,
        .group = "Group_00",
        .loop_override = 0
    };
    wm_layout_pose(scene->error_panel, &clip, 1);
    wm_layout_set_pose_text(scene->error_panel, "T_TimerMes",
                             message(scene, identifier));
    wm_layout_set_pane_visible(scene->error_panel, "T_TimerMes_01", false);
    wm_layout_set_pane_visible(scene->error_panel, "Wait", false);
    present(scene, scene->error_panel, WM_LAYOUT_IPL, NULL);
}

void wm_sd_scene_draw(WmSdScene *scene) {
    if (!scene || scene->phase == WM_SD_CLOSED) return;
    pose_footer(scene);
    wm_layout_present_filtered_with_fonts(
        scene->platform, scene->textures, scene->fonts,
        scene->footer, true, WM_LAYOUT_IPL, NULL,
        footer_background_filter, NULL);
    WmLayoutClip grid_clip = {
        .animation = "mn_SdcardMenu_a",
        .frame = scene->phase == WM_SD_SCROLL
                     ? wm_sd_scroll_animation_frame(scene->scroll_direction,
                                                     scene->scroll_frame)
                     : 0.0f,
        .loop_override = 0
    };
    wm_layout_pose(scene->grid, &grid_clip, 1);
    SdTraversal traversal = {.page = scene->page};
    /* The incoming page label follows N_Clock2, whose visible bit is clear
     * in the authored grid. HTML's sourceAnchorMatrices also visits it. */
    wm_layout_visit_all_transforms(scene->grid, true, WM_LAYOUT_IPL,
                                   NULL, collect_page_anchor, &traversal);
    WmLayoutDrawOptions options = {
        .wide = true, .mode = WM_LAYOUT_IPL, .alpha = 1.0f,
        .on_pane = collect_grid_pane, .context = &traversal
    };
    wm_layout_draw(scene->grid, &options);
    wm_layout_present_filtered_with_fonts(
        scene->platform, scene->textures, scene->fonts,
        scene->grid, true, WM_LAYOUT_IPL, NULL,
        grid_base_filter, NULL);
    draw_tiles(scene, &traversal);
    wm_layout_present_filtered_with_fonts(
        scene->platform, scene->textures, scene->fonts,
        scene->grid, true, WM_LAYOUT_IPL, NULL,
        grid_trim_filter, scene);
    draw_page_labels(scene, &traversal);
    wm_layout_present_filtered_with_fonts(
        scene->platform, scene->textures, scene->fonts,
        scene->footer, true, WM_LAYOUT_IPL, NULL,
        footer_front_filter, NULL);
    for (size_t index = 0; index < 2; index++) {
        SdBalloon *balloon = &scene->balloons[index];
        if (balloon->phase == SD_BALLOON_ENTER ||
            balloon->phase == SD_BALLOON_LEAVE) {
            draw_balloon(scene, index == 0 ? WM_SD_CONTROL_BACK :
                                            WM_SD_CONTROL_HELP,
                         balloon->frame);
        }
    }
    draw_help(scene);
    draw_loading(scene);
    draw_media_error(scene);
}

static bool in_rectangle(WmSourceRect rectangle, int x, int y,
                         float margin) {
    return x >= rectangle.x - margin &&
           x <= rectangle.x + rectangle.width + margin &&
           y >= rectangle.y - margin &&
           y <= rectangle.y + rectangle.height + margin;
}

static bool footer_hit(WmSdScene *scene, const char *pane_name,
                       int x, int y, float margin) {
    WmSourceRect rectangle;
    return wm_source_pane_rect(scene->footer, pane_name, true,
                               WM_LAYOUT_IPL, NULL, &rectangle) &&
           in_rectangle(rectangle, x, y, margin);
}

static bool help_hit(WmSdScene *scene, const char *pane_name,
                     int x, int y) {
    WmSourceRect rectangle;
    return wm_source_pane_rect(scene->dialog, pane_name, true,
                               WM_LAYOUT_IPL, NULL, &rectangle) &&
           in_rectangle(rectangle, x, y, 0.0f);
}

WmSdHit wm_sd_scene_hit(WmSdScene *scene, int x, int y) {
    WmSdHit none = {WM_SD_CONTROL_NONE, 0};
    if (!scene || scene->phase == WM_SD_CLOSED) return none;
    if (scene->dialog_phase != SD_DIALOG_CLOSED) {
        if (scene->dialog_phase != SD_DIALOG_IDLE) return none;
        if (!(scene->welcome_active && scene->dialog_page == 0) &&
            help_hit(scene, "B_BtnA", x, y)) {
            return (WmSdHit){WM_SD_CONTROL_HELP_BACK, 0};
        }
        if (help_hit(scene, "B_BtnB", x, y)) {
            return (WmSdHit){WM_SD_CONTROL_HELP_NEXT, 0};
        }
        return none;
    }
    if (scene->loader_phase != SD_LOADER_CLOSED ||
        scene->welcome_pending || scene->phase == WM_SD_LEAVING) return none;
    pose_footer(scene);
    /* G_ArwRoop shifts the source hit pane by nearly three pixels. Retain
     * an expanded, already-held arrow before testing overlapping tiles. */
    if (scene->hover.control == WM_SD_CONTROL_PREVIOUS &&
        footer_hit(scene, "B_ArwL", x, y, 4.0f)) return scene->hover;
    if (scene->hover.control == WM_SD_CONTROL_NEXT &&
        footer_hit(scene, "B_ArwR", x, y, 4.0f)) return scene->hover;
    if (wm_arrow_interaction_side(&scene->arrows,
                                   WM_ARROW_PREVIOUS)->visible &&
        footer_hit(scene, "B_ArwL", x, y, 0.0f)) {
        return (WmSdHit){WM_SD_CONTROL_PREVIOUS, 0};
    }
    if (wm_arrow_interaction_side(&scene->arrows,
                                   WM_ARROW_NEXT)->visible &&
        footer_hit(scene, "B_ArwR", x, y, 0.0f)) {
        return (WmSdHit){WM_SD_CONTROL_NEXT, 0};
    }
    if (scene->phase == WM_SD_SCROLL) return none;
    if (footer_hit(scene, "B_Wiimenu", x, y, 0.0f)) {
        return (WmSdHit){WM_SD_CONTROL_BACK, 0};
    }
    if (footer_hit(scene, "B_Help", x, y, 0.0f)) {
        return (WmSdHit){WM_SD_CONTROL_HELP, 0};
    }
    if (scene->media_status != WM_SD_MEDIA_READY) return none;
    WmLayoutClip grid_clip = {
        .animation = "mn_SdcardMenu_a",
        .frame = 0.0f,
        .loop_override = 0
    };
    wm_layout_pose(scene->grid, &grid_clip, 1);
    for (unsigned index = 0; index < WM_SD_SLOTS_PER_PAGE; index++) {
        unsigned absolute = scene->page * WM_SD_SLOTS_PER_PAGE + index;
        if (!scene->channels[absolute].icon) continue;
        char name[16];
        snprintf(name, sizeof(name), "N_Ch_c%02u", index + 1);
        WmSourceRect rectangle;
        if (wm_source_pane_rect(scene->grid, name, true,
                                WM_LAYOUT_IPL, NULL, &rectangle) &&
            in_rectangle(rectangle, x, y, 0.0f)) {
            return (WmSdHit){WM_SD_CONTROL_CHANNEL, absolute};
        }
    }
    return none;
}

static bool same_hit(WmSdHit first, WmSdHit second) {
    return first.control == second.control &&
           (first.control != WM_SD_CONTROL_CHANNEL ||
            first.slot == second.slot);
}

static void clear_tile_focus(WmSdScene *scene) {
    for (size_t index = 0; index < WM_SD_SLOT_COUNT; index++) {
        if (!scene->tile_focus[index].active) continue;
        if (scene->tile_focus[index].entering) {
            scene->tile_focus[index].pending_leave = true;
        }
    }
}

static void target_balloon(WmSdScene *scene, WmSdControl control) {
    if (control != WM_SD_CONTROL_BACK && control != WM_SD_CONTROL_HELP)
        control = WM_SD_CONTROL_NONE;
    if (control == scene->balloon_hover) return;
    if (scene->balloon_hover != WM_SD_CONTROL_NONE) {
        size_t old_index = scene->balloon_hover == WM_SD_CONTROL_BACK ? 0 : 1;
        SdBalloon *old = &scene->balloons[old_index];
        old->phase = old->phase == SD_BALLOON_WAIT
                         ? SD_BALLOON_CLOSED : SD_BALLOON_LEAVE;
    }
    scene->balloon_hover = control;
    if (control != WM_SD_CONTROL_NONE) {
        size_t index = control == WM_SD_CONTROL_BACK ? 0 : 1;
        scene->balloons[index] = (SdBalloon){.phase = SD_BALLOON_WAIT};
    }
}

void wm_sd_scene_hover(WmSdScene *scene, WmSdHit hit) {
    if (!scene || scene->phase == WM_SD_CLOSED) return;
    if (scene->dialog_phase != SD_DIALOG_CLOSED) {
        if (scene->dialog_phase != SD_DIALOG_IDLE ||
            (hit.control != WM_SD_CONTROL_HELP_BACK &&
             hit.control != WM_SD_CONTROL_HELP_NEXT) ||
            (scene->welcome_active && scene->dialog_page == 0 &&
             hit.control == WM_SD_CONTROL_HELP_BACK)) {
            hit = (WmSdHit){WM_SD_CONTROL_NONE, 0};
        }
    } else if (scene->loader_phase != SD_LOADER_CLOSED ||
               scene->welcome_pending || scene->phase == WM_SD_LEAVING ||
               (scene->phase == WM_SD_SCROLL &&
                hit.control != WM_SD_CONTROL_PREVIOUS &&
                hit.control != WM_SD_CONTROL_NEXT)) {
        hit = (WmSdHit){WM_SD_CONTROL_NONE, 0};
    }
    if (hit.control == WM_SD_CONTROL_CHANNEL &&
        (hit.slot >= WM_SD_SLOT_COUNT ||
         scene->media_status != WM_SD_MEDIA_READY ||
         hit.slot / WM_SD_SLOTS_PER_PAGE != scene->page ||
         !scene->channels[hit.slot].icon)) {
        hit = (WmSdHit){WM_SD_CONTROL_NONE, 0};
    }
    if ((hit.control == WM_SD_CONTROL_PREVIOUS &&
         !wm_arrow_interaction_side(&scene->arrows,
                                     WM_ARROW_PREVIOUS)->visible) ||
        (hit.control == WM_SD_CONTROL_NEXT &&
         !wm_arrow_interaction_side(&scene->arrows,
                                     WM_ARROW_NEXT)->visible)) {
        hit = (WmSdHit){WM_SD_CONTROL_NONE, 0};
    }
    if (same_hit(scene->hover, hit)) return;
    if (scene->hover.control == WM_SD_CONTROL_CHANNEL) clear_tile_focus(scene);
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
    wm_arrow_interaction_hover(
        &scene->arrows, hit.control == WM_SD_CONTROL_PREVIOUS
                            ? WM_ARROW_PREVIOUS :
                        hit.control == WM_SD_CONTROL_NEXT
                            ? WM_ARROW_NEXT : -1);
    scene->hover = hit;
    target_balloon(scene, hit.control);
    if (hit.control == WM_SD_CONTROL_NONE) return;
    enqueue(scene, WM_SD_EVENT_HOVER_SOUND, 0);
    if (hit.control == WM_SD_CONTROL_CHANNEL) {
        SdFocus *focus = &scene->tile_focus[hit.slot];
        if (focus->active && focus->entering) {
            focus->pending_leave = false;
        } else {
            *focus = (SdFocus){.active = true, .entering = true};
        }
    } else if (hit.control != WM_SD_CONTROL_PREVIOUS &&
               hit.control != WM_SD_CONTROL_NEXT) {
        scene->button_focus[hit.control] =
            (SdFocus){.active = true, .entering = true};
    }
}

bool wm_sd_scene_activate(WmSdScene *scene, WmSdHit hit) {
    if (!scene || scene->phase == WM_SD_CLOSED) return false;
    if (scene->dialog_phase != SD_DIALOG_CLOSED) {
        if (scene->dialog_phase != SD_DIALOG_IDLE ||
            (hit.control != WM_SD_CONTROL_HELP_BACK &&
             hit.control != WM_SD_CONTROL_HELP_NEXT) ||
            (scene->welcome_active && scene->dialog_page == 0 &&
             hit.control == WM_SD_CONTROL_HELP_BACK)) return false;
        scene->dialog_previous_page = scene->dialog_page;
        scene->dialog_destination = (int)scene->dialog_page +
            (hit.control == WM_SD_CONTROL_HELP_BACK ? -1 : 1);
        scene->dialog_selected = hit.control;
        scene->dialog_phase = SD_DIALOG_SELECT;
        scene->dialog_frame = 0.0f;
        enqueue(scene, hit.control == WM_SD_CONTROL_HELP_BACK
                           ? WM_SD_EVENT_CANCEL_SOUND :
                             WM_SD_EVENT_CONFIRM_SOUND, 0);
        return true;
    }
    if (wm_sd_scene_is_locked(scene)) return false;
    if (hit.control == WM_SD_CONTROL_CHANNEL) {
        if (scene->media_status != WM_SD_MEDIA_READY ||
            hit.slot >= WM_SD_SLOT_COUNT ||
            hit.slot / WM_SD_SLOTS_PER_PAGE != scene->page ||
            !scene->channels[hit.slot].icon) return false;
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
    int direction = hit.control == WM_SD_CONTROL_PREVIOUS ? -1 :
                    hit.control == WM_SD_CONTROL_NEXT ? 1 : 0;
    if (scene->media_status != WM_SD_MEDIA_READY ||
        !wm_sd_page_can_move(scene->page, direction)) return false;
    scene->phase = WM_SD_SCROLL;
    scene->scroll_direction = direction;
    scene->scroll_frame = 0.0f;
    wm_arrow_interaction_press(&scene->arrows,
                                direction < 0 ? WM_ARROW_PREVIOUS :
                                                WM_ARROW_NEXT, 0.0f);
    clear_tile_focus(scene);
    enqueue(scene, WM_SD_EVENT_PAGE_SOUND, 0);
    return true;
}

bool wm_sd_scene_back(WmSdScene *scene) {
    if (!scene) return false;
    if (scene->dialog_phase != SD_DIALOG_CLOSED) {
        return wm_sd_scene_activate(scene,
                         (WmSdHit){WM_SD_CONTROL_HELP_BACK, 0});
    }
    return wm_sd_scene_activate(scene, (WmSdHit){WM_SD_CONTROL_BACK, 0});
}
