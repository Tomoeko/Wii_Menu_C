#import <AppKit/AppKit.h>
#import <dispatch/dispatch.h>

#include "pointer_transport.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

static uint64_t monotonic_ms(void) {
    struct timespec now;
    clock_gettime(CLOCK_MONOTONIC, &now);
    return (uint64_t)now.tv_sec * 1000 + (uint64_t)now.tv_nsec / 1000000;
}

@class WmPointerApp;

@interface WmPointerView : NSView
@property(nonatomic, weak) WmPointerApp *owner;
@property(nonatomic, strong) NSTrackingArea *pointerTrackingArea;
@end

@interface WmPointerApp : NSObject <NSApplicationDelegate, NSWindowDelegate> {
    WmPointerTransport _transport;
    uint16_t _x;
    uint16_t _y;
    uint8_t _buttons;
    BOOL _active;
    BOOL _motionPending;
    uint64_t _lastSentMs;
    uint64_t _lastInactiveHeartbeatMs;
    int _writeFd;
    BOOL _wasConnected;
}
@property(nonatomic, strong) NSWindow *window;
@property(nonatomic, strong) WmPointerView *view;
@property(nonatomic, strong) NSTimer *timer;
@property(nonatomic, strong) dispatch_source_t writeSource;
@property(nonatomic) const char *inputPath;
- (void)recordPointer:(NSEvent *)event immediate:(BOOL)immediate;
- (void)cancelPointer;
- (void)resizePointer;
@end

@implementation WmPointerView
- (BOOL)isFlipped { return YES; }
- (BOOL)acceptsFirstResponder { return YES; }
- (BOOL)acceptsFirstMouse:(NSEvent *)event { (void)event; return YES; }

- (void)drawRect:(NSRect)rect {
    [[NSColor blackColor] setFill];
    NSRectFill(rect);
}

- (void)updateTrackingAreas {
    [super updateTrackingAreas];
    if (self.pointerTrackingArea) [self removeTrackingArea:self.pointerTrackingArea];
    self.pointerTrackingArea = [[NSTrackingArea alloc]
        initWithRect:self.bounds
        options:NSTrackingMouseEnteredAndExited | NSTrackingMouseMoved |
                NSTrackingActiveAlways | NSTrackingInVisibleRect
        owner:self userInfo:nil];
    [self addTrackingArea:self.pointerTrackingArea];
}

- (void)mouseEntered:(NSEvent *)event { [self.owner recordPointer:event immediate:YES]; }
- (void)mouseExited:(NSEvent *)event { (void)event; [self.owner cancelPointer]; }
- (void)mouseMoved:(NSEvent *)event { [self.owner recordPointer:event immediate:NO]; }
- (void)mouseDragged:(NSEvent *)event { [self.owner recordPointer:event immediate:NO]; }
- (void)rightMouseDragged:(NSEvent *)event { [self.owner recordPointer:event immediate:NO]; }
- (void)mouseDown:(NSEvent *)event { [self.owner recordPointer:event immediate:YES]; }
- (void)mouseUp:(NSEvent *)event { [self.owner recordPointer:event immediate:YES]; }
- (void)rightMouseDown:(NSEvent *)event { [self.owner recordPointer:event immediate:YES]; }
- (void)rightMouseUp:(NSEvent *)event { [self.owner recordPointer:event immediate:YES]; }
- (void)keyDown:(NSEvent *)event {
    if (event.keyCode == 53) [self.owner cancelPointer];
}
@end

@implementation WmPointerApp
- (void)applicationDidFinishLaunching:(NSNotification *)notification {
    (void)notification;
    wm_pointer_transport_init(&_transport, self.inputPath,
                                (uint16_t)arc4random_uniform(65536));
    _writeFd = -1;
    self.window = [[NSWindow alloc]
        initWithContentRect:NSMakeRect(0, 0, 960, 540)
        styleMask:NSWindowStyleMaskTitled | NSWindowStyleMaskClosable |
                  NSWindowStyleMaskMiniaturizable | NSWindowStyleMaskResizable
        backing:NSBackingStoreBuffered defer:NO];
    self.window.delegate = self;
    self.window.title = @"Wii Menu PSVR2 Pointer — disconnected";
    self.window.contentMinSize = NSMakeSize(160, 90);
    self.window.acceptsMouseMovedEvents = YES;
    self.window.backgroundColor = NSColor.blackColor;
    self.view = [[WmPointerView alloc] initWithFrame:self.window.contentView.bounds];
    self.view.owner = self;
    self.view.autoresizingMask = NSViewWidthSizable | NSViewHeightSizable;
    self.window.contentView = self.view;
    [self.window center];
    [self.window makeKeyAndOrderFront:nil];
    [self.window makeFirstResponder:self.view];
    [NSApp activateIgnoringOtherApps:YES];
    self.timer = [NSTimer timerWithTimeInterval:0.008 target:self
                                      selector:@selector(tick:)
                                      userInfo:nil repeats:YES];
    [[NSRunLoop mainRunLoop] addTimer:self.timer forMode:NSRunLoopCommonModes];
}

- (void)sendState:(uint64_t)now {
    wm_pointer_transport_update(&_transport, _x, _y, _active,
                                  _buttons, now);
    _lastSentMs = now;
    _motionPending = NO;
    [self updateWriter];
}

