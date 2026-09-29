#ifndef WM_HOME_OVERLAY_INTERNAL_H
#define WM_HOME_OVERLAY_INTERNAL_H

#include "wii_menu/scenes/home_overlay.h"
#include "wii_menu/input/source_hit.h"
#include "wii_menu/layout/layout_runtime.h"
#include "wii_menu/render/texture_cache.h"
#include "wii_menu/fonts/font_cache.h"

#include <stdbool.h>
#include <stddef.h>

enum {
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

extern const char *const animation_suffixes[HOME_ANIMATION_COUNT];

bool wm_home_options_ready(const WmHomeOverlay *home);
bool wm_home_control_enabled(const WmHomeOverlay *home, WmHomeControl control);

#endif
