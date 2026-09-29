#include "wii_menu/platform/platform.h"
#include "wii_menu/render/viewport.h"
#include "geometry.h"
#include "host.h"
#include "material_blend.h"
#include "shaders.h"
#include "retained_frame.h"

#include <GLES2/gl2.h>

#include <stddef.h>
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

enum { WM_BATCH_QUADS = 1024 };

typedef struct WmVertex {
    float x;
    float y;
    float u;
    float v;
    float r;
    float g;
    float b;
    float a;
} WmVertex;

typedef struct WmMaterialGpuVertex {
    float x;
    float y;
    float color[4];
    float uv[WM_MATERIAL_TEXTURES][2];
} WmMaterialGpuVertex;

struct WmPlatform {
    WmGles2Host *host;
    int framebuffer_width;
    int framebuffer_height;
    WmViewport presentation;
    bool scissor_enabled;
    int scissor_x;
    int scissor_y;
    int scissor_width;
    int scissor_height;

    GLuint program;
    GLuint material_program;
    GLuint tev_vertex_shader;
    WmTevProgram *tev_programs;
    bool fragment_highp;
    bool warned_tev_limit;
    bool warned_tev_precision;
    bool warned_tev_encoding;
    GLuint vertex_buffer;
    GLuint white_texture;
    GLuint render_texture;
    GLuint render_framebuffer;
    bool rendering_target;
    WmGles2RetainedFrame retained;
    GLint projection_location;
    GLint texture_location;
    GLint material_frame_location;
    GLint material_texture_count_location;
    GLint material_registers_location;
    GLint material_konst_location;
    GLint material_alpha_location;
    GLint material_wrap_locations[2];

    WmVertex vertices[WM_BATCH_QUADS * WM_VERTICES_PER_QUAD];
    size_t quad_count;
    GLuint batch_texture;
    float fade_alpha;
};

static GLuint wm_upload_texture(int width, int height, const uint8_t *rgba) {
    GLuint texture = 0;
    glGenTextures(1, &texture);
    if (texture == 0) {
        return 0;
    }

    glBindTexture(GL_TEXTURE_2D, texture);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    /* CLAMP_TO_EDGE and no mipmaps keep arbitrary Wii texture sizes valid on ES2. */
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, width, height, 0, GL_RGBA, GL_UNSIGNED_BYTE,
                 rgba);
    if (glGetError() != GL_NO_ERROR) {
        glDeleteTextures(1, &texture);
        return 0;
    }
    return texture;
}

static void wm_flush(WmPlatform *platform) {
    if (platform->quad_count == 0) {
        return;
    }

    GLsizeiptr byte_count =
        (GLsizeiptr)(platform->quad_count * WM_VERTICES_PER_QUAD * sizeof(WmVertex));
    glUseProgram(platform->program);
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, platform->batch_texture);
    glBindBuffer(GL_ARRAY_BUFFER, platform->vertex_buffer);
    /* Replacing storage avoids waiting for a previous draw using this buffer. */
    glBufferData(GL_ARRAY_BUFFER, (GLsizeiptr)sizeof(platform->vertices), NULL,
                 GL_STREAM_DRAW);
    glBufferSubData(GL_ARRAY_BUFFER, 0, byte_count, platform->vertices);
    glEnableVertexAttribArray(0);
    glEnableVertexAttribArray(1);
    glEnableVertexAttribArray(2);
    glDisableVertexAttribArray(3);
    glDisableVertexAttribArray(4);
    glDisableVertexAttribArray(5);
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, (GLsizei)sizeof(WmVertex),
                          (const void *)offsetof(WmVertex, x));
    glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, (GLsizei)sizeof(WmVertex),
                          (const void *)offsetof(WmVertex, u));
    glVertexAttribPointer(2, 4, GL_FLOAT, GL_FALSE, (GLsizei)sizeof(WmVertex),
                          (const void *)offsetof(WmVertex, r));
    glEnable(GL_BLEND);
    glBlendFuncSeparate(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA, GL_ONE,
                        GL_ONE_MINUS_SRC_ALPHA);
    glDrawArrays(GL_TRIANGLES, 0,
                 (GLsizei)(platform->quad_count * WM_VERTICES_PER_QUAD));
    platform->quad_count = 0;
}

