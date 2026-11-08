#include "gl_pipeline.h"
#include <stdlib.h>

#define RENDER_POSITION "    vec2 pixel = position.xy;\n"
#define RENDER_TEXCOORD "    vTexel = vec3(texel, 1.0);\n"
#define RENDER_VERTEX_COLOUR "    return c;\n"
#define RENDER_OUTPUT_COLOUR "vec3 OutputColour(vec3 c) { return c; }\n"
#define RENDER_TINT "vec3 TextureTint() { return VertexColour() * ((mode.y & 66) != 0 && ps1Light == 0 ? 1.0 : 255.0 / 128.0); }\n"
#define RENDER_FILTER \
    "#define FILTER_TAPS 4\n" \
    "vec2 FilterPosition(vec2 t) { return t - 0.5; }\n" \
    "vec2 FilterHdPosition(vec2 t) { return t * vec2(hd.zw) / vec2(hdTexels.zw) - 0.5; }\n" \
    "vec2 FilterFraction(vec2 f) { return f; }\n" \
    "ivec2 FilterCorner(int i, vec2 f) { return ivec2(i & 1, i >> 1); }\n" \
    "float FilterWeight(int i, vec2 f) { vec2 w = mix(1.0 - f, f, vec2(FilterCorner(i, f))); return w.x * w.y; }\n"

#define PC_DITHER \
    "float Dither(ivec2 p) {\n" \
    "    const int matrix[16] = int[16](0, 8, 2, 10, 12, 4, 14, 6, 3, 11, 1, 9, 15, 7, 13, 5);\n" \
    "    return (float(matrix[(p.y & 3) * 4 + (p.x & 3)]) + 0.5) / 16.0;\n" \
    "}\n"

static const char *pcVertexSource =
#include "shaders/scene_vertex.h"
;
static const char *pcFragmentSource =
#include "shaders/material_fragment.h"
;
static RenderProgram pcProgram;

static RenderProgram *PcScene(void)
{
    if (!RenderGlProgramInit(&pcProgram, pcVertexSource, pcFragmentSource)) abort();
    return &pcProgram;
}

static const char *pcDisplayVertex =
    "void main() {\n"
    "    vec2 p = vec2(float((gl_VertexID << 1) & 2), float(gl_VertexID & 2));\n"
    "    gl_Position = vec4(p * 2.0 - 1.0, 0.0, 1.0);\n"
    "}\n";
static const char *pcDisplayFragment =
    "#ifdef GL_ES\n"
    "precision highp float;\n"
    "precision highp int;\n"
    "#endif\n"
    "uniform sampler2D source;\n"
    "uniform vec2 origin;\n"
    "out vec4 fragment;\n"
    PC_DITHER
    "void main() {\n"
    "    ivec2 p = ivec2(origin + gl_FragCoord.xy);\n"
    "    vec4 c = texelFetch(source, p, 0);\n"
    "    vec3 levels = vec3(31.0, 63.0, 31.0);\n"
    "    fragment = vec4(floor(c.rgb * levels + Dither(p)) / levels, c.a);\n"
    "}\n";

static GLuint displayProgram, displayArray, displayTexture, displayFramebuffer;
static GLint displayOrigin;
static struct {
    GLuint source;
    uint64_t revision;
    int x, y, w, h;
} displayCache;

static void PcDisplayInit(void)
{
    if (displayProgram) return;
    GLint linked;
    GLuint vs = RenderGlCompile(GL_VERTEX_SHADER, pcDisplayVertex);
    GLuint fs = RenderGlCompile(GL_FRAGMENT_SHADER, pcDisplayFragment);
    displayProgram = glCreateProgram();
    glAttachShader(displayProgram, vs);
    glAttachShader(displayProgram, fs);
    glLinkProgram(displayProgram);
    glDeleteShader(vs);
    glDeleteShader(fs);
    glGetProgramiv(displayProgram, GL_LINK_STATUS, &linked);
    if (!linked) { SDL_Log("PC RGB565 program failed to link"); abort(); }
    displayOrigin = glGetUniformLocation(displayProgram, "origin");
    glGenVertexArrays(1, &displayArray);
    glGenFramebuffers(1, &displayFramebuffer);
    glGenTextures(1, &displayTexture);
}

static void PcQuantizePicture(const RenderPresent *p, int w, int h)
{
    if (displayCache.source == p->texture && displayCache.revision == p->revision &&
        displayCache.x == p->left && displayCache.y == p->top && displayCache.w == w && displayCache.h == h) return;

    GLint destination, previousProgram;
    glGetIntegerv(GL_DRAW_FRAMEBUFFER_BINDING, &destination);
    glGetIntegerv(GL_CURRENT_PROGRAM, &previousProgram);
    PcDisplayInit();
    glActiveTexture(GL_TEXTURE0);
    glBindFramebuffer(GL_DRAW_FRAMEBUFFER, displayFramebuffer);
    if (displayCache.w != w || displayCache.h != h) {
        glBindTexture(GL_TEXTURE_2D, displayTexture);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, w, h, 0, GL_RGBA, GL_UNSIGNED_BYTE, NULL);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
        glFramebufferTexture2D(GL_DRAW_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, displayTexture, 0);
        if (glCheckFramebufferStatus(GL_DRAW_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE) {
            SDL_Log("PC RGB565 framebuffer incomplete");
            abort();
        }
    }
    glBindTexture(GL_TEXTURE_2D, p->texture);
    glUseProgram(displayProgram);
    glUniform2f(displayOrigin, (float)p->left, (float)p->top);
    glBindVertexArray(displayArray);
    glDisable(GL_SCISSOR_TEST);
    glDisable(GL_DEPTH_TEST);
    glDisable(GL_BLEND);
    glDisable(GL_DITHER);
    glViewport(0, 0, w, h);
    glDrawArrays(GL_TRIANGLES, 0, 3);
    glUseProgram((GLuint)previousProgram);
    glBindFramebuffer(GL_DRAW_FRAMEBUFFER, (GLuint)destination);
    displayCache.source = p->texture;
    displayCache.revision = p->revision;
    displayCache.x = p->left; displayCache.y = p->top;
    displayCache.w = w; displayCache.h = h;
}

static void PcPresent(const void *profile, const RenderPresent *p)
{
    const PcRenderSettings *settings = profile;
    RenderPresent shown = *p;
    int w = p->right - p->left, h = p->bottom - p->top;
    if (settings->rgb565 && w > 0 && h > 0) {
        PcQuantizePicture(p, w, h);
        shown.framebuffer = displayFramebuffer;
        shown.left = shown.top = 0;
        shown.right = w; shown.bottom = h;
    }
    RenderGlPresentDirect(&shown, settings->resample ? GL_LINEAR : GL_NEAREST);
}

static void PcDestroy(void)
{
    RenderGlProgramDestroy(&pcProgram);
    if (displayProgram) glDeleteProgram(displayProgram);
    if (displayArray) glDeleteVertexArrays(1, &displayArray);
    if (displayFramebuffer) glDeleteFramebuffers(1, &displayFramebuffer);
    if (displayTexture) glDeleteTextures(1, &displayTexture);
    displayProgram = displayArray = displayFramebuffer = displayTexture = 0;
    displayCache.source = 0;
    displayCache.w = displayCache.h = 0;
}

const RenderPipeline pcPipeline = {PcScene, PcPresent, PcDestroy};