- (void)updateWriter {
    if (self.writeSource && (_transport.fd != _writeFd || !_transport.count)) {
        dispatch_source_cancel(self.writeSource);
        self.writeSource = nil;
        _writeFd = -1;
    }
    if (!self.writeSource && _transport.fd >= 0 && _transport.count) {
        _writeFd = _transport.fd;
        self.writeSource = dispatch_source_create(DISPATCH_SOURCE_TYPE_WRITE,
            (uintptr_t)_writeFd, 0, dispatch_get_main_queue());
        __weak WmPointerApp *weakSelf = self;
        dispatch_source_set_event_handler(self.writeSource, ^{
            WmPointerApp *owner = weakSelf;
            if (!owner) return;
            wm_pointer_transport_flush(&owner->_transport, monotonic_ms());
            [owner updateWriter];
        });
        dispatch_resume(self.writeSource);
    }
}

- (void)recordPosition:(NSPoint)point immediate:(BOOL)immediate {
    BOOL active = wm_psvr2_pointer_normalize(
        point.x, point.y, self.view.bounds.size.width,
        self.view.bounds.size.height, &_x, &_y);
    active = active && self.window.visible && !self.window.miniaturized &&
             self.window.keyWindow && NSApp.active;
    NSUInteger physical = NSEvent.pressedMouseButtons;
    uint8_t buttons = (physical & 1 ? WM_PSVR2_POINTER_LEFT : 0) |
                      (physical & 2 ? WM_PSVR2_POINTER_RIGHT : 0);
    immediate |= active != _active || buttons != _buttons;
    _active = active;
    _buttons = buttons;
    _motionPending = YES;
    uint64_t now = monotonic_ms();
    if (immediate || now - _lastSentMs >= 8) [self sendState:now];
}

- (void)recordPointer:(NSEvent *)event immediate:(BOOL)immediate {
    [self recordPosition:[self.view convertPoint:event.locationInWindow fromView:nil]
                immediate:immediate];
}

- (void)resizePointer {
    /* Re-evaluate stationary hover when the user resizes the content area. */
    [self recordPosition:[self.view convertPoint:
                          self.window.mouseLocationOutsideOfEventStream fromView:nil]
                immediate:YES];
}

- (void)cancelPointer {
    _active = NO;
    [self sendState:monotonic_ms()];
}

- (void)tick:(NSTimer *)timer {
    (void)timer;
    uint64_t now = monotonic_ms();
    if (_motionPending) {
        [self sendState:now];
    } else if (_active || now - _lastInactiveHeartbeatMs >= 50) {
        wm_pointer_transport_heartbeat(&_transport, now);
        _lastInactiveHeartbeatMs = now;
        [self updateWriter];
    }
    BOOL connected = _transport.fd >= 0;
    if (connected != _wasConnected) {
        self.window.title = connected ? @"Wii Menu PSVR2 Pointer — connected"
                                      : @"Wii Menu PSVR2 Pointer — disconnected";
        _wasConnected = connected;
    }
}

- (void)windowDidResize:(NSNotification *)notification {
    (void)notification;
    [self resizePointer];
}
- (void)windowDidResignKey:(NSNotification *)notification {
    (void)notification;
    [self cancelPointer];
    if (self.writeSource) dispatch_source_cancel(self.writeSource);
    self.writeSource = nil;
}
- (void)windowDidMiniaturize:(NSNotification *)notification {
    (void)notification;
    [self cancelPointer];
}
- (void)windowDidBecomeKey:(NSNotification *)notification {
    (void)notification;
    [self resizePointer];
}
- (void)applicationDidResignActive:(NSNotification *)notification {
    (void)notification;
    [self cancelPointer];
}
- (void)applicationDidBecomeActive:(NSNotification *)notification {
    (void)notification;
    [self resizePointer];
}
- (BOOL)applicationShouldTerminateAfterLastWindowClosed:(NSApplication *)app {
    (void)app;
    return YES;
}
- (void)applicationWillTerminate:(NSNotification *)notification {
    (void)notification;
    [self.timer invalidate];
    [self cancelPointer];
    wm_pointer_transport_close(&_transport);
    fprintf(stderr, "Pointer packets written: %llu; coalesced pending motions: %llu.\n",
            (unsigned long long)_transport.packets_written,
            (unsigned long long)_transport.motions_coalesced);
}
@end

int main(int argc, char **argv) {
    if (argc != 3 || strcmp(argv[1], "--input-port") != 0 ||
        strncmp(argv[2], "/dev/", 5) != 0) {
        fprintf(stderr, "Usage: %s --input-port /dev/cu.usbmodemINPUT\n"
                "Select the SECOND Stage3 ACM port, bridged to /dev/fast_input.\n"
                "The control shell/upload port must not be used for input.\n", argv[0]);
        return 2;
    }
    @autoreleasepool {
        NSApplication *application = [NSApplication sharedApplication];
        [application setActivationPolicy:NSApplicationActivationPolicyRegular];
        WmPointerApp *delegate = [[WmPointerApp alloc] init];
        delegate.inputPath = argv[2];
        application.delegate = delegate;
        [application run];
    }
    return 0;
}
