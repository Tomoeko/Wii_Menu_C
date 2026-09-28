#import <AppKit/AppKit.h>
#import <Metal/Metal.h>
#import <QuartzCore/CAMetalLayer.h>

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "wii_menu/platform/platform.h"
#include "wii_menu/render/viewport.h"
#include "geometry.h"
#include "shaders.h"

#if !__has_feature(objc_arc)
#error "The Metal platform adapter must be compiled with -fobjc-arc."
#endif

#define WM_IN_FLIGHT_FRAMES 3

typedef struct WmVertex {
    float x;
    float y;
    float uv[WM_MATERIAL_TEXTURES][2];
    float padding[2];
    WmColor color;
} WmVertex;

_Static_assert(sizeof(WmVertex) == 16 * sizeof(float),
               "The C and Metal vertex layouts must match.");

typedef struct WmMaterialParams {
    float r0[4];
    float r1[4];
    float kc3[4];
    uint32_t texture_count;
    uint32_t alpha_comparisons;
    uint32_t alpha_operation;
    uint32_t alpha_references;
} WmMaterialParams;

_Static_assert(sizeof(WmMaterialParams) == 16 * sizeof(float),
               "The C and Metal material layouts must match.");

/* Four stage bytes per word keep the fragment parameter block compact while
 * preserving the raw BRLYT encoding used by the HTML renderer. */
typedef struct WmTevParams {
    float registers[3][4];
    float konst_colors[4][4];
    uint32_t stage_words[6][4];
    uint32_t swap[4];
    uint32_t stage_count;
    uint32_t alpha_comparisons;
    uint32_t alpha_operation;
    uint32_t alpha_references;
} WmTevParams;

_Static_assert(sizeof(WmTevParams) == 60 * sizeof(float),
               "The C and Metal TEV layouts must match.");

typedef enum WmBatchKind {
    WM_BATCH_BASIC,
    WM_BATCH_MATERIAL,
    WM_BATCH_TEV
} WmBatchKind;

typedef struct WmBatchState {
    WmBatchKind kind;
    bool clip_enabled;
    WmClipRect clip;
    uint32_t textures[WM_MATERIAL_TEXTURES];
    uint8_t wrap_s[WM_MATERIAL_TEXTURES];
    uint8_t wrap_t[WM_MATERIAL_TEXTURES];
    uint8_t blend_key;
    union {
        WmMaterialParams simple;
        WmTevParams tev;
    } params;
} WmBatchState;

typedef struct WmBatch {
    WmBatchState state;
    size_t first_vertex;
    size_t vertex_count;
} WmBatch;

struct WmPlatform {
    void *metal_state;
    WmEvent *events;
    size_t event_count;
    size_t event_read;
    size_t event_capacity;
    float fade_alpha;
};

static void wm_enqueue_event(WmPlatform *platform, WmEvent event);

@interface WmMetalView : NSView
@property(nonatomic, assign) WmPlatform *platform;
@property(nonatomic, strong) NSCursor *hiddenCursor;
@property(nonatomic, strong) NSTrackingArea *pointerTrackingArea;
@end

@interface WmWindowDelegate : NSObject <NSWindowDelegate>
@property(nonatomic, assign) WmPlatform *platform;
@property(nonatomic, weak) WmMetalView *view;
@end

@interface WmMetalState : NSObject {
@public
    NSWindow *window;
    WmMetalView *view;
    WmWindowDelegate *window_delegate;
    CAMetalLayer *layer;
    id<MTLDevice> device;
    id<MTLCommandQueue> command_queue;
    id<MTLRenderPipelineState> pipeline;
    id<MTLFunction> vertex_function;
    id<MTLFunction> material_fragment_function;
    id<MTLFunction> tev_fragment_function;
    NSMutableDictionary<NSNumber *, id<MTLRenderPipelineState>> *material_pipelines;
    id<MTLSamplerState> samplers[3][3];
    MTLRenderPassDescriptor *render_pass;
    NSMutableArray *textures;
    NSMutableArray<NSNumber *> *free_texture_handles;
    NSMutableIndexSet *retired_texture_handles;
    id<MTLBuffer> vertex_buffers[WM_IN_FLIGHT_FRAMES];
    id<MTLCommandBuffer> in_flight[WM_IN_FLIGHT_FRAMES];
    NSUInteger buffer_sizes[WM_IN_FLIGHT_FRAMES];
    WmVertex *vertices;
    size_t vertex_count;
    size_t vertex_capacity;
    WmBatch *batches;
    size_t batch_count;
    size_t batch_capacity;
    WmColor clear_color;
    uint32_t render_target_handle;
    bool clip_enabled;
    WmClipRect clip;
    uint64_t frame_number;
    bool warned_tev_limit;
}
@end

