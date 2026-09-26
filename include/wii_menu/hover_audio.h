#ifndef WII_MENU_HOVER_AUDIO_H
#define WII_MENU_HOVER_AUDIO_H

#include "wii_menu/board_scene.h"
#include "wii_menu/sd_scene.h"
#include "wii_menu/storage_scene.h"
#include "wii_menu/ui.h"

/* The requested local presentation uses the Memo focus cue for navigation
 * arrows and common footer icons. This deliberately differs from the HTML
 * reference's targeting cue; keyboard characters retain their own cue. */
const char *wm_hover_audio_menu_cue(WmHitType hit);
const char *wm_hover_audio_board_cue(WmBoardControl control);
const char *wm_hover_audio_sd_cue(WmSdControl control);
const char *wm_hover_audio_storage_cue(WmStorageControl control);

#endif
