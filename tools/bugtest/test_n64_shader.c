/* Offscreen readback of the production shaders. Requires a desktop OpenGL context.
   Run with tools/bugtest/render.sh; no disc, game settings or visible window needed. */
#define PORT_GLSL_VERSION "#version 330 core\n"
#include "../../platform/psx/gpu_gl.c"
#include <assert.h>
#include <math.h>

/* Flush uses the real batching/depth code; no game or settings file is needed. */
PortSettings portSettings;
uint16_t psxVram[VRAM_HEIGHT][VRAM_WIDTH];
uint16_t psxVramHigh[VRAM_HEIGHT][VRAM_WIDTH];

static uint16_t words[1024 * 1024];
static BatchVertex triangle[3];
static int bounds[4] = {0, 0, 1, 1};
static uint16_t testHd[4];
static int16_t testHdTexels[4];

static void Upload(void)
{
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, vramTexture);
    glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, 1024, 1024, GL_RED_INTEGER, GL_UNSIGNED_SHORT, words);
}

static void Render(int filtered, int depth, float u, float v, int tint, int semi, int pass,
                   float red, float green, float blue, float alpha)
{
    float result[4], expected[4] = {red, green, blue, alpha};
    memset(triangle, 0, sizeof(triangle));
    triangle[1].x = 2; triangle[2].y = 2;
    for (int i = 0; i < 3; i++) {
        BatchVertex *b = &triangle[i];
        b->w = 1; b->r = b->g = b->b = tint / 255.0f; b->a = 1;
        b->u = u; b->v = v;
        b->texture[2] = 32; b->texture[3] = 16;
        b->mode[0] = 1; b->mode[2] = (uint8_t)depth;
        b->mode[3] = (uint8_t)((filtered << 1) | ((semi + 1) << 4));
        for (int k = 0; k < 4; k++) b->filterBounds[k] = (int16_t)bounds[k];
        memcpy(b->hd, testHd, sizeof(testHd));
        memcpy(b->hdTexels, testHdTexels, sizeof(testHdTexels));
    }
    glBufferData(GL_ARRAY_BUFFER, sizeof(triangle), triangle, GL_STREAM_DRAW);
    glUniform1i(glGetUniformLocation(program, "pass"), pass);
    glClearColor(0, 0, 0, 0); glClear(GL_COLOR_BUFFER_BIT);
    glDrawArrays(GL_TRIANGLES, 0, 3);
    glReadPixels(0, 0, 1, 1, GL_RGBA, GL_FLOAT, result);
    assert(glGetError() == GL_NO_ERROR);
    for (int k = 0; k < 4; k++) {
        if (fabsf(result[k] - expected[k]) > 0.002f) {
            fprintf(stderr, "filter=%d depth=%d uv=%g,%g semi=%d pass=%d channel=%d: got %g expected %g\n",
                    filtered, depth, u, v, semi, pass, k, result[k], expected[k]);
            abort();
        }
    }
}

/* A red/green/blue/white square makes four-point and three-point filtering visibly
   different. On the diagonal only green and blue contribute; red or white there
   would reveal the wrong diagonal or an unwanted four-corner centre average. */
static void ThreePointSquare(int depth)
{
    const float full = 248.0f / 255.0f;
    Render(1, depth, 0, 0, 128, -1, 0, full, 0, 0, 1);
    Render(1, depth, 1, 0, 128, -1, 0, 0, full, 0, 1);
    Render(1, depth, 0, 1, 128, -1, 0, 0, 0, full, 1);
    Render(1, depth, 1, 1, 128, -1, 0, full, full, full, 1);
    Render(1, depth, .25f, .25f, 128, -1, 0, full / 2, full / 4, full / 4, 1);
    Render(1, depth, .75f, .75f, 128, -1, 0, full / 2, full * .75f, full * .75f, 1);
    Render(1, depth, .75f, .125f, 128, -1, 0, full / 8, full * .75f, full / 8, 1);
    Render(1, depth, .75f, .5f, 128, -1, 0, full / 4, full * .75f, full / 2, 1);
    Render(1, depth, .25f, .75f, 128, -1, 0, 0, full / 4, full * .75f, 1);
    Render(1, depth, .5f, .5f, 128, -1, 0, 0, full / 2, full / 2, 1);
    /* The fractions truncate to 7/32 and 15/32, leaving 10/32 for red. */
    Render(1, depth, .249f, .499f, 128, -1, 0, full * 10 / 32, full * 7 / 32, full * 15 / 32, 1);
    /* Neighbouring 1/32 steps on either side of the diagonal use opposite third corners. */
    Render(1, depth, .5f, .46875f, 128, -1, 0, full / 32, full / 2, full * 15 / 32, 1);
    Render(1, depth, .5f, .53125f, 128, -1, 0, full / 32, full / 2, full * 17 / 32, 1);
}

