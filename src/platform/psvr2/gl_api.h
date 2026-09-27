#ifndef WII_MENU_PSVR2_GL_API_H
#define WII_MENU_PSVR2_GL_API_H

/* Narrow first-party declaration of the stable EGL 1.4 / GLES 2.0 ABI.
 * Device libraries are supplied by the PSVR2 build system, not vendored.
 * Rendering uses core ES2. Native scanout additionally needs the checked
 * EGL_EXT_image_dma_buf_import and GL_OES_EGL_image extensions. */
#include <stddef.h>
#include <stdint.h>

typedef void *EGLDisplay;
typedef void *EGLContext;
typedef void *EGLSurface;
typedef void *EGLConfig;
typedef void *EGLImageKHR;
typedef void *EGLNativeDisplayType;
typedef uint32_t EGLBoolean;
typedef uint32_t EGLenum;
typedef int32_t EGLint;
typedef uint32_t GLenum;
typedef uint32_t GLuint;
typedef int32_t GLint;
typedef int32_t GLsizei;
typedef uint32_t GLbitfield;
typedef uint8_t GLboolean;
typedef char GLchar;
typedef float GLfloat;
typedef ptrdiff_t GLintptr;
typedef ptrdiff_t GLsizeiptr;

#define EGL_NO_DISPLAY ((EGLDisplay)0)
#define EGL_NO_CONTEXT ((EGLContext)0)
#define EGL_NO_SURFACE ((EGLSurface)0)
#define EGL_DEFAULT_DISPLAY ((EGLNativeDisplayType)0)
#define EGL_FALSE 0
#define EGL_EXTENSIONS 0x3055
#define EGL_NONE 0x3038
#define EGL_SURFACE_TYPE 0x3033
#define EGL_RENDERABLE_TYPE 0x3040
#define EGL_OPENGL_ES2_BIT 4
#define EGL_RED_SIZE 0x3024
#define EGL_GREEN_SIZE 0x3023
#define EGL_BLUE_SIZE 0x3022
#define EGL_ALPHA_SIZE 0x3021
#define EGL_CONTEXT_CLIENT_VERSION 0x3098
#define EGL_OPENGL_ES_API 0x30A0
#define EGL_WIDTH 0x3057
#define EGL_HEIGHT 0x3056
#define EGL_LINUX_DMA_BUF_EXT 0x3270
#define EGL_LINUX_DRM_FOURCC_EXT 0x3271
#define EGL_DMA_BUF_PLANE0_FD_EXT 0x3272
#define EGL_DMA_BUF_PLANE0_OFFSET_EXT 0x3273
#define EGL_DMA_BUF_PLANE0_PITCH_EXT 0x3274
#define GL_FALSE 0
#define GL_TRUE 1
#define GL_NO_ERROR 0
#define GL_ZERO 0
#define GL_ONE 1
#define GL_TRIANGLES 4
#define GL_FLOAT 0x1406
#define GL_UNSIGNED_BYTE 0x1401
#define GL_SRC_ALPHA 0x0302
#define GL_ONE_MINUS_SRC_ALPHA 0x0303
#define GL_DST_ALPHA 0x0304
#define GL_ONE_MINUS_DST_ALPHA 0x0305
#define GL_DST_COLOR 0x0306
#define GL_ONE_MINUS_DST_COLOR 0x0307
#define GL_BLEND 0x0BE2
#define GL_CULL_FACE 0x0B44
#define GL_DEPTH_TEST 0x0B71
#define GL_SCISSOR_TEST 0x0C11
#define GL_COLOR_BUFFER_BIT 0x4000
#define GL_TEXTURE_2D 0x0DE1
#define GL_RGBA 0x1908
#define GL_TEXTURE_MAG_FILTER 0x2800
#define GL_TEXTURE_MIN_FILTER 0x2801
#define GL_TEXTURE_WRAP_S 0x2802
#define GL_TEXTURE_WRAP_T 0x2803
#define GL_LINEAR 0x2601
#define GL_CLAMP_TO_EDGE 0x812F
#define GL_TEXTURE0 0x84C0
#define GL_UNPACK_ALIGNMENT 0x0CF5
#define GL_MAX_TEXTURE_SIZE 0x0D33
#define GL_EXTENSIONS 0x1F03
#define GL_ARRAY_BUFFER 0x8892
#define GL_STREAM_DRAW 0x88E0
#define GL_VERTEX_SHADER 0x8B31
#define GL_FRAGMENT_SHADER 0x8B30
#define GL_COMPILE_STATUS 0x8B81
#define GL_LINK_STATUS 0x8B82
#define GL_HIGH_FLOAT 0x8DF2
#define GL_FRAMEBUFFER 0x8D40
#define GL_RENDERBUFFER 0x8D41
#define GL_FRAMEBUFFER_BINDING 0x8CA6
#define GL_COLOR_ATTACHMENT0 0x8CE0
#define GL_FRAMEBUFFER_COMPLETE 0x8CD5

