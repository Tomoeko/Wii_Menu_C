#include "channel_drag_internal.h"

#include <math.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

static bool finite_length(float length) {
    return isfinite(length) && length >= 0.0f;
}

static bool valid_lengths(const WmChannelDragLengths *lengths) {
    return lengths && finite_length(lengths->mask_appear) &&
           finite_length(lengths->mask_lost) && finite_length(lengths->shade_appear) &&
           finite_length(lengths->shade_lost) && finite_length(lengths->drop_appear) &&
           finite_length(lengths->drop_lost);
}

static WmChannelDragState empty_state(void) {
    return (WmChannelDragState){
        .phase = WM_CHANNEL_DRAG_NONE, .source = -1, .target = -1};
}

WmChannelDrag *wm_channel_drag_create_controller(const WmChannelDragLengths *lengths) {
    if (!valid_lengths(lengths))
        return NULL;
    WmChannelDrag *drag = calloc(1, sizeof(*drag));
    if (!drag)
        return NULL;
    drag->lengths = *lengths;
    drag->state = empty_state();
    drag->revision = 1;
    return drag;
}

void wm_channel_drag_destroy(WmChannelDrag *drag) {
    if (!drag)
        return;
    wm_channel_drag_presentation_destroy(drag->presentation);
    free(drag);
}

WmChannelDragState wm_channel_drag_state(const WmChannelDrag *drag) {
    return drag ? drag->state : empty_state();
}

bool wm_channel_drag_active(const WmChannelDrag *drag) {
    return drag && drag->state.phase != WM_CHANNEL_DRAG_NONE;
}

bool wm_channel_drag_start(WmChannelDrag *drag, int index, const WmMenu *menu,
                           float pointer_x, float pointer_y) {
    if (!drag || !menu || wm_channel_drag_active(drag) || index < 0 ||
        index >= WM_SLOT_COUNT || !isfinite(pointer_x) || !isfinite(pointer_y) ||
        !menu->slots[index].occupied || strcmp(menu->slots[index].id, "disc") == 0)
        return false;
    drag->state = (WmChannelDragState){.phase = WM_CHANNEL_DRAG_GRAB,
                                       .source = index,
                                       .target = index,
                                       .pointer_x = pointer_x,
                                       .pointer_y = pointer_y};
    drag->revision++;
    return true;
}

void wm_channel_drag_point(WmChannelDrag *drag, float pointer_x, float pointer_y,
                           int target, int edge) {
    if (!drag || !isfinite(pointer_x) || !isfinite(pointer_y) ||
        (drag->state.phase != WM_CHANNEL_DRAG_GRAB &&
         drag->state.phase != WM_CHANNEL_DRAG_MOVING))
        return;
    WmChannelDragState *state = &drag->state;
    state->pointer_x = pointer_x;
    state->pointer_y = pointer_y;
    state->target = target;
    edge = edge < 0 ? -1 : edge > 0 ? 1 : 0;
    if (state->edge != edge)
        state->edge_frames = 0.0f;
    state->edge = edge;
    drag->revision++;
}

WmChannelDragSound wm_channel_drag_release(WmChannelDrag *drag, const WmMenu *menu,
                                           bool scrolling) {
    if (!drag || (drag->state.phase != WM_CHANNEL_DRAG_GRAB &&
                  drag->state.phase != WM_CHANNEL_DRAG_MOVING)) {
        return WM_CHANNEL_DRAG_SOUND_NONE;
    }
    WmChannelDragState *state = &drag->state;
    if (state->phase == WM_CHANNEL_DRAG_GRAB || scrolling) {
        state->pending_release = true;
        return WM_CHANNEL_DRAG_SOUND_NONE;
    }
    if (!menu)
        return WM_CHANNEL_DRAG_SOUND_NONE;
    bool valid =
        state->target > 0 && state->target < WM_SLOT_COUNT &&
        (state->target == state->source || !menu->slots[state->target].occupied);
    state->phase = valid ? WM_CHANNEL_DRAG_DROP_IN : WM_CHANNEL_DRAG_CANCEL;
    state->frame = 0.0f;
    state->release_frame = 0.0f;
    state->release_started = true;
    state->pending_release = false;
    state->valid_drop = valid;
    drag->revision++;
    return valid ? WM_CHANNEL_DRAG_SOUND_DROP : WM_CHANNEL_DRAG_SOUND_INVALID_DROP;
}

void wm_channel_drag_cancel(WmChannelDrag *drag) {
    if (!wm_channel_drag_active(drag))
        return;
    drag->state.phase = WM_CHANNEL_DRAG_CANCEL;
    drag->state.frame = 0.0f;
    drag->state.release_frame = 0.0f;
    drag->state.release_started = true;
    drag->state.pending_release = false;
    drag->state.valid_drop = false;
    drag->revision++;
}

