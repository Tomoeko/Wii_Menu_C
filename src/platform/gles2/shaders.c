#include "shaders.h"

#include <stddef.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

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
    "    if (kind < 0.5)\n"
    "        return false;\n"
    "    if (kind > 6.5)\n"
    "        return true;\n"
    "    if (kind < 1.5)\n"
    "        return value < reference;\n"
    "    if (kind < 2.5)\n"
    "        return value == reference;\n"
    "    if (kind < 3.5)\n"
    "        return value <= reference;\n"
    "    if (kind < 4.5)\n"
    "        return value > reference;\n"
    "    if (kind < 5.5)\n"
    "        return value != reference;\n"
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
    "        if (!passes)\n"
    "            discard;\n"
    "    }\n"
    "    gl_FragColor = pixel;\n"
    "}\n";

typedef struct WmShaderText {
    char *data;
    size_t length;
    size_t capacity;
    bool failed;
} WmShaderText;

static void wm_emit(WmShaderText *text, const char *format, ...) {
    if (text->failed) {
        return;
    }
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
    vsnprintf(text->data + text->length, text->capacity - text->length, format, args);
    text->length += (size_t)count;
    va_end(args);
}

static void wm_swizzle(uint8_t pattern, char output[5]) {
    static const char channels[] = "rgba";
    for (unsigned index = 0; index < 4; ++index) {
        output[index] = channels[(pattern >> (index * 2)) & 3];
    }
    output[4] = '\0';
}

static void wm_konst_expression(unsigned selector, bool alpha, char output[64]) {
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
                                    const uint8_t stage[16]) {
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
        wm_emit(text, "    c%u = cd%u + ((%s %s %s) ? cc%u : vec3(0.0));\n", index,
                index, packed_a, compare, packed_b, index);
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
                                    const uint8_t stage[16]) {
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
        wm_emit(text, "    a%u = ((ad%u %s mix(aa%u, ab%u, ac%u)) + %s) * %s;\n", index,
                index, kind == 1 ? "-" : "+", index, index, index,
                bias[(stage[10] >> 4) & 3], scale[stage[10] >> 6]);
    }
    if (stage[11] & 1) {
        wm_emit(text, "    a%u = clamp(a%u, 0.0, 1.0);\n", index, index);
    }
}

static void wm_emit_tev_stage(WmShaderText *text, const WmTevKey *key, unsigned index) {
    static const char *const color_inputs[16] = {
        "p.rgb",     "vec3(p.a)",  "r0.rgb",  "vec3(r0.a)",  "r1.rgb",  "vec3(r1.a)",
        "r2.rgb",    "vec3(r2.a)", "tex.rgb", "vec3(tex.a)", "ras.rgb", "vec3(ras.a)",
        "vec3(1.0)", "vec3(0.5)",  "kcolor",  "vec3(0.0)"};
    static const char *const alpha_inputs[8] = {"p.a",   "r0.a",  "r1.a",   "r2.a",
                                                "tex.a", "ras.a", "kalpha", "0.0"};
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
        wm_emit(text, "    tex = texture2D(t%u, wrapUV(texUV%u, vec2(%u.0, %u.0)));\n",
                slot, coord, key->wrap_s[slot], key->wrap_t[slot]);
    } else {
        wm_emit(text, "    tex = vec4(1.0);\n");
    }
    wm_emit(text, "    tex = tex.%s;\n", tex_swizzle);
    wm_emit(text, "    ras = %s;\n",
            stage[1] == 255 || stage[1] == 6 || stage[1] == 7 ? "vec4(0.0)" : "raster");
    wm_emit(text, "    ras = ras.%s;\n", ras_swizzle);
    wm_emit(text, "    kcolor = %s;\n", kcolor);
    wm_emit(text, "    kalpha = %s;\n", kalpha);

    wm_emit(text, "    vec3 ca%u = %s;\n", index, color_inputs[stage[4] & 15]);
    wm_emit(text, "    vec3 cb%u = %s;\n", index, color_inputs[stage[4] >> 4]);
    wm_emit(text, "    vec3 cc%u = %s;\n", index, color_inputs[stage[5] & 15]);
    wm_emit(text, "    vec3 cd%u = %s;\n", index, color_inputs[stage[5] >> 4]);
    wm_emit(text, "    float aa%u = %s;\n", index, alpha_inputs[stage[8] & 7]);
    wm_emit(text, "    float ab%u = %s;\n", index, alpha_inputs[(stage[8] >> 4) & 7]);
    wm_emit(text, "    float ac%u = %s;\n", index, alpha_inputs[stage[9] & 7]);
    wm_emit(text, "    float ad%u = %s;\n", index, alpha_inputs[(stage[9] >> 4) & 7]);
    wm_emit_color_operation(text, index, stage);
    wm_emit_alpha_operation(text, index, stage);
    wm_emit(text, "    %s.rgb = c%u;\n", destinations[(stage[7] >> 1) & 3], index);
    wm_emit(text, "    %s.a = a%u;\n", destinations[(stage[11] >> 1) & 3], index);
}

static void wm_alpha_condition(char output[96], uint8_t kind, uint8_t reference) {
    static const char *const comparisons[8] = {
        "", "<", "==", "<=", ">", "!=", ">=", ""};
    if (kind == 0 || kind > 7) {
        snprintf(output, 96, "false");
    } else if (kind == 7) {
        snprintf(output, 96, "true");
    } else {
        snprintf(output, 96, "tevAlpha8(p.a) %s %u.0", comparisons[kind], reference);
    }
}

