#include "wii_menu/home_overlay.h"

#include "wii_menu/layout_present.h"
#include "wii_menu/layout_runtime.h"
#include "wii_menu/material_prepare.h"
#include "wii_menu/source_hit.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

enum {
    HOME_PATH_CAPACITY = 4096,
    HOME_CLIP_CAPACITY = 80,
    HOME_EFFECT_CAPACITY = 16,
    HOME_ANIMATION_COUNT = 40
};

typedef enum HomeAnimation {
    ANIM_12BTN_ON,
    ANIM_BTRY_GRY,
    ANIM_BTRY_RED,
    ANIM_BTRY_WHT,
    ANIM_BTRY_WINK,
    ANIM_BTRY_WINK_GRY,
    ANIM_CLOSE_BAR_IN,
    ANIM_CLOSE_BAR_OUT,
    ANIM_CLOSE_BAR_PSH,
    ANIM_CMN_MSG_BTN_IN,
    ANIM_CMN_MSG_BTN_OUT,
    ANIM_CMN_MSG_BTN_PSH,
    ANIM_CMN_MSG_IN,
    ANIM_CMN_MSG_OUT,
    ANIM_CMN_MSG_RTRN,
    ANIM_CNTBTN_IN,
    ANIM_CNTBTN_OUT,
    ANIM_CNTBTN_PSH,
    ANIM_CNTRL_DWN,
    ANIM_CNTRL_UP,
    ANIM_CNTRL_WNDW_OPN,
    ANIM_HMMENU_BAR_IN,
    ANIM_HMMENU_BAR_OUT,
    ANIM_HMMENU_BAR_PSH,
    ANIM_HMMENU_FNSH,
    ANIM_HMMENU_STRT,
    ANIM_LINK_MSG_IN,
    ANIM_LINK_MSG_OUT,
    ANIM_LTRICN_ON,
    ANIM_OPTN_BAR_IN,
    ANIM_OPTN_BAR_OUT,
    ANIM_OPTN_BAR_PSH,
    ANIM_OPTN_BTN_IN,
    ANIM_OPTN_BTN_OUT,
    ANIM_OPTN_BTN_PSH,
    ANIM_SOUND_GRY,
    ANIM_SOUND_YLW,
    ANIM_VB_BTN_WHT_PSH,
    ANIM_VB_BTN_YLW_PSH,
    ANIM_VB_BTN_YLW_YLW
} HomeAnimation;

static const char *const animation_suffixes[HOME_ANIMATION_COUNT] = {
    "12btn_on", "btry_gry", "btry_red", "btry_wht", "btry_wink",
    "btry_wink_gry", "close_bar_in", "close_bar_out", "close_bar_psh",
    "cmn_msg_btn_in", "cmn_msg_btn_out", "cmn_msg_btn_psh", "cmn_msg_in",
    "cmn_msg_out", "cmn_msg_rtrn", "cntBtn_in", "cntBtn_out",
    "cntBtn_psh", "cntrl_dwn", "cntrl_up", "cntrl_wndw_opn",
    "hmMenu_bar_in", "hmMenu_bar_out", "hmMenu_bar_psh", "hmMenu_fnsh",
    "hmMenu_strt", "link_msg_in", "link_msg_out", "ltrIcn_on",
    "optn_bar_in", "optn_bar_out", "optn_bar_psh", "optn_btn_in",
    "optn_btn_out", "optn_btn_psh", "sound_gry", "sound_ylw",
    "vb_btn_wht_psh", "vb_btn_ylw_psh", "vb_btn_ylw_ylw"
};

typedef enum HomeDialogPhase {
    DIALOG_NONE,
    DIALOG_PRESS,
    DIALOG_IN,
    DIALOG_IDLE,
    DIALOG_NO,
    DIALOG_RETURN,
    DIALOG_YES,
    DIALOG_FADE
} HomeDialogPhase;

typedef enum HomeReconnectPhase {
    RECONNECT_NONE,
    RECONNECT_PRESS,
    RECONNECT_IN,
    RECONNECT_RETRY,
    RECONNECT_WAIT,
    RECONNECT_CONNECTED,
    RECONNECT_STOP_RETRY,
    RECONNECT_OUT
} HomeReconnectPhase;

typedef struct HomeEffect {
    HomeAnimation animation;
    char group[32];
    float frame;
    float duration;
} HomeEffect;

struct WmHomeOverlay {
    WmPlatform *platform;
    WmTextureCache *textures;
    WmFontCache *fonts;
    WmLayout *layout;
    WmHomeCueCallback cue;
    WmHomeRemoteCallback remote_changed;
    void *callback_context;
    char animation_names[HOME_ANIMATION_COUNT][64];
    float animation_frames[HOME_ANIMATION_COUNT];
    WmHomePhase phase;
    WmHomeOutcome outcome;
    float frame;
    WmHomeControl hover;
    float hover_frame;
    float focus_sound_age;
    bool options_open;
    bool options_closing;
    float options_frame;
    bool open_controller_sound;
    WmHomeRemoteState remote;
    float rumble_lock;
    HomeDialogPhase dialog;
    float dialog_frame;
    HomeReconnectPhase reconnect;
    float reconnect_frame;
    float reconnect_prompt_frame;
    float reconnect_wait_frame;
    float reconnect_final_frame;
    float reconnect_next_connection;
    unsigned reconnect_start_failures;
    unsigned reconnect_stop_failures;
    unsigned reconnect_count;
    unsigned reconnect_players[4];
    unsigned speaker_player[4];
    float speaker_frame[4];
    size_t speaker_count;
    bool held_one;
    bool held_two;
    WmHomeReconnectFixture fixture;
    HomeEffect effects[HOME_EFFECT_CAPACITY];
    size_t effect_count;
    WmSourceRect control_rects[WM_HOME_CONTROL_NO + 1];
    bool control_rect_valid[WM_HOME_CONTROL_NO + 1];
    bool pose_dirty;
    bool hit_dirty;
};

