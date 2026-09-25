#include "wii_menu/platform.h"
#include "wii_menu/viewport.h"

#include <EGL/egl.h>
#include <GLES2/gl2.h>
#include <X11/Xlib.h>
#include <X11/Xutil.h>
#include <X11/keysym.h>

#include <stddef.h>
#include <math.h>
#include <stdint.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

enum {
    WM_BATCH_QUADS = 1024,
    WM_VERTICES_PER_QUAD = 6
};

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

struct WmPlatform {
    Display *display;
    Window window;
    Colormap colormap;
    Cursor hidden_cursor;
    Atom delete_window;
    int window_width;
    int window_height;
    int framebuffer_width;
    int framebuffer_height;
    WmViewport presentation;
    bool scissor_enabled;
    int scissor_x;
    int scissor_y;
    int scissor_width;
    int scissor_height;

    EGLDisplay egl_display;
    EGLSurface egl_surface;
    EGLContext egl_context;

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
    bool swap_failure_reported;
    float fade_alpha;
};

static const char *const wm_vertex_source =
    "attribute vec2 a_position;\n"
    "attribute vec2 a_uv;\n"
    "attribute vec4 a_color;\n"
    "uniform vec2 u_frame_size;\n"
    "varying WM_UV_PRECISION vec2 v_uv;\n"
    "varying lowp vec4 v_color;\n"
    "\n"
    "void main() {\n"
    "    vec2 clip = vec2(2.0 * a_position.x / u_frame_size.x - 1.0,\n"
    "                     1.0 - 2.0 * a_position.y / u_frame_size.y);\n"
    "    gl_Position = vec4(clip, 0.0, 1.0);\n"
    "    v_uv = a_uv;\n"
    "    v_color = a_color;\n"
    "}\n";

static const char *const wm_fragment_source =
    "precision WM_UV_PRECISION float;\n"
    "uniform sampler2D u_texture;\n"
    "varying WM_UV_PRECISION vec2 v_uv;\n"
    "varying lowp vec4 v_color;\n"
    "\n"
    "void main() {\n"
    "    gl_FragColor = texture2D(u_texture, v_uv) * v_color;\n"
    "}\n";

static const char *const wm_material_vertex_source =
    "attribute vec2 a_position;\n"
    "attribute vec4 a_color;\n"
    "attribute vec2 a_uv0;\n"
    "attribute vec2 a_uv1;\n"
    "uniform vec2 u_frame_size;\n"
    "varying lowp vec4 v_color;\n"
    "varying WM_UV_PRECISION vec2 v_uv0;\n"
    "varying WM_UV_PRECISION vec2 v_uv1;\n"
    "void main() {\n"
    "    gl_Position = vec4(\n"
    "        2.0 * a_position.x / u_frame_size.x - 1.0,\n"
    "        1.0 - 2.0 * a_position.y / u_frame_size.y,\n"
    "        0.0, 1.0);\n"
    "    v_color = a_color;\n"
    "    v_uv0 = a_uv0;\n"
    "    v_uv1 = a_uv1;\n"
    "}\n";

static const char *const wm_tev_vertex_source =
    "attribute vec2 a_position;\n"
    "attribute vec4 a_color;\n"
    "attribute vec2 a_uv0;\n"
    "attribute vec2 a_uv1;\n"
    "attribute vec2 a_uv2;\n"
    "attribute vec2 a_uv3;\n"
    "uniform vec2 u_frame_size;\n"
    "varying WM_UV_PRECISION vec4 raster;\n"
    "varying WM_UV_PRECISION vec2 texUV0;\n"
    "varying WM_UV_PRECISION vec2 texUV1;\n"
    "varying WM_UV_PRECISION vec2 texUV2;\n"
    "varying WM_UV_PRECISION vec2 texUV3;\n"
    "void main() {\n"
    "    gl_Position = vec4(\n"
    "        2.0 * a_position.x / u_frame_size.x - 1.0,\n"
    "        1.0 - 2.0 * a_position.y / u_frame_size.y,\n"
    "        0.0, 1.0);\n"
    "    raster = a_color;\n"
    "    texUV0 = a_uv0;\n"
    "    texUV1 = a_uv1;\n"
    "    texUV2 = a_uv2;\n"
    "    texUV3 = a_uv3;\n"
    "}\n";

static const char *const wm_material_fragment_source =
    "precision WM_UV_PRECISION float;\n"
    "uniform sampler2D u_texture0;\n"
    "uniform sampler2D u_texture1;\n"
    "uniform int u_texture_count;\n"
    "uniform vec4 u_registers[3];\n"
    "uniform vec4 u_konst[4];\n"
    "uniform vec4 u_alpha_compare;\n"
    "uniform vec2 u_wrap0;\n"
    "uniform vec2 u_wrap1;\n"
    "varying lowp vec4 v_color;\n"
    "varying WM_UV_PRECISION vec2 v_uv0;\n"
    "varying WM_UV_PRECISION vec2 v_uv1;\n"
    "float wrapAxis(float coordinate, float mode) {\n"
    "    if (mode > 1.5) {\n"
    "        return 1.0 - abs(mod(coordinate, 2.0) - 1.0);\n"
    "    }\n"
    "    if (mode > 0.5) {\n"
    "        return fract(coordinate);\n"
    "    }\n"
    "    return clamp(coordinate, 0.0, 1.0);\n"
    "}\n"
    "vec2 wrapUV(vec2 uv, vec2 mode) {\n"
    "    return vec2(wrapAxis(uv.x, mode.x), wrapAxis(uv.y, mode.y));\n"
    "}\n"
    "bool compareAlpha(float kind, float value, float reference) {\n"
    "    if (kind < 0.5) return false;\n"
    "    if (kind > 6.5) return true;\n"
    "    if (kind < 1.5) return value < reference;\n"
    "    if (kind < 2.5) return value == reference;\n"
    "    if (kind < 3.5) return value <= reference;\n"
    "    if (kind < 4.5) return value > reference;\n"
    "    if (kind < 5.5) return value != reference;\n"
    "    return value >= reference;\n"
    "}\n"
    "void main() {\n"
    "    vec4 pixel = u_registers[1];\n"
    "    if (u_texture_count > 0) {\n"
    "        vec4 first = texture2D(u_texture0, wrapUV(v_uv0, u_wrap0));\n"
    "        if (u_texture_count > 1) {\n"
    "            vec4 second = texture2D(u_texture1, wrapUV(v_uv1, u_wrap1));\n"
    "            vec4 sampleValue = mix(second, first, u_konst[3].a);\n"
    "            pixel = mix(u_registers[0], u_registers[1], sampleValue);\n"
    "        } else {\n"
    "            pixel = mix(u_registers[0], u_registers[1], first);\n"
    "        }\n"
    "    }\n"
    "    pixel *= v_color;\n"
    "    if (u_alpha_compare.x >= 0.0) {\n"
    "        float value = mod(floor(pixel.a * 255.0 + 0.5), 256.0);\n"
    "        float low = mod(u_alpha_compare.x, 16.0);\n"
    "        float high = floor(u_alpha_compare.x / 16.0);\n"
    "        bool first = compareAlpha(low, value, u_alpha_compare.z);\n"
    "        bool second = compareAlpha(high, value, u_alpha_compare.w);\n"
    "        bool passes = u_alpha_compare.y < 0.5 ? (first && second) :\n"
    "                      u_alpha_compare.y < 1.5 ? (first || second) :\n"
    "                      u_alpha_compare.y < 2.5 ? (first != second) :\n"
    "                                                  (first == second);\n"
    "        if (!passes) discard;\n"
    "    }\n"
    "    gl_FragColor = pixel;\n"
    "}\n";

