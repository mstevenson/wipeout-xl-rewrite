/* Shared OpenGL transport for independent renderer implementations.

   This module owns texture uploads, vertex batches and scene framebuffers, using
   the game's video-memory coordinate system. Renderer-owned programs implement
   rasterization; renderer-owned presentation callbacks process completed pictures.
   No renderer's shaders or private resources live here.

   Widescreen (the settings): the screen's columns of video memory (x 0 to
   320, every row: both of the game's screen buffers) are drawn into a
   framebuffer of their own that reaches wideMargin console pixels beyond
   either side, so that what the game projects past the screen's edges is
   seen; its middle is the console's picture. Untextured primitives across
   the whole screen (clears, fades, flashes) are stretched over the sides;
   pictures keep their shape. A menu is shown wide too, but only its
   backdrop, enlarged evenly, and those fills reach the sides: the rest
   stays in the console's columns, where panels sliding in from off screen
   come into view as on the console. Whenever video memory's copy of those columns
   is read or shown 4:3 it is brought up to date first (SyncWide), and what
   the CPU writes there goes to both.

   The scale (the settings' resolution, or as many pixels as the window shows) can change while the game
   runs: the framebuffers are made again at the new scale from what they held (SetScale).

   HD textures (the player's texture pack, hdtex.c): a textured primitive that the pack has an image for
   carries where that image is in the atlas (an RGBA texture of the images in use, at the framebuffer's scale)
   and which texels of its page it covers; the shader samples the image there instead of video memory. */
#include <SDL3/SDL.h>
#include "../port_gl.h"
#include <stddef.h>
#include <stdlib.h>
#include <string.h>

#include "../psx/gpu.h"
#include "renderer.h"
#include "../n64_material.h"
#include "../hdtex.h"
#include "../link.h"
#include "../platform.h"
#include "../settings.h"

#include "gl_pipeline.h"
#include "saturn_native.h"
#include <math.h>

/* ---- state ---- */

static int scale = 1; /* multiplier of the active profile's native resolution */
static float rasterX = 1, rasterY = 1; /* native pixels per game-coordinate unit */
static float ScaleX(void) { return rasterX * scale; }
static float ScaleY(void) { return rasterY * scale; }
static int PixelX(float x) { return (int)SDL_lroundf(x * ScaleX()); }
static int PixelY(float y) { return (int)SDL_lroundf(y * ScaleY()); }
static float TargetWidth(int width) { return PixelX(width) / ScaleX(); }
static float TargetHeight(int height) { return PixelY(height) / ScaleY(); }
static float WideOrigin(int margin) { return -PixelX(margin) / ScaleX(); }

void GpuPixelSize(int w, int h, int *pixelW, int *pixelH)
{
    *pixelW = PixelX(w);
    *pixelH = PixelY(h);
}
static GLuint framebuffer, colour;   /* all of video memory, scaled */
static GLuint depthBuffer;           /* ... its depth (the depth buffer: Flush) */
static GLuint vramTexture;           /* the CPU copy, R16UI */
static GLuint glyphTexture;          /* the glyph page (GpuSetGlyphMemory): RGBA, premultiplied, filtered */
static GLuint portPagesTexture;      /* the port's other pages (GpuSetPortPage), side by side: RGBA, filtered */
static GLuint hdAtlas;               /* HD textures: the images in use (HdPlaced) */
static GLuint vertexArray, vertexBuffer;
static RenderProgram *scene;
static const RenderPipeline *pipeline, *presentPipeline;
static uint64_t pictureRevision[2], nextPictureRevision;
static int vramDirty = 1;
static GLuint windowFramebuffer;     /* the window's own: 0, but on iOS SDL's view has one of its own */
static int dirtyLeft, dirtyTop, dirtyRight, dirtyBottom;
static int areaX, areaY, areaW = VRAM_WIDTH, areaH = VRAM_HEIGHT;
static int initialised;

#define SCREEN_COLUMNS 320
#define MAX_WIDE_MARGIN 400
static int wideMargin;                         /* console pixels beyond each side; 0: no wide framebuffer */
static GLuint wideFramebuffer, wideColour, wideDepth;
/* The HDR draw FBO shares SDR colour and depth, keeping the draw buffer
   configuration stable across batches. The auxiliary FBO seeds and copies HDR. */
typedef struct SceneHdr {
    GLuint textures[2], framebuffer, drawFramebuffer;
    unsigned bands;
    int unavailable;
} SceneHdr;
static SceneHdr sceneHdr, wideHdr;
static const GLenum hdrDrawBuffers[] = {GL_COLOR_ATTACHMENT0, GL_COLOR_ATTACHMENT1, GL_COLOR_ATTACHMENT2};
static int wideDirty;                          /* drawn into since its middle was copied to video memory's */
static int wideHdrDirty;                       /* HDR needs its own copy only for narrow presentation */
static int areaWide;                           /* the draw area is in the screen's columns: drawn wide */
static int sceneWide;                          /* the game built a race's scene since the last new picture */
static int drawnSincePresent;                  /* the game drew anything since the last picture shown */
static int interpolatingDraw;                  /* libgpu draws a picture between two of the game's frames */
static int picturesWithoutScene = 1000;        /* new pictures in a row that were not a race's */
static int menuWide;                           /* the game built a menu's backdrop since the last new picture */
static int picturesWithoutMenu = 1000;         /* ... and new pictures in a row that were not a menu's */
static int backdropNext;                       /* the primitive being drawn is a menu's backdrop (GpuBackdrop) */

/* A batch of triangles drawn with one blending state. What else a primitive's draw state says
   (texture page, palette, texture window, colour depth) goes with each vertex, so that primitives of
   different textures, which the ordering tables interleave, are drawn together. */
#define BATCH_VERTICES 8192
typedef struct BatchVertex {
    float x, y, w;
    float r, g, b, a;
    float u, v;
    int16_t texture[4]; /* page x, page y, palette x, palette y */
    uint8_t window[4];  /* texture window: mask x, mask y, offset x, offset y */
    uint8_t mode[4];    /* textured, shading flags (below), colour depth, filter/blend flags */
    uint16_t hd[4];     /* HD texture: its part of the atlas, x, y, w, h (w 0: none) */
    int16_t filterBounds[4]; /* full image's left/top/right/bottom texels; prevents sampling adjacent images */
    int16_t hdTexels[4]; /* ... the texels of the page it covers, x, y, w, h */
    int16_t emissionUv[4]; /* source emission centre and half-size; width zero means whole face */
} BatchVertex;
static BatchVertex batch[BATCH_VERTICES];
static int primitiveFilterBounds[4];
static int batchCount, batchPoints;
#define SATURN_MARGIN (MAX_WIDE_MARGIN * 11 / 10)
#define SATURN_WIDTH (352 + 2*SATURN_MARGIN)
static uint8_t saturnPixels[VRAM_HEIGHT][SATURN_WIDTH][4];
static int saturnLeft=SATURN_WIDTH, saturnTop=VRAM_HEIGHT, saturnRight, saturnBottom;
static void SaturnFlushPixels(void);
static void SaturnFlushGpu(void);
/* The console's two screen buffers occupy rows 0..255 and 256..511.
   Drawing the next buffer must not change presentation of the previous one. */
typedef struct PictureOwner {
    RenderFrame render;
    int valid, race;
} PictureOwner;
static PictureOwner pictureOwners[2];
static float batchLeft, batchTop, batchRight, batchBottom;

/* Both screen revisions share a clock, so a crop spanning them can use the newer one.
   Track all columns: readback can also request rectangles outside the screen area. */
static void PictureChanged(float y, float h)
{
    if (h <= 0) return;
    uint64_t revision = ++nextPictureRevision;
    for (int i = 0; i < 2; i++) {
        if (y < (i + 1) * 256 && y + h > i * 256) pictureRevision[i] = revision;
    }
}

static void RememberPicture(float x, float y, float w, float h)
{
    if (w <= 0 || h <= 0 || x >= SCREEN_COLUMNS + wideMargin || x + w <= -wideMargin) return;
    for (int i = 0; i < 2; i++) {
        if (y < (i + 1) * 256 && y + h > i * 256) {
            pictureOwners[i] = (PictureOwner){*RenderCurrent(), 1, GpuRaceWide()};
        }
    }
}
static GpuDrawState batchState;
static int batchDepth; /* at least one polygon needs depth writes to hide surfaces behind it */

/* The largest scale: the wide framebuffer at its widest within the GPU's textures, and at most 12 (the
   framebuffers then take some 560 MB). */
int GpuMaxScale(RendererId id)
{
    GLint size = 4096;
    int width, height;
    RenderNativeSize(id, portPal, &width, &height);
    glGetIntegerv(GL_MAX_TEXTURE_SIZE, &size);
    float x = width / 320.0f, y = height / (float)GPU_LINES;
    int pixels = (int)SDL_ceilf(SDL_max((SCREEN_COLUMNS + 2 * MAX_WIDE_MARGIN) * x, VRAM_HEIGHT * y));
    int budget = (int)(RENDER_MAX_SCALE / SDL_max(x, y));
    return SDL_clamp(size / pixels, 1, budget);
}

int GpuWindowScale(RendererId id)
{
    int width, height, nativeW, nativeH;
    PortDrawableSize(&width, &height);
    RenderNativeSize(id, portPal, &nativeW, &nativeH);
    if (!portSettings.widescreen) height = SDL_min(height, width * 3 / 4);
    int maximum = SDL_min(8, GpuMaxScale(id));
#ifdef PORT_MOBILE
    maximum = SDL_min(maximum, 3);
#endif
    return SDL_clamp((height + nativeH - 1) / nativeH, 1, maximum);
}

static int ChooseScale(void)
{
    const RenderFrame *frame = RenderCurrent();
    int wanted = RenderScale(frame);
    return wanted > 0 ? SDL_min(wanted, GpuMaxScale(frame->id)) : GpuWindowScale(frame->id);
}

int GpuScale(void)
{
    return scale;
}

/* Video memory's framebuffer at the scale. */
/* A depth buffer of that size, attached to the framebuffer bound, cleared. */
static GLuint DepthFor(int width, int height)
{
    GLuint depth;

    glGenRenderbuffers(1, &depth);
    glBindRenderbuffer(GL_RENDERBUFFER, depth);
    glRenderbufferStorage(GL_RENDERBUFFER, GL_DEPTH_COMPONENT24, width, height);
    glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_RENDERBUFFER, depth);
    glDisable(GL_SCISSOR_TEST);
    glDepthMask(GL_TRUE);
    glClear(GL_DEPTH_BUFFER_BIT);
    return depth;
}

static int CreateFramebuffer(void)
{
    PictureChanged(0, VRAM_HEIGHT);
    glGenTextures(1, &colour);
    glBindTexture(GL_TEXTURE_2D, colour);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, PixelX(VRAM_WIDTH), PixelY(VRAM_HEIGHT), 0, GL_RGBA, GL_UNSIGNED_BYTE,
                 NULL);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glGenFramebuffers(1, &framebuffer);
    glBindFramebuffer(GL_FRAMEBUFFER, framebuffer);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, colour, 0);
    depthBuffer = DepthFor(PixelX(VRAM_WIDTH), PixelY(VRAM_HEIGHT));
    if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE) {
        SDL_Log("framebuffer incomplete at scale %d", scale);
        return 0;
    }
    return 1;
}

static void DeleteHdr(SceneHdr *hdr)
{
    if (hdr->drawFramebuffer) glDeleteFramebuffers(1, &hdr->drawFramebuffer);
    if (hdr->framebuffer) glDeleteFramebuffers(1, &hdr->framebuffer);
    if (hdr->textures[0] || hdr->textures[1]) glDeleteTextures(2, hdr->textures);
    *hdr = (SceneHdr){0};
}

static int CreateHdr(SceneHdr *hdr, GLuint sdr, GLuint depth, int width)
{
    if (hdr->unavailable) return 0;
    if (hdr->framebuffer) return 1;
    glActiveTexture(GL_TEXTURE0);
    glGenTextures(2, hdr->textures);
    glGenFramebuffers(1, &hdr->framebuffer);
    glBindFramebuffer(GL_DRAW_FRAMEBUFFER, hdr->framebuffer);
    for (int i = 0; i < 2; i++) {
        glBindTexture(GL_TEXTURE_2D, hdr->textures[i]);
        glTexImage2D(GL_TEXTURE_2D, 0, i ? GL_RG8 : GL_RGBA16F, width, PixelY(VRAM_HEIGHT),
                     0, i ? GL_RG : GL_RGBA, i ? GL_UNSIGNED_BYTE : GL_HALF_FLOAT, NULL);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
        glFramebufferTexture2D(GL_DRAW_FRAMEBUFFER, GL_COLOR_ATTACHMENT0 + i, GL_TEXTURE_2D, hdr->textures[i], 0);
    }
    glDrawBuffers(2, hdrDrawBuffers);
    if (glCheckFramebufferStatus(GL_DRAW_FRAMEBUFFER) == GL_FRAMEBUFFER_COMPLETE) {
        glGenFramebuffers(1, &hdr->drawFramebuffer);
        glBindFramebuffer(GL_DRAW_FRAMEBUFFER, hdr->drawFramebuffer);
        glFramebufferTexture2D(GL_DRAW_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, sdr, 0);
        for (int i = 0; i < 2; i++)
            glFramebufferTexture2D(GL_DRAW_FRAMEBUFFER, GL_COLOR_ATTACHMENT1 + i, GL_TEXTURE_2D, hdr->textures[i], 0);
        glFramebufferRenderbuffer(GL_DRAW_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_RENDERBUFFER, depth);
        glDrawBuffers(3, hdrDrawBuffers);
        if (glCheckFramebufferStatus(GL_DRAW_FRAMEBUFFER) == GL_FRAMEBUFFER_COMPLETE) return 1;
    }
    DeleteHdr(hdr);
    hdr->unavailable = 1;
    SDL_Log("HDR scene target unavailable at %dx%d", width, PixelY(VRAM_HEIGHT));
    return 0;
}