static void emit_cue(WmHomeOverlay *home, const char *suffix)
{
    if (!home || !home->cue || !suffix) return;
    char symbol[64];
    int length = snprintf(symbol, sizeof(symbol), "HOMESE_%s", suffix);
    if (length > 0 && length < (int)sizeof(symbol)) {
        home->cue(home->callback_context, symbol);
    }
}

static void emit_speaker(WmHomeOverlay *home, unsigned player)
{
    if (!home || !home->cue) return;
    char symbol[64];
    int length = player
        ? snprintf(symbol, sizeof(symbol), "HOME_SPEAKER_CONNECT%u", player)
        : snprintf(symbol, sizeof(symbol), "HOME_SPEAKER_VOLUME");
    if (length > 0 && length < (int)sizeof(symbol)) {
        home->cue(home->callback_context, symbol);
    }
}

static void emit_remote_change(WmHomeOverlay *home)
{
    if (home && home->remote_changed) {
        home->remote_changed(home->callback_context, &home->remote);
    }
}

static float clamped_frame(const WmHomeOverlay *home, HomeAnimation animation,
                           float frame)
{
    float last = home->animation_frames[animation] - 1.0f;
    return fminf(fmaxf(frame, 0.0f), fmaxf(last, 0.0f));
}

static void add_clip(const WmHomeOverlay *home, WmLayoutClip clips[],
                     size_t *count, HomeAnimation animation, float frame,
                     const char *group)
{
    if (*count >= HOME_CLIP_CAPACITY) return;
    clips[*count] = (WmLayoutClip){
        .animation = home->animation_names[animation],
        .frame = clamped_frame(home, animation, frame),
        .group = group ? group : animation_suffixes[animation],
        .recursive_group = false,
        .loop_override = 0
    };
    (*count)++;
}

static void set_effect(WmHomeOverlay *home, HomeAnimation animation,
                       const char *group, float duration)
{
    if (!home || !group || strlen(group) >= sizeof(home->effects[0].group)) {
        return;
    }
    for (size_t index = 0; index < home->effect_count; index++) {
        if (strcmp(home->effects[index].group, group) != 0) continue;
        home->effect_count--;
        memmove(&home->effects[index], &home->effects[index + 1],
                (home->effect_count - index) * sizeof(home->effects[0]));
        break;
    }
    if (home->effect_count == HOME_EFFECT_CAPACITY) return;
    HomeEffect *effect = &home->effects[home->effect_count++];
    *effect = (HomeEffect){
        .animation = animation,
        .frame = 0.0f,
        .duration = duration
    };
    strcpy(effect->group, group);
    home->pose_dirty = true;
}

static void clear_effect_group(WmHomeOverlay *home, const char *group)
{
    if (!home || !group) return;
    for (size_t index = 0; index < home->effect_count; index++) {
        if (strcmp(home->effects[index].group, group) != 0) continue;
        home->effect_count--;
        memmove(&home->effects[index], &home->effects[index + 1],
                (home->effect_count - index) * sizeof(home->effects[0]));
        home->pose_dirty = true;
        break;
    }
}

static void change_phase(WmHomeOverlay *home, WmHomePhase phase)
{
    home->phase = phase;
    home->frame = 0.0f;
    home->hover = WM_HOME_CONTROL_NONE;
    home->hover_frame = 0.0f;
    home->pose_dirty = true;
}

static bool options_ready(const WmHomeOverlay *home)
{
    if (home->options_open) {
        return home->options_frame >= home->animation_frames[ANIM_OPTN_BAR_PSH];
    }
    if (!home->options_closing) return true;
    return home->options_frame >= 40.0f;
}

bool wm_home_overlay_active(const WmHomeOverlay *home)
{
    return home && home->phase != WM_HOME_CLOSED;
}

bool wm_home_overlay_ready(const WmHomeOverlay *home)
{
    return wm_home_overlay_active(home) && home->phase == WM_HOME_IDLE &&
           options_ready(home) && home->rumble_lock <= 0.0f &&
           home->reconnect == RECONNECT_NONE &&
           (home->dialog == DIALOG_NONE || home->dialog == DIALOG_IDLE);
}

WmHomePhase wm_home_overlay_phase(const WmHomeOverlay *home)
{
    return home ? home->phase : WM_HOME_CLOSED;
}

float wm_home_overlay_frame(const WmHomeOverlay *home)
{
    return home ? home->frame : 0.0f;
}

float wm_home_overlay_fade_alpha(const WmHomeOverlay *home)
{
    if (!home || home->phase != WM_HOME_RETURN_FADE) return 0.0f;
    return floorf(fminf(255.0f, home->frame * 255.0f / 30.0f)) / 255.0f;
}

