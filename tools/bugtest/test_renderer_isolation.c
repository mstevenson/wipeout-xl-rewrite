/* Linked once per renderer, WITHOUT the registry, backend or any other renderer.
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
    /* Pin the pixel fixture's curve independently of the user's default preset. */
    settings.modern.tonemapper = MODERN_TONEMAP_ORIGINAL;
    const Renderer *renderer = &TEST_RENDERER;
    const void *profile = &settings.TEST_PROFILE;
    RenderResources resources = renderer->resources ? renderer->resources(profile) : (RenderResources){0};
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
    GLuint auxiliary[2] = {0}, auxiliaryTarget = 0;
    if (resources.hdr) {
        glGenTextures(2, auxiliary);
        glGenFramebuffers(1, &auxiliaryTarget);
        glBindFramebuffer(GL_DRAW_FRAMEBUFFER, auxiliaryTarget);
        for (int i = 0; i < 2; i++) {
            glBindTexture(GL_TEXTURE_2D, auxiliary[i]);
            glTexImage2D(GL_TEXTURE_2D, 0, i ? GL_RG8 : GL_RGBA16F, 16, 16, 0,
                         i ? GL_RG : GL_RGBA, i ? GL_UNSIGNED_BYTE : GL_HALF_FLOAT, NULL);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
            glFramebufferTexture2D(GL_DRAW_FRAMEBUFFER, GL_COLOR_ATTACHMENT0 + i, GL_TEXTURE_2D, auxiliary[i], 0);
        }
        const GLenum attachments[] = {GL_COLOR_ATTACHMENT0, GL_COLOR_ATTACHMENT1};
        glDrawBuffers(2, attachments);
        assert(glCheckFramebufferStatus(GL_DRAW_FRAMEBUFFER) == GL_FRAMEBUFFER_COMPLETE);
        RenderGlSeedHdr(textures[0], auxiliaryTarget, 0, 0, 16, 16);
        present.hdrTexture = auxiliary[0];
        present.hdrMask = auxiliary[1];
    }
    for (int cycle = 0; cycle < 3; cycle++) {
        RenderProgram *scene = pipeline->scene();
        GLuint program = scene->program;
        assert(program && isProgram(program));
        assert(pipeline->scene()->program == program);
        glUseProgram(program);
        if (pipeline->bind) pipeline->bind(profile, resources.hdr);
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
    RenderGlSeedShutdown();
    glDeleteTextures(2, auxiliary);
    glDeleteFramebuffers(1, &auxiliaryTarget);
    glDeleteTextures(2, textures);
    glDeleteFramebuffers(2, framebuffers);
    SDL_GL_DestroyContext(context);
    SDL_DestroyWindow(window);
    SDL_Quit();
    puts("Standalone profile policy, batch binding, HDR/display scaling and resource lifecycle: OK");
    return 0;
}
