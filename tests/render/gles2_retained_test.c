#define _POSIX_C_SOURCE 200809L

#include "wii_menu/platform/platform.h"
#include "frame_damage.h"

#include <EGL/egl.h>
#include <GLES2/gl2.h>

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

enum { TEST_FRAMES = 20 };

static unsigned char *reference[TEST_FRAMES];
static size_t reference_size[TEST_FRAMES];
static unsigned frame_index;
static bool recording_reference;

EGLBoolean __real_eglSwapBuffers(EGLDisplay display, EGLSurface surface);

/* Capture before the swap, where GLES2 guarantees the back buffer contents.
 * Linker wrapping keeps readback and test instrumentation out of the app. */
EGLBoolean __wrap_eglSwapBuffers(EGLDisplay display, EGLSurface surface) {
    EGLint width = 0, height = 0;
    assert(eglQuerySurface(display, surface, EGL_WIDTH, &width));
    assert(eglQuerySurface(display, surface, EGL_HEIGHT, &height));
    assert(width > 0 && height > 0 && frame_index < TEST_FRAMES);
    size_t size = (size_t)width * (size_t)height * 4;
    unsigned char *pixels = malloc(size);
    assert(pixels);
    glReadPixels(0, 0, width, height, GL_RGBA, GL_UNSIGNED_BYTE, pixels);
    assert(glGetError() == GL_NO_ERROR);
    if (recording_reference) {
        reference[frame_index] = pixels;
        reference_size[frame_index] = size;
    } else {
        assert(size == reference_size[frame_index]);
        for (size_t index = 0; index < size; index++) {
            if (pixels[index] != reference[frame_index][index]) {
                fprintf(stderr, "Frame %u differs at byte %zu: %u instead of %u\n",
                        frame_index, index, pixels[index],
                        reference[frame_index][index]);
                abort();
            }
        }
        free(pixels);
    }
    frame_index++;
    return __real_eglSwapBuffers(display, surface);
}

static void draw_quad(WmPlatform *platform, float x, float y, float width, float height,
                      WmColor color, uint32_t texture) {
    WmQuad quad = {.x = x,
                   .y = y,
                   .width = width,
                   .height = height,
                   .u0 = 0,
                   .v0 = 0,
                   .u1 = 1,
                   .v1 = 1,
                   .color = color,
                   .texture = texture};
    wm_platform_draw_quad(platform, &quad);
}

static void draw_material(WmPlatform *platform, unsigned frame, uint32_t texture) {
    WmMaterialQuad quad = {0};
    quad.texture_count = 1;
    quad.textures[0] = texture;
    quad.registers[0][0] = 0.15f;
    quad.registers[1][0] = 0.7f;
    quad.registers[1][1] = 0.5f;
    quad.registers[1][2] = 0.25f;
    quad.registers[1][3] = frame >= 8 ? 0.6f : 0.9f;
    quad.has_alpha_compare = true;
    quad.alpha_compare[0] = 0x74;
    quad.alpha_compare[2] = 16;
    quad.has_blend_mode = true;
    quad.blend_mode[0] = 1;
    quad.blend_mode[1] = 4;
    quad.blend_mode[2] = 5;
    const float xy[4][2] = {
        {170.2f, 121.3f}, {270.7f, 129.1f}, {160.5f, 212.6f}, {280.8f, 218.7f}};
    for (unsigned index = 0; index < 4; index++) {
        quad.vertices[index].x = xy[index][0];
        quad.vertices[index].y = xy[index][1];
        quad.vertices[index].uv[0][0] = (float)(index % 2);
        quad.vertices[index].uv[0][1] = (float)(index / 2);
        quad.vertices[index].color = (WmColor){1, 1, 1, 0.8f};
    }
    wm_platform_draw_material_quad(platform, &quad);

    /* A one-stage TEV material multiplies the sampled texture by the vertex
     * color, including alpha. Exercise the specialized shader path as well. */
    quad.tev_stage_count = 1;
    quad.tev_swap_table[0] = 0xe4;
    quad.tev_stages[0][4] = 0x8f; /* color A = zero, B = texture */
    quad.tev_stages[0][5] = 0xfa; /* color C = raster, D = zero */
    quad.tev_stages[0][7] = 1;    /* clamp color */
    quad.tev_stages[0][8] = 0x47; /* alpha A = zero, B = texture */
    quad.tev_stages[0][9] = 0x75; /* alpha C = raster, D = zero */
    quad.tev_stages[0][11] = 1;   /* clamp alpha */
    for (unsigned index = 0; index < 4; index++) {
        quad.vertices[index].x += 220;
        quad.vertices[index].color.a = frame >= 5 ? 0.5f : 0.8f;
    }
    wm_platform_draw_material_quad(platform, &quad);
}