static void HdThreePoint(void)
{
    const float full = 248.0f / 255.0f;
    float pixels[4 * 4 * 4];
    GLuint atlas;
    /* Magenta neighbours surround the 2x2 replacement, exposing any atlas bleed. */
    for (int i = 0; i < 16; i++) {
        pixels[i * 4] = pixels[i * 4 + 2] = pixels[i * 4 + 3] = 1;
        pixels[i * 4 + 1] = 0;
    }
    const float square[4][4] = {{full, 0, 0, 1}, {0, full, 0, 1},
                               {0, 0, full, 1}, {full, full, full, 1}};
    for (int y = 0; y < 2; y++)
        for (int x = 0; x < 2; x++)
            memcpy(pixels + ((y + 1) * 4 + x + 1) * 4, square[y * 2 + x], sizeof(square[0]));
    glActiveTexture(GL_TEXTURE2);
    glGenTextures(1, &atlas); glBindTexture(GL_TEXTURE_2D, atlas);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA32F, 4, 4, 0, GL_RGBA, GL_FLOAT, pixels);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glUniform1i(glGetUniformLocation(program, "hdAtlas"), 2);
    testHd[0] = testHd[1] = 1; testHd[2] = testHd[3] = 2;
    testHdTexels[2] = testHdTexels[3] = 2;
    ThreePointSquare(0);
    Render(1, 0, 1.75f, 0, 128, -1, 0, 0, full, 0, 1);
    Render(0, 0, .5f, .5f, 128, -1, 0, full, 0, 0, 1); /* PS1 HD stays nearest */
    /* Two replacement pixels per source texel: retain the existing centre alignment. */
    testHdTexels[2] = testHdTexels[3] = 1;
    Render(1, 0, .125f, .125f, 128, -1, 0, full / 2, full * .75f, full * .75f, 1);
    Render(1, 0, .875f, .875f, 128, -1, 0, full, full, full, 1);
    memset(testHd, 0, sizeof(testHd)); memset(testHdTexels, 0, sizeof(testHdTexels));
    glUniform1i(glGetUniformLocation(program, "hdAtlas"), 1);
    glDeleteTextures(1, &atlas);
    glActiveTexture(GL_TEXTURE0);
}

static void DepthTriangle(const GpuDrawState *state, float x, float depth, int red, int green)
{
    GpuVertex v = {0};
    v.x = x; v.y = 100; v.w = depth;
    v.r = (uint8_t)red; v.g = (uint8_t)green; v.a = 255;
    v.u = v.v = .5f;
    Begin(state, 3);
    Add(&v);
    v.x += 2; Add(&v);
    v.x -= 2; v.y += 2; Add(&v);
}

/* Filtering needs alpha blending even on solid geometry. That must not let a
   leading translucent effect turn off depth writes for the rest of its batch. */
