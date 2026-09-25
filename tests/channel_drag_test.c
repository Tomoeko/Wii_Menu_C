#include "wii_menu/channel_drag.h"

#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Controller tests do not submit graphics. These satisfy link units shared
 * with the resource-backed presenter and catch an unexpected draw. */
void wm_platform_draw_vertices(WmPlatform *platform,
                               const WmDrawVertex vertices[4],
                               uint32_t texture) {
    (void)platform;
    (void)vertices;
    (void)texture;
    assert(false);
}

void wm_platform_draw_material_quad(WmPlatform *platform,
                                    const WmMaterialQuad *quad) {
    (void)platform;
    (void)quad;
    assert(false);
}

void wm_platform_prepare_material(WmPlatform *platform,
                                  const WmMaterialQuad *quad) {
    (void)platform;
    (void)quad;
}

uint32_t wm_platform_create_texture(WmPlatform *platform, int width,
                                    int height, const uint8_t *rgba) {
    (void)platform;
    (void)width;
    (void)height;
    (void)rgba;
    assert(false);
    return 0;
}

void wm_platform_destroy_texture(WmPlatform *platform, uint32_t texture) {
    (void)platform;
    (void)texture;
    assert(false);
}

/* Last playable frames from the USA 4.3 chanSel WAD export. The controller
 * reads these from animation metadata in production. */
static const WmChannelDragLengths source_lengths = {
    .mask_appear = 10,
    .mask_lost = 10,
    .shade_appear = 1,
    .shade_lost = 6,
    .drop_appear = 15,
    .drop_lost = 10
};

static bool near(float left, float right) {
    return fabsf(left - right) < 0.0001f;
}

static WmChannelDrag *controller(void) {
    WmChannelDrag *drag = wm_channel_drag_create_controller(&source_lengths);
    assert(drag);
    return drag;
}

static WmMenu menu_fixture(void) {
    WmMenu menu = {0};
    menu.slots[0].occupied = true;
    strcpy(menu.slots[0].id, "disc");
    menu.slots[1].occupied = true;
    strcpy(menu.slots[1].id, "mii");
    menu.slots[2].occupied = true;
    strcpy(menu.slots[2].id, "photo");
    return menu;
}

static WmChannelDragPose pose(WmChannelDrag *drag, WmChannelDragLayer layer,
                              const char *animation, float frame) {
    WmChannelDragPose result = wm_channel_drag_pose(drag, layer);
    assert(result.visible);
    assert(strcmp(result.animation, animation) == 0);
    assert(near(result.frame, frame));
    return result;
}

static void test_authored_grab_and_drop(void) {
    WmMenu menu = menu_fixture();
    WmChannelDrag *drag = controller();
    assert(!wm_channel_drag_start(drag, 0, &menu, 0, 0));
    assert(wm_channel_drag_start(drag, 1, &menu, 100, 100));
    assert(!wm_channel_drag_start(drag, 2, &menu, 100, 100));
    wm_channel_drag_point(drag, 150, 120, 3, 0);
    assert(wm_channel_drag_release(drag, &menu, false) ==
           WM_CHANNEL_DRAG_SOUND_NONE);
    assert(wm_channel_drag_state(drag).pending_release);
    WmChannelDragEvents events = wm_channel_drag_advance(drag, 9.5f,
                                                           &menu, false);
    assert(events.sound == WM_CHANNEL_DRAG_SOUND_NONE);
    assert(wm_channel_drag_state(drag).phase == WM_CHANNEL_DRAG_GRAB);
    pose(drag, WM_CHANNEL_DRAG_MASK, "my_TVMask_a_Apear", 9.5f);
    pose(drag, WM_CHANNEL_DRAG_SHADE, "my_TVShade_a_Apear", 1);
    events = wm_channel_drag_advance(drag, 0.5f, &menu, false);
    assert(events.sound == WM_CHANNEL_DRAG_SOUND_DROP);
    WmChannelDragState state = wm_channel_drag_state(drag);
    assert(state.phase == WM_CHANNEL_DRAG_DROP_IN);
    assert(near(state.appearance_frame, 10));
    assert(near(state.release_frame, 0));
    pose(drag, WM_CHANNEL_DRAG_MASK, "my_TVMask_a_Lost", 0);
    pose(drag, WM_CHANNEL_DRAG_DROP, "my_TVApear_a_Apear", 0);
    events = wm_channel_drag_advance(drag, 14.75f, &menu, false);
    assert(!events.move);
    WmChannelDragPose shade_before = pose(
        drag, WM_CHANNEL_DRAG_SHADE, "my_TVShade_a_Lost", 6);
    events = wm_channel_drag_advance(drag, 0.25f, &menu, false);
    assert(events.move && events.move_source == 1 && events.move_target == 3);
    assert(wm_channel_drag_state(drag).phase == WM_CHANNEL_DRAG_DROP_OUT);
    pose(drag, WM_CHANNEL_DRAG_DROP, "my_TVApear_a_Lost", 0);
    WmChannelDragPose shade_after = pose(
        drag, WM_CHANNEL_DRAG_SHADE, "my_TVShade_a_Lost", 6);
    assert(near(shade_before.frame, shade_after.frame));
    pose(drag, WM_CHANNEL_DRAG_MASK, "my_TVMask_a_Lost", 10);
    wm_channel_drag_advance(drag, 9.999f, &menu, false);
    assert(wm_channel_drag_active(drag));
    wm_channel_drag_advance(drag, 0.001f, &menu, false);
    assert(!wm_channel_drag_active(drag));
    wm_channel_drag_destroy(drag);
}

