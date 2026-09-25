#define _POSIX_C_SOURCE 200809L

#include "wii_menu/board_erase.h"

#include "wii_menu/layout_present.h"
#include "wii_menu/layout_runtime.h"
#include "wii_menu/material_prepare.h"
#include "wii_menu/source_hit.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

enum { ERASE_PATH_CAPACITY = 4096 };

typedef struct EraseFocus {
    bool active;
    bool entering;
    float frame;
} EraseFocus;

struct WmBoardErase {
    WmPlatform *platform;
    WmTextureCache *textures;
    WmFontCache *fonts;
    WmLayout *layout;
    WmBoardErasePhase phase;
    float frame;
    WmBoardEraseControl selected;
    WmBoardEraseControl hover;
    EraseFocus focus[WM_ERASE_CONTROL_OK + 1];
    WmBoardEraseOutcome outcome;
};

static float clamp_frame(float value, float maximum) {
    return fminf(fmaxf(value, 0.0f), maximum);
}

WmBoardErase *wm_board_erase_create(WmPlatform *platform,
                                     const char *assets_directory,
                                     WmTextureCache *textures,
                                     WmFontCache *fonts) {
    if (!platform || !assets_directory || !assets_directory[0] ||
        !textures || !fonts) return NULL;
    WmBoardErase *erase = calloc(1, sizeof(*erase));
    if (!erase) return NULL;
    erase->platform = platform;
    erase->textures = textures;
    erase->fonts = fonts;
    char path[ERASE_PATH_CAPACITY];
    int length = snprintf(path, sizeof(path), "%s/%s", assets_directory,
                          "layouts/dlgWdw/my_DialogWindow_b.json");
    if (length < 0 || length >= (int)sizeof(path)) {
        wm_board_erase_destroy(erase);
        return NULL;
    }
    char error[160] = {0};
    erase->layout = wm_layout_load_json(path, error, sizeof(error));
    if (!erase->layout) {
        fprintf(stderr, "Could not load Memo erase layout: %s\n", error);
        wm_board_erase_destroy(erase);
        return NULL;
    }
    wm_layout_prepare_materials(platform, erase->layout);
    return erase;
}

void wm_board_erase_destroy(WmBoardErase *erase) {
    if (!erase) return;
    wm_layout_destroy(erase->layout);
    free(erase);
}

void wm_board_erase_reset(WmBoardErase *erase) {
    if (!erase) return;
    erase->phase = WM_ERASE_CLOSED;
    erase->frame = 0.0f;
    erase->selected = WM_ERASE_CONTROL_NONE;
    erase->hover = WM_ERASE_CONTROL_NONE;
    erase->outcome = WM_ERASE_OUTCOME_NONE;
    memset(erase->focus, 0, sizeof(erase->focus));
}

bool wm_board_erase_open(WmBoardErase *erase) {
    if (!erase || erase->phase != WM_ERASE_CLOSED) return false;
    erase->phase = WM_ERASE_ENTER;
    erase->frame = 0.0f;
    erase->selected = WM_ERASE_CONTROL_NONE;
    erase->hover = WM_ERASE_CONTROL_NONE;
    erase->outcome = WM_ERASE_OUTCOME_NONE;
    memset(erase->focus, 0, sizeof(erase->focus));
    return true;
}

WmBoardErasePhase wm_board_erase_phase(const WmBoardErase *erase) {
    return erase ? erase->phase : WM_ERASE_CLOSED;
}

void wm_board_erase_advance(WmBoardErase *erase, float frames) {
    if (!erase || erase->phase == WM_ERASE_CLOSED ||
        !isfinite(frames) || frames <= 0.0f) return;
    for (size_t index = 0; index <
         sizeof(erase->focus) / sizeof(erase->focus[0]); index++) {
        if (erase->focus[index].active) erase->focus[index].frame += frames;
    }
    float remaining = frames;
    while (remaining > 0.0f && erase->phase != WM_ERASE_IDLE &&
           erase->phase != WM_ERASE_CLOSED) {
        float duration = erase->phase == WM_ERASE_SELECT ? 21.0f : 26.0f;
        float amount = fminf(remaining, duration - erase->frame);
        erase->frame += amount;
        remaining -= amount;
        if (erase->frame < duration) break;
        if (erase->phase == WM_ERASE_ENTER) {
            erase->phase = WM_ERASE_IDLE;
        } else if (erase->phase == WM_ERASE_SELECT) {
            erase->phase = WM_ERASE_EXIT;
        } else {
            erase->phase = WM_ERASE_CLOSED;
            erase->outcome = erase->selected == WM_ERASE_CONTROL_OK
                                 ? WM_ERASE_OUTCOME_ACCEPT
                                 : WM_ERASE_OUTCOME_CANCEL;
        }
        erase->frame = 0.0f;
    }
}

