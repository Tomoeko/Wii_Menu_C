#include "menu_pointer.h"

#include "wii_menu/audio/hover_audio.h"
#include "wii_menu/render/ui.h"

static bool same_hit(WmHit first, WmHit second) {
    return first.type == second.type && first.slot == second.slot;
}

static WmHit hit_menu(const WmMenu *menu, const WmResourceScene *resource_scene,
                      const WmPreviewScene *preview_scene, int x, int y) {
    if (menu->notice[0])
        return wm_ui_notice_hit(menu, x, y);
    if (resource_scene && menu->screen == WM_SCREEN_GRID && !menu->home_open)
        return wm_resource_scene_hit(resource_scene, menu, x, y);
    if (preview_scene && !menu->home_open &&
        wm_preview_scene_available(preview_scene, menu))
        return wm_preview_scene_hit(preview_scene, menu, x, y);
    return wm_ui_hit(menu, x, y);
}

static WmHit hover_menu(const WmMenu *menu, const WmResourceScene *resource_scene,
                        const WmPreviewScene *preview_scene, int x, int y, WmHit held) {
    if (!menu->notice[0] && menu->screen == WM_SCREEN_PREVIEW && preview_scene &&
        !menu->home_open && wm_preview_scene_available(preview_scene, menu))
        return wm_preview_scene_hover_hit(preview_scene, menu, x, y, held);
    return hit_menu(menu, resource_scene, preview_scene, x, y);
}

static WmHit drag_hover(const WmResourceScene *scene, const WmMenu *menu, int x,
                        int y) {
    WmHit hit = wm_resource_scene_hit(scene, menu, x, y);
    return hit.type == WM_HIT_PAGE_PREVIOUS || hit.type == WM_HIT_PAGE_NEXT
               ? hit
               : (WmHit){WM_HIT_NONE, -1};
}

static void drag_point(WmChannelDrag *drag, const WmResourceScene *scene,
                       const WmMenu *menu, int x, int y) {
    WmHit arrow = drag_hover(scene, menu, x, y);
    int edge = 0;
    if (arrow.type == WM_HIT_PAGE_PREVIOUS)
        edge = -1;
    else if (arrow.type == WM_HIT_PAGE_NEXT)
        edge = 1;
    int slot = wm_resource_scene_slot_at(scene, menu, x, y);
    wm_channel_drag_point(drag, (float)x, (float)y, slot, edge);
}

static void play_drag_sound(WmAudio *audio, WmChannelDragSound sound) {
    if (sound == WM_CHANNEL_DRAG_SOUND_DROP)
        wm_audio_play(audio, "drop");
    if (sound == WM_CHANNEL_DRAG_SOUND_INVALID_DROP)
        wm_audio_play(audio, "invalidDrop");
}

static void play_hover_cue(WmAudio *audio, WmHit hit) {
    const char *cue = wm_hover_audio_menu_cue(hit.type);
    if (cue)
        wm_audio_play(audio, cue);
}

void wm_app_menu_pointer_move(WmAppMenuPointer *input, WmMenu *menu,
                              WmResourceScene *resource_scene,
                              WmPreviewScene *preview_scene, WmChannelDrag *drag,
                              WmAudio *audio, int x, int y) {
    if (input->drag_button != 0)
        drag_point(drag, resource_scene, menu, x, y);
    WmHit next =
        input->drag_button != 0
            ? drag_hover(resource_scene, menu, x, y)
            : hover_menu(menu, resource_scene, preview_scene, x, y, input->hovered);
    if (!same_hit(input->hovered, next))
        play_hover_cue(audio, next);
    input->hovered = next;
}

void wm_app_menu_pointer_down(WmAppMenuPointer *input, WmMenu *menu,
                              WmResourceScene *resource_scene,
                              WmPreviewScene *preview_scene, WmChannelDrag *drag,
                              WmAudio *audio, WmPointerButton button, int x, int y) {
    if ((button == WM_POINTER_MIDDLE || button == WM_POINTER_RIGHT) && drag &&
        resource_scene && menu->screen == WM_SCREEN_GRID &&
        menu->transition == WM_TRANSITION_NONE && !menu->home_open &&
        !menu->notice[0]) {
        int slot = wm_resource_scene_slot_at(resource_scene, menu, x, y);
        if (wm_channel_drag_start(drag, slot, menu, (float)x, (float)y)) {
            input->drag_button = button;
            input->drag_has_previous = false;
            input->drag_pitch = 1.0f;
            drag_point(drag, resource_scene, menu, x, y);
            wm_audio_play(audio, "grab");
            wm_audio_hold_loop(audio, "drag", 0.0f, 0.0f, 1.0f);
            input->hovered = (WmHit){WM_HIT_NONE, -1};
            input->pressed = input->hovered;
            return;
        }
    }
    WmHit exact = hit_menu(menu, resource_scene, preview_scene, x, y);
    WmHit next = hover_menu(menu, resource_scene, preview_scene, x, y, input->hovered);
    if (!same_hit(input->hovered, next))
        play_hover_cue(audio, next);
    input->hovered = next;
    input->pressed = button == WM_POINTER_LEFT ? exact : (WmHit){WM_HIT_NONE, -1};
}