static unsigned HdrBands(float top, float bottom)
{
    return ((top < 256 && bottom > 0) ? 1u : 0u) |
           ((top < 512 && bottom > 256) ? 2u : 0u);
}

static void SeedHdrRect(const SceneHdr *hdr, GLuint sdr, int left, int top, int right, int bottom)
{
    if (hdr->bands & HdrBands(top, bottom))
        RenderGlSeedHdr(sdr, hdr->framebuffer, PixelX(left), PixelY(top),
                         PixelX(right) - PixelX(left), PixelY(bottom) - PixelY(top));
}

/* A solid background can clear the HDR attachments in the existing scissor. */
static void ClearHdr(const SceneHdr *hdr, int y, int h, const GLfloat sdr[4])
{
    if (!(hdr->bands & HdrBands(y, y + h))) return;
    const GLfloat mask[4] = {0};
    glBindFramebuffer(GL_DRAW_FRAMEBUFFER, hdr->framebuffer);
    glClearBufferfv(GL_COLOR, 0, sdr);
    glClearBufferfv(GL_COLOR, 1, mask);
}

static void SeedHdrBands(SceneHdr *hdr, GLuint sdr, unsigned bands, int width)
{
    for (int i = 0; i < 2; i++) {
        unsigned band = 1u << i;
        if ((bands & band) && !(hdr->bands & band)) {
            int y = PixelY(i * 256);
            RenderGlSeedHdr(sdr, hdr->framebuffer, 0, y, width, PixelY((i + 1) * 256) - y);
            hdr->bands |= band;
        }
    }
}

static void BlitHdr(const SceneHdr *src, const SceneHdr *dst, int x, int y, int right, int bottom,
                    int leftTo, int topTo, int rightTo, int bottomTo)
{
    const GLenum maskOnly[] = {GL_NONE, GL_COLOR_ATTACHMENT1};
    glDisable(GL_SCISSOR_TEST);
    glBindFramebuffer(GL_READ_FRAMEBUFFER, src->framebuffer);
    glBindFramebuffer(GL_DRAW_FRAMEBUFFER, dst->framebuffer);
    glDrawBuffers(1, hdrDrawBuffers);
    glBlitFramebuffer(x, y, right, bottom, leftTo, topTo, rightTo, bottomTo, GL_COLOR_BUFFER_BIT, GL_NEAREST);
    glReadBuffer(GL_COLOR_ATTACHMENT1);
    glDrawBuffers(2, maskOnly);
    glBlitFramebuffer(x, y, right, bottom, leftTo, topTo, rightTo, bottomTo, GL_COLOR_BUFFER_BIT, GL_NEAREST);
    glReadBuffer(GL_COLOR_ATTACHMENT0);
    glDrawBuffers(2, hdrDrawBuffers);
}

/* Preserve both display buffers when adding or removing widescreen columns. */
static void CopyHdrCentre(const SceneHdr *src, int from, SceneHdr *dst, int to,
                          GLuint sdr, GLuint depth, int width)
{
    dst->bands &= src->bands;
    if (!src->bands || !CreateHdr(dst, sdr, depth, PixelX(width))) return;
    if (to) SeedHdrBands(dst, sdr, src->bands, PixelX(width));
    for (int i = 0; i < 2; i++) {
        if (!(src->bands & (1u << i))) continue;
        int y = PixelY(i * 256), bottom = PixelY((i + 1) * 256);
        BlitHdr(src, dst, PixelX(from), y, PixelX(from + SCREEN_COLUMNS), bottom,
                PixelX(to), y, PixelX(to + SCREEN_COLUMNS), bottom);
    }
    dst->bands |= src->bands;
}

static void UsePipeline(RendererId id)
{
    const RenderPipeline *next = RenderGlPipeline(id);
    if (pipeline != next) {
        if (pipeline && pipeline != presentPipeline) pipeline->destroy();
        pipeline = next;
        scene = NULL;
    }
    scene = pipeline->scene();
}

void GpuRendererShutdown(void)
{
    if (pipeline) pipeline->destroy();
    if (presentPipeline && presentPipeline != pipeline) presentPipeline->destroy();
    pipeline = presentPipeline = NULL;
    scene = NULL;
    DeleteHdr(&sceneHdr);
    DeleteHdr(&wideHdr);
    RenderGlSeedShutdown();
    wideHdrDirty = 0;
    SaturnNativeShutdown();
    memset(saturnPixels, 0, sizeof(saturnPixels));
    saturnLeft=SATURN_WIDTH;saturnTop=VRAM_HEIGHT;saturnRight=saturnBottom=0;
}

static void CreateGlyphTexture(void)
{
    glGenTextures(1, &glyphTexture);
    glBindTexture(GL_TEXTURE_2D, glyphTexture);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, GLYPH_ATLAS_SIZE, GLYPH_ATLAS_SIZE, 0, GL_RGBA, GL_UNSIGNED_BYTE, NULL);
    /* Renderer shaders fetch only level zero and perform their own filtering. */
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
}

int GpuInit(void)
{
    if (initialised) {
        return 1;
    }
    if (!RenderGlLoad()) {
        return 0;
    }
    glDisable(GL_DITHER);

    glGenVertexArrays(1, &vertexArray);
    glBindVertexArray(vertexArray);
    glGenBuffers(1, &vertexBuffer);
    glBindBuffer(GL_ARRAY_BUFFER, vertexBuffer);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, sizeof(BatchVertex), (void *)offsetof(BatchVertex, x));
    glEnableVertexAttribArray(1);
    glVertexAttribPointer(1, 4, GL_FLOAT, GL_FALSE, sizeof(BatchVertex), (void *)offsetof(BatchVertex, r));
    glEnableVertexAttribArray(2);
    glVertexAttribPointer(2, 2, GL_FLOAT, GL_FALSE, sizeof(BatchVertex), (void *)offsetof(BatchVertex, u));
    glEnableVertexAttribArray(3);
    glVertexAttribIPointer(3, 4, GL_SHORT, sizeof(BatchVertex), (void *)offsetof(BatchVertex, texture));
    glEnableVertexAttribArray(4);
    glVertexAttribIPointer(4, 4, GL_UNSIGNED_BYTE, sizeof(BatchVertex), (void *)offsetof(BatchVertex, window));
    glEnableVertexAttribArray(5);
    glVertexAttribIPointer(5, 4, GL_UNSIGNED_BYTE, sizeof(BatchVertex), (void *)offsetof(BatchVertex, mode));
    glEnableVertexAttribArray(6);
    glVertexAttribIPointer(6, 4, GL_UNSIGNED_SHORT, sizeof(BatchVertex), (void *)offsetof(BatchVertex, hd));
    glEnableVertexAttribArray(7);
    glVertexAttribIPointer(7, 4, GL_SHORT, sizeof(BatchVertex), (void *)offsetof(BatchVertex, hdTexels));

    glEnableVertexAttribArray(8);
    glVertexAttribIPointer(8, 4, GL_SHORT, sizeof(BatchVertex), (void *)offsetof(BatchVertex, filterBounds));

    glEnableVertexAttribArray(9);
    glVertexAttribIPointer(9, 4, GL_SHORT, sizeof(BatchVertex), (void *)offsetof(BatchVertex, emissionUv));

    glPixelStorei(GL_UNPACK_ALIGNMENT, 2);
    glPixelStorei(GL_PACK_ALIGNMENT, 1);
    glGenTextures(1, &vramTexture);
    glBindTexture(GL_TEXTURE_2D, vramTexture);
    /* both halves of video memory (gpu.h): the console's, then the second */
    glTexImage2D(GL_TEXTURE_2D, 0, GL_R16UI, VRAM_WIDTH, VRAM_HEIGHT * 2, 0, GL_RED_INTEGER, GL_UNSIGNED_SHORT, NULL);
    glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, VRAM_WIDTH, VRAM_HEIGHT, GL_RED_INTEGER, GL_UNSIGNED_SHORT, psxVram);
    glTexSubImage2D(GL_TEXTURE_2D, 0, 0, VRAM_HEIGHT, VRAM_WIDTH, VRAM_HEIGHT, GL_RED_INTEGER, GL_UNSIGNED_SHORT,
                    psxVramHigh);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);

    CreateGlyphTexture();
    /* the port's other pages: see-through until set */
    glGenTextures(1, &portPagesTexture);
    glBindTexture(GL_TEXTURE_2D, portPagesTexture);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, PORT_PAGES_ACROSS * 256, PORT_PAGES_DOWN * 256, 0, GL_RGBA,
                 GL_UNSIGNED_BYTE, NULL);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    /* the HD textures' atlas: one texel until there are images (a texture the shader can sample, as drivers want) */
    glGenTextures(1, &hdAtlas);
    glBindTexture(GL_TEXTURE_2D, hdAtlas);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, 1, 1, 0, GL_RGBA, GL_UNSIGNED_BYTE, "\0\0\0\0");
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);

    scale = ChooseScale();
    int nativeW, nativeH;
    RenderNativeSize(RenderCurrent()->id, portPal, &nativeW, &nativeH);
    rasterX = nativeW / 320.0f;
    rasterY = nativeH / (float)GPU_LINES;
    HdTexSetScale((int)SDL_ceilf(SDL_max(ScaleX(), ScaleY())));
    if (!CreateFramebuffer()) {
        return 0;
    }
    glViewport(0, 0, PixelX(VRAM_WIDTH), PixelY(VRAM_HEIGHT));
    glClearColor(0, 0, 0, 1);
    glClear(GL_COLOR_BUFFER_BIT);
    initialised = 1;
    return 1;
}

/* Distances to the four edges of the upload that supplied each VRAM word.
   PS1 pages overlap at indexed depths, so record physical words, not UVs. */
static uint32_t vramBounds[VRAM_HEIGHT * 2][VRAM_WIDTH];

static void SetVramBounds(int x, int y, int w, int h, int upload)
{
    if (w <= 0 || h <= 0) return;
    int top = y >= VRAM_HEIGHT ? VRAM_HEIGHT : 0;
    if (x < 0 || y < top || x + w > VRAM_WIDTH || y + h > top + VRAM_HEIGHT) {
        memset(vramBounds[top], 0, sizeof(vramBounds[0]) * VRAM_HEIGHT);
        return;
    }
    upload &= w <= 256 && h <= 256;
    for (int row = 0; row < h; row++) {
        uint32_t *dst = &vramBounds[y + row][x];
        if (!upload) {
            memset(dst, 0, (size_t)w * sizeof(*dst));
            continue;
        }
        uint32_t vertical = (uint32_t)row << 16 | (uint32_t)(h - 1 - row) << 24;
        for (int col = 0; col < w; col++)
            dst[col] = vertical | (uint32_t)col | (uint32_t)(w - 1 - col) << 8;
    }
}

void GpuVramUploaded(int x, int y, int w, int h)
{
    SetVramBounds(x, y, w, h, 1);
}

void GpuVramOverwritten(int x, int y, int w, int h)
{
    SetVramBounds(x, y, w, h, 0);
}

void GpuVramChanged(int x, int y, int w, int h)
{
    int right = x + w, bottom = y + h;

    if (w <= 0 || h <= 0) {
        return;
    }
    HdTexChanged(x, y, w, h);
    if (x < 0 || y < 0 || right > VRAM_WIDTH || bottom > VRAM_HEIGHT) {
        x = 0;
        y = 0;
        right = VRAM_WIDTH;
        bottom = VRAM_HEIGHT;
    }
    if (!vramDirty) {
        dirtyLeft = x;
        dirtyTop = y;
        dirtyRight = right;
        dirtyBottom = bottom;
        vramDirty = 1;
    } else {
        dirtyLeft = SDL_min(dirtyLeft, x);
        dirtyTop = SDL_min(dirtyTop, y);
        dirtyRight = SDL_max(dirtyRight, right);
        dirtyBottom = SDL_max(dirtyBottom, bottom);
    }
}

static void Flush(void);

void GpuSetPortPage(int page, const uint32_t *rgba)
{
    if (page < 1 || page > 63 || !initialised) {
        return;
    }
    Flush(); /* (what is drawn so far, from the page as it was) */
    glActiveTexture(GL_TEXTURE3);
    glBindTexture(GL_TEXTURE_2D, portPagesTexture);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 4);
    glTexSubImage2D(GL_TEXTURE_2D, 0, (page - 1) % PORT_PAGES_ACROSS * 256, (page - 1) / PORT_PAGES_ACROSS * 256, 256,
                    256, GL_RGBA, GL_UNSIGNED_BYTE, rgba);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 2);
    glActiveTexture(GL_TEXTURE0);
}

void GpuSetGlyphMemory(const uint32_t *rgba)
{
    glActiveTexture(GL_TEXTURE1);
    glBindTexture(GL_TEXTURE_2D, glyphTexture);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 4);
    glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, GLYPH_ATLAS_SIZE, GLYPH_ATLAS_SIZE, GL_RGBA, GL_UNSIGNED_BYTE, rgba);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 2);
    glActiveTexture(GL_TEXTURE0);
}

static int vramHighDirty;

void GpuVramHighChanged(void)
{
    vramHighDirty = 1;
}