EGLDisplay eglGetDisplay(EGLNativeDisplayType native_display);
EGLBoolean eglInitialize(EGLDisplay display, EGLint *major, EGLint *minor);
EGLBoolean eglBindAPI(EGLenum api);
EGLBoolean eglChooseConfig(EGLDisplay display, const EGLint *attributes,
                           EGLConfig *config, EGLint size, EGLint *count);
EGLContext eglCreateContext(EGLDisplay display, EGLConfig config,
                            EGLContext share, const EGLint *attributes);
EGLBoolean eglMakeCurrent(EGLDisplay display, EGLSurface draw,
                          EGLSurface read, EGLContext context);
EGLContext eglGetCurrentContext(void);
EGLBoolean eglDestroyContext(EGLDisplay display, EGLContext context);
EGLBoolean eglDestroySurface(EGLDisplay display, EGLSurface surface);
EGLBoolean eglTerminate(EGLDisplay display);
EGLint eglGetError(void);
const char *eglQueryString(EGLDisplay display, EGLint name);
void (*eglGetProcAddress(const char *name))(void);

void glActiveTexture(GLenum texture);
void glAttachShader(GLuint program, GLuint shader);
void glBindAttribLocation(GLuint program, GLuint index, const GLchar *name);
void glBindBuffer(GLenum target, GLuint buffer);
void glBindFramebuffer(GLenum target, GLuint framebuffer);
void glBindRenderbuffer(GLenum target, GLuint renderbuffer);
void glBindTexture(GLenum target, GLuint texture);
void glBlendFuncSeparate(GLenum source_rgb, GLenum destination_rgb,
                          GLenum source_alpha, GLenum destination_alpha);
void glBufferData(GLenum target, GLsizeiptr size, const void *data, GLenum usage);
void glBufferSubData(GLenum target, GLintptr offset, GLsizeiptr size, const void *data);
GLenum glCheckFramebufferStatus(GLenum target);
void glClear(GLbitfield mask);
void glClearColor(GLfloat red, GLfloat green, GLfloat blue, GLfloat alpha);
void glCompileShader(GLuint shader);
GLuint glCreateProgram(void);
GLuint glCreateShader(GLenum type);
void glDeleteBuffers(GLsizei count, const GLuint *buffers);
void glDeleteFramebuffers(GLsizei count, const GLuint *framebuffers);
void glDeleteRenderbuffers(GLsizei count, const GLuint *renderbuffers);
void glDeleteProgram(GLuint program);
void glDeleteShader(GLuint shader);
void glDeleteTextures(GLsizei count, const GLuint *textures);
void glDisable(GLenum capability);
void glDisableVertexAttribArray(GLuint index);
void glDrawArrays(GLenum mode, GLint first, GLsizei count);
void glEnable(GLenum capability);
void glEnableVertexAttribArray(GLuint index);
void glFinish(void);
void glFramebufferTexture2D(GLenum target, GLenum attachment,
                             GLenum texture_target, GLuint texture, GLint level);
void glFramebufferRenderbuffer(GLenum target, GLenum attachment,
                               GLenum renderbuffer_target, GLuint renderbuffer);
void glGenBuffers(GLsizei count, GLuint *buffers);
void glGenFramebuffers(GLsizei count, GLuint *framebuffers);
void glGenRenderbuffers(GLsizei count, GLuint *renderbuffers);
void glGenTextures(GLsizei count, GLuint *textures);
GLenum glGetError(void);
const unsigned char *glGetString(GLenum name);
void glGetIntegerv(GLenum name, GLint *value);
void glGetProgramInfoLog(GLuint program, GLsizei size, GLsizei *length, GLchar *log);
void glGetProgramiv(GLuint program, GLenum name, GLint *value);
void glGetShaderInfoLog(GLuint shader, GLsizei size, GLsizei *length, GLchar *log);
void glGetShaderPrecisionFormat(GLenum shader_type, GLenum precision_type,
                                GLint *range, GLint *precision);
void glGetShaderiv(GLuint shader, GLenum name, GLint *value);
GLint glGetUniformLocation(GLuint program, const GLchar *name);
void glLinkProgram(GLuint program);
void glPixelStorei(GLenum name, GLint value);
void glScissor(GLint x, GLint y, GLsizei width, GLsizei height);
void glShaderSource(GLuint shader, GLsizei count, const GLchar *const *strings,
                     const GLint *lengths);
void glTexImage2D(GLenum target, GLint level, GLint internal_format,
                  GLsizei width, GLsizei height, GLint border, GLenum format,
                  GLenum type, const void *pixels);
void glTexParameteri(GLenum target, GLenum name, GLint value);
void glUniform1i(GLint location, GLint value);
void glUniform2f(GLint location, GLfloat first, GLfloat second);
void glUniform4f(GLint location, GLfloat first, GLfloat second,
                  GLfloat third, GLfloat fourth);
void glUniform4fv(GLint location, GLsizei count, const GLfloat *values);
void glUseProgram(GLuint program);
void glVertexAttribPointer(GLuint index, GLint size, GLenum type,
                            GLboolean normalized, GLsizei stride, const void *pointer);
void glViewport(GLint x, GLint y, GLsizei width, GLsizei height);

#endif
