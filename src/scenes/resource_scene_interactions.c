#include "resource_scene_internal.h"

#include <math.h>
#include <stdio.h>
#include <string.h>

static int footer_hover_index(WmHit hover) {
    switch (hover.type) {
        case WM_HIT_SETTINGS:
            return 0;
        case WM_HIT_BOARD:
            return 1;
        case WM_HIT_PAGE_PREVIOUS:
            return 2;
        case WM_HIT_PAGE_NEXT:
            return 3;
        default:
            return -1;
    }
}

static void balloon_target(WmResourceScene *scene, int target) {
    if (target == scene->balloon_target)
        return;
    int old = scene->balloon_target;
    if (old >= 0) {
        BalloonAnimation *state = &scene->balloons[old];
        if (state->phase == BALLOON_WAIT)
            state->phase = BALLOON_NONE;
        else if (old >= WM_SLOT_COUNT)
            state->phase = BALLOON_LEAVE;
        else
            state->leaving = true;
    }
    scene->balloon_target = target;
    if (target >= 0) {
        BalloonAnimation *state = &scene->balloons[target];
        if (target < WM_SLOT_COUNT && state->phase != BALLOON_NONE) {
            /* ChannelObj lets an existing bubble finish its exit when the
             * pointer returns, then starts a fresh wait. Replacing it with
             * WAIT here makes the visible bubble disappear in one frame. */
            state->leaving = false;
        } else {
            *state = (BalloonAnimation){.phase = BALLOON_WAIT, .frame = 0.0f};
        }
    }
}

void wm_resource_scene_dismiss_balloon(WmResourceScene *scene) {
    if (!scene || scene->balloon_target < 0)
        return;
    scene->dismissed_balloon = scene->balloon_target;
    scene->fade_dismissed_balloon = false;
    balloon_target(scene, -1);
}

void wm_resource_scene_retire_footer_focus(WmResourceScene *scene) {
    if (!scene)
        return;
    memset(scene->footer_states, 0, sizeof(scene->footer_states));
    scene->hovered_footer = -1;
}

void wm_resource_scene_pointer_moved(WmResourceScene *scene) {
    if (!scene || !scene->fade_dismissed_balloon)
        return;
    scene->dismissed_balloon = -1;
    scene->fade_dismissed_balloon = false;
}

static int available_balloon_target(WmResourceScene *scene, int target) {
    if (target == scene->dismissed_balloon)
        return -1;
    scene->dismissed_balloon = -1;
    scene->fade_dismissed_balloon = false;
    return target;
}

static void advance_balloons(WmResourceScene *scene, float frames) {
    for (int index = 0; index < BALLOON_COUNT; index++) {
        BalloonAnimation *state = &scene->balloons[index];
        if (index < WM_SLOT_COUNT) {
            if (state->phase == BALLOON_NONE)
                continue;
            state->frame += frames;
            const float duration = scene->balloon_end + 1.0f;
            switch (state->phase) {
                case BALLOON_WAIT:
                    if (state->frame + 0.0001f >= 20.0f) {
                        state->frame = fmaxf(0.0f, state->frame - 20.0f);
                        state->phase = BALLOON_ENTER;
                        scene->balloon_sound_pending = true;
                    }
                    break;
                case BALLOON_ENTER:
                    if (state->frame >= duration) {
                        state->frame = duration;
                        state->phase = BALLOON_HOLD;
                    }
                    break;
                case BALLOON_HOLD:
                    if (state->leaving) {
                        state->frame = 0.0f;
                        state->phase = BALLOON_LEAVE;
                    }
                    break;
                case BALLOON_LEAVE:
                    if (state->frame >= duration) {
                        *state = index == scene->balloon_target
                                     ? (BalloonAnimation){.phase = BALLOON_WAIT}
                                     : (BalloonAnimation){0};
                    }
                    break;
                case BALLOON_NONE:
                    break;
            }
            continue;
        }
        switch (state->phase) {
            case BALLOON_WAIT:
                state->frame += frames;
                /* Seconds arrive as float from the frame clock. Treat a
                 * tiny rounding shortfall as the authored update boundary
                 * so 17 successive 60 Hz samples cannot become 18. */
                if (state->frame + 0.0001f >= 17.0f) {
                    state->frame = fmaxf(0.0f, state->frame - 17.0f);
                    state->phase = BALLOON_ENTER;
                    scene->balloon_sound_pending = true;
                }
                break;
            case BALLOON_ENTER:
                state->frame = fminf(scene->balloon_end, state->frame + frames);
                if (state->frame >= scene->balloon_end) {
                    state->phase = state->leaving ? BALLOON_LEAVE : BALLOON_HOLD;
                }
                break;
            case BALLOON_HOLD:
                if (state->leaving)
                    state->phase = BALLOON_LEAVE;
                break;
            case BALLOON_LEAVE:
                state->frame = fmaxf(0.0f, state->frame - frames);
                if (state->frame == 0.0f)
                    state->phase = BALLOON_NONE;
                break;
            case BALLOON_NONE:
                break;
        }
    }
}