static void UploadVram(void)
{
    if (vramHighDirty) {
        glBindTexture(GL_TEXTURE_2D, vramTexture);
        glTexSubImage2D(GL_TEXTURE_2D, 0, 0, VRAM_HEIGHT, VRAM_WIDTH, VRAM_HEIGHT, GL_RED_INTEGER, GL_UNSIGNED_SHORT,
                        psxVramHigh);
        vramHighDirty = 0;
    }
    if (!vramDirty) {
        return;
    }
    glBindTexture(GL_TEXTURE_2D, vramTexture);
    if (dirtyRight <= dirtyLeft) {
        glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, VRAM_WIDTH, VRAM_HEIGHT, GL_RED_INTEGER, GL_UNSIGNED_SHORT, psxVram);
    } else {
        int row;
        for (row = dirtyTop; row < dirtyBottom; row++) {
            glTexSubImage2D(GL_TEXTURE_2D, 0, dirtyLeft, row, dirtyRight - dirtyLeft, 1, GL_RED_INTEGER,
                            GL_UNSIGNED_SHORT, &psxVram[row][dirtyLeft]);
        }
    }
    vramDirty = 0;
    dirtyLeft = dirtyTop = dirtyRight = dirtyBottom = 0;
}

/* ---- widescreen ---- */

static int WideWidth(void)
{
    return SCREEN_COLUMNS + 2 * wideMargin;
}

/* Whether a rectangle of video memory reaches into the screen's columns (with a wide framebuffer). */
static int InScreenColumns(int x, int w)
{
    return wideMargin && x < SCREEN_COLUMNS && x + w > 0;
}

/* The middle of the wide framebuffer into video memory's, before that is read. */
static void SyncWide(void)
{
    if (!wideDirty) {
        return;
    }
    glBindFramebuffer(GL_READ_FRAMEBUFFER, wideFramebuffer);
    glBindFramebuffer(GL_DRAW_FRAMEBUFFER, framebuffer);
    glDisable(GL_SCISSOR_TEST);
    glBlitFramebuffer(PixelX(wideMargin), 0, PixelX(wideMargin) + PixelX(SCREEN_COLUMNS), PixelY(VRAM_HEIGHT), 0, 0,
                      PixelX(SCREEN_COLUMNS), PixelY(VRAM_HEIGHT), GL_COLOR_BUFFER_BIT, GL_NEAREST);
    glBindFramebuffer(GL_FRAMEBUFFER, framebuffer);
    wideDirty = 0;
}

static void SyncWideHdr(void)
{
    if (!wideHdrDirty) return;
    CopyHdrCentre(&wideHdr, wideMargin, &sceneHdr, 0, colour, depthBuffer, VRAM_WIDTH);
    glBindFramebuffer(GL_FRAMEBUFFER, framebuffer);
    wideHdrDirty = 0;
}

/* Clears rows y to y + h of the wide framebuffer beyond the screen's columns. */
static void ClearWideSides(int y, int h, float r, float g, float b)
{
    glBindFramebuffer(GL_FRAMEBUFFER, wideFramebuffer);
    glEnable(GL_SCISSOR_TEST);
    glClearColor(r, g, b, 1.0f);
    glScissor(0, PixelY(y), PixelX(wideMargin), PixelY(y + h) - PixelY(y));
    glClear(GL_COLOR_BUFFER_BIT);
    glScissor(PixelX(wideMargin) + PixelX(SCREEN_COLUMNS), PixelY(y), PixelX(wideMargin), PixelY(y + h) - PixelY(y));
    glClear(GL_COLOR_BUFFER_BIT);
    SeedHdrRect(&wideHdr, wideColour, 0, y, wideMargin, y + h);
    SeedHdrRect(&wideHdr, wideColour, wideMargin + SCREEN_COLUMNS, y, WideWidth(), y + h);
    glDisable(GL_SCISSOR_TEST);
    glBindFramebuffer(GL_FRAMEBUFFER, framebuffer);
}

/* What the CPU wrote into video memory's framebuffer, in the screen's columns, into the wide one too. A
   picture across the whole screen is shown as the console shows it, with black beside it. */
static void CopyToWide(int x, int y, int w, int h)
{
    int left = SDL_max(x, 0), right = SDL_min(x + w, SCREEN_COLUMNS);

    if (!InScreenColumns(x, w) || h <= 0) {
        return;
    }
    glBindFramebuffer(GL_READ_FRAMEBUFFER, framebuffer);
    glBindFramebuffer(GL_DRAW_FRAMEBUFFER, wideFramebuffer);
    glDisable(GL_SCISSOR_TEST);
    glBlitFramebuffer(PixelX(left), PixelY(y), PixelX(right), PixelY((y + h)), PixelX(wideMargin) + PixelX(left), PixelY(y),
                      PixelX(wideMargin) + PixelX(right), PixelY((y + h)), GL_COLOR_BUFFER_BIT, GL_NEAREST);
    SeedHdrRect(&wideHdr, wideColour, left + wideMargin, y, right + wideMargin, y + h);
    glBindFramebuffer(GL_FRAMEBUFFER, framebuffer);
    if (left == 0 && right == SCREEN_COLUMNS) {
        ClearWideSides(y, h, 0, 0, 0);
    }
}

/* The margin the window asks for: the console's 4:3 picture across its height, the width it leaves (rounded up:
   the picture covers the window, the part of a column left over is cut off its sides when shown). */
static int WantedMargin(void)
{
    int width, height, margin;

    if (PortLinkRole() == LINK_SLAVE) {
        /* the second player's game draws its half of the split window as wide as the first player's game */
        return PortLinkSplit() && PortLinkBand(&margin, &width, &height) ? SDL_clamp(margin, 0, MAX_WIDE_MARGIN) : 0;
    }
    if (!portSettings.widescreen || (picturesWithoutScene > 1 && picturesWithoutMenu > 1)) {
        return 0;
    }
    PortDrawableSize(&width, &height);
    if (height <= 0) {
        return 0;
    }
    if (PortLinkSplit()) {
        /* each half of a split window: the band's rows across half the height (PAL's are as much wider as its
           lines are more) */
        margin = (int)SDL_ceil((GPU_SPLIT_ROWS * 2.0 * 240 / GPU_LINES * width / height - SCREEN_COLUMNS) / 2);
    } else {
        /* (240: the columns a 4:3 picture has for its height's 320 x 3/4, whatever its lines) */
        margin = (int)SDL_ceil((240.0 * width / height - SCREEN_COLUMNS) / 2);
    }
    return SDL_clamp(margin, 0, MAX_WIDE_MARGIN);
}

/* Makes the wide framebuffer for a margin (none for 0), from what video memory's shows. */
static void SetWideMargin(int margin)
{
    if (margin == wideMargin) {
        return;
    }
    PictureChanged(0, VRAM_HEIGHT);
    SyncWide();
    SyncWideHdr();
    if (wideFramebuffer) {
        DeleteHdr(&wideHdr);
        glDeleteFramebuffers(1, &wideFramebuffer);
        glDeleteTextures(1, &wideColour);
        glDeleteRenderbuffers(1, &wideDepth);
        wideFramebuffer = wideColour = wideDepth = 0;
    }
    wideMargin = margin;
    if (margin) {
        glGenTextures(1, &wideColour);
        glBindTexture(GL_TEXTURE_2D, wideColour);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, PixelX(WideWidth()), PixelY(VRAM_HEIGHT), 0, GL_RGBA, GL_UNSIGNED_BYTE,
                     NULL);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glGenFramebuffers(1, &wideFramebuffer);
        glBindFramebuffer(GL_FRAMEBUFFER, wideFramebuffer);
        glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, wideColour, 0);
        wideDepth = DepthFor(PixelX(WideWidth()), PixelY(VRAM_HEIGHT));
        glDisable(GL_SCISSOR_TEST);
        glClearColor(0, 0, 0, 1);
        glClear(GL_COLOR_BUFFER_BIT);
        CopyToWide(0, 0, SCREEN_COLUMNS, VRAM_HEIGHT);
        CopyHdrCentre(&sceneHdr, 0, &wideHdr, wideMargin, wideColour, wideDepth, WideWidth());
    }
    glBindFramebuffer(GL_FRAMEBUFFER, framebuffer);
    areaWide = InScreenColumns(areaX, areaW) && areaX >= 0 && areaX + areaW <= SCREEN_COLUMNS;
}

int PortScreenMargin(void)
{
    return wideMargin;
}

void PortWideScene(void)
{
    sceneWide = 1;
}

void GpuWideMenu(void)
{
    menuWide = 1;
}

static void Flush(void);

void GpuWideLoadScreen(void)
{
    menuWide = 1;
    if (!wideMargin) {
        /* at once, before its first picture is drawn: the screen before it (the title) was 4:3 */
        Flush();
        picturesWithoutMenu = 0;
        SetWideMargin(WantedMargin());
    }
}

int GpuRaceWide(void)
{
    /* (the pictures between a race's frames are drawn after the new picture that ended sceneWide) */
    return sceneWide || picturesWithoutScene == 0;
}

void GpuBackdrop(int on)
{
    backdropNext = on;
}

/* The framebuffer the next primitives go to, its viewport and the draw area's scissor. */
static void BindTarget(GLuint hdrTarget)
{
    glUniform2f(scene->uniformNativeGrid, rasterX, rasterY);
    if (areaWide) {
        /* a draw area across the screen reaches over the sides as well: a race's scene, and in a menu what fills
           them and the panels that slide out to the window's edges and back */
        int full = areaX == 0 && areaW == SCREEN_COLUMNS;

        glBindFramebuffer(GL_FRAMEBUFFER, hdrTarget ? hdrTarget : wideFramebuffer);
        glViewport(0, 0, PixelX(WideWidth()), PixelY(VRAM_HEIGHT));
        glEnable(GL_SCISSOR_TEST);
        glScissor(full ? 0 : PixelX((areaX + wideMargin)), PixelY(areaY), full ? PixelX(WideWidth()) : PixelX(areaX + wideMargin + areaW) - PixelX(areaX + wideMargin),
                  PixelY(areaY + areaH) - PixelY(areaY));
        glUniform2f(scene->uniformTarget, TargetWidth(WideWidth()), TargetHeight(VRAM_HEIGHT));
        glUniform2f(scene->uniformOrigin, WideOrigin(wideMargin), 0.0f);
        wideDirty = 1;
        wideHdrDirty |= wideHdr.bands != 0 || sceneHdr.bands != 0;
        drawnSincePresent |= !interpolatingDraw;
        return;
    }
    glBindFramebuffer(GL_FRAMEBUFFER, hdrTarget ? hdrTarget : framebuffer);
    glViewport(0, 0, PixelX(VRAM_WIDTH), PixelY(VRAM_HEIGHT));
    glEnable(GL_SCISSOR_TEST);
    glScissor(PixelX(areaX), PixelY(areaY), PixelX(areaX + areaW) - PixelX(areaX), PixelY(areaY + areaH) - PixelY(areaY));
    glUniform2f(scene->uniformTarget, TargetWidth(VRAM_WIDTH), TargetHeight(VRAM_HEIGHT));
    glUniform2f(scene->uniformOrigin, 0.0f, 0.0f);
    drawnSincePresent |= !interpolatingDraw;
}

/* Filtered edges need blending even on otherwise opaque polygons. */
static int Blended(const GpuDrawState *s)
{
    return s->semi >= 0 || s->fade || (s->textured && (s->depth == 3 || s->filtered));
}

/* Batch by blending and passes; other state travels with each vertex. */
static int SameState(const GpuDrawState *a, const GpuDrawState *b)
{
    /* (the console's subtraction is a blend of its own; the opaque are drawn without blending, which is cheaper) */
    return (a->semi == 2) == (b->semi == 2) && Blended(a) == Blended(b);
}

/* The premultiplied blend every fragment says how much of what is behind stays in (the shader's Out), or the
   console's subtraction; the framebuffer's alpha stays as it is. */
static void SetBlend(int subtract)
{
    if (!Blended(&batchState)) {
        glDisable(GL_BLEND); /* (the shader writes the colour itself: coverage 1, no fade) */
        return;
    }
    glEnable(GL_BLEND);
    glBlendEquation(subtract ? GL_FUNC_REVERSE_SUBTRACT : GL_FUNC_ADD);
    glBlendFuncSeparate(GL_ONE, subtract ? GL_ONE : GL_ONE_MINUS_SRC_ALPHA, GL_ZERO, GL_ONE);
}

/* The depth buffer's slack: what is drawn after may be this much farther (a fraction of its depth) than what is
   drawn before at that pixel and still be drawn over it, as in the game's order. */
#define DEPTH_SLACK 0.02f

static void DrawBatch(int pass)
{
    glUniform1i(scene->uniformPass, pass);
    SetBlend(pass == 2);
    glDrawArrays(batchPoints ? GL_POINTS : GL_TRIANGLES, 0, batchCount);
}