WmHomeOverlay *wm_home_overlay_create(WmPlatform *platform,
                                       const char *assets_directory,
                                       WmTextureCache *textures,
                                       WmFontCache *fonts,
                                       WmHomeCueCallback cue,
                                       WmHomeRemoteCallback remote_changed,
                                       void *callback_context)
{
    if (!platform || !assets_directory || !*assets_directory || !textures ||
        !fonts) return NULL;
    WmHomeOverlay *home = calloc(1, sizeof(*home));
    if (!home) return NULL;
    home->platform = platform;
    home->textures = textures;
    home->fonts = fonts;
    home->cue = cue;
    home->remote_changed = remote_changed;
    home->callback_context = callback_context;
    home->phase = WM_HOME_CLOSED;
    home->pose_dirty = true;
    home->focus_sound_age = 3.0f;
    home->remote.volume = 0.7f;
    home->remote.rumble = true;
    for (size_t index = 0; index < 4; index++) {
        home->remote.controllers[index] = (WmHomeRemote){
            .connected = index == 0,
            .battery = 4
        };
    }
    home->fixture = (WmHomeReconnectFixture){
        .mode = WM_HOME_RECONNECT_AUTOMATIC,
        .players = {1},
        .player_count = 1,
        .delay_frames = 180.0f,
        .interval_frames = 24.0f
    };
    char path[HOME_PATH_CAPACITY];
    int length = snprintf(path, sizeof(path), "%s/layouts/homeBtn1/th_HomeBtn_d.json",
                          assets_directory);
    if (length < 0 || length >= (int)sizeof(path)) {
        free(home);
        return NULL;
    }
    char error[160] = {0};
    home->layout = wm_layout_load_json(path, error, sizeof(error));
    if (!home->layout) {
        fprintf(stderr, "Could not load HOME layout: %s\n", error);
        free(home);
        return NULL;
    }
    for (size_t index = 0; index < HOME_ANIMATION_COUNT; index++) {
        length = snprintf(home->animation_names[index],
                          sizeof(home->animation_names[index]),
                          "th_HomeBtn_d_%s", animation_suffixes[index]);
        WmLayoutAnimationInfo info;
        if (length < 0 ||
            length >= (int)sizeof(home->animation_names[index]) ||
            !wm_layout_animation_info(home->layout,
                                      home->animation_names[index], &info) ||
            !isfinite(info.frames) || info.frames <= 0.0f) {
            fprintf(stderr, "HOME layout lacks animation %s\n",
                    animation_suffixes[index]);
            wm_home_overlay_destroy(home);
            return NULL;
        }
        home->animation_frames[index] = info.frames;
    }
    wm_layout_set_text(home->layout, "T_Dialog", "Return to the Wii Menu?");
    wm_layout_set_text(home->layout, "T_msg_00",
                       "Simultaneously press ① and ②\non each Wii Remote in the\ndesired player order.");
    wm_layout_set_text(home->layout, "T_msg_01", "Disconnecting...");
    wm_layout_prepare_materials(platform, home->layout);
    return home;
}

void wm_home_overlay_destroy(WmHomeOverlay *home)
{
    if (!home) return;
    wm_layout_destroy(home->layout);
    free(home);
}

bool wm_home_overlay_set_remote_state(WmHomeOverlay *home,
                                      const WmHomeRemoteState *state)
{
    if (!home || !state || !isfinite(state->volume) || state->volume < 0.0f ||
        state->volume > 1.0f) return false;
    for (size_t index = 0; index < 4; index++) {
        if (state->controllers[index].battery > 4) return false;
    }
    home->remote = *state;
    home->pose_dirty = true;
    return true;
}

WmHomeRemoteState wm_home_overlay_remote_state(const WmHomeOverlay *home)
{
    return home ? home->remote : (WmHomeRemoteState){0};
}

bool wm_home_overlay_set_reconnect_fixture(
    WmHomeOverlay *home, const WmHomeReconnectFixture *fixture)
{
    if (!home || !fixture || fixture->mode > WM_HOME_RECONNECT_TIMEOUT ||
        fixture->player_count == 0 || fixture->player_count > 4 ||
        !isfinite(fixture->delay_frames) || fixture->delay_frames < 0.0f ||
        fixture->delay_frames > 3600.0f ||
        !isfinite(fixture->interval_frames) ||
        fixture->interval_frames < 0.0f ||
        fixture->interval_frames > 3600.0f ||
        fixture->start_failures > 600 || fixture->stop_failures > 600) {
        return false;
    }
    for (size_t index = 0; index < fixture->player_count; index++) {
        if (fixture->players[index] < 1 || fixture->players[index] > 4) {
            return false;
        }
        for (size_t previous = 0; previous < index; previous++) {
            if (fixture->players[previous] == fixture->players[index]) {
                return false;
            }
        }
    }
    home->fixture = *fixture;
    return true;
}

bool wm_home_overlay_open(WmHomeOverlay *home)
{
    if (!home || wm_home_overlay_active(home)) return false;
    home->outcome = WM_HOME_OUTCOME_NONE;
    home->options_open = false;
    home->options_closing = false;
    home->options_frame = 0.0f;
    home->open_controller_sound = false;
    home->dialog = DIALOG_NONE;
    home->dialog_frame = 0.0f;
    home->reconnect = RECONNECT_NONE;
    home->reconnect_count = 0;
    home->speaker_count = 0;
    home->effect_count = 0;
    home->rumble_lock = 0.0f;
    home->held_one = false;
    home->held_two = false;
    change_phase(home, WM_HOME_ENTER);
    return true;
}

void wm_home_overlay_reset(WmHomeOverlay *home)
{
    if (!home) return;
    home->dialog = DIALOG_NONE;
    home->reconnect = RECONNECT_NONE;
    home->speaker_count = 0;
    home->effect_count = 0;
    home->held_one = false;
    home->held_two = false;
    home->outcome = WM_HOME_OUTCOME_NONE;
    change_phase(home, WM_HOME_CLOSED);
}

WmHomeOutcome wm_home_overlay_take_outcome(WmHomeOverlay *home)
{
    if (!home) return WM_HOME_OUTCOME_NONE;
    WmHomeOutcome outcome = home->outcome;
    home->outcome = WM_HOME_OUTCOME_NONE;
    return outcome;
}

static void begin_reconnect_wait(WmHomeOverlay *home)
{
    home->reconnect_start_failures = home->fixture.start_failures;
    home->reconnect_stop_failures = home->fixture.stop_failures;
    home->reconnect_wait_frame = 0.0f;
    home->reconnect_final_frame = -1.0f;
    home->reconnect_next_connection = home->fixture.delay_frames;
    home->reconnect_frame = 0.0f;
    if (home->reconnect_start_failures > 0) {
        home->reconnect_start_failures--;
        home->reconnect = RECONNECT_RETRY;
    } else {
        home->reconnect = RECONNECT_WAIT;
    }
}

static void finish_reconnect_wait(WmHomeOverlay *home)
{
    home->reconnect_frame = 0.0f;
    if (home->reconnect_stop_failures > 0) {
        home->reconnect_stop_failures--;
        home->reconnect = RECONNECT_STOP_RETRY;
    } else {
        home->reconnect = RECONNECT_OUT;
        emit_cue(home, "END_CONNECT_WINDOW");
    }
}