@implementation WmMetalState
@end

static WmMetalState *wm_state(WmPlatform *platform)
{
    return platform ? (__bridge WmMetalState *)platform->metal_state : nil;
}

static void wm_release_retired_textures(WmMetalState *state)
{
    NSUInteger handle = state->retired_texture_handles.firstIndex;
    while (handle != NSNotFound) {
        [state->textures replaceObjectAtIndex:handle withObject:[NSNull null]];
        [state->free_texture_handles addObject:@(handle)];
        handle = [state->retired_texture_handles indexGreaterThanIndex:handle];
    }
    [state->retired_texture_handles removeAllIndexes];
}

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
            NSString *characters = native_event.charactersIgnoringModifiers;
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
    };
    if ((native_event.modifierFlags & NSEventModifierFlagCommand) &&
        (key == 'q' || key == 'Q')) {
        event.type = WM_EVENT_QUIT;
    }
    wm_enqueue_event(self.platform, event);
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
}

- (void)windowWillClose:(NSNotification *)notification
{
    (void)notification;
    WmEvent event = { .type = WM_EVENT_QUIT };
    wm_enqueue_event(self.platform, event);
}

@end

static id<MTLTexture> wm_make_texture(WmMetalState *state, int width, int height,
                                       const uint8_t *rgba)
{
    if (width <= 0 || height <= 0 || !rgba ||
        (size_t)width > SIZE_MAX / 4 ||
        (size_t)height > SIZE_MAX / ((size_t)width * 4)) {
        return nil;
    }

    MTLTextureDescriptor *descriptor =
        [MTLTextureDescriptor texture2DDescriptorWithPixelFormat:MTLPixelFormatRGBA8Unorm
                                                           width:(NSUInteger)width
                                                          height:(NSUInteger)height
                                                       mipmapped:NO];
    descriptor.usage = MTLTextureUsageShaderRead;
    descriptor.storageMode = MTLStorageModeShared;
    id<MTLTexture> texture = [state->device newTextureWithDescriptor:descriptor];
    if (!texture) {
        return nil;
    }

    MTLRegion region = MTLRegionMake2D(0, 0, (NSUInteger)width,
                                        (NSUInteger)height);
    [texture replaceRegion:region
              mipmapLevel:0
                withBytes:rgba
              bytesPerRow:(NSUInteger)width * 4];
    return texture;
}

enum { WM_BLEND_DISABLED = 64, WM_BLEND_DEFAULT = 4 * 8 + 5 };

static id<MTLRenderPipelineState> wm_make_pipeline(WmMetalState *state,
                                                    id<MTLFunction> fragment,
                                                    uint8_t blend_key)
{
    static const MTLBlendFactor factors[8] = {
        MTLBlendFactorZero,
        MTLBlendFactorOne,
        MTLBlendFactorDestinationColor,
        MTLBlendFactorOneMinusDestinationColor,
        MTLBlendFactorSourceAlpha,
        MTLBlendFactorOneMinusSourceAlpha,
        MTLBlendFactorDestinationAlpha,
        MTLBlendFactorOneMinusDestinationAlpha,
    };

    MTLRenderPipelineDescriptor *description = [MTLRenderPipelineDescriptor new];
    description.vertexFunction = state->vertex_function;
    description.fragmentFunction = fragment;
    MTLRenderPipelineColorAttachmentDescriptor *attachment =
        description.colorAttachments[0];
    attachment.pixelFormat = MTLPixelFormatBGRA8Unorm;
    attachment.blendingEnabled = blend_key != WM_BLEND_DISABLED;
    if (blend_key != WM_BLEND_DISABLED) {
        attachment.rgbBlendOperation = MTLBlendOperationAdd;
        attachment.alphaBlendOperation = MTLBlendOperationAdd;
        attachment.sourceRGBBlendFactor = factors[blend_key / 8];
        attachment.destinationRGBBlendFactor = factors[blend_key % 8];
        attachment.sourceAlphaBlendFactor = MTLBlendFactorOne;
        attachment.destinationAlphaBlendFactor = MTLBlendFactorOneMinusSourceAlpha;
    }
    NSError *error = nil;
    id<MTLRenderPipelineState> pipeline =
        [state->device newRenderPipelineStateWithDescriptor:description error:&error];
    if (!pipeline) {
        const char *message = error.localizedDescription.UTF8String;
        fprintf(stderr, "Metal pipeline unavailable: %s\n",
                message ? message : "unknown error");
    }
    return pipeline;
}

