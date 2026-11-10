/* Offscreen readback of the production shaders. Requires a desktop OpenGL context.
   Run with tools/bugtest/render.sh; no disc, game settings or visible window needed. */
#define PORT_GLSL_VERSION "#version 330 core\n"
#include "../../platform/port_gl.h"
static int drawCalls;
static void CountDrawArrays(GLenum mode, GLint first, GLsizei count)
{
    drawCalls++;
    glDrawArrays(mode, first, count);
}
#define glDrawArrays CountDrawArrays
#include "../../platform/render/gl_backend.c"
#include "../../platform/render/n64_gl.c"
#undef glDrawArrays
#include "../../platform/render/saturn_gl.c"
#include <assert.h>
#include <math.h>

static PFNGLISPROGRAMPROC isProgram;
static int testAtlasUnit = 1;

static void TestProgram(RendererId id)
{
    UsePipeline(id);
    glUseProgram(scene->program);
    glUniform2f(scene->uniformTarget, 1, 1);
    glUniform2f(scene->uniformOrigin, 0, 0);
    glUniform1f(scene->uniformDepthPull, 1);
    glUniform1i(scene->uniformDepthPass, 0);
    glUniform1i(scene->uniformVram, 0);
    glUniform1i(scene->uniformGlyphs, 1);
    glUniform1i(scene->uniformHdAtlas, testAtlasUnit);
    glUniform1i(scene->uniformPortPages, 1);
    glBindVertexArray(vertexArray);
    glBindBuffer(GL_ARRAY_BUFFER, vertexBuffer);
}

/* Flush uses the real batching/depth code; no game or settings file is needed. */
PortSettings portSettings = {.render = RENDER_DEFAULTS};
static void ResetRenderSettings(void)
{
    RenderDefaults(&portSettings.render, RENDER_AUTO);
    portSettings.render.modernForAllPlatforms = 0;
}
static int n64Track;
int portPal;
static int testLinkRole, testTracing;
int PortLinkRole(void) { return testLinkRole; }
int PortTracing(void) { return testTracing; }
static int testWindowWidth = 1280, testWindowHeight = 960;
void PortDrawableSize(int *w, int *h) { *w = testWindowWidth; *h = testWindowHeight; }
void HdTexSetScale(int value) { (void)value; }
void HdTexChanged(int x, int y, int w, int h) { (void)x; (void)y; (void)w; (void)h; }
int PortOriginalTrackIs64(void) { return n64Track; }
int PortOriginalTrack(void) { return -1; }
int PortPcData(void) { return 0; }
int PortSaturnDisc(void) { return 0; }
const struct Disc *PsxIoDisc(void) { return NULL; }

int PortN64TextureInfo(int px,int py,int depth,int cx,int cy,float u,float v,int bounds[4],int *flags)
{ (void)px;(void)py;(void)depth;(void)cx;(void)cy;(void)u;(void)v;(void)bounds;(void)flags;return 0; }

/* The synthetic scene can enable replacement lookups without loading a pack. */
static int hdLookups;
int HdTexActive(void) { return portSettings.hdTextures; }
unsigned HdTexEpoch(void) { return 0; }
int HdTexImageCount(void) { return 0; }
const uint8_t *HdTexImage(int image, int *w, int *h) { (void)image;*w=*h=0;return NULL; }
int HdTexFind(int px, int py, int depth, int cx, int cy, float u0, float v0, float u1, float v1, HdTexHit *hit)
{
    (void)px; (void)py; (void)depth; (void)cx; (void)cy;
    (void)u0; (void)v0; (void)u1; (void)v1; (void)hit;
    hdLookups++;
    return 0;
}

uint16_t psxVram[VRAM_HEIGHT][VRAM_WIDTH];
uint16_t psxVramHigh[VRAM_HEIGHT][VRAM_WIDTH];

static uint16_t words[1024 * 1024];
static BatchVertex triangle[3];
static int bounds[4] = {0, 0, 1, 1};
static uint16_t testHd[4];
static int16_t testHdTexels[4];
static uint8_t testWindow[4];
static uint8_t testShade;
static uint8_t testAddress;
static int testPs1Light;
static float testFade = 1.0f;
static int testPageX;
static int testPixelX;

static void Upload(void)
{
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, vramTexture);
    glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, 1024, 1024, GL_RED_INTEGER, GL_UNSIGNED_SHORT, words);
}

static void Render(RendererId renderer, int filtered, int depth, float u, float v, int tint, int semi, int pass,
                   float red, float green, float blue, float alpha)
{
    float result[4], expected[4] = {red, green, blue, alpha};
    TestProgram(renderer);
    glUniform2f(scene->uniformOrigin, testPixelX, 0);
    memset(triangle, 0, sizeof(triangle));
    triangle[1].x = 2; triangle[2].y = 2;
    for (int i = 0; i < 3; i++) {
        BatchVertex *b = &triangle[i];
        b->x += testPixelX;
        b->w = 1; b->r = b->g = b->b = tint / 255.0f; b->a = testFade;
        b->u = u; b->v = v;
        b->texture[2] = 32; b->texture[3] = 16;
        b->texture[0] = (int16_t)testPageX;
        b->mode[0] = 1; b->mode[2] = (uint8_t)(depth | (testAddress >> 2) | (testPs1Light ? 32 : 0));
        b->mode[1] = testShade;
        b->mode[3] = (uint8_t)((filtered << 1) | ((semi + 1) << 4));
        for (int k = 0; k < 4; k++) b->filterBounds[k] = (int16_t)bounds[k];
        memcpy(b->hd, testHd, sizeof(testHd));
        memcpy(b->hdTexels, testHdTexels, sizeof(testHdTexels));
        memcpy(b->window, testWindow, sizeof(testWindow));
    }
    glBufferData(GL_ARRAY_BUFFER, sizeof(triangle), triangle, GL_STREAM_DRAW);
    glUniform1i(glGetUniformLocation(scene->program, "pass"), pass);
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
    Render(RENDER_N64, 1, depth, 0, 0, 128, -1, 0, full, 0, 0, 1);
    Render(RENDER_N64, 1, depth, 1, 0, 128, -1, 0, 0, full, 0, 1);
    Render(RENDER_N64, 1, depth, 0, 1, 128, -1, 0, 0, 0, full, 1);
    Render(RENDER_N64, 1, depth, 1, 1, 128, -1, 0, full, full, full, 1);
    Render(RENDER_N64, 1, depth, .25f, .25f, 128, -1, 0, full / 2, full / 4, full / 4, 1);
    Render(RENDER_N64, 1, depth, .75f, .75f, 128, -1, 0, full / 2, full * .75f, full * .75f, 1);
    Render(RENDER_N64, 1, depth, .75f, .125f, 128, -1, 0, full / 8, full * .75f, full / 8, 1);
    Render(RENDER_N64, 1, depth, .75f, .5f, 128, -1, 0, full / 4, full * .75f, full / 2, 1);
    Render(RENDER_N64, 1, depth, .25f, .75f, 128, -1, 0, 0, full / 4, full * .75f, 1);
    Render(RENDER_N64, 1, depth, .5f, .5f, 128, -1, 0, 0, full / 2, full / 2, 1);
    /* The fractions truncate to 7/32 and 15/32, leaving 10/32 for red. */
    Render(RENDER_N64, 1, depth, .249f, .499f, 128, -1, 0, full * 10 / 32, full * 7 / 32, full * 15 / 32, 1);
    /* Neighbouring 1/32 steps on either side of the diagonal use opposite third corners. */
    Render(RENDER_N64, 1, depth, .5f, .46875f, 128, -1, 0, full / 32, full / 2, full * 15 / 32, 1);
    Render(RENDER_N64, 1, depth, .5f, .53125f, 128, -1, 0, full / 32, full / 2, full * 17 / 32, 1);
}

/* Bilinear has texel centres at .5 and uses all four corners, with unquantized weights. */
static void BilinearSquare(int depth)
{
    const float full = 248.0f / 255.0f;
    Render(RENDER_PC, 2, depth, .5f, .5f, 128, -1, 0, full, 0, 0, 1);
    Render(RENDER_PC, 2, depth, 1.5f, .5f, 128, -1, 0, 0, full, 0, 1);
    Render(RENDER_PC, 2, depth, .5f, 1.5f, 128, -1, 0, 0, 0, full, 1);
    Render(RENDER_PC, 2, depth, 1.5f, 1.5f, 128, -1, 0, full, full, full, 1);
    Render(RENDER_PC, 2, depth, 1, 1, 128, -1, 0, full / 2, full / 2, full / 2, 1);
    Render(RENDER_PC, 2, depth, .75f, .75f, 128, -1, 0, full * .625f, full / 4, full / 4, 1);
    Render(RENDER_PC, 2, depth, 1.1f, .8f, 128, -1, 0, full * .46f, full * .6f, full * .3f, 1);
}

static void HdFiltering(void)
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
    testAtlasUnit = 2; glUniform1i(glGetUniformLocation(scene->program, "hdAtlas"), 2);
    testHd[0] = testHd[1] = 1; testHd[2] = testHd[3] = 2;
    testHdTexels[2] = testHdTexels[3] = 2;
    ThreePointSquare(0);
    BilinearSquare(0);
    Render(RENDER_PC, 2, 0, 0, 0, 128, -1, 0, full, 0, 0, 1); /* atlas edge clamps */
    Render(RENDER_PC, 2, 0, 1.9f, .5f, 128, -1, 0, 0, full, 0, 1);
    Render(RENDER_N64, 1, 0, 1.75f, 0, 128, -1, 0, 0, full, 0, 1);
    Render(RENDER_PS1, 0, 0, .5f, .5f, 128, -1, 0, full, 0, 0, 1); /* PS1 HD stays nearest */
    /* Source addressing must survive HD replacement, including edges and negative/repeated UVs. */
    testShade = SHADE_N64 | SHADE_AFFINE_COLOUR;
    testAddress = 0x40;
    Render(RENDER_N64, 1, 2, 1.75f, 0, 255, -1, 0, full * .75f, full * .25f, 0, 1);
    Render(RENDER_N64, 1, 2, -.25f, 0, 255, -1, 0, full * .75f, full * .25f, 0, 1);
    Render(RENDER_PC, 2, 2, 2.25f, .5f, 255, -1, 0, full * .75f, full * .25f, 0, 1);
    Render(RENDER_N64, 0, 2, 3, 0, 255, -1, 0, 0, full, 0, 1);
    testAddress = 0x50;
    Render(RENDER_N64, 1, 2, 2.25f, 0, 255, -1, 0, full * .25f, full * .75f, 0, 1);
    Render(RENDER_N64, 1, 2, -.25f, 0, 255, -1, 0, full, 0, 0, 1);
    Render(RENDER_N64, 0, 2, 2, 0, 255, -1, 0, 0, full, 0, 1);
    testAddress = 0x60;
    Render(RENDER_N64, 1, 2, 0, 2.25f, 255, -1, 0, full * .25f, 0, full * .75f, 1);
    /* Upscaled HD coordinates use the native mask but address replacement pixels. */
    testHdTexels[2] = testHdTexels[3] = 1;
    bounds[2] = bounds[3] = 0;
    testAddress = 0x40;
    Render(RENDER_N64, 1, 2, .75f, -.25f, 255, -1, 0, full, 0, 0, 1);
    testHdTexels[2] = testHdTexels[3] = 2;
    bounds[2] = bounds[3] = 1;
    testShade = testAddress = 0;
    /* Two replacement pixels per source texel: retain the existing centre alignment. */
    testHdTexels[2] = testHdTexels[3] = 1;
    Render(RENDER_N64, 1, 0, .125f, .125f, 128, -1, 0, full / 2, full * .75f, full * .75f, 1);
    Render(RENDER_N64, 1, 0, .875f, .875f, 128, -1, 0, full, full, full, 1);
    Render(RENDER_PC, 2, 0, .25f, .25f, 128, -1, 0, full, 0, 0, 1);
    Render(RENDER_PC, 2, 0, .5f, .5f, 128, -1, 0, full / 2, full / 2, full / 2, 1);
    /* Nearest N64 sampling must prefer the green HD texel over red native VRAM,
       with the same material and primitive-transparency rules in every profile. */
    testHdTexels[2] = testHdTexels[3] = 2;
    words[0] = words[1] = 31;
    Upload();
    for (int id = RENDER_PS1; id <= RENDER_N64; id++) {
        RenderSettings settings = RENDER_DEFAULTS;
        settings.pc.bilinear = 0;
        settings.n64.threePoint = 0;
        RenderFrame frame = RenderSnapshot((RendererId)id, &settings);
        RenderMaterial material = {.n64 = 1, .textured = 1, .bounds = 1};
        RenderStyle style = RenderPrepare(&frame, &material);
        assert(style.filter == 0);
        testShade = (uint8_t)style.shade;
        Render(frame.id, 0, 2, 1, 0, 255, -1, 0, 0, full, 0, 1);
        Render(frame.id, 0, 2, 1, 0, 255, 0, 0, 0, full / 2, 0, .5f);
        Render(frame.id, 0, 2, 1, 0, 255, 0, 3, 0, 0, 0, 0); /* translucent texels never write depth */
        /* Outside the replacement's coverage, retain the native N64 fallback. */
        testHdTexels[2] = 1;
        Render(frame.id, 0, 2, 1, 0, 255, -1, 0, id == RENDER_PS1 ? full : 1, 0, 0, 1);
        testHdTexels[2] = 2;
        /* An HD colour key must not reveal the opaque native texture underneath. */
        const float transparent[4] = {0, full, 0, 0};
        glActiveTexture(GL_TEXTURE2);
        glBindTexture(GL_TEXTURE_2D, atlas);
        glTexSubImage2D(GL_TEXTURE_2D, 0, 2, 1, 1, 1, GL_RGBA, GL_FLOAT, transparent);
        Render(frame.id, 0, 2, 1, 0, 255, -1, 0, 0, 0, 0, 0);
        glTexSubImage2D(GL_TEXTURE_2D, 0, 2, 1, 1, 1, GL_RGBA, GL_FLOAT, square[1]);
    }
    testShade = 0;
    puts("N64 nearest HD replacements: all profiles, primitive transparency, depth and native fallback: OK");
    memset(testHd, 0, sizeof(testHd)); memset(testHdTexels, 0, sizeof(testHdTexels));
    testAtlasUnit = 1; glUniform1i(glGetUniformLocation(scene->program, "hdAtlas"), 1);
    glDeleteTextures(1, &atlas);
    glActiveTexture(GL_TEXTURE0);
}

static void GlyphUpload(void)
{
    GLint previous, width, filter;
    GLuint saved = glyphTexture;
    size_t bytes = GLYPH_ATLAS_SIZE * GLYPH_ATLAS_SIZE * 4;
    uint8_t *pixels = malloc(bytes);
    assert(pixels);
    glActiveTexture(GL_TEXTURE1);
    glGetIntegerv(GL_TEXTURE_BINDING_2D, &previous);
    CreateGlyphTexture();
    for (int value = 64; value <= 128; value += 64) {
        memset(pixels, value, bytes);
        for (size_t i = 3; i < bytes; i += 4) pixels[i] = 255;
        GpuSetGlyphMemory((const uint32_t *)pixels);
        glActiveTexture(GL_TEXTURE1);
        glGetTexParameteriv(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, &filter);
        glGetTexLevelParameteriv(GL_TEXTURE_2D, 1, GL_TEXTURE_WIDTH, &width);
        assert(filter == GL_NEAREST && width == 0);
        for (int id = RENDER_PS1; id <= RENDER_N64; id++) {
            testShade = SHADE_RAW;
            int filtered = id == RENDER_PS1 ? 0 : id == RENDER_PC ? 2 : 1;
            float c = value / 255.0f;
            Render((RendererId)id, filtered, 3, 10, 10, 255, -1, 0, c, c, c, 1);
        }
    }
    glActiveTexture(GL_TEXTURE1);
    glBindTexture(GL_TEXTURE_2D, (GLuint)previous);
    glDeleteTextures(1, &glyphTexture);
    glyphTexture = saved;
    glActiveTexture(GL_TEXTURE0);
    testShade = 0;
    free(pixels);
    puts("Production glyph uploads remain sampleable by every renderer without allocating mipmaps: OK");
}

/* Full-colour source encoding must not force PC filtering or bypass PS1 rasterization. */
static void FullColourProfiles(void)
{
    const uint8_t square[] = {255,0,0,255, 0,255,0,255, 0,0,255,255, 255,255,255,255};
    const uint8_t grey[] = {104,104,104,255, 104,104,104,255, 104,104,104,255, 104,104,104,255};
    const float expected[3][2][3] = {
        {{1,0,0}, {0,1,0}}, /* nearest */
        {{0,.5f,.5f}, {.5f,1,.5f}}, /* three-point */
        {{1,0,0}, {.5f,.5f,0}} /* bilinear */
    };
    GLuint rgba;
    GLint previous;
    glActiveTexture(GL_TEXTURE1);
    glGetIntegerv(GL_TEXTURE_BINDING_2D, &previous);
    glGenTextures(1, &rgba); glBindTexture(GL_TEXTURE_2D, rgba);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    testHdTexels[2] = testHdTexels[3] = 2;
    for (int page = 0; page < 2; page++) {
        int size = page ? 256 : 512; /* glyph coordinates address a higher-resolution atlas */
        float unit = 256.0f / size;
        testPageX = page ? 64 : 0;
        glActiveTexture(GL_TEXTURE1); glBindTexture(GL_TEXTURE_2D, rgba);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, size, size, 0, GL_RGBA, GL_UNSIGNED_BYTE, NULL);
        for (int id = RENDER_PS1; id <= RENDER_N64; id++) for (int enabled = 0; enabled < 2; enabled++)
        for (int spatial = 0; spatial < 2; spatial++) {
            for (int other = 0; other < 2; other++) {
                RenderSettings settings = RENDER_DEFAULTS;
                RenderMaterial m = {.fullColour = 1, .textured = 1, .dither = 1, .ui = !spatial};
                settings.ps1 = (Ps1RenderSettings){other, other, other, other, other};
                settings.pc = (PcRenderSettings){other, other, other};
                settings.n64 = (N64RenderSettings){other, other, other, other, other, other};
                if (id == RENDER_PS1) settings.ps1 = (Ps1RenderSettings){enabled, enabled, enabled, enabled, enabled};
                if (id == RENDER_PC) settings.pc = (PcRenderSettings){enabled, enabled, enabled};
                if (id == RENDER_N64) settings.n64.threePoint = enabled;
                RenderFrame f = RenderSnapshot((RendererId)id, &settings);
                RenderStyle style = RenderPrepare(&f, &m);
                testShade = style.shade;
                glActiveTexture(GL_TEXTURE1); glBindTexture(GL_TEXTURE_2D, rgba);
                glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, 2, 2, GL_RGBA, GL_UNSIGNED_BYTE, square);
                int filter = enabled ? (id == RENDER_PC ? 2 : id == RENDER_N64 && spatial) : 0;
                float precision = id == RENDER_PS1 && enabled ? 248.0f / 255 : 1;
                for (int sample = 0; sample < 2; sample++) {
                    const float *c = expected[filter][sample];
                    Render(f.id, style.filter, 3, (sample ? 1 : .5f) * unit, .5f * unit, page ? 255 : 128,
                           -1, 0, c[0] * precision, c[1] * precision, c[2] * precision, 1);
                }
                glActiveTexture(GL_TEXTURE1); glBindTexture(GL_TEXTURE_2D, rgba);
                glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, 2, 2, GL_RGBA, GL_UNSIGNED_BYTE, grey);
                float c = (id == RENDER_PS1 && enabled ? 96.0f : 104.0f) / 255;
                Render(f.id, style.filter, 3, .5f * unit, .5f * unit, page ? 255 : 128, -1, 0, c, c, c, 1);
                if (id == RENDER_PS1 && enabled) {
                    f.settings.ps1.dither = 0;
                    testShade = RenderPrepare(&f, &m).shade;
                    Render(f.id, 0, 3, .5f * unit, .5f * unit, page ? 255 : 128,
                           -1, 0, 104.0f/255, 104.0f/255, 104.0f/255, 1);
                }
            }
        }
    }
    testShade = 0; testPageX = 0;
    memset(testHdTexels, 0, sizeof(testHdTexels));
    glActiveTexture(GL_TEXTURE1); glBindTexture(GL_TEXTURE_2D, (GLuint)previous);
    glDeleteTextures(1, &rgba); glActiveTexture(GL_TEXTURE0);
    puts("Full-colour PC pages and high-resolution glyphs obey each renderer's filtering and PS1 precision/dither controls: OK");
}

static void DepthTriangle(const GpuDrawState *state, float x, float depth, int red, int green)
{
    GpuVertex v = {0};
    v.x = x; v.y = 100; v.w = depth;
    v.r = (uint8_t)red; v.g = (uint8_t)green; v.a = 255;
    v.u = v.v = .5f;
    RenderMaterial material = {state->n64, state->depth == 3, state->textured,
                               state->raw, state->dither, primitiveFilterBounds[2] >= 0};
    RenderStyle style = RenderPrepare(RenderCurrent(), &material);
    Begin(state, 3);
    Add(&v, style.shade);
    v.x += 2; Add(&v, style.shade);
    v.x -= 2; v.y += 2; Add(&v, style.shade);
}

/* Affine world primitives and HUD share zero-depth transport, but only the former use PC tint. */
static void BillboardModulation(void)
{
    RenderSettings saved = portSettings.render;
    const float full = 248.0f / 255.0f;
    words[0] = words[1] = words[1024] = words[1025] = 0x7fff;
    Upload();
    for (int id = RENDER_PS1; id <= RENDER_PC; id++) {
        for (int filtered = 0; filtered < 2; filtered++) {
            for (int depth = 0; depth < 2; depth++) {
                ResetRenderSettings();
                portSettings.render.selection = (RendererId)id;
                portSettings.render.ps1.colourPrecision = 0;
                portSettings.render.ps1.depth = portSettings.render.pc.depth = depth;
                portSettings.render.pc.bilinear = filtered;
                RenderCommit();
                for (int ui = 0; ui < 2; ui++) {
                    RenderMaterial material = {.textured = 1, .ui = ui};
                    RenderStyle style = RenderPrepare(RenderCurrent(), &material);
                    GpuDrawState draw = {.textured = 1, .depth = 2, .semi = -1, .filtered = style.filter};
                    GpuVertex v = {.y = 100, .r = 128, .g = 128, .b = 128, .a = 255,
                                   .u = .5f, .v = .5f, .world = !ui};
                    float pixel[4], z;
                    glDisable(GL_SCISSOR_TEST); glDepthMask(GL_TRUE);
                    glClearColor(0, 0, 0, 1); glClearDepth(.25);
                    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
                    Begin(&draw, 3);
                    Add(&v, style.shade);
                    v.x += 2; Add(&v, style.shade);
                    v.x -= 2; v.y += 2; Add(&v, style.shade);
                    assert(!batchDepth);
                    for (int i = 0; i < 3; i++) assert(batch[i].w == 0);
                    Flush();
                    glReadPixels(0, 100, 1, 1, GL_RGBA, GL_FLOAT, pixel);
                    glReadPixels(0, 100, 1, 1, GL_DEPTH_COMPONENT, GL_FLOAT, &z);
                    float expected = id == RENDER_PC && !ui ? full * 128 / 255 : full;
                    for (int c = 0; c < 3; c++) assert(fabsf(pixel[c] - expected) < .002f);
                    assert(fabsf(pixel[3] - 1) < .002f && fabsf(z - .25f) < .00001f);
                }
            }
        }
    }
    portSettings.render = saved; RenderCommit();
    assert(glGetError() == GL_NO_ERROR);
    puts("Affine world primitives use PC tint; HUD/PS1 brightness and depth-free transport are preserved: OK");
}