bool wm_home_overlay_connect(WmHomeOverlay *home, unsigned player)
{
    if (!home || (home->reconnect != RECONNECT_WAIT &&
                  home->reconnect != RECONNECT_CONNECTED) || player < 1 ||
        player > 4 || home->reconnect_count >= home->fixture.player_count) {
        return false;
    }
    bool allowed = false;
    for (size_t index = 0; index < home->fixture.player_count; index++) {
        if (home->fixture.players[index] == player) allowed = true;
    }
    if (!allowed) return false;
    for (size_t index = 0; index < home->reconnect_count; index++) {
        if (home->reconnect_players[index] == player) return false;
    }
    home->reconnect_players[home->reconnect_count++] = player;
    home->remote.controllers[player - 1].connected = true;
    home->reconnect = RECONNECT_CONNECTED;
    home->reconnect_frame = 0.0f;
    emit_cue(home, player == 1 ? "CONNECTED" :
             player == 2 ? "CONNECTED2" :
             player == 3 ? "CONNECTED3" : "CONNECTED4");
    char group[16];
    snprintf(group, sizeof(group), "plyr_0%u", player - 1);
    set_effect(home, ANIM_BTRY_WINK, group, 80.0f);
    if (home->speaker_count < 4) {
        home->speaker_player[home->speaker_count] = player;
        home->speaker_frame[home->speaker_count++] = 0.0f;
    }
    emit_remote_change(home);
    home->pose_dirty = true;
    if (home->fixture.mode != WM_HOME_RECONNECT_TIMEOUT &&
        home->reconnect_count == home->fixture.player_count) {
        home->reconnect_final_frame = 0.0f;
    }
    return true;
}

bool wm_home_overlay_reconnect_key(WmHomeOverlay *home, char key, bool down)
{
    if (!home || (key != '1' && key != '2')) return false;
    bool *held = key == '1' ? &home->held_one : &home->held_two;
    bool was_held = *held;
    *held = down;
    if (down && !was_held && home->held_one && home->held_two &&
        (home->reconnect == RECONNECT_WAIT ||
         home->reconnect == RECONNECT_CONNECTED)) {
        for (size_t index = 0; index < home->fixture.player_count; index++) {
            unsigned player = home->fixture.players[index];
            bool already_connected = false;
            for (size_t prior = 0; prior < home->reconnect_count; prior++) {
                already_connected |= home->reconnect_players[prior] == player;
            }
            if (!already_connected) return wm_home_overlay_connect(home, player);
        }
    }
    return home->reconnect != RECONNECT_NONE;
}

static void advance_reconnect(WmHomeOverlay *home, float step)
{
    if (home->reconnect == RECONNECT_NONE) return;
    home->reconnect_frame += step;
    home->pose_dirty = true;
    if (home->reconnect == RECONNECT_WAIT ||
        home->reconnect == RECONNECT_CONNECTED ||
        home->reconnect == RECONNECT_STOP_RETRY ||
        home->reconnect == RECONNECT_OUT) {
        home->reconnect_prompt_frame += step;
    }
    if (home->reconnect == RECONNECT_PRESS && home->reconnect_frame >= 15.0f) {
        for (size_t index = 0; index < 4; index++) {
            home->remote.controllers[index].connected = false;
        }
        emit_remote_change(home);
        home->reconnect = RECONNECT_IN;
        home->reconnect_frame = 0.0f;
    } else if (home->reconnect == RECONNECT_IN &&
               home->reconnect_frame >= 119.0f) {
        begin_reconnect_wait(home);
    } else if (home->reconnect == RECONNECT_RETRY &&
               home->reconnect_frame >= 6.0f) {
        home->reconnect_frame = 0.0f;
        if (home->reconnect_start_failures > 0) {
            home->reconnect_start_failures--;
        } else {
            home->reconnect = RECONNECT_WAIT;
        }
    } else if (home->reconnect == RECONNECT_STOP_RETRY &&
               home->reconnect_frame >= 6.0f) {
        finish_reconnect_wait(home);
    } else if (home->reconnect == RECONNECT_WAIT ||
               home->reconnect == RECONNECT_CONNECTED) {
        home->reconnect_wait_frame += step;
        if (home->reconnect_final_frame >= 0.0f) {
            home->reconnect_final_frame += step;
        }
        if (home->fixture.mode == WM_HOME_RECONNECT_AUTOMATIC) {
            while (home->reconnect_wait_frame >=
                       home->reconnect_next_connection &&
                   home->reconnect_count < home->fixture.player_count) {
                unsigned player = home->fixture.players[home->reconnect_count];
                home->reconnect = RECONNECT_WAIT;
                wm_home_overlay_connect(home, player);
                home->reconnect_next_connection = home->reconnect_wait_frame +
                    home->fixture.interval_frames;
            }
        }
        if (home->reconnect_final_frame >= 30.0f ||
            home->reconnect_wait_frame > 3600.0f) {
            finish_reconnect_wait(home);
        }
    } else if (home->reconnect == RECONNECT_OUT &&
               home->reconnect_frame >= 19.0f) {
        home->reconnect = RECONNECT_NONE;
    }
}

static void advance_effects(WmHomeOverlay *home, float step)
{
    for (size_t index = 0; index < home->effect_count;) {
        HomeEffect *effect = &home->effects[index];
        effect->frame += step;
        if (effect->frame >= effect->duration) {
            home->effect_count--;
            memmove(effect, effect + 1,
                    (home->effect_count - index) * sizeof(*effect));
        } else {
            index++;
        }
    }
    for (size_t index = 0; index < home->speaker_count;) {
        home->speaker_frame[index] += step;
        if (home->speaker_frame[index] >= 24.0f) {
            emit_speaker(home, home->speaker_player[index]);
            home->speaker_count--;
            memmove(&home->speaker_frame[index], &home->speaker_frame[index + 1],
                    (home->speaker_count - index) * sizeof(home->speaker_frame[0]));
            memmove(&home->speaker_player[index], &home->speaker_player[index + 1],
                    (home->speaker_count - index) * sizeof(home->speaker_player[0]));
        } else {
            index++;
        }
    }
}