static bool wm_initialize_graphics(WmPlatform *platform) {
    GLint range[2] = {0, 0};
    GLint precision = 0;
    glGetShaderPrecisionFormat(GL_FRAGMENT_SHADER, GL_HIGH_FLOAT, range, &precision);
    platform->fragment_highp = precision > 0;
    platform->program = wm_gles2_create_quad_program();
    if (platform->program == 0) {
        return false;
    }
    platform->material_program = wm_gles2_create_material_program();
    if (!platform->material_program)
        return false;
    platform->tev_vertex_shader =
        wm_gles2_create_tev_vertex_shader(platform->fragment_highp);
    if (!platform->tev_vertex_shader)
        return false;

    glUseProgram(platform->material_program);
    platform->material_frame_location =
        glGetUniformLocation(platform->material_program, "u_frame_size");
    platform->material_texture_count_location =
        glGetUniformLocation(platform->material_program, "u_texture_count");
    platform->material_registers_location =
        glGetUniformLocation(platform->material_program, "u_registers");
    platform->material_konst_location =
        glGetUniformLocation(platform->material_program, "u_konst");
    platform->material_alpha_location =
        glGetUniformLocation(platform->material_program, "u_alpha_compare");
    platform->material_wrap_locations[0] =
        glGetUniformLocation(platform->material_program, "u_wrap0");
    platform->material_wrap_locations[1] =
        glGetUniformLocation(platform->material_program, "u_wrap1");
    glUniform1i(glGetUniformLocation(platform->material_program, "u_texture0"), 0);
    glUniform1i(glGetUniformLocation(platform->material_program, "u_texture1"), 1);
    glUniform2f(platform->material_frame_location, (float)WM_FRAME_WIDTH,
                (float)WM_FRAME_HEIGHT);

    platform->projection_location =
        glGetUniformLocation(platform->program, "u_frame_size");
    platform->texture_location = glGetUniformLocation(platform->program, "u_texture");
    if (platform->projection_location < 0 || platform->texture_location < 0) {
        fprintf(stderr, "GLES2: required shader uniforms are unavailable.\n");
        return false;
    }

    glUseProgram(platform->program);
    glUniform2f(platform->projection_location, (float)WM_FRAME_WIDTH,
                (float)WM_FRAME_HEIGHT);
    glUniform1i(platform->texture_location, 0);

    glGenBuffers(1, &platform->vertex_buffer);
    if (platform->vertex_buffer == 0) {
        fprintf(stderr, "GLES2: could not allocate the quad buffer.\n");
        return false;
    }
    glBindBuffer(GL_ARRAY_BUFFER, platform->vertex_buffer);
    glBufferData(GL_ARRAY_BUFFER, (GLsizeiptr)sizeof(platform->vertices), NULL,
                 GL_STREAM_DRAW);

    glEnableVertexAttribArray(0);
    glEnableVertexAttribArray(1);
    glEnableVertexAttribArray(2);
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, (GLsizei)sizeof(WmVertex),
                          (const void *)offsetof(WmVertex, x));
    glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, (GLsizei)sizeof(WmVertex),
                          (const void *)offsetof(WmVertex, u));
    glVertexAttribPointer(2, 4, GL_FLOAT, GL_FALSE, (GLsizei)sizeof(WmVertex),
                          (const void *)offsetof(WmVertex, r));

    static const uint8_t white_pixel[4] = {255, 255, 255, 255};
    platform->white_texture = wm_upload_texture(1, 1, white_pixel);
    if (platform->white_texture == 0) {
        fprintf(stderr, "GLES2: could not allocate the white texture.\n");
        return false;
    }

    glDisable(GL_DEPTH_TEST);
    glDisable(GL_CULL_FACE);
    glEnable(GL_BLEND);
    glBlendFuncSeparate(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA, GL_ONE,
                        GL_ONE_MINUS_SRC_ALPHA);
    wm_gles2_retained_initialize(&platform->retained);
    if (platform->retained.allowed &&
        !wm_gles2_host_preserve_back_buffer(platform->host)) {
        wm_gles2_retained_destroy(&platform->retained);
    }
    return glGetError() == GL_NO_ERROR;
}