static void Flush(void)
{
    const GpuDrawState *s = &batchState;

    /* Only the profile-controlled shaders may dither; GL enables driver dithering by default. */
    SaturnFlushGpu();
    SaturnFlushPixels();
    glDisable(GL_DITHER);
    if (!batchCount) {
        return;
    }
    const RenderFrame *render = RenderCurrent();
    RenderResources resources = RenderResourcesFor(render);
    UsePipeline(render->id);
    int fullWide = areaWide && areaX == 0 && areaW == SCREEN_COLUMNS;
    float left = SDL_max(batchLeft, fullWide ? -wideMargin : areaX);
    float right = SDL_min(batchRight, fullWide ? SCREEN_COLUMNS + wideMargin : areaX + areaW);
    float top = SDL_max(batchTop, areaY), bottom = SDL_min(batchBottom, areaY + areaH);
    if (right > left) PictureChanged(top, bottom - top);
    RememberPicture(left, top, right - left, bottom - top);
    UploadVram();
    SceneHdr *hdr = areaWide ? &wideHdr : &sceneHdr;
    unsigned bands = right > left && left < SCREEN_COLUMNS + (areaWide ? wideMargin : 0) && bottom > top
                         ? HdrBands(top, bottom) : 0;
    int writeHdr = 0;
    if (bands && resources.hdr && GpuRaceWide()) {
        int width = PixelX(areaWide ? WideWidth() : VRAM_WIDTH);
        writeHdr = CreateHdr(hdr, areaWide ? wideColour : colour, areaWide ? wideDepth : depthBuffer, width);
        if (writeHdr) SeedHdrBands(hdr, areaWide ? wideColour : colour, bands,
                                   areaWide ? width : PixelX(SCREEN_COLUMNS));
    }
    if (!writeHdr) hdr->bands &= ~bands;
    glUseProgram(scene->program);
    BindTarget(writeHdr ? hdr->drawFramebuffer : 0);
    glUniform1f(scene->uniformPointSize, batchPoints ? (float)scale : 0.0f);
#if !defined(__EMSCRIPTEN__) && !defined(PORT_GL_LINKED)
    if (batchPoints) glEnable(GL_PROGRAM_POINT_SIZE);
#endif
    glBindVertexArray(vertexArray);
    glBindBuffer(GL_ARRAY_BUFFER, vertexBuffer);
    glBufferData(GL_ARRAY_BUFFER, (GLsizeiptr)(batchCount * sizeof(BatchVertex)), batch, GL_STREAM_DRAW);
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, vramTexture);
    glUniform1i(scene->uniformVram, 0);
    glActiveTexture(GL_TEXTURE1);
    glBindTexture(GL_TEXTURE_2D, glyphTexture);
    glUniform1i(scene->uniformGlyphs, 1);
    glActiveTexture(GL_TEXTURE2);
    /* Profiles using original art still need a complete texture for the unused sampler. */
    glBindTexture(GL_TEXTURE_2D, resources.originalTextures ? glyphTexture : hdAtlas);
    glUniform1i(scene->uniformHdAtlas, 2);
    glActiveTexture(GL_TEXTURE3);
    glBindTexture(GL_TEXTURE_2D, portPagesTexture);
    glUniform1i(scene->uniformPortPages, 3);
    if (pipeline->bind) pipeline->bind(RenderProfile(render), writeHdr);
    glActiveTexture(GL_TEXTURE0);
    /* The depth buffer: what has a depth is drawn where nothing nearer is, give or take DEPTH_SLACK (the game's own
       order decides among things at about the same depth: a shadow or a pad on the track). The depth of the batch's
       opaque texels is written first, so that the batch is tested against itself too: a polygon behind another of
       the same batch (a far wall in the same batch as the track in front of it) is hidden by it whatever their
       order in the batch, as by one of an earlier batch. Without it (the console's look) the game's order alone
       decides. */
    if (RenderDepth(render)) {
        glEnable(GL_DEPTH_TEST);
        /* Check the whole batch: a leading translucent effect must not suppress an opaque road's depth. */
        if (batchDepth) {
            glColorMask(GL_FALSE, GL_FALSE, GL_FALSE, GL_FALSE);
            glDepthMask(GL_TRUE);
            glDepthFunc(GL_LESS);
            glUniform1i(scene->uniformDepthPass, 1);
            glUniform1i(scene->uniformPass, 3);
            glDisable(GL_BLEND);
            glDrawArrays(batchPoints ? GL_POINTS : GL_TRIANGLES, 0, batchCount);
            glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
        }
        glDepthFunc(GL_LEQUAL);
        glDepthMask(GL_FALSE);
        glUniform1f(scene->uniformDepthPull, 1.0f / (1.0f - DEPTH_SLACK));
        glUniform1i(scene->uniformDepthPass, 0);
    } else {
        glDisable(GL_DEPTH_TEST);
    }
    if (s->semi == 2) {
        /* the console's subtraction: what is opaque (texels without the semi-transparency bit) drawn first, then
           the rest subtracted */
        DrawBatch(1);
        DrawBatch(2);
    } else {
        DrawBatch(0);
    }
    glDisable(GL_DEPTH_TEST);
#if !defined(__EMSCRIPTEN__) && !defined(PORT_GL_LINKED)
    if (batchPoints) glDisable(GL_PROGRAM_POINT_SIZE);
#endif
    batchCount = 0;
}

void GpuFlush(void)
{
    Flush();
}

void GpuInterpolating(int on)
{
    Flush();
    interpolatingDraw = on;
}

void GpuSetDrawArea(int x, int y, int w, int h, int offsetX, int offsetY)
{
    (void)offsetX;
    (void)offsetY;
    Flush();
    areaX = x;
    areaY = y;
    areaW = w > 0 ? w : 0;
    areaH = h > 0 ? h : 0;
    areaWide = InScreenColumns(x, w) && x >= 0 && x + w <= SCREEN_COLUMNS;
}

/* Semi-transparent textured primitives must not share a batch: each one's
   opaque and blended passes have to come before the next primitive. */
static GpuDrawState primitiveState; /* the primitive being added */
static uint16_t primitiveHd[4];     /* ... its HD texture (BatchVertex) */
static int16_t primitiveHdTexels[4];

/* ---- HD textures: the images in use, side by side in one texture ---- */

static int hdAtlasSize;
static unsigned hdEpoch;            /* the images' (HdTexEpoch) the atlas holds */
static unsigned hdFill = 1;         /* the atlas's filling: an image is in it when hdPlaced[] says this one */
static unsigned *hdPlaced;
static int *hdPlace;                /* x, y, w, h of each image in it */
static int hdImages;
static int hdShelfX, hdShelfY, hdShelfH; /* the shelf being filled, left to right, the shelves top to bottom */
static int pictures, hdEmptiedAt = -1;

/* The atlas empty again (what was drawn from it is drawn first). */
static void HdEmpty(void)
{
    Flush();
    hdFill++;
    hdShelfX = hdShelfY = hdShelfH = 0;
}

/* Where image is in the atlas (put there now if it was not); 0 while it is not ready, or does not fit. */
static int HdPlaced(int image, int *at)
{
    const uint8_t *pixels;
    int w, h, size;

    if (HdTexEpoch() != hdEpoch || HdTexImageCount() != hdImages) {
        /* other images (another scale, another pack): an atlas for them */
        size = SDL_min(scale <= 6 ? 4096 : 8192, GpuMaxScale(RenderCurrent()->id) * (SCREEN_COLUMNS + 2 * MAX_WIDE_MARGIN));
        HdEmpty();
        hdEpoch = HdTexEpoch();
        hdImages = HdTexImageCount();
        hdPlaced = SDL_realloc(hdPlaced, sizeof(unsigned) * (size_t)SDL_max(hdImages, 1));
        hdPlace = SDL_realloc(hdPlace, sizeof(int) * 4 * (size_t)SDL_max(hdImages, 1));
        memset(hdPlaced, 0, sizeof(unsigned) * (size_t)SDL_max(hdImages, 1));
        if (size != hdAtlasSize) {
            if (!hdAtlas) {
                glGenTextures(1, &hdAtlas);
            }
            glBindTexture(GL_TEXTURE_2D, hdAtlas);
            glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, size, size, 0, GL_RGBA, GL_UNSIGNED_BYTE, NULL);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
            hdAtlasSize = size;
        }
    }
    if (image >= hdImages) {
        return 0;
    }
    if (hdPlaced[image] != hdFill) {
        if (!(pixels = HdTexImage(image, &w, &h)) || w > hdAtlasSize || h > hdAtlasSize) {
            return 0;
        }
        if (hdShelfX + w > hdAtlasSize) {
            hdShelfY += hdShelfH;
            hdShelfX = hdShelfH = 0;
        }
        if (hdShelfY + h > hdAtlasSize) {
            /* full: emptied for the images in use now (once a picture: past that some go without) */
            if (hdEmptiedAt == pictures) {
                return 0;
            }
            HdEmpty();
            hdEmptiedAt = pictures;
        }
        glBindTexture(GL_TEXTURE_2D, hdAtlas);
        glPixelStorei(GL_UNPACK_ALIGNMENT, 4);
        glTexSubImage2D(GL_TEXTURE_2D, 0, hdShelfX, hdShelfY, w, h, GL_RGBA, GL_UNSIGNED_BYTE, pixels);
        glPixelStorei(GL_UNPACK_ALIGNMENT, 2);
        hdPlace[image * 4] = hdShelfX;
        hdPlace[image * 4 + 1] = hdShelfY;
        hdPlace[image * 4 + 2] = w;
        hdPlace[image * 4 + 3] = h;
        hdPlaced[image] = hdFill;
        hdShelfX += w;
        hdShelfH = SDL_max(hdShelfH, h);
    }
    memcpy(at, &hdPlace[image * 4], sizeof(int) * 4);
    return 1;
}

/* The primitive's HD texture, if the pack has one for its texels. */
static void FindHd(const GpuDrawState *state, const GpuVertex *vertices, int count)
{
    float u0 = vertices[0].u, v0 = vertices[0].v, u1 = u0, v1 = v0;
    HdTexHit hit;
    int at[4], i;

    memset(primitiveHd, 0, sizeof(primitiveHd));
    if (!state->textured || (state->depth > 2 && state->depth != 3)) {
        return;
    }
    for (i = 1; i < count; i++) {
        u0 = SDL_min(u0, vertices[i].u);
        v0 = SDL_min(v0, vertices[i].v);
        u1 = SDL_max(u1, vertices[i].u);
        v1 = SDL_max(v1, vertices[i].v);
    }
    if (state->depth == 3) {
        /* a port page (the PC version's) has no HD texture: the texels the primitive covers instead, which its
           filtered samples stay within (an image's edge does not take in the one beside it on the page) */
        primitiveHdTexels[0] = (int16_t)u0;
        primitiveHdTexels[1] = (int16_t)v0;
        primitiveHdTexels[2] = (int16_t)u1;
        primitiveHdTexels[3] = (int16_t)v1;
        return;
    }
    if (scale == 1 || RenderResourcesFor(RenderCurrent()).originalTextures || !HdTexActive()) {
        return;
    }
    if (!HdTexFind(state->pageX, state->pageY, state->depth, state->clutX, state->clutY, u0, v0, u1, v1, &hit) ||
        !HdPlaced(hit.image, at)) {
        return;
    }
    /* the image's part over those texels: the replaced texture's texels are at[2] / nameW pixels wide */
    primitiveHd[0] = (uint16_t)(at[0] + hit.srcX * at[2] / hit.nameW);
    primitiveHd[1] = (uint16_t)(at[1] + hit.srcY * at[3] / hit.nameH);
    primitiveHd[2] = (uint16_t)SDL_max(hit.w * at[2] / hit.nameW, 1);
    primitiveHd[3] = (uint16_t)SDL_max(hit.h * at[3] / hit.nameH, 1);
    primitiveHdTexels[0] = (int16_t)hit.x;
    primitiveHdTexels[1] = (int16_t)hit.y;
    primitiveHdTexels[2] = (int16_t)hit.w;
    primitiveHdTexels[3] = (int16_t)hit.h;
}


static void BeginMode(const GpuDrawState *state, int vertices, int points)
{
    SaturnFlushGpu();
    SaturnFlushPixels();
    if (batchCount && (batchPoints != points || !SameState(state, &batchState) || batchCount + vertices > BATCH_VERTICES ||
                       (state->textured && state->semi == 2))) {
        Flush();
    }
    if (!batchCount) {
        batchState = *state;
        batchPoints = points;
        batchDepth = 0;
    }
    primitiveState = *state;
}

static void Begin(const GpuDrawState *state, int vertices)
{
    BeginMode(state, vertices, 0);
}

static void Add(const GpuVertex *v, unsigned shade)
{
    if (!batchCount) {
        batchLeft = batchRight = v->x;
        batchTop = batchBottom = v->y;
    } else {
        batchLeft = SDL_min(batchLeft, v->x); batchRight = SDL_max(batchRight, v->x);
        batchTop = SDL_min(batchTop, v->y); batchBottom = SDL_max(batchBottom, v->y);
    }
    BatchVertex *out = &batch[batchCount++];

    out->x = v->x;
    out->y = v->y;
    out->w = v->w > 0.0f && v->noDepth ? -v->w : v->w; /* (the vertex shader's position.z) */
    batchDepth |= out->w > 0.0f && v->a == 255 && (primitiveState.textured || primitiveState.semi < 0);
    out->r = v->r / 255.0f;
    out->g = v->g / 255.0f;
    out->b = v->b / 255.0f;
    out->a = v->a / 255.0f;
    out->u = v->u;
    out->v = v->v;
    out->texture[0] = (int16_t)primitiveState.pageX;
    out->texture[1] = (int16_t)primitiveState.pageY;
    out->texture[2] = (int16_t)primitiveState.clutX;
    out->texture[3] = (int16_t)primitiveState.clutY;
    out->window[0] = (uint8_t)primitiveState.windowMaskX;
    out->window[1] = (uint8_t)primitiveState.windowMaskY;
    out->window[2] = (uint8_t)primitiveState.windowOffsetX;
    out->window[3] = (uint8_t)primitiveState.windowOffsetY;
    out->mode[0] = (uint8_t)primitiveState.textured;
    out->mode[1] = (uint8_t)shade;
    /* Depth and addressing, then source light encoding, emission and screen overlay. */
    out->mode[2] = (uint8_t)(primitiveState.depth | (primitiveState.textureFlags >> 2) |
                             (primitiveState.ps1Light ? 32 : 0) |
                             (primitiveState.emission.enabled ? 64 : 0) | (primitiveState.screenOverlay ? 128 : 0));
    /* Depth bias, sampling rule and blend mode + 1. */
    out->mode[3] = (uint8_t)((v->backFacing || v->exactDepth) | (primitiveState.filtered << 1) |
                             (primitiveState.semi + 1) << 4);
    for (int i = 0; i < 4; i++) out->filterBounds[i] = (int16_t)primitiveFilterBounds[i];
    memcpy(out->hd, primitiveHd, sizeof(out->hd));
    memcpy(out->hdTexels, primitiveHdTexels, sizeof(out->hdTexels));
    for (int i = 0; i < 4; i++) out->emissionUv[i] = (int16_t)primitiveState.emission.uv[i];
}