float wm_home_overlay_advance(WmHomeOverlay *home, float frames)
{
    if (!home || !isfinite(frames) || frames <= 0.0f) return 0.0f;
    if (!wm_home_overlay_active(home)) return frames;
    float remaining = frames;
    while (remaining > 0.0f) {
        float fraction = fmodf(home->frame, 1.0f);
        float step = fminf(1.0f - fraction, remaining);
        if (step <= 0.0f) break;
        remaining -= step;
        home->frame += step;
        home->pose_dirty = true;
        home->hover_frame += step;
        home->focus_sound_age += step;
        home->rumble_lock = fmaxf(0.0f, home->rumble_lock - step);
        advance_effects(home, step);

        if (home->phase == WM_HOME_ENTER &&
            home->frame >= home->animation_frames[ANIM_HMMENU_STRT]) {
            change_phase(home, WM_HOME_IDLE);
            emit_cue(home, "HOME_BUTTON");
        } else if (home->phase == WM_HOME_LEAVE && home->frame >= 39.0f) {
            change_phase(home, WM_HOME_CLOSED);
            home->outcome = WM_HOME_OUTCOME_CLOSED;
            return remaining;
        } else if (home->phase == WM_HOME_RETURN_FADE &&
                   home->frame >= 30.0f) {
            change_phase(home, WM_HOME_CLOSED);
            home->dialog = DIALOG_NONE;
            home->outcome = WM_HOME_OUTCOME_RETURN_MENU;
            return remaining;
        }
        if (home->options_open || home->options_closing) {
            home->options_frame += step;
            if (home->options_open && !home->open_controller_sound &&
                home->options_frame >= 16.0f) {
                home->open_controller_sound = true;
                emit_cue(home, "OPEN_CONTROLLER");
            }
        }
        if (home->dialog != DIALOG_NONE) {
            home->dialog_frame += step;
            if (home->dialog == DIALOG_PRESS &&
                home->dialog_frame >= 16.0f) {
                home->dialog = DIALOG_IN;
                home->dialog_frame = 0.0f;
            } else if (home->dialog == DIALOG_IN &&
                       home->dialog_frame >= 24.0f) {
                home->dialog = DIALOG_IDLE;
                home->dialog_frame = 24.0f;
            } else if (home->dialog == DIALOG_NO &&
                       home->dialog_frame >= 20.0f) {
                home->dialog = DIALOG_RETURN;
                home->dialog_frame = 0.0f;
            } else if (home->dialog == DIALOG_RETURN &&
                       home->dialog_frame >= 19.0f) {
                home->dialog = DIALOG_NONE;
            } else if (home->dialog == DIALOG_YES &&
                       home->dialog_frame >= 20.0f) {
                home->dialog = DIALOG_FADE;
                home->dialog_frame = 20.0f;
                change_phase(home, WM_HOME_RETURN_FADE);
            }
        }
        advance_reconnect(home, step);
    }
    return 0.0f;
}

static bool control_enabled(const WmHomeOverlay *home, WmHomeControl control)
{
    if (!wm_home_overlay_ready(home)) return false;
    if (home->dialog != DIALOG_NONE) {
        return control == WM_HOME_CONTROL_YES ||
               control == WM_HOME_CONTROL_NO;
    }
    if (control == WM_HOME_CONTROL_OPTIONS) return true;
    if (home->options_open) {
        return control >= WM_HOME_CONTROL_VOLUME_DOWN &&
               control <= WM_HOME_CONTROL_RECONNECT;
    }
    return control == WM_HOME_CONTROL_CLOSE ||
           control == WM_HOME_CONTROL_RETURN;
}

typedef struct HoverBinding {
    HomeAnimation animation;
    const char *group;
    float duration;
    bool valid;
} HoverBinding;

static HoverBinding hover_binding(WmHomeControl control, bool entering,
                                  bool options_open)
{
    switch (control) {
        case WM_HOME_CONTROL_CLOSE:
            return (HoverBinding){
                entering ? ANIM_HMMENU_BAR_IN : ANIM_HMMENU_BAR_OUT,
                entering ? "hmMenu_bar_in" : "hmMenu_bar_out", 10.0f, true
            };
        case WM_HOME_CONTROL_RETURN:
            return (HoverBinding){
                entering ? ANIM_CNTBTN_IN : ANIM_CNTBTN_OUT,
                "btnL_00_inOut", 10.0f, true
            };
        case WM_HOME_CONTROL_OPTIONS:
            return (HoverBinding){
                options_open
                    ? (entering ? ANIM_CLOSE_BAR_IN : ANIM_CLOSE_BAR_OUT)
                    : (entering ? ANIM_OPTN_BAR_IN : ANIM_OPTN_BAR_OUT),
                entering ? "optn_bar_in" : "optn_bar_out", 10.0f, true
            };
        case WM_HOME_CONTROL_YES:
        case WM_HOME_CONTROL_NO:
            return (HoverBinding){
                entering ? ANIM_CMN_MSG_BTN_IN : ANIM_CMN_MSG_BTN_OUT,
                control == WM_HOME_CONTROL_YES
                    ? "msgBtn_00_inOut" : "msgBtn_01_inOut",
                10.0f, true
            };
        case WM_HOME_CONTROL_VOLUME_DOWN:
        case WM_HOME_CONTROL_VOLUME_UP:
        case WM_HOME_CONTROL_RUMBLE_ON:
        case WM_HOME_CONTROL_RUMBLE_OFF:
        case WM_HOME_CONTROL_RECONNECT: {
            static const char *const groups[] = {
                "optnBtn_00_inOut", "optnBtn_01_inOut",
                "optnBtn_10_inOut", "optnBtn_11_inOut",
                "optnBtn_20_inOut"
            };
            return (HoverBinding){
                entering ? ANIM_OPTN_BTN_IN : ANIM_OPTN_BTN_OUT,
                groups[control - WM_HOME_CONTROL_VOLUME_DOWN], 10.0f, true
            };
        }
        default:
            return (HoverBinding){0};
    }
}