WmPlatform *wm_platform_create(const char *title, int window_width, int window_height) {
    if (window_width <= 0 || window_height <= 0) {
        fprintf(stderr, "GLES2: window dimensions must be positive.\n");
        return NULL;
    }

    WmPlatform *platform = calloc(1, sizeof(*platform));
    if (platform == NULL) {
        return NULL;
    }
    platform->host = wm_gles2_host_create(title, window_width, window_height);
    if (platform->host == NULL) {
        wm_platform_destroy(platform);
        return NULL;
    }
    if (!wm_initialize_graphics(platform)) {
        fprintf(stderr, "GLES2: graphics initialization failed.\n");
        wm_platform_destroy(platform);
        return NULL;
    }
    wm_gles2_host_show(platform->host);
    return platform;
}

void wm_platform_destroy(WmPlatform *platform) {
    if (platform == NULL) {
        return;
    }

    if (wm_gles2_host_make_current(platform->host)) {
        if (platform->white_texture != 0) {
            glDeleteTextures(1, &platform->white_texture);
        }
        if (platform->render_framebuffer != 0) {
            glDeleteFramebuffers(1, &platform->render_framebuffer);
        }
        if (platform->render_texture != 0) {
            glDeleteTextures(1, &platform->render_texture);
        }
        if (platform->vertex_buffer != 0) {
            glDeleteBuffers(1, &platform->vertex_buffer);
        }
        if (platform->program != 0) {
            glDeleteProgram(platform->program);
        }
        if (platform->material_program != 0) {
            glDeleteProgram(platform->material_program);
        }
        for (WmTevProgram *entry = platform->tev_programs; entry; entry = entry->next) {
            if (entry->program)
                glDeleteProgram(entry->program);
        }
        if (platform->tev_vertex_shader) {
            glDeleteShader(platform->tev_vertex_shader);
        }
    }
    wm_gles2_host_destroy(platform->host);
    wm_gles2_retained_destroy(&platform->retained);
    while (platform->tev_programs) {
        WmTevProgram *next = platform->tev_programs->next;
        free(platform->tev_programs);
        platform->tev_programs = next;
    }
    free(platform);
}

bool wm_platform_poll(WmPlatform *platform, WmEvent *event) {
    return platform != NULL && wm_gles2_host_poll(platform->host, event);
}

void wm_platform_begin(WmPlatform *platform, WmColor clear_color) {
    if (platform == NULL) {
        return;
    }

    platform->quad_count = 0;
    platform->rendering_target = false;
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    int width = 0;
    int height = 0;
    wm_gles2_host_surface_size(platform->host, &width, &height);
    if (platform->framebuffer_width != width || platform->framebuffer_height != height)
        wm_frame_damage_invalidate(platform->retained.commands);
    platform->framebuffer_width = width;
    platform->framebuffer_height = height;
    platform->presentation = wm_viewport_fit(width, height);
    WmViewport content = platform->presentation;
    glDisable(GL_SCISSOR_TEST);
    platform->scissor_enabled = false;
    glViewport(content.x, height - content.y - content.height, content.width,
               content.height);
    if (wm_gles2_retained_begin(&platform->retained, content.width, content.height,
                                clear_color)) {
        /* Retain the content pixels in EGL's original back buffer. Clear only
         * the letterbox bars, keeping the original rasterization and precision. */
        glEnable(GL_SCISSOR_TEST);
        glClearColor(0, 0, 0, 1);
        const WmViewport bars[] = {
            {0, 0, width, content.y},
            {0, content.y + content.height, width, height - content.y - content.height},
            {0, content.y, content.x, content.height},
            {content.x + content.width, content.y, width - content.x - content.width,
             content.height}};
        for (size_t index = 0; index < sizeof(bars) / sizeof(bars[0]); index++) {
            WmViewport bar = bars[index];
            if (bar.width <= 0 || bar.height <= 0)
                continue;
            glScissor(bar.x, height - bar.y - bar.height, bar.width, bar.height);
            glClear(GL_COLOR_BUFFER_BIT);
        }
        glDisable(GL_SCISSOR_TEST);
        return;
    }
    glDisable(GL_SCISSOR_TEST);
    glClearColor(0, 0, 0, 1);
    glClear(GL_COLOR_BUFFER_BIT);
    glEnable(GL_SCISSOR_TEST);
    glScissor(content.x, height - content.y - content.height, content.width,
              content.height);
    glClearColor(clear_color.r, clear_color.g, clear_color.b, clear_color.a);
    glClear(GL_COLOR_BUFFER_BIT);
    glDisable(GL_SCISSOR_TEST);
    platform->scissor_enabled = false;
}

