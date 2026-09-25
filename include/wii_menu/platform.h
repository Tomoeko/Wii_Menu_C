#ifndef WII_MENU_PLATFORM_H
#define WII_MENU_PLATFORM_H

#include <stdbool.h>
#include <stdint.h>

/* Coordinates are in the 640 x 456 logical framebuffer, with the origin at
 * the upper left. A backend scales that framebuffer to its drawable. */
#define WM_FRAME_WIDTH 640
#define WM_FRAME_HEIGHT 456

typedef struct WmPlatform WmPlatform;

typedef enum WmEventType {
    WM_EVENT_NONE,
    WM_EVENT_QUIT,
    WM_EVENT_POINTER_DOWN,
    WM_EVENT_POINTER_UP,
    WM_EVENT_POINTER_MOVE,
    WM_EVENT_POINTER_LEAVE,
    WM_EVENT_KEY_DOWN
} WmEventType;

typedef enum WmKey {
    WM_KEY_UNKNOWN = 0,
    WM_KEY_LEFT = 256,
    WM_KEY_RIGHT,
    WM_KEY_UP,
    WM_KEY_DOWN,
    WM_KEY_ENTER,
    WM_KEY_ESCAPE,
    WM_KEY_BACKSPACE,
    WM_KEY_HOME
} WmKey;

typedef enum WmPointerButton {
    WM_POINTER_LEFT = 1,
    WM_POINTER_MIDDLE = 2,
    WM_POINTER_RIGHT = 3
} WmPointerButton;

typedef struct WmEvent {
    WmEventType type;
    int x;
    int y;
    WmKey key;
    WmPointerButton button;
    /* Pointer coordinates remain valid outside the fitted 16:9 picture.
     * Scene input decides whether an active drag consumes that event. */
    bool outside_viewport;
    /* Focus loss cancels a held action; pointer departure may finish it. */
    bool cancel_capture;
} WmEvent;

typedef struct WmColor {
    float r;
    float g;
    float b;
    float a;
} WmColor;

typedef struct WmClipRect {
    float x;
    float y;
    float width;
    float height;
} WmClipRect;

typedef struct WmQuad {
    float x;
    float y;
    float width;
    float height;
    float u0;
    float v0;
    float u1;
    float v1;
    WmColor color;
    uint32_t texture; /* Zero selects the backend's white texture. */
} WmQuad;

/* A transformed textured quad, ordered left-top, right-top, left-bottom,
 * right-bottom. The two triangles are (0,1,3) and (0,3,2). */
typedef struct WmDrawVertex {
    float x;
    float y;
    float u;
    float v;
    WmColor color;
} WmDrawVertex;

enum { WM_MATERIAL_TEXTURES = 4, WM_MATERIAL_TEV_STAGES = 16 };

typedef struct WmMaterialVertex {
    float x;
    float y;
    WmColor color;
    float uv[WM_MATERIAL_TEXTURES][2];
} WmMaterialVertex;

typedef struct WmMaterialQuad {
    WmMaterialVertex vertices[4];
    uint32_t textures[WM_MATERIAL_TEXTURES];
    uint8_t wrap_s[WM_MATERIAL_TEXTURES];
    uint8_t wrap_t[WM_MATERIAL_TEXTURES];
    unsigned texture_count;
    float registers[3][4];
    float konst_colors[4][4];
    uint8_t tev_stages[WM_MATERIAL_TEV_STAGES][16];
    unsigned tev_stage_count;
    uint8_t tev_swap_table[4];
    uint8_t alpha_compare[4];
    uint8_t blend_mode[4];
    bool has_alpha_compare;
    bool has_blend_mode;
} WmMaterialQuad;

/* Each platform backend implements this API and owns its window and GPU
 * resources. Exactly one backend is linked into an executable. */
WmPlatform *wm_platform_create(const char *title, int window_width, int window_height);
void wm_platform_destroy(WmPlatform *platform);
bool wm_platform_poll(WmPlatform *platform, WmEvent *event);
void wm_platform_begin(WmPlatform *platform, WmColor clear_color);
/* Clip subsequent draws to a logical framebuffer rectangle. NULL disables
 * clipping. Both backends preserve draw order across clip changes. */
void wm_platform_set_clip(WmPlatform *platform, const WmClipRect *rect);
void wm_platform_draw_quad(WmPlatform *platform, const WmQuad *quad);
void wm_platform_draw_vertices(WmPlatform *platform,
                               const WmDrawVertex vertices[4], uint32_t texture);
void wm_platform_draw_material_quad(WmPlatform *platform,
                                    const WmMaterialQuad *quad);
/* Prepare a material's shader while layouts load. Call after platform creation
 * on its rendering thread. GLES2 caches supported TEV programs (up to six
 * stages); Metal already has its shader ready and treats this as a no-op. */
void wm_platform_prepare_material(WmPlatform *platform,
                                  const WmMaterialQuad *quad);
/* Apply the source scene fader after the scene's last draw, before presenting.
 * This is ignored for offscreen preview captures. */
void wm_platform_set_fade_alpha(WmPlatform *platform, float alpha);
void wm_platform_end(WmPlatform *platform);
uint32_t wm_platform_create_texture(WmPlatform *platform, int width, int height,
                                    const uint8_t *rgba);
/* A reusable 640 x 456 render target for captured scene composition. The
 * returned handle can be drawn like any other texture. At most one capture
 * target is active per backend instance. */
uint32_t wm_platform_create_render_texture(WmPlatform *platform);
bool wm_platform_begin_target(WmPlatform *platform, uint32_t texture,
                              WmColor clear_color);
void wm_platform_destroy_texture(WmPlatform *platform, uint32_t texture);

#endif
