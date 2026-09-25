#ifndef WII_MENU_CHANNEL_ANIMATION_H
#define WII_MENU_CHANNEL_ANIMATION_H

#include "wii_menu/layout_runtime.h"

#include <stdbool.h>
#include <stddef.h>

typedef enum WmChannelAnimationKind {
    WM_CHANNEL_ICON,
    WM_CHANNEL_BANNER
} WmChannelAnimationKind;

/* Optional source-font measurement, in layout units. The string may be a
 * newline-delimited slice and is not necessarily NUL-terminated at length. */
typedef float (*WmChannelMeasureText)(void *context, const WmLayout *layout,
                                       const WmLayoutPaneState *pane,
                                       const char *utf8, size_t length);

typedef struct WmChannelAnimationOptions {
    const char *language; /* JPN, ENG, GER, FRA, SPA, ITA, or NED; default ENG. */
    bool network_configured;
    bool custom_banner;
    bool has_base_frame;
    float base_frame; /* Independent banner intro clock, when supplied. */
    WmChannelMeasureText measure_text;
    void *measure_context;
} WmChannelAnimationOptions;

enum { WM_CHANNEL_ANIMATION_MAX_CLIPS = 18 };

typedef struct WmChannelAnimationClip {
    char animation[128]; /* Actual name from the imported layout. */
    char group[16]; /* Empty if the matching Rso group is absent. */
    float frame; /* Already scheduled, so play forward and hold. */
} WmChannelAnimationClip;

typedef struct WmChannelAnimationPlan {
    WmChannelAnimationClip clips[WM_CHANNEL_ANIMATION_MAX_CLIPS];
    size_t count;
} WmChannelAnimationPlan;

/* FrameController::calc: one initial pass may start below the loop minimum. */
float wm_channel_animation_frame(float elapsed, float minimum, float maximum,
                                  float initial, float speed, bool loop);

/* The ID must be exactly 16 hexadecimal characters. Its last four decoded
 * bytes select a known first-party schedule; other IDs use base animations. */
bool wm_channel_animation_plan(const WmLayout *layout, const char *title_id,
                                WmChannelAnimationKind kind, float elapsed_frames,
                                const WmChannelAnimationOptions *options,
                                WmChannelAnimationPlan *plan);

/* Rebuild the pose from the imported layout, then apply source branch, language,
 * and text choices. Missing optional panes and clips are ignored. */
bool wm_channel_animation_pose(WmLayout *layout, const char *title_id,
                                WmChannelAnimationKind kind, float elapsed_frames,
                                const WmChannelAnimationOptions *options);

#endif
