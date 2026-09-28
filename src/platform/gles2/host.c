#include "host.h"

#include "wii_menu/render/viewport.h"

#include <EGL/egl.h>
#include <X11/Xlib.h>
#include <X11/Xutil.h>
#include <X11/keysym.h>

#include <stdio.h>
#include <stdlib.h>

struct WmGles2Host {
    Display *display;
    Window window;
    Colormap colormap;
    Cursor hidden_cursor;
    Atom delete_window;
    int window_width;
    int window_height;
    EGLDisplay egl_display;
    EGLSurface egl_surface;
    EGLContext egl_context;
    bool swap_failure_reported;
};

static void wm_report_egl_error(const char *operation) {
    fprintf(stderr, "GLES2: %s failed (EGL 0x%04x).\n", operation,
            (unsigned int)eglGetError());
}

static bool wm_choose_config(EGLDisplay display, EGLConfig *config) {
    EGLint rgba8888[] = {EGL_SURFACE_TYPE,
                                      EGL_WINDOW_BIT,
                                      EGL_RENDERABLE_TYPE,
                                      EGL_OPENGL_ES2_BIT,
                                      EGL_RED_SIZE,
                                      8,
                                      EGL_GREEN_SIZE,
                                      8,
                                      EGL_BLUE_SIZE,
                                      8,
                                      EGL_ALPHA_SIZE,
                                      8,
                                      EGL_NONE};
    EGLint rgb565[] = {EGL_SURFACE_TYPE,
                                    EGL_WINDOW_BIT,
                                    EGL_RENDERABLE_TYPE,
                                    EGL_OPENGL_ES2_BIT,
                                    EGL_RED_SIZE,
                                    5,
                                    EGL_GREEN_SIZE,
                                    6,
                                    EGL_BLUE_SIZE,
                                    5,
                                    EGL_ALPHA_SIZE,
                                    0,
                                    EGL_NONE};
    EGLint *formats[] = {rgba8888, rgb565};
    for (size_t index = 0; index < sizeof(formats) / sizeof(formats[0]); index++) {
        /* Prefer retained window pixels without requiring this EGL capability. */
        const EGLint surface_types[] = {
            EGL_WINDOW_BIT | EGL_SWAP_BEHAVIOR_PRESERVED_BIT, EGL_WINDOW_BIT
        };
        for (size_t choice = 0; choice < 2; choice++) {
            EGLint count = 0;
            formats[index][1] = surface_types[choice];
            if (eglChooseConfig(display, formats[index], config, 1, &count) && count > 0)
                return true;
        }
    }
    return false;
}