void wm_home_overlay_hover(WmHomeOverlay *home, WmHomeControl control)
{
    if (!home || control == home->hover) return;
    WmHomeControl next = control_enabled(home, control)
                             ? control : WM_HOME_CONTROL_NONE;
    if (next == home->hover) return;
    HoverBinding previous = hover_binding(home->hover, false,
                                          home->options_open);
    if (previous.valid) {
        set_effect(home, previous.animation, previous.group,
                   previous.duration);
    }
    home->hover = next;
    home->hover_frame = 0.0f;
    home->pose_dirty = true;
    HoverBinding incoming = hover_binding(next, false, home->options_open);
    if (incoming.valid) clear_effect_group(home, incoming.group);
    if (next != WM_HOME_CONTROL_NONE && home->focus_sound_age > 2.0f) {
        emit_cue(home, "FOCUS");
        home->focus_sound_age = 0.0f;
    }
}

bool wm_home_overlay_activate(WmHomeOverlay *home, WmHomeControl control)
{
    if (!control_enabled(home, control)) return false;
    home->pose_dirty = true;
    if (home->dialog != DIALOG_NONE) {
        home->dialog = control == WM_HOME_CONTROL_YES ? DIALOG_YES : DIALOG_NO;
        home->dialog_frame = 0.0f;
        home->hover = WM_HOME_CONTROL_NONE;
        emit_cue(home, control == WM_HOME_CONTROL_YES
                          ? "GOTO_MENU" : "CANCEL");
        return true;
    }
    switch (control) {
        case WM_HOME_CONTROL_CLOSE:
            change_phase(home, WM_HOME_LEAVE);
            emit_cue(home, "RETURN_APP");
            return true;
        case WM_HOME_CONTROL_RETURN:
            home->dialog = DIALOG_PRESS;
            home->dialog_frame = 0.0f;
            home->hover = WM_HOME_CONTROL_NONE;
            set_effect(home, ANIM_CNTBTN_PSH, "btnL_00_psh", 17.0f);
            emit_cue(home, "SELECT");
            return true;
        case WM_HOME_CONTROL_OPTIONS:
            home->hover = WM_HOME_CONTROL_NONE;
            home->options_frame = 0.0f;
            if (home->options_open) {
                home->options_open = false;
                home->options_closing = true;
                emit_cue(home, "CLOSE_CONTROLLER");
            } else {
                home->options_open = true;
                home->options_closing = false;
                home->open_controller_sound = false;
                emit_cue(home, "SELECT");
            }
            return true;
        case WM_HOME_CONTROL_VOLUME_DOWN:
        case WM_HOME_CONTROL_VOLUME_UP: {
            int direction = control == WM_HOME_CONTROL_VOLUME_UP ? 1 : -1;
            int current = home->remote.muted
                              ? 0 : (int)lroundf(home->remote.volume * 10.0f);
            int next = current + direction;
            if (next < 0) next = 0;
            if (next > 10) next = 10;
            if (next == current) {
                emit_cue(home, "NOTHING_DONE");
            } else {
                home->remote.volume = (float)next / 10.0f;
                home->remote.muted = false;
                emit_cue(home, direction > 0
                                  ? (next == 10 ? "VOLUME_PLUS_LIMIT"
                                                : "VOLUME_PLUS")
                                  : (next == 0 ? "VOLUME_MINUS_LIMIT"
                                               : "VOLUME_MINUS"));
                emit_remote_change(home);
                emit_speaker(home, 0);
                set_effect(home, ANIM_OPTN_BTN_PSH,
                           direction > 0 ? "optnBtn_01_psh"
                                         : "optnBtn_00_psh", 16.0f);
            }
            return true;
        }
        case WM_HOME_CONTROL_RUMBLE_ON:
        case WM_HOME_CONTROL_RUMBLE_OFF: {
            bool enabled = control == WM_HOME_CONTROL_RUMBLE_ON;
            bool changed = home->remote.rumble != enabled;
            emit_cue(home, changed ? (enabled ? "VIBE_ON" : "VIBE_OFF")
                                   : "NOTHING_DONE");
            if (enabled) {
                set_effect(home, changed ? ANIM_VB_BTN_WHT_PSH
                                         : ANIM_VB_BTN_YLW_YLW,
                           "optnBtn_10_cntrl", 24.0f);
                if (changed) {
                    set_effect(home, ANIM_VB_BTN_YLW_PSH,
                               "optnBtn_11_psh", 16.0f);
                }
                home->rumble_lock = 24.0f;
            } else if (changed) {
                set_effect(home, ANIM_VB_BTN_WHT_PSH,
                           "optnBtn_11_psh", 24.0f);
                set_effect(home, ANIM_VB_BTN_YLW_PSH,
                           "optnBtn_10_psh", 16.0f);
                home->rumble_lock = 24.0f;
            }
            home->remote.rumble = enabled;
            emit_remote_change(home);
            return true;
        }
        case WM_HOME_CONTROL_RECONNECT:
            home->reconnect = RECONNECT_PRESS;
            home->reconnect_frame = 0.0f;
            home->reconnect_prompt_frame = 0.0f;
            home->reconnect_count = 0;
            home->held_one = false;
            home->held_two = false;
            home->hover = WM_HOME_CONTROL_NONE;
            set_effect(home, ANIM_OPTN_BTN_PSH, "optnBtn_20_psh", 16.0f);
            emit_cue(home, "SELECT");
            emit_cue(home, "START_CONNECT_WINDOW");
            return true;
        default:
            return false;
    }
}

bool wm_home_overlay_back(WmHomeOverlay *home)
{
    if (!wm_home_overlay_ready(home)) return false;
    if (home->dialog != DIALOG_NONE) {
        return wm_home_overlay_activate(home, WM_HOME_CONTROL_NO);
    }
    if (home->options_open) {
        return wm_home_overlay_activate(home, WM_HOME_CONTROL_OPTIONS);
    }
    return wm_home_overlay_activate(home, WM_HOME_CONTROL_CLOSE);
}

