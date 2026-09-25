#include "wii_menu/arrow_interaction.h"

#include <assert.h>
#include <stdio.h>

static void check_common_footer(void)
{
    WmArrowInteraction arrows;
    wm_arrow_interaction_init(&arrows, (WmArrowInteractionConfig){
        .focus_in_frames = 15.0f,
        .focus_out_frames = 15.0f,
        .press_frames = 30.0f,
        .visibility_frames = 10.0f
    });
    const WmArrowSideState *left = wm_arrow_interaction_side(
        &arrows, WM_ARROW_PREVIOUS);
    const WmArrowSideState *right = wm_arrow_interaction_side(
        &arrows, WM_ARROW_NEXT);
    assert(left && right && !left->focus_active && !right->focus_active);
    assert(wm_arrow_interaction_set_visible(&arrows, WM_ARROW_PREVIOUS, true));
    assert(!wm_arrow_interaction_set_visible(&arrows, WM_ARROW_PREVIOUS, true));
    wm_arrow_interaction_advance(&arrows, 6.0f);
    assert(left->visibility_age == 6.0f);

    assert(wm_arrow_interaction_hover(&arrows, WM_ARROW_PREVIOUS));
    wm_arrow_interaction_advance(&arrows, 15.0f);
    assert(left->focus_active && left->focus_entering);
    assert(left->focus_age == 15.0f);
    assert(!wm_arrow_interaction_hover(&arrows, WM_ARROW_PREVIOUS));
    assert(left->focus_age == 15.0f);

    assert(wm_arrow_interaction_press(&arrows, WM_ARROW_PREVIOUS, 0.0f));
    wm_arrow_interaction_advance(&arrows, 20.0f);
    assert(left->pressed && left->press_age == 20.0f);
    wm_arrow_interaction_min_press_age(&arrows, WM_ARROW_PREVIOUS, 25.0f);
    assert(left->press_age == 25.0f);
    wm_arrow_interaction_advance(&arrows, 5.0f);
    assert(left->pressed && left->press_age == 30.0f);
    wm_arrow_interaction_advance(&arrows, 0.1f);
    assert(!left->pressed);

    assert(wm_arrow_interaction_hover(&arrows, WM_ARROW_NEXT));
    assert(left->focus_active && !left->focus_entering &&
           left->focus_age == 0.0f);
    assert(right->focus_active && right->focus_entering &&
           right->focus_age == 0.0f);
    wm_arrow_interaction_advance(&arrows, 15.0f);
    assert(left->focus_active && left->focus_age == 15.0f);
    assert(wm_arrow_interaction_hover(&arrows, -1));
    assert(!right->focus_entering && right->focus_age == 0.0f);
    wm_arrow_interaction_reset_feedback(&arrows);
    assert(arrows.hovered == -1 && !left->focus_active && !right->focus_active);
    assert(left->visible && left->visibility_age == 10.0f);
}

static void check_sd_footer(void)
{
    WmArrowInteraction arrows;
    wm_arrow_interaction_init(&arrows, (WmArrowInteractionConfig){
        .focus_in_frames = 13.0f,
        .focus_out_frames = 13.0f,
        .press_frames = 28.0f,
        .visibility_frames = 11.0f,
        .clear_focus_out_at_end = true,
        .clear_press_at_end = true
    });
    wm_arrow_interaction_reset(&arrows, false, true, 11.0f);
    const WmArrowSideState *left = wm_arrow_interaction_side(
        &arrows, WM_ARROW_PREVIOUS);
    const WmArrowSideState *right = wm_arrow_interaction_side(
        &arrows, WM_ARROW_NEXT);
    assert(!left->visible && right->visible);
    assert(left->visibility_age == 11.0f &&
           right->visibility_age == 11.0f);
    assert(wm_arrow_interaction_hover(&arrows, WM_ARROW_NEXT));
    wm_arrow_interaction_advance(&arrows, 13.0f);
    assert(right->focus_age == 13.0f);
    assert(wm_arrow_interaction_press(&arrows, WM_ARROW_NEXT, 0.0f));
    wm_arrow_interaction_advance(&arrows, 27.0f);
    assert(right->pressed && right->press_age == 27.0f);
    wm_arrow_interaction_advance(&arrows, 1.0f);
    assert(!right->pressed);

    assert(wm_arrow_interaction_set_visible(&arrows, WM_ARROW_PREVIOUS, true));
    assert(left->visibility_age == 0.0f);
    assert(!wm_arrow_interaction_set_visible(&arrows, WM_ARROW_NEXT, true));
    assert(right->visibility_age == 11.0f);
    assert(wm_arrow_interaction_hover(&arrows, WM_ARROW_PREVIOUS));
    wm_arrow_interaction_advance(&arrows, 11.0f);
    assert(left->visibility_age == 11.0f);
    assert(right->focus_active && !right->focus_entering);
    wm_arrow_interaction_advance(&arrows, 2.0f);
    assert(!right->focus_active);
    assert(!wm_arrow_interaction_press(&arrows, -1, 0.0f));
    assert(wm_arrow_interaction_side(&arrows, -1) == NULL);
}

int main(void)
{
    check_common_footer();
    check_sd_footer();
    puts("Shared arrow focus, press, and visibility timing passed.");
    return 0;
}