WmChannelDragEvents wm_channel_drag_advance(WmChannelDrag *drag, float frames,
                                            const WmMenu *menu, bool scrolling) {
    WmChannelDragEvents events = {0};
    if (!wm_channel_drag_active(drag) || !isfinite(frames) || frames < 0.0f) {
        return events;
    }
    WmChannelDragState *state = &drag->state;
    state->frame += frames;
    state->appearance_frame += frames;
    if (state->release_started)
        state->release_frame += frames;
    if (state->phase == WM_CHANNEL_DRAG_GRAB) {
        float grab_frames =
            fmaxf(drag->lengths.mask_appear, drag->lengths.shade_appear);
        if (state->frame >= grab_frames) {
            state->phase = WM_CHANNEL_DRAG_MOVING;
            state->frame = 0.0f;
        }
    }
    if (state->phase == WM_CHANNEL_DRAG_MOVING && state->pending_release &&
        !scrolling) {
        events.sound = wm_channel_drag_release(drag, menu, false);
    }
    if (state->phase == WM_CHANNEL_DRAG_MOVING && state->edge && !scrolling) {
        state->edge_frames += frames;
        if (state->edge_frames >= 15.0f) {
            events.page = state->edge;
            state->edge_frames = 0.0f;
        }
    }
    if (state->phase == WM_CHANNEL_DRAG_DROP_IN &&
        state->frame >= drag->lengths.drop_appear) {
        events.move = true;
        events.move_source = state->source;
        events.move_target = state->target;
        state->phase = WM_CHANNEL_DRAG_DROP_OUT;
        state->frame = 0.0f;
    }
    if ((state->phase == WM_CHANNEL_DRAG_DROP_OUT &&
         state->frame >= drag->lengths.drop_lost) ||
        (state->phase == WM_CHANNEL_DRAG_CANCEL &&
         state->frame >= 21.0f + drag->lengths.mask_lost)) {
        drag->state = empty_state();
    }
    drag->revision++;
    return events;
}

WmChannelDragPose wm_channel_drag_pose(const WmChannelDrag *drag,
                                       WmChannelDragLayer layer) {
    WmChannelDragPose pose = {0};
    if (!wm_channel_drag_active(drag))
        return pose;
    const WmChannelDragState *state = &drag->state;
    bool leaving = state->phase == WM_CHANNEL_DRAG_DROP_IN ||
                   state->phase == WM_CHANNEL_DRAG_DROP_OUT ||
                   state->phase == WM_CHANNEL_DRAG_CANCEL;
    switch (layer) {
        case WM_CHANNEL_DRAG_MASK:
            pose.visible = true;
            pose.animation = leaving ? "my_TVMask_a_Lost" : "my_TVMask_a_Apear";
            pose.frame = state->phase == WM_CHANNEL_DRAG_CANCEL
                             ? fmaxf(0.0f, state->frame - 21.0f)
                         : state->phase == WM_CHANNEL_DRAG_DROP_OUT
                             ? drag->lengths.mask_lost
                         : leaving ? state->frame
                                   : state->appearance_frame;
            pose.frame = fminf(pose.frame, leaving ? drag->lengths.mask_lost
                                                   : drag->lengths.mask_appear);
            break;
        case WM_CHANNEL_DRAG_SHADE:
            pose.visible = true;
            pose.animation = leaving ? "my_TVShade_a_Lost" : "my_TVShade_a_Apear";
            pose.frame = leaving ? state->release_frame : state->appearance_frame;
            pose.frame = fminf(pose.frame, leaving ? drag->lengths.shade_lost
                                                   : drag->lengths.shade_appear);
            break;
        case WM_CHANNEL_DRAG_DROP:
            pose.visible = state->phase == WM_CHANNEL_DRAG_DROP_IN ||
                           state->phase == WM_CHANNEL_DRAG_DROP_OUT;
            pose.animation = state->phase == WM_CHANNEL_DRAG_DROP_OUT
                                 ? "my_TVApear_a_Lost"
                                 : "my_TVApear_a_Apear";
            pose.frame = fminf(state->frame, state->phase == WM_CHANNEL_DRAG_DROP_OUT
                                                 ? drag->lengths.drop_lost
                                                 : drag->lengths.drop_appear);
            break;
        default:
            break;
    }
    return pose;
}

WmChannelDragAudioParameters
wm_channel_drag_audio_parameters(float pointer_x, float pointer_y, bool has_previous,
                                 float previous_x, float previous_y, float frames) {
    WmChannelDragAudioParameters parameters = {.pitch = 1.0f};
    if (!isfinite(pointer_x) || !isfinite(pointer_y))
        return parameters;
    float speed = has_previous && frames > 0.0f && isfinite(frames) &&
                          isfinite(previous_x) && isfinite(previous_y)
                      ? hypotf(pointer_x - previous_x, pointer_y - previous_y) / frames
                      : 0.0f;
    parameters.gain = fminf(1.0f, 2.0f * speed / 304.0f);
    parameters.pan = fmaxf(-1.0f, fminf(1.0f, pointer_x / 304.0f));
    if (speed > 30.0f) {
        parameters.pitch = speed / 30.0f;
        parameters.changes_pitch = true;
    }
    return parameters;
}

WmChannelDragAudioParameters
wm_channel_drag_audio_for_framebuffer(bool wide, float pointer_x, float pointer_y,
                                      bool has_previous, float previous_x,
                                      float previous_y, float frames) {
    float source_width = wide ? 832.0f : 608.0f;
    float source_x = (pointer_x / WM_FRAME_WIDTH - 0.5f) * source_width;
    float source_y = WM_FRAME_HEIGHT * 0.5f - pointer_y;
    float previous_source_x = (previous_x / WM_FRAME_WIDTH - 0.5f) * source_width;
    float previous_source_y = WM_FRAME_HEIGHT * 0.5f - previous_y;
    return wm_channel_drag_audio_parameters(
        source_x, source_y, has_previous, previous_source_x, previous_source_y, frames);
}
