#ifndef WM_RESOURCE_SCENE_INTERNAL_H
#define WM_RESOURCE_SCENE_INTERNAL_H

#include "wii_menu/scenes/resource_scene.h"

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

/* Opaque publicly. The controller, interaction clock, and renderer borrow
 * the same layout/cache objects; only create/destroy owns their lifetime. */
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

extern const float wm_resource_scene_sd_position[12];

void wm_resource_scene_draw_balloons(WmResourceScene *scene, const WmMenu *menu,
                                     const WmBoardScene *board);

/* The scene has no separate update entry point. These functions advance
 * authored cue state at the existing draw-time clock boundary; keep the
 * calls before geometry submission to preserve cue timing. */
void wm_resource_scene_advance_interactions(WmResourceScene *scene, const WmMenu *menu,
                                            float elapsed_seconds, WmHit hover,
                                            bool suppress_balloons);
void wm_resource_scene_advance_board_balloon_state(WmResourceScene *scene,
                                                   const WmBoardScene *board,
                                                   WmBoardHit hover,
                                                   float elapsed_seconds);

#endif