static const char *control_pane_name(WmHomeControl control)
{
    switch (control) {
        case WM_HOME_CONTROL_CLOSE: return "B_btn_00";
        case WM_HOME_CONTROL_RETURN: return "B_btnL_00";
        case WM_HOME_CONTROL_OPTIONS: return "B_bar_10";
        case WM_HOME_CONTROL_VOLUME_DOWN: return "B_optnBtn_00";
        case WM_HOME_CONTROL_VOLUME_UP: return "B_optnBtn_01";
        case WM_HOME_CONTROL_RUMBLE_ON: return "B_optnBtn_10";
        case WM_HOME_CONTROL_RUMBLE_OFF: return "B_optnBtn_11";
        case WM_HOME_CONTROL_RECONNECT: return "B_optnBtn_20";
        case WM_HOME_CONTROL_YES: return "B_BtnA";
        case WM_HOME_CONTROL_NO: return "B_BtnB";
        default: return NULL;
    }
}

static void pose_home_layout(WmHomeOverlay *home)
{
    if (!home || !home->layout || !home->pose_dirty) return;
    WmLayoutClip clips[HOME_CLIP_CAPACITY];
    size_t count = 0;
    add_clip(home, clips, &count, ANIM_HMMENU_STRT,
             home->phase == WM_HOME_ENTER ? home->frame : 21.0f, NULL);

    static const char *const player_groups[4] = {
        "plyr_00", "plyr_01", "plyr_02", "plyr_03"
    };
    for (size_t player = 0; player < 4; player++) {
        const WmHomeRemote *remote = &home->remote.controllers[player];
        add_clip(home, clips, &count,
                 remote->connected ? ANIM_BTRY_WHT : ANIM_BTRY_GRY,
                 0.0f, player_groups[player]);
        if (remote->connected && remote->battery < 2) {
            add_clip(home, clips, &count, ANIM_BTRY_RED, 0.0f,
                     player_groups[player]);
        }
    }

    static const char *const volume_groups[10] = {
        "vol_00", "vol_01", "vol_02", "vol_03", "vol_04",
        "vol_05", "vol_06", "vol_07", "vol_08", "vol_09"
    };
    int filled = home->remote.muted
                     ? 0 : (int)lroundf(home->remote.volume * 10.0f);
    for (size_t bar = 0; bar < 10; bar++) {
        add_clip(home, clips, &count,
                 (int)bar < filled ? ANIM_SOUND_YLW : ANIM_SOUND_GRY,
                 0.0f, volume_groups[bar]);
    }
    add_clip(home, clips, &count,
             home->remote.rumble ? ANIM_VB_BTN_WHT_PSH
                                 : ANIM_VB_BTN_YLW_PSH,
             24.0f, "optnBtn_10_psh");
    add_clip(home, clips, &count,
             home->remote.rumble ? ANIM_VB_BTN_YLW_PSH
                                 : ANIM_VB_BTN_WHT_PSH,
             24.0f, "optnBtn_11_psh");

    bool closing = home->options_closing && !home->options_open;
    bool ready = options_ready(home);
    if (home->options_open || closing) {
        float opening_frame = closing
                                  ? home->animation_frames[ANIM_OPTN_BAR_PSH]
                                  : home->options_frame;
        add_clip(home, clips, &count, ANIM_OPTN_BAR_PSH,
                 opening_frame, NULL);
        add_clip(home, clips, &count, ANIM_CNTRL_UP, opening_frame, NULL);
        if (opening_frame >= 16.0f) {
            add_clip(home, clips, &count, ANIM_CNTRL_WNDW_OPN,
                     opening_frame - 16.0f, NULL);
        }
        if (closing) {
            add_clip(home, clips, &count, ANIM_HMMENU_BAR_PSH,
                     home->options_frame, NULL);
            add_clip(home, clips, &count, ANIM_CLOSE_BAR_PSH,
                     home->options_frame, NULL);
            add_clip(home, clips, &count, ANIM_CNTRL_DWN,
                     home->options_frame, NULL);
        }
        if (home->options_open && ready &&
            home->hover == WM_HOME_CONTROL_OPTIONS) {
            add_clip(home, clips, &count, ANIM_CLOSE_BAR_IN,
                     home->hover_frame, "optn_bar_in");
        }
        if (home->options_open && ready &&
            home->hover >= WM_HOME_CONTROL_VOLUME_DOWN &&
            home->hover <= WM_HOME_CONTROL_RECONNECT) {
            static const char *const option_groups[] = {
                "optnBtn_00_inOut", "optnBtn_01_inOut",
                "optnBtn_10_inOut", "optnBtn_11_inOut",
                "optnBtn_20_inOut"
            };
            add_clip(home, clips, &count, ANIM_OPTN_BTN_IN,
                     home->hover_frame,
                     option_groups[home->hover - WM_HOME_CONTROL_VOLUME_DOWN]);
        }
    }
    if (!home->options_open && ready) {
        if (home->hover == WM_HOME_CONTROL_CLOSE) {
            add_clip(home, clips, &count, ANIM_HMMENU_BAR_IN,
                     home->hover_frame, NULL);
        } else if (home->hover == WM_HOME_CONTROL_RETURN) {
            add_clip(home, clips, &count, ANIM_CNTBTN_IN,
                     home->hover_frame, "btnL_00_inOut");
        } else if (home->hover == WM_HOME_CONTROL_OPTIONS) {
            add_clip(home, clips, &count, ANIM_OPTN_BAR_IN,
                     home->hover_frame, NULL);
        }
    }
    if (home->phase == WM_HOME_LEAVE) {
        add_clip(home, clips, &count, ANIM_HMMENU_BAR_PSH,
                 fminf(19.0f, home->frame), NULL);
        if (home->frame >= 19.0f) {
            add_clip(home, clips, &count, ANIM_HMMENU_FNSH,
                     home->frame - 19.0f, NULL);
        }
    }
    if (home->dialog != DIALOG_NONE && home->dialog != DIALOG_PRESS) {
        add_clip(home, clips, &count, ANIM_CMN_MSG_IN,
                 home->dialog == DIALOG_IN ? home->dialog_frame : 24.0f,
                 NULL);
        if (home->dialog == DIALOG_RETURN) {
            add_clip(home, clips, &count, ANIM_CMN_MSG_RTRN,
                     home->dialog_frame, NULL);
        }
        if (home->dialog == DIALOG_YES || home->dialog == DIALOG_NO ||
            home->dialog == DIALOG_FADE) {
            add_clip(home, clips, &count, ANIM_CMN_MSG_BTN_PSH,
                     home->dialog_frame,
                     home->dialog == DIALOG_NO ? "msgBtn_01_psh"
                                               : "msgBtn_00_psh");
        }
        if (home->hover == WM_HOME_CONTROL_YES ||
            home->hover == WM_HOME_CONTROL_NO) {
            add_clip(home, clips, &count, ANIM_CMN_MSG_BTN_IN,
                     home->hover_frame,
                     home->hover == WM_HOME_CONTROL_YES
                         ? "msgBtn_00_inOut" : "msgBtn_01_inOut");
        }
    }
    if (home->reconnect != RECONNECT_NONE &&
        home->reconnect != RECONNECT_PRESS) {
        add_clip(home, clips, &count, ANIM_LINK_MSG_IN,
                 home->reconnect == RECONNECT_IN
                     ? home->reconnect_frame : 119.0f, NULL);
        if (home->reconnect != RECONNECT_IN &&
            home->reconnect != RECONNECT_RETRY) {
            add_clip(home, clips, &count, ANIM_12BTN_ON,
                     fmodf(home->reconnect_prompt_frame, 50.0f), NULL);
        }
        if (home->reconnect == RECONNECT_OUT) {
            add_clip(home, clips, &count, ANIM_LINK_MSG_OUT,
                     home->reconnect_frame, NULL);
        }
    }
    for (size_t index = 0; index < home->effect_count; index++) {
        const HomeEffect *effect = &home->effects[index];
        add_clip(home, clips, &count, effect->animation, effect->frame,
                 effect->group);
    }
    if (!wm_layout_pose(home->layout, clips, count)) return;

    for (size_t player = 0; player < 4; player++) {
        for (size_t bar = 0; bar < 4; bar++) {
            char pane[32];
            snprintf(pane, sizeof(pane), "btryPwr_0%zu_%zu", player, bar);
            wm_layout_set_pane_visible(
                home->layout, pane,
                home->remote.controllers[player].connected &&
                    bar < home->remote.controllers[player].battery);
        }
    }
    wm_layout_set_pane_visible(home->layout, "back_02", false);
    wm_layout_set_pane_visible(home->layout, "let_icn_00", false);
    bool dialog_visible = home->dialog != DIALOG_NONE &&
                          home->dialog != DIALOG_PRESS;
    wm_layout_set_pane_visible(home->layout, "T_Dialog", dialog_visible);
    wm_layout_set_pane_visible(home->layout, "N_Dialog", dialog_visible);
    bool reconnect_visible = home->reconnect != RECONNECT_NONE &&
                             home->reconnect != RECONNECT_PRESS;
    wm_layout_set_pane_visible(home->layout, "T_msg_00", reconnect_visible);
    wm_layout_set_pane_visible(home->layout, "T_msg_01", reconnect_visible);
    static const char *const option_buttons[] = {
        "B_optnBtn_00", "B_optnBtn_01", "B_optnBtn_10",
        "B_optnBtn_11", "B_optnBtn_20"
    };
    for (size_t index = 0; index < 5; index++) {
        wm_layout_set_pane_visible(home->layout, option_buttons[index],
                                   home->options_open ||
                                       (closing && !ready));
    }
    home->pose_dirty = false;
    home->hit_dirty = true;
}