/* Video memory is kept twice: psxVram (what the CPU uploads and reads, and
   what textures sample) and the framebuffer (what the GPU draws, at the
   display's scale). The blocks the GPU has drawn into since they were last
   written by the CPU hold their true pixels in the framebuffer only. */
#define BLOCK 8
static uint8_t drawnBlocks[VRAM_HEIGHT / BLOCK][VRAM_WIDTH / BLOCK];

static void MarkDrawn(float minX, float minY, float maxX, float maxY)
{
    int left = SDL_max((int)minX, areaX), top = SDL_max((int)minY, areaY);
    int right = SDL_min((int)maxX + 1, areaX + areaW), bottom = SDL_min((int)maxY + 1, areaY + areaH);
    int bx, by;

    if (right <= left || bottom <= top) {
        return;
    }
    for (by = top / BLOCK; by <= (bottom - 1) / BLOCK && by < VRAM_HEIGHT / BLOCK; by++) {
        for (bx = left / BLOCK; bx <= (right - 1) / BLOCK && bx < VRAM_WIDTH / BLOCK; bx++) {
            drawnBlocks[by][bx] = 1;
        }
    }
}

static void ClearDrawn(int x, int y, int w, int h)
{
    int bx, by;

    /* only blocks the rectangle covers completely */
    for (by = (y + BLOCK - 1) / BLOCK; (by + 1) * BLOCK <= y + h && by < VRAM_HEIGHT / BLOCK; by++) {
        for (bx = (x + BLOCK - 1) / BLOCK; (bx + 1) * BLOCK <= x + w && bx < VRAM_WIDTH / BLOCK; bx++) {
            drawnBlocks[by][bx] = 0;
        }
    }
}

static int saturnSkyDrawn;
typedef struct SaturnDraw {
    GpuDrawState source;
    int noDepth;
} SaturnDraw;

static int SaturnSampleIndex(void *user, float u, float v)
{
    SaturnDraw *d = user;
    /* Stabilize exact texel boundaries across CPU division and shader arithmetic. */
    int x = (int)floorf(u+0.00001f) & 255, y = (int)floorf(v+0.00001f) & 255;
    x = (x & ~(d->source.windowMaskX*8)) | ((d->source.windowOffsetX & d->source.windowMaskX)*8);
    y = (y & ~(d->source.windowMaskY*8)) | ((d->source.windowOffsetY & d->source.windowMaskY)*8);
    int py = (d->source.pageY + y) & 1023;
    const uint16_t *row = py >= 512 ? psxVramHigh[py-512] : psxVram[py];
    return (row[(d->source.pageX + x/4) & 1023] >> ((x&3)*4)) & 15;
}

static void SaturnEmit(void *user, int x, int y, uint16_t rgb, int blend, float depth, uint8_t alpha)
{
    SaturnDraw *d = user;
    GpuDrawState state = {0};
    state.semi = blend == -2 ? 0 : blend; state.fade = alpha < 255;
    if (blend == -2) rgb = 0; /* RGB destination shadow: half the destination. */
    GpuVertex pixel = {0};
    pixel.x = x / rasterX; pixel.y = y / rasterY; pixel.w = depth;
    pixel.noDepth = d->noDepth; pixel.world = 1;
    pixel.r = (uint8_t)((rgb & 31)*8); pixel.g = (uint8_t)(((rgb>>5)&31)*8);
    pixel.b = (uint8_t)(((rgb>>10)&31)*8); pixel.a = alpha;
    unsigned shade = SHADE_AFFINE_COLOUR | SHADE_PS1_COLOUR;
    BeginMode(&state, 1, 1);
    Add(&pixel, shade);
}

/* Aggregate ordered native writes as premultiplied colour/coverage. The final
   overlay blends over prior compatibility draws; flush before the next such draw.
   Optional depth uses the point path so it still shares the host depth buffer. */
static void SaturnComposite(void *user, int x, int y, uint16_t rgb, int blend, float depth, uint8_t alpha)
{
    (void)user; (void)depth;
    int px=x+SATURN_MARGIN;
    if (px<0 || px>=SATURN_WIDTH || y<0 || y>=VRAM_HEIGHT) return;
    uint8_t *out=saturnPixels[y][px];
    unsigned a=blend >= 0 || blend == -2 ? (alpha+1)/2 : alpha;
    if (a==255) {
        /* Most road/scenery writes are opaque: avoid reading and blending the
           previous pixel (including repeated writes along a distorted edge). */
        out[0]=(uint8_t)((rgb&31)*8);
        out[1]=(uint8_t)(((rgb>>5)&31)*8);
        out[2]=(uint8_t)(((rgb>>10)&31)*8);
        out[3]=255;
    } else {
        for(int c=0;c<3;c++) {
            unsigned src=blend == -2 ? 0 : ((rgb>>(5*c))&31)*8;
            out[c]=(uint8_t)((src*a+out[c]*(255-a)+127)/255);
        }
        out[3]=(uint8_t)(a+(out[3]*(255-a)+127)/255);
    }
    if(px<saturnLeft) saturnLeft=px;
    if(px>=saturnRight) saturnRight=px+1;
    if(y<saturnTop) saturnTop=y;
    if(y>=saturnBottom) saturnBottom=y+1;
}

static void SaturnFlushPixels(void)
{
    if(saturnRight<=saturnLeft || saturnBottom<=saturnTop) return;
    UsePipeline(RENDER_SATURN);
    glUseProgram(scene->program); BindTarget(0);
    int left=saturnLeft-SATURN_MARGIN, right=saturnRight-SATURN_MARGIN;
    RenderSaturnPixels(&saturnPixels[0][0][0], SATURN_WIDTH, VRAM_HEIGHT, SATURN_MARGIN,
        left,saturnTop,right,saturnBottom,(areaWide ? TargetWidth(WideWidth()) : TargetWidth(VRAM_WIDTH))*rasterX,
        TargetHeight(VRAM_HEIGHT)*rasterY,areaWide ? WideOrigin(wideMargin)*rasterX : 0,(float)scale,(float)scale);
    glUseProgram(scene->program);
    (areaWide ? &wideHdr : &sceneHdr)->bands &= ~HdrBands(saturnTop / rasterY, saturnBottom / rasterY);
    PictureChanged(saturnTop/rasterY,(saturnBottom-saturnTop)/rasterY);
    RememberPicture(left/rasterX,saturnTop/rasterY,(right-left)/rasterX,(saturnBottom-saturnTop)/rasterY);
    for(int y=saturnTop;y<saturnBottom;y++)
        memset(saturnPixels[y][saturnLeft],0,(size_t)(saturnRight-saturnLeft)*4);
    saturnLeft=SATURN_WIDTH;saturnTop=VRAM_HEIGHT;saturnRight=saturnBottom=0;
}

static void SaturnFlushGpu(void)
{
    if(!RenderSaturnQuadsPending()) return;
    UsePipeline(RENDER_SATURN);
    glActiveTexture(GL_TEXTURE0);UploadVram();
    glUseProgram(scene->program);BindTarget(0);
    int left=areaWide ? -wideMargin : areaX;
    int right=areaWide ? SCREEN_COLUMNS+wideMargin : areaX+areaW;
    RenderSaturnFlushQuads(vramTexture,PixelX(areaWide ? WideWidth() : VRAM_WIDTH),PixelY(VRAM_HEIGHT),
        areaWide ? WideOrigin(wideMargin) : 0,ScaleX(),ScaleY(),scale,left,areaY,right,areaY+areaH);
    glUseProgram(scene->program);
    (areaWide ? &wideHdr : &sceneHdr)->bands &= ~HdrBands(areaY, areaY + areaH);
    PictureChanged(areaY,areaH);RememberPicture(left,areaY,right-left,areaH);
}

/* Explicit reference mode for tests and visual comparisons; GPU is the default. */
static int SaturnReference(void)
{
    const char *value=SDL_getenv("WIPEOUT_SATURN_REFERENCE");
    return value && SDL_atoi(value)!=0;
}

static void SaturnBackground(const GpuDrawState *state)
{
    if (saturnSkyDrawn) return;
    const uint16_t *palette; int width, height;
    const uint8_t *picture = SaturnNativeSkyData(&palette, &width, &height);
    if (!picture) return;
    saturnSkyDrawn = 1;
    Flush();
    UsePipeline(RENDER_SATURN);
    glUseProgram(scene->program);
    BindTarget(0);
    int left = areaWide ? -wideMargin : areaX;
    int right = areaWide ? SCREEN_COLUMNS + wideMargin : areaX + areaW;
    RenderSaturnBackground(picture, palette, width, height, SaturnNativeSkyRevision(),
        state, !RenderResourcesFor(RenderCurrent()).cameraAlignedSky, left, areaY, right, areaY + areaH,
        areaX + areaW*.5f, areaY + areaH*.5f,
        areaWide ? TargetWidth(WideWidth()) : TargetWidth(VRAM_WIDTH), TargetHeight(VRAM_HEIGHT),
        areaWide ? WideOrigin(wideMargin) : 0, ScaleX(), ScaleY(), scale);
    glUseProgram(scene->program);
    (areaWide ? &wideHdr : &sceneHdr)->bands &= ~HdrBands(areaY, areaY + areaH);
    PictureChanged(areaY, areaH);
    RememberPicture(left, areaY, right-left, areaH);
    MarkDrawn(areaX,areaY,areaX+areaW,areaY+areaH);
}

/* Match the whole primitive to an uploaded image. Texture windows still
   use the console's per-tap address calculation. */
static int NativeFilterBounds(const GpuDrawState *state, const GpuVertex *v, int count, int bounds[4])
{
    if (state->windowMaskX || state->windowMaskY || state->depth >= 3) return 0;
    int shift = 2 - state->depth;
    int u = (int)floorf(v[0].u), t = (int)floorf(v[0].v);
    if (u < 0 || u > 255 || t < 0 || t > 255) return 0;
    int x = state->pageX + (u >> shift), y = state->pageY + t;
    if (x >= VRAM_WIDTH || y >= VRAM_HEIGHT * 2) return 0;
    uint32_t rect = vramBounds[y][x];
    if (!rect) return 0;
    int left = ((u >> shift) - (int)(rect & 255)) * (1 << shift);
    int right = (((u >> shift) + (int)((rect >> 8) & 255) + 1) << shift) - 1;
    int top = t - (int)((rect >> 16) & 255), bottom = t + (int)(rect >> 24);
    if (left < 0 || right > 255 || top < 0 || bottom > 255) return 0;
    for (int i = 0; i < count; i++) {
        if (v[i].u < left || v[i].u >= right + 1 || v[i].v < top || v[i].v >= bottom + 1)
            return 0;
    }
    bounds[0] = left; bounds[1] = top; bounds[2] = right; bounds[3] = bottom;
    return 1;
}