WmGles2Host *wm_gles2_host_create(const char *title, int width, int height) {
    WmGles2Host *host = calloc(1, sizeof(*host));
    if (host == NULL) {
        return NULL;
    }
    host->egl_display = EGL_NO_DISPLAY;
    host->egl_surface = EGL_NO_SURFACE;
    host->egl_context = EGL_NO_CONTEXT;
    host->window_width = width;
    host->window_height = height;

    host->display = XOpenDisplay(NULL);
    if (host->display == NULL) {
        fprintf(stderr, "GLES2: could not open the X11 display.\n");
        goto fail;
    }

    host->egl_display = eglGetDisplay((EGLNativeDisplayType)host->display);
    if (host->egl_display == EGL_NO_DISPLAY ||
        !eglInitialize(host->egl_display, NULL, NULL)) {
        wm_report_egl_error("display initialization");
        goto fail;
    }
    if (!eglBindAPI(EGL_OPENGL_ES_API)) {
        wm_report_egl_error("OpenGL ES selection");
        goto fail;
    }

    EGLConfig config;
    if (!wm_choose_config(host->egl_display, &config)) {
        wm_report_egl_error("ES2 window configuration selection");
        goto fail;
    }

    EGLint visual_id = 0;
    if (!eglGetConfigAttrib(host->egl_display, config, EGL_NATIVE_VISUAL_ID,
                            &visual_id)) {
        wm_report_egl_error("X11 visual lookup");
        goto fail;
    }

    int screen = DefaultScreen(host->display);
    Visual *visual = DefaultVisual(host->display, screen);
    int depth = DefaultDepth(host->display, screen);
    XVisualInfo *visual_info = NULL;
    if (visual_id != 0) {
        XVisualInfo query = {0};
        query.visualid = (VisualID)visual_id;
        query.screen = screen;
        int matches = 0;
        visual_info = XGetVisualInfo(host->display, VisualIDMask | VisualScreenMask,
                                     &query, &matches);
        if (visual_info == NULL || matches == 0) {
            fprintf(stderr, "GLES2: EGL requested an unavailable X11 visual.\n");
            if (visual_info != NULL) {
                XFree(visual_info);
            }
            goto fail;
        }
        visual = visual_info->visual;
        depth = visual_info->depth;
    }

    Window root = RootWindow(host->display, screen);
    host->colormap = XCreateColormap(host->display, root, visual, AllocNone);
    if (host->colormap == 0) {
        fprintf(stderr, "GLES2: could not create the X11 colormap.\n");
        if (visual_info != NULL) {
            XFree(visual_info);
        }
        goto fail;
    }

    XSetWindowAttributes attributes = {0};
    attributes.colormap = host->colormap;
    attributes.border_pixel = 0;
    attributes.event_mask = StructureNotifyMask | ExposureMask | PointerMotionMask |
                            ButtonPressMask | ButtonReleaseMask | KeyPressMask |
                            KeyReleaseMask |
                            EnterWindowMask | LeaveWindowMask | FocusChangeMask;
    host->window = XCreateWindow(host->display, root, 0, 0, (unsigned int)width,
                                 (unsigned int)height, 0, depth, InputOutput, visual,
                                 CWColormap | CWBorderPixel | CWEventMask, &attributes);
    if (visual_info != NULL) {
        XFree(visual_info);
    }
    if (host->window == 0) {
        fprintf(stderr, "GLES2: could not create the X11 window.\n");
        goto fail;
    }

    const char transparent_pixel = 0;
    Pixmap blank =
        XCreateBitmapFromData(host->display, host->window, &transparent_pixel, 1, 1);
    if (blank != None) {
        XColor transparent = {0};
        host->hidden_cursor = XCreatePixmapCursor(host->display, blank, blank,
                                                  &transparent, &transparent, 0, 0);
        XFreePixmap(host->display, blank);
        if (host->hidden_cursor != None) {
            XDefineCursor(host->display, host->window, host->hidden_cursor);
        }
    }

    XStoreName(host->display, host->window, title != NULL ? title : "Wii Menu");
    host->delete_window = XInternAtom(host->display, "WM_DELETE_WINDOW", False);
    XSetWMProtocols(host->display, host->window, &host->delete_window, 1);

    host->egl_surface = eglCreateWindowSurface(host->egl_display, config,
                                               (EGLNativeWindowType)host->window, NULL);
    if (host->egl_surface == EGL_NO_SURFACE) {
        wm_report_egl_error("window surface creation");
        goto fail;
    }

    static const EGLint context_attributes[] = {EGL_CONTEXT_CLIENT_VERSION, 2,
                                                EGL_NONE};
    host->egl_context =
        eglCreateContext(host->egl_display, config, EGL_NO_CONTEXT, context_attributes);
    if (host->egl_context == EGL_NO_CONTEXT) {
        wm_report_egl_error("ES2 context creation");
        goto fail;
    }
    if (!eglMakeCurrent(host->egl_display, host->egl_surface, host->egl_surface,
                        host->egl_context)) {
        wm_report_egl_error("context activation");
        goto fail;
    }
    return host;

fail:
    wm_gles2_host_destroy(host);
    return NULL;
}