static void ModernBrightness(void)
{
    RenderSettings saved = portSettings.render;
    const float full = 248.0f / 255.0f;
    words[0] = 0x7fff;
    Upload();
    portSettings.render.selection = RENDER_MODERN;
    const int levels[] = {MODERN_BRIGHTNESS_MAX, MODERN_BRIGHTNESS_DEFAULT, MODERN_BRIGHTNESS_MIN};
    for (int i = 0; i < SDL_arraysize(levels); i++) {
        portSettings.render.modern.brightness = levels[i];
        RenderCommit();
        glDisable(GL_SCISSOR_TEST);
        glClearColor(0, 0, 0, 1);
        glClear(GL_COLOR_BUFFER_BIT);
        for (int mode = 0; mode < 4; mode++) {
            int x = 2 * mode, tint = mode == 1 ? 255 : mode == 3 ? 32 : 128;
            GpuDrawState draw = {.textured = 1, .raw = mode == 3, .depth = 2, .semi = -1};
            GpuVertex v[3] = {
                {.x = x, .y = 100}, {.x = x + 2, .y = 100}, {.x = x, .y = 102}
            };
            for (int corner = 0; corner < 3; corner++) {
                v[corner].r = v[corner].g = v[corner].b = tint;
                v[corner].a = 255;
                v[corner].world = mode != 2;
                v[corner].u = v[corner].v = .5f;
            }
            GpuTriangles(&draw, v, 3);
        }
        Flush();
        float gain = levels[i] / 100.0f;
        float expected[] = {full * gain, fminf(full * 255 / 128 * gain, 1.0f), full, full};
        for (int mode = 0; mode < 4; mode++) {
            float pixel[4];
            glReadPixels(2 * mode, 100, 1, 1, GL_RGBA, GL_FLOAT, pixel);
            for (int c = 0; c < 3; c++) assert(fabsf(pixel[c] - expected[mode]) < .002f);
            assert(fabsf(pixel[3] - 1) < .002f);
        }
    }
    portSettings.render = saved; RenderCommit();
    assert(glGetError() == GL_NO_ERROR);
    puts("Modern brightness: PS1 maximum, half-strength floor, tint before clipping, and HUD/raw texture handling: OK");
}

/* The same sloped road patch is drawn at the four track grid resolutions.
   Far vertices have twice the depth, so a screen-linear tint fails this test. */
static void ModernTrackLighting(void)
{
    RenderSettings saved = portSettings.render;
    portSettings.render.selection = RENDER_MODERN;
    portSettings.render.modern.brightness = 100;
    portSettings.render.modern.bilinear = 0;
    portSettings.render.modern.bloom = 0;
    portSettings.render.modern.smoothTrackLighting = 1;
    RenderCommit();
    words[0] = 0x7fff;
    Upload();

    GpuDrawState draw = {.textured = 1, .depth = 2, .semi = -1, .smoothTrackLighting = 1};
    float reference[24][32], pixels[24][32];
    for (int size = 1; size <= 8; size *= 2) {
        glDisable(GL_SCISSOR_TEST); glDepthMask(GL_TRUE);
        glClearColor(0, 0, 0, 1); glClearDepth(1);
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
        for (int row = 0; row < size; row++) {
            for (int col = 0; col < size; col++) {
                GpuVertex v[4] = {0};
                for (int i = 0; i < 4; i++) {
                    float u = (float)(col + (i & 1)) / size, t = (float)(row + (i >> 1)) / size;
                    v[i].w = 100 * (1 + t);
                    v[i].x = 3200 * u / v[i].w;
                    v[i].y = 100 + 4800 * t / v[i].w;
                    v[i].r = v[i].g = v[i].b = 32 + (int)(96 * t);
                    v[i].a = 255;
                    v[i].u = v[i].v = .5f;
                }
                GpuTriangles(&draw, v, 3);
                GpuTriangles(&draw, v + 1, 3);
            }
        }
        Flush();
        glReadPixels(0, 100, 32, 24, GL_RED, GL_FLOAT, pixels);
        for (int y = 0; y < 24; y++) {
            float screen = (y + .5f) / 24;
            float along = screen / (2 - screen);
            float expected = (248.0f / 255) * (32 + 96 * along) / 128;
            /* Every row covers at least the first sixteen pixels. */
            for (int x = 0; x < 16; x++) assert(fabsf(pixels[y][x] - expected) < .003f);
            for (int x = 0; x < 32; x++) {
                if (size == 1) reference[y][x] = pixels[y][x];
                else assert(fabsf(pixels[y][x] - reference[y][x]) < .003f);
            }
        }
    }
    portSettings.render = saved; RenderCommit();
    assert(glGetError() == GL_NO_ERROR);
    puts("Modern track lighting: perspective-correct tint matches across all four detail grids: OK");
}

/* Exercise a tilted 3D billboard through the production shader and depth batches. */
static void BillboardQuad(int textured, int ui, float topDepth, float bottomDepth)
{
    RenderMaterial material = {.textured = textured, .ui = ui};
    RenderStyle style = RenderPrepare(RenderCurrent(), &material);
    GpuDrawState draw = {.textured = textured, .depth = 2, .semi = -1, .filtered = style.filter};
    GpuVertex v[4] = {0};
    static const int corners[] = {0, 1, 3, 0, 3, 2};
    assert(!(style.shade & SHADE_AFFINE_UV));
    for (int i = 0; i < 4; i++) {
        v[i] = (GpuVertex){.x = (i & 1) * 8, .y = i < 2 ? 100 : 108,
            .w = ui ? 0 : i < 2 ? topDepth : bottomDepth,
            .r = textured ? (ui ? 128 : 255) : 0, .g = textured ? (ui ? 128 : 255) : 0,
            .b = 255, .a = 255, .u = .5f, .v = ui || i < 2 ? .5f : 1.5f,
            .world = !ui, .exactDepth = !ui};
    }
    Begin(&draw, 6);
    for (int i = 0; i < 6; i++) Add(&v[corners[i]], style.shade);
    Flush();
}

static void ModernFilteredTriangle(const GpuDrawState *draw, int x, float u, float du, int world)
{
    GpuVertex v[3] = {
        {.x = x, .y = 100, .u = u},
        {.x = x + 3, .y = 100, .u = u + du},
        {.x = x, .y = 103, .u = u}
    };
    for (int i = 0; i < 3; i++) {
        v[i].v = 202.5f;
        v[i].a = 255;
        v[i].world = world;
    }
    GpuTriangles(draw, v, 3);
}

static void ModernFiltering(void)
{
    RenderSettings saved = portSettings.render;
    portSettings.render.selection = RENDER_MODERN;

    /* One 32x32 indexed tile and a contrasting word on its left. Every
       other row is blue; the remaining rows alternate red and yellow. */
    GpuDrawState draw = {.textured = 1, .raw = 1, .semi = -1, .pageX = 64,
                         .clutY = 400, .trackTile = 1};
    psxVram[400][1] = 31;
    psxVram[400][2] = 31 | (31 << 5);
    psxVram[400][3] = 31 << 10;
    psxVram[400][4] = 31 << 5;
    for (int y = 192; y < 224; y++) {
        psxVram[y][71] = 0x4444;
        for (int x = 72; x < 80; x++) psxVram[y][x] = y & 1 ? 0x3333 : 0x2121;
    }
    GpuVramUploaded(72, 192, 8, 32);
    GpuVramChanged(71, 192, 9, 32);
    GpuVramChanged(0, 400, 16, 1);
    for (int bilinear = 0; bilinear <= 1; bilinear++) {
        portSettings.render.modern.bilinear = bilinear;
        RenderCommit();
        glDisable(GL_SCISSOR_TEST);
        glClear(GL_COLOR_BUFFER_BIT);
        ModernFilteredTriangle(&draw, 0, 33.0f, 0.0f, 1);
        assert(primitiveState.filtered == (bilinear ? 2 : 0));
        ModernFilteredTriangle(&draw, 3, 32.25f, 0.0f, 1);
        ModernFilteredTriangle(&draw, 6, 33.0f, 0.0f, 0);
        assert(!primitiveState.filtered); /* HUD uses nearest in both modes. */
        Flush();
        uint8_t pixel[4];
        glReadPixels(0, 100, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, pixel);
        int expectedGreen = bilinear ? 124 : 248;
        assert(pixel[0] == 248 && abs(pixel[1] - expectedGreen) <= 1 && pixel[2] == 0);
        glReadPixels(3, 100, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, pixel);
        assert(pixel[0] == 248 && pixel[1] == 0 && pixel[2] == 0); /* No green from the neighboring upload. */
        glReadPixels(6, 100, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, pixel);
        assert(pixel[0] == 248 && pixel[1] == 248 && pixel[2] == 0);
    }
    psxVram[400][1] = psxVram[400][2] = 31 << 10;
    GpuVramChanged(0, 400, 16, 1);
    ModernFilteredTriangle(&draw, 0, 32.5f, 24.0f, 1);
    Flush();
    uint8_t pixel[4];
    glReadPixels(0, 100, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, pixel);
    assert(pixel[0] == 0 && pixel[1] == 0 && pixel[2] == 248);
    psxVram[400][1] = 0;
    GpuVramChanged(0, 400, 16, 1);
    ModernFilteredTriangle(&draw, 0, 32.5f, 24.0f, 1);
    assert(primitiveState.filtered == 2); /* Transparent tiles stay on the coverage-aware sampler. */
    Flush();
    GpuVramOverwritten(72, 192, 8, 32);
    portSettings.render = saved;
    RenderCommit();
    assert(glGetError() == GL_NO_ERROR);
    puts("Modern filtering: nearest and bilinear pixels, texture edges, palette updates and HUD: OK");
}

static void WorldBillboardDepth(void)
{
    RenderSettings saved = portSettings.render;
    const float full = 248.0f / 255.0f;
    portSettings.render.selection = RENDER_PC;
    portSettings.render.pc = (PcRenderSettings){1, 1, 1};
    RenderCommit();
    words[0] = words[1] = 31;
    words[1024] = words[1025] = 31 << 5;
    Upload();
    for (int scenario = 0; scenario < 4; scenario++) {
        float pixel[4], z;
        glDisable(GL_SCISSOR_TEST); glDepthMask(GL_TRUE);
        glClearColor(0, 0, 0, 1); glClearDepth(1);
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
        if (scenario == 1) BillboardQuad(0, 0, 60, 60); /* nearer wall drawn first */
        BillboardQuad(1, 0, 80, 160);
        if (scenario == 2) BillboardQuad(0, 0, 220, 220); /* farther wall drawn afterwards */
        if (scenario == 3) BillboardQuad(1, 1, 0, 0); /* HUD keeps 128-neutral brightness */
        glReadPixels(1, 103, 1, 1, GL_RGBA, GL_FLOAT, pixel);
        glReadPixels(1, 103, 1, 1, GL_DEPTH_COMPONENT, GL_FLOAT, &z);
        /* At y=103.5, screen t=7/16. Perspective t is (t/160)/((1-t)/80+t/160)=7/25. */
        float red = scenario == 1 ? 0 : scenario == 3 ? full : full * 18 / 25;
        float green = scenario == 1 || scenario == 3 ? 0 : full * 7 / 25;
        assert(fabsf(pixel[0] - red) < .002f && fabsf(pixel[1] - green) < .002f);
        assert(fabsf(pixel[2] - (scenario == 1 ? 1 : 0)) < .002f);
        assert(fabsf(z - (scenario == 1 ? 1 - 1.0f / 60 : 1 - 5.0f / 512)) < .00001f);
    }
    portSettings.render = saved; RenderCommit();
    assert(glGetError() == GL_NO_ERROR);
    puts("PC 3D billboards: perspective UVs, per-corner depth, occlusion in either draw order and unchanged HUD: OK");
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
    scene->uniformVram = glGetUniformLocation(scene->program, "vram");
    scene->uniformGlyphs = glGetUniformLocation(scene->program, "glyphs");
    scene->uniformHdAtlas = glGetUniformLocation(scene->program, "hdAtlas");
    scene->uniformPortPages = glGetUniformLocation(scene->program, "portPages");
    scene->uniformPass = glGetUniformLocation(scene->program, "pass");
    scene->uniformTarget = glGetUniformLocation(scene->program, "target");
    scene->uniformOrigin = glGetUniformLocation(scene->program, "origin");
    scene->uniformDepthPull = glGetUniformLocation(scene->program, "depthPull");
    scene->uniformDepthPass = glGetUniformLocation(scene->program, "depthPass");
    primitiveFilterBounds[0] = primitiveFilterBounds[1] = 0;
    primitiveFilterBounds[2] = primitiveFilterBounds[3] = 1;
    words[0] = words[1024] = 0x11;
    words[16 * 1024 + 33] = 0x7fff;
    Upload();
    portSettings.render.selection = RENDER_PC; RenderCommit();
    for (int filtered = 0; filtered < 3; filtered++) {
        int tint = 255;
        surface.n64 = filtered == 1;
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
                    if (reverse) DepthTriangle(&surface, x, 150, 0, tint);
                    DepthTriangle(&surface, x, 100, tint, 0);
                    if (!reverse) DepthTriangle(&surface, x, 150, 0, tint);
                    Flush();
                    glReadPixels(x, 100, 1, 1, GL_RGBA, GL_FLOAT, pixel);
                    glReadPixels(x, 100, 1, 1, GL_DEPTH_COMPONENT, GL_FLOAT, &z);
                    if (fabsf(pixel[0] - (filtered == 1 ? 1.0f : 248.0f / 255.0f)) > .002f || pixel[1] > .002f || fabsf(z - .99f) > .00001f) {
                        fprintf(stderr, "depth batch: filter=%d leading effect=%d reverse=%d edge=%d: rgb=%g,%g,%g depth=%g\n",
                                filtered, leading, reverse, edge, pixel[0], pixel[1], pixel[2], z);
                        abort();
                    }
                }
            }
        }
    }
    /* N64 depth remains enabled when the PC option is disabled. */
    n64Track = 1; portSettings.render.selection = RENDER_AUTO; RenderCommit();
    surface.n64 = 1;
    glDisable(GL_SCISSOR_TEST); glDepthMask(GL_TRUE);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    DepthTriangle(&surface, 0, 100, 255, 0);
    DepthTriangle(&surface, 0, 150, 0, 255);
    Flush();
    {
        float pixel[4];
        glReadPixels(0, 100, 1, 1, GL_RGBA, GL_FLOAT, pixel);
        assert(fabsf(pixel[0] - 1.0f) < .002f && pixel[1] < .002f);
    }
    n64Track = 0;
    /* Each profile's depth toggle works with the other profile set to the opposite value. */
    for (int id = RENDER_PS1; id <= RENDER_PC; id++) for (int enabled = 0; enabled < 2; enabled++) {
        portSettings.render.selection = (RendererId)id;
        portSettings.render.ps1.depth = id == RENDER_PS1 ? enabled : !enabled;
        portSettings.render.pc.depth = id == RENDER_PC ? enabled : !enabled;
        RenderCommit();
        surface.n64 = 0;
        glDisable(GL_SCISSOR_TEST); glDepthMask(GL_TRUE);
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
        int tint = id == RENDER_PC ? 255 : 128;
        DepthTriangle(&surface, 0, 100, tint, 0);
        DepthTriangle(&surface, 0, 150, 0, tint);
        Flush();
        float pixel[4], full = 248.0f / 255.0f;
        glReadPixels(0, 100, 1, 1, GL_RGBA, GL_FLOAT, pixel);
        assert(fabsf(pixel[0] - (enabled ? full : 0)) < .002f);
        assert(fabsf(pixel[1] - (enabled ? 0 : full)) < .002f);
    }
    portSettings.render.selection = RENDER_PS1;
    portSettings.render.ps1.depth = 0; portSettings.render.pc.depth = 1; RenderCommit();
    assert(glGetError() == GL_NO_ERROR);
    BillboardModulation();
    ModernBrightness();
    ModernTrackLighting();
    ModernFiltering();
    WorldBillboardDepth();
    glDeleteRenderbuffers(1, &depth);
    puts("PC depth batches occlude correctly; PS1 and PC depth toggles are independent: OK");
}

/* Native Saturn pixels use the production batch and scene shader. */
static void NativeSaturn(void)
{
    unsetenv("WIPEOUT_SATURN_VDP2_REFERENCE");
    RenderSettings saved = portSettings.render;
    int oldX=areaX,oldY=areaY,oldW=areaW,oldH=areaH;
    ResetRenderSettings();
    portSettings.render.selection = RENDER_SATURN;
    RenderCommit();
    areaX=0;areaY=100;areaW=8;areaH=8;
    glDisable(GL_SCISSOR_TEST);glClearColor(0,0,0,1);glClear(GL_COLOR_BUFFER_BIT);
    SaturnNativePoly poly = {.mode=0xc4, .colour=0x800a, .corners={0,1,2,3},
                            .gouraud={0x4215,0x4215,0x4215,0x4215}};
    GpuVertex v[4] = {{.x=0,.y=100,.a=255},{.x=7,.y=100,.a=255},
                     {.x=7,.y=107,.a=255},{.x=0,.y=107,.a=255}};
    RenderNativeTarget target = NativeTarget();
    SaturnDraw draw = {.noDepth=1, .target=&target};
    SaturnVdp1Draw(&poly,v,0,100,8,108,SaturnEmit,NULL,&draw);Flush();
    float pixels[8*8*4];
    glReadPixels(0,100,8,8,GL_RGBA,GL_FLOAT,pixels);
    for(int i=0;i<64;i++) {
        assert(fabsf(pixels[i*4]-15.0f*(8.0f/255))<.002f);
        assert(pixels[i*4+1]<.002f && pixels[i*4+2]<.002f);
    }
    /* Production composite transport: one overlay instead of a vertex per write.
       Native/compatibility/native ordering and repeated half blends must survive flushes. */
    GpuDrawState native={.saturn=&poly};
    poly.mode=0xc0;poly.colour=0x801f;
    GpuTriangles(&native,v,4);Flush();
    glReadPixels(0,100,8,8,GL_RGBA,GL_FLOAT,pixels);
    for(int i=0;i<64;i++) assert(fabsf(pixels[i*4]-248.0f/255)<.002f && pixels[i*4+1]<.002f);
    SaturnComposite(NULL,2,102,0xfc00,0,0,255);
    SaturnComposite(NULL,2,102,0xfc00,0,0,255);
    Flush();glReadPixels(0,100,8,8,GL_RGBA,GL_FLOAT,pixels);
    assert(fabsf(pixels[(2*8+2)*4]-62.0f/255)<.01f && fabsf(pixels[(2*8+2)*4+2]-186.0f/255)<.01f);
    GpuDrawState flat={.semi=-1};
    GpuVertex cover[3]={{.x=0,.y=100,.g=248,.a=255},{.x=8,.y=100,.g=248,.a=255},
                       {.x=0,.y=108,.g=248,.a=255}};
    GpuTriangles(&flat,cover,3);Flush();
    SaturnComposite(NULL,1,101,0xffff,-2,0,255);Flush();
    glReadPixels(0,100,8,8,GL_RGBA,GL_FLOAT,pixels);
    assert(pixels[(1*8+1)*4]<.01f && fabsf(pixels[(1*8+1)*4+1]-124.0f/255)<.01f);
    assert(saturnRight==0 && saturnBottom==0);
    uint8_t plane[4]={0,1,2,3};uint16_t palette[256]={0x801f,0x83e0,0xfc00,0xffff};
    SaturnNativeSky(plane,palette,2,2);
    for(int screen=0;screen<2;screen++) {
    areaY=100+256*screen;
    for(int heading=-3;heading<=3;heading++) for(int pitch=0;pitch<=3;pitch+=3) for(int roll=0;roll<2;roll++) {
        GpuDrawState sky = {.skyYaw=(float)heading, .skyPitch=(float)pitch, .skyRoll=roll*1.57079632679f};
        /* Interpolated pictures clear colour/depth without calling GpuClearDepth.
           Replay several into each screen buffer: each needs a fresh sky. */
        GpuClearArea(0,areaY,8,8,0,0,0);
        sky.saturnSky = 1;
        GpuTriangles(&sky, NULL, 0);Flush();
        glReadPixels(0,areaY,8,8,GL_RGBA,GL_FLOAT,pixels);
        for(int y=0;y<8;y++) for(int x=0;x<8;x++) {
            float dx=(x-4)*(352.0f/320),dy=(float)y-4;
            int sx=(int)floorf((roll ? dy : dx)+176+heading/2)&1;
            int sy=(int)floorf((roll ? -dx : dy)+240-pitch/3)&1;
            uint16_t expected=palette[sy*2+sx];
            for(int c=0;c<3;c++) assert(fabsf(pixels[(y*8+x)*4+c]-((expected>>(5*c))&31)*(8.0f/255))<.002f);
        }
    }
    }
    /* A non-symmetric panorama catches a reversed yaw (a two-colour wrap cannot).
       Positive camera yaw must advance the panorama sample, moving it left. */
    areaY=100;
    uint8_t ramp[32];uint16_t rampPalette[256]={0};
    for(int x=0;x<32;x++) { ramp[x]=(uint8_t)x;rampPalette[x]=(uint16_t)(0x8000|x); }
    SaturnNativeSky(ramp,rampPalette,32,1);
    for(int turn=-1;turn<=1;turn++) {
        GpuDrawState sky={.saturnSky=1,.skyYaw=(float)(turn*4)};
        GpuClearArea(0,areaY,8,8,0,0,0);
        GpuTriangles(&sky,v,4);Flush();
        float landmark[4];glReadPixels(4,104,1,1,GL_RGBA,GL_FLOAT,landmark);
        assert(fabsf(landmark[0]-(16+turn*2)*(8.0f/255))<.002f);
        /* Replaying another sky polygon in this pass must not cover the world. */
        poly.colour=0xfc00;GpuTriangles(&native,v,4);
        GpuTriangles(&sky,v,4);Flush();
        glReadPixels(4,104,1,1,GL_RGBA,GL_FLOAT,landmark);
        assert(landmark[0]<.002f && fabsf(landmark[2]-248.0f/255)<.002f);
    }
    /* Original 0.AAA: RBG0 Xst/Yst=0, DX/DYst=1, kx/ky=1,
       C=P=(176,120), Mx=heading/2, My=120-pitch/3. Mednafen's original
       Phenitia Park state confirms HRes=1 (352 dots), VRes=1 (240 lines).
       On the port's 320-dot scene, every ten X pixels span eleven Saturn
       dots. Test full-sized, asymmetric data so periodic tiny patterns cannot
       hide reversed signs, doubled angular rates or incorrect viewport scale. */
    uint8_t *panorama=malloc(2048*512);
    float *frame=malloc(320*240*4*sizeof(float));
    assert(panorama && frame);
    for(int i=0;i<256;i++) rampPalette[i]=(uint16_t)(0x8000|(i&31)|((i>>5)<<7));
    for(int y=0;y<512;y++) for(int x=0;x<2048;x++)
        panorama[y*2048+x]=(uint8_t)(x*13+y*29+(x>>5)*7+(y>>3)*11);
    SaturnNativeSky(panorama,rampPalette,2048,512);
    areaX=0;areaW=320;areaH=240;
    for(int screen=0;screen<2;screen++) {
        areaY=screen*256;
        for(int heading=-704;heading<=704;heading+=704) for(int pitch=-360;pitch<=360;pitch+=360) {
            GpuDrawState sky={.saturnSky=1,.skyYaw=(float)heading,.skyPitch=(float)pitch};
            GpuClearArea(0,areaY,320,240,0,0,0);
            GpuTriangles(&sky,v,4);Flush();
            glReadPixels(0,areaY,320,240,GL_RGBA,GL_FLOAT,frame);
            for(int y=0;y<240;y+=7) for(int x=0;x<320;x+=10) {
                int sx=(x/10*11+heading/2)&2047,sy=(y+120-pitch/3)&511;
                uint16_t expected=rampPalette[panorama[sy*2048+sx]];
                for(int c=0;c<3;c++) {
                    if(fabsf(frame[(y*320+x)*4+c]-((expected>>(5*c))&31)*(8.0f/255))>=.002f)
                        fprintf(stderr,"Saturn sky screen=%d heading=%d pitch=%d xy=%d,%d source=%d,%d channel=%d got=%f expected=%f\n",
                            screen,heading,pitch,x,y,sx,sy,c,frame[(y*320+x)*4+c],((expected>>(5*c))&31)*(8.0f/255));
                    assert(fabsf(frame[(y*320+x)*4+c]-((expected>>(5*c))&31)*(8.0f/255))<.002f);
                }
            }
        }
    }
    portSettings.render.saturn.cameraAlignedSky = 1; RenderCommit();
    /* Independent forward projection of a fixed world landmark. The sky must
       put it at the same pixel as world geometry, including off-axis views,
       combined pitch/roll, alternate focal lengths and a complete yaw turn. */
    for(int scene=0;scene<2;scene++) for(int turn=-1;turn<=1;turn++) for(int bank=0;bank<2;bank++) {
        const float radians=6.28318530718f/4096;
        float bearing=scene ? 2010 : 100, elevation=60;
        float yaw=(scene ? 2048 : 0)+turn*180, pitch=bank ? 90 : 0, roll=bank ? .25f : 0;
        float focal=scene ? 240 : 160;
        memset(panorama,0,2048*512);
        int sx=(int)floorf(176+bearing/2),sy=(int)floorf(240+elevation/3);
        for(int y=-3;y<=3;y++) for(int x=-3;x<=3;x++)
            panorama[((sy+y)&511)*2048+((sx+x)&2047)]=1;
        uint16_t markPalette[256]={0x8000,0x801f};
        SaturnNativeSky(panorama,markPalette,2048,512);
        /* Forward world-to-view rotation: Ry(-heading), Rx(-skyPitch),
           Rz(-roll), followed by the production world's focal*x/z. */
        float x=sinf((bearing-yaw)*radians)*cosf(elevation*radians);
        float y=sinf(elevation*radians),z=cosf((bearing-yaw)*radians)*cosf(elevation*radians);
        float yp=cosf(pitch*radians)*y+sinf(pitch*radians)*z;
        z=-sinf(pitch*radians)*y+cosf(pitch*radians)*z;
        float xp=cosf(roll)*x+sinf(roll)*yp;
        yp=-sinf(roll)*x+cosf(roll)*yp;
        int px=(int)roundf(160+focal*xp/z),py=(int)roundf(120+focal*yp/z);
        assert(px>=0 && px<320 && py>=0 && py<240);
        areaY=scene*256;
        for(int revolution=-1;revolution<=1;revolution++) {
            GpuDrawState sky={.saturnSky=1,.skyYaw=yaw+revolution*4096,
                .skyPitch=pitch,.skyRoll=roll,.skyFocal=focal};
            GpuClearArea(0,areaY,320,240,0,0,0);
            GpuTriangles(&sky,v,4);Flush();
            float pixel[4];glReadPixels(px,areaY+py,1,1,GL_RGBA,GL_FLOAT,pixel);
            assert(fabsf(pixel[0]-248.0f/255)<.002f && pixel[1]<.002f && pixel[2]<.002f);
        }
    }
    free(frame);free(panorama);
    SaturnNativeSky(NULL,NULL,0,0);saturnSkyDrawn=0;
    areaX=oldX;areaY=oldY;areaW=oldW;areaH=oldH;
    portSettings.render=saved;RenderCommit();memset(pictureOwners,0,sizeof(pictureOwners));
    BindTarget(0);
    assert(glGetError()==GL_NO_ERROR);
    puts("Saturn production pixels: native quad additive RGB555 and full-resolution VDP2 plane wrapping/heading/pitch/roll: OK");
    puts("Saturn sky landmarks follow world projection through yaw/pitch/roll, focal changes and full-turn wrapping: OK");
}


