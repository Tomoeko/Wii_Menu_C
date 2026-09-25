#ifndef WII_MENU_MATERIAL_PREPARE_H
#define WII_MENU_MATERIAL_PREPARE_H

#include "wii_menu/layout_runtime.h"
#include "wii_menu/platform.h"

/* Compile/cache every material signature after loading a source layout and
 * before rendering its first frame. No textures or framebuffers are touched. */
void wm_layout_prepare_materials(WmPlatform *platform, const WmLayout *layout);

#endif