static int wm_clamp_clip_edge(float coordinate, float scale, int limit, bool upper) {
    float value = coordinate * scale;
    if (!isfinite(value) || value <= 0.0f)
        return 0;
    if (value >= (float)limit)
        return limit;
    return (int)(upper ? ceilf(value) : floorf(value));
}

void wm_platform_set_clip(WmPlatform *platform, const WmClipRect *rect) {
    if (!platform)
        return;
    WmGles2RetainedFrame *retained = &platform->retained;
    bool cached = retained->active && !platform->rendering_target;
    if (cached && retained->recording) {
        retained->clip =
            rect ? *rect : (WmClipRect){0, 0, WM_FRAME_WIDTH, WM_FRAME_HEIGHT};
        return;
    }
    bool region_active = cached && retained->region_active;
    if (!rect && !region_active) {
        if (!platform->scissor_enabled)
            return;
        wm_flush(platform);
        glDisable(GL_SCISSOR_TEST);
        platform->scissor_enabled = false;
        return;
    }
    WmClipRect full = {0, 0, WM_FRAME_WIDTH, WM_FRAME_HEIGHT};
    if (!rect)
        rect = &full;
    WmViewport content = platform->presentation;
    float scale_x = (float)content.width / WM_FRAME_WIDTH;
    float scale_y = (float)content.height / WM_FRAME_HEIGHT;
    int x0 = wm_clamp_clip_edge(rect->x, scale_x, content.width, false);
    int x1 = wm_clamp_clip_edge(rect->x + rect->width, scale_x, content.width, true);
    int y0 = wm_clamp_clip_edge(rect->y, scale_y, content.height, false);
    int y1 = wm_clamp_clip_edge(rect->y + rect->height, scale_y, content.height, true);
    if (x1 < x0)
        x1 = x0;
    if (y1 < y0)
        y1 = y0;
    if (region_active) {
        WmViewport region = retained->region;
        if (x0 < region.x)
            x0 = region.x;
        if (y0 < region.y)
            y0 = region.y;
        if (x1 > region.x + region.width)
            x1 = region.x + region.width;
        if (y1 > region.y + region.height)
            y1 = region.y + region.height;
        if (x1 < x0)
            x1 = x0;
        if (y1 < y0)
            y1 = y0;
    }
    int scissor_x = content.x + x0;
    int scissor_y = platform->framebuffer_height - content.y - y1;
    int scissor_width = x1 - x0;
    int scissor_height = y1 - y0;
    if (platform->scissor_enabled && platform->scissor_x == scissor_x &&
        platform->scissor_y == scissor_y && platform->scissor_width == scissor_width &&
        platform->scissor_height == scissor_height) {
        return;
    }
    wm_flush(platform);
    glEnable(GL_SCISSOR_TEST);
    glScissor(scissor_x, scissor_y, scissor_width, scissor_height);
    platform->scissor_enabled = true;
    platform->scissor_x = scissor_x;
    platform->scissor_y = scissor_y;
    platform->scissor_width = scissor_width;
    platform->scissor_height = scissor_height;
}