void GpuTriangles(const GpuDrawState *given, const GpuVertex *vertices, int count)
{
    if (RenderCurrent()->id == RENDER_SATURN) {
        if (given->saturnSky) { SaturnBackground(given); return; }
        if (given->saturn && RenderResourcesFor(RenderCurrent()).nativeQuads) {
            if (batchCount) Flush();
            SaturnDraw d = {.source = *given, .noDepth = vertices[0].noDepth};
            SaturnNativePoly poly = *given->saturn;
            poly.alphaMesh = (poly.mode & 0x100) && RenderResourcesFor(RenderCurrent()).alphaMesh;
            if (poly.dynamicLight) {
                SaturnShipLight(SaturnTrackLight(vertices[0].r, vertices[0].g, vertices[0].b), poly.gouraud);
            }
            if(SaturnReference()) {
                GpuVertex native[4];
                for (int k = 0; k < 4; k++) {
                    native[k] = vertices[k < count ? k : 0];
                    native[k].x *= rasterX; native[k].y *= rasterY;
                }
                SaturnVdp1Draw(&poly, native, (int)SDL_lroundf((areaWide ? -wideMargin : areaX)*rasterX),
                              (int)SDL_lroundf(areaY*rasterY),
                              (int)SDL_lroundf((areaWide ? SCREEN_COLUMNS+wideMargin : areaX+areaW)*rasterX),
                              (int)SDL_lroundf((areaY+areaH)*rasterY),
                              RenderDepth(RenderCurrent()) ? SaturnEmit : SaturnComposite, SaturnSampleIndex, &d);
            } else {
                /* Pipeline creation precedes submission: switching away destroys
                   its pending commands only after the normal flush boundary. */
                UsePipeline(RENDER_SATURN);
                int clipLeft=areaWide ? -wideMargin : areaX;
                int clipRight=areaWide ? SCREEN_COLUMNS+wideMargin : areaX+areaW;
                if(!RenderSaturnQuad(&poly,vertices,given,SaturnNativeMaterialRevision(),RenderDepth(RenderCurrent()),
                                    ScaleX(),ScaleY(),clipLeft,areaY,clipRight,areaY+areaH)) {
                    SaturnFlushGpu();
                    if(!RenderSaturnQuad(&poly,vertices,given,SaturnNativeMaterialRevision(),RenderDepth(RenderCurrent()),
                                        ScaleX(),ScaleY(),clipLeft,areaY,clipRight,areaY+areaH)) abort();
                }
            }
            float left=vertices[0].x,right=left,top=vertices[0].y,bottom=top;
            for(int k=1;k<count;k++) {
                left=fminf(left,vertices[k].x);right=fmaxf(right,vertices[k].x);
                top=fminf(top,vertices[k].y);bottom=fmaxf(bottom,vertices[k].y);
            }
            if(isfinite(left) && isfinite(right) && isfinite(top) && isfinite(bottom))
                MarkDrawn(SDL_clamp(floorf(left),areaX,areaX+areaW),SDL_clamp(floorf(top),areaY,areaY+areaH),
                          SDL_clamp(ceilf(right),areaX,areaX+areaW),SDL_clamp(ceilf(bottom),areaY,areaY+areaH));
            return;
        }
    }
    GpuDrawState faded = *given;
    const GpuDrawState *state = &faded;
    int i;
    int perspective = 1, world = 0, stretch, backdrop;
    float minX = vertices[0].x, minY = vertices[0].y, maxX = minX, maxY = minY;

    for (i = 1; i < count; i++) {
        minX = SDL_min(minX, vertices[i].x);
        minY = SDL_min(minY, vertices[i].y);
        maxX = SDL_max(maxX, vertices[i].x);
        maxY = SDL_max(maxY, vertices[i].y);
    }
    MarkDrawn(minX, minY, maxX, maxY);
    for (i = 0; i < count; i++) {
        faded.fade |= vertices[i].a < 255;
        faded.n64 |= vertices[i].n64;
        world |= vertices[i].world || vertices[i].w > 0.0f;
    }

    for (i = 0; i < count; i++) {
        if (vertices[i].w <= 0.0f) {
            perspective = 0;
        }
    }
    /* widescreen: an untextured primitive in 2D across the whole screen (a clear, a fade, a flash) covers
       the sides as well */
    stretch = areaWide && areaX == 0 && areaW == SCREEN_COLUMNS && !given->textured && !perspective && minX <= 0.0f &&
              maxX >= (float)SCREEN_COLUMNS;
    /* ... and a menu's backdrop is enlarged about the middle of the draw area to fill them, keeping its shape */
    backdrop = backdropNext && areaWide && areaX == 0 && areaW == SCREEN_COLUMNS && !perspective && !GpuRaceWide();
    /* Decode material metadata independently of the selected rasterizer. */
    primitiveFilterBounds[0] = primitiveFilterBounds[1] = 0;
    primitiveFilterBounds[2] = primitiveFilterBounds[3] = -1;
    int bounds = faded.n64 && faded.textured && faded.depth < 3 &&
        PortN64TextureInfo(faded.pageX, faded.pageY, faded.depth, faded.clutX, faded.clutY,
                           vertices[0].u, vertices[0].v, primitiveFilterBounds, &faded.textureFlags);
    int nativeBounds[4] = {0, 0, -1, -1};
    int native = world && faded.textured && !faded.n64 &&
                 NativeFilterBounds(state, vertices, count, nativeBounds);
    const RenderFrame *render = RenderCurrent();
    RenderMaterial material = {faded.n64, faded.depth == 3, faded.textured, perspective, faded.raw, faded.dither,
                               bounds || native, !world};
    RenderStyle style = RenderPrepare(render, &material);
    if (style.nativeBounds) memcpy(primitiveFilterBounds, nativeBounds, sizeof(nativeBounds));
    faded.filtered = style.filter;
    faded.screenOverlay = !world;
    FindHd(state, vertices, count);
    Begin(state, count);
    for (i = 0; i < count; i++) {
        GpuVertex v = vertices[i];
        if (!perspective) {
            v.w = 0.0f;
        }
        if (stretch) {
            v.x = v.x * WideWidth() / SCREEN_COLUMNS - wideMargin;
        } else if (backdrop) {
            float zoom = (float)WideWidth() / SCREEN_COLUMNS, middle = areaY + areaH * 0.5f;

            v.x = SCREEN_COLUMNS * 0.5f + (v.x - SCREEN_COLUMNS * 0.5f) * zoom;
            v.y = middle + (v.y - middle) * zoom;
        }
        Add(&v, style.shade);
    }
}

/* A one-pixel line as a thin quad. */
void GpuLine(const GpuDrawState *state, const GpuVertex *a, const GpuVertex *b)
{
    GpuVertex q[4];
    float dx = b->x - a->x, dy = b->y - a->y;
    float length = SDL_sqrtf(dx * dx + dy * dy);
    float nx, ny;

    if (length < 0.001f) {
        dx = 1.0f;
        dy = 0.0f;
        length = 1.0f;
    }
    nx = -dy / length * 0.5f;
    ny = dx / length * 0.5f;
    q[0] = *a;
    q[1] = *a;
    q[2] = *b;
    q[3] = *b;
    q[0].x += nx + 0.5f;
    q[0].y += ny + 0.5f;
    q[1].x -= nx - 0.5f;
    q[1].y -= ny - 0.5f;
    q[2].x += nx + 0.5f;
    q[2].y += ny + 0.5f;
    q[3].x -= nx - 0.5f;
    q[3].y -= ny - 0.5f;
    q[0].w = q[1].w = q[2].w = q[3].w = 0.0f;
    GpuTriangles(state, q, 3);
    GpuTriangles(state, q + 1, 3);
}

void GpuFill(int x, int y, int w, int h, uint8_t r, uint8_t g, uint8_t b)
{
    int i, j;
    uint16_t pixel = (uint16_t)((r >> 3) | ((g >> 3) << 5) | ((b >> 3) << 10));

    Flush();
    /* video memory as well, for anything the game reads back or textures from */
    for (j = 0; j < h; j++) {
        for (i = 0; i < w; i++) {
            psxVram[(y + j) & (VRAM_HEIGHT - 1)][(x + i) & (VRAM_WIDTH - 1)] = pixel;
        }
    }
    GpuVramChanged(x, y, w, h);
    HdTexOverwritten(x, y, w, h);
    GpuVramOverwritten(x, y, w, h);
    ClearDrawn(x, y, w, h);
    GpuClearArea(x, y, w, h, r, g, b);
}

/* Clears (x, y, w, h) of the framebuffers: `buffers` (colour to r, g, b, depth to the farthest). */
static void Clear(int x, int y, int w, int h, GLbitfield buffers, int seedHdr, uint8_t r, uint8_t g, uint8_t b)
{
    Flush();
    /* Every cleared render pass needs its own sky, including interpolated
       pictures, which call GpuClearArea rather than GpuClearDepth. */
    saturnSkyDrawn = 0;
    if (buffers & GL_COLOR_BUFFER_BIT) {
        if (w > 0) PictureChanged(y, h);
        RememberPicture(x, y, SDL_min(x + w, SCREEN_COLUMNS) - x, h);
    }
    glBindFramebuffer(GL_FRAMEBUFFER, framebuffer);
    glEnable(GL_SCISSOR_TEST);
    glScissor(PixelX(x), PixelY(y), PixelX(x + w) - PixelX(x), PixelY(y + h) - PixelY(y));
    const GLfloat background[] = {r / 255.0f, g / 255.0f, b / 255.0f, 1.0f};
    glClearColor(background[0], background[1], background[2], background[3]);
    glDepthMask(GL_TRUE);
    glClear(buffers);
    if (buffers & GL_COLOR_BUFFER_BIT) ClearHdr(&sceneHdr, y, h, background);
    if (InScreenColumns(x, w) && wideFramebuffer) {
        /* across the whole screen: the sides too */
        int left = x <= 0 && x + w >= SCREEN_COLUMNS ? 0 : SDL_max(x, 0) + wideMargin;
        int right = x <= 0 && x + w >= SCREEN_COLUMNS ? WideWidth() : SDL_min(x + w, SCREEN_COLUMNS) + wideMargin;

        glBindFramebuffer(GL_FRAMEBUFFER, wideFramebuffer);
        glScissor(PixelX(left), PixelY(y), PixelX(right) - PixelX(left), PixelY(y + h) - PixelY(y));
        glClear(buffers);
        if (buffers & GL_COLOR_BUFFER_BIT) ClearHdr(&wideHdr, y, h, background);
        else if (seedHdr) SeedHdrRect(&wideHdr, wideColour, left, y, right, y + h);
    }
    if (seedHdr && !(buffers & GL_COLOR_BUFFER_BIT))
        SeedHdrRect(&sceneHdr, colour, x, y, SDL_min(x + w, SCREEN_COLUMNS), y + h);
    glBindFramebuffer(GL_FRAMEBUFFER, framebuffer);
    glDisable(GL_SCISSOR_TEST);
}

void GpuClearArea(int x, int y, int w, int h, uint8_t r, uint8_t g, uint8_t b)
{
    Clear(x, y, w, h, GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT, 0, r, g, b);
}

void GpuClearDepth(int x, int y, int w, int h, int mainTable)
{
    const RenderFrame *render = RenderCurrent();
    int seedHdr = mainTable && RenderResourcesFor(render).hdr &&
                  GpuRaceWide() && (sceneHdr.bands || wideHdr.bands);
    if (seedHdr && w > 0) PictureChanged(y, h);
    Clear(x, y, w, h, GL_DEPTH_BUFFER_BIT, seedHdr, 0, 0, 0);
}

/* Reads drawn pixels back into video memory (15-bit, each the framebuffer's
   pixel at the centre of the console pixel). */
void GpuReadBack(int x, int y, int w, int h)
{
    uint8_t *pixels;
    int i, j;

    if (w <= 0 || h <= 0) {
        return;
    }
    Flush();
    if (x < 0 || y < 0 || x + w > VRAM_WIDTH || y + h > VRAM_HEIGHT) {
        return;
    }
    SyncWide();
    pixels = malloc((size_t)w * h * 4);
    {
        /* shrunk to the console's pixels first, each the framebuffer's at its centre (a nearest blit samples
           there), so that little is read (a picture each frame for the video screens, Video) */
        static GLuint small, smallTexture;
        static int smallW, smallH;

        if (!small) {
            glGenFramebuffers(1, &small);
            glGenTextures(1, &smallTexture);
        }
        if (w > smallW || h > smallH) {
            smallW = SDL_max(w, smallW);
            smallH = SDL_max(h, smallH);
            glBindTexture(GL_TEXTURE_2D, smallTexture);
            glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, smallW, smallH, 0, GL_RGBA, GL_UNSIGNED_BYTE, NULL);
            glBindFramebuffer(GL_FRAMEBUFFER, small);
            glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, smallTexture, 0);
        }
        glBindFramebuffer(GL_READ_FRAMEBUFFER, framebuffer);
        glBindFramebuffer(GL_DRAW_FRAMEBUFFER, small);
        glDisable(GL_SCISSOR_TEST);
        glBlitFramebuffer(PixelX(x), PixelY(y), PixelX((x + w)), PixelY((y + h)), 0, 0, w, h, GL_COLOR_BUFFER_BIT,
                          GL_NEAREST);
        glBindFramebuffer(GL_READ_FRAMEBUFFER, small);
        glReadPixels(0, 0, w, h, GL_RGBA, GL_UNSIGNED_BYTE, pixels);
        glBindFramebuffer(GL_FRAMEBUFFER, framebuffer);
    }
    for (j = 0; j < h; j++) {
        for (i = 0; i < w; i++) {
            const uint8_t *p = pixels + ((size_t)j * w + i) * 4;
            uint16_t pixel = (uint16_t)((p[0] >> 3) | ((p[1] >> 3) << 5) | ((p[2] >> 3) << 10));
            if (!drawnBlocks[(y + j) / BLOCK][(x + i) / BLOCK]) {
                continue; /* uploaded, never drawn over: psxVram holds it */
            }
            psxVram[y + j][x + i] = (psxVram[y + j][x + i] & 0x8000) | pixel;
        }
    }
    free(pixels);
    GpuVramChanged(x, y, w, h);
}

/* Pixels the CPU wrote (LoadImage, MoveImage) go to the framebuffer too, so
   they are shown and drawn over as on the console. */
void GpuWriteFramebuffer(int x, int y, int w, int h)
{
    static GLuint texture, readFramebuffer;
    uint8_t *rgba;
    int i, j;

    if (w <= 0 || h <= 0 || x < 0 || y < 0 || x + w > VRAM_WIDTH || y + h > VRAM_HEIGHT) {
        return;
    }
    Flush();
    PictureChanged(y, h);
    RememberPicture(x, y, SDL_min(x + w, SCREEN_COLUMNS) - x, h);
    if (!texture) {
        glGenTextures(1, &texture);
        glGenFramebuffers(1, &readFramebuffer);
    }
    rgba = malloc((size_t)w * h * 4);
    for (j = 0; j < h; j++) {
        for (i = 0; i < w; i++) {
            uint16_t pixel = psxVram[y + j][x + i];
            uint8_t *out = rgba + ((size_t)j * w + i) * 4;
            out[0] = (uint8_t)((pixel & 31) << 3);
            out[1] = (uint8_t)(((pixel >> 5) & 31) << 3);
            out[2] = (uint8_t)(((pixel >> 10) & 31) << 3);
            out[3] = 255;
        }
    }
    glBindTexture(GL_TEXTURE_2D, texture);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 4);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, w, h, 0, GL_RGBA, GL_UNSIGNED_BYTE, rgba);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 2);
    free(rgba);
    glBindFramebuffer(GL_READ_FRAMEBUFFER, readFramebuffer);
    glFramebufferTexture2D(GL_READ_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, texture, 0);
    glBindFramebuffer(GL_DRAW_FRAMEBUFFER, framebuffer);
    glDisable(GL_SCISSOR_TEST);
    glBlitFramebuffer(0, 0, w, h, PixelX(x), PixelY(y), PixelX((x + w)), PixelY((y + h)), GL_COLOR_BUFFER_BIT, GL_NEAREST);
    glBindFramebuffer(GL_FRAMEBUFFER, framebuffer);
    ClearDrawn(x, y, w, h);
    SeedHdrRect(&sceneHdr, colour, x, y, SDL_min(x + w, SCREEN_COLUMNS), y + h);
    glBindFramebuffer(GL_DRAW_FRAMEBUFFER, framebuffer);
    CopyToWide(x, y, w, h);
}

