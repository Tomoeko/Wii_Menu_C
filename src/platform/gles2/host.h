#ifndef WM_GLES2_HOST_H
#define WM_GLES2_HOST_H

#include "wii_menu/platform/platform.h"

/* X11 and EGL stay behind this private interface. The host owns the native
 * window and current ES2 context; the renderer owns every GL object. */
typedef struct WmGles2Host WmGles2Host;

WmGles2Host *wm_gles2_host_create(const char *title, int width, int height);
void wm_gles2_host_show(WmGles2Host *host);
bool wm_gles2_host_make_current(WmGles2Host *host);
/* Optional EGL capability; failure leaves the full redraw path available. */
bool wm_gles2_host_preserve_back_buffer(WmGles2Host *host);
void wm_gles2_host_destroy(WmGles2Host *host);

bool wm_gles2_host_poll(WmGles2Host *host, WmEvent *event);
void wm_gles2_host_surface_size(WmGles2Host *host, int *width, int *height);
bool wm_gles2_host_present(WmGles2Host *host);

#endif
