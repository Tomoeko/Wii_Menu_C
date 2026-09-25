#include "wii_menu/arrow_interaction.h"

#include <math.h>
#include <string.h>

static bool valid_side(int side)
{
    return side >= 0 && side < WM_ARROW_COUNT;
}

static float clamp_age(float age, float duration)
{
    return fminf(fmaxf(age, 0.0f), fmaxf(duration, 0.0f));
}

void wm_arrow_interaction_init(WmArrowInteraction *interaction,
                               WmArrowInteractionConfig config)
{
    if (!interaction) return;
    memset(interaction, 0, sizeof(*interaction));
    interaction->config = config;
    interaction->hovered = -1;
    for (int side = 0; side < WM_ARROW_COUNT; side++)
        interaction->sides[side].focus_entering = true;
}

void wm_arrow_interaction_reset_feedback(WmArrowInteraction *interaction)
{
    if (!interaction) return;
    interaction->hovered = -1;
    for (int side = 0; side < WM_ARROW_COUNT; side++) {
        WmArrowSideState *state = &interaction->sides[side];
        state->focus_active = false;
        state->focus_entering = true;
        state->focus_age = 0.0f;
        state->pressed = false;
        state->press_age = 0.0f;
    }
}

void wm_arrow_interaction_reset(WmArrowInteraction *interaction,
                                bool previous_visible, bool next_visible,
                                float visibility_age)
{
    if (!interaction) return;
    wm_arrow_interaction_reset_feedback(interaction);
    interaction->sides[WM_ARROW_PREVIOUS].visible = previous_visible;
    interaction->sides[WM_ARROW_NEXT].visible = next_visible;
    for (int side = 0; side < WM_ARROW_COUNT; side++) {
        interaction->sides[side].visibility_age = clamp_age(
            visibility_age, interaction->config.visibility_frames);
    }
}

bool wm_arrow_interaction_set_visible(WmArrowInteraction *interaction,
                                      int side, bool visible)
{
    if (!interaction || !valid_side(side)) return false;
    WmArrowSideState *state = &interaction->sides[side];
    if (state->visible == visible) return false;
    state->visible = visible;
    state->visibility_age = 0.0f;
    return true;
}

bool wm_arrow_interaction_hover(WmArrowInteraction *interaction, int side)
{
    if (!interaction) return false;
    if (!valid_side(side)) side = -1;
    if (interaction->hovered == side) return false;
    if (interaction->hovered >= 0) {
        WmArrowSideState *old = &interaction->sides[interaction->hovered];
        old->focus_active = true;
        old->focus_entering = false;
        old->focus_age = 0.0f;
    }
    interaction->hovered = side;
    if (side >= 0) {
        WmArrowSideState *next = &interaction->sides[side];
        next->focus_active = true;
        next->focus_entering = true;
        next->focus_age = 0.0f;
    }
    return true;
}

bool wm_arrow_interaction_press(WmArrowInteraction *interaction, int side,
                                float initial_age)
{
    if (!interaction || !valid_side(side) || !isfinite(initial_age) ||
        initial_age < 0.0f) return false;
    WmArrowSideState *state = &interaction->sides[side];
    state->pressed = true;
    state->press_age = clamp_age(initial_age, interaction->config.press_frames);
    return true;
}

void wm_arrow_interaction_min_press_age(WmArrowInteraction *interaction,
                                        int side, float age)
{
    if (!interaction || !valid_side(side) || !isfinite(age) || age < 0.0f)
        return;
    WmArrowSideState *state = &interaction->sides[side];
    if (state->pressed)
        state->press_age = fmaxf(state->press_age,
                                clamp_age(age, interaction->config.press_frames));
}

void wm_arrow_interaction_advance(WmArrowInteraction *interaction,
                                  float frames)
{
    if (!interaction || !isfinite(frames) || frames < 0.0f) return;
    for (int side = 0; side < WM_ARROW_COUNT; side++) {
        WmArrowSideState *state = &interaction->sides[side];
        state->visibility_age = clamp_age(
            state->visibility_age + frames,
            interaction->config.visibility_frames);
        if (state->focus_active) {
            float duration = state->focus_entering
                                 ? interaction->config.focus_in_frames
                                 : interaction->config.focus_out_frames;
            state->focus_age = clamp_age(state->focus_age + frames, duration);
            if (!state->focus_entering &&
                interaction->config.clear_focus_out_at_end &&
                state->focus_age >= duration) {
                state->focus_active = false;
            }
        }
        if (state->pressed) {
            float next_age = state->press_age + frames;
            float duration = interaction->config.press_frames;
            bool finished = interaction->config.clear_press_at_end
                                ? next_age >= duration : next_age > duration;
            if (finished) state->pressed = false;
            else state->press_age = next_age;
        }
    }
}

const WmArrowSideState *wm_arrow_interaction_side(
    const WmArrowInteraction *interaction, int side)
{
    return interaction && valid_side(side) ? &interaction->sides[side] : NULL;
}