typedef struct WmShaderText {
    char *data;
    size_t length;
    size_t capacity;
    bool failed;
} WmShaderText;

static void wm_emit(WmShaderText *text, const char *format, ...)
{
    if (text->failed) return;
    va_list args;
    va_start(args, format);
    va_list copy;
    va_copy(copy, args);
    int count = vsnprintf(NULL, 0, format, copy);
    va_end(copy);
    if (count < 0 || (size_t)count > SIZE_MAX - text->length - 1) {
        text->failed = true;
        va_end(args);
        return;
    }
    size_t needed = text->length + (size_t)count + 1;
    if (needed > text->capacity) {
        size_t capacity = text->capacity ? text->capacity : 2048;
        while (capacity < needed) {
            if (capacity > SIZE_MAX / 2) {
                capacity = needed;
                break;
            }
            capacity *= 2;
        }
        char *data = realloc(text->data, capacity);
        if (!data) {
            text->failed = true;
            va_end(args);
            return;
        }
        text->data = data;
        text->capacity = capacity;
    }
    vsnprintf(text->data + text->length, text->capacity - text->length,
              format, args);
    text->length += (size_t)count;
    va_end(args);
}

static void wm_swizzle(uint8_t pattern, char output[5])
{
    static const char channels[] = "rgba";
    for (unsigned index = 0; index < 4; ++index) {
        output[index] = channels[(pattern >> (index * 2)) & 3];
    }
    output[4] = '\0';
}

static void wm_konst_expression(unsigned selector, bool alpha,
                                char output[64])
{
    if (selector < 8) {
        if (alpha) {
            snprintf(output, 64, "%.3f", (8.0 - selector) / 8.0);
        } else {
            snprintf(output, 64, "vec3(%.3f)", (8.0 - selector) / 8.0);
        }
    } else if (!alpha && selector >= 12 && selector < 16) {
        snprintf(output, 64, "kc[%u].rgb", selector - 12);
    } else if (selector >= 16) {
        unsigned color = (selector - 16) % 4;
        unsigned channel = (selector - 16) / 4;
        if (alpha) {
            snprintf(output, 64, "kc[%u][%u]", color, channel);
        } else {
            snprintf(output, 64, "vec3(kc[%u][%u])", color, channel);
        }
    } else {
        snprintf(output, 64, "%s", alpha ? "1.0" : "vec3(1.0)");
    }
}

static void wm_emit_color_operation(WmShaderText *text, unsigned index,
                                    const uint8_t stage[16])
{
    unsigned kind = stage[6] & 15;
    const char *compare = kind & 1 ? "==" : ">";
    wm_emit(text, "    vec3 c%u;\n", index);
    if (kind >= 14) {
        wm_emit(text,
                "    bvec3 comparison%u = %s(tevColor8(ca%u), "
                "tevColor8(cb%u));\n",
                index, kind & 1 ? "equal" : "greaterThan", index, index);
        wm_emit(text,
                "    c%u = cd%u + vec3(comparison%u.x ? cc%u.x : 0.0, "
                "comparison%u.y ? cc%u.y : 0.0, "
                "comparison%u.z ? cc%u.z : 0.0);\n",
                index, index, index, index, index, index, index, index);
    } else if (kind >= 8) {
        char packed_a[128];
        char packed_b[128];
        if (kind < 10) {
            snprintf(packed_a, sizeof(packed_a), "tevColor8(ca%u).r", index);
            snprintf(packed_b, sizeof(packed_b), "tevColor8(cb%u).r", index);
        } else if (kind < 12) {
            snprintf(packed_a, sizeof(packed_a),
                     "dot(tevColor8(ca%u).rg, vec2(1.0, 256.0))", index);
            snprintf(packed_b, sizeof(packed_b),
                     "dot(tevColor8(cb%u).rg, vec2(1.0, 256.0))", index);
        } else {
            snprintf(packed_a, sizeof(packed_a),
                     "dot(tevColor8(ca%u), vec3(1.0, 256.0, 65536.0))", index);
            snprintf(packed_b, sizeof(packed_b),
                     "dot(tevColor8(cb%u), vec3(1.0, 256.0, 65536.0))", index);
        }
        wm_emit(text, "    c%u = cd%u + ((%s %s %s) ? cc%u : vec3(0.0));\n",
                index, index, packed_a, compare, packed_b, index);
    } else {
        static const char *const bias[4] = {"0.0", "0.5", "-0.5", "0.0"};
        static const char *const scale[4] = {"1.0", "2.0", "4.0", "0.5"};
        wm_emit(text,
                "    c%u = ((cd%u %s mix(ca%u, cb%u, cc%u)) + "
                "vec3(%s)) * %s;\n",
                index, index, kind == 1 ? "-" : "+", index, index, index,
                bias[(stage[6] >> 4) & 3], scale[stage[6] >> 6]);
    }
    if (stage[7] & 1) {
        wm_emit(text, "    c%u = clamp(c%u, 0.0, 1.0);\n", index, index);
    }
}

static void wm_emit_alpha_operation(WmShaderText *text, unsigned index,
                                    const uint8_t stage[16])
{
    unsigned kind = stage[10] & 15;
    wm_emit(text, "    float a%u;\n", index);
    if (kind >= 8) {
        wm_emit(text,
                "    a%u = ad%u + ((tevAlpha8(aa%u) %s "
                "tevAlpha8(ab%u)) ? ac%u : 0.0);\n",
                index, index, index, kind & 1 ? "==" : ">", index, index);
    } else {
        static const char *const bias[4] = {"0.0", "0.5", "-0.5", "0.0"};
        static const char *const scale[4] = {"1.0", "2.0", "4.0", "0.5"};
        wm_emit(text,
                "    a%u = ((ad%u %s mix(aa%u, ab%u, ac%u)) + %s) * %s;\n",
                index, index, kind == 1 ? "-" : "+", index, index, index,
                bias[(stage[10] >> 4) & 3], scale[stage[10] >> 6]);
    }
    if (stage[11] & 1) {
        wm_emit(text, "    a%u = clamp(a%u, 0.0, 1.0);\n", index, index);
    }
}