static float saturnExpected[128][128][3];
static int SaturnOracleIndex(void *user,float u,float v)
{
    SaturnDraw *draw=user;
    return SaturnSampleIndex(draw,u,v);
}
static void SaturnOraclePixel(void *user,int x,int y,uint16_t rgb,int blend,float depth,uint8_t alpha)
{
    SaturnDraw *draw=user;
    (void)depth;
    assert(x>=0 && x<64*scale && y>=100*scale && y<164*scale);
    if(scale>1 && (draw->source.saturn->mode&0x100) && ((x/scale+y/scale)&1)) return;
    float a=alpha/255.0f;
    if(blend==0 || blend==-2) a*=.5f;
    for(int c=0;c<3;c++) {
        float src=blend==-2 ? 0 : ((rgb>>(5*c))&31)*(8.0f/255);
        float *out=&saturnExpected[y-100*scale][x][c];
        *out=src*a+*out*(1-a);
        /* The GL target rounds each ordered write to RGBA8. */
        *out=roundf(*out*255)/255;
    }
}
static void SaturnGpuReference(void)
{
    RenderSettings saved=portSettings.render;
    int ox=areaX,oy=areaY,ow=areaW,oh=areaH;
    ResetRenderSettings();
    portSettings.render.selection=RENDER_SATURN;RenderCommit();
    areaX=0;areaY=100;areaW=areaH=64;
    GLuint oldFramebuffer=framebuffer,testTexture,testDepth;
    int oldScale=scale;
    glGenFramebuffers(1,&framebuffer);glGenTextures(1,&testTexture);
    glBindFramebuffer(GL_FRAMEBUFFER,framebuffer);
    glActiveTexture(GL_TEXTURE0);glBindTexture(GL_TEXTURE_2D,testTexture);
    glTexImage2D(GL_TEXTURE_2D,0,GL_RGBA8,VRAM_WIDTH*2,VRAM_HEIGHT*2,0,GL_RGBA,GL_UNSIGNED_BYTE,NULL);
    glFramebufferTexture2D(GL_FRAMEBUFFER,GL_COLOR_ATTACHMENT0,GL_TEXTURE_2D,testTexture,0);
    testDepth=DepthFor(VRAM_WIDTH*2,VRAM_HEIGHT*2);
    assert(glCheckFramebufferStatus(GL_FRAMEBUFFER)==GL_FRAMEBUFFER_COMPLETE);
    uint8_t texture[64];
    for(int i=0;i<64;i++) texture[i]=(uint8_t)((i*7+i/8)&15);
    SaturnNativePoly poly={.colour=0xa56a,.corners={0,1,2,3},.width=8,.height=8,.pixels=texture,
        .gouraud={0x4210,0x7fef,0x1084,0x5294}};
    for(int i=0;i<16;i++) poly.palette[i]=(uint16_t)(0x8000|(i*2)|((31-i)<<5)|((i+4)<<10));
    const float shapes[][8]={
        {4,104,47,104,47,147,4,147}, {30,99,60,130,30,160,0,130},
        {-20,110,52,103,44,151,9,162}, {5,105,54,109,48,154,5,105},
        {4,104,40,104,40,104,4,151}, {22,122,22,122,22,122,22,122},
        {54,103,8,111,4,153,49,147}, {5,105,50,150,50,105,5,150},
        {4,112,55,112,55,112,4,112}, {20,103,20,158,20,158,20,103}
    };
    const int modes[]={0xc0,0xc4,0xc8,0xcc,0x1cc,0xc1,0xc2,0xc3,0xc7,0x08};
    /* The production road path samples packed compatibility VRAM with windows;
       models sample the immutable native index atlas. Exercise both. */
    for(int y=0;y<64;y++) for(int x=0;x<16;x++) {
        uint16_t word=0;
        for(int n=0;n<4;n++) word |= ((x*4+n+y*3)&15)<<(n*4);
        psxVram[y+256][x+128]=word;
    }
    vramDirty=1;
    unsigned checked=0;
    for(scale=1;scale<=2;scale++) {
    for(int path=0;path<3;path++)
    for(size_t shape=0;shape<sizeof(shapes)/sizeof(shapes[0]);shape++) {
        for(size_t mode=0;mode<sizeof(modes)/sizeof(modes[0]);mode++) {
            GpuVertex v[4]={0}, scaled[4];
            poly.mode=modes[mode];poly.textured=(mode>=2 && mode<=4) || mode==9;
            poly.pixels=path==1 ? NULL : texture;
            poly.clipped=path==2;poly.vertexAttributes=path==2;
            for(int k=0;k<4;k++) v[k]=(GpuVertex){.x=shapes[shape][k*2]+.5f,.y=shapes[shape][k*2+1],
                .w=100,.a=255,.u=k==1 || k==2 ? 31:0,.v=k>=2?31:0};
            if(path==2) for(int k=0;k<4;k++) {
                v[k].x+=.125f;v[k].y+=.25f;
                v[k].u=(k==1 || k==2) ? 7.0f : 0;v[k].v=k>=2 ? 7.0f : 0;
                for(int c=0;c<3;c++) v[k].saturnShade[c]=(poly.gouraud[k]>>(5*c))&31;
                v[k].a=128+k*30;
            }
            Flush();glDisable(GL_SCISSOR_TEST);glClearColor(24/255.0f,48/255.0f,72/255.0f,1);
            glClear(GL_COLOR_BUFFER_BIT);BindTarget(0);
            for(int k=0;k<4;k++) {
                scaled[k]=v[k];
                scaled[k].x*=scale;scaled[k].y*=scale;
            }
            for(int y=0;y<64*scale;y++) for(int x=0;x<64*scale;x++) for(int c=0;c<3;c++)
                saturnExpected[y][x][c]=(c+1)*24/255.0f;
            GpuDrawState state={.saturn=&poly,.pageX=128,.pageY=256,.windowMaskX=1,.windowMaskY=2,
                .windowOffsetX=1,.windowOffsetY=2};
            RenderNativeTarget target = NativeTarget();
            SaturnDraw oracle={.source=state, .target=&target};
            SaturnNativePoly scaledPoly=poly;
            /* Rasterize on the finer grid, then apply the original dot mask in the callback. */
            if(scale>1) scaledPoly.mode &= ~0x100;
            SaturnVdp1Draw(&scaledPoly,scaled,0,100*scale,64*scale,164*scale,SaturnOraclePixel,SaturnOracleIndex,&oracle);
            GpuTriangles(&state,v,4);
            assert(RenderSaturnQuadsPending() && batchCount==0 && saturnRight==0);
            Flush();
            uint8_t actual[128*128*4];glReadPixels(0,100*scale,64*scale,64*scale,GL_RGBA,GL_UNSIGNED_BYTE,actual);
            for(int y=0;y<64*scale;y++) for(int x=0;x<64*scale;x++) for(int c=0;c<3;c++) {
                int expected=(int)lroundf(saturnExpected[y][x][c]*255);
                int got=actual[(y*64*scale+x)*4+c];
                if(abs(got-expected)>2) {
                    fprintf(stderr,"Saturn GPU/reference mismatch scale %d path %d shape %zu mode %x at %d,%d ch %d: %d vs %d\n",
                        scale,path,shape,poly.mode,x,y,c,got,expected);abort();
                }
                checked++;
            }
        }
    }
    }
    scale=2;
    /* The smooth Saturn profile uses compatibility Gouraud rays. Their dim
       edges must add to the flare centre and blue trail, never erase them. */
    for(int mesh=0;mesh<2;mesh++) {
        portSettings.render.saturn.mesh=mesh;
        portSettings.render.saturn.affineTextures=0;
        portSettings.render.saturn.colourPrecision=0;
        RenderCommit();
        GpuVertex effect[3]={{.x=0,.y=100,.w=100,.a=255},
                             {.x=16,.y=100,.w=100,.a=255},
                             {.x=0,.y=116,.w=100,.a=255}};
        GpuDrawState shadow={.semi=0},light={.semi=1};
        GpuClearArea(0,100,64,64,32,16,8);
        GpuTriangles(&shadow,effect,3);
        for(int k=0;k<3;k++) effect[k].b=255;
        GpuTriangles(&light,effect,3);Flush();
        uint8_t pixels[4*4*4];
        glReadPixels(0,200,4,4,GL_RGBA,GL_UNSIGNED_BYTE,pixels);
        for(int y=0;y<4;y++) for(int x=0;x<4;x++) {
            int dot=((x/2+y/2)&1)==0;
            const uint8_t *pixel=&pixels[(y*4+x)*4];
            int r=mesh ? (dot ? 0:32) : 16;
            int g=mesh ? (dot ? 0:16) : 8;
            int b=mesh ? (dot ? 255:8) : 132;
            assert(abs(pixel[0]-r)<=1 && abs(pixel[1]-g)<=1 && abs(pixel[2]-b)<=1);
        }
        for(int k=0;k<3;k++) effect[k].r=effect[k].g=255;
        GpuTriangles(&light,effect,3);
        for(int k=0;k<3;k++) { effect[k].r=effect[k].g=0;effect[k].b=16; }
        GpuTriangles(&light,effect,3);Flush();
        glReadPixels(0,200,4,4,GL_RGBA,GL_UNSIGNED_BYTE,pixels);
        for(int y=0;y<4;y++) for(int x=0;x<4;x++) {
            int dot=((x/2+y/2)&1)==0;
            const uint8_t *pixel=&pixels[(y*4+x)*4];
            int r=mesh ? (dot ? 255:32) : 144;
            int g=mesh ? (dot ? 255:16) : 136;
            int b=mesh ? (dot ? 255:8) : 255;
            assert(abs(pixel[0]-r)<=1 && abs(pixel[1]-g)<=1 && abs(pixel[2]-b)<=1);
        }
    }
    portSettings.render.saturn.affineTextures=1;
    portSettings.render.saturn.colourPrecision=1;
    RenderCommit();
    puts("Saturn compatibility effects retain blue over shadows and bright overlapping flare centres: OK");
    /* Both transparency modes on the compatibility trail and native textured flare.
       Half of the flare's source image uses the transparent texture index. */
    GpuVertex trail[3]={{.x=0,.y=100,.w=100,.g=255,.a=255},
                       {.x=16,.y=100,.w=100,.g=255,.a=255},
                       {.x=0,.y=116,.w=100,.g=255,.a=255}};
    GpuDrawState additive={.semi=1};
    uint8_t flareTexels[]={1,0};
    SaturnNativePoly flarePoly={.mode=0x188,.corners={0,1,2,3},.width=2,.height=1,
        .textured=1,.pixels=flareTexels,.palette={[1]=0x801f}};
    GpuDrawState flareDraw={.saturn=&flarePoly};
    GpuVertex flareQuad[4]={{.x=0,.y=100,.w=100,.a=255},{.x=7,.y=100,.w=100,.a=255},
                          {.x=7,.y=107,.w=100,.a=255},{.x=0,.y=107,.w=100,.a=255}};
    SDL_Environment *env=SDL_GetEnvironment();
    for(int reference=0;reference<2;reference++) {
        assert(SDL_SetEnvironmentVariable(env,"WIPEOUT_SATURN_REFERENCE",reference ? "1" : "0",true));
        for(int mesh=0;mesh<2;mesh++) {
            portSettings.render.saturn.mesh=mesh;RenderCommit();
            GpuClearArea(0,100,64,64,0,0,255);
            GpuTriangles(&additive,trail,3);
            GpuTriangles(&flareDraw,flareQuad,4);Flush();
            uint8_t pixels[12*4*4];glReadPixels(0,200,12,4,GL_RGBA,GL_UNSIGNED_BYTE,pixels);
            for(int y=0;y<4;y++) for(int x=0;x<12;x++) {
                int dot=((x/2+y/2)&1)==0;
                int flareTexel=x<8;
                const uint8_t *pixel=&pixels[(y*12+x)*4];
                int red=mesh ? (dot && flareTexel ? 248 : 0) : (flareTexel ? 124 : 0);
                int green=mesh ? (dot && !flareTexel ? 248 : 0) : (flareTexel ? 62 : 124);
                int blue=mesh ? (dot && flareTexel ? 0 : 255) : (flareTexel ? 128 : 255);
                assert(abs(pixel[0]-red)<=1 && abs(pixel[1]-green)<=1 && abs(pixel[2]-blue)<=1);
            }
        }
        /* Alpha mesh can test depth, but must leave later draws free to write. */
        portSettings.render.saturn.mesh=0;
        portSettings.render.saturn.depth=1;RenderCommit();
        GpuClearArea(0,100,64,64,0,0,0);
        GpuClearDepth(0,100,64,64,0);
        GpuTriangles(&flareDraw,flareQuad,4);
        SaturnNativePoly solid={.mode=0xc0,.colour=0xfc00,.corners={0,1,2,3}};
        GpuDrawState solidDraw={.saturn=&solid};
        GpuVertex behind[4];memcpy(behind,flareQuad,sizeof(behind));
        for(int k=0;k<4;k++) behind[k].w=200;
        GpuTriangles(&solidDraw,behind,4);Flush();
        uint8_t pixel[4];glReadPixels(1,201,1,1,GL_RGBA,GL_UNSIGNED_BYTE,pixel);
        assert(pixel[0]==0 && pixel[1]==0 && pixel[2]==248);
        portSettings.render.saturn.depth=0;RenderCommit();
    }
    assert(SDL_UnsetEnvironmentVariable(env,"WIPEOUT_SATURN_REFERENCE"));
    /* Adjacent framebuffer samples inside one console pixel can see distinct
       panorama texels. The literal RBG0 reference still samples one dot. */
    uint8_t ramp[32];uint16_t palette[256]={0};
    for(int x=0;x<32;x++) { ramp[x]=(uint8_t)x;palette[x]=(uint16_t)(0x8000|x); }
    SaturnNativeSky(ramp,palette,32,1);
    portSettings.render.saturn.cameraAlignedSky=1;RenderCommit();
    for(int reference=0;reference<2;reference++) {
        if(reference) setenv("WIPEOUT_SATURN_VDP2_REFERENCE","1",1);
        GpuDrawState sky={.saturnSky=1,.skyFocal=160};
        GpuClearArea(0,100,64,64,0,0,0);
        sky.saturnSky = 1;
        GpuTriangles(&sky, NULL, 0);
        float samples[8];glReadPixels(64,264,2,1,GL_RGBA,GL_FLOAT,samples);
        assert(fabsf(samples[0]-16.0f*(8.0f/255))<.002f);
        assert(fabsf(samples[4]-(reference ? 16.0f : 17.0f)*(8.0f/255))<.002f);
    }
    unsetenv("WIPEOUT_SATURN_VDP2_REFERENCE");
    SaturnNativeSky(NULL,NULL,0,0);saturnSkyDrawn=0;
    /* Several command-buffer turnovers must retain ordering. */
    portSettings.render.saturn.depth=0;RenderCommit();
    SaturnNativePoly flat={.mode=0xc0,.corners={0,1,2,3}};
    GpuVertex square[4]={{.x=4,.y=104,.w=100,.a=255},{.x=20,.y=104,.w=100,.a=255},
                         {.x=20,.y=120,.w=100,.a=255},{.x=4,.y=120,.w=100,.a=255}};
    GpuDrawState native={.saturn=&flat};
    for(int i=0;i<130;i++) { flat.colour=i==129 ? 0xfc00 : 0x801f;GpuTriangles(&native,square,4); }
    Flush();float pixel[4];glReadPixels(10*scale,110*scale,1,1,GL_RGBA,GL_FLOAT,pixel);
    assert(fabsf(pixel[2]-248.0f/255)<.002f && pixel[0]<.01f);
    /* With depth disabled draw order wins; with it enabled the nearer quad wins,
       in either order. A preceding half blend must not disable later depth writes. */
    for(int enabled=0;enabled<2;enabled++) for(int reverse=0;reverse<2;reverse++) {
        portSettings.render.saturn.depth=enabled;RenderCommit();
        BindTarget(0);glDisable(GL_SCISSOR_TEST);glDepthMask(GL_TRUE);
        glClear(GL_COLOR_BUFFER_BIT|GL_DEPTH_BUFFER_BIT);
        flat.mode=0xc3;flat.colour=0xffff;
        for(int k=0;k<4;k++) square[k].w=200;
        GpuTriangles(&native,square,4);
        flat.mode=0xc0;
        for(int i=0;i<2;i++) {
            int near=(i==0)^reverse;
            flat.colour=near ? 0x801f : 0x83e0;
            for(int k=0;k<4;k++) square[k].w=near ? 80 : 160;
            GpuTriangles(&native,square,4);
        }
        Flush();glReadPixels(10*scale,110*scale,1,1,GL_RGBA,GL_FLOAT,pixel);
        assert(fabsf(pixel[(enabled || reverse) ? 0 : 1]-248.0f/255)<.002f);
        assert(pixel[(enabled || reverse) ? 1 : 0]<.01f);
    }
    /* At the midpoint a receding native surface is at z=182, so it hides
       compatibility geometry at z=300 even though linear view z gives 550. */
    GpuDrawState compatibility={.semi=-1};
    GpuVertex overlap[3]={{.x=4,.y=104,.w=300,.g=248,.a=255},
                          {.x=30,.y=104,.w=300,.g=248,.a=255},
                          {.x=4,.y=130,.w=300,.g=248,.a=255}};
    square[0].w=square[3].w=100;
    square[1].w=square[2].w=1000;
    flat.colour=0x801f;
    for(int reference=0;reference<2;reference++) {
        assert(SDL_SetEnvironmentVariable(env,"WIPEOUT_SATURN_REFERENCE",reference ? "1" : "0",true));
        for(int clipped=0;clipped<2;clipped++) {
            flat.clipped=clipped;
            flat.corners[3]=clipped ? 0 : 3;
            GpuClearArea(0,100,64,64,0,0,0);
            GpuTriangles(&native,square,clipped ? 3 : 4);Flush();
            glReadPixels(12*scale,108*scale,1,1,GL_DEPTH_COMPONENT,GL_FLOAT,pixel);
            assert(fabsf(pixel[0]-(1.0f-.5f/100-.5f/1000))<.00001f);
            GpuTriangles(&compatibility,overlap,3);Flush();
            glReadPixels(12*scale,108*scale,1,1,GL_RGBA,GL_FLOAT,pixel);
            assert(fabsf(pixel[0]-248.0f/255)<.002f && pixel[1]<.01f);
        }
    }
    assert(SDL_UnsetEnvironmentVariable(env,"WIPEOUT_SATURN_REFERENCE"));
    puts("Saturn sloped quads and clipped triangles share reciprocal depth with compatibility geometry: OK");

    SaturnNativePoly road={.textured=1,.trackTile=1,.mode=0xc0,.corners={0,1,3,2}};
    GpuDrawState roadDraw={.saturn=&road,.pageX=128,.pageY=256};
    GpuVertex tile[4]={{.x=0,.y=104,.w=100,.a=255},
                       {.x=32,.y=104,.w=1000,.a=255,.u=31},
                       {.x=0,.y=136,.w=100,.a=255},
                       {.x=32,.y=136,.w=1000,.a=255,.u=31}};
    for (int i=0;i<16;i++) road.palette[i]=(uint16_t)(0x8000 | i);
    for (int i=0;i<8;i++) psxVram[256][128+i]=(uint16_t)(0x3210 + (i%4)*0x4444);
    vramDirty=1;
    portSettings.render.saturn.depth=0;RenderCommit();
    for (int reference=0;reference<2;reference++) {
        assert(SDL_SetEnvironmentVariable(env,"WIPEOUT_SATURN_REFERENCE",reference ? "1" : "0",true));
        for (int clipped=0;clipped<2;clipped++) {
            road.clipped=clipped;
            road.corners[2]=clipped ? 2 : 3;
            road.corners[3]=clipped ? 0 : 2;
            GpuClearArea(0,100,64,64,0,0,0);
            GpuTriangles(&roadDraw,tile,clipped ? 3 : 4);Flush();
            glReadPixels(16*scale,108*scale,1,1,GL_RGBA,GL_FLOAT,pixel);
            /* Perspective UV samples index 2. Affine UV would sample index 15. */
            assert(fabsf(pixel[0]-2.0f*(8.0f/255))<.003f);
        }
    }
    assert(SDL_UnsetEnvironmentVariable(env,"WIPEOUT_SATURN_REFERENCE"));
    road.clipped=0;road.corners[2]=3;road.corners[3]=2;
    tile[2].x=-100000;tile[3].x=100000;
    for (int i=0;i<4;i++) tile[i].u=2;
    GpuClearArea(0,100,64,64,0,0,0);
    GpuTriangles(&roadDraw,tile,4);Flush();
    glReadPixels(16*scale,124*scale,1,1,GL_RGBA,GL_FLOAT,pixel);
    assert(fabsf(pixel[0]-2.0f*(8.0f/255))<.003f);
    puts("Saturn road tiles retain perspective UVs and clip oversized projections to the viewport: OK");
    glDeleteFramebuffers(1,&framebuffer);glDeleteTextures(1,&testTexture);glDeleteRenderbuffers(1,&testDepth);
    framebuffer=oldFramebuffer;scale=oldScale;
    areaX=ox;areaY=oy;areaW=ow;areaH=oh;
    portSettings.render=saved;RenderCommit();memset(pictureOwners,0,sizeof(pictureOwners));BindTarget(0);
    assert(glGetError()==GL_NO_ERROR);
    printf("Saturn GPU forward quads vs software reference: %u channel comparisons, coverage/collapsed/reversed/mesh/end-codes/shading/blending/VRAM/clipping/fades/scaling/depth/batch ordering: OK\n",checked);
}

