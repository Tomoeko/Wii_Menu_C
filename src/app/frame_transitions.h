#ifndef WM_APP_FRAME_TRANSITIONS_H
#define WM_APP_FRAME_TRANSITIONS_H

#include "app_runtime.h"

/* The returned health flag describes the entire frame, including the frame
 * on which Health accepts and finishes its final animation. */
bool wm_app_advance_before_events(WmAppRuntime *app, uint64_t frame_start,
                                  float elapsed);
bool wm_app_try_enter_home(WmAppRuntime *app, uint64_t now);
void wm_app_update_preview_clock(WmAppRuntime *app, uint64_t frame_start);

#endif
