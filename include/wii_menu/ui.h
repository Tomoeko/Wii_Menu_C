#ifndef WII_MENU_UI_H
#define WII_MENU_UI_H

#include "wii_menu/menu.h"
#include "wii_menu/platform.h"
#include "wii_menu/pointer.h"

typedef enum WmHitType {
    WM_HIT_NONE,
    WM_HIT_CHANNEL,
    WM_HIT_PAGE_PREVIOUS,
    WM_HIT_PAGE_NEXT,
    WM_HIT_PREVIEW_PREVIOUS,
    WM_HIT_PREVIEW_NEXT,
    WM_HIT_SETTINGS,
    WM_HIT_BOARD,
    WM_HIT_SD,
    WM_HIT_BACK,
    WM_HIT_HOME,
    WM_HIT_HOME_CLOSE,
    WM_HIT_HOME_MENU,
    WM_HIT_START,
    WM_HIT_NOTICE_DISMISS
} WmHitType;

typedef struct WmHit {
    WmHitType type;
    int slot;
} WmHit;

WmHit wm_ui_hit(const WmMenu *menu, int x, int y);
void wm_ui_draw(WmPlatform *platform, const WmMenu *menu, WmHit hover,
                const WmPointer *pointer);
WmHit wm_ui_notice_hit(const WmMenu *menu, int x, int y);
void wm_ui_draw_notice(WmPlatform *platform, const WmMenu *menu);

#endif
