#include "platform_metal_internal.h"

#include "wii_menu/render/viewport.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void wm_enqueue_event(WmPlatform *platform, WmEvent event)
{
    if (!platform) {
        return;
    }

    if (platform->event_read == platform->event_count) {
        platform->event_read = 0;
        platform->event_count = 0;
    }
    if (platform->event_count == platform->event_capacity) {
        size_t capacity = platform->event_capacity ? platform->event_capacity * 2 : 32;
        if (capacity < platform->event_capacity || capacity > SIZE_MAX / sizeof(WmEvent)) {
            return;
        }
        WmEvent *events = realloc(platform->events, capacity * sizeof(WmEvent));
        if (!events) {
            return;
        }
        platform->events = events;
        platform->event_capacity = capacity;
    }
    platform->events[platform->event_count++] = event;
}

@implementation WmMetalView

- (BOOL)acceptsFirstResponder
{
    return YES;
}

- (BOOL)acceptsFirstMouse:(NSEvent *)event
{
    (void)event;
    return YES;
}

- (void)resetCursorRects
{
    [super resetCursorRects];
    if (self.hiddenCursor) {
        [self addCursorRect:self.bounds cursor:self.hiddenCursor];
    }
}

- (void)updateTrackingAreas
{
    [super updateTrackingAreas];
    if (self.pointerTrackingArea) {
        [self removeTrackingArea:self.pointerTrackingArea];
    }
    NSTrackingAreaOptions options = NSTrackingMouseEnteredAndExited |
                                    NSTrackingMouseMoved |
                                    NSTrackingActiveInActiveApp |
                                    NSTrackingInVisibleRect;
    self.pointerTrackingArea = [[NSTrackingArea alloc]
        initWithRect:self.bounds options:options owner:self userInfo:nil];
    [self addTrackingArea:self.pointerTrackingArea];
}

- (void)mouseEntered:(NSEvent *)event
{
    [self recordPointer:event type:WM_EVENT_POINTER_MOVE];
}

- (void)mouseExited:(NSEvent *)event
{
    (void)event;
    wm_enqueue_event(self.platform, (WmEvent){ .type = WM_EVENT_POINTER_LEAVE });
}

- (void)recordPointer:(NSEvent *)native_event type:(WmEventType)type
{
    NSPoint point = [self convertPoint:native_event.locationInWindow fromView:nil];
    NSRect bounds = self.bounds;
    if (bounds.size.width <= 0 || bounds.size.height <= 0) {
        return;
    }

    NSSize backing = [self convertSizeToBacking:bounds.size];
    WmViewport content = wm_viewport_fit((int)llround(backing.width),
                                          (int)llround(backing.height));
    if (content.width <= 0 || content.height <= 0) {
        wm_enqueue_event(self.platform, (WmEvent){ .type = WM_EVENT_POINTER_LEAVE });
        return;
    }
    int x = 0, y = 0;
    int output_x = (int)floor(point.x * backing.width / bounds.size.width);
    int output_y = (int)floor((bounds.size.height - point.y) *
                              backing.height / bounds.size.height);
    bool outside = !wm_viewport_map_pointer_unbounded(
        content, output_x, output_y, &x, &y);
    WmEvent event = {
        .type = type,
        .x = x,
        .y = y,
        .outside_viewport = outside,
        .key = WM_KEY_UNKNOWN,
        .button = native_event.buttonNumber == 1 ? WM_POINTER_RIGHT
                  : native_event.buttonNumber == 2 ? WM_POINTER_MIDDLE
                  : WM_POINTER_LEFT,
    };
    wm_enqueue_event(self.platform, event);
}

- (void)mouseDown:(NSEvent *)event
{
    [self recordPointer:event type:WM_EVENT_POINTER_DOWN];
}

- (void)mouseUp:(NSEvent *)event
{
    [self recordPointer:event type:WM_EVENT_POINTER_UP];
}

- (void)mouseMoved:(NSEvent *)event
{
    [self recordPointer:event type:WM_EVENT_POINTER_MOVE];
}

- (void)mouseDragged:(NSEvent *)event
{
    [self recordPointer:event type:WM_EVENT_POINTER_MOVE];
}

- (void)rightMouseDown:(NSEvent *)event
{
    [self mouseDown:event];
}