void wm_gles2_host_show(WmGles2Host *host) {
    if (!eglSwapInterval(host->egl_display, 1)) {
        wm_report_egl_error("swap interval selection");
    }
    XMapWindow(host->display, host->window);
    XFlush(host->display);
}

bool wm_gles2_host_make_current(WmGles2Host *host) {
    if (host == NULL || host->egl_display == EGL_NO_DISPLAY ||
        host->egl_context == EGL_NO_CONTEXT || host->egl_surface == EGL_NO_SURFACE) {
        return false;
    }
    eglMakeCurrent(host->egl_display, host->egl_surface, host->egl_surface,
                   host->egl_context);
    return eglGetCurrentContext() == host->egl_context;
}

bool wm_gles2_host_preserve_back_buffer(WmGles2Host *host)
{
    if (!eglSurfaceAttrib(host->egl_display, host->egl_surface,
                          EGL_SWAP_BEHAVIOR, EGL_BUFFER_PRESERVED)) {
        eglGetError();
        return false;
    }
    EGLint behavior = EGL_BUFFER_DESTROYED;
    return eglQuerySurface(host->egl_display, host->egl_surface,
                            EGL_SWAP_BEHAVIOR, &behavior) &&
        behavior == EGL_BUFFER_PRESERVED;
}

void wm_gles2_host_destroy(WmGles2Host *host) {
    if (host == NULL) {
        return;
    }
    if (host->egl_display != EGL_NO_DISPLAY) {
        eglMakeCurrent(host->egl_display, EGL_NO_SURFACE, EGL_NO_SURFACE,
                       EGL_NO_CONTEXT);
        if (host->egl_context != EGL_NO_CONTEXT) {
            eglDestroyContext(host->egl_display, host->egl_context);
        }
        if (host->egl_surface != EGL_NO_SURFACE) {
            eglDestroySurface(host->egl_display, host->egl_surface);
        }
        eglTerminate(host->egl_display);
    }
    if (host->display != NULL) {
        if (host->hidden_cursor != None) {
            XFreeCursor(host->display, host->hidden_cursor);
        }
        if (host->window != 0) {
            XDestroyWindow(host->display, host->window);
        }
        if (host->colormap != 0) {
            XFreeColormap(host->display, host->colormap);
        }
        XCloseDisplay(host->display);
    }
    free(host);
}

static void wm_pointer_event(WmGles2Host *host, WmEvent *event, int x, int y) {
    WmViewport viewport = wm_viewport_fit(host->window_width, host->window_height);
    if (viewport.width <= 0 || viewport.height <= 0) {
        event->type = WM_EVENT_POINTER_LEAVE;
        return;
    }
    event->outside_viewport =
        !wm_viewport_map_pointer_unbounded(viewport, x, y, &event->x, &event->y);
}

static WmKey wm_lookup_key(XKeyEvent *key_event) {
    KeySym symbol = NoSymbol;
    char text[8] = {0};
    int count = XLookupString(key_event, text, (int)sizeof(text), &symbol, NULL);

    switch (symbol) {
        case XK_Left:
            return WM_KEY_LEFT;
        case XK_Right:
            return WM_KEY_RIGHT;
        case XK_Up:
            return WM_KEY_UP;
        case XK_Down:
            return WM_KEY_DOWN;
        case XK_Return:
        case XK_KP_Enter:
            return WM_KEY_ENTER;
        case XK_Escape:
            return WM_KEY_ESCAPE;
        case XK_BackSpace:
            return WM_KEY_BACKSPACE;
        case XK_Home:
            return WM_KEY_HOME;
        default:
            break;
    }

    if (count == 1 && (unsigned char)text[0] >= 32 && (unsigned char)text[0] <= 126) {
        unsigned char character = (unsigned char)text[0];
        return (WmKey)character;
    }
    return WM_KEY_UNKNOWN;
}