static WmVertex wm_vertex(float x, float y, float u, float v, WmColor color) {
    return (WmVertex){x, y, u, v, color.r, color.g, color.b, color.a};
}

void wm_platform_draw_quad(WmPlatform *platform, const WmQuad *quad) {
    if (platform == NULL || quad == NULL || quad->width == 0.0f ||
        quad->height == 0.0f) {
        return;
    }

    WmDrawVertex vertices[WM_QUAD_CORNERS];
    wm_render_quad_corners(quad, vertices);
    wm_platform_draw_vertices(platform, vertices, quad->texture);
}

void wm_platform_draw_vertices(WmPlatform *platform, const WmDrawVertex corners[4],
                               uint32_t texture_handle) {
    if (platform == NULL || corners == NULL)
        return;

    if (!platform->rendering_target && platform->retained.recording) {
        if (wm_frame_damage_quad(platform->retained.commands, corners, texture_handle,
                                 &platform->retained.clip))
            return;
        wm_gles2_retained_materialize(&platform->retained, platform, wm_flush);
    }

    GLuint texture =
        texture_handle != 0 ? (GLuint)texture_handle : platform->white_texture;
    if (platform->quad_count != 0 && (platform->batch_texture != texture ||
                                      platform->quad_count == WM_BATCH_QUADS)) {
        wm_flush(platform);
    }
    platform->batch_texture = texture;

    WmVertex *vertices =
        &platform->vertices[platform->quad_count * WM_VERTICES_PER_QUAD];
    for (size_t index = 0; index < WM_VERTICES_PER_QUAD; index++) {
        const WmDrawVertex *corner = &corners[wm_quad_triangle_order[index]];
        /* FBO storage has the OpenGL bottom-left texture origin, while scene
         * quads use top-left image coordinates. */
        float v = texture == platform->render_texture ? 1.0f - corner->v : corner->v;
        vertices[index] = wm_vertex(corner->x, corner->y, corner->u, v, corner->color);
    }
    platform->quad_count++;
}

/* ES 2.0 has no sampler objects and cannot repeat arbitrary NPOT textures.
 * The material shader applies GX wrap coordinates before sampling textures
 * that remain CLAMP_TO_EDGE at the API level. */
static bool wm_tev_supported(WmPlatform *platform, const WmMaterialQuad *quad) {
    if (quad->tev_stage_count == 0)
        return false;
    if (quad->tev_stage_count > WM_ES2_TEV_STAGES) {
        if (!platform->warned_tev_limit) {
            fprintf(stderr, "GLES2: materials with over six TEV stages use the "
                            "simple material fallback.\n");
            platform->warned_tev_limit = true;
        }
        return false;
    }
    if (!platform->fragment_highp) {
        for (unsigned stage = 0; stage < quad->tev_stage_count; ++stage) {
            unsigned kind = quad->tev_stages[stage][6] & 15;
            if (kind == 12 || kind == 13) {
                if (!platform->warned_tev_precision) {
                    fprintf(stderr, "GLES2: 24-bit TEV comparisons require "
                                    "fragment highp; using the simple fallback.\n");
                    platform->warned_tev_precision = true;
                }
                return false;
            }
        }
    }
    bool invalid = quad->has_alpha_compare &&
                   ((quad->alpha_compare[0] & 15) > 7 ||
                    (quad->alpha_compare[0] >> 4) > 7 || quad->alpha_compare[1] > 3);
    for (unsigned stage = 0; stage < quad->tev_stage_count; ++stage) {
        const uint8_t *bytes = quad->tev_stages[stage];
        invalid = invalid || (bytes[8] & 15) > 7 || (bytes[8] >> 4) > 7 ||
                  (bytes[9] & 15) > 7 || (bytes[9] >> 4) > 7;
    }
    if (invalid) {
        if (!platform->warned_tev_encoding) {
            fprintf(stderr, "GLES2: invalid TEV selector encoding uses the "
                            "simple material fallback.\n");
            platform->warned_tev_encoding = true;
        }
        return false;
    }
    return true;
}