static id<MTLRenderPipelineState> wm_material_pipeline(WmMetalState *state,
                                                        WmBatchKind kind,
                                                        uint8_t blend_key)
{
    NSNumber *key = @((NSUInteger)kind * 65u + blend_key);
    id<MTLRenderPipelineState> pipeline = state->material_pipelines[key];
    if (!pipeline) {
        /* Pipeline variants are cached; the shader library is compiled once. */
        id<MTLFunction> fragment = kind == WM_BATCH_TEV
            ? state->tev_fragment_function : state->material_fragment_function;
        pipeline = wm_make_pipeline(state, fragment, blend_key);
        if (pipeline) {
            state->material_pipelines[key] = pipeline;
        }
    }
    return pipeline;
}

static bool wm_prepare_metal(WmMetalState *state)
{
    state->device = MTLCreateSystemDefaultDevice();
    if (!state->device) {
        fprintf(stderr, "Metal device unavailable.\n");
        return false;
    }

    state->command_queue = [state->device newCommandQueue];
    NSError *error = nil;
    id<MTLLibrary> library = [state->device newLibraryWithSource:wm_metal_shader_source()
                                                         options:nil
                                                           error:&error];
    if (!library) {
        const char *message = error.localizedDescription.UTF8String;
        fprintf(stderr, "Metal shader compilation failed: %s\n",
                message ? message : "unknown error");
        return false;
    }

    state->vertex_function = [library newFunctionWithName:@"wm_vertex"];
    id<MTLFunction> basic_fragment = [library newFunctionWithName:@"wm_fragment"];
    state->material_fragment_function =
        [library newFunctionWithName:@"wm_material_fragment"];
    state->tev_fragment_function = [library newFunctionWithName:@"wm_tev_fragment"];
    if (!state->vertex_function || !basic_fragment ||
        !state->material_fragment_function || !state->tev_fragment_function) {
        fprintf(stderr, "Metal shader entry point unavailable.\n");
        return false;
    }
    state->pipeline = wm_make_pipeline(state, basic_fragment, WM_BLEND_DEFAULT);
    state->material_pipelines = [NSMutableDictionary dictionary];
    if (!state->pipeline || !state->command_queue) {
        return false;
    }
    if (!wm_material_pipeline(state, WM_BATCH_MATERIAL, WM_BLEND_DEFAULT) ||
        !wm_material_pipeline(state, WM_BATCH_MATERIAL, WM_BLEND_DISABLED) ||
        !wm_material_pipeline(state, WM_BATCH_TEV, WM_BLEND_DEFAULT) ||
        !wm_material_pipeline(state, WM_BATCH_TEV, WM_BLEND_DISABLED)) {
        return false;
    }

    MTLSamplerDescriptor *sampler_description = [MTLSamplerDescriptor new];
    sampler_description.minFilter = MTLSamplerMinMagFilterLinear;
    sampler_description.magFilter = MTLSamplerMinMagFilterLinear;
    const MTLSamplerAddressMode wraps[3] = {
        MTLSamplerAddressModeClampToEdge,
        MTLSamplerAddressModeRepeat,
        MTLSamplerAddressModeMirrorRepeat,
    };
    for (unsigned s = 0; s < 3; ++s) {
        for (unsigned t = 0; t < 3; ++t) {
            sampler_description.sAddressMode = wraps[s];
            sampler_description.tAddressMode = wraps[t];
            state->samplers[s][t] =
                [state->device newSamplerStateWithDescriptor:sampler_description];
            if (!state->samplers[s][t]) {
                return false;
            }
        }
    }
    state->render_pass = [MTLRenderPassDescriptor renderPassDescriptor];
    if (!state->render_pass) {
        return false;
    }

    const uint8_t white[] = { 255, 255, 255, 255 };
    id<MTLTexture> white_texture = wm_make_texture(state, 1, 1, white);
    if (!white_texture) {
        return false;
    }
    state->textures = [NSMutableArray arrayWithObject:white_texture];
    state->free_texture_handles = [NSMutableArray array];
    state->retired_texture_handles = [NSMutableIndexSet indexSet];
    return true;
}

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
            for (NSUInteger i = 0; i < WM_IN_FLIGHT_FRAMES; ++i) {
                [state->in_flight[i] waitUntilCompleted];
            }
            [state->window close];
            free(state->vertices);
            free(state->batches);
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

void wm_platform_begin(WmPlatform *platform, WmColor clear_color)
{
    WmMetalState *state = wm_state(platform);
    if (!state) {
        return;
    }
    state->clear_color = clear_color;
    state->render_target_handle = 0;
    state->vertex_count = 0;
    state->batch_count = 0;
    /* A failed presentation may leave queued draws behind. The new frame
     * discards those draws before their texture handles become reusable. */
    wm_release_retired_textures(state);
    state->clip_enabled = false;
    WmQuad background = {
        .x = 0, .y = 0,
        .width = WM_FRAME_WIDTH, .height = WM_FRAME_HEIGHT,
        .u0 = 0, .v0 = 0, .u1 = 1, .v1 = 1,
        .color = clear_color
    };
    wm_platform_draw_quad(platform, &background);
}