/* MoveImage in the drawn picture: the rectangle copied at the display's
   scale (through a texture, as the two may overlap). */
void GpuMoveFramebuffer(int x, int y, int w, int h, int toX, int toY)
{
    static GLuint texture, readFramebuffer;

    if (w <= 0 || h <= 0 || x < 0 || y < 0 || toX < 0 || toY < 0 || x + w > VRAM_WIDTH || y + h > VRAM_HEIGHT ||
        toX + w > VRAM_WIDTH || toY + h > VRAM_HEIGHT) {
        return;
    }
    Flush();
    PictureChanged(toY, h);
    PictureOwner owners[2] = {pictureOwners[0], pictureOwners[1]};
    RememberPicture(toX, toY, SDL_min(toX + w, SCREEN_COLUMNS) - toX, h);
    if (x < SCREEN_COLUMNS && toX < SCREEN_COLUMNS) {
        for (int i = 0; i < 2; i++) {
            if (toY >= (i + 1) * 256 || toY + h <= i * 256) continue;
            int source = (y + SDL_max(toY, i * 256) - toY) / 256;
            if (owners[source].valid) {
                pictureOwners[i] = owners[source];
            }
        }
    }
    SyncWide();
    if (!texture) {
        glGenTextures(1, &texture);
        glGenFramebuffers(1, &readFramebuffer);
    }
    glBindFramebuffer(GL_READ_FRAMEBUFFER, framebuffer);
    glBindTexture(GL_TEXTURE_2D, texture);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, PixelX(x + w) - PixelX(x), PixelY(y + h) - PixelY(y), 0, GL_RGBA, GL_UNSIGNED_BYTE, NULL);
    glCopyTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, PixelX(x), PixelY(y), PixelX(x + w) - PixelX(x), PixelY(y + h) - PixelY(y));
    glBindFramebuffer(GL_READ_FRAMEBUFFER, readFramebuffer);
    glFramebufferTexture2D(GL_READ_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, texture, 0);
    glBindFramebuffer(GL_DRAW_FRAMEBUFFER, framebuffer);
    glDisable(GL_SCISSOR_TEST);
    glBlitFramebuffer(0, 0, PixelX(x + w) - PixelX(x), PixelY(y + h) - PixelY(y), PixelX(toX), PixelY(toY), PixelX((toX + w)), PixelY((toY + h)),
                      GL_COLOR_BUFFER_BIT, GL_NEAREST);
    glBindFramebuffer(GL_FRAMEBUFFER, framebuffer);
    /* both copies now agree in the destination */
    ClearDrawn(toX, toY, w, h);
    SeedHdrRect(&sceneHdr, colour, toX, toY, SDL_min(toX + w, SCREEN_COLUMNS), toY + h);
    glBindFramebuffer(GL_DRAW_FRAMEBUFFER, framebuffer);
    CopyToWide(toX, toY, w, h);
}

/* Video memory row 0 is drawn at the bottom of the framebuffer (OpenGL
   counts rows upward), so the displayed area is flipped when shown. */
static void Present(int x, int y, int w, int h, int interpolated);
static void AfterPicture(void);

static int displayShakeX, displayShakeY; /* (GpuDisplayShake) */

void GpuDisplayShake(int x, int y)
{
    displayShakeX = x;
    displayShakeY = y;
}

void GpuPresent(int x, int y, int w, int h)
{
    Present(x, y, w, h, 0);
}

void GpuPresentInterpolated(int x, int y, int w, int h)
{
    Present(x, y, w, h, 1);
}

/* Widescreen's bars beside a picture shown 4:3 (the start-up screens): the colour of the picture's column at x when it
   is one colour from top to bottom (the Red Bull screen's white), as if the picture went on; else black. */
static void BarColour(int x, int y, int h, float colour[3])
{
    static uint8_t *pixels;
    static size_t size;
    size_t need = (size_t)(PixelY(y + h) - PixelY(y)) * 4;
    size_t i;

    colour[0] = colour[1] = colour[2] = 0;
    if (need > size) {
        pixels = realloc(pixels, need);
        size = need;
    }
    glBindFramebuffer(GL_READ_FRAMEBUFFER, framebuffer);
    glPixelStorei(GL_PACK_ALIGNMENT, 4);
    glReadPixels(PixelX(x), PixelY(y), 1, PixelY(y + h) - PixelY(y), GL_RGBA, GL_UNSIGNED_BYTE, pixels);
    for (i = 4; i < need; i += 4) {
        if (memcmp(pixels + i, pixels, 3) != 0) {
            return;
        }
    }
    for (i = 0; i < 3; i++) {
        colour[i] = pixels[i] / 255.0f;
    }
}

/* Display and readback dispatch to the picture's owner, including linked bands.
   Presentation must leave the source framebuffer and VRAM untouched. */
static void BlitShown(int wide, int left, int top, int right, int bottom, int x0, int y0, int x1, int y1)
{
    int buffer = SDL_clamp(top / PixelY(256), 0, 1);
    const PictureOwner *owner = &pictureOwners[buffer];
    const RenderFrame *shown = owner->valid ? &owner->render : RenderCurrent();
    uint64_t revision = pictureRevision[buffer];
    if (top < PixelY(256) && bottom > PixelY(256))
        revision = SDL_max(pictureRevision[0], pictureRevision[1]);
    RenderPresent p = {wide ? wideColour : colour, wide ? wideFramebuffer : framebuffer,
                       revision, scale, owner->valid ? owner->race : GpuRaceWide(),
                       left, top, right, bottom, x0, y0, x1, y1, 0, 0, 0};
    const SceneHdr *hdr = wide ? &wideHdr : &sceneHdr;
    unsigned bands = (top < PixelY(256) ? 1u : 0u) | (bottom > PixelY(256) ? 2u : 0u);
    if ((hdr->bands & bands) == bands && (wide || right <= PixelX(SCREEN_COLUMNS))) {
        p.hdrTexture = hdr->textures[0];
        p.hdrMask = hdr->textures[1];
        if (wide) p.hdrWidth = PixelX(WideWidth());
    }
    const RenderPipeline *next = RenderGlPipeline(shown->id);
    /* Presenting an older buffer neither compiles a scene shader nor evicts the
       drawing renderer. Release resources once neither path uses their owner. */
    if (presentPipeline != next) {
        if (presentPipeline && presentPipeline != pipeline) presentPipeline->destroy();
        presentPipeline = next;
    }
    presentPipeline->present(RenderProfile(shown), &p);
}

static void Present(int x, int y, int w, int h, int interpolated)
{
    int width, height, outW, outH, outX, outY;

    Flush();
    if (interpolated) {
        PortVideoBetweenFrames();
    }
    PortDrawableSize(&width, &height);
    glBindFramebuffer(GL_DRAW_FRAMEBUFFER, windowFramebuffer);
    glDisable(GL_SCISSOR_TEST);
    glViewport(0, 0, width, height);
    glClearColor(0, 0, 0, 1);
    glClear(GL_COLOR_BUFFER_BIT);
    if (w > 0 && h > 0) {
        /* The console shows its picture at 4:3 whatever the width in pixels; the wide one is as much wider
           as its sides add. */
        int wide = wideMargin && x == 0 && w == SCREEN_COLUMNS;
        double across = wide ? WideWidth() : w;
        int left, right;

        if (wide && 240.0 * width / height < across) {
            across = 240.0 * width / height; /* the columns the window shows: the rest is cut off the sides */
        }
        outH = height;
        outW = (int)((double)height * 4 / 3 * across / w);
        if (outW > width) {
            outW = width;
            outH = (int)((double)width * 3 / 4 * w / across);
        }
        outX = (width - outW) / 2;
        outY = (height - outH) / 2;
        /* the console's jolt of the picture on the TV: a unit is 10 of the GPU's clocks, w/256 pixels; a line, one
           of the field's (an interlaced picture's two) */
        outX += (int)SDL_lround(displayShakeX * (w / 256.0) * outW / across);
        outY -= (int)SDL_lround(displayShakeY * (h > 288 ? 2.0 : 1.0) * outH / h);
        if (!wide) {
            SyncWide();
            SyncWideHdr();
        }
        left = wide ? PixelX((WideWidth() - across) / 2) : PixelX(x);
        right = wide ? PixelX(WideWidth()) - left : PixelX((x + w));
        glBindFramebuffer(GL_DRAW_FRAMEBUFFER, windowFramebuffer);
        BlitShown(wide, left, PixelY(y), right, PixelY((y + h)), outX, outY + outH, outX + outW, outY);
        if (!wide && portSettings.widescreen && outX > 0) {
            float colour[3];

            BarColour(x, y, h, colour);
            glEnable(GL_SCISSOR_TEST);
            glScissor(0, 0, outX, height);
            glClearColor(colour[0], colour[1], colour[2], 1);
            glClear(GL_COLOR_BUFFER_BIT);
            BarColour(x + w - 1, y, h, colour);
            glScissor(outX + outW, 0, width - outX - outW, height);
            glClearColor(colour[0], colour[1], colour[2], 1);
            glClear(GL_COLOR_BUFFER_BIT);
            glDisable(GL_SCISSOR_TEST);
        }
    }
    glBindFramebuffer(GL_FRAMEBUFFER, windowFramebuffer);
    PortVideoPresent();
    AfterPicture();
}

/* The second player's game handed its picture over instead of showing it (libgpu.c): as after a picture. */
void GpuPictureHandedOver(void)
{
    AfterPicture();
}

/* The framebuffers made again at another scale, from what they held (enlarged or reduced: the next picture
   is drawn at the new scale). */
static void SetScale(int newScale, int nativeW, int nativeH)
{
    GLuint oldColour = colour, oldFramebuffer = framebuffer, oldDepth = depthBuffer;
    int oldScale = scale, margin = wideMargin;
    float oldX = rasterX, oldY = rasterY;
    int oldWidth = PixelX(VRAM_WIDTH), oldHeight = PixelY(VRAM_HEIGHT);

    Flush();
    SetWideMargin(0); /* (its middle into video memory's first) */
    scale = newScale;
    rasterX = nativeW / 320.0f;
    rasterY = nativeH / (float)GPU_LINES;
    if (!CreateFramebuffer()) {
        glDeleteFramebuffers(1, &framebuffer);
        glDeleteTextures(1, &colour);
        glDeleteRenderbuffers(1, &depthBuffer);
        colour = oldColour;
        framebuffer = oldFramebuffer;
        depthBuffer = oldDepth;
        scale = oldScale;
        rasterX = oldX; rasterY = oldY;
    } else {
        glBindFramebuffer(GL_READ_FRAMEBUFFER, oldFramebuffer);
        glBindFramebuffer(GL_DRAW_FRAMEBUFFER, framebuffer);
        glDisable(GL_SCISSOR_TEST);
        glBlitFramebuffer(0, 0, oldWidth, oldHeight, 0, 0, PixelX(VRAM_WIDTH),
                          PixelY(VRAM_HEIGHT), GL_COLOR_BUFFER_BIT, scale < oldScale ? GL_LINEAR : GL_NEAREST);
        SceneHdr oldHdr = sceneHdr;
        sceneHdr = (SceneHdr){0};
        if (oldHdr.bands && CreateHdr(&sceneHdr, colour, depthBuffer, PixelX(VRAM_WIDTH))) {
            BlitHdr(&oldHdr, &sceneHdr, 0, 0, oldWidth, oldHeight, 0, 0, PixelX(VRAM_WIDTH), PixelY(VRAM_HEIGHT));
            sceneHdr.bands = oldHdr.bands;
        }
        DeleteHdr(&oldHdr);
        glDeleteFramebuffers(1, &oldFramebuffer);
        glDeleteTextures(1, &oldColour);
        glDeleteRenderbuffers(1, &oldDepth);
    }
    glBindFramebuffer(GL_FRAMEBUFFER, framebuffer);
    SetWideMargin(margin);
    HdTexSetScale((int)SDL_ceilf(SDL_max(ScaleX(), ScaleY())));
}

void GpuSetWindowFramebuffer(unsigned int number)
{
    windowFramebuffer = number;
}

void GpuClearWindow(float r, float g, float b)
{
    int width, height;

    Flush();
    PortDrawableSize(&width, &height);
    glBindFramebuffer(GL_FRAMEBUFFER, windowFramebuffer);
    glDisable(GL_SCISSOR_TEST);
    glViewport(0, 0, width, height);
    glClearColor(r, g, b, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT);
}

/* After a new picture: the window's shape, the setting or the end of a race may ask for another margin (a
   picture shown again, as each field shows one, or while the frame recorder holds the game, changes nothing),
   or another scale. */
void GpuUpdateResolution(void)
{
    if (!initialised) return;
    int width, height, wanted = ChooseScale();
    RenderNativeSize(RenderCurrent()->id, portPal, &width, &height);
    if (wanted != scale || rasterX != width / 320.0f || rasterY != height / (float)GPU_LINES)
        SetScale(wanted, width, height);
}