static void test_edge_paging_and_invalid_target(void) {
    WmMenu menu = menu_fixture();
    WmChannelDrag *drag = controller();
    assert(wm_channel_drag_start(drag, 1, &menu, 80, 100));
    wm_channel_drag_advance(drag, 10, &menu, false);
    assert(wm_channel_drag_state(drag).phase == WM_CHANNEL_DRAG_MOVING);
    wm_channel_drag_point(drag, 700, 100, -1, 1);
    assert(wm_channel_drag_advance(drag, 14, &menu, false).page == 0);
    assert(wm_channel_drag_advance(drag, 100, &menu, true).page == 0);
    assert(wm_channel_drag_advance(drag, 1, &menu, false).page == 1);
    assert(near(wm_channel_drag_state(drag).edge_frames, 0));
    wm_channel_drag_point(drag, 20, 100, 2, -1);
    assert(near(wm_channel_drag_state(drag).edge_frames, 0));
    assert(wm_channel_drag_release(drag, &menu, false) ==
           WM_CHANNEL_DRAG_SOUND_INVALID_DROP);
    assert(wm_channel_drag_state(drag).phase == WM_CHANNEL_DRAG_CANCEL);
    assert(!wm_channel_drag_advance(drag, 30, &menu, false).move);
    assert(wm_channel_drag_active(drag));
    pose(drag, WM_CHANNEL_DRAG_MASK, "my_TVMask_a_Lost", 9);
    wm_channel_drag_advance(drag, 1, &menu, false);
    assert(!wm_channel_drag_active(drag));
    wm_channel_drag_destroy(drag);
}

static void test_scrolling_defers_release_and_appearance_holds(void) {
    WmMenu menu = menu_fixture();
    WmChannelDrag *drag = controller();
    assert(wm_channel_drag_start(drag, 1, &menu, 100, 100));
    wm_channel_drag_advance(drag, 10, &menu, false);
    wm_channel_drag_point(drag, 160, 80, 15, 1);
    assert(wm_channel_drag_release(drag, &menu, true) ==
           WM_CHANNEL_DRAG_SOUND_NONE);
    assert(wm_channel_drag_advance(drag, 20, &menu, true).sound ==
           WM_CHANNEL_DRAG_SOUND_NONE);
    assert(wm_channel_drag_state(drag).phase == WM_CHANNEL_DRAG_MOVING);
    pose(drag, WM_CHANNEL_DRAG_MASK, "my_TVMask_a_Apear", 10);
    pose(drag, WM_CHANNEL_DRAG_SHADE, "my_TVShade_a_Apear", 1);
    WmChannelDragEvents events = wm_channel_drag_advance(drag, 1, &menu, false);
    assert(events.sound == WM_CHANNEL_DRAG_SOUND_DROP);
    assert(events.page == 0); /* Release takes precedence over edge paging. */
    events = wm_channel_drag_advance(drag, 15, &menu, false);
    assert(events.move && events.move_source == 1 && events.move_target == 15);
    wm_channel_drag_destroy(drag);
}