void wm_platform_set_clip(WmPlatform *platform, const WmClipRect *rect)
{
    WmMetalState *state = wm_state(platform);
    if (!state) return;
    state->clip_enabled = rect != NULL;
    if (rect) state->clip = *rect;
}

static bool wm_reserve_vertices(WmMetalState *state, size_t count)
{
    if (count <= state->vertex_capacity) {
        return true;
    }
    size_t capacity = state->vertex_capacity ? state->vertex_capacity : 768;
    while (capacity < count) {
        if (capacity > SIZE_MAX / 2) {
            return false;
        }
        capacity *= 2;
    }
    if (capacity > SIZE_MAX / sizeof(WmVertex)) {
        return false;
    }
    WmVertex *vertices = realloc(state->vertices, capacity * sizeof(WmVertex));
    if (!vertices) {
        return false;
    }
    state->vertices = vertices;
    state->vertex_capacity = capacity;
    return true;
}

static bool wm_reserve_batches(WmMetalState *state, size_t count)
{
    if (count <= state->batch_capacity) {
        return true;
    }
    size_t capacity = state->batch_capacity ? state->batch_capacity : 64;
    while (capacity < count) {
        if (capacity > SIZE_MAX / 2) {
            return false;
        }
        capacity *= 2;
    }
    if (capacity > SIZE_MAX / sizeof(WmBatch)) {
        return false;
    }
    WmBatch *batches = realloc(state->batches, capacity * sizeof(WmBatch));
    if (!batches) {
        return false;
    }
    state->batches = batches;
    state->batch_capacity = capacity;
    return true;
}

static WmVertex wm_vertex(float x, float y, float u, float v, WmColor color)
{
    WmVertex vertex = { .x = x, .y = y, .color = color };
    vertex.uv[0][0] = u;
    vertex.uv[0][1] = v;
    return vertex;
}

static uint32_t wm_resolve_texture(WmMetalState *state, uint32_t handle)
{
    if ((NSUInteger)handle >= state->textures.count ||
        [state->retired_texture_handles containsIndex:handle] ||
        [state->textures objectAtIndex:handle] == [NSNull null]) {
        return 0;
    }
    return handle;
}

static bool wm_batch_states_equal(const WmBatchState *left,
                                  const WmBatchState *right)
{
    if (left->kind != right->kind ||
        left->clip_enabled != right->clip_enabled ||
        (left->clip_enabled && memcmp(&left->clip, &right->clip,
                                     sizeof(left->clip)) != 0) ||
        memcmp(left->textures, right->textures, sizeof(left->textures)) != 0 ||
        memcmp(left->wrap_s, right->wrap_s, sizeof(left->wrap_s)) != 0 ||
        memcmp(left->wrap_t, right->wrap_t, sizeof(left->wrap_t)) != 0 ||
        left->blend_key != right->blend_key) {
        return false;
    }
    if (left->kind == WM_BATCH_MATERIAL) {
        return memcmp(&left->params.simple, &right->params.simple,
                      sizeof(left->params.simple)) == 0;
    }
    if (left->kind == WM_BATCH_TEV) {
        return memcmp(&left->params.tev, &right->params.tev,
                      sizeof(left->params.tev)) == 0;
    }
    return true;
}

static void wm_queue_quad(WmMetalState *state, const WmBatchState *batch_state,
                          const WmVertex corners[4])
{
    if (state->vertex_count > SIZE_MAX - WM_VERTICES_PER_QUAD) {
        return;
    }
    WmBatchState clipped_state = *batch_state;
    clipped_state.clip_enabled = state->clip_enabled;
    clipped_state.clip = state->clip;
    bool new_batch = state->batch_count == 0 ||
        !wm_batch_states_equal(&state->batches[state->batch_count - 1].state,
                               &clipped_state);
    if (!wm_reserve_vertices(state, state->vertex_count + WM_VERTICES_PER_QUAD) ||
        (new_batch && !wm_reserve_batches(state, state->batch_count + 1))) {
        return;
    }

    WmVertex *vertices = state->vertices + state->vertex_count;
    for (size_t index = 0; index < WM_VERTICES_PER_QUAD; ++index) {
        vertices[index] = corners[wm_quad_triangle_order[index]];
    }
    if (new_batch) {
        state->batches[state->batch_count++] = (WmBatch){
            .state = clipped_state,
            .first_vertex = state->vertex_count,
            .vertex_count = WM_VERTICES_PER_QUAD,
        };
    } else {
        state->batches[state->batch_count - 1].vertex_count += WM_VERTICES_PER_QUAD;
    }
    state->vertex_count += WM_VERTICES_PER_QUAD;
}

