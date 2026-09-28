#ifndef WII_MENU_HOVER_AUDIO_H
#define WII_MENU_HOVER_AUDIO_H

#include "wii_menu/board/board_scene.h"
#include "wii_menu/scenes/sd_scene.h"
#include "wii_menu/scenes/storage_scene.h"
#include "wii_menu/render/ui.h"

/* Navigation arrows and common footer icons use the Memo focus cue;
 * keyboard characters retain their own cue. */
const char *wm_hover_audio_menu_cue(WmHitType hit);
const char *wm_hover_audio_board_cue(WmBoardControl control);
const char *wm_hover_audio_sd_cue(WmSdControl control);
const char *wm_hover_audio_storage_cue(WmStorageControl control);

#endif
