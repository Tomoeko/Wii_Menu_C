#ifndef WII_MENU_PSVR2_POINTER_H
#define WII_MENU_PSVR2_POINTER_H

#include "wii_menu/platform/platform.h"

typedef struct WmPsvr2Pointer WmPsvr2Pointer;

/* NULL selects /dev/fast_input, the dedicated Stage3 ttyGS1 bridge. Never use
 * ttyGS0: that port belongs to the control shell and upload tool. Missing input
 * is allowed at startup; a disconnected endpoint is retried once per second. */
WmPsvr2Pointer *wm_psvr2_pointer_create(const char *path);
void wm_psvr2_pointer_destroy(WmPsvr2Pointer *pointer);
bool wm_psvr2_pointer_poll(WmPsvr2Pointer *pointer, WmEvent *event);

#endif