static void DepthBatches(GLuint fbo, GLuint output, GLuint dummy)
{
    GpuDrawState surface = {.textured = 1, .semi = -1, .clutX = 32, .clutY = 16};
    GpuDrawState effect = {.semi = 1}; /* additive untextured polygon */
    GLuint depth;
    const int edges[] = {0, VRAM_WIDTH - 2};
    framebuffer = fbo; areaW = VRAM_WIDTH; areaH = VRAM_HEIGHT;
    glyphTexture = portPagesTexture = hdAtlas = dummy;
    vramDirty = vramHighDirty = 0;
    glActiveTexture(GL_TEXTURE0); glBindTexture(GL_TEXTURE_2D, output);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA32F, VRAM_WIDTH, VRAM_HEIGHT, 0, GL_RGBA, GL_FLOAT, NULL);
    glGenRenderbuffers(1, &depth); glBindRenderbuffer(GL_RENDERBUFFER, depth);
    glRenderbufferStorage(GL_RENDERBUFFER, GL_DEPTH_COMPONENT24, VRAM_WIDTH, VRAM_HEIGHT);
    glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_RENDERBUFFER, depth);
    assert(glCheckFramebufferStatus(GL_FRAMEBUFFER) == GL_FRAMEBUFFER_COMPLETE);
    uniformVram = glGetUniformLocation(program, "vram");
    uniformGlyphs = glGetUniformLocation(program, "glyphs");
    uniformHdAtlas = glGetUniformLocation(program, "hdAtlas");
    uniformPortPages = glGetUniformLocation(program, "portPages");
    uniformPass = glGetUniformLocation(program, "pass");
    uniformTarget = glGetUniformLocation(program, "target");
    uniformOrigin = glGetUniformLocation(program, "origin");
    uniformDepthPull = glGetUniformLocation(program, "depthPull");
    uniformDepthPass = glGetUniformLocation(program, "depthPass");
    primitiveFilterBounds[0] = primitiveFilterBounds[1] = 0;
    primitiveFilterBounds[2] = primitiveFilterBounds[3] = 1;
    words[0] = words[1024] = 0x11;
    words[16 * 1024 + 33] = 0x7fff;
    Upload();
    for (int filtered = 0; filtered < 2; filtered++) {
        surface.filtered = filtered;
        for (int leading = 0; leading < 2; leading++) {
            for (int reverse = 0; reverse < 2; reverse++) {
                for (int edge = 0; edge < 2; edge++) {
                    float pixel[4], z;
                    int x = edges[edge];
                    glDisable(GL_SCISSOR_TEST); glDepthMask(GL_TRUE);
                    glClearColor(0, 0, 0, 0); glClearDepth(1);
                    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
                    /* Zero-colour additive effect changes batching but not the picture. */
                    if (leading) DepthTriangle(&effect, x, 50, 0, 0);
                    if (reverse) DepthTriangle(&surface, x, 150, 0, 128);
                    DepthTriangle(&surface, x, 100, 128, 0);
                    if (!reverse) DepthTriangle(&surface, x, 150, 0, 128);
                    Flush();
                    glReadPixels(x, 100, 1, 1, GL_RGBA, GL_FLOAT, pixel);
                    glReadPixels(x, 100, 1, 1, GL_DEPTH_COMPONENT, GL_FLOAT, &z);
                    if (fabsf(pixel[0] - 248.0f / 255.0f) > .002f || pixel[1] > .002f || fabsf(z - .99f) > .00001f) {
                        fprintf(stderr, "depth batch: filter=%d leading effect=%d reverse=%d edge=%d: rgb=%g,%g,%g depth=%g\n",
                                filtered, leading, reverse, edge, pixel[0], pixel[1], pixel[2], z);
                        abort();
                    }
                }
            }
        }
    }
    assert(glGetError() == GL_NO_ERROR);
    glDeleteRenderbuffers(1, &depth);
    puts("Production depth batches: foreground occludes background at both screen edges, with/without a leading effect: OK");
}