void wm_platform_draw_quad(WmPlatform *platform, const WmQuad *quad)
{
    if (!quad || quad->width <= 0 || quad->height <= 0) {
        return;
    }

    WmDrawVertex corners[WM_QUAD_CORNERS];
    wm_render_quad_corners(quad, corners);
    wm_platform_draw_vertices(platform, corners, quad->texture);
}

void wm_platform_draw_vertices(WmPlatform *platform,
                               const WmDrawVertex corners[4], uint32_t texture_handle)
{
    WmMetalState *state = wm_state(platform);
    if (!state || !corners) {
        return;
    }

    WmVertex vertices[4];
    for (unsigned index = 0; index < 4; ++index) {
        const WmDrawVertex *corner = &corners[index];
        vertices[index] = wm_vertex(corner->x, corner->y, corner->u,
                                    corner->v, corner->color);
    }
    WmBatchState batch = {0};
    batch.kind = WM_BATCH_BASIC;
    batch.textures[0] = wm_resolve_texture(state, texture_handle);
    wm_queue_quad(state, &batch, vertices);
}

void wm_platform_draw_material_quad(WmPlatform *platform,
                                    const WmMaterialQuad *quad)
{
    WmMetalState *state = wm_state(platform);
    if (!state || !quad) {
        return;
    }

    if (quad->tev_stage_count > 6 && !state->warned_tev_limit) {
        fprintf(stderr, "Metal: materials with over six TEV stages use the "
                        "simple material fallback.\n");
        state->warned_tev_limit = true;
    }

    WmVertex vertices[4] = {0};
    for (unsigned index = 0; index < 4; ++index) {
        vertices[index].x = quad->vertices[index].x;
        vertices[index].y = quad->vertices[index].y;
        vertices[index].color = quad->vertices[index].color;
        memcpy(vertices[index].uv, quad->vertices[index].uv,
               sizeof(vertices[index].uv));
    }

    WmBatchState batch = {0};
    batch.kind = quad->tev_stage_count > 0 && quad->tev_stage_count <= 6
        ? WM_BATCH_TEV : WM_BATCH_MATERIAL;
    for (unsigned index = 0; index < WM_MATERIAL_TEXTURES; ++index) {
        batch.textures[index] = wm_resolve_texture(state, quad->textures[index]);
        batch.wrap_s[index] = quad->wrap_s[index] < 3 ? quad->wrap_s[index] : 0;
        batch.wrap_t[index] = quad->wrap_t[index] < 3 ? quad->wrap_t[index] : 0;
    }
    batch.blend_key = WM_BLEND_DEFAULT;
    if (quad->has_blend_mode) {
        if (quad->blend_mode[0] == 0) {
            batch.blend_key = WM_BLEND_DISABLED;
        } else {
            uint8_t source = quad->blend_mode[1] < 8 ? quad->blend_mode[1] : 4;
            uint8_t destination = quad->blend_mode[2] < 8 ? quad->blend_mode[2] : 5;
            batch.blend_key = source * 8 + destination;
        }
    }
    uint32_t comparisons = quad->has_alpha_compare
        ? quad->alpha_compare[0] : 0x77u;
    uint32_t operation = quad->has_alpha_compare
        ? quad->alpha_compare[1] : 0;
    uint32_t references = quad->has_alpha_compare
        ? ((uint32_t)quad->alpha_compare[2] |
           ((uint32_t)quad->alpha_compare[3] << 8)) : 0;
    if (batch.kind == WM_BATCH_TEV) {
        memcpy(batch.params.tev.registers, quad->registers,
               sizeof(batch.params.tev.registers));
        memcpy(batch.params.tev.konst_colors, quad->konst_colors,
               sizeof(batch.params.tev.konst_colors));
        for (unsigned stage = 0; stage < quad->tev_stage_count; ++stage) {
            for (unsigned word = 0; word < 4; ++word) {
                uint32_t packed = 0;
                for (unsigned byte = 0; byte < 4; ++byte) {
                    packed |= (uint32_t)quad->tev_stages[stage][word * 4 + byte]
                              << (byte * 8);
                }
                batch.params.tev.stage_words[stage][word] = packed;
            }
        }
        for (unsigned index = 0; index < 4; ++index) {
            batch.params.tev.swap[index] = quad->tev_swap_table[index];
        }
        batch.params.tev.stage_count = quad->tev_stage_count;
        batch.params.tev.alpha_comparisons = comparisons;
        batch.params.tev.alpha_operation = operation;
        batch.params.tev.alpha_references = references;
    } else {
        memcpy(batch.params.simple.r0, quad->registers[0],
               sizeof(batch.params.simple.r0));
        memcpy(batch.params.simple.r1, quad->registers[1],
               sizeof(batch.params.simple.r1));
        memcpy(batch.params.simple.kc3, quad->konst_colors[3],
               sizeof(batch.params.simple.kc3));
        batch.params.simple.texture_count = quad->texture_count < 2
            ? quad->texture_count : 2;
        batch.params.simple.alpha_comparisons = comparisons;
        batch.params.simple.alpha_operation = operation;
        batch.params.simple.alpha_references = references;
    }
    wm_queue_quad(state, &batch, vertices);
}