static void wm_emit_tev_stage(WmShaderText *text, const WmTevKey *key,
                              unsigned index)
{
    static const char *const color_inputs[16] = {
        "p.rgb", "vec3(p.a)", "r0.rgb", "vec3(r0.a)",
        "r1.rgb", "vec3(r1.a)", "r2.rgb", "vec3(r2.a)",
        "tex.rgb", "vec3(tex.a)", "ras.rgb", "vec3(ras.a)",
        "vec3(1.0)", "vec3(0.5)", "kcolor", "vec3(0.0)"
    };
    static const char *const alpha_inputs[8] = {
        "p.a", "r0.a", "r1.a", "r2.a", "tex.a", "ras.a", "kalpha", "0.0"
    };
    static const char *const destinations[4] = {"p", "r0", "r1", "r2"};
    const uint8_t *stage = key->stages[index];
    unsigned slot = ((stage[3] & 1u) << 8) | stage[2];
    unsigned coord = stage[0];
    char tex_swizzle[5];
    char ras_swizzle[5];
    char kcolor[64];
    char kalpha[64];
    wm_swizzle(key->swap[(stage[3] >> 3) & 3], tex_swizzle);
    wm_swizzle(key->swap[(stage[3] >> 1) & 3], ras_swizzle);
    wm_konst_expression(stage[7] >> 3, false, kcolor);
    wm_konst_expression(stage[11] >> 3, true, kalpha);

    wm_emit(text, "    // Original TEV stage %u.\n", index);
    if (slot < 4 && coord < 4) {
        wm_emit(text,
                "    tex = texture2D(t%u, wrapUV(texUV%u, vec2(%u.0, %u.0)));\n",
                slot, coord, key->wrap_s[slot], key->wrap_t[slot]);
    } else {
        wm_emit(text, "    tex = vec4(1.0);\n");
    }
    wm_emit(text, "    tex = tex.%s;\n", tex_swizzle);
    wm_emit(text, "    ras = %s;\n",
            stage[1] == 255 || stage[1] == 6 || stage[1] == 7
                ? "vec4(0.0)" : "raster");
    wm_emit(text, "    ras = ras.%s;\n", ras_swizzle);
    wm_emit(text, "    kcolor = %s;\n", kcolor);
    wm_emit(text, "    kalpha = %s;\n", kalpha);

    wm_emit(text, "    vec3 ca%u = %s;\n", index,
            color_inputs[stage[4] & 15]);
    wm_emit(text, "    vec3 cb%u = %s;\n", index,
            color_inputs[stage[4] >> 4]);
    wm_emit(text, "    vec3 cc%u = %s;\n", index,
            color_inputs[stage[5] & 15]);
    wm_emit(text, "    vec3 cd%u = %s;\n", index,
            color_inputs[stage[5] >> 4]);
    wm_emit(text, "    float aa%u = %s;\n", index,
            alpha_inputs[stage[8] & 7]);
    wm_emit(text, "    float ab%u = %s;\n", index,
            alpha_inputs[(stage[8] >> 4) & 7]);
    wm_emit(text, "    float ac%u = %s;\n", index,
            alpha_inputs[stage[9] & 7]);
    wm_emit(text, "    float ad%u = %s;\n", index,
            alpha_inputs[(stage[9] >> 4) & 7]);
    wm_emit_color_operation(text, index, stage);
    wm_emit_alpha_operation(text, index, stage);
    wm_emit(text, "    %s.rgb = c%u;\n",
            destinations[(stage[7] >> 1) & 3], index);
    wm_emit(text, "    %s.a = a%u;\n",
            destinations[(stage[11] >> 1) & 3], index);
}

static void wm_alpha_condition(char output[96], uint8_t kind,
                                uint8_t reference)
{
    static const char *const comparisons[8] = {
        "", "<", "==", "<=", ">", "!=", ">=", ""
    };
    if (kind == 0 || kind > 7) {
        snprintf(output, 96, "false");
    } else if (kind == 7) {
        snprintf(output, 96, "true");
    } else {
        snprintf(output, 96, "tevAlpha8(p.a) %s %u.0",
                 comparisons[kind], reference);
    }
}

static char *wm_tev_fragment_source(const WmTevKey *key)
{
    WmShaderText text = {0};
    wm_emit(&text,
            "precision WM_UV_PRECISION float;\n"
            "uniform sampler2D t0;\n"
            "uniform sampler2D t1;\n"
            "uniform sampler2D t2;\n"
            "uniform sampler2D t3;\n"
            "uniform vec4 regs[3];\n"
            "uniform vec4 kc[4];\n"
            "varying WM_UV_PRECISION vec4 raster;\n"
            "varying WM_UV_PRECISION vec2 texUV0;\n"
            "varying WM_UV_PRECISION vec2 texUV1;\n"
            "varying WM_UV_PRECISION vec2 texUV2;\n"
            "varying WM_UV_PRECISION vec2 texUV3;\n"
            "float wrapAxis(float coordinate, float mode) {\n"
            "    if (mode > 1.5) return 1.0 - abs(mod(coordinate, 2.0) - 1.0);\n"
            "    if (mode > 0.5) return fract(coordinate);\n"
            "    return clamp(coordinate, 0.0, 1.0);\n"
            "}\n"
            "vec2 wrapUV(vec2 uv, vec2 mode) {\n"
            "    return vec2(wrapAxis(uv.x, mode.x), wrapAxis(uv.y, mode.y));\n"
            "}\n"
            "vec3 tevColor8(vec3 value) {\n"
            "    return mod(floor(value * 255.0 + 0.5), 256.0);\n"
            "}\n"
            "float tevAlpha8(float value) {\n"
            "    return mod(floor(value * 255.0 + 0.5), 256.0);\n"
            "}\n"
            "void main() {\n"
            "    vec4 p = vec4(0.0);\n"
            "    vec4 r0 = regs[0];\n"
            "    vec4 r1 = regs[1];\n"
            "    vec4 r2 = regs[2];\n"
            "    vec4 tex;\n"
            "    vec4 ras;\n"
            "    vec3 kcolor;\n"
            "    float kalpha;\n");
    for (unsigned index = 0; index < key->stage_count; ++index) {
        wm_emit_tev_stage(&text, key, index);
    }
    if (key->has_alpha_compare &&
        !(key->alpha_compare[0] == 0x77 && key->alpha_compare[1] < 2)) {
        char first[96];
        char second[96];
        wm_alpha_condition(first, key->alpha_compare[0] & 15,
                           key->alpha_compare[2]);
        wm_alpha_condition(second, key->alpha_compare[0] >> 4,
                           key->alpha_compare[3]);
        static const char *const operators[4] = {"&&", "||", "!=", "=="};
        unsigned operation = key->alpha_compare[1] < 4
            ? key->alpha_compare[1] : 0;
        wm_emit(&text, "    if (!((%s) %s (%s))) discard;\n",
                first, operators[operation], second);
    }
    wm_emit(&text, "    gl_FragColor = p;\n}\n");
    if (text.failed) {
        free(text.data);
        return NULL;
    }
    return text.data;
}

static void wm_report_egl_error(const char *operation)
{
    fprintf(stderr, "GLES2: %s failed (EGL 0x%04x).\n",
            operation, (unsigned int)eglGetError());
}

static GLuint wm_compile_shader(GLenum type, const char *source,
                                const char *precision_directive)
{
    GLuint shader = glCreateShader(type);
    GLint compiled = GL_FALSE;

    if (shader == 0) {
        fprintf(stderr, "GLES2: could not allocate a shader.\n");
        return 0;
    }
    const GLchar *parts[] = {precision_directive, source};
    glShaderSource(shader, 2, parts, NULL);
    glCompileShader(shader);
    glGetShaderiv(shader, GL_COMPILE_STATUS, &compiled);
    if (compiled == GL_TRUE) {
        return shader;
    }

    char log[1024] = {0};
    glGetShaderInfoLog(shader, (GLsizei)sizeof(log), NULL, log);
    fprintf(stderr, "GLES2: shader compilation failed: %s\n", log);
    glDeleteShader(shader);
    return 0;
}

