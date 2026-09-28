#ifndef WII_MENU_GLES2_RETAINED_FRAME_H
#define WII_MENU_GLES2_RETAINED_FRAME_H

#include "frame_damage.h"

#include <GLES2/gl2.h>

typedef struct WmGles2RetainedFrame {
    WmFrameDamage *commands;
    int width;
    int height;
    WmColor clear;
    WmClipRect clip;
    WmViewport region;
    bool allowed;
    bool active;
    bool recording;
    bool region_active;
} WmGles2RetainedFrame;

typedef void (*WmGles2Flush)(WmPlatform *platform);

void wm_gles2_retained_initialize(WmGles2RetainedFrame *frame);
void wm_gles2_retained_destroy(WmGles2RetainedFrame *frame);
bool wm_gles2_retained_begin(WmGles2RetainedFrame *frame, int width, int height,
                            WmColor clear);
void wm_gles2_retained_render(WmGles2RetainedFrame *frame, WmPlatform *platform,
                             WmGles2Flush flush);
/* Materialize deferred commands before destroying a referenced texture or
 * falling back to direct drawing when the fixed command storage fills. */
void wm_gles2_retained_materialize(WmGles2RetainedFrame *frame,
                                  WmPlatform *platform, WmGles2Flush flush);

#endif
