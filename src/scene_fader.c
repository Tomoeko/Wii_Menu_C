#include "wii_menu/scene_fader.h"

#include <math.h>

float wm_native_fade_alpha(float updates, bool revealing) {
    if (!isfinite(updates) || updates < 0.0f) return 0.0f;
    float frame = fminf(20.0f, fmaxf(0.0f, floorf(updates) - 1.0f));
    float progress = floorf(frame * 255.0f / 20.0f);
    return (revealing ? 255.0f - progress : progress) / 255.0f;
}

float wm_menu_entrance_alpha(float updates_after_health) {
    if (!isfinite(updates_after_health) || updates_after_health < 0.0f)
        return 1.0f;
    if (updates_after_health < 23.0f) return 1.0f;
    if (wm_menu_entrance_complete(updates_after_health)) return 0.0f;
    return wm_native_fade_alpha(updates_after_health - 23.0f, true);
}

bool wm_menu_entrance_complete(float updates_after_health) {
    return isfinite(updates_after_health) &&
           updates_after_health >= 45.0f;
}

bool wm_scene_fader_start(WmSceneFader *fader) {
    if (!fader || fader->phase != WM_SCENE_FADER_IDLE) return false;
    fader->phase = WM_SCENE_FADER_OUT;
    fader->frame = 0.0f;
    return true;
}

bool wm_scene_fader_advance(WmSceneFader *fader, float frames) {
    if (!fader || !isfinite(frames) || frames < 0.0f ||
        fader->phase == WM_SCENE_FADER_IDLE) return false;
    if (fader->phase == WM_SCENE_FADER_OUT) {
        fader->frame += frames;
        if (fader->frame < 23.0f) return false;
        fader->phase = WM_SCENE_FADER_IN;
        fader->frame = 0.0f;
        return true;
    }
    fader->frame += frames;
    if (fader->frame >= 22.0f) {
        fader->phase = WM_SCENE_FADER_IDLE;
        fader->frame = 0.0f;
    }
    return false;
}

float wm_scene_fader_alpha(const WmSceneFader *fader) {
    if (!fader || fader->phase == WM_SCENE_FADER_IDLE) return 0.0f;
    return wm_native_fade_alpha(fader->frame,
                                fader->phase == WM_SCENE_FADER_IN);
}

bool wm_scene_fader_active(const WmSceneFader *fader) {
    return fader && fader->phase != WM_SCENE_FADER_IDLE;
}
