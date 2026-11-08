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
    /* Upscale a hard black/white edge: nearest must retain only source colours,
       while linear must blend the two pixels on either side of the boundary. */
    for (int y = 0; y < 16; y++) {
        for (int x = 0; x < 16; x++) {
            int offset = (y * 16 + x) * 4;
            pixels[offset] = pixels[offset + 1] = pixels[offset + 2] = x < 4 ? 0 : 255;
            pixels[offset + 3] = 255;
        }
    }
    glBindTexture(GL_TEXTURE_2D, textures[0]);
    glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, 16, 16, GL_RGBA, GL_UNSIGNED_BYTE, pixels);
    present.revision++;
    present.race = 0;
    present.right = present.bottom = 8;
    for (int linear = 0; linear <= 1; linear++) {
        settings.TEST_PROFILE.resample = linear;
        glBindFramebuffer(GL_DRAW_FRAMEBUFFER, framebuffers[1]);
        pipeline->present(&settings.TEST_PROFILE, &present);
        glBindFramebuffer(GL_READ_FRAMEBUFFER, framebuffers[1]);
        glReadPixels(0, 0, 16, 16, GL_RGBA, GL_UNSIGNED_BYTE, pixels);
        for (int x = 0; x < 16; x++) {
            int expected = x < 8 ? 0 : 255;
            if (linear && x == 7) expected = 64;
            if (linear && x == 8) expected = 191;
            for (int c = 0; c < 3; c++) {
                int actual = pixels[(8 * 16 + x) * 4 + c];
                assert(actual >= expected - 1 && actual <= expected + 1);
            }
        }
        assert(glGetError() == GL_NO_ERROR);
    }
    pipeline->destroy();
    glDeleteTextures(2, textures);
    glDeleteFramebuffers(2, framebuffers);
    SDL_GL_DestroyContext(context);
    SDL_DestroyWindow(window);
    SDL_Quit();
    puts("Standalone renderer compilation, nearest/linear display scaling and resource lifecycle: OK");
    return 0;
}