- (void)rightMouseUp:(NSEvent *)event
{
    [self mouseUp:event];
}

- (void)rightMouseDragged:(NSEvent *)event
{
    [self mouseDragged:event];
}

- (void)otherMouseDown:(NSEvent *)event
{
    [self mouseDown:event];
}

- (void)otherMouseUp:(NSEvent *)event
{
    [self mouseUp:event];
}

- (void)otherMouseDragged:(NSEvent *)event
{
    [self mouseDragged:event];
}

- (void)keyDown:(NSEvent *)native_event
{
    WmKey key = WM_KEY_UNKNOWN;
    switch (native_event.keyCode) {
        case 123: key = WM_KEY_LEFT; break;
        case 124: key = WM_KEY_RIGHT; break;
        case 125: key = WM_KEY_DOWN; break;
        case 126: key = WM_KEY_UP; break;
        case 36:
        case 76: key = WM_KEY_ENTER; break;
        case 53: key = WM_KEY_ESCAPE; break;
        case 51:
        case 117: key = WM_KEY_BACKSPACE; break;
        case 115: key = WM_KEY_HOME; break;
        default: {
            NSString *characters = native_event.characters;
            if (characters.length > 0) {
                unichar character = [characters characterAtIndex:0];
                if (character < 128) {
                    key = (WmKey)character;
                }
            }
            break;
        }
    }

    WmEvent event = {
        .type = WM_EVENT_KEY_DOWN,
        .key = key,
        .shift_down = (native_event.modifierFlags & NSEventModifierFlagShift) != 0,
        .caps_lock_on = (native_event.modifierFlags & NSEventModifierFlagCapsLock) != 0,
    };
    if ((native_event.modifierFlags & NSEventModifierFlagCommand) &&
        (key == 'q' || key == 'Q')) {
        event.type = WM_EVENT_QUIT;
    }
    wm_enqueue_event(self.platform, event);
}

- (void)flagsChanged:(NSEvent *)native_event
{
    WmKey key = WM_KEY_UNKNOWN;
    if (native_event.keyCode == 56 || native_event.keyCode == 60)
        key = WM_KEY_SHIFT;
    else if (native_event.keyCode == 57)
        key = WM_KEY_CAPS_LOCK;
    wm_enqueue_event(self.platform, (WmEvent){
        .type = WM_EVENT_KEY_MODIFIERS,
        .key = key,
        .shift_down = (native_event.modifierFlags & NSEventModifierFlagShift) != 0,
        .caps_lock_on = (native_event.modifierFlags & NSEventModifierFlagCapsLock) != 0,
    });
}

@end

@implementation WmWindowDelegate

- (void)windowDidBecomeKey:(NSNotification *)notification
{
    NSWindow *window = notification.object;
    if (!self.view || !window) return;
    [window makeFirstResponder:self.view];
    [window setAcceptsMouseMovedEvents:YES];
    [self.view updateTrackingAreas];
}

- (void)windowDidResignKey:(NSNotification *)notification
{
    (void)notification;
    wm_enqueue_event(self.platform, (WmEvent){
        .type = WM_EVENT_POINTER_LEAVE,
        .cancel_capture = true
    });
    wm_enqueue_event(self.platform, (WmEvent){
        .type = WM_EVENT_KEY_MODIFIERS,
        .caps_lock_on = ([NSEvent modifierFlags] & NSEventModifierFlagCapsLock) != 0,
    });
}

- (void)windowWillClose:(NSNotification *)notification
{
    (void)notification;
    WmEvent event = { .type = WM_EVENT_QUIT };
    wm_enqueue_event(self.platform, event);
}

@end

