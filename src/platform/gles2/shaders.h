#ifndef WII_MENU_GLES2_SHADERS_H
#define WII_MENU_GLES2_SHADERS_H

#include "wii_menu/platform/platform.h"

#include <GLES2/gl2.h>

#include <stdbool.h>
#include <stdint.h>

enum { WM_ES2_TEV_STAGES = 6 };

typedef struct WmTevKey {
    uint8_t stage_count;
    uint8_t stages[WM_ES2_TEV_STAGES][16];
    uint8_t swap[4];
    uint8_t wrap_s[WM_MATERIAL_TEXTURES];
    uint8_t wrap_t[WM_MATERIAL_TEXTURES];
    uint8_t has_alpha_compare;
    uint8_t alpha_compare[4];
} WmTevKey;

typedef struct WmTevProgram {
    WmTevKey key;
    GLuint program;
    GLint registers_location;
    GLint konst_location;
    struct WmTevProgram *next;
} WmTevProgram;

GLuint wm_gles2_create_quad_program(void);
GLuint wm_gles2_create_material_program(void);
GLuint wm_gles2_create_tev_vertex_shader(bool fragment_highp);

/* The caller owns the cache and releases its programs before the GL context.
 * Failed entries remain cached to avoid recompiling during later draws. */
WmTevProgram *wm_gles2_get_tev_program(WmTevProgram **programs, GLuint vertex_shader,
                                       bool fragment_highp, const WmMaterialQuad *quad);

#endif
