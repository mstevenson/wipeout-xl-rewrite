/* Linked once per renderer, WITHOUT the registry, backend or either other renderer.
   A renderer cannot hide a cross-module dependency behind the combined game binary. */
#include "render/gl_pipeline.h"
#include <assert.h>
#include <stdio.h>

#ifndef TEST_PIPELINE
#error Select one pipeline and its profile at compile time
#endif

int main(void)
{
    assert(SDL_Init(SDL_INIT_VIDEO));
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 3);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 3);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_CORE);
    SDL_Window *window = SDL_CreateWindow("Isolated renderer", 16, 16, SDL_WINDOW_OPENGL | SDL_WINDOW_HIDDEN);
    assert(window);
    SDL_GLContext context = SDL_GL_CreateContext(window);
    assert(context && SDL_GL_MakeCurrent(window, context) && RenderGlLoad());
    PFNGLISPROGRAMPROC isProgram = (PFNGLISPROGRAMPROC)SDL_GL_GetProcAddress("glIsProgram");
    assert(isProgram);
    const RenderPipeline *pipeline = &TEST_PIPELINE;
    RenderSettings settings = RENDER_DEFAULTS;
    GLuint textures[2], framebuffers[2];
    uint8_t pixels[16 * 16 * 4];
    glGenTextures(2, textures);
    glGenFramebuffers(2, framebuffers);
    for (int i = 0; i < 2; i++) {
        glBindTexture(GL_TEXTURE_2D, textures[i]);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, 16, 16, 0, GL_RGBA, GL_UNSIGNED_BYTE, NULL);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glBindFramebuffer(GL_FRAMEBUFFER, framebuffers[i]);
        glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, textures[i], 0);
        assert(glCheckFramebufferStatus(GL_FRAMEBUFFER) == GL_FRAMEBUFFER_COMPLETE);
        glClearColor(1, 0, 0, 1);
        glClear(GL_COLOR_BUFFER_BIT);
    }
    RenderPresent present = {textures[0], framebuffers[0], 1, 1, 1, 0, 0, 16, 16, 0, 0, 16, 16};
    for (int cycle = 0; cycle < 3; cycle++) {
        RenderProgram *scene = pipeline->scene();
        GLuint program = scene->program;
        assert(program && isProgram(program));
        assert(pipeline->scene()->program == program);
        glBindFramebuffer(GL_DRAW_FRAMEBUFFER, framebuffers[1]);
        pipeline->present(&settings.TEST_PROFILE, &present);
        glBindFramebuffer(GL_READ_FRAMEBUFFER, framebuffers[1]);
        glReadPixels(0, 0, 16, 16, GL_RGBA, GL_UNSIGNED_BYTE, pixels);
        assert(pixels[0] >= 248 && !pixels[1] && !pixels[2]);
        pipeline->destroy();
        assert(!scene->program && !isProgram(program));
        assert(glGetError() == GL_NO_ERROR);
    }
    glDeleteTextures(2, textures);
    glDeleteFramebuffers(2, framebuffers);
    SDL_GL_DestroyContext(context);
    SDL_DestroyWindow(window);
    SDL_Quit();
    puts("Standalone renderer compilation, presentation and resource lifecycle: OK");
    return 0;
}