static void N64SharedMaterials(void)
{
    RenderSettings saved = portSettings.render;
    int savedTrack = n64Track;
    const float full = 248.0f / 255.0f;
    RenderMaterial ui = {.textured = 1, .dither = 1, .ui = 1};
    GpuDrawState effect = {.semi = 1, .dither = 1};
    words[0] = 31; words[1] = 31 << 5;
    words[1024] = 31 << 10; words[1025] = 0x7fff;
    Upload();
    portSettings.render.selection = RENDER_N64;
    portSettings.render.n64 = (N64RenderSettings){0};
    n64Track = 1;
    for (int other = 0; other < 2; other++) {
        portSettings.render.ps1 = (Ps1RenderSettings){other, other, other, other, other};
        portSettings.render.pc = (PcRenderSettings){other, other, other};
        glDisable(GL_BLEND);
        glUniform1f(scene->uniformDepthPull, 1);
        portSettings.render.n64.threePoint = 0; RenderCommit();
        RenderStyle style = RenderPrepare(RenderCurrent(), &ui);
        testShade = style.shade;
        Render(RENDER_N64, style.filter, 2, .5f, .5f, 100, -1, 0, full * 100 / 128, 0, 0, 1);
        portSettings.render.n64.threePoint = 1; RenderCommit();
        style = RenderPrepare(RenderCurrent(), &ui);
        testShade = style.shade;
        assert(style.filter == 0);
        Render(RENDER_N64, style.filter, 2, .5f, .5f, 128, -1, 0, full, 0, 0, 1);
        RenderMaterial world = ui;
        world.ui = 0;
        style = RenderPrepare(RenderCurrent(), &world);
        assert(style.filter == 1);
        testShade = style.shade;
        Render(RENDER_N64, style.filter, 2, .5f, .5f, 128, -1, 0, 0, full / 2, full / 2, 1);
        portSettings.render.n64.threePoint = 0; RenderCommit();
        for (int spatial = 0; spatial < 2; spatial++) {
            RenderMaterial material = {.dither = 1};
            GpuVertex v = {.y = 100, .w = spatial ? 100 : 0, .r = 100, .g = 100, .b = 100, .a = 255};
            float pixels[4 * 4 * 4];
            style = RenderPrepare(RenderCurrent(), &material);
            glClearColor(0, 0, 0, 0); glClear(GL_COLOR_BUFFER_BIT);
            Begin(&effect, 3);
            Add(&v, style.shade);
            v.x += 8; Add(&v, style.shade);
            v.x -= 8; v.y += 8; Add(&v, style.shade);
            Flush();
            glReadPixels(0, 100, 4, 4, GL_RGBA, GL_FLOAT, pixels);
            for (int i = 0; i < 16; i++) for (int c = 0; c < 3; c++)
                assert(fabsf(pixels[i * 4 + c] - 100.0f / 255) < .002f);
        }
    }
    portSettings.render = saved; n64Track = savedTrack; RenderCommit();
    memset(pictureOwners, 0, sizeof(pictureOwners)); testShade = 0;
    assert(glGetError() == GL_NO_ERROR);
    puts("N64 shared XL UI filtering and undithered 2D/3D additive effects ignore PS1/PC profiles: OK");
}

/* Driver dithering must not bypass any profile, or interfere with explicit PS1 shader dithering. */
static void WorldDithering(void)
{
    static const int offsets[16] = {-4,0,-3,1, 2,-2,3,-1, -3,1,-4,0, 3,-1,2,-2};
    RenderSettings saved = portSettings.render;
    RenderMaterial material = {.dither = 1};
    GpuDrawState effect = {.semi = 1, .dither = 1};
    for (int id = RENDER_PS1; id <= RENDER_N64; id++) for (int dither = 0; dither < 2; dither++) {
        ResetRenderSettings();
        portSettings.render.selection = (RendererId)id;
        portSettings.render.ps1.dither = dither;
        RenderCommit();
        RenderStyle style = RenderPrepare(RenderCurrent(), &material);
        GpuVertex v = {.y = 100, .w = 100, .r = 104, .g = 104, .b = 104, .a = 255};
        float pixels[4 * 4 * 4];
        glClearColor(0, 0, 0, 0); glClear(GL_COLOR_BUFFER_BIT);
        glEnable(GL_DITHER); /* simulate the default context state or another GL user's state */
        Begin(&effect, 3);
        Add(&v, style.shade);
        v.x += 8; Add(&v, style.shade);
        v.x -= 8; v.y += 8; Add(&v, style.shade);
        Flush();
        assert(!glIsEnabled(GL_DITHER));
        glReadPixels(0, 100, 4, 4, GL_RGBA, GL_FLOAT, pixels);
        for (int i = 0; i < 16; i++) {
            float expected = (id == RENDER_PS1 && dither && offsets[i] < 0 ? 96.0f : 104.0f) / 255;
            for (int c = 0; c < 3; c++) assert(fabsf(pixels[i * 4 + c] - expected) < .002f);
        }
    }
    /* Empty batches also reset GL state before direct fills and framebuffer writes. */
    glEnable(GL_DITHER); Flush(); assert(!glIsEnabled(GL_DITHER));
    portSettings.render = saved; RenderCommit(); memset(pictureOwners, 0, sizeof(pictureOwners));
    assert(glGetError() == GL_NO_ERROR);
    puts("World draws disable driver dithering for all profiles; only the selected PS1 shader dither toggle adds its 4x4 pattern: OK");
}

/* ShowMenu/InitMenuBox use flat blue/purple option boxes; InitMenuPolys uses
   Gouraud green shades. UI (w == 0) must keep RGB5 even when ineligible for
   dithering, and eligible shades use VRAM coordinates in either screen buffer. */
static void MainMenuColour(void)
{
    static const int offsets[16] = {-4,0,-3,1, 2,-2,3,-1, -3,1,-4,0, 3,-1,2,-2};
    static const struct { int rgb[3], dither; } elements[] = {
        {{0, 0, 46}, 0}, {{85, 0, 40}, 0}, /* POLY_F4 options */
        {{24, 66, 11}, 1}, {{85, 0, 40}, 1} /* POLY_G4 shades/selection */
    };
    RenderSettings saved = portSettings.render;
    for (int precision = 0; precision < 2; precision++)
    for (int dither = 0; dither < 2; dither++)
    for (int buffer = 0; buffer < 2; buffer++)
    for (size_t e = 0; e < sizeof(elements) / sizeof(elements[0]); e++) {
        ResetRenderSettings();
        portSettings.render.selection = RENDER_PS1;
        portSettings.render.ps1.colourPrecision = precision;
        portSettings.render.ps1.dither = dither;
        RenderCommit();
        RenderMaterial material = {.ui = 1, .dither = elements[e].dither};
        RenderStyle style = RenderPrepare(RenderCurrent(), &material);
        GpuDrawState draw = {.semi = -1, .dither = elements[e].dither};
        int x = 13, y = 101 + buffer * 256;
        GpuVertex v = {.x = x, .y = y, .r = elements[e].rgb[0],
            .g = elements[e].rgb[1], .b = elements[e].rgb[2], .a = 255};
        float pixels[4 * 4 * 4];
        glClearColor(1, 1, 1, 1); glClear(GL_COLOR_BUFFER_BIT);
        Begin(&draw, 3);
        Add(&v, style.shade);
        v.x += 8; Add(&v, style.shade);
        v.x -= 8; v.y += 8; Add(&v, style.shade);
        Flush();
        glReadPixels(x, y, 4, 4, GL_RGBA, GL_FLOAT, pixels);
        for (int py = 0; py < 4; py++) for (int px = 0; px < 4; px++) {
            for (int c = 0; c < 3; c++) {
                int value = elements[e].rgb[c];
                if (precision) {
                    if (dither && elements[e].dither)
                        value += offsets[((y + py) & 3) * 4 + ((x + px) & 3)];
                    value = SDL_clamp(value, 0, 255) / 8 * 8;
                }
                assert(fabsf(pixels[(py * 4 + px) * 4 + c] - value / 255.0f) < .002f);
            }
        }
    }
    portSettings.render = saved; RenderCommit(); memset(pictureOwners, 0, sizeof(pictureOwners));
    assert(glGetError() == GL_NO_ERROR);
    puts("PS1 main-menu UI: RGB5 flat boxes, eligible 4x4 dithering, both screen buffers and independent toggles: OK");
}

/* Unequal depths expose accidental perspective interpolation of lighting and PS1 UVs. */
static void Interpolation(void)
{
    const float full = 248.0f / 255.0f;
    for (int test = 0; test < 4; test++) {
        float result[4];
        TestProgram(test == 0 ? RENDER_N64 : test == 3 ? RENDER_PC : RENDER_PS1);
        const float expected[4][3] = {{.5f,.25f,.25f}, {full,0,0}, {0,full,0},
                                     {full * 7 / 11,full * 4 / 11,0}};
        memset(triangle, 0, sizeof(triangle));
        triangle[1].x = 2; triangle[2].y = 2;
        for (int i = 0; i < 3; i++) {
            BatchVertex *b = &triangle[i];
            b->w = (float)(1 << i); b->a = 1;
            b->r = i == 0; b->g = i == 1; b->b = i == 2;
            b->u = i == 1 ? 2.5f : .5f; b->v = .5f;
            b->mode[0] = test != 0;
            b->mode[1] = test == 0 ? SHADE_N64 | SHADE_AFFINE_COLOUR : SHADE_RAW;
            if (test == 2) b->mode[1] |= SHADE_AFFINE_UV | SHADE_PS1_COLOUR;
            b->mode[2] = 2;
            b->mode[3] = test == 3 ? 2 << 1 : 0;
        }
        glBufferData(GL_ARRAY_BUFFER, sizeof(triangle), triangle, GL_STREAM_DRAW);
        glUniform1i(glGetUniformLocation(scene->program, "pass"), 0);
        glClear(GL_COLOR_BUFFER_BIT); glDrawArrays(GL_TRIANGLES, 0, 3);
        glReadPixels(0, 0, 1, 1, GL_RGBA, GL_FLOAT, result);
        for (int k = 0; k < 3; k++) assert(fabsf(result[k] - expected[test][k]) < .002f);
    }
    /* A subpixel triangle misses this pixel on PC, but its PS1 integer corners cover it. */
    for (int ps1 = 0; ps1 < 4; ps1++) {
        float result[4];
        TestProgram(RENDER_PS1);
        memset(triangle, 0, sizeof(triangle));
        for (int i = 0; i < 3; i++) {
            BatchVertex *b = &triangle[i];
            b->x = .75f + (i == 1 ? 2 : 0);
            b->y = .75f + (i == 2 ? 2 : 0);
            b->w = b->r = b->a = 1;
            b->mode[1] = (ps1 & 1 ? SHADE_PS1_COLOUR : 0) | (ps1 & 2 ? SHADE_SNAP : 0);
        }
        glBufferData(GL_ARRAY_BUFFER, sizeof(triangle), triangle, GL_STREAM_DRAW);
        glClear(GL_COLOR_BUFFER_BIT); glDrawArrays(GL_TRIANGLES, 0, 3);
        glReadPixels(0, 0, 1, 1, GL_RGBA, GL_FLOAT, result);
        assert(fabsf(result[0] - (ps1 & 2 ? (ps1 & 1 ? full : 1.0f) : 0)) < .002f);
    }
    assert(glGetError() == GL_NO_ERROR);
}

/* Gameplay road tint is 128-neutral even though its texture stays N64 encoded. */
static void N64GameplayLight(void)
{
    words[0] = words[1] = words[1024] = words[1025] = 15;
    Upload();
    for (int id = RENDER_PS1; id <= RENDER_N64; id++) {
        RenderFrame frame = RenderSnapshot((RendererId)id, &(RenderSettings)RENDER_DEFAULTS);
        RenderMaterial material = {.n64 = 1, .textured = 1, .bounds = 1};
        RenderStyle style = RenderPrepare(&frame, &material);
        testShade = (uint8_t)style.shade;
        for (int filtering = 0; filtering < 2; filtering++) {
            int filter = filtering ? style.filter : 0;
            /* Authored RGB8 stays half intensity at 128; depleted gameplay pads are neutral. */
            testPs1Light = 0;
            float authored = (123.0f * 128 / 255) / 255;
            if (id == RENDER_PS1) authored = 56.0f / 255;
            Render(frame.id, filter, 2, .5f, .5f, 128, -1, 0, authored, 0, 0, 1);
            testPs1Light = 1;
            Render(frame.id, filter, 2, .5f, .5f, 128, -1, 0, (id == RENDER_PS1 ? 120.0f : 123.0f) / 255, 0, 0, 1);
            /* Preserve overbright animation rather than saturating an RGB8 conversion. */
            Render(frame.id, filter, 2, .5f, .5f, 255, -1, 0, id == RENDER_PS1 ? 240.0f / 255 : 123.0f / 128, 0, 0, 1);
            Render(frame.id, filter, 2, .5f, .5f, 35, -1, 0, id == RENDER_PS1 ? 32.0f / 255 : 123.0f * 35 / (128 * 255), 0, 0, 1);
            testShade |= SHADE_RAW;
            Render(frame.id, filter, 2, .5f, .5f, 35, -1, 0, (id == RENDER_PS1 ? 120.0f : 123.0f) / 255, 0, 0, 1);
            testShade = (uint8_t)style.shade;
        }
    }
    /* Verify transport packs light separately from all three source addressing bits. */
    GpuDrawState saved = primitiveState;
    assert(batchCount == 0);
    primitiveState = (GpuDrawState){.textured = 1, .n64 = 1, .depth = 2, .ps1Light = 1, .textureFlags = 0x70};
    GpuVertex vertex = {.r = 128, .g = 35, .b = 255, .a = 255};
    Add(&vertex, SHADE_N64);
    assert(batch[0].mode[2] == (2 | (0x70 >> 2) | 32));
    assert(fabsf(batch[0].r - 128.0f / 255) < .0001f && batch[0].b == 1.0f);
    batchCount = 0;
    primitiveState = saved;
    testPs1Light = testShade = 0;
    puts("All renderers retain neutral, coloured and overbright N64 gameplay road tint with nearest/filtered sampling: OK");
}

/* Saturate modulation before fade and semi-transparency, regardless of sampler. */
static void N64SaturatedLight(void)
{
    words[0] = words[1] = words[1024] = words[1025] = 31;
    Upload();
    testPs1Light = 1;
    for (int id = RENDER_PS1; id <= RENDER_N64; id++) {
        RenderFrame frame = RenderSnapshot((RendererId)id, &(RenderSettings)RENDER_DEFAULTS);
        RenderMaterial material = {.n64 = 1, .textured = 1, .bounds = 1};
        RenderStyle style = RenderPrepare(&frame, &material);
        testShade = (uint8_t)style.shade;
        float full = id == RENDER_PS1 ? 248.0f / 255 : 1.0f;
        for (int filtering = 0; filtering < 2; filtering++) {
            int filter = filtering ? style.filter : 0;
            for (int fade = 0; fade < 3; fade++) {
                testFade = fade * .5f;
                Render(frame.id, filter, 2, .5f, .5f, 255, -1, 0, full * testFade, 0, 0, testFade);
                for (int semi = 0; semi < 4; semi++) {
                    float weight = semi == 0 ? .5f : semi == 3 ? .25f : 1.0f;
                    Render(frame.id, filter, 2, .5f, .5f, 255, semi, 0, full * weight * testFade,
                           0, 0, semi == 0 ? .5f * testFade : 0);
                }
            }
        }
    }
    testFade = 1.0f;
    testPs1Light = testShade = 0;
    puts("Saturated N64 road lighting clamps before fade and every blend mode in all profiles/samplers: OK");
}

/* N64 source RGB reaches 255 and bit 15 is ignored, including transparent 0x8000.
   Sampling and shading must remain independent of the PC switch. */
static void N64Materials(void)
{
    n64Track = 1;
    testShade = SHADE_N64 | SHADE_AFFINE_COLOUR;
    words[0] = 31; words[1] = (31 << 5) | 0x8000;
    words[1024] = 31 << 10; words[1025] = 0x7fff;
    Upload();
    for (int pc = 0; pc < 2; pc++) {
        GpuDrawState material = {.n64 = 1, .textured = 1, .semi = -1, .depth = 2};
        GpuVertex v = {.w = 7, .r = 137, .g = 137, .b = 137, .a = 255};
        portSettings.render.selection = RENDER_AUTO;
        portSettings.render.ps1Selection = pc ? RENDER_PC : RENDER_PS1; RenderCommit();
        RenderMaterial description = {.n64 = 1, .textured = 1, .bounds = 1};
        RenderStyle style = RenderPrepare(RenderCurrent(), &description);
        batchCount = 0; Begin(&material, 3); Add(&v, style.shade);
        assert(batch[0].mode[1] == (SHADE_N64 | SHADE_AFFINE_COLOUR));
        assert(batch[0].r == 137 / 255.0f && batch[0].w == 7);
        batchCount = 0;
        Render(RENDER_N64, 1, 2, 0, 0, 255, -1, 0, 1, 0, 0, 1);
        Render(RENDER_N64, 1, 2, 1, 0, 255, -1, 0, 0, 1, 0, 1);
        Render(RENDER_N64, 1, 2, 1, 0, 255, 0, 0, 0, .5f, 0, .5f); /* primitive alpha */
        Render(RENDER_N64, 1, 2, 0, 0, 137, -1, 0, 137/255.0f, 0, 0, 1);
        Render(RENDER_N64, 1, 2, .5f, .5f, 255, -1, 0, 0, .5f, .5f, 1);
        testWindow[0] = testWindow[2] = 1;
        Render(RENDER_N64, 0, 2, 0, 0, 255, -1, 0, 1, 0, 0, 1); /* absent bounds: still N64, no PS1 window */
        memset(testWindow, 0, sizeof(testWindow));
        testAddress = 0x40;
        Render(RENDER_N64, 1, 2, 1.5f, 0, 255, -1, 0, .5f, .5f, 0, 1);
        testAddress = 0x50;
        Render(RENDER_N64, 1, 2, 1.5f, 0, 255, -1, 0, 0, 1, 0, 1);
        Render(RENDER_N64, 1, 2, -.5f, 0, 255, -1, 0, 1, 0, 0, 1);
        testAddress = 0x60;
        Render(RENDER_N64, 1, 2, 0, 1.5f, 255, -1, 0, 0, 0, 1, 1);
        testAddress = 0;
    }
    words[0] = 0x8000; Upload();
    Render(RENDER_N64, 1, 2, 0, 0, 255, -1, 0, 0, 0, 0, 0);
    Render(RENDER_N64, 0, 2, 0, 0, 255, -1, 0, 0, 0, 0, 0);
    testShade = 0;
    n64Track = 0; RenderCommit();
    puts("N64 RGB8 tint, RGB5551 palette semantics, wrap/mirror and PC-independent flags: OK");
}

