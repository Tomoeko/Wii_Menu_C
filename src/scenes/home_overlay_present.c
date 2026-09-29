#include "home_overlay_internal.h"

#include "wii_menu/layout/layout_present.h"
#include "wii_menu/render/material_prepare.h"

#include <math.h>
#include <stdio.h>
#include <string.h>

static float clamped_frame(const WmHomeOverlay *home, HomeAnimation animation,
                           float frame) {
    float last = home->animation_frames[animation] - 1.0f;
    return fminf(fmaxf(frame, 0.0f), fmaxf(last, 0.0f));
}

static void add_clip(const WmHomeOverlay *home, WmLayoutClip clips[], size_t *count,
                     HomeAnimation animation, float frame, const char *group) {
    if (*count >= HOME_CLIP_CAPACITY)
        return;
    clips[*count] =
        (WmLayoutClip){.animation = home->animation_names[animation],
                       .frame = clamped_frame(home, animation, frame),
                       .group = group ? group : animation_suffixes[animation],
                       .recursive_group = false,
                       .loop_override = 0};
    (*count)++;
}

static const char *control_pane_name(WmHomeControl control) {
    switch (control) {
        case WM_HOME_CONTROL_CLOSE:
            return "B_btn_00";
        case WM_HOME_CONTROL_RETURN:
            return "B_btnL_00";
        case WM_HOME_CONTROL_OPTIONS:
            return "B_bar_10";
        case WM_HOME_CONTROL_VOLUME_DOWN:
            return "B_optnBtn_00";
        case WM_HOME_CONTROL_VOLUME_UP:
            return "B_optnBtn_01";
        case WM_HOME_CONTROL_RUMBLE_ON:
            return "B_optnBtn_10";
        case WM_HOME_CONTROL_RUMBLE_OFF:
            return "B_optnBtn_11";
        case WM_HOME_CONTROL_RECONNECT:
            return "B_optnBtn_20";
        case WM_HOME_CONTROL_YES:
            return "B_BtnA";
        case WM_HOME_CONTROL_NO:
            return "B_BtnB";
        default:
            return NULL;
    }
}

static void add_remote_clips(WmHomeOverlay *home, WmLayoutClip clips[], size_t *count) {
    static const char *const player_groups[4] = {"plyr_00", "plyr_01", "plyr_02",
                                                 "plyr_03"};
    for (size_t player = 0; player < 4; player++) {
        const WmHomeRemote *remote = &home->remote.controllers[player];
        add_clip(home, clips, count, remote->connected ? ANIM_BTRY_WHT : ANIM_BTRY_GRY,
                 0.0f, player_groups[player]);
        if (remote->connected && remote->battery < 2) {
            add_clip(home, clips, count, ANIM_BTRY_RED, 0.0f, player_groups[player]);
        }
    }

    static const char *const volume_groups[10] = {
        "vol_00", "vol_01", "vol_02", "vol_03", "vol_04",
        "vol_05", "vol_06", "vol_07", "vol_08", "vol_09"};
    int filled = home->remote.muted ? 0 : (int)lroundf(home->remote.volume * 10.0f);
    for (size_t bar = 0; bar < 10; bar++) {
        add_clip(home, clips, count,
                 (int)bar < filled ? ANIM_SOUND_YLW : ANIM_SOUND_GRY, 0.0f,
                 volume_groups[bar]);
    }
    add_clip(home, clips, count,
             home->remote.rumble ? ANIM_VB_BTN_WHT_PSH : ANIM_VB_BTN_YLW_PSH, 24.0f,
             "optnBtn_10_psh");
    add_clip(home, clips, count,
             home->remote.rumble ? ANIM_VB_BTN_YLW_PSH : ANIM_VB_BTN_WHT_PSH, 24.0f,
             "optnBtn_11_psh");
}

