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
    glBindAttribLocation(s->program, 3, "texture");
    glBindAttribLocation(s->program, 4, "window");
    glBindAttribLocation(s->program, 5, "modeIn");
    glBindAttribLocation(s->program, 6, "hdIn");
    glBindAttribLocation(s->program, 7, "hdTexelsIn");
    glBindAttribLocation(s->program, 8, "filterBoundsIn");
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