WmAppMenuPointerRelease wm_app_menu_pointer_up(WmAppMenuPointer *input, WmMenu *menu,
                                               WmResourceScene *resource_scene,
                                               WmPreviewScene *preview_scene,
                                               WmChannelDrag *drag, WmPointer *pointer,
                                               WmAudio *audio, const WmEvent *event) {
    WmAppMenuPointerRelease result = {.activated = {WM_HIT_NONE, -1},
                                      .released_drag = false};
    if (input->drag_button != 0 && event->button == input->drag_button) {
        drag_point(drag, resource_scene, menu, event->x, event->y);
        play_drag_sound(audio, wm_channel_drag_release(
                                   drag, menu, menu->transition == WM_TRANSITION_PAGE));
        wm_audio_stop_loop(audio, "drag");
        input->drag_button = 0;
        input->drag_has_previous = false;
        if (event->outside_viewport) {
            wm_pointer_hide(pointer);
            input->hovered = (WmHit){WM_HIT_NONE, -1};
        } else {
            input->hovered =
                hit_menu(menu, resource_scene, preview_scene, event->x, event->y);
        }
        input->pressed = (WmHit){WM_HIT_NONE, -1};
        result.released_drag = true;
        return result;
    }
    WmHit exact = hit_menu(menu, resource_scene, preview_scene, event->x, event->y);
    input->hovered = hover_menu(menu, resource_scene, preview_scene, event->x, event->y,
                                input->hovered);
    if (event->button == WM_POINTER_LEFT && input->pressed.type != WM_HIT_NONE &&
        same_hit(input->pressed, exact)) {
        result.activated = exact;
    }
    input->pressed = (WmHit){WM_HIT_NONE, -1};
    return result;
}

void wm_app_menu_pointer_leave(WmAppMenuPointer *input, WmChannelDrag *drag,
                               WmAudio *audio) {
    if (input->drag_button != 0) {
        wm_channel_drag_cancel(drag);
        wm_audio_stop_loop(audio, "drag");
        input->drag_button = 0;
        input->drag_has_previous = false;
    }
    input->hovered = (WmHit){WM_HIT_NONE, -1};
    input->pressed = (WmHit){WM_HIT_NONE, -1};
}

void wm_app_menu_drag_advance(WmAppMenuPointer *input, WmMenu *menu,
                              WmResourceScene *resource_scene,
                              WmPreviewScene *preview_scene, WmChannelDrag *drag,
                              WmAudio *audio, float frames) {
    WmChannelDragEvents events = wm_channel_drag_advance(
        drag, frames, menu, menu->transition == WM_TRANSITION_PAGE);
    play_drag_sound(audio, events.sound);
    if (events.page && wm_menu_change_page(menu, events.page)) {
        wm_resource_scene_press_arrow(resource_scene, events.page);
        wm_audio_play(audio, "page");
    }
    if (events.move &&
        wm_menu_move_channel(menu, events.move_source, events.move_target)) {
        wm_resource_scene_move_channel(resource_scene, events.move_source,
                                       events.move_target);
        wm_preview_scene_move_channel(preview_scene, events.move_source,
                                      events.move_target);
    }
    if (input->drag_button != 0) {
        WmChannelDragState state = wm_channel_drag_state(drag);
        WmChannelDragAudioParameters parameters = wm_channel_drag_audio_for_framebuffer(
            true, state.pointer_x, state.pointer_y, input->drag_has_previous,
            input->drag_previous_x, input->drag_previous_y, frames);
        if (parameters.changes_pitch)
            input->drag_pitch = parameters.pitch;
        wm_audio_hold_loop(audio, "drag", parameters.gain, parameters.pan,
                           input->drag_pitch);
        input->drag_previous_x = state.pointer_x;
        input->drag_previous_y = state.pointer_y;
        input->drag_has_previous = true;
    }
}
