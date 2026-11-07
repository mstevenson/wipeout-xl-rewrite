/* Renderer-private programs over shared OpenGL transport. No module calls another. */
#ifndef RENDER_GL_PIPELINE_H
#define RENDER_GL_PIPELINE_H
#include "gl_api.h"
#include "renderer.h"
#include <stdint.h>

#define RENDER_MAX_SCALE 12

typedef struct RenderProgram {
    GLuint program;
    GLint uniformVram, uniformGlyphs, uniformPortPages, uniformHdAtlas, uniformPass, uniformTarget;
    GLint uniformDepthPull, uniformDepthPass, uniformOrigin, uniformPointSize;
} RenderProgram;
/* Immutable source identity and crop. Revision changes whenever scene pixels change. */
typedef struct RenderPresent {
    GLuint texture, framebuffer;
    uint64_t revision;
    int scale, race, left, top, right, bottom, x0, y0, x1, y1;
} RenderPresent;
typedef struct RenderPipeline {
    RenderProgram *(*scene)(void);
    void (*present)(const void *profile, const RenderPresent *);
    void (*destroy)(void);
} RenderPipeline;
extern const RenderPipeline ps1Pipeline, pcPipeline, n64Pipeline, saturnPipeline;
const RenderPipeline *RenderGlPipeline(RendererId id);
GLuint RenderGlCompile(GLenum type, const char *source);
int RenderGlProgramInit(RenderProgram *s, const char *vertexSource, const char *fragmentSource);
void RenderGlProgramDestroy(RenderProgram *s);
void RenderGlPresentDirect(const RenderPresent *p, GLenum filter);
void RenderSaturnPixels(const uint8_t *rgba, int width, int height, int margin,
    int left, int top, int right, int bottom, int targetW, int originX, int scale);
struct GpuDrawState;
struct GpuVertex;
struct SaturnNativePoly;
int RenderSaturnQuadsPending(void);
int RenderSaturnQuad(const struct SaturnNativePoly *p,const struct GpuVertex *v,
    const struct GpuDrawState *state,uint64_t revision,int depth,int scale,int left,int top,int right,int bottom);
void RenderSaturnFlushQuads(GLuint vram,int targetW,int targetH,int originX,int scale,
    int left,int top,int right,int bottom);
void RenderSaturnBackground(const uint8_t *pixels, const uint16_t *palette, int width, int height,
    uint64_t revision, const struct GpuDrawState *state, int originalScroll, int left, int top, int right, int bottom,
    float cx, float cy, int targetW, int targetH, int originX, int scale);
#endif
