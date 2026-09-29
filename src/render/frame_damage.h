#ifndef WII_MENU_FRAME_DAMAGE_H
#define WII_MENU_FRAME_DAMAGE_H

#include "wii_menu/platform/platform.h"
#include "wii_menu/render/viewport.h"

#include <stddef.h>

enum { WM_FRAME_COMMAND_CAPACITY = 2048 };

typedef enum WmFrameCommandKind {
    WM_FRAME_COMMAND_QUAD,
    WM_FRAME_COMMAND_MATERIAL
} WmFrameCommandKind;

typedef struct WmFrameCommand {
    WmFrameCommandKind kind;
    WmClipRect clip;
    WmClipRect bounds;
    uint32_t texture;
    union {
        WmDrawVertex vertices[4];
        WmMaterialQuad material;
    } draw;
} WmFrameCommand;

typedef struct WmFrameDamage WmFrameDamage;

/* Fixed-capacity storage is allocated once. Commands own copied draw data;
 * textures remain owned by the caller until recorded draws have completed. */
WmFrameDamage *wm_frame_damage_create(void);
void wm_frame_damage_destroy(WmFrameDamage *damage);
void wm_frame_damage_begin(WmFrameDamage *damage, int width, int height, WmColor clear);
bool wm_frame_damage_quad(WmFrameDamage *damage, const WmDrawVertex vertices[4],
                          uint32_t texture, const WmClipRect *clip);
bool wm_frame_damage_material(WmFrameDamage *damage, const WmMaterialQuad *quad,
                              const WmClipRect *clip);
const WmFrameCommand *wm_frame_damage_commands(const WmFrameDamage *damage,
                                               size_t *count);
/* Disjoint, top-left framebuffer rectangles. Replaying every intersecting
 * command in order after clearing each region preserves alpha composition. */
const WmViewport *wm_frame_damage_regions(WmFrameDamage *damage, size_t *count);
bool wm_frame_command_intersects(const WmFrameCommand *command, WmViewport region,
                                 int width, int height);
void wm_frame_damage_commit(WmFrameDamage *damage);
void wm_frame_damage_invalidate(WmFrameDamage *damage);

#endif