static void AfterPicture(void)
{
    pictures++;
    GpuUpdateResolution();
    if (drawnSincePresent) {
        picturesWithoutScene = sceneWide ? 0 : SDL_min(picturesWithoutScene + 1, 1000);
        picturesWithoutMenu = menuWide ? 0 : SDL_min(picturesWithoutMenu + 1, 1000);
        sceneWide = menuWide = 0;
        drawnSincePresent = 0;
        SetWideMargin(WantedMargin());
    }
}

/* A movie frame: drawn straight to the window through a texture of its own. */
/* The largest rectangle of aspect aw:ah inside (x, y, w, h), centred. */
static void Fit(int x, int y, int w, int h, int aw, int ah, int *ox, int *oy, int *ow, int *oh)
{
    *ow = w;
    *oh = w * ah / aw;
    if (*oh > h) {
        *oh = h;
        *ow = h * aw / ah;
    }
    *ox = x + (w - *ow) / 2;
    *oy = y + (h - *oh) / 2;
}

uint8_t *GpuReadArea(int x, int y, int w, int h, int *outW, int *outH)
{
    static uint8_t *pixels;
    static size_t size;
    size_t need = (size_t)(PixelX(x + w) - PixelX(x)) * (PixelY(y + h) - PixelY(y)) * 4;

    Flush();
    SyncWide();
    if (need > size) {
        pixels = realloc(pixels, need);
        size = need;
    }
    glBindFramebuffer(GL_READ_FRAMEBUFFER, framebuffer);
    glPixelStorei(GL_PACK_ALIGNMENT, 4);
    glReadPixels(PixelX(x), PixelY(y), PixelX(x + w) - PixelX(x), PixelY(y + h) - PixelY(y), GL_RGBA, GL_UNSIGNED_BYTE, pixels);
    glBindFramebuffer(GL_FRAMEBUFFER, windowFramebuffer);
    *outW = PixelX(x + w) - PixelX(x);
    *outH = PixelY(y + h) - PixelY(y);
    return pixels;
}

/* The frame recorder (framecap.c): an area of video memory as shown, scaled to outW x outH, RGBA, top row
   first. */
int GpuShownMargin(int x, int w)
{
    return wideMargin && x == 0 && w == SCREEN_COLUMNS ? wideMargin : 0;
}

/* The picture at (x, y, w, h) with its sides (GpuReadPicture) scaled to outW x outH, in a framebuffer bound for
   reading. */
static void ScalePicture(int x, int y, int w, int h, int margin, int outW, int outH)
{
    static GLuint small, smallTexture;
    static int smallW, smallH;
    int wide = margin && margin == GpuShownMargin(x, w);

    Flush();
    if (!wide) {
        SyncWide();
        SyncWideHdr();
    }
    if (!small || smallW != outW || smallH != outH) {
        if (!small) {
            glGenFramebuffers(1, &small);
            glGenTextures(1, &smallTexture);
        }
        glBindTexture(GL_TEXTURE_2D, smallTexture);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, outW, outH, 0, GL_RGBA, GL_UNSIGNED_BYTE, NULL);
        glBindFramebuffer(GL_FRAMEBUFFER, small);
        glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, smallTexture, 0);
        smallW = outW;
        smallH = outH;
    }
    glDisable(GL_SCISSOR_TEST);
    glBindFramebuffer(GL_READ_FRAMEBUFFER, wide ? wideFramebuffer : framebuffer);
    glBindFramebuffer(GL_DRAW_FRAMEBUFFER, small);
    if (wide) {
        BlitShown(1, 0, PixelY(y), PixelX(WideWidth()), PixelY((y + h)), 0, 0, outW, outH);
    } else {
        BlitShown(0, PixelX(x), PixelY(y), PixelX((x + w)), PixelY((y + h)), 0, 0, outW, outH);
    }
    glBindFramebuffer(GL_READ_FRAMEBUFFER, small);
    glPixelStorei(GL_PACK_ALIGNMENT, 4);
}

uint8_t *GpuReadPicture(int x, int y, int w, int h, int margin, int outW, int outH)
{
    static uint8_t *pixels;
    static size_t size;

    if ((size_t)outW * outH * 4 > size) {
        size = (size_t)outW * outH * 4;
        pixels = realloc(pixels, size);
    }
    ScalePicture(x, y, w, h, margin, outW, outH);
    glReadPixels(0, 0, outW, outH, GL_RGBA, GL_UNSIGNED_BYTE, pixels);
    glBindFramebuffer(GL_FRAMEBUFFER, windowFramebuffer);
    return pixels; /* video memory's rows go up the framebuffer: the first row read is the picture's top */
}

/* Pictures on their way back, one at a time for each reader (GpuRead*; GPU_READ_ASYNC: into a buffer, done
   when the fence is passed). */
static struct {
    int busy, w, h;
#ifdef GPU_READ_ASYNC
    GLuint buffer;
    GLsizeiptr bufferSize; /* (made again only for a larger picture: a driver may keep every one given up) */
    GLsync fence;
#else
    uint8_t *pixels;
    size_t size;
#endif
} reading[GPU_READERS];

int GpuReadPictureBusy(int reader)
{
    return reading[reader].busy;
}

int GpuReadPictureStart(int reader, int x, int y, int w, int h, int margin, int outW, int outH)
{
    if (reading[reader].busy) {
        return 0;
    }
#ifdef GPU_READ_ASYNC
    ScalePicture(x, y, w, h, margin, outW, outH);
    if (!reading[reader].buffer) {
        glGenBuffers(1, &reading[reader].buffer);
    }
    glBindBuffer(GL_PIXEL_PACK_BUFFER, reading[reader].buffer);
    if ((GLsizeiptr)outW * outH * 4 > reading[reader].bufferSize) {
        reading[reader].bufferSize = (GLsizeiptr)outW * outH * 4;
        glBufferData(GL_PIXEL_PACK_BUFFER, reading[reader].bufferSize, NULL, GL_STREAM_READ);
    }
    glReadPixels(0, 0, outW, outH, GL_RGBA, GL_UNSIGNED_BYTE, NULL);
    glBindBuffer(GL_PIXEL_PACK_BUFFER, 0);
    reading[reader].fence = glFenceSync(GL_SYNC_GPU_COMMANDS_COMPLETE, 0);
    glFlush();
    glBindFramebuffer(GL_FRAMEBUFFER, windowFramebuffer);
#else
    {
        /* (read at once: a copy of its own, as GpuReadPicture's buffer is shared) */
        size_t need = (size_t)outW * outH * 4;
        const uint8_t *pixels = GpuReadPicture(x, y, w, h, margin, outW, outH);

        if (need > reading[reader].size) {
            reading[reader].pixels = realloc(reading[reader].pixels, need);
            reading[reader].size = need;
        }
        memcpy(reading[reader].pixels, pixels, need);
    }
#endif
    reading[reader].w = outW;
    reading[reader].h = outH;
    reading[reader].busy = 1;
    return 1;
}

int GpuReadPictureDone(int reader, int wait, void (*use)(const uint8_t *pixels, int width, int height, void *user),
                       void *user)
{
    if (!reading[reader].busy) {
        return 0;
    }
#ifdef GPU_READ_ASYNC
    {
        GLenum status = glClientWaitSync(reading[reader].fence, wait ? GL_SYNC_FLUSH_COMMANDS_BIT : 0,
                                         wait ? 1000000000ull : 0);
        void *pixels;

        if (status == GL_TIMEOUT_EXPIRED) {
            return 0;
        }
        glDeleteSync(reading[reader].fence);
        reading[reader].fence = NULL;
        reading[reader].busy = 0;
        if (status == GL_WAIT_FAILED) {
            return 0;
        }
        glBindBuffer(GL_PIXEL_PACK_BUFFER, reading[reader].buffer);
        pixels = glMapBufferRange(GL_PIXEL_PACK_BUFFER, 0, (GLsizeiptr)reading[reader].w * reading[reader].h * 4,
                                  GL_MAP_READ_BIT);
        if (pixels) {
            use((const uint8_t *)pixels, reading[reader].w, reading[reader].h, user);
            glUnmapBuffer(GL_PIXEL_PACK_BUFFER);
        }
        glBindBuffer(GL_PIXEL_PACK_BUFFER, 0);
        return pixels != NULL;
    }
#else
    (void)wait;
    reading[reader].busy = 0;
    use(reading[reader].pixels, reading[reader].w, reading[reader].h, user);
    return 1;
#endif
}

void GpuPresentSplit(int x, int y, int w, int h, const uint8_t *lower, int lowerW, int lowerH, int lowerNew)
{
    static GLuint texture, readFramebuffer;
    static int textureW, textureH;
    int width, height, ox, oy, ow, oh;

    /* widescreen: each half as wide as the window, which is as wide as the picture with its sides (half the
       height across the same width is the band's shape) */
    int wide = GpuShownMargin(x, w) != 0, across = wide ? WideWidth() : w;

    Flush();
    if (!wide) {
        SyncWide();
        SyncWideHdr();
    }
    PortDrawableSize(&width, &height);
    /* the other game draws the lower half as wide, and hands it over at its size in pixels */
    PortLinkSetBand(GpuShownMargin(x, w), width, height / 2);
    glBindFramebuffer(GL_DRAW_FRAMEBUFFER, windowFramebuffer);
    glDisable(GL_SCISSOR_TEST);
    glViewport(0, 0, width, height);
    glClearColor(0, 0, 0, 1);
    glClear(GL_COLOR_BUFFER_BIT);
    /* the upper half: this game's band (OpenGL's rows go up: the upper half is from height / 2), its pixels as wide
       as the console's are (PAL's 256 lines across the same 4:3 screen: wider than tall) */
    Fit(0, height - height / 2, width, height / 2, across * GPU_LINES, h * 240, &ox, &oy, &ow, &oh);
    BlitShown(wide, wide ? 0 : PixelX(x), PixelY(y), PixelX((wide ? across : x + w)), PixelY((y + h)),
              ox, oy + oh, ox + ow, oy);
    /* the lower half: the other game's band, as it read it (rows from the bottom) */
    if (lower && lowerW > 0 && lowerH > 0) {
        if (!texture) {
            glGenTextures(1, &texture);
            glGenFramebuffers(1, &readFramebuffer);
        }
        glBindTexture(GL_TEXTURE_2D, texture);
        glPixelStorei(GL_UNPACK_ALIGNMENT, 4);
        if (lowerW != textureW || lowerH != textureH) {
            glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, lowerW, lowerH, 0, GL_RGBA, GL_UNSIGNED_BYTE, lower);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
            textureW = lowerW;
            textureH = lowerH;
        } else if (lowerNew) {
            glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, lowerW, lowerH, GL_RGBA, GL_UNSIGNED_BYTE, lower);
        }
        glPixelStorei(GL_UNPACK_ALIGNMENT, 2);
        Fit(0, 0, width, height / 2, lowerW, lowerH, &ox, &oy, &ow, &oh);
        glBindFramebuffer(GL_READ_FRAMEBUFFER, readFramebuffer);
        glFramebufferTexture2D(GL_READ_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, texture, 0);
        glBlitFramebuffer(0, 0, lowerW, lowerH, ox, oy + oh, ox + ow, oy, GL_COLOR_BUFFER_BIT, GL_LINEAR);
    }
    glBindFramebuffer(GL_FRAMEBUFFER, windowFramebuffer);
    PortVideoPresent();
    AfterPicture();
}

void GpuShowImage(const uint8_t *rgb, int width, int height)
{
    static GLuint texture, readFramebuffer;
    static int textureWidth, textureHeight;
    int windowWidth, windowHeight, outW, outH, outX, outY;

    Flush();
    if (!texture) {
        glGenTextures(1, &texture);
        glGenFramebuffers(1, &readFramebuffer);
    }
    glBindTexture(GL_TEXTURE_2D, texture);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    if (width != textureWidth || height != textureHeight) {
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGB8, width, height, 0, GL_RGB, GL_UNSIGNED_BYTE, rgb);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        textureWidth = width;
        textureHeight = height;
    } else {
        glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, width, height, GL_RGB, GL_UNSIGNED_BYTE, rgb);
    }
    glPixelStorei(GL_UNPACK_ALIGNMENT, 2);
    PortDrawableSize(&windowWidth, &windowHeight);
    {
        /* as large as the window takes it: a picture wider than 4:3 in square pixels, as the console's 320x240 (the
           PC's 320x192 movies), in its own shape; a narrower one as the console showed it, over the 4:3 screen */
        double aspect = (double)width / height < 4.0 / 3.0 ? 4.0 / 3.0 : (double)width / height;

        outW = windowWidth;
        outH = (int)(windowWidth / aspect + 0.5);
        if (outH > windowHeight) {
            outH = windowHeight;
            outW = (int)(windowHeight * aspect + 0.5);
        }
    }
    outX = (windowWidth - outW) / 2;
    outY = (windowHeight - outH) / 2;
    glBindFramebuffer(GL_DRAW_FRAMEBUFFER, windowFramebuffer);
    glDisable(GL_SCISSOR_TEST);
    glViewport(0, 0, windowWidth, windowHeight);
    glClearColor(0, 0, 0, 1);
    glClear(GL_COLOR_BUFFER_BIT);
    glBindFramebuffer(GL_READ_FRAMEBUFFER, readFramebuffer);
    glFramebufferTexture2D(GL_READ_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, texture, 0);
    /* image row 0 is the top: flip into OpenGL's upward rows */
    glBlitFramebuffer(0, 0, width, height, outX, outY + outH, outX + outW, outY, GL_COLOR_BUFFER_BIT, GL_LINEAR);
    glBindFramebuffer(GL_FRAMEBUFFER, windowFramebuffer);
    PortVideoPresent();
}