static GLuint TestN64Picture(GLuint source, int x, int y, int width, int height,
                             const N64RenderSettings *settings, int pixelScale)
{
    N64ViPasses passes = N64ViStages(settings);
    return N64Picture(source, x, y, width, height, settings, pixelScale, &passes);
}

static void ViFiltering(void)
{
    GLuint input, output, destination, raw;
    uint8_t source[16 * 16 * 4], pixels[sizeof(source)], previous[sizeof(source)];
    glActiveTexture(GL_TEXTURE0);
    glGenTextures(1, &input); glBindTexture(GL_TEXTURE_2D, input);
    memset(source, 100, sizeof(source));
    for (int i = 3; i < sizeof(source); i += 4) source[i] = 255;
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, 16, 16, 0, GL_RGBA, GL_UNSIGNED_BYTE, source);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glGenFramebuffers(1, &raw); glBindFramebuffer(GL_FRAMEBUFFER, raw);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, input, 0);
    scale = 1;
    GLuint shown = TestN64Picture(input, 0, 0, 16, 16, &portSettings.render.n64, scale);
    glBindFramebuffer(GL_READ_FRAMEBUFFER, shown);
    glReadPixels(0, 0, 16, 16, GL_RGBA, GL_UNSIGNED_BYTE, pixels);
    /* A 100-grey input dithers to alternating RGB5 12/13, then reconstructs to 100. */
    for (int y = 1; y < 15; y++) for (int x = 1; x < 15; x++)
        assert(pixels[(y * 16 + x) * 4] == 100);
    /* Read the actual quantization stage: all sixteen thresholds and scale invariance. */
    for (int testScale = 1; testScale <= 2; testScale++) {
        static const int thresholds[16] = {0,6,1,7,4,2,5,3,3,5,2,4,7,1,6,0};
        glUseProgram(n64ViProgram); glBindVertexArray(n64ViArray);
        glBindTexture(GL_TEXTURE_2D, input);
        glBindFramebuffer(GL_DRAW_FRAMEBUFFER, shown);
        glUniform4i(glGetUniformLocation(n64ViProgram, "region"), 0, 0, 16, 16);
        glUniform1i(glGetUniformLocation(n64ViProgram, "pixelScale"), testScale);
        glUniform1i(glGetUniformLocation(n64ViProgram, "stage"), 0);
        glDrawArrays(GL_TRIANGLES, 0, 3);
        glReadPixels(0, 0, 16, 16, GL_RGBA, GL_UNSIGNED_BYTE, pixels);
        for (int y = 0; y < 16; y++) for (int x = 0; x < 16; x++) {
            int threshold = thresholds[((y / testScale) & 3) * 4 + ((x / testScale) & 3)];
            assert(pixels[(y * 16 + x) * 4] == (threshold < 4 ? 107 : 99));
        }
    }
    /* Precision, dithering and reconstruction have independently observable output. */
    N64RenderSettings options = portSettings.render.n64;
    options.dedither = options.divot = options.dither = 0;
    shown = TestN64Picture(input, 0, 0, 16, 16, &options, scale);
    glBindFramebuffer(GL_READ_FRAMEBUFFER, shown);
    glReadPixels(0, 0, 16, 16, GL_RGBA, GL_UNSIGNED_BYTE, pixels);
    assert(pixels[(8 * 16 + 8) * 4] == 99); /* truncated RGB5 12, expanded */
    options.dither = 1;
    shown = TestN64Picture(input, 0, 0, 16, 16, &options, scale);
    glBindFramebuffer(GL_READ_FRAMEBUFFER, shown);
    glReadPixels(0, 0, 16, 16, GL_RGBA, GL_UNSIGNED_BYTE, pixels);
    assert(pixels[(8 * 16 + 8) * 4] == 107 && pixels[(8 * 16 + 9) * 4] == 99);
    options.colourPrecision = 0; options.dedither = 1;
    shown = TestN64Picture(input, 0, 0, 16, 16, &options, scale);
    assert(!shown); /* No applicable VI effects: use the original framebuffer. */
    glBindFramebuffer(GL_READ_FRAMEBUFFER, raw);
    glReadPixels(0, 0, 16, 16, GL_RGBA, GL_UNSIGNED_BYTE, pixels);
    assert(pixels[(8 * 16 + 8) * 4] == 100); /* precision off also bypasses inapplicable dedither */
    /* Approximate divot correction removes isolated high-contrast holes. */
    memset(source, 128, sizeof(source));
    memset(source + (8 * 16 + 8) * 4, 0, 3);
    glBindTexture(GL_TEXTURE_2D, input);
    glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, 16, 16, GL_RGBA, GL_UNSIGNED_BYTE, source);
    shown = TestN64Picture(input, 0, 0, 16, 16, &portSettings.render.n64, scale);
    glBindFramebuffer(GL_READ_FRAMEBUFFER, shown);
    glReadPixels(0, 0, 16, 16, GL_RGBA, GL_UNSIGNED_BYTE, pixels);
    assert(pixels[(8 * 16 + 8) * 4] == 127);
    options = portSettings.render.n64; options.divot = 0;
    shown = TestN64Picture(input, 0, 0, 16, 16, &options, scale);
    glBindFramebuffer(GL_READ_FRAMEBUFFER, shown);
    glReadPixels(0, 0, 16, 16, GL_RGBA, GL_UNSIGNED_BYTE, pixels);
    assert(pixels[(8 * 16 + 8) * 4] == 8); /* hole retained without divot correction */
    /* The display wrapper uses VI for N64 races only and never modifies its source. */
    memset(source, 128, sizeof(source));
    for (int y = 0; y < 16; y++) for (int x = 8; x < 16; x++)
        memset(source + (y * 16 + x) * 4, 220, 3);
    for (int i = 3; i < sizeof(source); i += 4) source[i] = 255;
    glBindTexture(GL_TEXTURE_2D, input);
    glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, 16, 16, GL_RGBA, GL_UNSIGNED_BYTE, source);
    glGenTextures(1, &output); glBindTexture(GL_TEXTURE_2D, output);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, 16, 16, 0, GL_RGBA, GL_UNSIGNED_BYTE, NULL);
    glGenFramebuffers(1, &destination); glBindFramebuffer(GL_FRAMEBUFFER, destination);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, output, 0);
    PictureChanged(0, VRAM_HEIGHT);
    colour = input; n64Track = sceneWide = 1; memset(pictureOwners, 0, sizeof(pictureOwners));
    for (int pc = 0; pc < 2; pc++) {
        portSettings.render.selection = RENDER_AUTO;
        portSettings.render.ps1Selection = pc ? RENDER_PC : RENDER_PS1; RenderCommit();
        glBindFramebuffer(GL_DRAW_FRAMEBUFFER, destination);
        BlitShown(0, 0, 0, 16, 16, 0, 0, 16, 16);
        glBindFramebuffer(GL_READ_FRAMEBUFFER, destination);
        glReadPixels(0, 0, 16, 16, GL_RGBA, GL_UNSIGNED_BYTE, pixels);
        if (pc) assert(!memcmp(pixels, previous, sizeof(pixels)));
        memcpy(previous, pixels, sizeof(pixels));
        assert(pixels[(8 * 16 + 7) * 4] < 140 && pixels[(8 * 16 + 8) * 4] > 210);
    }
    /* A settings commit must not reprocess the already drawn picture with the new profile. */
    RememberPicture(0, 0, 320, 512);
    portSettings.render.selection = RENDER_PS1; RenderCommit();
    glBindFramebuffer(GL_DRAW_FRAMEBUFFER, destination);
    BlitShown(0, 0, 0, 16, 16, 0, 0, 16, 16);
    glBindFramebuffer(GL_READ_FRAMEBUFFER, destination);
    glReadPixels(0, 0, 16, 16, GL_RGBA, GL_UNSIGNED_BYTE, pixels);
    assert(!memcmp(previous, pixels, sizeof(pixels)));
    memset(pictureOwners, 0, sizeof(pictureOwners));
    portSettings.render.selection = RENDER_AUTO; RenderCommit();
    glBindFramebuffer(GL_FRAMEBUFFER, raw);
    glReadPixels(0, 0, 16, 16, GL_RGBA, GL_UNSIGNED_BYTE, pixels);
    assert(!memcmp(source, pixels, sizeof(source)));
    framebuffer = raw;
    N64RenderSettings savedN64 = portSettings.render.n64;
    /* Isolate the VI from the PC renderer's own presentation effect. */
    portSettings.render.pc.rgb565 = 0;
    /* Explicit PS1/PC selection on an N64 track must bypass every N64 display control. */
    for (int id = RENDER_PS1; id <= RENDER_PC; id++) for (int other = 0; other < 2; other++) {
        portSettings.render.selection = (RendererId)id;
        portSettings.render.n64 = (N64RenderSettings){other, other, other, other, other, other};
        n64Track = sceneWide = 1; RenderCommit();
        glBindFramebuffer(GL_DRAW_FRAMEBUFFER, destination);
        BlitShown(0, 0, 0, 16, 16, 0, 0, 16, 16);
        glBindFramebuffer(GL_READ_FRAMEBUFFER, destination);
        glReadPixels(0, 0, 16, 16, GL_RGBA, GL_UNSIGNED_BYTE, pixels);
        assert(!memcmp(source, pixels, sizeof(source)));
    }
    portSettings.render.n64 = savedN64;
    portSettings.render.selection = RENDER_AUTO;
    for (int menu = 0; menu < 2; menu++) {
        n64Track = menu; RenderCommit(); sceneWide = 0; picturesWithoutScene = 1000;
        glBindFramebuffer(GL_DRAW_FRAMEBUFFER, destination);
        BlitShown(0, 0, 0, 16, 16, 0, 0, 16, 16);
        glBindFramebuffer(GL_READ_FRAMEBUFFER, destination);
        glReadPixels(0, 0, 16, 16, GL_RGBA, GL_UNSIGNED_BYTE, pixels);
        assert(!memcmp(source, pixels, sizeof(source))); /* PS1 and menus bypass VI */
    }
    assert(glGetError() == GL_NO_ERROR);
    glDeleteTextures(1, &input); glDeleteTextures(1, &output);
    glDeleteFramebuffers(1, &raw); glDeleteFramebuffers(1, &destination);
    puts("N64 display: magic-square RGB5, native pixel scale, VI dedither, sharp edges, PC independence and menu/PS1 bypass: OK");
}

static GLuint ViTarget(GLuint *texture, int width, int height, const uint8_t *pixels)
{
    GLuint target;
    glGenTextures(1, texture); glBindTexture(GL_TEXTURE_2D, *texture);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, width, height, 0, GL_RGBA, GL_UNSIGNED_BYTE, pixels);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glGenFramebuffers(1, &target); glBindFramebuffer(GL_FRAMEBUFFER, target);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, *texture, 0);
    assert(glCheckFramebufferStatus(GL_FRAMEBUFFER) == GL_FRAMEBUFFER_COMPLETE);
    return target;
}

/* Frozen pre-optimization scheduler: three RGBA8 passes, including disabled copy stages.
   Keep this independent of N64Picture's pass selection and ping-pong bookkeeping. */
static void ViReference(GLuint source, int x, int y, int width, int height,
                        GLuint destination, int outWidth, int outHeight, int flip,
                        const N64RenderSettings *settings)
{
    N64ViInit();
    GLuint textures[2], targets[2];
    for (int i = 0; i < 2; i++) targets[i] = ViTarget(&textures[i], width, height, NULL);
    glUseProgram(n64ViProgram); glBindVertexArray(n64ViArray);
    glDisable(GL_SCISSOR_TEST); glDisable(GL_DEPTH_TEST); glDisable(GL_BLEND);
    glViewport(0, 0, width, height);
    glUniform1i(glGetUniformLocation(n64ViProgram, "source"), 0);
    glUniform1i(glGetUniformLocation(n64ViProgram, "pixelScale"), scale);
    glUniform1i(glGetUniformLocation(n64ViProgram, "precisionOn"), settings->colourPrecision);
    glUniform1i(glGetUniformLocation(n64ViProgram, "ditherOn"), settings->dither);
    for (int pass = 0; pass < 3; pass++) {
        glBindFramebuffer(GL_DRAW_FRAMEBUFFER, targets[pass & 1]);
        glBindTexture(GL_TEXTURE_2D, pass ? textures[(pass - 1) & 1] : source);
        glUniform4i(glGetUniformLocation(n64ViProgram, "region"), pass ? 0 : x, pass ? 0 : y, width, height);
        int stage = pass;
        if ((pass == 1 && (!settings->dedither || !settings->colourPrecision)) ||
            (pass == 2 && !settings->divot)) stage = -1;
        glUniform1i(glGetUniformLocation(n64ViProgram, "stage"), stage);
        glDrawArrays(GL_TRIANGLES, 0, 3);
    }
    glBindFramebuffer(GL_READ_FRAMEBUFFER, targets[0]);
    glBindFramebuffer(GL_DRAW_FRAMEBUFFER, destination);
    glBlitFramebuffer(0, 0, width, height, 0, flip ? outHeight : 0, outWidth, flip ? 0 : outHeight,
                      GL_COLOR_BUFFER_BIT, settings->resample ? GL_LINEAR : GL_NEAREST);
    glUseProgram(scene->program);
    glDeleteFramebuffers(2, targets); glDeleteTextures(2, textures);
}

static void ViPasses(void)
{
    uint8_t source[32 * 24 * 4], pixels[sizeof(source)], expected[sizeof(source)];
    const int cases[][7] = {
        /* x, y, width, height, output width, output height, flip */
        {0, 0, 32, 24, 32, 24, 0},
        {3, 5, 13, 11, 13, 11, 0},
        {3, 5, 13, 11, 19, 17, 0},
        {3, 5, 13, 11, 7, 5, 0},
        {3, 5, 13, 11, 19, 17, 1}
    };
    GLuint input, output;
    RenderSettings saved = portSettings.render;
    glActiveTexture(GL_TEXTURE0);
    for (int y = 0; y < 24; y++) for (int x = 0; x < 32; x++) {
        int i = (y * 32 + x) * 4;
        source[i] = (x * 31 + y * 7) & 255;
        source[i + 1] = (x * 3 + y * 29) & 255;
        source[i + 2] = y < 12 ? 100 : x == 15 ? 0 : 128;
        source[i + 3] = 255; /* Scene clears and blend state retain opaque framebuffer alpha. */
    }
    GLuint raw = ViTarget(&input, 32, 24, source);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    GLuint destination = ViTarget(&output, 32, 24, NULL);
    PictureChanged(0, VRAM_HEIGHT);
    colour = wideColour = input; framebuffer = wideFramebuffer = raw;
    n64Track = sceneWide = 1; memset(pictureOwners, 0, sizeof(pictureOwners));
    portSettings.render.selection = RENDER_AUTO;
    for (int testScale = 1; testScale <= 2; testScale++) {
        scale = testScale;
        for (int bits = 0; bits < 64; bits++) {
            N64RenderSettings options = {bits & 1, (bits >> 1) & 1, (bits >> 2) & 1,
                                        (bits >> 3) & 1, (bits >> 4) & 1, (bits >> 5) & 1};
            portSettings.render.n64 = options; RenderCommit();
            /* Present/readback must use the drawn picture's settings, even after a commit. */
            RememberPicture(0, 0, 320, 512);
            portSettings.render.n64.colourPrecision = !options.colourPrecision; RenderCommit();
            int passes = options.colourPrecision + (options.colourPrecision && options.dedither) + options.divot;
            for (int c = 0; c < sizeof(cases) / sizeof(cases[0]); c++) {
                const int *r = cases[c];
                for (int wide = 0; wide <= 1; wide++) {
                    int oldWidth = n64ViWidth, oldHeight = n64ViHeight;
                    glBindFramebuffer(GL_DRAW_FRAMEBUFFER, destination);
                    drawCalls = 0;
                    BlitShown(wide, r[0], r[1], r[0] + r[2], r[1] + r[3],
                              0, r[6] ? r[5] : 0, r[4], r[6] ? 0 : r[5]);
                    /* Repeated source crops may be cached even when output size/flip changes. */
                    assert(drawCalls == 0 || drawCalls == passes + (!passes && options.resample));
                    if (wide && passes) assert(drawCalls == 0);
                    if (!passes) assert(n64ViWidth == oldWidth && n64ViHeight == oldHeight);
                    if (passes) {
                        assert(n64ViTexture[0] && n64ViFramebuffer[0]);
                        assert(!!n64ViTexture[1] == (passes > 1));
                        assert(!!n64ViFramebuffer[1] == (passes > 1));
                        GLint targetWidth, targetHeight;
                        for (int i = 0; i < SDL_min(passes, 2); i++) {
                            glBindTexture(GL_TEXTURE_2D, n64ViTexture[i]);
                            glGetTexLevelParameteriv(GL_TEXTURE_2D, 0, GL_TEXTURE_WIDTH, &targetWidth);
                            glGetTexLevelParameteriv(GL_TEXTURE_2D, 0, GL_TEXTURE_HEIGHT, &targetHeight);
                            assert(targetWidth == r[2] && targetHeight == r[3]);
                        }
                    }
                    glBindFramebuffer(GL_READ_FRAMEBUFFER, destination);
                    glReadPixels(0, 0, r[4], r[5], GL_RGBA, GL_UNSIGNED_BYTE, pixels);
                    ViReference(input, r[0], r[1], r[2], r[3], destination, r[4], r[5], r[6], &options);
                    glBindFramebuffer(GL_READ_FRAMEBUFFER, destination);
                    glReadPixels(0, 0, r[4], r[5], GL_RGBA, GL_UNSIGNED_BYTE, expected);
                    if (memcmp(pixels, expected, r[4] * r[5] * 4)) {
                        fprintf(stderr, "VI mismatch: scale=%d options=%d crop=%d wide=%d\n", testScale, bits, c, wide);
                        abort();
                    }
                }
            }
        }
    }
    glBindFramebuffer(GL_READ_FRAMEBUFFER, raw);
    glReadPixels(0, 0, 32, 24, GL_RGBA, GL_UNSIGNED_BYTE, pixels);
    assert(!memcmp(source, pixels, sizeof(source)));
    assert(glGetError() == GL_NO_ERROR);
    glDeleteTextures(1, &input); glDeleteTextures(1, &output);
    glDeleteFramebuffers(1, &raw); glDeleteFramebuffers(1, &destination);
    portSettings.render = saved; memset(pictureOwners, 0, sizeof(pictureOwners)); n64Track = sceneWide = 0; RenderCommit();
    puts("N64 VI: all 64 settings combinations match the old three-pass RGBA output; enabled-pass counts, crops, scaling, flips, wide/readback and snapshots: OK");
}

static void ViResampling(void)
{
    const int sizes[][2] = {{320, 240}, {640, 480}, {960, 720}, {7, 5}};
    RenderSettings saved = portSettings.render;
    for (int testScale = 1; testScale <= 2; testScale++) {
        int width = 1024 * testScale, height = 512 * testScale;
        size_t bytes = (size_t)width * height * 4;
        uint8_t *source = malloc(bytes), *pixels = malloc(960 * 720 * 4), *expected = malloc(960 * 720 * 4);
        assert(source && pixels && expected);
        for (int y = 0; y < height; y++) for (int x = 0; x < width; x++) {
            size_t i = ((size_t)y * width + x) * 4;
            source[i] = (x * 31 + y * 7) & 255;
            source[i + 1] = (x * 3 + y * 29) & 255;
            source[i + 2] = (x + y * 13) & 255;
            source[i + 3] = 255;
        }
        GLuint input, output;
        glActiveTexture(GL_TEXTURE0);
        GLuint raw = ViTarget(&input, width, height, source);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        GLuint destination = ViTarget(&output, 970, 730, NULL);
        PictureChanged(0, VRAM_HEIGHT);
        colour = input; framebuffer = raw; scale = testScale;
        ResetRenderSettings();
        portSettings.render.n64 = (N64RenderSettings){1, 0, 1, 1, 0, 1};
        memset(pictureOwners, 0, sizeof(pictureOwners)); n64Track = sceneWide = 1; RenderCommit();
        for (int c = 0; c < sizeof(sizes) / sizeof(sizes[0]); c++) for (int flip = 0; flip <= 1; flip++) {
            int x = 53 * scale, y = 240 * scale, w = 320 * scale, h = 240 * scale;
            int outW = sizes[c][0], outH = sizes[c][1];
            glBindFramebuffer(GL_DRAW_FRAMEBUFFER, destination);
            drawCalls = 0;
            BlitShown(0, x, y, x + w, y + h, 3, 5 + (flip ? outH : 0), 3 + outW, 5 + (flip ? 0 : outH));
            assert(drawCalls == 1); /* Only direct final resampling, no intermediate VI passes. */
            glBindFramebuffer(GL_READ_FRAMEBUFFER, destination);
            glReadPixels(3, 5, outW, outH, GL_RGBA, GL_UNSIGNED_BYTE, pixels);
            ViReference(input, x, y, w, h, destination, outW, outH, flip, &portSettings.render.n64);
            glBindFramebuffer(GL_READ_FRAMEBUFFER, destination);
            glReadPixels(0, 0, outW, outH, GL_RGBA, GL_UNSIGNED_BYTE, expected);
            for (size_t i = 0; i < (size_t)outW * outH * 4; i++) {
                if (pixels[i] != expected[i]) {
                    fprintf(stderr, "VI resample mismatch: scale=%d size=%d flip=%d byte=%zu got=%d expected=%d\n",
                            scale, c, flip, i, pixels[i], expected[i]);
                    abort();
                }
            }
        }
        assert(glGetError() == GL_NO_ERROR);
        glDeleteTextures(1, &input); glDeleteTextures(1, &output);
        glDeleteFramebuffers(1, &raw); glDeleteFramebuffers(1, &destination);
        free(source); free(pixels); free(expected);
    }
    portSettings.render = saved; n64Track = sceneWide = 0; RenderCommit();
    puts("N64 VI bypass: native-size scene textures, offset destinations, resized readback and flipped presentation match the old RGBA output: OK");
}