static void test_cancel_and_same_slot(void) {
    WmMenu menu = menu_fixture();
    WmChannelDrag *drag = controller();
    assert(wm_channel_drag_start(drag, 1, &menu, 100, 100));
    wm_channel_drag_cancel(drag);
    pose(drag, WM_CHANNEL_DRAG_MASK, "my_TVMask_a_Lost", 0);
    wm_channel_drag_advance(drag, 21, &menu, false);
    pose(drag, WM_CHANNEL_DRAG_MASK, "my_TVMask_a_Lost", 0);
    wm_channel_drag_advance(drag, 9, &menu, false);
    pose(drag, WM_CHANNEL_DRAG_MASK, "my_TVMask_a_Lost", 9);
    wm_channel_drag_advance(drag, 1, &menu, false);
    assert(!wm_channel_drag_active(drag));
    assert(wm_channel_drag_start(drag, 1, &menu, 100, 100));
    wm_channel_drag_advance(drag, 10, &menu, false);
    assert(wm_channel_drag_release(drag, &menu, false) ==
           WM_CHANNEL_DRAG_SOUND_DROP);
    WmChannelDragEvents events = wm_channel_drag_advance(drag, 15,
                                                           &menu, false);
    assert(events.move && events.move_source == 1 && events.move_target == 1);
    wm_channel_drag_destroy(drag);
}

static void test_drag_audio_parameters(void) {
    WmChannelDragAudioParameters audio = wm_channel_drag_audio_parameters(
        152, 0, false, 0, 0, 1);
    assert(near(audio.gain, 0));
    assert(near(audio.pan, 0.5f));
    assert(!audio.changes_pitch);
    audio = wm_channel_drag_audio_parameters(304, 0, true, 284, 0, 1);
    assert(near(audio.gain, 40.0f / 304.0f));
    assert(near(audio.pan, 1));
    assert(!audio.changes_pitch);
    audio = wm_channel_drag_audio_parameters(-500, 0, true, -200, 0, 2);
    assert(near(audio.gain, 300.0f / 304.0f));
    assert(near(audio.pan, -1));
    assert(audio.changes_pitch && near(audio.pitch, 5));
    audio = wm_channel_drag_audio_for_framebuffer(true, 352, 228,
                                                   true, 320, 228, 1);
    assert(near(audio.pan, 41.6f / 304.0f));
    assert(near(audio.gain, 83.2f / 304.0f));
}

static void test_optional_source_resources(void) {
    const char *assets_directory = getenv("WM_CHANNEL_DRAG_ASSETS");
    if (!assets_directory || !assets_directory[0]) return;
    WmPlatform *unused_platform = (WmPlatform *)(uintptr_t)1;
    WmTextureCache *unused_textures = (WmTextureCache *)(uintptr_t)1;
    WmChannelDrag *drag = wm_channel_drag_create(
        unused_platform, assets_directory, unused_textures, NULL, true);
    assert(drag);
    WmMenu menu = menu_fixture();
    assert(wm_channel_drag_start(drag, 1, &menu, 100, 100));
    assert(wm_channel_drag_advance(drag, 9.999f, &menu, false).page == 0);
    assert(wm_channel_drag_state(drag).phase == WM_CHANNEL_DRAG_GRAB);
    wm_channel_drag_advance(drag, 0.001f, &menu, false);
    assert(wm_channel_drag_state(drag).phase == WM_CHANNEL_DRAG_MOVING);
    wm_channel_drag_destroy(drag);
}

int main(void) {
    test_authored_grab_and_drop();
    test_edge_paging_and_invalid_target();
    test_scrolling_defers_release_and_appearance_holds();
    test_cancel_and_same_slot();
    test_drag_audio_parameters();
    test_optional_source_resources();
    puts("channel drag tests passed");
    return 0;
}