bool wm_gles2_host_poll(WmGles2Host *host, WmEvent *event) {
    if (host == NULL || event == NULL) {
        return false;
    }
    *event = (WmEvent){.type = WM_EVENT_NONE, .key = WM_KEY_UNKNOWN};

    while (XPending(host->display) > 0) {
        XEvent native_event;
        XNextEvent(host->display, &native_event);

        switch (native_event.type) {
            case ClientMessage:
                if ((Atom)native_event.xclient.data.l[0] == host->delete_window) {
                    event->type = WM_EVENT_QUIT;
                    return true;
                }
                break;
            case DestroyNotify:
                host->window = 0;
                event->type = WM_EVENT_QUIT;
                return true;
            case ConfigureNotify:
                host->window_width = native_event.xconfigure.width;
                host->window_height = native_event.xconfigure.height;
                break;
            case MotionNotify:
                event->type = WM_EVENT_POINTER_MOVE;
                wm_pointer_event(host, event, native_event.xmotion.x,
                                 native_event.xmotion.y);
                return true;
            case EnterNotify:
                event->type = WM_EVENT_POINTER_MOVE;
                wm_pointer_event(host, event, native_event.xcrossing.x,
                                 native_event.xcrossing.y);
                return true;
            case LeaveNotify:
                event->type = WM_EVENT_POINTER_LEAVE;
                return true;
            case FocusOut:
                event->type = WM_EVENT_POINTER_LEAVE;
                event->cancel_capture = true;
                return true;
            case KeyRelease:
            case KeyPress: {
                KeySym symbol = XLookupKeysym(&native_event.xkey, 0);
                bool pressed = native_event.type == KeyPress;
                if (symbol == XK_Shift_L || symbol == XK_Shift_R ||
                    symbol == XK_Caps_Lock) {
                    event->type = WM_EVENT_KEY_MODIFIERS;
                    event->key = symbol == XK_Caps_Lock
                        ? (pressed ? WM_KEY_CAPS_LOCK : WM_KEY_UNKNOWN)
                        : WM_KEY_SHIFT;
                    event->shift_down = (native_event.xkey.state & ShiftMask) != 0;
                    event->caps_lock_on = (native_event.xkey.state & LockMask) != 0;
                    if (symbol == XK_Caps_Lock && pressed)
                        event->caps_lock_on = !event->caps_lock_on;
                    if (symbol == XK_Shift_L || symbol == XK_Shift_R)
                        event->shift_down = pressed;
                    return true;
                }
                if (!pressed) break;
                event->type = WM_EVENT_KEY_DOWN;
                event->key = wm_lookup_key(&native_event.xkey);
                event->shift_down = (native_event.xkey.state & ShiftMask) != 0;
                event->caps_lock_on = (native_event.xkey.state & LockMask) != 0;
                return true;
            }
            case ButtonPress:
            case ButtonRelease:
                if (native_event.xbutton.button != Button1 &&
                    native_event.xbutton.button != Button2 &&
                    native_event.xbutton.button != Button3) {
                    break;
                }
                event->type = native_event.type == ButtonPress ? WM_EVENT_POINTER_DOWN
                                                               : WM_EVENT_POINTER_UP;
                event->button = (WmPointerButton)native_event.xbutton.button;
                wm_pointer_event(host, event, native_event.xbutton.x,
                                 native_event.xbutton.y);
                return true;
            default:
                break;
        }
    }
    return false;
}

void wm_gles2_host_surface_size(WmGles2Host *host, int *width, int *height) {
    EGLint surface_width = host->window_width;
    EGLint surface_height = host->window_height;
    eglQuerySurface(host->egl_display, host->egl_surface, EGL_WIDTH, &surface_width);
    eglQuerySurface(host->egl_display, host->egl_surface, EGL_HEIGHT, &surface_height);
    *width = surface_width;
    *height = surface_height;
}

bool wm_gles2_host_present(WmGles2Host *host) {
    if (eglSwapBuffers(host->egl_display, host->egl_surface)) return true;
    if (!host->swap_failure_reported) {
        wm_report_egl_error("frame presentation");
        host->swap_failure_reported = true;
    }
    return false;
}
