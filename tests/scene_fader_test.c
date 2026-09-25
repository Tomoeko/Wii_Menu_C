#include "wii_menu/scene_fader.h"

#include <assert.h>
#include <math.h>
#include <stdio.h>

static bool near(float first, float second) {
    return fabsf(first - second) < 0.00001f;
}

int main(void) {
    WmSceneFader fader = {0};
    assert(near(wm_native_fade_alpha(0, false), 0));
    assert(near(wm_native_fade_alpha(1, false), 0));
    assert(near(wm_native_fade_alpha(2, false), 12.0f / 255.0f));
    assert(near(wm_native_fade_alpha(22, false), 1));
    assert(near(wm_native_fade_alpha(0, true), 1));
    assert(near(wm_native_fade_alpha(22, true), 0));
    assert(near(wm_menu_entrance_alpha(0), 1));
    assert(near(wm_menu_entrance_alpha(22), 1));
    assert(near(wm_menu_entrance_alpha(23), 1));
    assert(near(wm_menu_entrance_alpha(25), 243.0f / 255.0f));
    assert(near(wm_menu_entrance_alpha(44), 0));
    assert(!wm_menu_entrance_complete(44));
    assert(wm_menu_entrance_complete(45));
    assert(near(wm_menu_entrance_alpha(45), 0));
    assert(wm_scene_fader_start(&fader));
    assert(!wm_scene_fader_start(&fader));
    assert(!wm_scene_fader_advance(&fader, 22.0f));
    assert(fader.phase == WM_SCENE_FADER_OUT);
    assert(near(wm_scene_fader_alpha(&fader), 1));
    assert(wm_scene_fader_advance(&fader, 1.0f));
    assert(fader.phase == WM_SCENE_FADER_IN);
    assert(near(wm_scene_fader_alpha(&fader), 1));
    assert(!wm_scene_fader_advance(&fader, 21.0f));
    assert(wm_scene_fader_active(&fader));
    assert(!wm_scene_fader_advance(&fader, 1.0f));
    assert(!wm_scene_fader_active(&fader));
    assert(near(wm_scene_fader_alpha(&fader), 0));
    puts("Native scene fader timing passed.");
    return 0;
}
