#ifndef WM_PLATFORM_METAL_INTERNAL_H
#define WM_PLATFORM_METAL_INTERNAL_H

#import <AppKit/AppKit.h>
#import <Metal/Metal.h>
#import <QuartzCore/CAMetalLayer.h>

#include "wii_menu/platform/platform.h"

#include <stdbool.h>
#include <stdint.h>

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
 * preserving the raw BRLYT encoding. */
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
    bool warned_tev_encoding;
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

/* GPU initialization precedes window creation. The window adapter retains
 * WmMetalState through platform->metal_state until wm_platform_destroy. */
bool wm_prepare_metal(WmMetalState *state);
void wm_wait_for_metal(WmMetalState *state);
void wm_release_metal(WmMetalState *state);

#endif