static GLuint wm_create_program(void)
{
    GLint range[2] = {0, 0};
    GLint precision = 0;
    glGetShaderPrecisionFormat(GL_FRAGMENT_SHADER, GL_HIGH_FLOAT, range, &precision);
    const char *precision_directive = precision > 0 ?
        "#define WM_UV_PRECISION highp\n" :
        "#define WM_UV_PRECISION mediump\n";

    GLuint vertex_shader = wm_compile_shader(
        GL_VERTEX_SHADER, wm_vertex_source, precision_directive);
    if (vertex_shader == 0) {
        return 0;
    }

    GLuint fragment_shader = wm_compile_shader(
        GL_FRAGMENT_SHADER, wm_fragment_source, precision_directive);
    if (fragment_shader == 0) {
        glDeleteShader(vertex_shader);
        return 0;
    }

    GLuint program = glCreateProgram();
    if (program == 0) {
        fprintf(stderr, "GLES2: could not allocate a shader program.\n");
        glDeleteShader(fragment_shader);
        glDeleteShader(vertex_shader);
        return 0;
    }

    glAttachShader(program, vertex_shader);
    glAttachShader(program, fragment_shader);
    glBindAttribLocation(program, 0, "a_position");
    glBindAttribLocation(program, 1, "a_uv");
    glBindAttribLocation(program, 2, "a_color");
    glLinkProgram(program);

    GLint linked = GL_FALSE;
    glGetProgramiv(program, GL_LINK_STATUS, &linked);
    glDeleteShader(fragment_shader);
    glDeleteShader(vertex_shader);
    if (linked == GL_TRUE) {
        return program;
    }

    char log[1024] = {0};
    glGetProgramInfoLog(program, (GLsizei)sizeof(log), NULL, log);
    fprintf(stderr, "GLES2: shader link failed: %s\n", log);
    glDeleteProgram(program);
    return 0;
}

static GLuint wm_create_material_program(void)
{
    GLint range[2] = {0, 0};
    GLint precision = 0;
    glGetShaderPrecisionFormat(GL_FRAGMENT_SHADER, GL_HIGH_FLOAT, range, &precision);
    const char *directive = precision > 0 ?
        "#define WM_UV_PRECISION highp\n" :
        "#define WM_UV_PRECISION mediump\n";
    GLuint vertex = wm_compile_shader(GL_VERTEX_SHADER, wm_material_vertex_source,
                                      directive);
    if (!vertex) return 0;
    GLuint fragment = wm_compile_shader(GL_FRAGMENT_SHADER,
                                        wm_material_fragment_source, directive);
    if (!fragment) { glDeleteShader(vertex); return 0; }
    GLuint program = glCreateProgram();
    if (!program) {
        glDeleteShader(vertex);
        glDeleteShader(fragment);
        return 0;
    }
    glAttachShader(program, vertex);
    glAttachShader(program, fragment);
    glBindAttribLocation(program, 0, "a_position");
    glBindAttribLocation(program, 1, "a_color");
    glBindAttribLocation(program, 2, "a_uv0");
    glBindAttribLocation(program, 3, "a_uv1");
    glLinkProgram(program);
    glDeleteShader(vertex);
    glDeleteShader(fragment);
    GLint linked = GL_FALSE;
    glGetProgramiv(program, GL_LINK_STATUS, &linked);
    if (linked) return program;
    char log[1024] = {0};
    glGetProgramInfoLog(program, (GLsizei)sizeof(log), NULL, log);
    fprintf(stderr, "GLES2: material program link failed: %s\n", log);
    glDeleteProgram(program);
    return 0;
}

static WmTevProgram *wm_get_tev_program(WmPlatform *platform,
                                        const WmMaterialQuad *quad)
{
    WmTevKey key = {0};
    key.stage_count = (uint8_t)quad->tev_stage_count;
    for (unsigned index = 0; index < key.stage_count; ++index) {
        memcpy(key.stages[index], quad->tev_stages[index], 16);
    }
    memcpy(key.swap, quad->tev_swap_table, sizeof(key.swap));
    for (unsigned index = 0; index < WM_MATERIAL_TEXTURES; ++index) {
        key.wrap_s[index] = quad->wrap_s[index] < 3 ? quad->wrap_s[index] : 0;
        key.wrap_t[index] = quad->wrap_t[index] < 3 ? quad->wrap_t[index] : 0;
    }
    key.has_alpha_compare = quad->has_alpha_compare;
    if (key.has_alpha_compare) {
        memcpy(key.alpha_compare, quad->alpha_compare,
               sizeof(key.alpha_compare));
    }

    for (WmTevProgram *entry = platform->tev_programs; entry; entry = entry->next) {
        if (memcmp(&entry->key, &key, sizeof(key)) == 0) {
            return entry->program ? entry : NULL;
        }
    }

    /* The first request compiles one GLSL ES 1.00 program. Preparing scene
     * materials during loading keeps that work outside the render loop. */
    WmTevProgram *entry = calloc(1, sizeof(*entry));
    if (!entry) return NULL;
    entry->key = key;
    entry->next = platform->tev_programs;
    platform->tev_programs = entry;
    char *source = wm_tev_fragment_source(&key);
    if (!source) return NULL;
    const char *directive = platform->fragment_highp
        ? "#define WM_UV_PRECISION highp\n"
        : "#define WM_UV_PRECISION mediump\n";
    GLuint fragment = wm_compile_shader(GL_FRAGMENT_SHADER, source, directive);
    free(source);
    if (!fragment) return NULL;

    GLuint program = glCreateProgram();
    if (!program) {
        glDeleteShader(fragment);
        return NULL;
    }
    glAttachShader(program, platform->tev_vertex_shader);
    glAttachShader(program, fragment);
    glBindAttribLocation(program, 0, "a_position");
    glBindAttribLocation(program, 1, "a_color");
    glBindAttribLocation(program, 2, "a_uv0");
    glBindAttribLocation(program, 3, "a_uv1");
    glBindAttribLocation(program, 4, "a_uv2");
    glBindAttribLocation(program, 5, "a_uv3");
    glLinkProgram(program);
    glDeleteShader(fragment);
    GLint linked = GL_FALSE;
    glGetProgramiv(program, GL_LINK_STATUS, &linked);
    if (!linked) {
        char log[1024] = {0};
        glGetProgramInfoLog(program, (GLsizei)sizeof(log), NULL, log);
        fprintf(stderr, "GLES2: TEV program link failed: %s\n", log);
        glDeleteProgram(program);
        return NULL;
    }

    entry->program = program;
    entry->registers_location = glGetUniformLocation(program, "regs");
    entry->konst_location = glGetUniformLocation(program, "kc");
    glUseProgram(program);
    glUniform2f(glGetUniformLocation(program, "u_frame_size"),
                (float)WM_FRAME_WIDTH, (float)WM_FRAME_HEIGHT);
    for (unsigned index = 0; index < WM_MATERIAL_TEXTURES; ++index) {
        char name[4];
        snprintf(name, sizeof(name), "t%u", index);
        glUniform1i(glGetUniformLocation(program, name), (GLint)index);
    }
    return entry;
}

