#ifndef WII_MENU_ARROW_INTERACTION_H
#define WII_MENU_ARROW_INTERACTION_H

#include <stdbool.h>

enum { WM_ARROW_PREVIOUS = 0, WM_ARROW_NEXT = 1, WM_ARROW_COUNT = 2 };

/* Clip lengths are source frames. A scene still chooses the animation names,
 * groups, and entry/exit pose belonging to its own WAD layout. */
typedef struct WmArrowInteractionConfig {
    float focus_in_frames;
    float focus_out_frames;
    float press_frames;
    float visibility_frames;
    bool clear_focus_out_at_end;
    bool clear_press_at_end;
} WmArrowInteractionConfig;

typedef struct WmArrowSideState {
    bool visible;
    float visibility_age;
    bool focus_active;
    bool focus_entering;
    float focus_age;
    bool pressed;
    float press_age;
} WmArrowSideState;

typedef struct WmArrowInteraction {
    WmArrowInteractionConfig config;
    WmArrowSideState sides[WM_ARROW_COUNT];
    int hovered;
} WmArrowInteraction;

void wm_arrow_interaction_init(WmArrowInteraction *interaction,
                               WmArrowInteractionConfig config);
void wm_arrow_interaction_reset(WmArrowInteraction *interaction,
                                bool previous_visible, bool next_visible,
                                float visibility_age);
void wm_arrow_interaction_reset_feedback(WmArrowInteraction *interaction);
bool wm_arrow_interaction_set_visible(WmArrowInteraction *interaction,
                                      int side, bool visible);
bool wm_arrow_interaction_hover(WmArrowInteraction *interaction, int side);
bool wm_arrow_interaction_press(WmArrowInteraction *interaction, int side,
                                float initial_age);
void wm_arrow_interaction_min_press_age(WmArrowInteraction *interaction,
                                        int side, float age);
void wm_arrow_interaction_advance(WmArrowInteraction *interaction,
                                  float frames);
const WmArrowSideState *wm_arrow_interaction_side(
    const WmArrowInteraction *interaction, int side);

#endif