static void add_options_clips(WmHomeOverlay *home, WmLayoutClip clips[], size_t *count,
                              bool closing, bool ready) {
    if (home->options_open || closing) {
        float opening_frame =
            closing ? home->animation_frames[ANIM_OPTN_BAR_PSH] : home->options_frame;
        add_clip(home, clips, count, ANIM_OPTN_BAR_PSH, opening_frame, NULL);
        add_clip(home, clips, count, ANIM_CNTRL_UP, opening_frame, NULL);
        if (opening_frame >= 16.0f) {
            add_clip(home, clips, count, ANIM_CNTRL_WNDW_OPN, opening_frame - 16.0f,
                     NULL);
        }
        if (closing) {
            add_clip(home, clips, count, ANIM_HMMENU_BAR_PSH, home->options_frame,
                     NULL);
            add_clip(home, clips, count, ANIM_CLOSE_BAR_PSH, home->options_frame, NULL);
            add_clip(home, clips, count, ANIM_CNTRL_DWN, home->options_frame, NULL);
        }
        if (home->options_open && ready && home->hover == WM_HOME_CONTROL_OPTIONS) {
            add_clip(home, clips, count, ANIM_CLOSE_BAR_IN, home->hover_frame,
                     "optn_bar_in");
        }
        if (home->options_open && ready && home->hover >= WM_HOME_CONTROL_VOLUME_DOWN &&
            home->hover <= WM_HOME_CONTROL_RECONNECT) {
            static const char *const option_groups[] = {
                "optnBtn_00_inOut", "optnBtn_01_inOut", "optnBtn_10_inOut",
                "optnBtn_11_inOut", "optnBtn_20_inOut"};
            add_clip(home, clips, count, ANIM_OPTN_BTN_IN, home->hover_frame,
                     option_groups[home->hover - WM_HOME_CONTROL_VOLUME_DOWN]);
        }
    }
    if (!home->options_open && ready) {
        if (home->hover == WM_HOME_CONTROL_CLOSE) {
            add_clip(home, clips, count, ANIM_HMMENU_BAR_IN, home->hover_frame, NULL);
        } else if (home->hover == WM_HOME_CONTROL_RETURN) {
            add_clip(home, clips, count, ANIM_CNTBTN_IN, home->hover_frame,
                     "btnL_00_inOut");
        } else if (home->hover == WM_HOME_CONTROL_OPTIONS) {
            add_clip(home, clips, count, ANIM_OPTN_BAR_IN, home->hover_frame, NULL);
        }
    }
}

static void add_dialog_clips(WmHomeOverlay *home, WmLayoutClip clips[], size_t *count) {
    if (home->dialog == DIALOG_NONE || home->dialog == DIALOG_PRESS)
        return;
    add_clip(home, clips, count, ANIM_CMN_MSG_IN,
             home->dialog == DIALOG_IN ? home->dialog_frame : 24.0f, NULL);
    if (home->dialog == DIALOG_RETURN)
        add_clip(home, clips, count, ANIM_CMN_MSG_RTRN, home->dialog_frame, NULL);
    if (home->dialog == DIALOG_YES || home->dialog == DIALOG_NO ||
        home->dialog == DIALOG_FADE) {
        add_clip(home, clips, count, ANIM_CMN_MSG_BTN_PSH, home->dialog_frame,
                 home->dialog == DIALOG_NO ? "msgBtn_01_psh" : "msgBtn_00_psh");
    }
    if (home->hover == WM_HOME_CONTROL_YES || home->hover == WM_HOME_CONTROL_NO) {
        add_clip(home, clips, count, ANIM_CMN_MSG_BTN_IN, home->hover_frame,
                 home->hover == WM_HOME_CONTROL_YES ? "msgBtn_00_inOut"
                                                    : "msgBtn_01_inOut");
    }
}

static void add_reconnect_clips(WmHomeOverlay *home, WmLayoutClip clips[],
                                size_t *count) {
    if (home->reconnect == RECONNECT_NONE || home->reconnect == RECONNECT_PRESS)
        return;
    add_clip(home, clips, count, ANIM_LINK_MSG_IN,
             home->reconnect == RECONNECT_IN ? home->reconnect_frame : 119.0f, NULL);
    if (home->reconnect != RECONNECT_IN && home->reconnect != RECONNECT_RETRY) {
        add_clip(home, clips, count, ANIM_12BTN_ON,
                 fmodf(home->reconnect_prompt_frame, 50.0f), NULL);
    }
    if (home->reconnect == RECONNECT_OUT)
        add_clip(home, clips, count, ANIM_LINK_MSG_OUT, home->reconnect_frame, NULL);
}

