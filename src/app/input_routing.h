#ifndef WM_APP_INPUT_ROUTING_H
#define WM_APP_INPUT_ROUTING_H

#include "wii_menu/animation/scene_fader.h"
#include "wii_menu/audio/audio.h"
#include "wii_menu/board/board_scene.h"
#include "wii_menu/menu/menu.h"
#include "wii_menu/platform/platform.h"
#include "wii_menu/scenes/options_scene.h"
#include "wii_menu/scenes/resource_scene.h"

typedef struct WmAppSceneFade {
    WmSceneFader clock;
    WmScreen destination;
} WmAppSceneFade;

WmBoardDate wm_app_today_date(void);
bool wm_app_play_board_compose_cues_with_non_scroll(WmAudio *audio, WmBoardScene *board,
                                                    bool *non_scroll_played);
bool wm_app_play_board_compose_cues(WmAudio *audio, WmBoardScene *board);
void wm_app_play_board_sound_events(WmAudio *audio, WmBoardScene *board);
void wm_app_activate_hit(WmMenu *menu, WmAudio *audio, WmResourceScene *resource_scene,
                         WmBoardScene *board_scene, WmOptionsScene *options_scene,
                         WmAppSceneFade *fade, WmHit hit);
void wm_app_handle_key(WmMenu *menu, WmAudio *audio, WmResourceScene *resource_scene,
                       WmBoardScene *board_scene, WmOptionsScene *options_scene,
                       WmAppSceneFade *fade, WmKey key, int *focused_slot);

#endif