void wm_platform_prepare_material(WmPlatform *platform,
                                  const WmMaterialQuad *quad)
{
    /* Both material fragment functions and stock blend pipelines are ready
     * when the Metal platform is created. */
    (void)platform;
    (void)quad;
}

static bool wm_upload_vertices(WmMetalState *state, NSUInteger slot)
{
    if (state->vertex_count == 0) {
        return true;
    }
    if (state->vertex_count > SIZE_MAX / sizeof(WmVertex)) {
        return false;
    }

    size_t length = state->vertex_count * sizeof(WmVertex);
    if (length > NSUIntegerMax) {
        return false;
    }
    if (state->buffer_sizes[slot] < length) {
        NSUInteger capacity = state->buffer_sizes[slot] ? state->buffer_sizes[slot] : 65536;
        while (capacity < length) {
            if (capacity > NSUIntegerMax / 2) {
                capacity = (NSUInteger)length;
                break;
            }
            capacity *= 2;
        }
        id<MTLBuffer> buffer =
            [state->device newBufferWithLength:capacity options:MTLResourceStorageModeShared];
        if (!buffer) {
            return false;
        }
        state->vertex_buffers[slot] = buffer;
        state->buffer_sizes[slot] = capacity;
    }

    memcpy(state->vertex_buffers[slot].contents, state->vertices, length);
    return true;
}

static NSUInteger wm_scissor_coordinate(double value, NSUInteger limit)
{
    if (value <= 0.0) return 0;
    if (value >= (double)limit) return limit;
    return (NSUInteger)value;
}

static MTLScissorRect wm_batch_scissor(const WmBatchState *batch,
                                      WmViewport viewport)
{
    if (!batch->clip_enabled) return (MTLScissorRect){
        (NSUInteger)viewport.x, (NSUInteger)viewport.y,
        (NSUInteger)viewport.width, (NSUInteger)viewport.height};
    double scale_x = (double)viewport.width / WM_FRAME_WIDTH;
    double scale_y = (double)viewport.height / WM_FRAME_HEIGHT;
    double left = floor((double)batch->clip.x * scale_x);
    double top = floor((double)batch->clip.y * scale_y);
    double right = ceil((double)(batch->clip.x + batch->clip.width) * scale_x);
    double bottom = ceil((double)(batch->clip.y + batch->clip.height) * scale_y);
    NSUInteger x0 = wm_scissor_coordinate(left, (NSUInteger)viewport.width);
    NSUInteger y0 = wm_scissor_coordinate(top, (NSUInteger)viewport.height);
    NSUInteger x1 = wm_scissor_coordinate(right, (NSUInteger)viewport.width);
    NSUInteger y1 = wm_scissor_coordinate(bottom, (NSUInteger)viewport.height);
    if (x1 < x0) x1 = x0;
    if (y1 < y0) y1 = y0;
    return (MTLScissorRect){x0 + (NSUInteger)viewport.x,
                            y0 + (NSUInteger)viewport.y, x1 - x0, y1 - y0};
}