static char *wm_tev_fragment_source(const WmTevKey *key) {
    WmShaderText text = {0};
    wm_emit(&text, "precision WM_UV_PRECISION float;\n"
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
                   "    if (mode > 1.5)\n"
                   "        return 1.0 - abs(mod(coordinate, 2.0) - 1.0);\n"
                   "    if (mode > 0.5)\n"
                   "        return fract(coordinate);\n"
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
        wm_alpha_condition(first, key->alpha_compare[0] & 15, key->alpha_compare[2]);
        wm_alpha_condition(second, key->alpha_compare[0] >> 4, key->alpha_compare[3]);
        static const char *const operators[4] = {"&&", "||", "!=", "=="};
        unsigned operation = key->alpha_compare[1] < 4 ? key->alpha_compare[1] : 0;
        wm_emit(&text,
                "    if (!((%s) %s (%s)))\n"
                "        discard;\n",
                first, operators[operation], second);
    }
    wm_emit(&text, "    gl_FragColor = p;\n}\n");
    if (text.failed) {
        free(text.data);
        return NULL;
    }
    return text.data;
}

static GLuint wm_compile_shader(GLenum type, const char *source,
                                const char *precision_directive) {
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

GLuint wm_gles2_create_tev_vertex_shader(bool fragment_highp) {
    /* The vertex and fragment shaders must agree on varying precision. */
    const char *directive = fragment_highp ? "#define WM_UV_PRECISION highp\n"
                                           : "#define WM_UV_PRECISION mediump\n";
    return wm_compile_shader(GL_VERTEX_SHADER, wm_tev_vertex_source, directive);
}

GLuint wm_gles2_create_quad_program(void) {
    GLint range[2] = {0, 0};
    GLint precision = 0;
    glGetShaderPrecisionFormat(GL_FRAGMENT_SHADER, GL_HIGH_FLOAT, range, &precision);
    const char *precision_directive = precision > 0
                                          ? "#define WM_UV_PRECISION highp\n"
                                          : "#define WM_UV_PRECISION mediump\n";

    GLuint vertex_shader =
        wm_compile_shader(GL_VERTEX_SHADER, wm_vertex_source, precision_directive);
    if (vertex_shader == 0) {
        return 0;
    }

    GLuint fragment_shader =
        wm_compile_shader(GL_FRAGMENT_SHADER, wm_fragment_source, precision_directive);
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

GLuint wm_gles2_create_material_program(void) {
    GLint range[2] = {0, 0};
    GLint precision = 0;
    glGetShaderPrecisionFormat(GL_FRAGMENT_SHADER, GL_HIGH_FLOAT, range, &precision);
    const char *directive = precision > 0 ? "#define WM_UV_PRECISION highp\n"
                                          : "#define WM_UV_PRECISION mediump\n";
    GLuint vertex =
        wm_compile_shader(GL_VERTEX_SHADER, wm_material_vertex_source, directive);
    if (!vertex) {
        return 0;
    }
    GLuint fragment =
        wm_compile_shader(GL_FRAGMENT_SHADER, wm_material_fragment_source, directive);
    if (!fragment) {
        glDeleteShader(vertex);
        return 0;
    }
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
    if (linked) {
        return program;
    }
    char log[1024] = {0};
    glGetProgramInfoLog(program, (GLsizei)sizeof(log), NULL, log);
    fprintf(stderr, "GLES2: material program link failed: %s\n", log);
    glDeleteProgram(program);
    return 0;
}

WmTevProgram *wm_gles2_get_tev_program(WmTevProgram **programs, GLuint vertex_shader,
                                       bool fragment_highp,
                                       const WmMaterialQuad *quad) {
    if (!programs || !quad || !vertex_shader || quad->tev_stage_count == 0 ||
        quad->tev_stage_count > WM_ES2_TEV_STAGES) {
        return NULL;
    }

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
        memcpy(key.alpha_compare, quad->alpha_compare, sizeof(key.alpha_compare));
    }

    for (WmTevProgram *entry = *programs; entry; entry = entry->next) {
        if (memcmp(&entry->key, &key, sizeof(key)) == 0) {
            return entry->program ? entry : NULL;
        }
    }

    /* The first request compiles one GLSL ES 1.00 program. Preparing scene
     * materials during loading keeps that work outside the render loop. */
    WmTevProgram *entry = calloc(1, sizeof(*entry));
    if (!entry) {
        return NULL;
    }
    entry->key = key;
    entry->next = *programs;
    *programs = entry;
    char *source = wm_tev_fragment_source(&key);
    if (!source) {
        return NULL;
    }
    const char *directive = fragment_highp ? "#define WM_UV_PRECISION highp\n"
                                           : "#define WM_UV_PRECISION mediump\n";
    GLuint fragment = wm_compile_shader(GL_FRAGMENT_SHADER, source, directive);
    free(source);
    if (!fragment) {
        return NULL;
    }

    GLuint program = glCreateProgram();
    if (!program) {
        glDeleteShader(fragment);
        return NULL;
    }
    glAttachShader(program, vertex_shader);
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
    glUniform2f(glGetUniformLocation(program, "u_frame_size"), (float)WM_FRAME_WIDTH,
                (float)WM_FRAME_HEIGHT);
    for (unsigned index = 0; index < WM_MATERIAL_TEXTURES; ++index) {
        char name[4];
        snprintf(name, sizeof(name), "t%u", index);
        glUniform1i(glGetUniformLocation(program, name), (GLint)index);
    }
    return entry;
}