WmHomeControl wm_home_overlay_hit(WmHomeOverlay *home, int x, int y)
{
    if (!wm_home_overlay_ready(home)) return WM_HOME_CONTROL_NONE;
    pose_home_layout(home);
    if (home->hit_dirty) {
        for (int control = WM_HOME_CONTROL_CLOSE;
             control <= WM_HOME_CONTROL_NO; control++) {
            const char *pane = control_pane_name((WmHomeControl)control);
            home->control_rect_valid[control] =
                control_enabled(home, (WmHomeControl)control) && pane &&
                wm_source_pane_rect(home->layout, pane, true, WM_LAYOUT_IPL,
                                    NULL, &home->control_rects[control]);
        }
        home->hit_dirty = false;
    }
    for (int control = WM_HOME_CONTROL_CLOSE;
         control <= WM_HOME_CONTROL_NO; control++) {
        if (!control_enabled(home, (WmHomeControl)control) ||
            !home->control_rect_valid[control]) continue;
        const WmSourceRect *rect = &home->control_rects[control];
        if ((float)x >= rect->x && (float)x < rect->x + rect->width &&
            (float)y >= rect->y && (float)y < rect->y + rect->height) {
            return (WmHomeControl)control;
        }
    }
    return WM_HOME_CONTROL_NONE;
}

bool wm_home_overlay_draw(WmHomeOverlay *home)
{
    if (!wm_home_overlay_active(home) || !home->layout) return false;
    pose_home_layout(home);
    wm_layout_present_with_fonts(home->platform, home->textures, home->fonts,
                                 home->layout, true, WM_LAYOUT_IPL, NULL);
    return true;
}

void wm_home_overlay_draw_fade(WmHomeOverlay *home)
{
    float alpha = wm_home_overlay_fade_alpha(home);
    if (alpha <= 0.0f) return;
    const WmQuad blackout = {
        .x = 0.0f,
        .y = 0.0f,
        .width = WM_FRAME_WIDTH,
        .height = WM_FRAME_HEIGHT,
        .u1 = 1.0f,
        .v1 = 1.0f,
        .color = {0.0f, 0.0f, 0.0f, alpha}
    };
    wm_platform_draw_quad(home->platform, &blackout);
}
