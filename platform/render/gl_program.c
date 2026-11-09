#include "gl_pipeline.h"
#include "../platform.h"
#include <stdlib.h>
#include <string.h>

GLuint RenderGlCompile(GLenum type, const char *source)
{
    GLuint shader = glCreateShader(type);
    const char *sources[2] = {PORT_GLSL_VERSION, source};
    GLint ok;

    glShaderSource(shader, 2, sources, NULL);
    glCompileShader(shader);
    glGetShaderiv(shader, GL_COMPILE_STATUS, &ok);
    if (!ok) {
        char log[4096];
        glGetShaderInfoLog(shader, sizeof(log), NULL, log);
        SDL_Log("shader: %s", log);
        abort();
    }
    return shader;
}

int RenderGlProgramInit(RenderProgram *s, const char *vertexSource, const char *fragmentSource)
{
    GLint ok;
    if (s->program) return 1;
    s->program = glCreateProgram();
    GLuint vertex = RenderGlCompile(GL_VERTEX_SHADER, vertexSource);
    glAttachShader(s->program, vertex);
    GLuint fragment = RenderGlCompile(GL_FRAGMENT_SHADER, fragmentSource);
    glAttachShader(s->program, fragment);
    glBindAttribLocation(s->program, 0, "position");
    glBindAttribLocation(s->program, 1, "colour");
    glBindAttribLocation(s->program, 2, "texel");
    glBindAttribLocation(s->program, 3, "textureIn");
    glBindAttribLocation(s->program, 4, "window");
    glBindAttribLocation(s->program, 5, "modeIn");
    glBindAttribLocation(s->program, 6, "hdIn");
    glBindAttribLocation(s->program, 7, "hdTexelsIn");
    glBindAttribLocation(s->program, 8, "filterBoundsIn");
    glBindAttribLocation(s->program, 9, "emissionUvIn");
    glBindAttribLocation(s->program, 10, "viewPositionIn");
    glLinkProgram(s->program);
    glDeleteShader(vertex);
    glDeleteShader(fragment);
    glGetProgramiv(s->program, GL_LINK_STATUS, &ok);
    if (!ok) {
        char log[4096];
        glGetProgramInfoLog(s->program, sizeof(log), NULL, log);
        SDL_Log("program: %s", log);
        RenderGlProgramDestroy(s);
        return 0;
    }
    s->uniformVram = glGetUniformLocation(s->program, "vram");
    s->uniformGlyphs = glGetUniformLocation(s->program, "glyphs");
    s->uniformPortPages = glGetUniformLocation(s->program, "portPages");
    s->uniformHdAtlas = glGetUniformLocation(s->program, "hdAtlas");
    s->uniformPass = glGetUniformLocation(s->program, "pass");
    s->uniformDepthPull = glGetUniformLocation(s->program, "depthPull");
    s->uniformDepthPass = glGetUniformLocation(s->program, "depthPass");
    s->uniformTarget = glGetUniformLocation(s->program, "target");
    s->uniformPointSize = glGetUniformLocation(s->program, "pointSize");
    s->uniformNativeGrid = glGetUniformLocation(s->program, "nativeGrid");
    s->uniformOrigin = glGetUniformLocation(s->program, "origin");
    /* each sampler its own unit from the start (an integer and a float texture may not share one) */
    glUseProgram(s->program);
    glUniform2f(s->uniformNativeGrid, 1, 1);
    glUniform1i(s->uniformVram, 0);
    glUniform1i(s->uniformGlyphs, 1);
    glUniform1i(s->uniformHdAtlas, 2);
    glUniform1i(s->uniformPortPages, 3);

    return 1;
}

void RenderGlProgramDestroy(RenderProgram *s)
{
    if (s->program) {
        GLint current;
        glGetIntegerv(GL_CURRENT_PROGRAM, &current);
        if ((GLuint)current == s->program) glUseProgram(0);
        glDeleteProgram(s->program);
    }
    memset(s, 0, sizeof(*s));
}