WmPlatform *wm_platform_create(const char *title, int window_width, int window_height)
{
    if (![NSThread isMainThread]) {
        fprintf(stderr, "The Metal window must be created on the main thread.\n");
        return NULL;
    }

    @autoreleasepool {
        WmPlatform *platform = calloc(1, sizeof(*platform));
        if (!platform) {
            return NULL;
        }
        WmMetalState *state = [WmMetalState new];
        platform->metal_state = (__bridge_retained void *)state;
        if (!wm_prepare_metal(state)) {
            wm_platform_destroy(platform);
            return NULL;
        }

        [NSApplication sharedApplication];
        [NSApp setActivationPolicy:NSApplicationActivationPolicyRegular];
        [NSApp finishLaunching];

        CGFloat width = window_width > 0 ? window_width : WM_FRAME_WIDTH;
        CGFloat height = window_height > 0 ? window_height : WM_FRAME_HEIGHT;
        NSRect frame = NSMakeRect(0, 0, width, height);
        NSWindowStyleMask style = NSWindowStyleMaskTitled |
                                  NSWindowStyleMaskClosable |
                                  NSWindowStyleMaskMiniaturizable |
                                  NSWindowStyleMaskResizable;
        state->window = [[NSWindow alloc] initWithContentRect:frame
                                                   styleMask:style
                                                     backing:NSBackingStoreBuffered
                                                       defer:NO];
        if (!state->window) {
            wm_platform_destroy(platform);
            return NULL;
        }
        [state->window setReleasedWhenClosed:NO];
        NSString *window_title = title ? [NSString stringWithUTF8String:title] : nil;
        state->window.title = window_title ? window_title : @"Wii Menu";

        state->view = [[WmMetalView alloc] initWithFrame:frame];
        state->view.platform = platform;
        NSBitmapImageRep *cursor_bitmap = [[NSBitmapImageRep alloc]
            initWithBitmapDataPlanes:NULL pixelsWide:1 pixelsHigh:1
                    bitsPerSample:8 samplesPerPixel:4 hasAlpha:YES isPlanar:NO
                   colorSpaceName:NSDeviceRGBColorSpace
                      bytesPerRow:4 bitsPerPixel:32];
        if (!cursor_bitmap) {
            wm_platform_destroy(platform);
            return NULL;
        }
        memset(cursor_bitmap.bitmapData, 0, 4);
        NSImage *cursor_image = [[NSImage alloc] initWithSize:NSMakeSize(1, 1)];
        [cursor_image addRepresentation:cursor_bitmap];
        state->view.hiddenCursor = [[NSCursor alloc]
            initWithImage:cursor_image hotSpot:NSZeroPoint];
        [state->view setWantsLayer:YES];
        state->layer = [CAMetalLayer layer];
        state->layer.device = state->device;
        state->layer.pixelFormat = MTLPixelFormatBGRA8Unorm;
        state->layer.framebufferOnly = YES;
        [state->view setLayer:state->layer];
        [state->window setContentView:state->view];
        [state->window invalidateCursorRectsForView:state->view];

        state->window_delegate = [WmWindowDelegate new];
        state->window_delegate.platform = platform;
        state->window_delegate.view = state->view;
        state->window.delegate = state->window_delegate;
        [state->window setAcceptsMouseMovedEvents:YES];
        [state->window center];
        [NSApp activateIgnoringOtherApps:YES];
        [state->window makeKeyAndOrderFront:nil];
        [state->window makeFirstResponder:state->view];
        return platform;
    }
}

void wm_platform_destroy(WmPlatform *platform)
{
    if (!platform) {
        return;
    }

    @autoreleasepool {
        WmMetalState *state = (__bridge_transfer WmMetalState *)platform->metal_state;
        if (state) {
            state->view.platform = NULL;
            state->window_delegate.platform = NULL;
            state->window.delegate = nil;
            wm_wait_for_metal(state);
            [state->window close];
            wm_release_metal(state);
        }
        free(platform->events);
        free(platform);
    }
}

bool wm_platform_poll(WmPlatform *platform, WmEvent *event)
{
    if (!platform || !event) {
        return false;
    }

    @autoreleasepool {
        if (platform->event_read == platform->event_count) {
            /* One native event may produce more than one queued menu event. */
            for (int attempt = 0; attempt < 64; ++attempt) {
                NSEvent *native_event =
                    [NSApp nextEventMatchingMask:NSEventMaskAny
                                      untilDate:[NSDate distantPast]
                                         inMode:NSDefaultRunLoopMode
                                        dequeue:YES];
                if (!native_event) {
                    break;
                }
                [NSApp sendEvent:native_event];
                if (platform->event_read < platform->event_count) {
                    break;
                }
            }
            [NSApp updateWindows];
        }

        if (platform->event_read == platform->event_count) {
            *event = (WmEvent){ .type = WM_EVENT_NONE };
            return false;
        }
        *event = platform->events[platform->event_read++];
        return true;
    }
}
