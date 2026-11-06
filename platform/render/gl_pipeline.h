/* Renderer-private programs over shared OpenGL transport. No module calls another. */
#ifndef RENDER_GL_PIPELINE_H
#define RENDER_GL_PIPELINE_H
#include "gl_api.h"
#include "renderer.h"
#include <stdint.h>

typedef struct RenderProgram {
    GLuint program;
    GLint uniformVram, uniformGlyphs, uniformPortPages, uniformHdAtlas, uniformPass, uniformTarget;
    GLint uniformDepthPull, uniformDepthPass, uniformOrigin;
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
extern const RenderPipeline ps1Pipeline, pcPipeline, n64Pipeline;
const RenderPipeline *RenderGlPipeline(RendererId id);
GLuint RenderGlCompile(GLenum type, const char *source);
int RenderGlProgramInit(RenderProgram *s, const char *vertexSource, const char *fragmentSource);
void RenderGlProgramDestroy(RenderProgram *s);
void RenderGlPresentDirect(const RenderPresent *p, GLenum filter);
#endif