static void SaturnPresentation(void)
{
    /* Direct sRGB presentation preserves scene bytes, including dark detail. */
    uint8_t source[40*4*4], pixels[64*4*4], unchanged[sizeof(source)];
    memset(source,255,sizeof(source));
    for(int y=1;y<3;y++) for(int x=0;x<32;x++) {
        uint8_t *p=&source[(y*40+x+4)*4];
        p[0]=x*8;p[1]=(31-x)*8;p[2]=(y==1 ? x*8 : 255);
    }
    GLuint input,output;
    GLuint raw=ViTarget(&input,40,4,source), destination=ViTarget(&output,70,10,NULL);
    RenderSettings settings=RENDER_DEFAULTS;
    GLint previous;
    glGetIntegerv(GL_CURRENT_PROGRAM,&previous);
    for(int race=0;race<2;race++) for(int scale=1;scale<=2;scale++) for(int flip=0;flip<4;flip++) {
        int w=32*scale,h=2*scale;
        RenderPresent p={.texture=input,.framebuffer=raw,.scale=scale,.race=race,
            .left=4,.top=1,.right=36,.bottom=3,
            .x0=3+((flip&1)?w:0),.y0=2+((flip&2)?h:0),
            .x1=3+((flip&1)?0:w),.y1=2+((flip&2)?0:h)};
        /* Repeat without changing the input: presentation must not compound. */
        for(int repeat=0;repeat<2;repeat++) {
            glBindFramebuffer(GL_DRAW_FRAMEBUFFER,destination);
            saturnPipeline.present(&settings.saturn,&p);
            GLint current;glGetIntegerv(GL_CURRENT_PROGRAM,&current);assert(current==previous);
            glBindFramebuffer(GL_READ_FRAMEBUFFER,destination);
            glReadPixels(3,2,w,h,GL_RGBA,GL_UNSIGNED_BYTE,pixels);
            for(int y=0;y<h;y++) for(int x=0;x<w;x++) {
                int sx=(flip&1) ? 31-x/scale : x/scale;
                int sy=(flip&2) ? 1-y/scale : y/scale;
                const uint8_t *q=&pixels[(y*w+x)*4];
                assert(q[0]==sx*8 && q[1]==(31-sx)*8);
                assert(q[2]==(sy ? 255:sx*8) && q[3]==255);
                if(sx==0) assert(q[0]==0);
            }
        }
    }
    glBindFramebuffer(GL_READ_FRAMEBUFFER,raw);
    glReadPixels(0,0,40,4,GL_RGBA,GL_UNSIGNED_BYTE,unchanged);
    assert(!memcmp(source,unchanged,sizeof(source)));
    glDeleteTextures(1,&input);glDeleteTextures(1,&output);
    glDeleteFramebuffers(1,&raw);glDeleteFramebuffers(1,&destination);
    assert(glGetError()==GL_NO_ERROR);
    puts("Saturn direct sRGB presentation: black/white, all RGB5 levels, crops, nearest scaling, flips and unchanged source: OK");
}

/* Real ownership transitions and cache invalidation, without shader-test shortcuts. */
static void RendererLifecycle(void)
{
    GLuint input, output;
    uint8_t source[16 * 16 * 4], before[sizeof(source)], after[sizeof(source)];
    memset(source, 100, sizeof(source));
    for (int i = 3; i < sizeof(source); i += 4) source[i] = 255;
    glActiveTexture(GL_TEXTURE0);
    GLuint raw = ViTarget(&input, 16, 16, source);
    GLuint destination = ViTarget(&output, 16, 16, NULL);
    colour = input; framebuffer = raw; wideFramebuffer = 0; wideMargin = 0; areaWide = 0;
    scale = 1; PictureChanged(0, VRAM_HEIGHT); memset(pictureOwners, 0, sizeof(pictureOwners)); n64Track = sceneWide = 1;
    ResetRenderSettings();
    RenderCommit();
    GpuRendererShutdown();
    RememberPicture(0, 0, 320, 512);
    for (int repeat = 0; repeat < 2; repeat++) {
        glBindFramebuffer(GL_DRAW_FRAMEBUFFER, destination);
        drawCalls = 0;
        BlitShown(0, 0, 0, 16, 16, 0, 0, 16, 16);
        assert(drawCalls == (repeat ? 0 : 3));
        assert(!pipeline && !scene && !n64Program.program); /* presentation needs only VI */
    }
    glBindFramebuffer(GL_READ_FRAMEBUFFER, destination);
    glReadPixels(0, 0, 16, 16, GL_RGBA, GL_UNSIGNED_BYTE, before);
    /* A real backend clear changes pixels and invalidates the cached VI output. */
    GpuClearArea(0, 0, 16, 16, 240, 40, 40);
    glBindFramebuffer(GL_DRAW_FRAMEBUFFER, destination);
    drawCalls = 0;
    BlitShown(0, 0, 0, 16, 16, 0, 0, 16, 16);
    assert(drawCalls == 3);
    glBindFramebuffer(GL_READ_FRAMEBUFFER, destination);
    glReadPixels(0, 0, 16, 16, GL_RGBA, GL_UNSIGNED_BYTE, after);
    assert(memcmp(before, after, sizeof(before)));
    GLuint viTextures[2] = {n64ViTexture[0], n64ViTexture[1]};
    assert(viTextures[0] && glIsTexture(viTextures[0]));
    portSettings.render.selection = RENDER_PS1; RenderCommit();
    /* A commit alone retains the last N64 picture and its cached presentation. */
    glBindFramebuffer(GL_DRAW_FRAMEBUFFER, destination);
    drawCalls = 0;
    BlitShown(0, 0, 0, 16, 16, 0, 0, 16, 16);
    assert(drawCalls == 0 && !n64Program.program);
    /* New geometry takes ownership; retain the last presenter until it switches too. */
    GpuDrawState state = {.semi = -1};
    GpuVertex v = {.x = 0, .y = 0, .a = 255, .r = 255};
    Begin(&state, 3); Add(&v, 0);
    v.x = 4; Add(&v, 0); v.y = 4; Add(&v, 0);
    Flush();
    assert(pipeline == &ps1Pipeline && pictureOwners[0].render.id == RENDER_PS1);
    assert(n64ViProgram && viCache.valid);
    GLuint drawingProgram = scene->program;
    glBindFramebuffer(GL_DRAW_FRAMEBUFFER, destination);
    BlitShown(0, 0, 0, 16, 16, 0, 0, 16, 16);
    assert(scene->program == drawingProgram);
    assert(!n64Program.program && !n64ViProgram && !n64ViArray && !n64ViTexture[0] && !viCache.valid);
    assert(!glIsTexture(viTextures[0]) && !glIsTexture(viTextures[1]));
    for (int cycle = 0; cycle < 4; cycle++) {
        portSettings.render.selection = RENDER_N64; RenderCommit(); memset(pictureOwners, 0, sizeof(pictureOwners));
        glBindFramebuffer(GL_DRAW_FRAMEBUFFER, destination);
        drawCalls = 0;
        BlitShown(0, 0, 0, 16, 16, 0, 0, 16, 16);
        assert(drawCalls == 3 && n64ViProgram && !n64Program.program);
        UsePipeline(cycle & 1 ? RENDER_PS1 : RENDER_PC);
        drawingProgram = scene->program;
        glBindFramebuffer(GL_DRAW_FRAMEBUFFER, destination);
        drawCalls = 0;
        BlitShown(0, 0, 0, 16, 16, 0, 0, 16, 16);
        assert(!drawCalls && scene->program == drawingProgram && isProgram(drawingProgram));
        portSettings.render.selection = cycle & 1 ? RENDER_PS1 : RENDER_PC;
        RenderCommit();
        glBindFramebuffer(GL_DRAW_FRAMEBUFFER, destination);
        BlitShown(0, 0, 0, 16, 16, 0, 0, 16, 16);
        assert(!n64Program.program && !n64ViProgram && !viCache.valid);
    }
    GpuRendererShutdown();
    assert(!pipeline && !scene && !presentPipeline && !isProgram(drawingProgram));
    assert(glGetError() == GL_NO_ERROR);
    glDeleteTextures(1, &input); glDeleteTextures(1, &output);
    glDeleteFramebuffers(1, &raw); glDeleteFramebuffers(1, &destination);
    puts("Presentation skips scene compilation and retains the drawing program; unused renderer resources are released: OK");
}


/* The displayed and next console screens can have different owners/settings. */
static void ScreenBufferOwnership(void)
{
    GLuint input, output;
    size_t bytes = VRAM_WIDTH * VRAM_HEIGHT * 4;
    uint8_t *source = malloc(bytes), before[16*16*4], after[sizeof(before)], rawPixels[sizeof(before)];
    memset(source, 100, bytes);
    for (size_t i = 3; i < bytes; i += 4) source[i] = 255;
    glActiveTexture(GL_TEXTURE0);
    GLuint raw = ViTarget(&input, VRAM_WIDTH, VRAM_HEIGHT, source);
    GLuint destination = ViTarget(&output, 16, 16, NULL);
    colour = input; framebuffer = raw; wideMargin = areaWide = 0; scale = 1;
    ResetRenderSettings();
    for (int direction = 0; direction < 3; direction++) {
        n64Track = sceneWide = 1; PictureChanged(0, VRAM_HEIGHT);
        memset(pictureOwners, 0, sizeof(pictureOwners));
        portSettings.render.selection = direction ? RENDER_N64 : RENDER_PS1;
        portSettings.render.n64 = (N64RenderSettings){1,1,1,1,1,1};
        RenderCommit(); RememberPicture(0, 0, 320, 256);
        RenderFrame front = pictureOwners[0].render;
        uint64_t frontRevision = pictureRevision[0];
        glBindFramebuffer(GL_DRAW_FRAMEBUFFER, destination);
        BlitShown(0, 0, 0, 16, 16, 0, 0, 16, 16);
        glBindFramebuffer(GL_READ_FRAMEBUFFER, destination);
        glReadPixels(0, 0, 16, 16, GL_RGBA, GL_UNSIGNED_BYTE, before);
        portSettings.render.selection = direction == 1 ? RENDER_PS1 : RENDER_N64;
        if (direction == 2) portSettings.render.n64.colourPrecision = 0;
        RenderCommit();
        areaX = 0; areaY = 256; areaW = 320; areaH = 240;
        GpuDrawState state = {.semi = -1};
        GpuVertex v = {.x = 0, .y = 256, .a = 255, .r = 255};
        Begin(&state, 3); Add(&v, 0); v.x = 4; Add(&v, 0); v.y = 260; Add(&v, 0); Flush();
        GLuint drawingProgram = scene->program;
        assert(pictureRevision[0] == frontRevision && pictureRevision[1] > frontRevision);
        assert(!memcmp(&pictureOwners[0].render, &front, sizeof(front)));
        assert(pictureOwners[1].render.id == RenderCurrent()->id);
        glBindFramebuffer(GL_READ_FRAMEBUFFER, raw);
        glReadPixels(0, 0, 16, 16, GL_RGBA, GL_UNSIGNED_BYTE, rawPixels);
        for (int i = 0; i < sizeof(rawPixels); i++) assert(rawPixels[i] == (i % 4 == 3 ? 255 : 100));
        /* Even scene/menu state for the next frame must not alter the front picture. */
        sceneWide = 0; picturesWithoutScene = 1000;
        glBindFramebuffer(GL_DRAW_FRAMEBUFFER, destination);
        drawCalls = 0;
        BlitShown(0, 0, 0, 16, 16, 0, 0, 16, 16);
        assert(!drawCalls && scene->program == drawingProgram);
        glBindFramebuffer(GL_READ_FRAMEBUFFER, destination);
        glReadPixels(0, 0, 16, 16, GL_RGBA, GL_UNSIGNED_BYTE, after);
        assert(!memcmp(before, after, sizeof(before)));
        /* Image copies carry their source owner, including when the CPU policy differs. */
        GpuMoveFramebuffer(0, 0, 320, 240, 0, 256);
        assert(!memcmp(&pictureOwners[1].render, &front, sizeof(front)) && pictureOwners[1].race);
        GpuClearArea(0, 256, 320, 240, 100, 100, 100);
        assert(pictureOwners[1].render.id == RenderCurrent()->id && !pictureOwners[1].race);
        GpuWriteFramebuffer(0, 256, 1, 1);
        assert(!memcmp(&pictureOwners[0].render, &front, sizeof(front)));
        assert(pictureRevision[0] == frontRevision);
        glBindFramebuffer(GL_DRAW_FRAMEBUFFER, destination);
        drawCalls = 0;
        BlitShown(0, 0, 0, 16, 16, 0, 0, 16, 16);
        assert(!drawCalls && scene->program == drawingProgram);
        GpuWriteFramebuffer(0, 0, 1, 1);
        assert(!memcmp(&pictureOwners[0].render, RenderCurrent(), sizeof(front)));
        assert(pictureRevision[0] > frontRevision);
        uint64_t backRevision = pictureRevision[1];
        frontRevision = pictureRevision[0];
        GpuClearArea(0, 255, 1, 2, 100, 100, 100);
        assert(pictureRevision[0] > frontRevision && pictureRevision[1] > backRevision);
        /* Restore the test source for the next direction. */
        glBindTexture(GL_TEXTURE_2D, input);
        glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, VRAM_WIDTH, VRAM_HEIGHT, GL_RGBA, GL_UNSIGNED_BYTE, source);
    }
    /* A readback crop straddling both screens must notice a change in either one,
       even when the other screen previously had the newer revision. */
    ResetRenderSettings();
    n64Track = sceneWide = 1; RenderCommit();
    RememberPicture(0, 0, 320, 512); PictureChanged(0, VRAM_HEIGHT);
    for (int update = 0; update < 3; update++) {
        if (update) GpuClearArea(0, update == 1 ? 260 : 250, 16, 1, 240, 40, 40);
        for (int repeat = 0; repeat < 2; repeat++) {
            glBindFramebuffer(GL_DRAW_FRAMEBUFFER, destination);
            drawCalls = 0;
            BlitShown(0, 0, 248, 16, 264, 0, 0, 16, 16);
            assert(drawCalls == (repeat ? 0 : 3));
        }
    }
    GpuClearDepth(0, 248, 16, 16, 0);
    glBindFramebuffer(GL_DRAW_FRAMEBUFFER, destination);
    drawCalls = 0;
    BlitShown(0, 0, 248, 16, 264, 0, 0, 16, 16);
    assert(!drawCalls); /* depth-only writes do not invalidate displayed colour */
    uint64_t frontRevision = pictureRevision[0], backRevision = pictureRevision[1];
    SetWideMargin(1);
    assert(pictureRevision[0] > frontRevision && pictureRevision[1] > backRevision);
    glBindFramebuffer(GL_DRAW_FRAMEBUFFER, destination);
    drawCalls = 0;
    BlitShown(0, 0, 248, 16, 264, 0, 0, 16, 16);
    assert(drawCalls == 3);
    SetWideMargin(0);
    assert(glGetError() == GL_NO_ERROR);
    GpuRendererShutdown();
    glDeleteTextures(1, &input); glDeleteTextures(1, &output);
    glDeleteFramebuffers(1, &raw); glDeleteFramebuffers(1, &destination); free(source);
    puts("Screen-buffer ownership and revisions: back-buffer draws/copies/clears/uploads preserve front VI cache and pixels: OK");
    puts("VI cache: crops spanning both buffers, depth-only clears and framebuffer reallocations: OK");
}

static void CheckHdLookup(int expected)
{
    GpuDrawState draw = {.textured = 1, .depth = 2};
    GpuVertex vertex = {0};
    hdLookups = 0;
    FindHd(&draw, &vertex, 1);
    assert(hdLookups == expected);
}

static void PcRgb565(void)
{
    uint8_t source[4 * 8 * 8], pixels[sizeof(source)];
    memset(source, 128, sizeof(source));
    GLuint input, output, raw = ViTarget(&input, 8, 8, source), dest = ViTarget(&output, 8, 8, NULL);
    RenderPresent p = {input, raw, 1, 1, 0, 0, 0, 8, 8, 0, 0, 8, 8};
    PcRenderSettings settings = {.rgb565 = 1};
    pcPipeline.present(&settings, &p);
    glBindFramebuffer(GL_READ_FRAMEBUFFER, dest);
    glReadPixels(0, 0, 8, 8, GL_RGBA, GL_UNSIGNED_BYTE, pixels);
    assert(pixels[0] == 123 && pixels[1] == 125 && pixels[2] == 123 && pixels[3] == 128);
    assert(pixels[4] == 132 && pixels[5] == 130 && pixels[6] == 132 && pixels[7] == 128);
    uint8_t full[sizeof(pixels)];
    memcpy(full, pixels, sizeof(full));
    p.left = p.top = 1;
    p.x1 = p.y1 = 7;
    pcPipeline.present(&settings, &p);
    glBindFramebuffer(GL_READ_FRAMEBUFFER, dest);
    glReadPixels(0, 0, 8, 8, GL_RGBA, GL_UNSIGNED_BYTE, pixels);
    for (int y = 0; y < 7; y++) assert(!memcmp(pixels + y * 32, full + (y + 1) * 32 + 4, 7 * 4));
    p.left = p.top = 0;
    p.x1 = p.y1 = 8;
    settings.rgb565 = 0;
    pcPipeline.present(&settings, &p);
    glBindFramebuffer(GL_READ_FRAMEBUFFER, dest);
    glReadPixels(0, 0, 8, 8, GL_RGBA, GL_UNSIGNED_BYTE, pixels);
    assert(!memcmp(pixels, source, sizeof(source)));
    glBindFramebuffer(GL_READ_FRAMEBUFFER, raw);
    glReadPixels(0, 0, 8, 8, GL_RGBA, GL_UNSIGNED_BYTE, pixels);
    assert(!memcmp(pixels, source, sizeof(source)));
    assert(glGetError() == GL_NO_ERROR);
    glDeleteFramebuffers(1, &raw); glDeleteFramebuffers(1, &dest);
    glDeleteTextures(1, &input); glDeleteTextures(1, &output);
    puts("PC dithered RGB565 quantizes both channel precisions, keeps crop phase and preserves its source: OK");
}

static void ProfileNativeResolution(void)
{
    Flush();
    wideMargin = areaWide = wideDirty = 0;
    framebuffer = colour = depthBuffer = 0;
    scale = 1; rasterX = rasterY = 1;
    assert(CreateFramebuffer());
    initialised = 1;
    ResetRenderSettings();
    portSettings.render.modern.tonemapper = MODERN_TONEMAP_ORIGINAL;
    for (portPal = 0; portPal <= 1; portPal++) {
        for (int id = RENDER_PS1; id < RENDER_COUNT; id++) {
            portSettings.render.selection = (RendererId)id;
            for (int multiplier = 1; multiplier <= 2; multiplier++) {
                *RenderScaleSetting(&portSettings.render, (RendererId)id) = multiplier;
                RenderCommit(); GpuUpdateResolution();
                for (int enabled = 0; enabled <= 1; enabled++) {
                    portSettings.hdTextures = enabled;
                    CheckHdLookup(enabled && (id == RENDER_MODERN || multiplier > 1));
                }
                int nativeW, nativeH, width, height;
                RenderNativeSize((RendererId)id, portPal, &nativeW, &nativeH);
                GpuPixelSize(320, GPU_LINES, &width, &height);
                assert(width == nativeW * multiplier && height == nativeH * multiplier);
                for (int screen = 0; screen < 2; screen++) {
                    int y = screen * 256;
                    GpuSetDrawArea(0, y, 320, GPU_LINES, 0, y);
                    GpuClearArea(0, y, 320, GPU_LINES, 0, 0, 0);
                    GpuVertex v[4] = {{.x=0,.y=y}, {.x=320,.y=y},
                                     {.x=0,.y=y+GPU_LINES}, {.x=320,.y=y+GPU_LINES}};
                    for (int k=0;k<4;k++) { v[k].r=248; v[k].a=255; v[k].w=100; }
                    GpuDrawState draw = {.semi=-1};
                    GpuTriangles(&draw, v, 3); GpuTriangles(&draw, v+1, 3); Flush();
                    const uint8_t *pixels = GpuReadArea(0, y, 320, GPU_LINES, &width, &height);
                    assert(width == nativeW * multiplier && height == nativeH * multiplier);
                    const int corners[] = {0, width-1, (height-1)*width, height*width-1};
                    for (int k=0;k<4;k++) {
                        if (pixels[corners[k]*4] < 240 || pixels[corners[k]*4+1] != 0)
                            fprintf(stderr, "profile=%d PAL=%d scale=%d screen=%d corner=%d RGB=%d,%d,%d\n",
                                id,portPal,multiplier,screen,k,pixels[corners[k]*4],pixels[corners[k]*4+1],pixels[corners[k]*4+2]);
                        assert(pixels[corners[k]*4] >= 240 && pixels[corners[k]*4+1] == 0);
                    }
                    if (id == RENDER_SATURN) {
                        SaturnNativePoly poly = {.colour=0x83e0,.mode=0xc0,.corners={0,1,3,2}};
                        draw.saturn = &poly;
                        GpuTriangles(&draw,v,4); Flush();
                        pixels = GpuReadArea(0, y, 320, GPU_LINES, &width, &height);
                        for (int k=0;k<4;k++) assert(pixels[corners[k]*4] == 0 && pixels[corners[k]*4+1] == 248);
                        SDL_setenv_unsafe("WIPEOUT_SATURN_REFERENCE", "1", 1);
                        for (int referenceDepth=0;referenceDepth<=1;referenceDepth++) {
                            portSettings.render.saturn.depth=referenceDepth; RenderCommit();
                            GpuClearArea(0,y,320,GPU_LINES,0,0,0);
                            GpuTriangles(&draw,v,4); Flush();
                            pixels=GpuReadArea(0,y,320,GPU_LINES,&width,&height);
                            for (int k=0;k<4;k++) assert(pixels[corners[k]*4] == 0 && pixels[corners[k]*4+1] == 248);
                        }
                        SDL_unsetenv_unsafe("WIPEOUT_SATURN_REFERENCE");
                        portSettings.render.saturn.depth=0; RenderCommit();
                    }
                }
                /* Copies, CPU readback and presentation preserve both native extents. */
                GpuReadBack(0, 256, 320, GPU_LINES);
                assert((psxVram[256+GPU_LINES-1][319] & 0x7fff) == (id == RENDER_SATURN ? 31 << 5 : 31));
                GpuMoveFramebuffer(0, 256, 320, GPU_LINES, 0, 0);
                const uint8_t *shown = GpuReadPicture(0, 0, 320, GPU_LINES, 0, width, height);
                assert(shown[(height*width-1)*4+(id == RENDER_SATURN ? 1 : 0)] >= 240);
                SetWideMargin(17);
                int wideW, wideH;
                GpuPixelSize(354, GPU_LINES, &wideW, &wideH);
                shown = GpuReadPicture(0, 0, 320, GPU_LINES, 17, wideW, wideH);
                assert(shown[((wideH/2)*wideW+wideW/2)*4+(id == RENDER_SATURN ? 1 : 0)] >= 240);
                SetWideMargin(0);
                assert(glGetError() == GL_NO_ERROR);
            }
        }
    }
    portPal = 0;
    assert(GpuWindowScale(RENDER_PC) == 2 && GpuWindowScale(RENDER_SATURN) == 4);
    portSettings.render.selection = RENDER_SATURN;
    portSettings.render.saturn.scale = 0;
    RenderCommit(); GpuUpdateResolution();
    assert(scale == 4 && PixelX(320) == 1408 && PixelY(240) == 960);
    CheckHdLookup(1);
    testWindowWidth = 352; testWindowHeight = 240;
    GpuUpdateResolution();
    assert(scale == 1);
    CheckHdLookup(0);
    initialised = 0;
    puts("Native profile grids: PS1 NTSC/PAL, N64, Saturn and PC; switches, scales, both buffers, quads, "
         "readback, widescreen and HD texture gating: OK");
}