bool wm_resource_scene_take_balloon_sound(WmResourceScene *scene) {
    if (!scene)
        return false;
    bool pending = scene->balloon_sound_pending;
    scene->balloon_sound_pending = false;
    return pending;
}

void wm_resource_scene_set_message_badge(WmResourceScene *scene, unsigned today_count,
                                         bool new_mail) {
    if (!scene)
        return;
    unsigned count = today_count > 99 ? 99 : today_count;
    if (count != scene->message_badge_count) {
        char label[4] = {0};
        if (count)
            snprintf(label, sizeof(label), "%u", count);
        wm_layout_set_text(scene->footer, "T_BbsMark1", label);
        scene->message_badge_count = count;
    }
    bool active = count > 0 && new_mail;
    if (active != scene->new_mail_active) {
        scene->new_mail_active = active;
        scene->new_mail_age = 0.0f;
        if (active)
            scene->new_mail_sound_pending = true;
        else
            scene->new_mail_sound_pending = false;
    }
}

bool wm_resource_scene_take_new_mail_sound(WmResourceScene *scene) {
    if (!scene)
        return false;
    bool pending = scene->new_mail_sound_pending;
    scene->new_mail_sound_pending = false;
    return pending;
}

void wm_resource_scene_advance_interactions(WmResourceScene *scene, const WmMenu *menu,
                                            float elapsed_seconds, WmHit hover,
                                            bool suppress_balloons) {
    float frames = fmaxf(0.0f, elapsed_seconds - scene->last_draw_seconds) * 60.0f;
    scene->last_draw_seconds = elapsed_seconds;
    if (scene->new_mail_active && frames > 0.0f) {
        scene->new_mail_age += frames;
        if (scene->new_mail_age >= 180.0f) {
            scene->new_mail_age = fmodf(scene->new_mail_age, 180.0f);
            scene->new_mail_sound_pending = true;
        }
    }

    int slot = hover.type == WM_HIT_CHANNEL ? hover.slot : -1;
    if (slot < 0 || slot >= WM_SLOT_COUNT || menu->transition != WM_TRANSITION_NONE)
        slot = -1;
    if (slot != scene->hovered_slot) {
        if (scene->hovered_slot >= 0) {
            scene->focus_states[scene->hovered_slot].leaving = true;
        }
        scene->hovered_slot = slot;
        if (slot >= 0) {
            FocusAnimation *focus = &scene->focus_states[slot];
            if (!focus->active || focus->off_phase) {
                *focus = (FocusAnimation){.active = true};
            } else {
                focus->leaving = false;
            }
        }
    }
    int bubble = slot;
    if (bubble < 0 && hover.type == WM_HIT_SETTINGS)
        bubble = BALLOON_SETTINGS;
    if (bubble < 0 && hover.type == WM_HIT_BOARD)
        bubble = BALLOON_BOARD;
    if (bubble < 0 && hover.type == WM_HIT_SD)
        bubble = BALLOON_SD;
    if (suppress_balloons) {
        memset(scene->balloons, 0, sizeof(scene->balloons));
        scene->balloon_target = -1;
        scene->dismissed_balloon = bubble;
        scene->fade_dismissed_balloon = bubble >= 0;
        scene->balloon_sound_pending = false;
    } else {
        balloon_target(scene, available_balloon_target(scene, bubble));
        advance_balloons(scene, frames);
    }
    for (int index = 0; index < WM_SLOT_COUNT; index++) {
        FocusAnimation *focus = &scene->focus_states[index];
        if (!focus->active)
            continue;
        focus->frame += frames;
        if (!focus->off_phase && focus->frame >= 5.0f && focus->leaving) {
            focus->off_phase = true;
            focus->frame = 0.0f;
        }
        if (focus->off_phase && focus->frame >= 30.0f) {
            focus->active = false;
        }
    }

    int footer = footer_hover_index(hover);
    if (footer != scene->hovered_footer) {
        if (scene->hovered_footer >= 0) {
            scene->footer_states[scene->hovered_footer] =
                (HoverAnimation){.active = true, .entering = false};
        }
        scene->hovered_footer = footer;
        if (footer >= 0) {
            scene->footer_states[footer] =
                (HoverAnimation){.active = true, .entering = true};
        }
    }
    for (size_t index = 0; index < 4; index++) {
        if (scene->footer_states[index].active) {
            scene->footer_states[index].frame += frames;
        }
    }

    bool sd_hovered = hover.type == WM_HIT_SD;
    if (scene->sd_hovered != sd_hovered) {
        scene->sd_hovered = sd_hovered;
        scene->sd_hover_changed_at = elapsed_seconds * 60.0f;
    }

    if (menu->page != scene->previous_page) {
        int direction = menu->page > scene->previous_page ? 1 : -1;
        scene->arrow_press_age[direction > 0 ? 1 : 0] = 0.0f;
        scene->previous_page = menu->page;
    }
    bool zooming = menu->transition == WM_TRANSITION_SELECT ||
                   menu->transition == WM_TRANSITION_BACK;
    bool visible[] = {!zooming && menu->page > 0,
                      !zooming && menu->page < WM_PAGE_COUNT - 1};
    for (size_t index = 0; index < 2; index++) {
        if (scene->arrow_visible[index] != visible[index]) {
            scene->arrow_visible[index] = visible[index];
            scene->arrow_age[index] = 0.0f;
        }
    }
    for (size_t index = 0; index < 2; index++) {
        scene->arrow_age[index] += frames;
        if (scene->arrow_press_age[index] >= 0.0f) {
            scene->arrow_press_age[index] += frames;
            if (scene->arrow_press_age[index] > 30.0f) {
                scene->arrow_press_age[index] = -1.0f;
            }
        }
    }
}
void wm_resource_scene_advance_board_balloon_state(WmResourceScene *scene,
                                                   const WmBoardScene *board,
                                                   WmBoardHit hover,
                                                   float elapsed_seconds) {
    int target = -1;
    if (wm_board_scene_phase(board) == WM_BOARD_READY &&
        wm_board_scene_child(board) == WM_BOARD_CHILD_NONE &&
        !wm_board_scene_dragging(board)) {
        if (hover.control == WM_BOARD_CONTROL_BACK)
            target = BALLOON_BOARD_BACK;
        else if (hover.control == WM_BOARD_CONTROL_CALENDAR)
            target = BALLOON_CALENDAR;
        else if (hover.control == WM_BOARD_CONTROL_CREATE)
            target = BALLOON_CREATE;
    }
    balloon_target(scene, available_balloon_target(scene, target));
    float frames = fmaxf(0.0f, elapsed_seconds - scene->last_draw_seconds) * 60.0f;
    scene->last_draw_seconds = elapsed_seconds;
    advance_balloons(scene, frames);
}