static void update_home_visibility(WmHomeOverlay *home, bool closing, bool ready) {
    for (size_t player = 0; player < 4; player++) {
        const WmHomeRemote *remote = &home->remote.controllers[player];
        for (size_t bar = 0; bar < 4; bar++) {
            char pane[32];
            snprintf(pane, sizeof(pane), "btryPwr_0%zu_%zu", player, bar);
            wm_layout_set_pane_visible(home->layout, pane,
                                       remote->connected && bar < remote->battery);
        }
    }
    wm_layout_set_pane_visible(home->layout, "back_02", false);
    wm_layout_set_pane_visible(home->layout, "let_icn_00", false);
    bool dialog_visible = home->dialog != DIALOG_NONE && home->dialog != DIALOG_PRESS;
    wm_layout_set_pane_visible(home->layout, "T_Dialog", dialog_visible);
    wm_layout_set_pane_visible(home->layout, "N_Dialog", dialog_visible);
    bool reconnect_visible =
        home->reconnect != RECONNECT_NONE && home->reconnect != RECONNECT_PRESS;
    wm_layout_set_pane_visible(home->layout, "T_msg_00", reconnect_visible);
    wm_layout_set_pane_visible(home->layout, "T_msg_01", reconnect_visible);
    static const char *const option_buttons[] = {
        "B_optnBtn_00", "B_optnBtn_01", "B_optnBtn_10", "B_optnBtn_11", "B_optnBtn_20"};
    for (size_t index = 0; index < 5; index++) {
        wm_layout_set_pane_visible(home->layout, option_buttons[index],
                                   home->options_open || (closing && !ready));
    }
}

static void pose_home_layout(WmHomeOverlay *home) {
    if (!home || !home->layout || !home->pose_dirty)
        return;
    WmLayoutClip clips[HOME_CLIP_CAPACITY];
    size_t count = 0;
    add_clip(home, clips, &count, ANIM_HMMENU_STRT,
             home->phase == WM_HOME_ENTER ? home->frame : 21.0f, NULL);
    add_remote_clips(home, clips, &count);

    bool closing = home->options_closing && !home->options_open;
    bool ready = wm_home_options_ready(home);
    add_options_clips(home, clips, &count, closing, ready);
    if (home->phase == WM_HOME_LEAVE) {
        add_clip(home, clips, &count, ANIM_HMMENU_BAR_PSH, fminf(19.0f, home->frame),
                 NULL);
        if (home->frame >= 19.0f) {
            add_clip(home, clips, &count, ANIM_HMMENU_FNSH, home->frame - 19.0f, NULL);
        }
    }
    add_dialog_clips(home, clips, &count);
    add_reconnect_clips(home, clips, &count);
    for (size_t index = 0; index < home->effect_count; index++) {
        const HomeEffect *effect = &home->effects[index];
        add_clip(home, clips, &count, effect->animation, effect->frame, effect->group);
    }
    if (!wm_layout_pose(home->layout, clips, count))
        return;
    update_home_visibility(home, closing, ready);
    home->pose_dirty = false;
    home->hit_dirty = true;
}

WmHomeControl wm_home_overlay_hit(WmHomeOverlay *home, int x, int y) {
    if (!wm_home_overlay_ready(home))
        return WM_HOME_CONTROL_NONE;
    pose_home_layout(home);
    if (home->hit_dirty) {
        for (int control = WM_HOME_CONTROL_CLOSE; control <= WM_HOME_CONTROL_NO;
             control++) {
            const char *pane = control_pane_name((WmHomeControl)control);
            home->control_rect_valid[control] =
                wm_home_control_enabled(home, (WmHomeControl)control) && pane &&
                wm_source_pane_rect(home->layout, pane, true, WM_LAYOUT_IPL, NULL,
                                    &home->control_rects[control]);
        }
        home->hit_dirty = false;
    }
    for (int control = WM_HOME_CONTROL_CLOSE; control <= WM_HOME_CONTROL_NO;
         control++) {
        if (!wm_home_control_enabled(home, (WmHomeControl)control) ||
            !home->control_rect_valid[control])
            continue;
        const WmSourceRect *rect = &home->control_rects[control];
        if ((float)x >= rect->x && (float)x < rect->x + rect->width &&
            (float)y >= rect->y && (float)y < rect->y + rect->height) {
            return (WmHomeControl)control;
        }
    }
    return WM_HOME_CONTROL_NONE;
}

bool wm_home_overlay_draw(WmHomeOverlay *home) {
    if (!wm_home_overlay_active(home) || !home->layout)
        return false;
    pose_home_layout(home);
    wm_layout_present_with_fonts(home->platform, home->textures, home->fonts,
                                 home->layout, true, WM_LAYOUT_IPL, NULL);
    return true;
}

void wm_home_overlay_draw_fade(WmHomeOverlay *home) {
    float alpha = wm_home_overlay_fade_alpha(home);
    if (alpha <= 0.0f)
        return;
    const WmQuad blackout = {.x = 0.0f,
                             .y = 0.0f,
                             .width = WM_FRAME_WIDTH,
                             .height = WM_FRAME_HEIGHT,
                             .u1 = 1.0f,
                             .v1 = 1.0f,
                             .color = {0.0f, 0.0f, 0.0f, alpha}};
    wm_platform_draw_quad(home->platform, &blackout);
}