static void HdrQuad(const GpuDrawState *draw, int x, int y, float depth, int tint, float u)
{
    GpuVertex v[6];
    static const int corners[] = {0, 1, 2, 1, 2, 3};
    for (int i = 0; i < 6; i++) {
        int corner = corners[i];
        v[i] = (GpuVertex){.x = x + (corner & 1) * 4, .y = y + (corner >> 1) * 4, .w = depth,
                           .u = u + 0.5f, .v = 148.5f, .r = tint, .g = tint, .b = tint, .a = 255};
    }
    GpuTriangles(draw, v, 6);
}

static int HdrRed(const uint8_t *pixels, int x, int y)
{
    return pixels[(y * 64 + x) * 4];
}

static void ModernHdr(void)
{
    Flush();
    GpuRendererShutdown();
    glDeleteFramebuffers(1, &framebuffer);
    glDeleteTextures(1, &colour);
    glDeleteRenderbuffers(1, &depthBuffer);
    wideMargin = areaWide = wideDirty = 0;
    portPal = 0;
    scale = 1; rasterX = rasterY = 1;
    assert(CreateFramebuffer());
    ResetRenderSettings();
    portSettings.render.selection = RENDER_MODERN;
    portSettings.render.modern.brightness = MODERN_BRIGHTNESS_MAX; /* Keep the HDR fixture's input levels fixed. */
    portSettings.render.modern.tonemapper = MODERN_TONEMAP_ORIGINAL;
    PortWideScene();
    RenderCommit();
    GpuSetDrawArea(0, 0, 320, 240, 0, 0);
    psxVram[148][174] = psxVram[148][180] = psxVram[148][200] = 31;
    GpuVramChanged(174, 148, 27, 1);

    /* The same highlight must survive nearest and filtered material paths. */
    for (int bilinear = 0; bilinear <= 1; bilinear++) {
        portSettings.render.modern.bilinear = bilinear;
        RenderCommit();
        GpuClearArea(0, 0, 320, 240, 0, 0, 0);
        GpuClearArea(32, 24, 4, 4, 179, 130, 90); /* A coloured midtone near a linear peak of 0.45. */
        GpuDrawState draw = {.textured = 1, .depth = 2, .semi = -1};
        HdrQuad(&draw, 10, 10, 100, 255, 174);
        HdrQuad(&draw, 24, 10, 200, 255, 174);
        GpuDrawState solid = {.semi = -1};
        HdrQuad(&solid, 24, 10, 50, 0, 0); /* A nearer surface hides the emitter. */
        draw.emission = (MaterialEmission){.enabled = 1, .uv = {174, 148, 11, 9}};
        HdrQuad(&draw, 38, 10, 100, 128, 174);
        HdrQuad(&draw, 54, 10, 100, 128, 200); /* Bright texel outside the tagged bulb's UVs. */
        HdrQuad(&solid, 24, 24, 50, 60, 0); /* Below the blend range. */
        GpuClearDepth(0, 0, 320, 240, 0);
        HdrQuad(&solid, 7, 10, 0, 22, 0); /* Opaque HUD covers both the source and its halo. */
        Flush();

        float encoded[4];
        glBindFramebuffer(GL_READ_FRAMEBUFFER, sceneHdr.framebuffer);
        glReadPixels(11, 11, 1, 1, GL_RGBA, GL_FLOAT, encoded);
        assert(encoded[0] > 1.9f && encoded[1] == 0.0f);

        const uint8_t *shown = GpuReadPicture(0, 0, 64, 32, 0, 64, 32);
        assert(HdrRed(shown, 14, 11) > 20); /* Unmarked overbright surface emits. */
        assert(HdrRed(shown, 10, 11) == 22);
        assert(HdrRed(shown, 28, 11) == 0);
        assert(HdrRed(shown, 42, 11) > 8);
        assert(HdrRed(shown, 58, 11) == 0);
        if (!bilinear) {
            const int highlight = (11 * 64 + 11) * 4;
            const int hud = (11 * 64 + 10) * 4;
            const int midtone = (25 * 64 + 33) * 4;
            assert(shown[highlight + 1] == 0 && shown[highlight + 2] == 0);
            assert(HdrRed(shown, 25, 25) == 60 && HdrRed(shown, 33, 25) == 179);
            assert(shown[midtone + 2] == 90);
            portSettings.render.modern.tonemapper = MODERN_TONEMAP_SOFT_SHADOWS;
            assert(RenderCommit());
            /* Apply the new profile to the same pixels and exercise the resolve cache. */
            RememberPicture(0, 0, 320, 240);
            shown = GpuReadPicture(0, 0, 64, 32, 0, 64, 32);
            assert(shown[highlight] > shown[highlight + 1] && shown[highlight + 1] > 100);
            assert(shown[highlight + 1] == shown[highlight + 2]);
            assert(HdrRed(shown, 25, 25) == 60);
            assert(abs(shown[midtone] - 177) <= 1 && abs(shown[midtone + 2] - 86) <= 1);
            assert(shown[hud] == 22 && shown[hud + 1] == 22 && shown[hud + 2] == 22);
            portSettings.render.modern.tonemapper = MODERN_TONEMAP_ORIGINAL;
            assert(RenderCommit());
            RememberPicture(0, 0, 320, 240);
            shown = GpuReadPicture(0, 0, 64, 32, 0, 64, 32);
            assert(shown[highlight + 1] == 0 && shown[highlight + 2] == 0);
        }
        int outW, outH;
        const uint8_t *raw = GpuReadArea(0, 0, 64, 32, &outW, &outH);
        assert(outW == 64 && outH == 32);
        assert(HdrRed(raw, 11, 11) == 255 && HdrRed(raw, 14, 11) == 0);
        GpuReadBack(10, 10, 5, 4);
        assert((psxVram[11][11] & 0x7fff) == 31 && (psxVram[11][14] & 0x7fff) == 0);

        SetWideMargin(12);
        GLuint output, destination = ViTarget(&output, 64, 32, NULL);
        uint8_t wide[64 * 32 * 4];
        glBindFramebuffer(GL_DRAW_FRAMEBUFFER, destination);
        BlitShown(1, 12, 0, 76, 32, 0, 0, 64, 32);
        glBindFramebuffer(GL_READ_FRAMEBUFFER, destination);
        glReadPixels(0, 0, 64, 32, GL_RGBA, GL_UNSIGNED_BYTE, wide);
        assert(!memcmp(shown, wide, sizeof(wide)));
        glBindFramebuffer(GL_DRAW_FRAMEBUFFER, destination);
        BlitShown(1, 14, 0, 74, 32, 0, 0, 60, 32);
        uint8_t cropped[60 * 32 * 4];
        glBindFramebuffer(GL_READ_FRAMEBUFFER, destination);
        glReadPixels(0, 0, 60, 32, GL_RGBA, GL_UNSIGNED_BYTE, cropped);
        for (int y = 0; y < 32; y++)
            assert(!memcmp(cropped + y * 60 * 4, wide + (y * 64 + 2) * 4, 60 * 4));

        draw.emission = (MaterialEmission){0};
        HdrQuad(&draw, 54, 10, 50, 255, 200);
        raw = GpuReadArea(0, 0, 64, 32, &outW, &outH);
        assert(HdrRed(raw, 55, 11) == 255 && HdrRed(raw, 58, 11) == 0);
        assert(wideHdrDirty && !wideDirty); /* Raw VRAM has no use for the HDR copy. */
        shown = GpuReadPicture(0, 0, 64, 32, 0, 64, 32);
        assert(HdrRed(shown, 58, 11) > 20 && !wideHdrDirty);
        SetWideMargin(0);
        glDeleteFramebuffers(1, &destination);
        glDeleteTextures(1, &output);

        /* A console write replaces both the scene colour and its emission. */
        for (int y = 10; y < 14; y++) memset(&psxVram[y][10], 0, 4 * sizeof(uint16_t));
        GpuWriteFramebuffer(10, 10, 4, 4);
        shown = GpuReadPicture(0, 0, 64, 32, 0, 64, 32);
        assert(HdrRed(shown, 14, 11) == 0);
        assert(glGetError() == GL_NO_ERROR);
    }

    SetScale(2, 320, 240);
    const uint8_t *shown = GpuReadPicture(0, 0, 64, 32, 0, 64, 32);
    assert(HdrRed(shown, 42, 11) > 8); /* The displayed scene survives resizing. */

    /* A thin emitter at high scale must give one halo with no secondary peaks. */
    SetScale(4, 320, 240);
    GpuClearArea(0, 0, 320, 240, 0, 0, 0);
    GpuSetDrawArea(30, 10, 1, 4, 0, 0);
    GpuDrawState light = {.textured = 1, .semi = -1};
    HdrQuad(&light, 30, 10, 100, 255, 174);
    GpuSetDrawArea(0, 0, 320, 240, 0, 0);
    shown = GpuReadPicture(0, 0, 64, 32, 0, 256, 128);
    const uint8_t *row = shown + 47 * 256 * 4;
    assert(row[124 * 4] > 20 && row[164 * 4] == 0);
    for (int x = 125; x < 164; x++) assert(row[x * 4] <= row[(x - 1) * 4] + 1);
    SetScale(1, 320, 240);

    /* Compare a textured subtractive shadow, an additive flare and a flare over
       a shadow clipped to black with opaque patches of the expected SDR colours. */
    psxVram[148][208] = 0x8000 | (8 << 10) | (8 << 5) | 8;
    GpuVramChanged(208, 148, 1, 1);
    for (int bilinear = 0; bilinear <= 1; bilinear++) {
        portSettings.render.modern.bilinear = bilinear;
        RenderCommit();
        GpuClearArea(0, 0, 320, 240, 96, 96, 96);
        GpuDrawState solid = {.semi = -1};
        GpuDrawState shadow = {.textured = 1, .raw = 1, .depth = 2, .semi = 2};
        GpuDrawState flare = {.semi = 1};
        HdrQuad(&shadow, 4, 4, 50, 255, 208);
        HdrQuad(&flare, 12, 4, 50, 80, 0);
        HdrQuad(&solid, 20, 4, 50, 24, 0);
        HdrQuad(&shadow, 20, 4, 50, 255, 208);
        HdrQuad(&flare, 20, 4, 50, 80, 0);
        HdrQuad(&solid, 28, 4, 50, 32, 0);
        HdrQuad(&solid, 36, 4, 50, 176, 0);
        HdrQuad(&solid, 44, 4, 50, 80, 0);
        int outW, outH;
        const uint8_t *raw = GpuReadArea(0, 0, 64, 32, &outW, &outH);
        assert(HdrRed(raw, 5, 5) == 32 && HdrRed(raw, 13, 5) == 176 && HdrRed(raw, 21, 5) == 80);
        for (int mapper = MODERN_TONEMAP_ORIGINAL; mapper <= MODERN_TONEMAP_SOFT_SHADOWS; mapper++) {
            portSettings.render.modern.tonemapper = mapper;
            RenderCommit();
            RememberPicture(0, 0, 320, 240);
            shown = GpuReadPicture(0, 0, 64, 32, 0, 64, 32);
            for (int x = 5; x <= 21; x += 8)
                assert(abs(HdrRed(shown, x, 5) - HdrRed(shown, x + 24, 5)) <= 1);
            if (mapper == MODERN_TONEMAP_ORIGINAL)
                assert(HdrRed(shown, 5, 5) == 32 && HdrRed(shown, 13, 5) == 176 && HdrRed(shown, 21, 5) == 80);
        }
    }

    /* Emission masks are per-material source data, not a fixture baked into GLSL. */
    portSettings.render.modern.bilinear = 0;
    portSettings.render.modern.tonemapper = MODERN_TONEMAP_ORIGINAL;
    RenderCommit();
    GpuClearArea(0, 0, 320, 240, 0, 0, 0);
    GpuDrawState masked = {.textured = 1, .depth = 2, .semi = -1,
                           .emission = {.enabled = 1, .uv = {200, 148, 2, 2}}};
    HdrQuad(&masked, 10, 10, 100, 128, 200);
    HdrQuad(&masked, 38, 10, 100, 128, 174);
    shown = GpuReadPicture(0, 0, 64, 32, 0, 64, 32);
    assert(HdrRed(shown, 14, 11) > 8 && HdrRed(shown, 42, 11) == 0);

    /* Both an explicit opt-out and an unavailable half-float target use SDR. */
    portSettings.render.modern.weaponLights = 0;
    for (int enabled = 0; enabled <= 1; enabled++) {
        portSettings.render.modern.bloom = enabled;
        RenderCommit();
        DeleteHdr(&sceneHdr);
        sceneHdr.unavailable = enabled;
        GpuClearArea(0, 0, 320, 240, 0, 0, 0);
        GpuDrawState draw = {.textured = 1, .depth = 2, .semi = -1};
        HdrQuad(&draw, 10, 10, 100, 255, 174);
        shown = GpuReadPicture(0, 0, 64, 32, 0, 64, 32);
        assert(HdrRed(shown, 11, 11) == 255 && HdrRed(shown, 14, 11) == 0);
        assert(glGetError() == GL_NO_ERROR);
    }
    GpuRendererShutdown();
    glDeleteFramebuffers(1, &framebuffer);
    glDeleteTextures(1, &colour);
    glDeleteRenderbuffers(1, &depthBuffer);
    puts("Modern HDR: overbright nearest and filtered materials, fixture mask, HUD, depth, smooth high-scale bloom, "
         "tonemapper switching, legacy blends, widescreen, SDR fallback and raw VRAM: OK");
}

/* A surface with known view coordinates: real material, batch and presentation paths. */
enum { DARK_HULL = 6 };
static void LitSurface(GpuDrawState draw, int kind)
{
    GpuVertex v[4] = {0};
    for (int i = 0; i < 4; i++) {
        v[i] = (GpuVertex){.x = (i & 1) * 64, .y = (i >> 1) * 32, .w = kind == 1 ? 0 : 1000,
            .r = 64, .g = 64, .b = 64, .a = 255, .u = .5f, .v = .5f,
            .noDepth = kind == 2, .view = {((i & 1) * 64 - 32) * 20, ((i >> 1) * 32 - 16) * 20, 1000}};
        if (kind == DARK_HULL) {
            v[i].r = v[i].g = v[i].b = 0;
            v[i].u = (i & 1) * 20.0f;
        }
    }
    GpuTriangles(&draw, v, 3); GpuTriangles(&draw, v + 1, 3);
}

static void ModernWeaponLights(void)
{
    Flush(); GpuRendererShutdown();
    wideMargin = areaWide = wideDirty = 0;
    portPal = 0; scale = 1; rasterX = rasterY = 1;
    assert(CreateFramebuffer());
    ResetRenderSettings();
    portSettings.render.selection = RENDER_MODERN;
    portSettings.render.modern.brightness = 100;
    portSettings.render.modern.bloom = 0;
    portSettings.render.modern.tonemapper = MODERN_TONEMAP_ORIGINAL;
    PortWideScene(); RenderCommit();
    GpuSetDrawArea(0, 0, 320, 240, 0, 0);
    psxVram[0][0] = 0x2108; /* grey material, kept dim by its original tint */
    GpuVramChanged(0, 0, 1, 1);
    /* Light and dark paint, repeated light paint for the rim, black, and yellow livery. */
    const uint16_t hull[] = {0x7fff, 0x2108, 0x7fff, 0x8000, 0x03ff};
    for (int i = 0; i < 20; i++) psxVram[1][i] = hull[i / 4];
    GpuVramChanged(0, 1, 20, 1);
    GpuLightSet lights = {.count = 1, .lights = {{{0, 0, 800}, 700, {1, .03f, .01f}, 3}}};
    GpuDrawState draw = {.textured = 1, .depth = 2, .semi = -1, .pointLights = &lights};
    uint8_t reference[64 * 32 * 4];
    for (int filter = 0; filter <= 1; filter++) {
        portSettings.render.modern.bilinear = filter;
        portSettings.render.modern.weaponLights = 0; RenderCommit();
        GpuClearArea(0, 0, 320, 240, 0, 0, 0); LitSurface(draw, 0);
        memcpy(reference, GpuReadPicture(0, 0, 64, 32, 0, 64, 32), sizeof(reference));
        portSettings.render.modern.weaponLights = 1; RenderCommit();
        GpuClearArea(0, 0, 320, 240, 0, 0, 0); LitSurface(draw, 0);
        const uint8_t *pixels = GpuReadPicture(0, 0, 64, 32, 0, 64, 32);
        int centre = (16 * 64 + 32) * 4, edge = (16 * 64) * 4;
        assert(pixels[centre] > reference[centre] + 50);
        assert(pixels[centre] > pixels[centre + 2] + 50);
        assert(pixels[edge] < pixels[centre] - 40); /* finite radius and smooth falloff */
        assert(abs(pixels[edge] - reference[edge]) <= 1);
        /* A light behind the surface contributes nothing, despite being nearby. */
        lights.lights[0].position[2] = 1200;
        GpuClearArea(0, 0, 320, 240, 0, 0, 0); LitSurface(draw, 0);
        pixels = GpuReadPicture(0, 0, 64, 32, 0, 64, 32);
        assert(abs(pixels[centre] - reference[centre]) <= 1);
        lights.lights[0].position[2] = 800;
        /* HUD/sky provenance is excluded, as are unlit material and shadow/effect blends. */
        for (int kind = 1; kind <= 5; kind++) {
            GpuDrawState excluded = draw;
            if (kind == 3) excluded.pointLights = NULL;
            if (kind == 4) excluded.semi = 2;
            if (kind == 5) excluded.raw = 1;
            GpuClearArea(0, 0, 320, 240, 0, 0, 0); LitSurface(excluded, kind);
            pixels = GpuReadPicture(0, 0, 64, 32, 0, 64, 32);
            assert(abs(pixels[centre] - pixels[centre + 2]) <= 1);
        }
        /* In complete darkness, the shield alone reveals the paint without filling black texels. */
        GpuLightSet empty = {0};
        GpuDrawState shielded = draw;
        shielded.pointLights = &empty;
        shielded.pageY = 1;
        shielded.shieldGlow[1] = .32f;
        shielded.shieldGlow[2] = shielded.shieldGlow[3] = 1.0f;
        const int darkPaint = (16 * 64 + 19) * 4, rim = (16 * 64 + 6) * 4;
        const int black = (16 * 64 + 44) * 4, yellow = (16 * 64 + 57) * 4;
        GpuClearArea(0, 0, 320, 240, 0, 0, 0); LitSurface(shielded, DARK_HULL);
        pixels = GpuReadPicture(0, 0, 64, 32, 0, 64, 32);
        assert(pixels[centre + 2] > pixels[centre] + 25);
        assert(pixels[rim + 2] > pixels[centre + 2]);
        assert(pixels[centre + 2] > pixels[darkPaint + 2] + 60 && pixels[darkPaint + 2] > 10);
        assert(pixels[black] == 0 && pixels[black + 1] == 0 && pixels[black + 2] == 0);
        assert(pixels[yellow] > 60 && pixels[yellow + 1] > 60 && pixels[yellow + 2] == 0);
        shielded.shieldGlow[0] = 1.0f;
        shielded.shieldGlow[1] = shielded.shieldGlow[2] = 0.0f;
        GpuClearArea(0, 0, 320, 240, 0, 0, 0); LitSurface(shielded, DARK_HULL);
        pixels = GpuReadPicture(0, 0, 64, 32, 0, 64, 32);
        assert(pixels[centre] > pixels[centre + 2] + 25);
        portSettings.render.modern.weaponLights = 0; RenderCommit();
        GpuClearArea(0, 0, 320, 240, 0, 0, 0); LitSurface(shielded, DARK_HULL);
        pixels = GpuReadPicture(0, 0, 64, 32, 0, 64, 32);
        assert(pixels[centre] == 0 && pixels[centre + 1] == 0 && pixels[centre + 2] == 0);
        portSettings.render.modern.weaponLights = 1; RenderCommit();
    }
    /* Bloom and lights have independent switches; overbright light enters HDR,
       and raw VRAM reads remain bounded SDR. */
    lights.lights[0].intensity = 100;
    lights.lights[0].radius = 1800;
    for (int bloom = 0; bloom <= 1; bloom++) {
        portSettings.render.modern.bloom = bloom; RenderCommit();
        GpuClearArea(0, 0, 320, 240, 0, 0, 0); LitSurface(draw, 0);
        const uint8_t *pixels = GpuReadPicture(0, 0, 80, 32, 0, 80, 32);
        int outside = (16 * 80 + 65) * 4;
        if (!bloom) assert(pixels[outside] == 0);
        else assert(pixels[outside] > 0);
        assert(sceneHdr.bands);
        GpuReadBack(32, 16, 1, 1); assert((psxVram[16][32] & 31) == 31);
    }
    /* Falling back to SDR still lights surfaces. */
    DeleteHdr(&sceneHdr); sceneHdr.unavailable = 1;
    GpuClearArea(0, 0, 320, 240, 0, 0, 0); LitSurface(draw, 0);
    const uint8_t *pixels = GpuReadPicture(0, 0, 64, 32, 0, 64, 32);
    assert(pixels[(16 * 64 + 32) * 4] == 255);
    /* Other profiles ignore the same source metadata. */
    portSettings.render.selection = RENDER_PC; RenderCommit();
    GpuClearArea(0, 0, 320, 240, 0, 0, 0); LitSurface(draw, 0);
    pixels = GpuReadPicture(0, 0, 64, 32, 0, 64, 32);
    int centre = (16 * 64 + 32) * 4;
    assert(pixels[centre] == pixels[centre + 2]);
    assert(glGetError() == GL_NO_ERROR);
    GpuRendererShutdown();
    glDeleteFramebuffers(1, &framebuffer); glDeleteTextures(1, &colour); glDeleteRenderbuffers(1, &depthBuffer);
    puts("Modern dynamic lights: diffuse falloff, shield-lit hull textures in darkness and hit tint, filtered/nearest, HUD/sky/shadow exclusion, bloom independence, HDR/SDR and profile isolation: OK");
}

static void MotionQuad(const GpuDrawState *draw, int x, int y, int w, int h, float depth, int red, int green, float dy)
{
    GpuVertex v[4];
    for (int i = 0; i < 4; i++) {
        v[i] = (GpuVertex){.x = x + (i & 1) * w, .y = y + (i >> 1) * h, .w = depth,
                           .r = red, .g = green, .b = green, .a = 255,
                           .view = {0, 0, depth}, .shutterDelta = {0, dy, 0}};
    }
    GpuTriangles(draw, v, 3);
    GpuTriangles(draw, v + 1, 3);
}