void wm_platform_end(WmPlatform *platform)
{
    WmMetalState *state = wm_state(platform);
    if (!state || !state->window.isVisible) {
        return;
    }

    if (!state->render_target_handle && platform->fade_alpha > 0.0f) {
        wm_platform_set_clip(platform, NULL);
        WmQuad cover = {
            .x = 0, .y = 0, .width = WM_FRAME_WIDTH,
            .height = WM_FRAME_HEIGHT,
            .u0 = 0, .v0 = 0, .u1 = 1, .v1 = 1,
            .color = {0, 0, 0, platform->fade_alpha}, .texture = 0
        };
        wm_platform_draw_quad(platform, &cover);
    }

    @autoreleasepool {
        uint32_t target_handle = state->render_target_handle;
        state->render_target_handle = 0;
        id<MTLTexture> target = nil;
        if (target_handle > 0 && (NSUInteger)target_handle < state->textures.count) {
            id entry = [state->textures objectAtIndex:target_handle];
            if (entry != [NSNull null]) target = entry;
        }
        NSSize size = [state->view convertSizeToBacking:state->view.bounds.size];
        NSUInteger drawable_width = target ? target.width :
            (NSUInteger)llround(size.width);
        NSUInteger drawable_height = target ? target.height :
            (NSUInteger)llround(size.height);
        if (drawable_width == 0 || drawable_height == 0) {
            return;
        }
        WmViewport content = target
            ? (WmViewport){0, 0, (int)drawable_width, (int)drawable_height}
            : wm_viewport_fit((int)drawable_width, (int)drawable_height);
        if (!target) {
            state->layer.contentsScale = state->window.backingScaleFactor;
            CGSize drawable_size = CGSizeMake(drawable_width, drawable_height);
            if (!CGSizeEqualToSize(state->layer.drawableSize, drawable_size)) {
                state->layer.drawableSize = drawable_size;
            }
        }

        NSUInteger slot = state->frame_number % WM_IN_FLIGHT_FRAMES;
        [state->in_flight[slot] waitUntilCompleted];
        state->in_flight[slot] = nil;
        if (!wm_upload_vertices(state, slot)) {
            fprintf(stderr, "Metal vertex buffer allocation failed.\n");
            return;
        }

        id<CAMetalDrawable> drawable = target ? nil : [state->layer nextDrawable];
        if (!target && !drawable) {
            return;
        }
        id<MTLCommandBuffer> commands = [state->command_queue commandBuffer];
        if (!commands) {
            return;
        }

        MTLRenderPassColorAttachmentDescriptor *color = state->render_pass.colorAttachments[0];
        color.texture = target ? target : drawable.texture;
        color.loadAction = MTLLoadActionClear;
        color.storeAction = MTLStoreActionStore;
        color.clearColor = target
            ? MTLClearColorMake(state->clear_color.r, state->clear_color.g,
                                state->clear_color.b, state->clear_color.a)
            : MTLClearColorMake(0, 0, 0, 1);
        id<MTLRenderCommandEncoder> encoder =
            [commands renderCommandEncoderWithDescriptor:state->render_pass];
        color.texture = nil;
        if (!encoder) {
            return;
        }

        /* The logical raster is anamorphic inside a centered 16:9 output. */
        MTLViewport viewport = { content.x, content.y,
                                 content.width, content.height, 0.0, 1.0 };
        MTLScissorRect scissor = {
            (NSUInteger)content.x, (NSUInteger)content.y,
            (NSUInteger)content.width, (NSUInteger)content.height
        };
        [encoder setViewport:viewport];
        [encoder setScissorRect:scissor];
        if (state->vertex_count > 0) {
            [encoder setVertexBuffer:state->vertex_buffers[slot] offset:0 atIndex:0];
            id<MTLRenderPipelineState> bound_pipeline = nil;
            for (size_t i = 0; i < state->batch_count; ++i) {
                const WmBatch *batch = &state->batches[i];
                MTLScissorRect batch_scissor = wm_batch_scissor(
                    &batch->state, content);
                if (batch_scissor.width == 0 || batch_scissor.height == 0) {
                    continue;
                }
                if (batch_scissor.x != scissor.x ||
                    batch_scissor.y != scissor.y ||
                    batch_scissor.width != scissor.width ||
                    batch_scissor.height != scissor.height) {
                    [encoder setScissorRect:batch_scissor];
                    scissor = batch_scissor;
                }
                WmBatchKind kind = batch->state.kind;
                id<MTLRenderPipelineState> pipeline = kind != WM_BATCH_BASIC
                    ? wm_material_pipeline(state, kind, batch->state.blend_key)
                    : state->pipeline;
                if (!pipeline) {
                    continue;
                }
                if (pipeline != bound_pipeline) {
                    [encoder setRenderPipelineState:pipeline];
                    bound_pipeline = pipeline;
                }
                unsigned texture_count = kind == WM_BATCH_TEV ? 4u :
                    kind == WM_BATCH_MATERIAL ? 2u : 1u;
                for (unsigned slot_index = 0; slot_index < texture_count;
                     ++slot_index) {
                    uint32_t handle = batch->state.textures[slot_index];
                    id entry = [state->textures objectAtIndex:handle];
                    id<MTLTexture> texture = entry == [NSNull null]
                        ? [state->textures objectAtIndex:0] : entry;
                    [encoder setFragmentTexture:texture atIndex:slot_index];
                    [encoder setFragmentSamplerState:
                        state->samplers[batch->state.wrap_s[slot_index]]
                                       [batch->state.wrap_t[slot_index]]
                                             atIndex:slot_index];
                }
                if (kind == WM_BATCH_MATERIAL) {
                    [encoder setFragmentBytes:&batch->state.params.simple
                                      length:sizeof(batch->state.params.simple)
                                     atIndex:0];
                } else if (kind == WM_BATCH_TEV) {
                    [encoder setFragmentBytes:&batch->state.params.tev
                                      length:sizeof(batch->state.params.tev)
                                     atIndex:0];
                }
                [encoder drawPrimitives:MTLPrimitiveTypeTriangle
                           vertexStart:batch->first_vertex
                           vertexCount:batch->vertex_count];
            }
        }
        [encoder endEncoding];
        if (drawable) [commands presentDrawable:drawable];
        [commands commit];
        wm_release_retired_textures(state);
        state->in_flight[slot] = commands;
        state->frame_number++;
    }
}