void RenderGlPresentDirect(const RenderPresent *p, GLenum filter)
{
    glBindFramebuffer(GL_READ_FRAMEBUFFER, p->framebuffer);
    glDisable(GL_SCISSOR_TEST);
    glBlitFramebuffer(p->left, p->top, p->right, p->bottom,
                      p->x0, p->y0, p->x1, p->y1, GL_COLOR_BUFFER_BIT, filter);
}

RenderPictureKey RenderGlPictureKey(const RenderPresent *p, GLuint texture)
{
    return (RenderPictureKey){texture, p->revision, p->left, p->top, p->right, p->bottom};
}

int RenderGlPictureMatches(const RenderPictureKey *key, const RenderPresent *p, GLuint texture)
{
    return key->texture == texture && key->revision == p->revision &&
           key->left == p->left && key->top == p->top && key->right == p->right && key->bottom == p->bottom;
}

void RenderGlBeginFullscreen(GLuint program, GLuint array)
{
    glUseProgram(program);
    glBindVertexArray(array);
    glDisable(GL_SCISSOR_TEST);
    glDisable(GL_DEPTH_TEST);
    glDisable(GL_BLEND);
}

int RenderGlResizeTarget(GLuint *texture, GLuint *framebuffer, int width, int height,
                         GLenum format, GLenum type, GLenum filter)
{
    if (!*texture) {
        glGenTextures(1, texture);
        glGenFramebuffers(1, framebuffer);
    }
    glBindTexture(GL_TEXTURE_2D, *texture);
    glTexImage2D(GL_TEXTURE_2D, 0, format, width, height, 0, GL_RGBA, type, NULL);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, filter);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, filter);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glBindFramebuffer(GL_DRAW_FRAMEBUFFER, *framebuffer);
    glFramebufferTexture2D(GL_DRAW_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, *texture, 0);
    return glCheckFramebufferStatus(GL_DRAW_FRAMEBUFFER) == GL_FRAMEBUFFER_COMPLETE;
}

/* Generic framebuffer transport: preserve display-encoded RGB and reset coverage. */
static RenderProgram seedProgram;
static GLuint seedArray;

void RenderGlSeedHdr(GLuint source, GLuint target, int x, int y, int w, int h)
{
    if (w <= 0 || h <= 0) return;
    GLint previous;
    glGetIntegerv(GL_CURRENT_PROGRAM, &previous);
    if (!seedProgram.program) {
        const char *vertex =
            "void main() {\n"
            "    vec2 uv = vec2(float((gl_VertexID << 1) & 2), float(gl_VertexID & 2));\n"
            "    gl_Position = vec4(uv * 2.0 - 1.0, 0.0, 1.0);\n"
            "}\n";
        const char *fragment =
            "#ifdef GL_ES\nprecision highp float;\nprecision highp int;\nprecision highp sampler2D;\n#endif\n"
            "uniform sampler2D source;\n"
            "layout(location = 0) out vec4 colour;\n"
            "layout(location = 1) out vec4 coverage;\n"
            "void main() {\n"
            "    colour = vec4(texelFetch(source, ivec2(gl_FragCoord.xy), 0).rgb, 1.0);\n"
            "    coverage = vec4(0.0);\n"
            "}\n";
        if (!RenderGlProgramInit(&seedProgram, vertex, fragment)) abort();
        glUniform1i(glGetUniformLocation(seedProgram.program, "source"), 0);
        glGenVertexArrays(1, &seedArray);
    }
    RenderGlBeginFullscreen(seedProgram.program, seedArray);
    glBindFramebuffer(GL_DRAW_FRAMEBUFFER, target);
    glViewport(x, y, w, h);
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, source);
    glDrawArrays(GL_TRIANGLES, 0, 3);
    glUseProgram((GLuint)previous);
}

void RenderGlSeedShutdown(void)
{
    RenderGlProgramDestroy(&seedProgram);
    glDeleteVertexArrays(1, &seedArray);
    seedArray = 0;
}