static void ModernMotionResources(void)
{
    wideMargin = areaWide = wideDirty = 0;
    portPal = 0;
    scale = rasterX = rasterY = 1;
    assert(CreateFramebuffer());
    ResetRenderSettings();
    portSettings.render.selection = RENDER_MODERN;
    RenderCommit();
    PortWideScene();
    GpuSetDrawArea(0, 0, 320, 240, 0, 0);
    const GpuDrawState solid = {.semi = -1};
    /* A fresh linked or traced race must never allocate the velocity target. */
    for (int state = 0; state < 2; state++) {
        testLinkRole = state == 0 ? LINK_MASTER : LINK_NONE;
        testTracing = state == 1;
        GpuClearArea(0, 0, 320, 240, 0, 0, 0);
        MotionQuad(&solid, 20, 20, 40, 40, 100, 200, 100, 0);
        GpuFlush();
        assert(sceneHdr.framebuffer && !sceneHdr.velocity);
    }
    testTracing = 0;
    /* Check actual draw-buffer routing across opt-out, link, trace and re-enable. */
    for (int state = 0; state < 5; state++) {
        portSettings.render.modern.cameraBlur = state != 1;
        testLinkRole = state == 2 ? LINK_MASTER : LINK_NONE;
        testTracing = state == 3;
        RenderCommit();
        GpuClearArea(0, 0, 320, 240, 0, 0, 0);
        MotionQuad(&solid, 20, 20, 40, 40, 100, 200, 100, 0);
        GpuFlush();
        int enabled = state == 0 || state == 4;
        GLint buffer;
        glBindFramebuffer(GL_DRAW_FRAMEBUFFER, sceneHdr.drawFramebuffer);
        glGetIntegerv(GL_DRAW_BUFFER3, &buffer);
        assert(buffer == (enabled ? GL_COLOR_ATTACHMENT3 : GL_NONE));
        glBindFramebuffer(GL_DRAW_FRAMEBUFFER, sceneHdr.framebuffer);
        glGetIntegerv(GL_DRAW_BUFFER2, &buffer);
        assert(buffer == (enabled ? GL_COLOR_ATTACHMENT2 : GL_NONE));
        assert(sceneHdr.velocity && sceneHdr.velocityActive == enabled);
        assert(glGetError() == GL_NO_ERROR);
    }
    GpuRendererShutdown();
    glDeleteFramebuffers(1, &framebuffer);
    glDeleteTextures(1, &colour);
    glDeleteRenderbuffers(1, &depthBuffer);
    puts("Modern motion resources: link/trace allocation bypass and blur toggle draw-buffer routing: OK");
}

static void ModernCameraBlur(int renderScale)
{
    wideMargin = areaWide = wideDirty = 0;
    portPal = 0;
    scale = renderScale; rasterX = rasterY = renderScale;
    assert(CreateFramebuffer());
    ResetRenderSettings();
    portSettings.render.selection = RENDER_MODERN;
    portSettings.render.modern.resample = 0;
    portSettings.render.modern.weaponLights = 0;
    portSettings.render.modern.tonemapper = MODERN_TONEMAP_ORIGINAL;
    RenderCommit();
    PortWideScene();
    const GpuDrawState solid = {.semi = -1}, additive = {.semi = 1};
    const GpuDrawState hull = {.semi = -1, .hullMotion = 1};
    const GpuCameraMotion motion = {
        .earlier = {{1, 0, 0, -10}, {0, 1, 0, 0}, {0, 0, 1, 0}},
        .centreX = 160, .focal = 160
    };
    static uint8_t sharp[352 * 240 * 4], blurred[352 * 240 * 4];

    for (int wide = 0; wide <= 1; wide++) {
        int margin = wide * 16, width = 320 + margin * 2, y = wide * 256;
        size_t bytes = (size_t)width * 240 * 4;
        SetWideMargin(margin);
        portSettings.render.modern.bloom = wide;
        RenderCommit();
        GpuSetDrawArea(0, y, 320, 240, 0, y);
        GpuClearArea(0, y, 320, 240, 0, 0, 0);
        MotionQuad(&solid, -margin, y + 16, width, 64, 100, 32, 32, 0);
        MotionQuad(&solid, 20, y + 16, 4, 64, 100, 200, 200, 0);
        /* Two hulls at the same depth. Their contrasting stripes should
           stay sharp at zero screen motion and streak during lateral motion. */
        MotionQuad(&hull, 112, y + 16, 16, 64, 60, 64, 20, 0);
        MotionQuad(&hull, 117, y + 16, 4, 64, 60, 220, 30, 0);
        MotionQuad(&hull, 140, y + 16, 16, 64, 60, 64, 20, -10);
        MotionQuad(&hull, 140, y + 41, 16, 4, 60, 220, 30, -10);
        /* The points have identical SDR values. Only the right point has
           accumulated radiance above display white. */
        MotionQuad(&solid, 190, y + 44, 1, 1, 80, 255, 255, 0);
        MotionQuad(&solid, 220, y + 44, 1, 1, 80, 255, 255, 0);
        for (int i = 0; i < 3; i++)
            MotionQuad(&additive, 220, y + 44, 1, 1, 80, 255, 255, 0);
        MotionQuad(&solid, -margin, y + 80, width, 64, 6400, 32, 32, 0);
        MotionQuad(&solid, 20, y + 80, 4, 64, 6400, 200, 200, 0);
        GpuCameraMotion view = motion;
        view.centreY = y + 120;
        GpuCaptureCameraMotion(0, y, 320, 240, &view, 0);

        /* The second table clears depth. Keep the scene snapshot across HUD draws. */
        GpuClearDepth(0, y, 320, 240, 0);
        for (int mode = -1; mode <= 3; mode++) {
            GpuDrawState hud = {.semi = mode};
            MotionQuad(&hud, 40 + 12 * (mode + 1), y + 16, 4, 64, 0, 120, 0, 0);
        }
        GpuCaptureCameraMotion(0, y, 320, 240, NULL, 1);
        const uint8_t *pixels = GpuReadPicture(0, y, 320, 240, margin, width, 240);
        memcpy(blurred, pixels, bytes);
        assert(!memcmp(blurred, GpuReadPicture(0, y, 320, 240, margin, width, 240), bytes));

        /* A cut can reuse the texture and must still match the opt-out exactly. */
        pictureOwners[wide].render.settings.modern.cameraBlur = 0;
        memcpy(sharp, GpuReadPicture(0, y, 320, 240, margin, width, 240), bytes);
        ModernRenderSettings *profile = &pictureOwners[wide].render.settings.modern;
        profile->cameraBlur = 1;
        profile->cameraBlurAmount = 0;
        assert(!memcmp(sharp, GpuReadPicture(0, y, 320, 240, margin, width, 240), bytes));

        /* The default reaches two pixels into the track. At 50%, it reaches one. */
        int outerEdge = (40 * width + margin + 18) * 4;
        assert(blurred[outerEdge] > sharp[outerEdge]);
        profile->cameraBlurAmount = 50;
        assert(GpuReadPicture(0, y, 320, 240, margin, width, 240)[outerEdge] == sharp[outerEdge]);
        profile->cameraBlurAmount = MODERN_BLUR_DEFAULT;
        assert(!memcmp(blurred, GpuReadPicture(0, y, 320, 240, margin, width, 240), bytes));

        int edge = (40 * width + margin + 19) * 4;
        assert(blurred[edge] > sharp[edge]);
        int hullRegion = (40 * width + margin + 108) * 4;
        assert(!memcmp(blurred + hullRegion, sharp + hullRegion, 24 * 4));
        int movingStripeEdge = (39 * width + margin + 148) * 4;
        assert(blurred[movingStripeEdge] > sharp[movingStripeEdge]);
        int outsideSide = (30 * width + margin + 139) * 4;
        assert(blurred[outsideSide] == sharp[outsideSide]); /* perpendicular to the hull sweep */
        int outsideFront = (14 * width + margin + 148) * 4;
        assert(blurred[outsideFront] > sharp[outsideFront]); /* outside the original polygon, over sky */
        assert(!memcmp(blurred + 90 * width * 4, sharp + 90 * width * 4, 150 * width * 4));
        if (!wide) { /* bloom is off, so this difference comes from radiance in the motion gather */
            int point = (44 * width + 190) * 4, streak = point - 2 * 4;
            assert(sharp[point] == 255 && sharp[point + 30 * 4] == 255);
            assert(blurred[streak] > sharp[streak]);
            assert(blurred[streak + 30 * 4] > blurred[streak] + 32);
            profile->weaponLights = 1; /* enable the HDR tone mapper, still with bloom off */
            const uint8_t *mapped = GpuReadPicture(0, y, 320, 240, margin, width, 240);
            assert(mapped[streak + 30 * 4] > mapped[streak] + 32);
            profile->weaponLights = 0;
        }
        for (int mode = -1; mode <= 3; mode++) {
            int x = 40 + 12 * (mode + 1) + margin;
            assert(!memcmp(blurred + (40 * width + x - 2) * 4,
                           sharp + (40 * width + x - 2) * 4, 8 * 4));
        }
        GpuCaptureCameraMotion(0, y, 320, 240, NULL, 0);
        assert(!memcmp(sharp, GpuReadPicture(0, y, 320, 240, margin, width, 240), bytes));
        assert(glGetError() == GL_NO_ERROR);
    }
    SetWideMargin(0);
    GpuRendererShutdown();
    glDeleteFramebuffers(1, &framebuffer);
    glDeleteTextures(1, &colour);
    glDeleteRenderbuffers(1, &depthBuffer);
    printf("Modern camera blur at %dx: silhouette trails, HDR streaks, strength, stationary hull and HUD, cuts and widescreen: OK\n", renderScale);
}

int main(void)
{
    ResetRenderSettings();
    assert(SDL_Init(SDL_INIT_VIDEO));
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 3);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 3);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_CORE);
    SDL_Window *window = SDL_CreateWindow("N64 shader regression", 16, 16, SDL_WINDOW_OPENGL | SDL_WINDOW_HIDDEN);
    if (!window) { fprintf(stderr, "%s\n", SDL_GetError()); return 1; }
    SDL_GLContext context = SDL_GL_CreateContext(window);
    assert(context && SDL_GL_MakeCurrent(window, context) && RenderGlLoad());
    isProgram = (PFNGLISPROGRAMPROC)SDL_GL_GetProcAddress("glIsProgram");
    assert(isProgram);
    UsePipeline(RENDER_PS1);
    glUseProgram(scene->program);
    glUniform2f(glGetUniformLocation(scene->program, "target"), 1, 1);
    glUniform2f(glGetUniformLocation(scene->program, "origin"), 0, 0);
    glUniform1f(glGetUniformLocation(scene->program, "depthPull"), 1);
    glUniform1i(glGetUniformLocation(scene->program, "vram"), 0);
    glUniform1i(glGetUniformLocation(scene->program, "glyphs"), 1);
    testAtlasUnit = 1; glUniform1i(glGetUniformLocation(scene->program, "hdAtlas"), 1);
    glUniform1i(glGetUniformLocation(scene->program, "portPages"), 1);
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
    glVertexAttribIPointer(9, 4, GL_SHORT, sizeof(BatchVertex), (void *)offsetof(BatchVertex, emissionUv));
    glVertexAttribPointer(10, 4, GL_FLOAT, GL_FALSE, sizeof(BatchVertex), (void *)offsetof(BatchVertex, view));
    glVertexAttribPointer(11, 3, GL_FLOAT, GL_FALSE, sizeof(BatchVertex), (void *)offsetof(BatchVertex, shutterDelta));
    for (int i = 0; i < 12; i++) glEnableVertexAttribArray((GLuint)i);

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
    BilinearSquare(0);
    Render(RENDER_PS1, 0, 0, .5f, .5f, 128, -1, 0, full, 0, 0, 1); /* PS1 remains nearest */
    Render(RENDER_PS1, 0, 0, 1.5f, .5f, 128, -1, 0, 0, full, 0, 1); /* second 4-bit index */
    Render(RENDER_PS1, 0, 0, .5f, .5f, 255, -1, 0, 1, 0, 0, 1); /* PS1 overbright modulation retained */
    Render(RENDER_N64, 1, 0, .5f, .5f, PortN64Modulation(255), -1, 0, 0, half, half, 1);
    Render(RENDER_N64, 1, 0, .5f, .5f, PortN64Modulation(128), -1, 0, 0, half / 2, half / 2, 1);
    Render(RENDER_N64, 1, 0, 1.75f, 0, 128, -1, 0, 0, full, 0, 1); /* no neighbor bleed */
    Render(RENDER_N64, 1, 0, -.25f, -.25f, 128, -1, 0, full, 0, 0, 1); /* clamp before negative wrap */
    words[0] = 0x0201; words[1024] = 0x0403; Upload();
    ThreePointSquare(1);
    BilinearSquare(1);
    Render(RENDER_PS1, 0, 1, .5f, .5f, 128, -1, 0, full, 0, 0, 1);
    Render(RENDER_PS1, 0, 1, 1.5f, .5f, 128, -1, 0, 0, full, 0, 1); /* second 8-bit index */
    words[0] = 31; words[1] = 31 << 5; words[1024] = 31 << 10; words[1025] = 0x7fff; Upload();
    ThreePointSquare(2);
    BilinearSquare(2);
    Interpolation();
    testShade = SHADE_WORLD_TINT | SHADE_AFFINE_COLOUR;
    Render(RENDER_PC, 2, 2, .5f, .5f, 255, -1, 0, full, 0, 0, 1);
    Render(RENDER_PC, 2, 2, .5f, .5f, 128, -1, 0, full * 128 / 255, 0, 0, 1);
    testShade = SHADE_PS1_COLOUR | SHADE_AFFINE_COLOUR | SHADE_AFFINE_UV;
    Render(RENDER_PS1, 0, 2, .5f, .5f, 100, -1, 0, 192.0f / 255, 0, 0, 1);
    testShade |= SHADE_DITHER;
    Render(RENDER_PS1, 0, 2, .5f, .5f, 100, -1, 0, 184.0f / 255, 0, 0, 1);
    testShade = 0;
    Render(RENDER_PS1, 0, 2, .5f, .5f, 128, -1, 0, full, 0, 0, 1);
    Render(RENDER_PS1, 0, 2, 1.5f, .5f, 128, -1, 0, 0, full, 0, 1); /* direct colour */
    /* Only top-left is opaque: its triangular weight is 1-u-v, not (1-u)*(1-v). */
    words[0] = 0x01; words[1024] = 0; Upload();
    Render(RENDER_N64, 1, 0, .25f, .25f, 128, -1, 0, half, 0, 0, .5f);
    Render(RENDER_N64, 1, 0, .5f, .5f, 128, -1, 0, 0, 0, 0, 0);
    Render(RENDER_PC, 2, 0, 1, 1, 128, -1, 0, full / 4, 0, 0, .25f);
    Render(RENDER_PC, 2, 0, 1, 1, 255, -1, 0, .25f, 0, 0, .25f); /* no bright transparent fringe */
    Render(RENDER_PC, 2, 0, 1, 1, 128, -1, 3, 0, 0, 0, 0); /* partial coverage cannot write depth */
    words[0] = words[1024] = 0x01; Upload();
    Render(RENDER_PS1, 0, 0, 1, 0, 128, -1, 0, 0, 0, 0, 0); /* PS1 transparent texel */
    Render(RENDER_N64, 1, 0, .5f, .5f, 128, -1, 0, half, 0, 0, .5f); /* premultiplied transparent edge */
    Render(RENDER_N64, 1, 0, .5f, .5f, 128, -1, 3, 0, 0, 0, 0); /* edge must not occlude */
    words[0] = words[1024] = 0x21; palette[2] |= 0x8000; Upload();
    Render(RENDER_PS1, 0, 0, 1, 0, 128, 0, 0, 0, half, 0, .5f); /* PS1 semi-transparent texel */
    Render(RENDER_N64, 1, 0, .5f, .5f, 128, 0, 0, half, half / 2, 0, .75f);
    Render(RENDER_N64, 1, 0, .5f, .5f, 128, 1, 0, half, half, 0, .5f);
    Render(RENDER_N64, 1, 0, .5f, .5f, 128, 3, 0, half, half / 4, 0, .5f);
    Render(RENDER_N64, 1, 0, .5f, .5f, 128, 2, 1, half, 0, 0, .5f);
    Render(RENDER_N64, 1, 0, .5f, .5f, 128, 2, 2, 0, half, 0, 0);
    Render(RENDER_PC, 2, 0, 1, 1, 128, 0, 0, half, half / 2, 0, .75f);
    Render(RENDER_PC, 2, 0, 1, 1, 128, 1, 0, half, half, 0, .5f);
    Render(RENDER_PC, 2, 0, 1, 1, 128, 3, 0, half, half / 4, 0, .5f);
    Render(RENDER_PC, 2, 0, 1, 1, 128, 2, 1, half, 0, 0, .5f);
    Render(RENDER_PC, 2, 0, 1, 1, 128, 2, 2, 0, half, 0, 0);
    /* Saturn uses undithered RGB555 and replaces world translucency with native-pixel mesh. */
    testShade = SHADE_PS1_COLOUR | SHADE_AFFINE_COLOUR | SHADE_AFFINE_UV;
    Render(RENDER_SATURN, 3, 0, 1, 0, 128, 0, 0, 0, full, 0, 1);
    Render(RENDER_SATURN, 3, 0, 1, 0, 128, 0, 1, 0, 0, 0, 0); /* opaque pass excludes semi */
    Render(RENDER_SATURN, 3, 0, 1, 0, 128, 0, 2, 0, full, 0, 1);
    testPixelX = 1;
    Render(RENDER_SATURN, 3, 0, 1, 0, 128, 0, 2, 0, 0, 0, 0); /* alternate native pixel */
    Render(RENDER_SATURN, 3, 0, 0, 0, 128, 0, 1, full, 0, 0, 1); /* opaque texel survives */
    Render(RENDER_SATURN, 3, 0, 1, 0, 128, 1, 0, 0, 0, 0, 0); /* additive mesh gap */
    Render(RENDER_SATURN, 3, 0, 1, 0, 128, 3, 0, 0, 0, 0, 0); /* quarter-additive mesh gap */
    Render(RENDER_SATURN, 3, 0, 1, 0, 0, 0, 0, 0, 0, 0, 0); /* ship shadow mesh gap */
    testPixelX = 0;
    Render(RENDER_SATURN, PackedSampling((RenderStyle){.coverage = RENDER_COVERAGE_HALF}), 0, 1, 0, 128, 0, 0, 0, half, 0, .5f);
    Render(RENDER_SATURN, PackedSampling((RenderStyle){.coverage = RENDER_COVERAGE_HALF}), 0, 1, 0, 128, 3, 0, 0, full / 8, 0, 0);
    Render(RENDER_SATURN, PackedSampling((RenderStyle){.coverage = RENDER_COVERAGE_HALF}), 0, 1, 0, 0, 0, 0, 0, 0, 0, .5f); /* ship shadow alpha */
    Render(RENDER_SATURN, PackedSampling((RenderStyle){.coverage = RENDER_COVERAGE_HALF}), 0, 0, 0, 128, 1, 0, full, 0, 0, 1); /* opaque texel */
    Render(RENDER_SATURN, 3, 0, 1, 0, 128, 1, 0, 0, full, 0, 0); /* additive mesh dot */
    Render(RENDER_SATURN, 3, 0, 1, 0, 128, 3, 0, 0, full / 4, 0, 0); /* quarter-additive mesh dot */
    Render(RENDER_SATURN, 3, 0, 1, 0, 0, 1, 0, 0, 0, 0, 0); /* black additive sample contributes nothing */
    Render(RENDER_SATURN, 3, 0, 1, 0, 0, 0, 0, 0, 0, 0, 1); /* ship shadow mesh dot */
    Render(RENDER_SATURN, 3, 0, 1, 0, 128, 2, 2, 0, full, 0, 0); /* other subtractive effects */
    Render(RENDER_SATURN, 3, 0, 1, 0, 128, 0, 3, 0, 0, 0, 0); /* semi does not write depth */
    Render(RENDER_SATURN, 0, 0, 1, 0, 100, -1, 0, 0, 192.0f / 255, 0, 1);
    testShade = 0;
    palette[2] &= 0x7fff; words[63] = words[1024 + 63] = 0x2100;
    bounds[0] = 254; bounds[2] = 255; Upload();
    Render(RENDER_N64, 1, 0, 255.5f, 0, 128, -1, 0, 0, full, 0, 1); /* clamp before page wrap */
    Render(RENDER_PS1, 0, 0, 256.5f, 0, 128, -1, 0, full, 0, 0, 1); /* PS1 wraps rather than clamps */
    Render(RENDER_PS1, 0, 0, -.25f, 0, 128, -1, 0, 0, full, 0, 1);
    Render(RENDER_PC, 2, 0, 256, .5f, 128, -1, 0, half, half, 0, 1); /* taps wrap independently */
    Render(RENDER_PC, 2, 0, 0, .5f, 128, -1, 0, half, half, 0, 1);
    /* A texture window forces bit 3 of each tap; filtering must resolve those texels first. */
    words[2] = words[1026] = 0x21; Upload();
    testWindow[0] = testWindow[2] = 1;
    Render(RENDER_PC, 2, 0, 1, 1, 128, -1, 0, half, half, 0, 1);
    memset(testWindow, 0, sizeof(testWindow));
    bounds[0] = 0; bounds[2] = 1;
    HdFiltering();
    GlyphUpload();
    FullColourProfiles();
    N64Materials();
    N64GameplayLight();
    N64SaturatedLight();
    DepthBatches(fbo, output, dummy);
    NativeSaturn();
    SaturnGpuReference();
    N64SharedMaterials();
    WorldDithering();
    MainMenuColour();
    N64RenderSettings noVi = {.threePoint = 1, .dither = 1, .dedither = 1, .resample = 1};
    assert(!n64ViProgram && !TestN64Picture(0, 3, 5, 13, 11, &noVi, scale));
    assert(!n64ViProgram && !n64ViTexture[0] && !n64ViTexture[1]);
    ViFiltering();
    ViPasses();
    ViResampling();
    SaturnPresentation();
    RendererLifecycle();
    ScreenBufferOwnership();
    PcRgb565();
    ProfileNativeResolution();
    ModernHdr();
    ModernWeaponLights();
    ModernMotionResources();
    ModernCameraBlur(1);
    ModernCameraBlur(3);
    SDL_GL_DestroyContext(context); SDL_DestroyWindow(window); SDL_Quit();
    puts("Production GL shaders: N64 three-point, optional bilinear native/HD, bounds/wrapping/windows, coverage/blend passes and default PS1 sampling: OK");
    return 0;
}