void wm_platform_prepare_material(WmPlatform *platform, const WmMaterialQuad *quad) {
    if (!platform || !quad || quad->texture_count > WM_MATERIAL_TEXTURES)
        return;
    if (wm_tev_supported(platform, quad)) {
        (void)wm_gles2_get_tev_program(&platform->tev_programs,
                                       platform->tev_vertex_shader,
                                       platform->fragment_highp, quad);
    }
}

void wm_platform_draw_material_quad(WmPlatform *platform, const WmMaterialQuad *quad) {
    if (!platform || !quad || quad->texture_count > WM_MATERIAL_TEXTURES)
        return;
    WmMaterialBlend blend;
    if (!wm_material_blend_resolve(quad, &blend))
        return;
    if (!platform->rendering_target && platform->retained.recording) {
        if (wm_frame_damage_material(platform->retained.commands, quad,
                                     &platform->retained.clip))
            return;
        wm_gles2_retained_materialize(&platform->retained, platform, wm_flush);
    }
    wm_flush(platform);

    bool tev = wm_tev_supported(platform, quad);
    WmTevProgram *tev_program =
        tev ? wm_gles2_get_tev_program(&platform->tev_programs,
                                       platform->tev_vertex_shader,
                                       platform->fragment_highp, quad)
            : NULL;
    if (tev && !tev_program)
        tev = false;

    if (tev) {
        glUseProgram(tev_program->program);
        glUniform4fv(tev_program->registers_location, 3, &quad->registers[0][0]);
        glUniform4fv(tev_program->konst_location, 4, &quad->konst_colors[0][0]);
        for (unsigned unit = 0; unit < WM_MATERIAL_TEXTURES; ++unit) {
            glActiveTexture((GLenum)(GL_TEXTURE0 + unit));
            glBindTexture(GL_TEXTURE_2D, quad->textures[unit]
                                             ? (GLuint)quad->textures[unit]
                                             : platform->white_texture);
        }
    } else {
        glUseProgram(platform->material_program);
        int texture_count = (int)(quad->texture_count > 2 ? 2 : quad->texture_count);
        glUniform1i(platform->material_texture_count_location, texture_count);
        glUniform4fv(platform->material_registers_location, 3, &quad->registers[0][0]);
        glUniform4fv(platform->material_konst_location, 4, &quad->konst_colors[0][0]);
        bool alpha_test = quad->has_alpha_compare && !(quad->alpha_compare[0] == 0x77 &&
                                                       quad->alpha_compare[1] < 2);
        glUniform4f(platform->material_alpha_location,
                    alpha_test ? (float)quad->alpha_compare[0] : -1.0f,
                    (float)quad->alpha_compare[1], (float)quad->alpha_compare[2],
                    (float)quad->alpha_compare[3]);
        for (unsigned unit = 0; unit < 2; ++unit) {
            glActiveTexture((GLenum)(GL_TEXTURE0 + unit));
            glBindTexture(GL_TEXTURE_2D, quad->textures[unit]
                                             ? (GLuint)quad->textures[unit]
                                             : platform->white_texture);
            glUniform2f(platform->material_wrap_locations[unit],
                        (float)quad->wrap_s[unit], (float)quad->wrap_t[unit]);
        }
    }

    static const GLenum factors[8] = {GL_ZERO,      GL_ONE,
                                      GL_DST_COLOR, GL_ONE_MINUS_DST_COLOR,
                                      GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA,
                                      GL_DST_ALPHA, GL_ONE_MINUS_DST_ALPHA};
    if (!blend.enabled) {
        glDisable(GL_BLEND);
    } else {
        glEnable(GL_BLEND);
        glBlendFuncSeparate(factors[blend.source], factors[blend.destination], GL_ONE,
                            GL_ONE_MINUS_SRC_ALPHA);
    }

    WmMaterialGpuVertex vertices[WM_VERTICES_PER_QUAD];
    for (size_t index = 0; index < WM_VERTICES_PER_QUAD; index++) {
        const WmMaterialVertex *source = &quad->vertices[wm_quad_triangle_order[index]];
        WmMaterialGpuVertex *target = &vertices[index];
        target->x = source->x;
        target->y = source->y;
        target->color[0] = source->color.r;
        target->color[1] = source->color.g;
        target->color[2] = source->color.b;
        target->color[3] = source->color.a;
        memcpy(target->uv, source->uv, sizeof(target->uv));
    }
    glBindBuffer(GL_ARRAY_BUFFER, platform->vertex_buffer);
    glBufferData(GL_ARRAY_BUFFER, (GLsizeiptr)sizeof(vertices), vertices,
                 GL_STREAM_DRAW);
    glEnableVertexAttribArray(0);
    glEnableVertexAttribArray(1);
    glEnableVertexAttribArray(2);
    glEnableVertexAttribArray(3);
    if (tev) {
        glEnableVertexAttribArray(4);
        glEnableVertexAttribArray(5);
    } else {
        glDisableVertexAttribArray(4);
        glDisableVertexAttribArray(5);
    }
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE,
                          (GLsizei)sizeof(WmMaterialGpuVertex),
                          (const void *)offsetof(WmMaterialGpuVertex, x));
    glVertexAttribPointer(1, 4, GL_FLOAT, GL_FALSE,
                          (GLsizei)sizeof(WmMaterialGpuVertex),
                          (const void *)offsetof(WmMaterialGpuVertex, color));
    glVertexAttribPointer(2, 2, GL_FLOAT, GL_FALSE,
                          (GLsizei)sizeof(WmMaterialGpuVertex),
                          (const void *)offsetof(WmMaterialGpuVertex, uv[0]));
    glVertexAttribPointer(3, 2, GL_FLOAT, GL_FALSE,
                          (GLsizei)sizeof(WmMaterialGpuVertex),
                          (const void *)offsetof(WmMaterialGpuVertex, uv[1]));
    if (tev) {
        glVertexAttribPointer(4, 2, GL_FLOAT, GL_FALSE,
                              (GLsizei)sizeof(WmMaterialGpuVertex),
                              (const void *)offsetof(WmMaterialGpuVertex, uv[2]));
        glVertexAttribPointer(5, 2, GL_FLOAT, GL_FALSE,
                              (GLsizei)sizeof(WmMaterialGpuVertex),
                              (const void *)offsetof(WmMaterialGpuVertex, uv[3]));
    }
    glDrawArrays(GL_TRIANGLES, 0, WM_VERTICES_PER_QUAD);
}

