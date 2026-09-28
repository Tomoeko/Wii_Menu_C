#ifndef WII_MENU_PREVIEW_SCENE_INTERNAL_H
#define WII_MENU_PREVIEW_SCENE_INTERNAL_H

#include "wii_menu/input/arrow_interaction.h"
#include "wii_menu/scenes/preview_scene.h"

typedef struct PreviewFocus {
    float frame;
    bool active;
    bool entering;
} PreviewFocus;

/* Layouts belong to the scene. Platform, texture cache, and font cache are
 * borrowed; controller and presentation share them without taking ownership. */
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
    bool shop_clock_started;
    int shop_clock_slot;
    float shop_clock_origin_seconds;
    float shop_clock_last_seconds;
};

bool preview_scene_pose_frame(WmLayout *layout, const char *animation, float frame);
void preview_scene_preload_banner_textures(WmPreviewScene *scene,
                                           const WmLayout *layout);
void preview_scene_reset_button_focus(WmPreviewScene *scene);
void preview_scene_advance_arrow_clocks(WmPreviewScene *scene, float delta);
void preview_scene_advance_focus(WmPreviewScene *scene, const WmMenu *menu, WmHit hover,
                                 float seconds);

#endif
