#include "gl_pipeline.h"
#include <stdlib.h>
#include <string.h>

#define RENDER_POSITION "    vec2 pixel = position.xy;\n"
#define RENDER_TEXCOORD "    vTexel = vec3(texel, 1.0);\n"
#define RENDER_VERTEX_COLOUR "    return c;\n"
#define RENDER_OUTPUT_COLOUR "vec3 OutputColour(vec3 c) { return c; }\n"
#define RENDER_TINT "vec3 TextureTint() { return VertexColour() * ((mode.y & 64) != 0 && ps1Light == 0 ? 1.0 : 255.0 / 128.0); }\n"
#define RENDER_FILTER \
    "#define FILTER_TAPS 3\n" \
    "vec2 FilterPosition(vec2 t) { return t; }\n" \
    "vec2 FilterHdPosition(vec2 t) { return (t + 0.5) * vec2(hd.zw) / vec2(hdTexels.zw) - 0.5; }\n" \
    "vec2 FilterFraction(vec2 f) { return floor(f * 32.0) / 32.0; }\n" \
    "ivec2 FilterCorner(int i, vec2 f) { return i == 0 ? (f.x + f.y >= 1.0 ? ivec2(1) : ivec2(0)) : (i == 1 ? ivec2(1,0) : ivec2(0,1)); }\n" \
    "float FilterWeight(int i, vec2 f) { vec3 w = f.x + f.y >= 1.0 ? vec3(f.x + f.y - 1.0, 1.0 - f.y, 1.0 - f.x) : vec3(1.0 - f.x - f.y, f.x, f.y); return w[i]; }\n"

static const char *n64VertexSource =
#include "shaders/scene_vertex.h"
;
static const char *n64FragmentSource =
#include "shaders/material_fragment.h"
;
static RenderProgram n64Program;

static RenderProgram *N64Scene(void)
{
    if (!RenderGlProgramInit(&n64Program, n64VertexSource, n64FragmentSource)) abort();
    return &n64Program;
}

#include "shaders/n64_vi.h"

static GLuint n64ViProgram, n64ViArray, n64ViTexture[2], n64ViFramebuffer[2];
static int n64ViWidth, n64ViHeight;
static struct { GLint source, pixelScale, precisionOn, ditherOn, region, stage, displayRegion; } viUniform;

typedef struct N64ViPasses {
    int stages[3], count;
} N64ViPasses;

static N64ViPasses N64ViStages(const N64RenderSettings *settings)
{
    /* Dithering and dedithering have no effect without RGB5 precision.
       Resampling is handled during final presentation, independently of these passes. */
    N64ViPasses passes = {0};
    if (settings->colourPrecision) {
        passes.stages[passes.count++] = 0;
        if (settings->dedither) passes.stages[passes.count++] = 1;
    }
    if (settings->divot) passes.stages[passes.count++] = 2;
    return passes;
}

static void N64ViInit(void)
{
    if (!n64ViProgram) {
        GLint linked;
        GLuint vertex = RenderGlCompile(GL_VERTEX_SHADER, n64ViVertex);
        GLuint fragment = RenderGlCompile(GL_FRAGMENT_SHADER, n64ViFragment);
        n64ViProgram = glCreateProgram();
        glAttachShader(n64ViProgram, vertex);
        glAttachShader(n64ViProgram, fragment);
        glLinkProgram(n64ViProgram);
        glDeleteShader(vertex);
        glDeleteShader(fragment);
        glGetProgramiv(n64ViProgram, GL_LINK_STATUS, &linked);
        if (!linked) { SDL_Log("N64 VI program failed to link"); abort(); }
        viUniform.source = glGetUniformLocation(n64ViProgram, "source");
        viUniform.pixelScale = glGetUniformLocation(n64ViProgram, "pixelScale");
        viUniform.precisionOn = glGetUniformLocation(n64ViProgram, "precisionOn");
        viUniform.ditherOn = glGetUniformLocation(n64ViProgram, "ditherOn");
        viUniform.region = glGetUniformLocation(n64ViProgram, "region");
        viUniform.stage = glGetUniformLocation(n64ViProgram, "stage");
        viUniform.displayRegion = glGetUniformLocation(n64ViProgram, "displayRegion");
        glGenVertexArrays(1, &n64ViArray);
    }
}