void wm_platform_end(WmPlatform *platform) {
    if (platform == NULL) {
        return;
    }
    if (!platform->rendering_target && platform->fade_alpha > 0.0f) {
        wm_platform_set_clip(platform, NULL);
        WmQuad cover = {.x = 0,
                        .y = 0,
                        .width = WM_FRAME_WIDTH,
                        .height = WM_FRAME_HEIGHT,
                        .u0 = 0,
                        .v0 = 0,
                        .u1 = 1,
                        .v1 = 1,
                        .color = {0, 0, 0, platform->fade_alpha},
                        .texture = 0};
        wm_platform_draw_quad(platform, &cover);
    }
    wm_flush(platform);
    if (platform->rendering_target) {
        glBindFramebuffer(GL_FRAMEBUFFER, 0);
        platform->rendering_target = false;
        return;
    }
    wm_gles2_retained_render(&platform->retained, platform, wm_flush);
    if (!wm_gles2_host_present(platform->host))
        wm_frame_damage_invalidate(platform->retained.commands);
}

void wm_platform_set_fade_alpha(WmPlatform *platform, float alpha) {
    if (platform)
        platform->fade_alpha = fminf(1.0f, fmaxf(0.0f, alpha));
}

uint32_t wm_platform_create_render_texture(WmPlatform *platform) {
    if (!platform || platform->render_texture != 0)
        return 0;
    GLint limit = 0;
    glGetIntegerv(GL_MAX_TEXTURE_SIZE, &limit);
    if (WM_FRAME_WIDTH > limit || WM_FRAME_HEIGHT > limit)
        return 0;

    GLuint texture = wm_upload_texture(WM_FRAME_WIDTH, WM_FRAME_HEIGHT, NULL);
    if (!texture)
        return 0;
    GLuint framebuffer = 0;
    glGenFramebuffers(1, &framebuffer);
    if (!framebuffer) {
        glDeleteTextures(1, &texture);
        return 0;
    }
    GLint prior = 0;
    glGetIntegerv(GL_FRAMEBUFFER_BINDING, &prior);
    glBindFramebuffer(GL_FRAMEBUFFER, framebuffer);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, texture,
                           0);
    GLenum status = glCheckFramebufferStatus(GL_FRAMEBUFFER);
    glBindFramebuffer(GL_FRAMEBUFFER, (GLuint)prior);
    if (status != GL_FRAMEBUFFER_COMPLETE) {
        glDeleteFramebuffers(1, &framebuffer);
        glDeleteTextures(1, &texture);
        return 0;
    }
    platform->render_texture = texture;
    platform->render_framebuffer = framebuffer;
    return (uint32_t)texture;
}