static GLuint wm_upload_texture(int width, int height, const uint8_t *rgba)
{
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
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, width, height, 0,
                 GL_RGBA, GL_UNSIGNED_BYTE, rgba);
    if (glGetError() != GL_NO_ERROR) {
        glDeleteTextures(1, &texture);
        return 0;
    }
    return texture;
}

static void wm_flush(WmPlatform *platform)
{
    if (platform->quad_count == 0) {
        return;
    }

    GLsizeiptr byte_count = (GLsizeiptr)(platform->quad_count *
                                        WM_VERTICES_PER_QUAD * sizeof(WmVertex));
    glUseProgram(platform->program);
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, platform->batch_texture);
    glBindBuffer(GL_ARRAY_BUFFER, platform->vertex_buffer);
    /* Replacing storage avoids waiting for a previous draw using this buffer. */
    glBufferData(GL_ARRAY_BUFFER, (GLsizeiptr)sizeof(platform->vertices),
                 NULL, GL_STREAM_DRAW);
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
    glBlendFuncSeparate(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA,
                        GL_ONE, GL_ONE_MINUS_SRC_ALPHA);
    glDrawArrays(GL_TRIANGLES, 0,
                 (GLsizei)(platform->quad_count * WM_VERTICES_PER_QUAD));
    platform->quad_count = 0;
}

static bool wm_choose_config(EGLDisplay display, EGLConfig *config)
{
    static const EGLint rgba8888[] = {
        EGL_SURFACE_TYPE, EGL_WINDOW_BIT,
        EGL_RENDERABLE_TYPE, EGL_OPENGL_ES2_BIT,
        EGL_RED_SIZE, 8,
        EGL_GREEN_SIZE, 8,
        EGL_BLUE_SIZE, 8,
        EGL_ALPHA_SIZE, 8,
        EGL_NONE
    };
    static const EGLint rgb565[] = {
        EGL_SURFACE_TYPE, EGL_WINDOW_BIT,
        EGL_RENDERABLE_TYPE, EGL_OPENGL_ES2_BIT,
        EGL_RED_SIZE, 5,
        EGL_GREEN_SIZE, 6,
        EGL_BLUE_SIZE, 5,
        EGL_ALPHA_SIZE, 0,
        EGL_NONE
    };
    EGLint count = 0;

    if (eglChooseConfig(display, rgba8888, config, 1, &count) && count > 0) {
        return true;
    }
    count = 0;
    return eglChooseConfig(display, rgb565, config, 1, &count) && count > 0;
}

static bool wm_initialize_graphics(WmPlatform *platform)
{
    GLint range[2] = {0, 0};
    GLint precision = 0;
    glGetShaderPrecisionFormat(GL_FRAGMENT_SHADER, GL_HIGH_FLOAT,
                               range, &precision);
    platform->fragment_highp = precision > 0;
    const char *directive = platform->fragment_highp
        ? "#define WM_UV_PRECISION highp\n"
        : "#define WM_UV_PRECISION mediump\n";
    platform->program = wm_create_program();
    if (platform->program == 0) {
        return false;
    }
    platform->material_program = wm_create_material_program();
    if (!platform->material_program) return false;
    platform->tev_vertex_shader = wm_compile_shader(
        GL_VERTEX_SHADER, wm_tev_vertex_source, directive);
    if (!platform->tev_vertex_shader) return false;

    glUseProgram(platform->material_program);
    platform->material_frame_location = glGetUniformLocation(
        platform->material_program, "u_frame_size");
    platform->material_texture_count_location = glGetUniformLocation(
        platform->material_program, "u_texture_count");
    platform->material_registers_location = glGetUniformLocation(
        platform->material_program, "u_registers");
    platform->material_konst_location = glGetUniformLocation(
        platform->material_program, "u_konst");
    platform->material_alpha_location = glGetUniformLocation(
        platform->material_program, "u_alpha_compare");
    platform->material_wrap_locations[0] = glGetUniformLocation(
        platform->material_program, "u_wrap0");
    platform->material_wrap_locations[1] = glGetUniformLocation(
        platform->material_program, "u_wrap1");
    glUniform1i(glGetUniformLocation(platform->material_program, "u_texture0"), 0);
    glUniform1i(glGetUniformLocation(platform->material_program, "u_texture1"), 1);
    glUniform2f(platform->material_frame_location,
                (float)WM_FRAME_WIDTH, (float)WM_FRAME_HEIGHT);

    platform->projection_location = glGetUniformLocation(platform->program, "u_frame_size");
    platform->texture_location = glGetUniformLocation(platform->program, "u_texture");
    if (platform->projection_location < 0 || platform->texture_location < 0) {
        fprintf(stderr, "GLES2: required shader uniforms are unavailable.\n");
        return false;
    }

    glUseProgram(platform->program);
    glUniform2f(platform->projection_location,
                (float)WM_FRAME_WIDTH, (float)WM_FRAME_HEIGHT);
    glUniform1i(platform->texture_location, 0);

    glGenBuffers(1, &platform->vertex_buffer);
    if (platform->vertex_buffer == 0) {
        fprintf(stderr, "GLES2: could not allocate the quad buffer.\n");
        return false;
    }
    glBindBuffer(GL_ARRAY_BUFFER, platform->vertex_buffer);
    glBufferData(GL_ARRAY_BUFFER, (GLsizeiptr)sizeof(platform->vertices),
                 NULL, GL_STREAM_DRAW);

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
    glBlendFuncSeparate(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA,
                        GL_ONE, GL_ONE_MINUS_SRC_ALPHA);
    return glGetError() == GL_NO_ERROR;
}