void wm_platform_set_fade_alpha(WmPlatform *platform, float alpha)
{
    if (platform) platform->fade_alpha = fminf(1.0f, fmaxf(0.0f, alpha));
}

uint32_t wm_platform_create_render_texture(WmPlatform *platform)
{
    WmMetalState *state = wm_state(platform);
    if (!state || (state->free_texture_handles.count == 0 &&
                   state->textures.count >= UINT32_MAX)) return 0;
    MTLTextureDescriptor *descriptor = [MTLTextureDescriptor
        texture2DDescriptorWithPixelFormat:MTLPixelFormatBGRA8Unorm
                                    width:WM_FRAME_WIDTH
                                   height:WM_FRAME_HEIGHT
                                mipmapped:NO];
    descriptor.usage = MTLTextureUsageRenderTarget | MTLTextureUsageShaderRead;
    descriptor.storageMode = MTLStorageModePrivate;
    id<MTLTexture> texture = [state->device newTextureWithDescriptor:descriptor];
    if (!texture) return 0;
    uint32_t handle;
    if (state->free_texture_handles.count > 0) {
        NSNumber *available = state->free_texture_handles.lastObject;
        handle = available.unsignedIntValue;
        [state->free_texture_handles removeLastObject];
        [state->textures replaceObjectAtIndex:handle withObject:texture];
    } else {
        handle = (uint32_t)state->textures.count;
        [state->textures addObject:texture];
    }
    return handle;
}

bool wm_platform_begin_target(WmPlatform *platform, uint32_t texture,
                              WmColor clear_color)
{
    WmMetalState *state = wm_state(platform);
    if (!state || texture == 0 || (NSUInteger)texture >= state->textures.count) {
        return false;
    }
    if ([state->retired_texture_handles containsIndex:texture]) {
        return false;
    }
    id entry = [state->textures objectAtIndex:texture];
    if (entry == [NSNull null] ||
        !([(id<MTLTexture>)entry usage] & MTLTextureUsageRenderTarget)) {
        return false;
    }
    wm_platform_begin(platform, clear_color);
    state->vertex_count = 0;
    state->batch_count = 0;
    state->render_target_handle = texture;
    return true;
}

uint32_t wm_platform_create_texture(WmPlatform *platform, int width, int height,
                                    const uint8_t *rgba)
{
    WmMetalState *state = wm_state(platform);
    if (!state || (state->free_texture_handles.count == 0 &&
                   state->textures.count >= UINT32_MAX)) {
        return 0;
    }
    @autoreleasepool {
        id<MTLTexture> texture = wm_make_texture(state, width, height, rgba);
        if (!texture) {
            return 0;
        }
        uint32_t handle;
        if (state->free_texture_handles.count > 0) {
            NSNumber *available = state->free_texture_handles.lastObject;
            handle = available.unsignedIntValue;
            [state->free_texture_handles removeLastObject];
            [state->textures replaceObjectAtIndex:handle withObject:texture];
        } else {
            handle = (uint32_t)state->textures.count;
            [state->textures addObject:texture];
        }
        return handle;
    }
}

void wm_platform_destroy_texture(WmPlatform *platform, uint32_t texture)
{
    WmMetalState *state = wm_state(platform);
    if (!state || texture == 0 || (NSUInteger)texture >= state->textures.count) {
        return;
    }
    if ([state->retired_texture_handles containsIndex:texture] ||
        [state->textures objectAtIndex:texture] == [NSNull null]) {
        return;
    }
    if (state->render_target_handle == texture) state->render_target_handle = 0;
    /* Batches keep numeric handles until wm_platform_end encodes them. Keep
     * this object in its slot so earlier draws still use the original image. */
    [state->retired_texture_handles addIndex:texture];
}
