/* Renderer-private programs over shared OpenGL transport. No module calls another. */
#ifndef RENDER_GL_PIPELINE_H
#define RENDER_GL_PIPELINE_H
#include "gl_api.h"
#include "renderer.h"
#include "../psx/gpu.h"
#include <stdint.h>

#define RENDER_MAX_SCALE 12
#define MAX_WIDE_MARGIN 400

typedef struct RenderProgram {
    GLuint program;
    GLint uniformVram, uniformGlyphs, uniformPortPages, uniformHdAtlas, uniformPass, uniformTarget;
    GLint uniformDepthPull, uniformDepthPass, uniformOrigin, uniformPointSize, uniformNativeGrid;
} RenderProgram;
/* Immutable source identity and crop. Revision changes whenever scene pixels change. */
typedef struct RenderPresent {
    GLuint texture, framebuffer;
    uint64_t revision;
    int scale, race, left, top, right, bottom, x0, y0, x1, y1;
    GLuint hdrTexture, hdrMask;
    int hdrWidth; /* Full wide picture, shared by the window crop and recorder. */
} RenderPresent;
/* Source pixels only. Each effect adds its own settings to this cache key. */
typedef struct RenderPictureKey {
    GLuint texture;
    uint64_t revision;
    int left, top, right, bottom;
} RenderPictureKey;
RenderPictureKey RenderGlPictureKey(const RenderPresent *picture, GLuint texture);
int RenderGlPictureMatches(const RenderPictureKey *key, const RenderPresent *picture, GLuint texture);
void RenderGlBeginFullscreen(GLuint program, GLuint array);
int RenderGlResizeTarget(GLuint *texture, GLuint *framebuffer, int width, int height,
                         GLenum format, GLenum type, GLenum filter);

/* Current draw area and shared operations available to a native draw path.
   Coordinates are logical unless explicitly marked as framebuffer pixels. */
typedef struct RenderNativeTarget {
    int left, top, right, bottom, x, width, scale;
    int pixelWidth, pixelHeight;
    float targetWidth, targetHeight, origin, scaleX, scaleY, rasterX, rasterY;
    GLuint vram;
    const uint16_t (*vramLow)[VRAM_WIDTH], (*vramHigh)[VRAM_WIDTH];
    uint64_t pass; /* advances at each draw-area clear, independent of picture revision */
    void (*begin)(int flushAll);
    void (*bind)(int uploadVram);
    void (*changed)(float x, float y, float width, float height);
    void (*markDrawn)(float left, float top, float right, float bottom);
    void (*point)(const GpuDrawState *, const GpuVertex *, unsigned shade);
} RenderNativeTarget;
typedef struct RenderPipeline {
    RenderProgram *(*scene)(void);
    void (*present)(const void *profile, const RenderPresent *);
    void (*destroy)(void);
    /* Bind private uniforms/resources after the common scene bindings. */
    void (*bind)(const void *profile, int hdrActive);
    int (*submitNative)(const void *profile, const RenderNativeTarget *, const GpuDrawState *,
                        const GpuVertex *, int count);
    void (*flushNative)(const RenderNativeTarget *);
} RenderPipeline;
extern const RenderPipeline ps1Pipeline, pcPipeline, n64Pipeline, saturnPipeline, modernPipeline;
const RenderPipeline *RenderGlPipeline(RendererId id);
GLuint RenderGlCompile(GLenum type, const char *source);
int RenderGlProgramInit(RenderProgram *s, const char *vertexSource, const char *fragmentSource);
void RenderGlProgramDestroy(RenderProgram *s);
void RenderGlPresentDirect(const RenderPresent *p, GLenum filter);
/* Seed auxiliary colour from SDR and clear coverage, without changing its owner. */
void RenderGlSeedHdr(GLuint source, GLuint target, int x, int y, int w, int h);
void RenderGlSeedShutdown(void);
#endif