/* Returns zero when the source can be displayed directly, without allocating VI buffers. */
static GLuint N64Picture(GLuint source, int x, int y, int width, int height, const N64RenderSettings *settings, int pixelScale,
                         const N64ViPasses *passes)
{
    GLint previousProgram;

    if (!passes->count) {
        return 0;
    }
    glGetIntegerv(GL_CURRENT_PROGRAM, &previousProgram);
    N64ViInit();
    int targets = SDL_min(2, passes->count);
    /* A single enabled pass needs no ping-pong target. Release it when settings
       switch back to one pass, and recreate it lazily if another pass is enabled. */
    if (targets == 1 && n64ViTexture[1]) {
        glDeleteTextures(1, &n64ViTexture[1]);
        glDeleteFramebuffers(1, &n64ViFramebuffer[1]);
        n64ViTexture[1] = n64ViFramebuffer[1] = 0;
    }
    glActiveTexture(GL_TEXTURE0);
    for (int i = 0; i < targets; i++) {
        if (!n64ViTexture[i] || n64ViWidth != width || n64ViHeight != height) {
            if (!n64ViTexture[i]) {
                glGenTextures(1, &n64ViTexture[i]);
                glGenFramebuffers(1, &n64ViFramebuffer[i]);
            }
            glBindTexture(GL_TEXTURE_2D, n64ViTexture[i]);
            glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, width, height, 0, GL_RGBA, GL_UNSIGNED_BYTE, NULL);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
            glBindFramebuffer(GL_FRAMEBUFFER, n64ViFramebuffer[i]);
            glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, n64ViTexture[i], 0);
            if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE) {
                SDL_Log("N64 VI framebuffer incomplete"); abort();
            }
        }
    }
    n64ViWidth = width;
    n64ViHeight = height;
    glUseProgram(n64ViProgram);
    glBindVertexArray(n64ViArray);
    glDisable(GL_SCISSOR_TEST);
    glDisable(GL_DEPTH_TEST);
    glDisable(GL_BLEND);
    glViewport(0, 0, width, height);
    glUniform1i(viUniform.source, 0);
    glUniform1i(viUniform.pixelScale, pixelScale);
    glUniform1i(viUniform.precisionOn, settings->colourPrecision);
    glUniform1i(viUniform.ditherOn, settings->dither);
    for (int i = 0; i < passes->count; i++) {
        /* Ping-pong by executed passes; the first enabled stage reads the original crop. */
        glBindFramebuffer(GL_DRAW_FRAMEBUFFER, n64ViFramebuffer[i & 1]);
        glBindTexture(GL_TEXTURE_2D, source);
        glUniform4i(viUniform.region, x, y, width, height);
        glUniform1i(viUniform.stage, passes->stages[i]);
        glDrawArrays(GL_TRIANGLES, 0, 3);
        source = n64ViTexture[i & 1];
        x = y = 0;
    }
    glUseProgram((GLuint)previousProgram);
    return n64ViFramebuffer[(passes->count - 1) & 1];
}

/* Resample directly to the destination with the old cropped-buffer edge clamp.
   A linear framebuffer blit from the original scene can sample outside that crop.
   Scene textures already use GL_LINEAR; no intermediate VI buffers are needed. */
static void N64Resample(GLuint source, int x, int y, int width, int height, int x0, int y0, int x1, int y1)
{
    GLint previousProgram;
    glGetIntegerv(GL_CURRENT_PROGRAM, &previousProgram);
    N64ViInit();
    glUseProgram(n64ViProgram);
    glBindVertexArray(n64ViArray);
    glDisable(GL_SCISSOR_TEST);
    glDisable(GL_DEPTH_TEST);
    glDisable(GL_BLEND);
    glViewport(SDL_min(x0, x1), SDL_min(y0, y1), SDL_abs(x1 - x0), SDL_abs(y1 - y0));
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, source);
    glUniform1i(viUniform.source, 0);
    glUniform4i(viUniform.region, x, y, width, height);
    glUniform4i(viUniform.displayRegion, x0, y0, x1 - x0, y1 - y0);
    glUniform1i(viUniform.stage, 3);
    glDrawArrays(GL_TRIANGLES, 0, 3);
    glUseProgram((GLuint)previousProgram);
}


/* Cache only completed VI processing. Destination size/orientation do not affect it.
   The backend's source revision covers draws, clears, uploads, copies and reallocations. */
static struct {
    int valid;
    GLuint texture, result;
    uint64_t revision;
    int left, top, width, height, scale;
    N64RenderSettings settings;
} viCache;

static void N64Present(const void *profile, const RenderPresent *p)
{
    const N64RenderSettings *settings = profile;
    RenderPresent shown = *p;
    if (p->race && p->right > p->left && p->bottom > p->top) {
        N64ViPasses passes = N64ViStages(settings);
        if (!passes.count) {
            if (settings->resample) {
                N64Resample(p->texture, p->left, p->top, p->right - p->left, p->bottom - p->top,
                            p->x0, p->y0, p->x1, p->y1);
                return;
            }
        } else {
            GLint destination;
            int width = p->right - p->left, height = p->bottom - p->top;
            glGetIntegerv(GL_DRAW_FRAMEBUFFER_BINDING, &destination);
            if (!viCache.valid || viCache.texture != p->texture || viCache.revision != p->revision ||
                viCache.left != p->left || viCache.top != p->top || viCache.width != width ||
                viCache.height != height || viCache.scale != p->scale ||
                viCache.settings.colourPrecision != settings->colourPrecision ||
                viCache.settings.dither != settings->dither || viCache.settings.dedither != settings->dedither ||
                viCache.settings.divot != settings->divot) {
                viCache.result = N64Picture(p->texture, p->left, p->top, width, height, settings, p->scale, &passes);
                viCache.texture = p->texture;
                viCache.revision = p->revision;
                viCache.left = p->left; viCache.top = p->top;
                viCache.width = width; viCache.height = height; viCache.scale = p->scale;
                viCache.settings = *settings;
                viCache.valid = 1;
            }
            shown.framebuffer = viCache.result;
            shown.left = shown.top = 0;
            shown.right = width; shown.bottom = height;
            glBindFramebuffer(GL_DRAW_FRAMEBUFFER, (GLuint)destination);
        }
    }
    RenderGlPresentDirect(&shown, settings->resample ? GL_LINEAR : GL_NEAREST);
}

static void N64Destroy(void)
{
    RenderGlProgramDestroy(&n64Program);
    if (n64ViProgram) glDeleteProgram(n64ViProgram);
    if (n64ViArray) glDeleteVertexArrays(1, &n64ViArray);
    glDeleteTextures(2, n64ViTexture);
    glDeleteFramebuffers(2, n64ViFramebuffer);
    n64ViProgram = n64ViArray = 0;
    memset(n64ViTexture, 0, sizeof(n64ViTexture));
    memset(n64ViFramebuffer, 0, sizeof(n64ViFramebuffer));
    memset(&viUniform, 0, sizeof(viUniform));
    memset(&viCache, 0, sizeof(viCache));
    n64ViWidth = n64ViHeight = 0;
}

const RenderPipeline n64Pipeline = {N64Scene, N64Present, N64Destroy};
