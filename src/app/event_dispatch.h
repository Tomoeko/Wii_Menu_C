#ifndef WM_APP_EVENT_DISPATCH_H
#define WM_APP_EVENT_DISPATCH_H

#include "app_runtime.h"

/* Drains the native queue in the source priority order. A quit event marks
 * running false, but queued events still drain before the frame exits. */
void wm_app_poll_events(WmAppRuntime *app, uint64_t frame_start, bool health_frame,
                        bool *running);

#endif
