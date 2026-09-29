#include "home_overlay_internal.h"

#include "wii_menu/layout/layout_assets.h"
#include "wii_menu/render/material_prepare.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

const char *const animation_suffixes[HOME_ANIMATION_COUNT] = {
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

bool wm_home_options_ready(const WmHomeOverlay *home)
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
           wm_home_options_ready(home) && home->rumble_lock <= 0.0f &&
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
    home->layout = wm_layout_load_asset(
        assets_directory, "layouts/homeBtn1/th_HomeBtn_d.json", "HOME");
    if (!home->layout) {
        free(home);
        return NULL;
    }
    for (size_t index = 0; index < HOME_ANIMATION_COUNT; index++) {
        int length = snprintf(home->animation_names[index],
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

bool wm_home_control_enabled(const WmHomeOverlay *home, WmHomeControl control)
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
    WmHomeControl next = wm_home_control_enabled(home, control)
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
    if (!wm_home_control_enabled(home, control)) return false;
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