WmBoardEraseOutcome wm_board_erase_take_outcome(WmBoardErase *erase) {
    if (!erase) return WM_ERASE_OUTCOME_NONE;
    WmBoardEraseOutcome outcome = erase->outcome;
    erase->outcome = WM_ERASE_OUTCOME_NONE;
    return outcome;
}

bool wm_board_erase_activate(WmBoardErase *erase,
                              WmBoardEraseControl control) {
    if (!erase || erase->phase != WM_ERASE_IDLE ||
        (control != WM_ERASE_CONTROL_QUIT &&
         control != WM_ERASE_CONTROL_OK)) return false;
    erase->selected = control;
    erase->phase = WM_ERASE_SELECT;
    erase->frame = 0.0f;
    return true;
}

bool wm_board_erase_back(WmBoardErase *erase) {
    return wm_board_erase_activate(erase, WM_ERASE_CONTROL_QUIT);
}

static void pose(WmBoardErase *erase) {
    WmLayoutClip clips[5];
    size_t count = 0;
    clips[count++] = (WmLayoutClip){
        .animation = "my_DialogWindow_b_DialogIn",
        .group = "G_InOut",
        .frame = erase->phase == WM_ERASE_ENTER
                     ? clamp_frame(erase->frame, 25.0f) : 25.0f,
        .loop_override = 0
    };
    for (int button = WM_ERASE_CONTROL_QUIT;
         button <= WM_ERASE_CONTROL_OK; button++) {
        EraseFocus focus = erase->focus[button];
        if (!focus.active) continue;
        clips[count++] = (WmLayoutClip){
            .animation = focus.entering
                             ? "my_DialogWindow_b_FocusBtn_on"
                             : "my_DialogWindow_b_FocusBtn_off",
            .group = button == WM_ERASE_CONTROL_OK
                         ? "G_FocusBtnB" : "G_FocusBtnA",
            .frame = clamp_frame(focus.frame, 10.0f),
            .loop_override = 0
        };
    }
    if (erase->selected != WM_ERASE_CONTROL_NONE) {
        clips[count++] = (WmLayoutClip){
            .animation = "my_DialogWindow_b_SelectBtn_Ac",
            .group = erase->selected == WM_ERASE_CONTROL_OK
                         ? "G_SelectBtnB" : "G_SelectBtnA",
            .frame = erase->phase == WM_ERASE_SELECT
                         ? clamp_frame(erase->frame, 20.0f) : 20.0f,
            .loop_override = 0
        };
    }
    if (erase->phase == WM_ERASE_EXIT) {
        clips[count++] = (WmLayoutClip){
            .animation = "my_DialogWindow_b_DialogOut",
            .group = "G_InOut",
            .frame = clamp_frame(erase->frame, 25.0f),
            .loop_override = 0
        };
    }
    wm_layout_pose(erase->layout, clips, count);
    wm_layout_set_pose_text(erase->layout, "T_Dialog", "Erase this message?");
    wm_layout_set_pose_text(erase->layout, "T_BtnA", "Quit");
    wm_layout_set_pose_text(erase->layout, "T_BtnB", "OK");
}

void wm_board_erase_draw(WmBoardErase *erase) {
    if (!erase || erase->phase == WM_ERASE_CLOSED) return;
    pose(erase);
    wm_layout_present_with_fonts(erase->platform, erase->textures,
                                 erase->fonts, erase->layout, true,
                                 WM_LAYOUT_IPL, NULL);
}

WmBoardEraseControl wm_board_erase_hit(WmBoardErase *erase, int x, int y) {
    if (!erase || erase->phase != WM_ERASE_IDLE) return WM_ERASE_CONTROL_NONE;
    pose(erase);
    WmSourceRect rect;
    if (wm_source_pane_rect(erase->layout, "B_BtnA", true,
                            WM_LAYOUT_IPL, NULL, &rect) &&
        (float)x >= rect.x && (float)x < rect.x + rect.width &&
        (float)y >= rect.y && (float)y < rect.y + rect.height) {
        return WM_ERASE_CONTROL_QUIT;
    }
    if (wm_source_pane_rect(erase->layout, "B_BtnB", true,
                            WM_LAYOUT_IPL, NULL, &rect) &&
        (float)x >= rect.x && (float)x < rect.x + rect.width &&
        (float)y >= rect.y && (float)y < rect.y + rect.height) {
        return WM_ERASE_CONTROL_OK;
    }
    return WM_ERASE_CONTROL_NONE;
}

void wm_board_erase_hover(WmBoardErase *erase,
                           WmBoardEraseControl control) {
    if (!erase || erase->phase != WM_ERASE_IDLE ||
        erase->hover == control) return;
    if (erase->hover != WM_ERASE_CONTROL_NONE) {
        erase->focus[erase->hover] = (EraseFocus){true, false, 0};
    }
    erase->hover = control;
    if (control != WM_ERASE_CONTROL_NONE) {
        erase->focus[control] = (EraseFocus){true, true, 0};
    }
}