int main(void)
{
    assert(SDL_Init(SDL_INIT_VIDEO));
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 3);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 3);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_CORE);
    SDL_Window *window = SDL_CreateWindow("N64 shader regression", 16, 16, SDL_WINDOW_OPENGL | SDL_WINDOW_HIDDEN);
    if (!window) { fprintf(stderr, "%s\n", SDL_GetError()); return 1; }
    SDL_GLContext context = SDL_GL_CreateContext(window);
    assert(context && SDL_GL_MakeCurrent(window, context) && LoadFunctions());
    program = glCreateProgram();
    glAttachShader(program, Compile(GL_VERTEX_SHADER, vertexSource));
    glAttachShader(program, Compile(GL_FRAGMENT_SHADER, fragmentSource));
    const char *names[] = {"position", "colour", "texel", "texture", "window", "modeIn", "hdIn", "hdTexelsIn", "filterBoundsIn"};
    for (int i = 0; i < 9; i++) glBindAttribLocation(program, (GLuint)i, names[i]);
    glLinkProgram(program);
    GLint linked; glGetProgramiv(program, GL_LINK_STATUS, &linked); assert(linked);
    glUseProgram(program);
    glUniform2f(glGetUniformLocation(program, "target"), 1, 1);
    glUniform2f(glGetUniformLocation(program, "origin"), 0, 0);
    glUniform1f(glGetUniformLocation(program, "depthPull"), 1);
    glUniform1i(glGetUniformLocation(program, "vram"), 0);
    glUniform1i(glGetUniformLocation(program, "glyphs"), 1);
    glUniform1i(glGetUniformLocation(program, "hdAtlas"), 1);
    glUniform1i(glGetUniformLocation(program, "portPages"), 1);
    glGenVertexArrays(1, &vertexArray); glBindVertexArray(vertexArray);
    glGenBuffers(1, &vertexBuffer); glBindBuffer(GL_ARRAY_BUFFER, vertexBuffer);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, sizeof(BatchVertex), (void *)offsetof(BatchVertex, x));
    glVertexAttribPointer(1, 4, GL_FLOAT, GL_FALSE, sizeof(BatchVertex), (void *)offsetof(BatchVertex, r));
    glVertexAttribPointer(2, 2, GL_FLOAT, GL_FALSE, sizeof(BatchVertex), (void *)offsetof(BatchVertex, u));
    glVertexAttribIPointer(3, 4, GL_SHORT, sizeof(BatchVertex), (void *)offsetof(BatchVertex, texture));
    glVertexAttribIPointer(4, 4, GL_UNSIGNED_BYTE, sizeof(BatchVertex), (void *)offsetof(BatchVertex, window));
    glVertexAttribIPointer(5, 4, GL_UNSIGNED_BYTE, sizeof(BatchVertex), (void *)offsetof(BatchVertex, mode));
    glVertexAttribIPointer(6, 4, GL_UNSIGNED_SHORT, sizeof(BatchVertex), (void *)offsetof(BatchVertex, hd));
    glVertexAttribIPointer(7, 4, GL_SHORT, sizeof(BatchVertex), (void *)offsetof(BatchVertex, hdTexels));
    glVertexAttribIPointer(8, 4, GL_SHORT, sizeof(BatchVertex), (void *)offsetof(BatchVertex, filterBounds));
    for (int i = 0; i < 9; i++) glEnableVertexAttribArray((GLuint)i);

    glGenTextures(1, &vramTexture); glBindTexture(GL_TEXTURE_2D, vramTexture);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_R16UI, 1024, 1024, 0, GL_RED_INTEGER, GL_UNSIGNED_SHORT, words);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    GLuint output, dummy, fbo;
    glGenTextures(1, &output); glBindTexture(GL_TEXTURE_2D, output);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA32F, 1, 1, 0, GL_RGBA, GL_FLOAT, NULL);
    glGenFramebuffers(1, &fbo); glBindFramebuffer(GL_FRAMEBUFFER, fbo);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, output, 0);
    assert(glCheckFramebufferStatus(GL_FRAMEBUFFER) == GL_FRAMEBUFFER_COMPLETE);
    glActiveTexture(GL_TEXTURE1); glGenTextures(1, &dummy); glBindTexture(GL_TEXTURE_2D, dummy);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, 1, 1, 0, GL_RGBA, GL_UNSIGNED_BYTE, "\0\0\0\0");
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glViewport(0, 0, 1, 1); glDisable(GL_BLEND); glDisable(GL_DEPTH_TEST);

    const float full = 248.0f / 255.0f, half = 124.0f / 255.0f;
    uint16_t *palette = words + 16 * 1024 + 32;
    palette[1] = 31; palette[2] = 31 << 5; palette[3] = 31 << 10; palette[4] = 0x7fff;
    words[0] = 0x21; words[1024] = 0x43; Upload();
    ThreePointSquare(0);
    Render(0, 0, .5f, .5f, 128, -1, 0, full, 0, 0, 1); /* PS1 remains nearest */
    Render(0, 0, 1.5f, .5f, 128, -1, 0, 0, full, 0, 1); /* second 4-bit index */
    Render(0, 0, .5f, .5f, 255, -1, 0, 1, 0, 0, 1); /* PS1 overbright modulation retained */
    Render(1, 0, .5f, .5f, PortN64Modulation(255), -1, 0, 0, half, half, 1);
    Render(1, 0, .5f, .5f, PortN64Modulation(128), -1, 0, 0, half / 2, half / 2, 1);
    Render(1, 0, 1.75f, 0, 128, -1, 0, 0, full, 0, 1); /* no neighbor bleed */
    Render(1, 0, -.25f, -.25f, 128, -1, 0, full, 0, 0, 1); /* clamp before negative wrap */
    words[0] = 0x0201; words[1024] = 0x0403; Upload();
    ThreePointSquare(1);
    Render(0, 1, .5f, .5f, 128, -1, 0, full, 0, 0, 1);
    Render(0, 1, 1.5f, .5f, 128, -1, 0, 0, full, 0, 1); /* second 8-bit index */
    words[0] = 31; words[1] = 31 << 5; words[1024] = 31 << 10; words[1025] = 0x7fff; Upload();
    ThreePointSquare(2);
    Render(0, 2, .5f, .5f, 128, -1, 0, full, 0, 0, 1);
    Render(0, 2, 1.5f, .5f, 128, -1, 0, 0, full, 0, 1); /* direct colour */
    /* Only top-left is opaque: its triangular weight is 1-u-v, not (1-u)*(1-v). */
    words[0] = 0x01; words[1024] = 0; Upload();
    Render(1, 0, .25f, .25f, 128, -1, 0, half, 0, 0, .5f);
    Render(1, 0, .5f, .5f, 128, -1, 0, 0, 0, 0, 0);
    words[0] = words[1024] = 0x01; Upload();
    Render(0, 0, 1, 0, 128, -1, 0, 0, 0, 0, 0); /* PS1 transparent texel */
    Render(1, 0, .5f, .5f, 128, -1, 0, half, 0, 0, .5f); /* premultiplied transparent edge */
    Render(1, 0, .5f, .5f, 128, -1, 3, 0, 0, 0, 0); /* edge must not occlude */
    words[0] = words[1024] = 0x21; palette[2] |= 0x8000; Upload();
    Render(0, 0, 1, 0, 128, 0, 0, 0, half, 0, .5f); /* PS1 semi-transparent texel */
    Render(1, 0, .5f, .5f, 128, 0, 0, half, half / 2, 0, .75f);
    Render(1, 0, .5f, .5f, 128, 1, 0, half, half, 0, .5f);
    Render(1, 0, .5f, .5f, 128, 3, 0, half, half / 4, 0, .5f);
    Render(1, 0, .5f, .5f, 128, 2, 1, half, 0, 0, .5f);
    Render(1, 0, .5f, .5f, 128, 2, 2, 0, half, 0, 0);
    palette[2] &= 0x7fff; words[63] = words[1024 + 63] = 0x2100;
    bounds[0] = 254; bounds[2] = 255; Upload();
    Render(1, 0, 255.5f, 0, 128, -1, 0, 0, full, 0, 1); /* clamp before page wrap */
    Render(0, 0, 256.5f, 0, 128, -1, 0, full, 0, 0, 1); /* PS1 wraps rather than clamps */
    Render(0, 0, -.25f, 0, 128, -1, 0, 0, full, 0, 1);
    bounds[0] = 0; bounds[2] = 1;
    HdThreePoint();
    DepthBatches(fbo, output, dummy);
    SDL_GL_DestroyContext(context); SDL_DestroyWindow(window); SDL_Quit();
    puts("Production GL shaders: N64 three-point/native/HD/brightness/bounds/coverage/blend passes and unchanged PS1 sampling: OK");
    return 0;
}