static void render_scene(int width, int height, bool retained) {
    assert(setenv("WM_GLES2_RETAIN_FRAME", retained ? "1" : "0", 1) == 0);
    WmPlatform *platform =
        wm_platform_create("Retained frame comparison", width, height);
    assert(platform);
    if (retained) {
        EGLint behavior = EGL_BUFFER_DESTROYED;
        assert(eglQuerySurface(eglGetCurrentDisplay(), eglGetCurrentSurface(EGL_DRAW),
                               EGL_SWAP_BEHAVIOR, &behavior));
        if (behavior != EGL_BUFFER_PRESERVED) {
            wm_platform_destroy(platform);
            puts("Retained frame comparison skipped: EGL cannot preserve window "
                 "pixels.");
            exit(77);
        }
    }
    const uint8_t rgba[16] = {255, 32, 64,  80, 16,  200, 80, 255,
                              48,  32, 230, 0,  240, 210, 16, 160};
    uint32_t texture = wm_platform_create_texture(platform, 2, 2, rgba);
    uint32_t capture = wm_platform_create_render_texture(platform);
    assert(texture && capture);
    frame_index = 0;
    recording_reference = !retained;
    for (unsigned frame = 0; frame < TEST_FRAMES; frame++) {
        if (frame == 0 || frame == 10) {
            assert(wm_platform_begin_target(platform, capture,
                                            (WmColor){0.2f, 0.3f, 0.4f, 1}));
            draw_quad(platform, 30, 30, 200, 100,
                      (WmColor){frame == 0 ? 0.6f : 0.9f, 0.8f, 0.4f, 0.5f}, texture);
            wm_platform_end(platform);
        }
        WmColor clear = {0.1f, 0.2f, 0.3f, frame >= 18 ? 0.6f : 0.4f};
        wm_platform_begin(platform, clear);
        draw_quad(platform, 0, 0, WM_FRAME_WIDTH, WM_FRAME_HEIGHT,
                  (WmColor){0.8f, 0.6f, 0.4f, 0.35f}, texture);
        draw_quad(platform, 390, 250, 120, 90, (WmColor){1, 1, 1, 1}, capture);
        WmClipRect clip = {40.25f, 50.75f, frame >= 6 ? 95.3f : 80.6f, 70.4f};
        wm_platform_set_clip(platform, &clip);
        draw_quad(platform, frame >= 4 ? 60 : 20, 35, 170, 120,
                  (WmColor){0.3f, 0.8f, 0.6f, 0.7f}, texture);
        wm_platform_set_clip(platform, NULL);
        if (frame != 7 && frame != 8)
            draw_quad(platform, 300, 80, 25, 25, (WmColor){1, 0, 0, 0.5f}, 0);
        draw_material(platform, frame, texture);
        if (frame == 12) {
            uint32_t temporary = wm_platform_create_texture(platform, 2, 2, rgba);
            assert(temporary);
            draw_quad(platform, 350, 150, 45, 70, (WmColor){1, 1, 1, 1}, temporary);
            wm_platform_destroy_texture(platform, temporary);
        }
        if (frame == 14) {
            for (size_t index = 0; index <= WM_FRAME_COMMAND_CAPACITY; index++)
                draw_quad(platform, 560, 400, 10, 10, (WmColor){1, 1, 1, 0.01f}, 0);
        }
        wm_platform_set_fade_alpha(platform, frame == 16 ? 0.35f : 0);
        wm_platform_end(platform);
    }
    wm_platform_destroy_texture(platform, capture);
    wm_platform_destroy_texture(platform, texture);
    wm_platform_destroy(platform);
}

int main(void) {
    if (!getenv("DISPLAY"))
        return 77;
    const int sizes[2][2] = {{321, 241}, {960, 540}};
    for (size_t index = 0; index < 2; index++) {
        render_scene(sizes[index][0], sizes[index][1], false);
        render_scene(sizes[index][0], sizes[index][1], true);
        for (size_t frame = 0; frame < TEST_FRAMES; frame++)
            free(reference[frame]);
    }
    puts("Direct and retained GLES2 frames match byte for byte.");
    return 0;
}
