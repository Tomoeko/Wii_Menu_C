#ifndef WII_MENU_SCENE_FADER_H
#define WII_MENU_SCENE_FADER_H

#include <stdbool.h>

typedef enum WmSceneFaderPhase {
    WM_SCENE_FADER_IDLE,
    WM_SCENE_FADER_OUT,
    WM_SCENE_FADER_IN
} WmSceneFaderPhase;

typedef struct WmSceneFader {
    WmSceneFaderPhase phase;
    float frame;
} WmSceneFader;

/* USA 4.3 ColorFader::calc: visible alpha endpoints and completion are
 * separate. Out completes at update 23, In at update 22. */
float wm_native_fade_alpha(float updates, bool revealing);

/* Health SeenOut hands off at black. The original menu holds that black
 * output for 23 updates while resources become ready, then reveals the
 * stationary grid through the native 22-update ColorFader. */
float wm_menu_entrance_alpha(float updates_after_health);
bool wm_menu_entrance_complete(float updates_after_health);

/* Begin while the outgoing scene is still on screen. Advance returns true
 * exactly once, when the caller should switch scenes behind full black.
 * During either phase the source scene controllers should pause. */
bool wm_scene_fader_start(WmSceneFader *fader);
bool wm_scene_fader_advance(WmSceneFader *fader, float frames);
float wm_scene_fader_alpha(const WmSceneFader *fader);
bool wm_scene_fader_active(const WmSceneFader *fader);

#endif