bool wm_platform_begin_target(WmPlatform *platform, uint32_t texture,
                              WmColor clear_color) {
    if (!platform || texture == 0 || texture != platform->render_texture ||
        platform->render_framebuffer == 0)
        return false;
    wm_gles2_retained_materialize(&platform->retained, platform, wm_flush);
    wm_frame_damage_invalidate(platform->retained.commands);
    platform->quad_count = 0;
    platform->rendering_target = true;
    platform->framebuffer_width = WM_FRAME_WIDTH;
    platform->framebuffer_height = WM_FRAME_HEIGHT;
    platform->presentation = (WmViewport){0, 0, WM_FRAME_WIDTH, WM_FRAME_HEIGHT};
    glBindFramebuffer(GL_FRAMEBUFFER, platform->render_framebuffer);
    glViewport(0, 0, WM_FRAME_WIDTH, WM_FRAME_HEIGHT);
    glDisable(GL_SCISSOR_TEST);
    platform->scissor_enabled = false;
    glClearColor(clear_color.r, clear_color.g, clear_color.b, clear_color.a);
    glClear(GL_COLOR_BUFFER_BIT);
    return true;
}

uint32_t wm_platform_create_texture(WmPlatform *platform, int width, int height,
                                    const uint8_t *rgba) {
    if (platform == NULL || rgba == NULL || width <= 0 || height <= 0) {
        return 0;
    }

    GLint limit = 0;
    glGetIntegerv(GL_MAX_TEXTURE_SIZE, &limit);
    if (width > limit || height > limit) {
        fprintf(stderr, "GLES2: texture exceeds the GPU size limit.\n");
        return 0;
    }
    wm_flush(platform);
    wm_frame_damage_invalidate(platform->retained.commands);
    return (uint32_t)wm_upload_texture(width, height, rgba);
}

void wm_platform_destroy_texture(WmPlatform *platform, uint32_t texture) {
    if (platform == NULL || texture == 0 || texture == platform->white_texture) {
        return;
    }
    wm_gles2_retained_materialize(&platform->retained, platform, wm_flush);
    wm_frame_damage_invalidate(platform->retained.commands);
    wm_flush(platform);
    GLuint name = (GLuint)texture;
    if (name == platform->render_texture) {
        if (platform->rendering_target) {
            glBindFramebuffer(GL_FRAMEBUFFER, 0);
            platform->rendering_target = false;
        }
        glDeleteFramebuffers(1, &platform->render_framebuffer);
        platform->render_framebuffer = 0;
        platform->render_texture = 0;
    }
    glDeleteTextures(1, &name);
}