WmPlatform *wm_platform_create(const char *title, int window_width, int window_height)
{
    if (window_width <= 0 || window_height <= 0) {
        fprintf(stderr, "GLES2: window dimensions must be positive.\n");
        return NULL;
    }

    WmPlatform *platform = calloc(1, sizeof(*platform));
    if (platform == NULL) {
        return NULL;
    }
    platform->egl_display = EGL_NO_DISPLAY;
    platform->egl_surface = EGL_NO_SURFACE;
    platform->egl_context = EGL_NO_CONTEXT;
    platform->window_width = window_width;
    platform->window_height = window_height;

    platform->display = XOpenDisplay(NULL);
    if (platform->display == NULL) {
        fprintf(stderr, "GLES2: could not open the X11 display.\n");
        goto fail;
    }

    platform->egl_display = eglGetDisplay((EGLNativeDisplayType)platform->display);
    if (platform->egl_display == EGL_NO_DISPLAY ||
        !eglInitialize(platform->egl_display, NULL, NULL)) {
        wm_report_egl_error("display initialization");
        goto fail;
    }
    if (!eglBindAPI(EGL_OPENGL_ES_API)) {
        wm_report_egl_error("OpenGL ES selection");
        goto fail;
    }

    EGLConfig config;
    if (!wm_choose_config(platform->egl_display, &config)) {
        wm_report_egl_error("ES2 window configuration selection");
        goto fail;
    }

    EGLint visual_id = 0;
    if (!eglGetConfigAttrib(platform->egl_display, config,
                            EGL_NATIVE_VISUAL_ID, &visual_id)) {
        wm_report_egl_error("X11 visual lookup");
        goto fail;
    }

    int screen = DefaultScreen(platform->display);
    Visual *visual = DefaultVisual(platform->display, screen);
    int depth = DefaultDepth(platform->display, screen);
    XVisualInfo *visual_info = NULL;
    if (visual_id != 0) {
        XVisualInfo query = {0};
        query.visualid = (VisualID)visual_id;
        query.screen = screen;
        int matches = 0;
        visual_info = XGetVisualInfo(platform->display,
                                     VisualIDMask | VisualScreenMask,
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

    Window root = RootWindow(platform->display, screen);
    platform->colormap = XCreateColormap(platform->display, root, visual, AllocNone);
    if (platform->colormap == 0) {
        fprintf(stderr, "GLES2: could not create the X11 colormap.\n");
        if (visual_info != NULL) {
            XFree(visual_info);
        }
        goto fail;
    }

    XSetWindowAttributes attributes = {0};
    attributes.colormap = platform->colormap;
    attributes.border_pixel = 0;
    attributes.event_mask = StructureNotifyMask | ExposureMask |
                            PointerMotionMask | ButtonPressMask |
                            ButtonReleaseMask | KeyPressMask |
                            EnterWindowMask | LeaveWindowMask | FocusChangeMask;
    platform->window = XCreateWindow(
        platform->display, root, 0, 0,
        (unsigned int)window_width, (unsigned int)window_height,
        0, depth, InputOutput, visual,
        CWColormap | CWBorderPixel | CWEventMask, &attributes);
    if (visual_info != NULL) {
        XFree(visual_info);
    }
    if (platform->window == 0) {
        fprintf(stderr, "GLES2: could not create the X11 window.\n");
        goto fail;
    }

    const char transparent_pixel = 0;
    Pixmap blank = XCreateBitmapFromData(platform->display, platform->window,
                                         &transparent_pixel, 1, 1);
    if (blank != None) {
        XColor transparent = {0};
        platform->hidden_cursor = XCreatePixmapCursor(
            platform->display, blank, blank, &transparent, &transparent, 0, 0);
        XFreePixmap(platform->display, blank);
        if (platform->hidden_cursor != None) {
            XDefineCursor(platform->display, platform->window,
                          platform->hidden_cursor);
        }
    }

    XStoreName(platform->display, platform->window,
               title != NULL ? title : "Wii Menu");
    platform->delete_window = XInternAtom(platform->display, "WM_DELETE_WINDOW", False);
    XSetWMProtocols(platform->display, platform->window,
                    &platform->delete_window, 1);

    platform->egl_surface = eglCreateWindowSurface(
        platform->egl_display, config,
        (EGLNativeWindowType)platform->window, NULL);
    if (platform->egl_surface == EGL_NO_SURFACE) {
        wm_report_egl_error("window surface creation");
        goto fail;
    }

    static const EGLint context_attributes[] = {
        EGL_CONTEXT_CLIENT_VERSION, 2,
        EGL_NONE
    };
    platform->egl_context = eglCreateContext(
        platform->egl_display, config, EGL_NO_CONTEXT, context_attributes);
    if (platform->egl_context == EGL_NO_CONTEXT) {
        wm_report_egl_error("ES2 context creation");
        goto fail;
    }
    if (!eglMakeCurrent(platform->egl_display, platform->egl_surface,
                        platform->egl_surface, platform->egl_context)) {
        wm_report_egl_error("context activation");
        goto fail;
    }
    if (!wm_initialize_graphics(platform)) {
        fprintf(stderr, "GLES2: graphics initialization failed.\n");
        goto fail;
    }

    if (!eglSwapInterval(platform->egl_display, 1)) {
        wm_report_egl_error("swap interval selection");
    }
    XMapWindow(platform->display, platform->window);
    XFlush(platform->display);
    return platform;

fail:
    wm_platform_destroy(platform);
    return NULL;
}

void wm_platform_destroy(WmPlatform *platform)
{
    if (platform == NULL) {
        return;
    }

    if (platform->egl_display != EGL_NO_DISPLAY &&
        platform->egl_context != EGL_NO_CONTEXT &&
        platform->egl_surface != EGL_NO_SURFACE) {
        eglMakeCurrent(platform->egl_display, platform->egl_surface,
                       platform->egl_surface, platform->egl_context);
    }
    if (platform->egl_context != EGL_NO_CONTEXT &&
        eglGetCurrentContext() == platform->egl_context) {
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
        for (WmTevProgram *entry = platform->tev_programs; entry;
             entry = entry->next) {
            if (entry->program) glDeleteProgram(entry->program);
        }
        if (platform->tev_vertex_shader) {
            glDeleteShader(platform->tev_vertex_shader);
        }
    }
    if (platform->egl_display != EGL_NO_DISPLAY) {
        eglMakeCurrent(platform->egl_display, EGL_NO_SURFACE,
                       EGL_NO_SURFACE, EGL_NO_CONTEXT);
        if (platform->egl_context != EGL_NO_CONTEXT) {
            eglDestroyContext(platform->egl_display, platform->egl_context);
        }
        if (platform->egl_surface != EGL_NO_SURFACE) {
            eglDestroySurface(platform->egl_display, platform->egl_surface);
        }
        eglTerminate(platform->egl_display);
    }
    if (platform->display != NULL) {
        if (platform->hidden_cursor != None) {
            XFreeCursor(platform->display, platform->hidden_cursor);
        }
        if (platform->window != 0) {
            XDestroyWindow(platform->display, platform->window);
        }
        if (platform->colormap != 0) {
            XFreeColormap(platform->display, platform->colormap);
        }
        XCloseDisplay(platform->display);
    }
    while (platform->tev_programs) {
        WmTevProgram *next = platform->tev_programs->next;
        free(platform->tev_programs);
        platform->tev_programs = next;
    }
    free(platform);
}

static void wm_pointer_event(WmPlatform *platform, WmEvent *event,
                              int x, int y)
{
    WmViewport viewport = wm_viewport_fit(platform->window_width,
                                           platform->window_height);
    if (viewport.width <= 0 || viewport.height <= 0) {
        event->type = WM_EVENT_POINTER_LEAVE;
        return;
    }
    event->outside_viewport = !wm_viewport_map_pointer_unbounded(
        viewport, x, y, &event->x, &event->y);
}

static WmKey wm_lookup_key(XKeyEvent *key_event)
{
    KeySym symbol = NoSymbol;
    char text[8] = {0};
    int count = XLookupString(key_event, text, (int)sizeof(text), &symbol, NULL);

    switch (symbol) {
    case XK_Left:      return WM_KEY_LEFT;
    case XK_Right:     return WM_KEY_RIGHT;
    case XK_Up:        return WM_KEY_UP;
    case XK_Down:      return WM_KEY_DOWN;
    case XK_Return:
    case XK_KP_Enter:  return WM_KEY_ENTER;
    case XK_Escape:    return WM_KEY_ESCAPE;
    case XK_BackSpace: return WM_KEY_BACKSPACE;
    case XK_Home:      return WM_KEY_HOME;
    default:           break;
    }

    if (count == 1 && (unsigned char)text[0] >= 32 &&
        (unsigned char)text[0] <= 126) {
        unsigned char character = (unsigned char)text[0];
        if (character >= 'A' && character <= 'Z') {
            character = (unsigned char)(character - 'A' + 'a');
        }
        return (WmKey)character;
    }
    return WM_KEY_UNKNOWN;
}

bool wm_platform_poll(WmPlatform *platform, WmEvent *event)
{
    if (platform == NULL || event == NULL) {
        return false;
    }
    *event = (WmEvent){.type = WM_EVENT_NONE, .key = WM_KEY_UNKNOWN};

    while (XPending(platform->display) > 0) {
        XEvent native_event;
        XNextEvent(platform->display, &native_event);

        switch (native_event.type) {
        case ClientMessage:
            if ((Atom)native_event.xclient.data.l[0] == platform->delete_window) {
                event->type = WM_EVENT_QUIT;
                return true;
            }
            break;
        case DestroyNotify:
            platform->window = 0;
            event->type = WM_EVENT_QUIT;
            return true;
        case ConfigureNotify:
            platform->window_width = native_event.xconfigure.width;
            platform->window_height = native_event.xconfigure.height;
            break;
        case MotionNotify:
            event->type = WM_EVENT_POINTER_MOVE;
            wm_pointer_event(platform, event, native_event.xmotion.x,
                              native_event.xmotion.y);
            return true;
        case EnterNotify:
            event->type = WM_EVENT_POINTER_MOVE;
            wm_pointer_event(platform, event, native_event.xcrossing.x,
                              native_event.xcrossing.y);
            return true;
        case LeaveNotify:
            event->type = WM_EVENT_POINTER_LEAVE;
            return true;
        case FocusOut:
            event->type = WM_EVENT_POINTER_LEAVE;
            event->cancel_capture = true;
            return true;
        case ButtonPress:
        case ButtonRelease:
            if (native_event.xbutton.button != Button1 &&
                native_event.xbutton.button != Button2 &&
                native_event.xbutton.button != Button3) {
                break;
            }
            event->type = native_event.type == ButtonPress ?
                          WM_EVENT_POINTER_DOWN : WM_EVENT_POINTER_UP;
            event->button = (WmPointerButton)native_event.xbutton.button;
            wm_pointer_event(platform, event, native_event.xbutton.x,
                              native_event.xbutton.y);
            return true;
        case KeyPress:
            event->type = WM_EVENT_KEY_DOWN;
            event->key = wm_lookup_key(&native_event.xkey);
            return true;
        default:
            break;
        }
    }
    return false;
}

void wm_platform_begin(WmPlatform *platform, WmColor clear_color)
{
    if (platform == NULL) {
        return;
    }

    platform->quad_count = 0;
    platform->rendering_target = false;
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    EGLint width = platform->window_width;
    EGLint height = platform->window_height;
    eglQuerySurface(platform->egl_display, platform->egl_surface, EGL_WIDTH, &width);
    eglQuerySurface(platform->egl_display, platform->egl_surface, EGL_HEIGHT, &height);
    platform->framebuffer_width = width;
    platform->framebuffer_height = height;
    platform->presentation = wm_viewport_fit(width, height);
    WmViewport content = platform->presentation;
    glViewport(content.x, height - content.y - content.height,
               content.width, content.height);
    glDisable(GL_SCISSOR_TEST);
    glClearColor(0, 0, 0, 1);
    glClear(GL_COLOR_BUFFER_BIT);
    glEnable(GL_SCISSOR_TEST);
    glScissor(content.x, height - content.y - content.height,
              content.width, content.height);
    glClearColor(clear_color.r, clear_color.g, clear_color.b, clear_color.a);
    glClear(GL_COLOR_BUFFER_BIT);
    glDisable(GL_SCISSOR_TEST);
    platform->scissor_enabled = false;
}

static int wm_clamp_clip_edge(float coordinate, float scale, int limit,
                              bool upper)
{
    float value = coordinate * scale;
    if (!isfinite(value) || value <= 0.0f) return 0;
    if (value >= (float)limit) return limit;
    return (int)(upper ? ceilf(value) : floorf(value));
}

void wm_platform_set_clip(WmPlatform *platform, const WmClipRect *rect)
{
    if (!platform) return;
    if (!rect) {
        if (!platform->scissor_enabled) return;
        wm_flush(platform);
        glDisable(GL_SCISSOR_TEST);
        platform->scissor_enabled = false;
        return;
    }
    WmViewport content = platform->presentation;
    float scale_x = (float)content.width / WM_FRAME_WIDTH;
    float scale_y = (float)content.height / WM_FRAME_HEIGHT;
    int x0 = wm_clamp_clip_edge(rect->x, scale_x,
                                 content.width, false);
    int x1 = wm_clamp_clip_edge(rect->x + rect->width, scale_x,
                                 content.width, true);
    int y0 = wm_clamp_clip_edge(rect->y, scale_y,
                                 content.height, false);
    int y1 = wm_clamp_clip_edge(rect->y + rect->height, scale_y,
                                 content.height, true);
    if (x1 < x0) x1 = x0;
    if (y1 < y0) y1 = y0;
    int scissor_x = content.x + x0;
    int scissor_y = platform->framebuffer_height - content.y - y1;
    int scissor_width = x1 - x0;
    int scissor_height = y1 - y0;
    if (platform->scissor_enabled &&
        platform->scissor_x == scissor_x &&
        platform->scissor_y == scissor_y &&
        platform->scissor_width == scissor_width &&
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

static WmVertex wm_vertex(float x, float y, float u, float v, WmColor color)
{
    return (WmVertex){x, y, u, v, color.r, color.g, color.b, color.a};
}

void wm_platform_draw_quad(WmPlatform *platform, const WmQuad *quad)
{
    if (platform == NULL || quad == NULL ||
        quad->width == 0.0f || quad->height == 0.0f) {
        return;
    }

    const WmDrawVertex vertices[4] = {
        {quad->x, quad->y, quad->u0, quad->v0, quad->color},
        {quad->x + quad->width, quad->y, quad->u1, quad->v0, quad->color},
        {quad->x, quad->y + quad->height, quad->u0, quad->v1, quad->color},
        {quad->x + quad->width, quad->y + quad->height,
         quad->u1, quad->v1, quad->color},
    };
    wm_platform_draw_vertices(platform, vertices, quad->texture);
}

void wm_platform_draw_vertices(WmPlatform *platform,
                               const WmDrawVertex corners[4], uint32_t texture_handle)
{
    if (platform == NULL || corners == NULL) return;

    GLuint texture = texture_handle != 0 ? (GLuint)texture_handle : platform->white_texture;
    if (platform->quad_count != 0 &&
        (platform->batch_texture != texture || platform->quad_count == WM_BATCH_QUADS)) {
        wm_flush(platform);
    }
    platform->batch_texture = texture;

    WmVertex *vertices = &platform->vertices[platform->quad_count * WM_VERTICES_PER_QUAD];
    const unsigned order[6] = {0, 1, 3, 0, 3, 2};
    for (size_t index = 0; index < 6; index++) {
        const WmDrawVertex *corner = &corners[order[index]];
        /* FBO storage has the OpenGL bottom-left texture origin, while scene
         * quads use top-left image coordinates. */
        float v = texture == platform->render_texture
            ? 1.0f - corner->v : corner->v;
        vertices[index] = wm_vertex(corner->x, corner->y, corner->u,
                                    v, corner->color);
    }
    platform->quad_count++;
}

/* ES 2.0 has no sampler objects and cannot repeat arbitrary NPOT textures.
 * The material shader applies GX wrap coordinates before sampling textures
 * that remain CLAMP_TO_EDGE at the API level. */
static bool wm_tev_supported(WmPlatform *platform,
                             const WmMaterialQuad *quad)
{
    if (quad->tev_stage_count == 0) return false;
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
         (quad->alpha_compare[0] >> 4) > 7 ||
         quad->alpha_compare[1] > 3);
    for (unsigned stage = 0; stage < quad->tev_stage_count; ++stage) {
        const uint8_t *bytes = quad->tev_stages[stage];
        invalid = invalid || (bytes[8] & 15) > 7 ||
                  (bytes[8] >> 4) > 7 || (bytes[9] & 15) > 7 ||
                  (bytes[9] >> 4) > 7;
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

void wm_platform_prepare_material(WmPlatform *platform,
                                  const WmMaterialQuad *quad)
{
    if (!platform || !quad || quad->texture_count > WM_MATERIAL_TEXTURES) return;
    if (wm_tev_supported(platform, quad)) {
        (void)wm_get_tev_program(platform, quad);
    }
}

void wm_platform_draw_material_quad(WmPlatform *platform,
                                    const WmMaterialQuad *quad)
{
    if (!platform || !quad || quad->texture_count > WM_MATERIAL_TEXTURES) return;
    wm_flush(platform);

    bool tev = wm_tev_supported(platform, quad);
    WmTevProgram *tev_program = tev ? wm_get_tev_program(platform, quad) : NULL;
    if (tev && !tev_program) tev = false;

    if (tev) {
        glUseProgram(tev_program->program);
        glUniform4fv(tev_program->registers_location, 3,
                     &quad->registers[0][0]);
        glUniform4fv(tev_program->konst_location, 4,
                     &quad->konst_colors[0][0]);
        for (unsigned unit = 0; unit < WM_MATERIAL_TEXTURES; ++unit) {
            glActiveTexture((GLenum)(GL_TEXTURE0 + unit));
            glBindTexture(GL_TEXTURE_2D, quad->textures[unit]
                                          ? (GLuint)quad->textures[unit]
                                          : platform->white_texture);
        }
    } else {
        glUseProgram(platform->material_program);
        int texture_count = (int)(quad->texture_count > 2
                                      ? 2 : quad->texture_count);
        glUniform1i(platform->material_texture_count_location, texture_count);
        glUniform4fv(platform->material_registers_location, 3,
                     &quad->registers[0][0]);
        glUniform4fv(platform->material_konst_location, 4,
                     &quad->konst_colors[0][0]);
        bool alpha_test = quad->has_alpha_compare &&
                          !(quad->alpha_compare[0] == 0x77 &&
                            quad->alpha_compare[1] < 2);
        glUniform4f(platform->material_alpha_location,
                    alpha_test ? (float)quad->alpha_compare[0] : -1.0f,
                    (float)quad->alpha_compare[1],
                    (float)quad->alpha_compare[2],
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

    static const GLenum factors[8] = {
        GL_ZERO, GL_ONE, GL_DST_COLOR, GL_ONE_MINUS_DST_COLOR,
        GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA,
        GL_DST_ALPHA, GL_ONE_MINUS_DST_ALPHA
    };
    if (quad->has_blend_mode && quad->blend_mode[0] == 0) {
        glDisable(GL_BLEND);
    } else {
        unsigned source = quad->has_blend_mode ? quad->blend_mode[1] : 4;
        unsigned destination = quad->has_blend_mode ? quad->blend_mode[2] : 5;
        if (source >= 8 || destination >= 8) return;
        glEnable(GL_BLEND);
        glBlendFuncSeparate(factors[source], factors[destination],
                            GL_ONE, GL_ONE_MINUS_SRC_ALPHA);
    }

    WmMaterialGpuVertex vertices[6];
    static const unsigned order[6] = {0, 1, 3, 0, 3, 2};
    for (size_t index = 0; index < 6; index++) {
        const WmMaterialVertex *source = &quad->vertices[order[index]];
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
    glBufferData(GL_ARRAY_BUFFER, (GLsizeiptr)sizeof(vertices),
                 vertices, GL_STREAM_DRAW);
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
    glDrawArrays(GL_TRIANGLES, 0, 6);
}

void wm_platform_end(WmPlatform *platform)
{
    if (platform == NULL) {
        return;
    }
    if (!platform->rendering_target && platform->fade_alpha > 0.0f) {
        wm_platform_set_clip(platform, NULL);
        WmQuad cover = {
            .x = 0, .y = 0, .width = WM_FRAME_WIDTH,
            .height = WM_FRAME_HEIGHT,
            .u0 = 0, .v0 = 0, .u1 = 1, .v1 = 1,
            .color = {0, 0, 0, platform->fade_alpha}, .texture = 0
        };
        wm_platform_draw_quad(platform, &cover);
    }
    wm_flush(platform);
    if (platform->rendering_target) {
        glBindFramebuffer(GL_FRAMEBUFFER, 0);
        platform->rendering_target = false;
        return;
    }
    if (!eglSwapBuffers(platform->egl_display, platform->egl_surface) &&
        !platform->swap_failure_reported) {
        wm_report_egl_error("frame presentation");
        platform->swap_failure_reported = true;
    }
}

void wm_platform_set_fade_alpha(WmPlatform *platform, float alpha)
{
    if (platform) platform->fade_alpha = fminf(1.0f, fmaxf(0.0f, alpha));
}

uint32_t wm_platform_create_render_texture(WmPlatform *platform)
{
    if (!platform || platform->render_texture != 0) return 0;
    GLint limit = 0;
    glGetIntegerv(GL_MAX_TEXTURE_SIZE, &limit);
    if (WM_FRAME_WIDTH > limit || WM_FRAME_HEIGHT > limit) return 0;

    GLuint texture = wm_upload_texture(WM_FRAME_WIDTH, WM_FRAME_HEIGHT, NULL);
    if (!texture) return 0;
    GLuint framebuffer = 0;
    glGenFramebuffers(1, &framebuffer);
    if (!framebuffer) {
        glDeleteTextures(1, &texture);
        return 0;
    }
    GLint prior = 0;
    glGetIntegerv(GL_FRAMEBUFFER_BINDING, &prior);
    glBindFramebuffer(GL_FRAMEBUFFER, framebuffer);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0,
                           GL_TEXTURE_2D, texture, 0);
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
                              WmColor clear_color)
{
    if (!platform || texture == 0 || texture != platform->render_texture ||
        platform->render_framebuffer == 0) return false;
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
                                    const uint8_t *rgba)
{
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
    return (uint32_t)wm_upload_texture(width, height, rgba);
}

void wm_platform_destroy_texture(WmPlatform *platform, uint32_t texture)
{
    if (platform == NULL || texture == 0 || texture == platform->white_texture) {
        return;
    }
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
