/* Shared PlayStation rendering, independent of the graphics device.

   All of video memory is one framebuffer at an integer multiple of the
   console's resolution, so drawing areas, fills, the displayed area and
   copies keep the console's coordinates. Textures are fetched from a copy
   of video memory in the shader the way the console's GPU fetches them
   (4- and 8-bit texels through a palette, 15-bit direct, texel 0
   transparent, the top bit marking texels that take part in
   semi-transparency, texture windows), then modulated by the vertex colour
   as the console does. Semi-transparent primitives blend with the four
   PlayStation modes. Geometry is drawn at full resolution from the GTE's
   precise vertices, with perspective-correct texturing where the depth is
   known.

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
   and which texels of its page it covers; the shader samples the image there instead of video memory.

   Indexed pages are decoded once into a 16-bit texture array and invalidated when their source or palette
   changes. Geometry is collected across batch boundaries, uploaded together, then submitted as ordered
   depth/colour commands. Compatible consecutive commands share a draw; transfers end the sequence. */
#include <SDL3/SDL.h>
#include "../render/state.h"
#include "../render/modern/point_lights.h"
#include "../render/modern/post.h"
#include <math.h>
#include <stddef.h>
#include <stdlib.h>
#include <string.h>

#include "gpu.h"
#include "../hdtex.h"
#include "../link.h"
#include "../n64_material.h"
#include "../platform.h"
#include "../settings.h"
#include "../trace.h"

/* ---- state ---- */

static int scale = 1;
static int PixelX(float x) { return (int)SDL_lroundf(x * scale); }
static int PixelY(float y) { return (int)SDL_lroundf(y * scale); }
static int SpanX(float x, float w) { return PixelX(x + w) - PixelX(x); }
static int SpanY(float y, float h) { return PixelY(y + h) - PixelY(y); }
static RgHandle framebuffer, colour;   /* all of video memory, scaled */
typedef struct DepthUse {
    int written, left, top, right, bottom;
} DepthUse;
static DepthUse depthUse, wideDepthUse;
static RgHandle depthBuffer;           /* ... its depth (the depth buffer: Flush) */
static RgHandle vramTexture;           /* the CPU copy, R16UI */
static RgHandle glyphTexture;          /* the glyph page (GpuSetGlyphMemory): RGBA, premultiplied, filtered */
static RgHandle portPagesTexture;      /* the port's other pages (GpuSetPortPage), side by side: RGBA, filtered */
static RgHandle hdAtlas;               /* HD textures: the images in use (HdPlaced) */
static RgHandle program, vertexArray, vertexBuffer;
static int uniformVram, uniformGlyphs, uniformPortPages, uniformHdAtlas, uniformPass, uniformTarget;
static int uniformDepthPull, uniformDepthPass, uniformRasterScale;
static int vramDirty = 1;
static unsigned paletteEpoch = 1;
static struct {
    unsigned epoch, key;
    int passes;
} paletteCache[256];

static RgHandle windowFramebuffer;     /* the window's own: 0, but on iOS SDL's view has one of its own */
static int dirtyLeft, dirtyTop, dirtyRight, dirtyBottom;
static int areaX, areaY, areaW = VRAM_WIDTH, areaH = VRAM_HEIGHT;
static int initialised;
static uint64_t drawPass, skyPass, nextPictureRevision;
static struct {
    uint64_t revision;
    RenderFrame profile;
    int race;
} pictureOwners[2];
typedef struct SceneAuxiliary {
    ModernSurface hdr, mask, velocity;
    unsigned bands;
    int unavailable;
    uint64_t device;
} SceneAuxiliary;
static SceneAuxiliary sceneAux, wideAux;
static ModernMotion cameraMotion[2];
static int motionEligible;
void GpuMotionEligible(int eligible) { motionEligible = eligible; }

#define SCREEN_COLUMNS 320
#define MAX_WIDE_MARGIN 400
static int wideMargin;                         /* console pixels beyond each side; 0: no wide framebuffer */
static RgHandle wideFramebuffer, wideColour, wideDepth;
static int wideDirty;                          /* drawn into since its middle was copied to video memory's */
static int areaWide;                           /* the draw area is in the screen's columns: drawn wide */
static int sceneWide;                          /* the game built a race's scene since the last new picture */
static int drawnSincePresent;                  /* the game drew anything since the last picture shown */
static int interpolatingDraw;                  /* libgpu draws a picture between two of the game's frames */
static int picturesWithoutScene = 1000;        /* new pictures in a row that were not a race's */
static int menuWide;                           /* the game built a menu's backdrop since the last new picture */
static int picturesWithoutMenu = 1000;         /* ... and new pictures in a row that were not a menu's */
static int backdropNext;                       /* the primitive being drawn is a menu's backdrop (GpuBackdrop) */
static int uniformOrigin;

/* A batch of triangles drawn with one blending state. What else a primitive's draw state says
   (texture page, palette, texture window, colour depth) goes with each vertex, so that primitives of
   different textures, which the ordering tables interleave, are drawn together. */
#define BATCH_VERTICES 8192
typedef RenderModernVertex BatchVertex;
static RenderVertex baselineBatch[BATCH_VERTICES];
static BatchVertex batch[BATCH_VERTICES];
static int batchCount;
static GpuDrawState batchState;
static int batchBlend, batchBlendDirty;
typedef struct BlendRun {
    int first, blend;
} BlendRun;
static BlendRun batchBlendRuns[BATCH_VERTICES / 3 + 1];
static int batchBlendRunCount;
static BlendRun *queuedBlendRuns;
static int queuedBlendCount, queuedBlendCapacity;
static int batchDepth; /* a vertex of the batch can write the depth buffer (else Flush leaves out that pass) */
static int batchPalette; /* every primitive uses batchState's indexed palette and subtractive mode */
/* Collect geometry across blend/depth boundaries. Upload it once, then replay the
   original draw ranges in order. Transfers and public flushes end the sequence. */
typedef struct DrawRange {
    int first, count, depth, passes;
    /* Texture/material fields have already been packed into the vertices.
       Keep only command state, so disabled effects do not enlarge every range. */
    int semi, blend, depthPass, firstBlendRun, blendRunCount;
    const struct GpuLightSet *pointLights;
    float shieldGlow[4];
} DrawRange;
static unsigned char *queuedVertices;
static DrawRange *queuedRanges;
static int queuedCount, queuedCapacity, rangeCount, rangeCapacity;
static void SubmitRanges(void);

#define DECODED_PAGES 256
static RgHandle decodedTexture;
static int decodedCacheEnabled = 1;
static unsigned decodeSequence = 1;
static struct DecodedPage {
    uint64_t key;
    unsigned used;
    int valid;
    int x, y, clutX, clutY, depth;
} decodedPages[DECODED_PAGES];
enum { COVERAGE_BUCKETS = 256, COVERAGE_WAYS = 4 };
static struct TextureCoverage {
    uint64_t source;
    uint32_t region;
    RenderRect texture, palette;
    int valid, opaque;
} coverageCache[COVERAGE_BUCKETS * COVERAGE_WAYS];
static uint8_t coverageNextWay[COVERAGE_BUCKETS];
static struct TextureCoverage *lastCoverage;
static int coverageCacheUsed;
static void PagesChanged(int x, int y, int w, int h);
static void DecodeBatch(void);

/* The largest scale: the wide framebuffer at its widest within the GPU's textures, and at most 12 (the
   framebuffers then take some 560 MB). */
int GpuMaxScale(void)
{
    int size = 4096;

    /* A later backend may have different limits. The shared query reads the
       active device's capabilities without a driver call. */
    RgGetIntegerv(RG_MAX_TEXTURE_SIZE, &size);
    return SDL_clamp(size / (SCREEN_COLUMNS + 2 * MAX_WIDE_MARGIN), 1, 12);
}

/* The scale the window asks for: enough for its height, at least 1. */
int GpuWindowScale(void)
{
    int width, height;

    PortDrawableSize(&width, &height);
    return SDL_clamp((height + GPU_LINES - 1) / GPU_LINES, 1, SDL_min(8, GpuMaxScale()));
}

/* The scale wanted: the settings' resolution, or the window's. */
static int ChooseScale(void)
{
#ifdef PORT_MOBILE
    /* a phone or tablet: automatic stops at 3 (all of video memory at 3072x1536; its screen's 6 would be 75 MB
       drawn into); the settings' resolution goes higher */
    return portSettings.renderScale > 0 ? SDL_min(portSettings.renderScale, GpuMaxScale()) : SDL_min(GpuWindowScale(), 3);
#else
    return portSettings.renderScale > 0 ? SDL_min(portSettings.renderScale, GpuMaxScale()) : GpuWindowScale();
#endif
}

int GpuScale(void)
{
    return scale;
}

/* Video memory's framebuffer at the scale. */
/* A depth buffer of that size, attached to the framebuffer bound, cleared. */
static RgHandle DepthFor(int width, int height)
{
    RgHandle depth;

    RgGenRenderbuffers(1, &depth);
    RgBindRenderbuffer(RG_RENDERBUFFER, depth);
    RgRenderbufferStorage(RG_RENDERBUFFER, RG_DEPTH_COMPONENT24, width, height);
    RgFramebufferRenderbuffer(RG_FRAMEBUFFER, RG_DEPTH_ATTACHMENT, RG_RENDERBUFFER, depth);
    RgDisable(RG_SCISSOR_TEST);
    RgDepthMask(RG_TRUE);
    RgClear(RG_DEPTH_BUFFER_BIT);
    return depth;
}

static int CreateFramebuffer(void)
{
    RgGenTextures(1, &colour);
    RgBindTexture(RG_TEXTURE_2D, colour);
    RgTexImage2D(RG_TEXTURE_2D, 0, RG_RGBA8, PixelX(VRAM_WIDTH), PixelY(VRAM_HEIGHT), 0, RG_RGBA, RG_UNSIGNED_BYTE,
                 NULL);
    RgTexParameteri(RG_TEXTURE_2D, RG_TEXTURE_MIN_FILTER, RG_LINEAR);
    RgTexParameteri(RG_TEXTURE_2D, RG_TEXTURE_MAG_FILTER, RG_LINEAR);
    RgGenFramebuffers(1, &framebuffer);
    RgBindFramebuffer(RG_FRAMEBUFFER, framebuffer);
    RgFramebufferTexture2D(RG_FRAMEBUFFER, RG_COLOR_ATTACHMENT0, RG_TEXTURE_2D, colour, 0);
    depthBuffer = DepthFor(PixelX(VRAM_WIDTH), PixelY(VRAM_HEIGHT));
    depthUse.written = 0;
    if (RgCheckFramebufferStatus(RG_FRAMEBUFFER) != RG_FRAMEBUFFER_COMPLETE) {
        SDL_Log("framebuffer incomplete at scale %d", scale);
        return 0;
    }
    return 1;
}

int GpuInit(void)
{
    int ok;

    if (initialised) {
        return 1;
    }
    if (!RgInit()) {
        return 0;
    }
#ifdef __EMSCRIPTEN__
    /* A page/palette cache miss decodes all 65,536 texels on the CPU. The
       working set of a race churns this cache on older phones. Their existing
       raw VRAM sampler avoids that work and the 32 MiB array allocation. */
    decodedCacheEnabled = 0;
#endif
    const char *decodedCache = SDL_getenv("WIPEOUT_DECODED_CACHE");
    if (decodedCache) decodedCacheEnabled = SDL_atoi(decodedCache) != 0;
    program = RgProgram(RENDER_GAME);
    uniformVram = RgGetUniformLocation(program, "vram");
    uniformGlyphs = RgGetUniformLocation(program, "glyphs");
    uniformPortPages = RgGetUniformLocation(program, "portPages");
    uniformHdAtlas = RgGetUniformLocation(program, "hdAtlas");
    uniformPass = RgGetUniformLocation(program, "pass");
    uniformDepthPull = RgGetUniformLocation(program, "depthPull");
    uniformDepthPass = RgGetUniformLocation(program, "depthPass");
    uniformTarget = RgGetUniformLocation(program, "target");
    uniformOrigin = RgGetUniformLocation(program, "origin");
    uniformRasterScale = RgGetUniformLocation(program, "rasterScale");
    /* each sampler its own unit from the start (an integer and a float texture may not share one) */
    RgUseProgram(program);
    RgUniform1i(uniformVram, 0);
    RgUniform1i(uniformGlyphs, 1);
    RgUniform1i(uniformHdAtlas, 2);
    RgUniform1i(uniformPortPages, 3);
    RgUniform1i(RgGetUniformLocation(program, "decoded"), 4);

    RgGenBuffers(1, &vertexBuffer);
    RgBindBuffer(RG_ARRAY_BUFFER, vertexBuffer);
    RgPixelStorei(RG_UNPACK_ALIGNMENT, 2);
    RgPixelStorei(RG_PACK_ALIGNMENT, 1);
    RgGenTextures(1, &vramTexture);
    RgBindTexture(RG_TEXTURE_2D, vramTexture);
    /* both halves of video memory (gpu.h): the console's, then the second */
    RgTexImage2D(RG_TEXTURE_2D, 0, RG_R16UI, VRAM_WIDTH, VRAM_HEIGHT * 2, 0, RG_RED_INTEGER, RG_UNSIGNED_SHORT, NULL);
    RgTexSubImage2D(RG_TEXTURE_2D, 0, 0, 0, VRAM_WIDTH, VRAM_HEIGHT, RG_RED_INTEGER, RG_UNSIGNED_SHORT, psxVram);
    RgTexSubImage2D(RG_TEXTURE_2D, 0, 0, VRAM_HEIGHT, VRAM_WIDTH, VRAM_HEIGHT, RG_RED_INTEGER, RG_UNSIGNED_SHORT,
                    psxVramHigh);
    RgTexParameteri(RG_TEXTURE_2D, RG_TEXTURE_MIN_FILTER, RG_NEAREST);
    RgTexParameteri(RG_TEXTURE_2D, RG_TEXTURE_MAG_FILTER, RG_NEAREST);

    RgGenTextures(1, &decodedTexture);
    RgBindTexture(RG_TEXTURE_2D_ARRAY, decodedTexture);
    RgTexImage3D(RG_TEXTURE_2D_ARRAY, 0, RG_R16UI,
                 decodedCacheEnabled ? 256 : 1, decodedCacheEnabled ? 256 : 1,
                 decodedCacheEnabled ? DECODED_PAGES : 2, 0,
                 RG_RED_INTEGER, RG_UNSIGNED_SHORT, NULL);
    RgTexParameteri(RG_TEXTURE_2D_ARRAY, RG_TEXTURE_MIN_FILTER, RG_NEAREST);
    RgTexParameteri(RG_TEXTURE_2D_ARRAY, RG_TEXTURE_MAG_FILTER, RG_NEAREST);

    RgGenTextures(1, &glyphTexture);
    RgBindTexture(RG_TEXTURE_2D, glyphTexture);
    RgTexImage2D(RG_TEXTURE_2D, 0, RG_RGBA8, GLYPH_ATLAS_SIZE, GLYPH_ATLAS_SIZE, 0, RG_RGBA, RG_UNSIGNED_BYTE, NULL);
    RgTexParameteri(RG_TEXTURE_2D, RG_TEXTURE_MIN_FILTER, RG_LINEAR_MIPMAP_LINEAR);
    RgTexParameteri(RG_TEXTURE_2D, RG_TEXTURE_MAG_FILTER, RG_LINEAR);
    RgTexParameteri(RG_TEXTURE_2D, RG_TEXTURE_WRAP_S, RG_CLAMP_TO_EDGE);
    RgTexParameteri(RG_TEXTURE_2D, RG_TEXTURE_WRAP_T, RG_CLAMP_TO_EDGE);
    RgGenerateMipmap(RG_TEXTURE_2D); /* (complete before the glyphs come: a texture without its mipmaps is not) */
    /* the port's other pages: see-through until set */
    RgGenTextures(1, &portPagesTexture);
    RgBindTexture(RG_TEXTURE_2D, portPagesTexture);
    RgTexImage2D(RG_TEXTURE_2D, 0, RG_RGBA8, PORT_PAGES_ACROSS * 256, PORT_PAGES_DOWN * 256, 0, RG_RGBA,
                 RG_UNSIGNED_BYTE, NULL);
    RgTexParameteri(RG_TEXTURE_2D, RG_TEXTURE_MIN_FILTER, RG_LINEAR);
    RgTexParameteri(RG_TEXTURE_2D, RG_TEXTURE_MAG_FILTER, RG_LINEAR);
    RgTexParameteri(RG_TEXTURE_2D, RG_TEXTURE_WRAP_S, RG_CLAMP_TO_EDGE);
    RgTexParameteri(RG_TEXTURE_2D, RG_TEXTURE_WRAP_T, RG_CLAMP_TO_EDGE);
    /* the HD textures' atlas: one texel until there are images (a texture the shader can sample, as drivers want) */
    RgGenTextures(1, &hdAtlas);
    RgBindTexture(RG_TEXTURE_2D, hdAtlas);
    RgTexImage2D(RG_TEXTURE_2D, 0, RG_RGBA8, 1, 1, 0, RG_RGBA, RG_UNSIGNED_BYTE, "\0\0\0\0");
    RgTexParameteri(RG_TEXTURE_2D, RG_TEXTURE_MIN_FILTER, RG_NEAREST);
    RgTexParameteri(RG_TEXTURE_2D, RG_TEXTURE_MAG_FILTER, RG_NEAREST);

    scale = ChooseScale();
    HdTexSetScale(scale);
    if (!CreateFramebuffer()) {
        return 0;
    }
    RgViewport(0, 0, PixelX(VRAM_WIDTH), PixelY(VRAM_HEIGHT));
    RgClearColor(0, 0, 0, 1);
    RgClear(RG_COLOR_BUFFER_BIT);
    initialised = 1;
    return 1;
}

static void PalettesChanged(void)
{
    if (++paletteEpoch == 0) {
        memset(paletteCache, 0, sizeof(paletteCache));
        paletteEpoch = 1;
    }
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
    SubmitRanges(); /* completed batches sampled the previous GPU texture */
    /* A pending batch can still sample the new upload. Its earlier coverage proof is stale. */
    batchBlendDirty = 1;
    PalettesChanged();
    HdTexChanged(x, y, w, h);
    if (x < 0 || y < 0 || right > VRAM_WIDTH || bottom > VRAM_HEIGHT) {
        x = 0;
        y = 0;
        right = VRAM_WIDTH;
        bottom = VRAM_HEIGHT;
    }
    PagesChanged(x, y, right - x, bottom - y);
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
    RgActiveTexture(RG_TEXTURE3);
    RgBindTexture(RG_TEXTURE_2D, portPagesTexture);
    RgPixelStorei(RG_UNPACK_ALIGNMENT, 4);
    RgTexSubImage2D(RG_TEXTURE_2D, 0, (page - 1) % PORT_PAGES_ACROSS * 256, (page - 1) / PORT_PAGES_ACROSS * 256, 256,
                    256, RG_RGBA, RG_UNSIGNED_BYTE, rgba);
    RgPixelStorei(RG_UNPACK_ALIGNMENT, 2);
    RgActiveTexture(RG_TEXTURE0);
}

void GpuSetGlyphMemory(const uint32_t *rgba)
{
    SubmitRanges();
    RgActiveTexture(RG_TEXTURE1);
    RgBindTexture(RG_TEXTURE_2D, glyphTexture);
    RgPixelStorei(RG_UNPACK_ALIGNMENT, 4);
    RgTexSubImage2D(RG_TEXTURE_2D, 0, 0, 0, GLYPH_ATLAS_SIZE, GLYPH_ATLAS_SIZE, RG_RGBA, RG_UNSIGNED_BYTE, rgba);
    RgPixelStorei(RG_UNPACK_ALIGNMENT, 2);
    RgGenerateMipmap(RG_TEXTURE_2D);
    RgActiveTexture(RG_TEXTURE0);
}

static int vramHighDirty;

void GpuVramHighChanged(void)
{
    SubmitRanges();
    batchBlendDirty = 1;
    PagesChanged(0, VRAM_HEIGHT, VRAM_WIDTH, VRAM_HEIGHT);
    PalettesChanged();
    vramHighDirty = 1;
}

static void UploadVram(void)
{
    if (vramHighDirty) {
        RgBindTexture(RG_TEXTURE_2D, vramTexture);
        RgTexSubImage2D(RG_TEXTURE_2D, 0, 0, VRAM_HEIGHT, VRAM_WIDTH, VRAM_HEIGHT, RG_RED_INTEGER, RG_UNSIGNED_SHORT,
                        psxVramHigh);
        vramHighDirty = 0;
    }
    if (!vramDirty) {
        return;
    }
    RgBindTexture(RG_TEXTURE_2D, vramTexture);
    if (dirtyRight <= dirtyLeft) {
        RgTexSubImage2D(RG_TEXTURE_2D, 0, 0, 0, VRAM_WIDTH, VRAM_HEIGHT, RG_RED_INTEGER, RG_UNSIGNED_SHORT, psxVram);
    } else {
        RgPixelStorei(RG_UNPACK_ROW_LENGTH, VRAM_WIDTH);
        RgTexSubImage2D(RG_TEXTURE_2D, 0, dirtyLeft, dirtyTop, dirtyRight - dirtyLeft, dirtyBottom - dirtyTop,
                        RG_RED_INTEGER, RG_UNSIGNED_SHORT, &psxVram[dirtyTop][dirtyLeft]);
        RgPixelStorei(RG_UNPACK_ROW_LENGTH, 0);
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

static unsigned PictureBands(int top, int bottom)
{
    return ((top < 256 && bottom > 0) ? 1u : 0u) |
           ((top < VRAM_HEIGHT && bottom > 256) ? 2u : 0u);
}

static void PictureTouched(int x, int y, int w, int h)
{
    if (w <= 0 || h <= 0 || x >= SCREEN_COLUMNS + wideMargin || x + w <= -wideMargin) return;
    unsigned bands = PictureBands(y, y + h);
    uint64_t revision = ++nextPictureRevision;
    for (int band = 0; band < 2; band++) {
        if (!(bands & (1u << band))) continue;
        pictureOwners[band].revision = revision;
        pictureOwners[band].profile = *RenderCurrent();
        pictureOwners[band].race = GpuRaceWide();
    }
}

static void FreeSceneAux(SceneAuxiliary *scene)
{
    ModernSurfaceFree(&scene->hdr);
    ModernSurfaceFree(&scene->mask);
    ModernSurfaceFree(&scene->velocity);
    *scene = (SceneAuxiliary){0};
}

static int AllocateSceneAux(SceneAuxiliary *scene, int width, int withMotion)
{
    if (scene->device != RenderDeviceGeneration()) {
        scene->device = RenderDeviceGeneration();
        scene->unavailable = 0;
        for (int band = 0; band < 2; band++) cameraMotion[band].revision = 0;
    }
    if (scene->unavailable) return 0;
    int height = PixelY(VRAM_HEIGHT);
    if (!ModernSurfaceResize(&scene->hdr, width, height, RENDER_RGBA16_FLOAT) ||
        !ModernSurfaceResize(&scene->mask, width, height, RENDER_RGBA8)) {
        FreeSceneAux(scene);
        scene->unavailable = 1;
        scene->device = RenderDeviceGeneration();
        return 0;
    }
    if (withMotion && (scene->velocity.width != width || scene->velocity.height != height)) {
        if (ModernSurfaceResize(&scene->velocity, width, height, RENDER_RGBA16_FLOAT)) {
            RgBindFramebuffer(RG_DRAW_FRAMEBUFFER, scene->velocity.target);
            RgDisable(RG_SCISSOR_TEST);
            RgColorMask(1, 1, 1, 1);
            RgClearColor(0, 0, 0, 0);
            RgClear(RG_COLOR_BUFFER_BIT);
        }
    }
    return 1;
}

static void CopyPixels(RgHandle source, RgHandle dest, int x, int y, int w, int h, int toX)
{
    RgBindFramebuffer(RG_READ_FRAMEBUFFER, source);
    RgBindFramebuffer(RG_DRAW_FRAMEBUFFER, dest);
    RgDisable(RG_SCISSOR_TEST);
    RgBlitFramebuffer(x, y, x + w, y + h, toX, y, toX + w, y + h, RG_COLOR_BUFFER_BIT, RG_NEAREST);
}

static void SeedSceneRect(SceneAuxiliary *scene, RgHandle sdr, int x, int y, int w, int h)
{
    if (w <= 0 || h <= 0 || !scene->hdr.target) return;
    CopyPixels(sdr, scene->hdr.target, x, y, w, h, x);
    RgEnable(RG_SCISSOR_TEST);
    RgScissor(x, y, w, h);
    RgColorMask(1, 1, 1, 1);
    RgBindFramebuffer(RG_DRAW_FRAMEBUFFER, scene->mask.target);
    RgClearColor(0, 0, 0, 1);
    RgClear(RG_COLOR_BUFFER_BIT);
    if (scene->velocity.target) {
        RgBindFramebuffer(RG_DRAW_FRAMEBUFFER, scene->velocity.target);
        RgClearColor(0, 0, 0, 0);
        RgClear(RG_COLOR_BUFFER_BIT);
    }
    RgDisable(RG_SCISSOR_TEST);
}

static void SeedSceneBands(SceneAuxiliary *scene, RgHandle sdr, unsigned bands, int width)
{
    for (int band = 0; band < 2; band++) {
        unsigned bit = 1u << band;
        if (!(bands & bit) || (scene->bands & bit)) continue;
        int y = PixelY(band * 256), h = SpanY(band * 256, 256);
        SeedSceneRect(scene, sdr, 0, y, width, h);
        scene->bands |= bit;
    }
}

static void CopySceneCentre(SceneAuxiliary *source, SceneAuxiliary *dest, RgHandle sdr, int width, int from, int to)
{
    dest->bands &= source->bands;
    if (!source->bands || !AllocateSceneAux(dest, width, 0)) return;
    if (to) SeedSceneBands(dest, sdr, source->bands, width);
    int across = PixelX(SCREEN_COLUMNS);
    for (int band = 0; band < 2; band++) {
        unsigned bit = 1u << band;
        if (!(source->bands & bit)) continue;
        int y = PixelY(band * 256), h = SpanY(band * 256, 256);
        CopyPixels(source->hdr.target, dest->hdr.target, from, y, across, h, to);
        CopyPixels(source->mask.target, dest->mask.target, from, y, across, h, to);
        dest->bands |= bit;
    }
}

static void SeedWrittenPicture(int x, int y, int w, int h)
{
    if (w <= 0 || h <= 0 || x >= SCREEN_COLUMNS) return;
    int left = SDL_max(0, x), right = SDL_min(x + w, SCREEN_COLUMNS);
    unsigned bands = PictureBands(y, y + h);
    /* CPU writes invalidate motion and reseed just this rectangle; the remaining
       front picture retains its rendered policy. */
    uint64_t revision = ++nextPictureRevision;
    for (int band = 0; band < 2; band++) if (bands & (1u << band)) {
        if (!pictureOwners[band].revision) pictureOwners[band].profile = *RenderCurrent();
        pictureOwners[band].revision = revision;
        cameraMotion[band].revision = 0;
    }
    if (sceneAux.bands & bands)
        SeedSceneRect(&sceneAux, framebuffer, PixelX(left), PixelY(y), SpanX(left, right - left), SpanY(y, h));
    if (wideAux.bands & bands) {
        int whole = left == 0 && right == SCREEN_COLUMNS;
        int from = whole ? 0 : left + wideMargin, to = whole ? WideWidth() : right + wideMargin;
        SeedSceneRect(&wideAux, wideFramebuffer, PixelX(from), PixelY(y), SpanX(from, to - from), SpanY(y, h));
    }
    RgBindFramebuffer(RG_FRAMEBUFFER, framebuffer);
}

static int modernDrawState;
static void PrepareModernDraw(void)
{
    /* Once the last Modern-owned picture is gone, no policy/resource cleanup
       needs to run on baseline submissions until Modern is entered again. */
    if (!ModernEnabled() && !modernDrawState) return;
    modernDrawState = 1;
    const RenderFrame *frame = RenderCurrent();
    SceneAuxiliary *scene = areaWide ? &wideAux : &sceneAux;
    unsigned bands = areaW > 0 && areaH > 0 && areaX < SCREEN_COLUMNS ? PictureBands(areaY, areaY + areaH) : 0;
    RenderResources resources = RenderFrameResources(frame, PortLinkRole() != LINK_NONE, PortTracing());
    resources.cameraBlur &= motionEligible;
    RgGameAttachments(NULL, 0);
    if (!bands) return;
    int retained = 0;
    for (int band = 0; band < 2; band++) {
        RenderResources owner = RenderResourcesFor(&pictureOwners[band].profile);
        retained |= pictureOwners[band].race && (owner.hdr || owner.cameraBlur);
    }
    if (!retained && (!GpuRaceWide() || (!resources.hdr && !resources.cameraBlur))) {
        FreeSceneAux(&sceneAux); FreeSceneAux(&wideAux);
        for (int band = 0; band < 2; band++) {
            ModernSurfaceFree(&cameraMotion[band].depth);
            cameraMotion[band].revision = 0;
        }
        ModernPostReset();
        modernDrawState = 0;
        return;
    }
    if ((!resources.hdr && !resources.cameraBlur) || !GpuRaceWide() ||
        !AllocateSceneAux(scene, PixelX(areaWide ? WideWidth() : VRAM_WIDTH), resources.cameraBlur)) {
        scene->bands &= ~bands;
        return;
    }

    SeedSceneBands(scene, areaWide ? wideFramebuffer : framebuffer, bands,
                   PixelX(areaWide ? WideWidth() : SCREEN_COLUMNS));
    RgHandle auxiliary[3] = {scene->hdr.texture, scene->mask.texture,
                             resources.cameraBlur && scene->velocity.width ? scene->velocity.texture : 0};
    RgGameAttachments(auxiliary, frame->settings.modern.bloom ? 8.0f : 1.0f);
}

/* The middle of the wide framebuffer into video memory's, before that is read. */
static void SyncWide(void)
{
    if (!wideDirty) {
        return;
    }
    RgBindFramebuffer(RG_READ_FRAMEBUFFER, wideFramebuffer);
    RgBindFramebuffer(RG_DRAW_FRAMEBUFFER, framebuffer);
    RgDisable(RG_SCISSOR_TEST);
    RgBlitFramebuffer(PixelX(wideMargin), 0, PixelX(wideMargin + SCREEN_COLUMNS), PixelY(VRAM_HEIGHT), 0, 0,
                      PixelX(SCREEN_COLUMNS), PixelY(VRAM_HEIGHT), RG_COLOR_BUFFER_BIT, RG_NEAREST);
    CopySceneCentre(&wideAux, &sceneAux, framebuffer, PixelX(VRAM_WIDTH), PixelX(wideMargin), 0);
    RgBindFramebuffer(RG_FRAMEBUFFER, framebuffer);
    wideDirty = 0;
}

/* Clears rows y to y + h of the wide framebuffer beyond the screen's columns. */
static void ClearWideSides(int y, int h, float r, float g, float b)
{
    RgBindFramebuffer(RG_FRAMEBUFFER, wideFramebuffer);
    RgEnable(RG_SCISSOR_TEST);
    RgClearColor(r, g, b, 1.0f);
    RgScissor(0, PixelY(y), PixelX(wideMargin), SpanY(y, h));
    RgClear(RG_COLOR_BUFFER_BIT);
    RgScissor(PixelX(wideMargin + SCREEN_COLUMNS), PixelY(y), PixelX(wideMargin), SpanY(y, h));
    RgClear(RG_COLOR_BUFFER_BIT);
    RgDisable(RG_SCISSOR_TEST);
    RgBindFramebuffer(RG_FRAMEBUFFER, framebuffer);
}

/* What the CPU wrote into video memory's framebuffer, in the screen's columns, into the wide one too. A
   picture across the whole screen is shown as the console shows it, with black beside it. */
static void CopyToWide(int x, int y, int w, int h)
{
    int left = SDL_max(x, 0), right = SDL_min(x + w, SCREEN_COLUMNS);

    if (!InScreenColumns(x, w) || h <= 0) {
        return;
    }
    RgBindFramebuffer(RG_READ_FRAMEBUFFER, framebuffer);
    RgBindFramebuffer(RG_DRAW_FRAMEBUFFER, wideFramebuffer);
    RgDisable(RG_SCISSOR_TEST);
    RgBlitFramebuffer(PixelX(left), PixelY(y), PixelX(right), PixelY(y + h), PixelX(left + wideMargin), PixelY(y),
                      PixelX(right + wideMargin), PixelY(y + h), RG_COLOR_BUFFER_BIT, RG_NEAREST);
    RgBindFramebuffer(RG_FRAMEBUFFER, framebuffer);
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
    SyncWide();
    if (wideFramebuffer) {
        FreeSceneAux(&wideAux);
        RgDeleteFramebuffers(1, &wideFramebuffer);
        RgDeleteTextures(1, &wideColour);
        RgDeleteRenderbuffers(1, &wideDepth);
        wideFramebuffer = wideColour = wideDepth = 0;
    }
    wideMargin = margin;
    if (margin) {
        RgGenTextures(1, &wideColour);
        RgBindTexture(RG_TEXTURE_2D, wideColour);
        RgTexImage2D(RG_TEXTURE_2D, 0, RG_RGBA8, PixelX(WideWidth()), PixelY(VRAM_HEIGHT), 0, RG_RGBA, RG_UNSIGNED_BYTE,
                     NULL);
        RgTexParameteri(RG_TEXTURE_2D, RG_TEXTURE_MIN_FILTER, RG_LINEAR);
        RgTexParameteri(RG_TEXTURE_2D, RG_TEXTURE_MAG_FILTER, RG_LINEAR);
        RgGenFramebuffers(1, &wideFramebuffer);
        RgBindFramebuffer(RG_FRAMEBUFFER, wideFramebuffer);
        RgFramebufferTexture2D(RG_FRAMEBUFFER, RG_COLOR_ATTACHMENT0, RG_TEXTURE_2D, wideColour, 0);
        wideDepth = DepthFor(PixelX(WideWidth()), PixelY(VRAM_HEIGHT));
        wideDepthUse.written = 0;
        RgDisable(RG_SCISSOR_TEST);
        RgClearColor(0, 0, 0, 1);
        RgClear(RG_COLOR_BUFFER_BIT);
        CopyToWide(0, 0, SCREEN_COLUMNS, VRAM_HEIGHT);
        CopySceneCentre(&sceneAux, &wideAux, wideFramebuffer, PixelX(WideWidth()), 0, PixelX(margin));
    }
    RgBindFramebuffer(RG_FRAMEBUFFER, framebuffer);
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
static void BindTarget(void)
{
    if (areaWide) {
        /* a draw area across the screen reaches over the sides as well: a race's scene, and in a menu what fills
           them and the panels that slide out to the window's edges and back */
        int full = areaX == 0 && areaW == SCREEN_COLUMNS;

        RgBindFramebuffer(RG_FRAMEBUFFER, wideFramebuffer);
        RgViewport(0, 0, PixelX(WideWidth()), PixelY(VRAM_HEIGHT));
        RgEnable(RG_SCISSOR_TEST);
        RgScissor(full ? 0 : PixelX(areaX + wideMargin), PixelY(areaY), (full ? PixelX(WideWidth()) : SpanX(areaX + wideMargin, areaW)),
                  SpanY(areaY, areaH));
        RgUniform2f(uniformTarget, PixelX(WideWidth()) / (float)scale, PixelY(VRAM_HEIGHT) / (float)scale);
        RgUniform2f(uniformOrigin, -PixelX(wideMargin) / (float)scale, 0.0f);
        wideDirty = 1;
        drawnSincePresent |= !interpolatingDraw;
        return;
    }
    RgBindFramebuffer(RG_FRAMEBUFFER, framebuffer);
    RgViewport(0, 0, PixelX(VRAM_WIDTH), PixelY(VRAM_HEIGHT));
    RgEnable(RG_SCISSOR_TEST);
    RgScissor(PixelX(areaX), PixelY(areaY), SpanX(areaX, areaW), SpanY(areaY, areaH));
    RgUniform2f(uniformTarget, PixelX(VRAM_WIDTH) / (float)scale, PixelY(VRAM_HEIGHT) / (float)scale);
    RgUniform2f(uniformOrigin, 0.0f, 0.0f);
    drawnSincePresent |= !interpolatingDraw;
}

/* Whether two primitives can be drawn in one batch: the same blending and passes (what else their states say
   goes with the vertices). */
/* Whether a primitive needs blending, including partial coverage at filtered cutout edges. */
static int Blended(const GpuDrawState *s)
{
    return (s->semi >= 0 && s->semi != 4) || s->fade || (s->textured && s->depth == 3) ||
           (ModernEnabled() && s->textured && s->semi != 4 && (s->filtered & 3) != RENDER_NEAREST);
}

static int DepthPass(const GpuDrawState *s)
{
    return !(s->semi >= 0 && s->semi != 4 && !s->textured);
}

/* Baseline submission consumes only the original state and HUD-fill flag.
   Modern tails are never read while disabled, including command coalescing.
   Keeping their copies out of every triangle avoids charging opt-in material
   data to the common path. A boundary flush precedes every mode change. */
static void CopyDrawState(GpuDrawState *out, const GpuDrawState *in)
{
    if (ModernEnabled()) *out = *in;
    else {
        memcpy(out, in, offsetof(GpuDrawState, filtered));
        out->hudColourOverlay = in->hudColourOverlay;
    }
}

static int SameLights(const GpuDrawState *a, const GpuDrawState *b)
{
    return !ModernEnabled() || (a->pointLights == b->pointLights &&
        !memcmp(a->shieldGlow, b->shieldGlow, sizeof(a->shieldGlow)));
}

static int SameState(const GpuDrawState *a, const GpuDrawState *b, int blend)
{
    /* (the console's subtraction is a blend of its own; the opaque are drawn without blending, which is cheaper) */
    /* Filtered cutouts blend their edges but still write depth at full coverage. */
    return (a->semi == 2) == (b->semi == 2) && blend == batchBlend && SameLights(a, b) &&
           (!ModernEnabled() || DepthPass(a) == DepthPass(b));
}

/* The depth buffer's slack: what is drawn after may be this much farther (a fraction of its depth) than what is
   drawn before at that pixel and still be drawn over it, as in the game's order. */
#define DEPTH_SLACK 0.02f

enum { PALETTE_OPAQUE = 1, PALETTE_BLENDED = 2 };

/* A palette bounds the possible texels even when the texture wraps or uses a texture
   window. Check after UploadVram, against the same data the shader sees. Mixed palettes,
   direct-colour textures and HD replacements keep the original passes and draw ordering. */
static int PalettePasses(const GpuDrawState *s)
{
    unsigned key = (unsigned)(s->clutX & 1023) | (unsigned)(s->clutY & 1023) << 10 | (unsigned)s->depth << 20;
    unsigned slot = (key ^ (key >> 8) ^ (key >> 16)) & 255;
    int i, passes = 0, count = s->depth ? 256 : 16;

    if (paletteCache[slot].epoch == paletteEpoch && paletteCache[slot].key == key) {
        return paletteCache[slot].passes;
    }
    for (i = 0; i < count; i++) {
        int x = (s->clutX + i) & (VRAM_WIDTH - 1), y = s->clutY & (VRAM_HEIGHT - 1);
        uint16_t colour = s->clutY & VRAM_HEIGHT ? psxVramHigh[y][x] : psxVram[y][x];

        if (colour) {
            passes |= colour & 0x8000 ? PALETTE_BLENDED : PALETTE_OPAQUE;
        }
        if (passes == (PALETTE_OPAQUE | PALETTE_BLENDED)) {
            break;
        }
    }
    paletteCache[slot].epoch = paletteEpoch;
    paletteCache[slot].key = key;
    paletteCache[slot].passes = passes;
    return passes;
}

/* The shader wraps both texture and palette addresses over the two VRAM halves. */
static int WrappedOverlap(int a, int length, int b, int size)
{
    return length >= 1024 || size >= 1024 || ((a - b) & 1023) < size || ((b - a) & 1023) < length;
}

static void PagesChanged(int x, int y, int w, int h)
{
    for (int i = 0; coverageCacheUsed && i < COVERAGE_BUCKETS * COVERAGE_WAYS; i++) {
        struct TextureCoverage *entry = &coverageCache[i];
        if (!entry->valid) continue;
        RenderRect texture = entry->texture, palette = entry->palette;
        if ((WrappedOverlap(texture.x, texture.w, x, w) && WrappedOverlap(texture.y, texture.h, y, h)) ||
            (palette.w && WrappedOverlap(palette.x, palette.w, x, w) && WrappedOverlap(palette.y, 1, y, h)))
            entry->valid = 0;
    }
    for (int i = 0; i < DECODED_PAGES; i++) {
        struct DecodedPage *p = &decodedPages[i];
        if (!p->valid) continue;
        if ((WrappedOverlap(p->x, p->depth ? 128 : 64, x, w) && WrappedOverlap(p->y, 256, y, h)) ||
            (WrappedOverlap(p->clutX, p->depth ? 256 : 16, x, w) && WrappedOverlap(p->clutY, 1, y, h))) {
            p->valid = 0;
        }
    }
}

static uint16_t CpuTexel(int x, int y)
{
    return y & 512 ? psxVramHigh[y & 511][x & 1023] : psxVram[y & 511][x & 1023];
}

/* The region is in addressed UVs. Check actual palette indices so unused
   transparent entries do not put an opaque surface on the blended path. */
static int OpaqueTextureRegion(const GpuDrawState *s, const int uv[4])
{
    int pageX = s->pageX & 1023, pageY = s->pageY & 1023;
    int clutX = s->depth < 2 ? s->clutX & 1023 : 0, clutY = s->depth < 2 ? s->clutY & 1023 : 0;
    uint64_t source = (uint64_t)pageX | (uint64_t)pageY << 10 | (uint64_t)clutX << 20 |
                      (uint64_t)clutY << 30 | (uint64_t)s->depth << 40 | (uint64_t)(s->n64 != 0) << 42;
    uint32_t region = (uint32_t)uv[0] | (uint32_t)uv[1] << 8 | (uint32_t)uv[2] << 16 | (uint32_t)uv[3] << 24;
    if (lastCoverage && lastCoverage->valid && lastCoverage->source == source && lastCoverage->region == region)
        return lastCoverage->opaque;
    uint64_t key = source ^ ((uint64_t)region * UINT64_C(0x9e3779b97f4a7c15));
    unsigned bucket = (unsigned)(((key ^ key >> 32) * UINT64_C(0x9e3779b97f4a7c15)) >> 56);
    struct TextureCoverage *ways = &coverageCache[bucket * COVERAGE_WAYS], *entry = NULL;
    for (int i = 0; i < COVERAGE_WAYS; i++) {
        if (ways[i].valid && ways[i].source == source && ways[i].region == region) {
            lastCoverage = &ways[i];
            return lastCoverage->opaque;
        }
        if (!entry && !ways[i].valid) entry = &ways[i];
    }
    if (!entry) {
        entry = &ways[coverageNextWay[bucket]];
        coverageNextWay[bucket] = (coverageNextWay[bucket] + 1) % COVERAGE_WAYS;
    }

    int shift = 2 - s->depth;
    int paletteSize = s->depth == 0 ? 16 : s->depth == 1 ? 256 : 0;
    coverageCacheUsed = 1;
    lastCoverage = entry;
    *entry = (struct TextureCoverage){
        .source = source, .region = region, .valid = 1,
        .texture = {pageX + (uv[0] >> shift), pageY + uv[1],
                    (uv[2] >> shift) - (uv[0] >> shift) + 1, uv[3] - uv[1] + 1},
        .palette = {clutX, clutY, paletteSize, 1}
    };
    unsigned colourMask = s->n64 ? 0x7fffu : 0xffffu;
    uint8_t transparent[256];
    int hasTransparent = 0;
    for (int i = 0; i < paletteSize; i++) {
        transparent[i] = !(CpuTexel(clutX + i, clutY) & colourMask);
        hasTransparent |= transparent[i];
    }
    if (paletteSize && !hasTransparent) return entry->opaque = 1;
    for (int v = uv[1]; v <= uv[3]; v++) {
        for (int u = uv[0]; u <= uv[2]; u++) {
            unsigned word = CpuTexel(pageX + (u >> shift), pageY + v);
            if (paletteSize) {
                unsigned index = word >> ((u & ((1 << shift) - 1)) * (s->depth ? 8 : 4));
                if (transparent[index & (paletteSize - 1)]) return 0;
            } else if (!(word & colourMask)) {
                return 0;
            }
        }
    }
    return entry->opaque = 1;
}

/* Cache complete indexed pages as their original 16-bit texels. Colour conversion,
   modulation, texture windows and semi-transparency stay identical in the shader.
   Four-way buckets bound lookup work; a layer referenced by queued geometry is
   pinned until submission, with the original sampler as the overflow fallback. */
static int DecodedLayer(const RenderVertex *v)
{
    uint64_t key = (uint64_t)(v->texture[0] & 1023) | (uint64_t)(v->texture[1] & 1023) << 10 |
                   (uint64_t)(v->texture[2] & 1023) << 20 | (uint64_t)(v->texture[3] & 1023) << 30 |
                   (uint64_t)(v->mode[2] & 3) << 40;
    unsigned bucket = (unsigned)((key ^ (key >> 13) ^ (key >> 27)) * UINT64_C(0x9e3779b97f4a7c15) >> 58) * 4;
    int slot = -1;
    struct DecodedPage *page;
    static uint16_t pixels[256 * 256];

    for (int i = (int)bucket; i < (int)bucket + 4; i++) {
        if (decodedPages[i].key == key && decodedPages[i].valid) {
            decodedPages[i].used = decodeSequence;
            return i;
        }
        if (decodedPages[i].used != decodeSequence &&
            (slot < 0 || decodedPages[i].used < decodedPages[slot].used)) slot = i;
    }
    if (slot < 0) return -1;
    page = &decodedPages[slot];
    page->x = v->texture[0]; page->y = v->texture[1];
    page->clutX = v->texture[2]; page->clutY = v->texture[3];
    page->depth = v->mode[2] & 3;
    for (int y = 0; y < 256; y++) {
        for (int x = 0; x < 256; x++) {
            int shift = page->depth ? 1 : 2, bits = page->depth ? 8 : 4;
            unsigned word = CpuTexel(page->x + (x >> shift), page->y + y);
            unsigned index = word >> ((x & ((1 << shift) - 1)) * bits) & ((1 << bits) - 1);
            pixels[y * 256 + x] = CpuTexel(page->clutX + (int)index, page->clutY);
        }
    }
    RgActiveTexture(RG_TEXTURE4);
    RgBindTexture(RG_TEXTURE_2D_ARRAY, decodedTexture);
    RgTexSubImage3D(RG_TEXTURE_2D_ARRAY, 0, 0, 0, slot, 256, 256, 1, RG_RED_INTEGER, RG_UNSIGNED_SHORT, pixels);
    RgActiveTexture(RG_TEXTURE0);
    page->key = key;
    page->used = decodeSequence;
    page->valid = 1;
    return slot;
}

static void DecodeBatch(void)
{
    if (!decodedCacheEnabled || HdTexActive()) return;
    const int modern = ModernEnabled();
    for (int i = 0; i < batchCount; i += 3) {
        RenderVertex prefix, *v = &baselineBatch[i];
        if (modern) { memcpy(&prefix, &batch[i], sizeof(prefix)); v = &prefix; }
        if ((v->mode[0] & 3) && (v->mode[2] & 3) < 2 && !v->hd[2]) {
            int layer = DecodedLayer(v);
            if (layer >= 0) {
                for (int k = 0; k < 3; k++) {
                    if (modern) {
                        batch[i+k].mode[0] = (batch[i+k].mode[0] & ~3) | 2;
                        batch[i+k].texture[0] = (int16_t)layer;
                    } else {
                        v[k].mode[0] = (v[k].mode[0] & ~3) | 2;
                        v[k].texture[0] = (int16_t)layer;
                    }
                }
            }
        }
    }
}

static void FlushBatch(void)
{
    DrawRange *range;

    if (!batchCount) {
        return;
    }
    UploadVram();
    DecodeBatch();
    if (queuedCount + batchCount > queuedCapacity) {
        queuedCapacity = SDL_max(queuedCapacity * 2, queuedCount + batchCount);
        queuedVertices = SDL_realloc(queuedVertices, (size_t)queuedCapacity * sizeof(BatchVertex));
    }
    if (rangeCount == rangeCapacity) {
        rangeCapacity = SDL_max(rangeCapacity * 2, 256);
        queuedRanges = SDL_realloc(queuedRanges, (size_t)rangeCapacity * sizeof(*queuedRanges));
    }
    range = &queuedRanges[rangeCount++];
    range->first = queuedCount;
    range->count = batchCount;
    range->depth = batchDepth;
    range->semi = batchState.semi;
    range->blend = batchBlendDirty ? batchBlend : batchBlendRuns[0].blend;
    range->blendRunCount = batchBlendDirty || batchBlendRunCount == 1 ? 0 : batchBlendRunCount;
    if (range->blendRunCount) {
        if (queuedBlendCount + batchBlendRunCount > queuedBlendCapacity) {
            queuedBlendCapacity = SDL_max(queuedBlendCapacity * 2, queuedBlendCount + batchBlendRunCount);
            queuedBlendRuns = SDL_realloc(queuedBlendRuns, (size_t)queuedBlendCapacity * sizeof(*queuedBlendRuns));
        }
        range->firstBlendRun = queuedBlendCount;
        memcpy(queuedBlendRuns + queuedBlendCount, batchBlendRuns, (size_t)batchBlendRunCount * sizeof(*queuedBlendRuns));
        queuedBlendCount += batchBlendRunCount;
    }
    range->depthPass = DepthPass(&batchState);
    if (ModernEnabled()) {
        range->pointLights = batchState.pointLights;
        memcpy(range->shieldGlow, batchState.shieldGlow, sizeof(range->shieldGlow));
    }
    range->passes = batchPalette && !HdTexActive() ? PalettePasses(&batchState) : PALETTE_OPAQUE | PALETTE_BLENDED;
    size_t stride = ModernEnabled() ? sizeof(BatchVertex) : sizeof(RenderVertex);
    memcpy(queuedVertices + (size_t)queuedCount * stride,
           ModernEnabled() ? (const void *)batch : (const void *)baselineBatch, (size_t)batchCount * stride);
    queuedCount += batchCount;
    batchCount = 0;
    if (queuedCount >= 131072) SubmitRanges();
}

typedef struct DrawCommand {
    const DrawRange *state;
    int first, count, pass, blend; /* blend: 0 off, 1 premultiplied, 2 subtraction */
} DrawCommand;

/* Track a conservative bound of depth writes. Empty targets, including the
   regular VRAM target during widescreen drawing, need no repeated depth clear. */
static void NoteDepthWrite(void)
{
    DepthUse *use = areaWide ? &wideDepthUse : &depthUse;
    int full = areaWide && areaX == 0 && areaW == SCREEN_COLUMNS;
    int left = full ? 0 : SDL_max(areaX + (areaWide ? wideMargin : 0), 0);
    int right = full ? WideWidth() : SDL_min(areaX + areaW + (areaWide ? wideMargin : 0),
                                            areaWide ? WideWidth() : VRAM_WIDTH);
    int top = SDL_max(areaY, 0), bottom = SDL_min(areaY + areaH, VRAM_HEIGHT);
    if (right <= left || bottom <= top) return;
    if (!use->written) {
        *use = (DepthUse){1, left, top, right, bottom};
    } else {
        use->left = SDL_min(use->left, left); use->top = SDL_min(use->top, top);
        use->right = SDL_max(use->right, right); use->bottom = SDL_max(use->bottom, bottom);
    }
}

static RgBits NeededClear(DepthUse *use, int x, int y, int w, int h, RgBits buffers)
{
    if (!(buffers & RG_DEPTH_BUFFER_BIT)) return buffers;
    if (!use->written || x >= use->right || x + w <= use->left || y >= use->bottom || y + h <= use->top) {
        return buffers & ~RG_DEPTH_BUFFER_BIT;
    }
    if (x <= use->left && y <= use->top && x + w >= use->right && y + h >= use->bottom) use->written = 0;
    return buffers;
}

static void ExecuteCommand(const DrawCommand *command, int *previousPass, int *previousBlend)
{
    if (!command->count) return;
    if (command->pass != *previousPass) {
        int depth = command->pass == 3;
        RgUniform1i(uniformPass, command->pass);
        if (*previousPass < 0 || depth != (*previousPass == 3)) {
            RgUniform1i(uniformDepthPass, depth);
            RgColorMask(!depth, !depth, !depth, !depth);
            RgDepthMask(depth);
            RgDepthFunc(depth ? RG_LESS : RG_LEQUAL);
        }
        *previousPass = command->pass;
    }
    if (command->blend != *previousBlend) {
        if (!command->blend) {
            RgDisable(RG_BLEND);
        } else {
            RgEnable(RG_BLEND);
            RgBlendEquation(command->blend == 2 ? RG_FUNC_REVERSE_SUBTRACT : RG_FUNC_ADD);
            RgBlendFuncSeparate(RG_ONE, command->blend == 2 ? RG_ONE : RG_ONE_MINUS_SRC_ALPHA, RG_ZERO, RG_ONE);
        }
        *previousBlend = command->blend;
    }
    if (ModernEnabled()) {
        RenderLighting lighting = {0};
        const GpuLightSet *lights = command->state->pointLights;
        if (command->pass != 3 && lights) {
            lighting.count = SDL_clamp(lights->count, 0, RENDER_POINT_LIGHTS);
            for (int i = 0; i < lighting.count; i++) {
                const GpuPointLight *light = &lights->lights[i];
                memcpy(lighting.position[i], light->position, sizeof(light->position));
                memcpy(lighting.colour[i], light->colour, sizeof(light->colour));
                lighting.position[i][3] = SDL_max(light->radius, 1.0f);
                lighting.colour[i][3] = light->intensity;
            }
            memcpy(lighting.shield, command->state->shieldGlow, sizeof(lighting.shield));
        }
        RgLighting(&lighting);
    }
    RgDrawArrays(RG_TRIANGLES, command->first, command->count);
}

/* Coalesce only consecutive commands with identical raster state and contiguous
   vertices. In particular, a depth write or the opaque part of a mixed subtractive
   primitive remains a barrier. No triangles change order. */
static void AppendCommand(DrawCommand *pending, DrawCommand next, int *pass, int *blend)
{
    if (pending->count && pending->first + pending->count == next.first &&
        pending->pass == next.pass && pending->blend == next.blend &&
        (!ModernEnabled() || (pending->state->pointLights == next.state->pointLights &&
         !memcmp(pending->state->shieldGlow, next.state->shieldGlow, sizeof(next.state->shieldGlow))))) {
        pending->count += next.count;
    } else {
        ExecuteCommand(pending, pass, blend);
        *pending = next;
    }
}

static void SubmitRanges(void)
{
    DrawCommand pending = {0};
    int previousPass = -1, previousBlend = -1, depthNoted = 0;

    if (!rangeCount) return;
    PictureTouched(areaX, areaY, areaW, areaH);
    RgHandle nextProgram = RgProgram(ModernEnabled() ? RENDER_MODERN_SCENE : RENDER_GAME);
    if (program != nextProgram) {
        program = nextProgram;
        uniformPass = RgGetUniformLocation(program, "pass");
        uniformTarget = RgGetUniformLocation(program, "target");
        uniformOrigin = RgGetUniformLocation(program, "origin");
        uniformDepthPull = RgGetUniformLocation(program, "depthPull");
        uniformDepthPass = RgGetUniformLocation(program, "depthPass");
        uniformRasterScale = RgGetUniformLocation(program, "rasterScale");
    }
    PrepareModernDraw();
    RgGameGrid(1.0f, 1.0f);
    RgUseProgram(program);
    BindTarget();
    RgBindVertexArray(vertexArray);
    RgBindBuffer(RG_ARRAY_BUFFER, vertexBuffer);
    RgActiveTexture(RG_TEXTURE0);
    RgBindTexture(RG_TEXTURE_2D, vramTexture);
    RgActiveTexture(RG_TEXTURE1);
    RgBindTexture(RG_TEXTURE_2D, glyphTexture);
    RgActiveTexture(RG_TEXTURE2);
    RgBindTexture(RG_TEXTURE_2D, hdAtlas);
    RgActiveTexture(RG_TEXTURE3);
    RgBindTexture(RG_TEXTURE_2D, portPagesTexture);
    RgActiveTexture(RG_TEXTURE4);
    RgBindTexture(RG_TEXTURE_2D_ARRAY, decodedTexture);
    RgActiveTexture(RG_TEXTURE0);
    size_t stride = ModernEnabled() ? sizeof(BatchVertex) : sizeof(RenderVertex);
    RgBufferData(RG_ARRAY_BUFFER, (size_t)queuedCount * stride, queuedVertices, RG_STREAM_DRAW);
    RgUniform1f(uniformDepthPull, 1.0f / (1.0f - DEPTH_SLACK));
    RgUniform1f(uniformRasterScale, (float)scale);
    int useDepth = (ModernEnabled() || !portSettings.consoleDepth);
    if (!useDepth) RgDisable(RG_DEPTH_TEST);
    else RgEnable(RG_DEPTH_TEST);

    for (int i = 0; i < rangeCount; i++) {
        const DrawRange *range = &queuedRanges[i];
        int passes = range->passes;
        DrawCommand command = {range, range->first, range->count, 0, range->blend};

        /* Keep each original batch's depth prepass ahead of its colours. The
           two-percent depth slack and the game's ordering still decide visibility. */
        if (useDepth && range->depth && (passes & PALETTE_OPAQUE) &&
            range->depthPass) {
            command.pass = 3;
            command.blend = 0;
            if (!depthNoted) {
                NoteDepthWrite();
                depthNoted = 1;
            }
            AppendCommand(&pending, command, &previousPass, &previousBlend);
        }
        command.pass = 0;
        command.blend = range->blend;
        if (range->semi == 2) {
            if (passes & PALETTE_OPAQUE) {
                /* Without any semi texels, pass zero is equivalent to pass one
                   and can share a draw with the next ordinary colour command. */
                command.pass = passes & PALETTE_BLENDED ? 1 : 0;
                AppendCommand(&pending, command, &previousPass, &previousBlend);
            }
            if (passes & PALETTE_BLENDED) {
                command.pass = 2;
                command.blend = 2;
                AppendCommand(&pending, command, &previousPass, &previousBlend);
            }
        } else if (range->blendRunCount) {
            const BlendRun *runs = queuedBlendRuns + range->firstBlendRun;
            for (int run = 0; run < range->blendRunCount; run++) {
                int end = run + 1 < range->blendRunCount ? runs[run + 1].first : range->count;
                command.first = range->first + runs[run].first;
                command.count = end - runs[run].first;
                command.blend = runs[run].blend;
                AppendCommand(&pending, command, &previousPass, &previousBlend);
            }
        } else {
            AppendCommand(&pending, command, &previousPass, &previousBlend);
        }
    }
    ExecuteCommand(&pending, &previousPass, &previousBlend);
    RgGameAttachments(NULL, 0);
    RgDisable(RG_DEPTH_TEST);
    queuedCount = rangeCount = queuedBlendCount = 0;
    if (++decodeSequence == 0) {
        for (int i = 0; i < DECODED_PAGES; i++) decodedPages[i].used = 0;
        decodeSequence = 1;
    }
}

static void Flush(void)
{
    FlushBatch();
    SubmitRanges();
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

/* Subtractive textured primitives retain an opaque/blended pass boundary.
   SubmitRanges only combines the resulting commands when their order permits it. */
static GpuDrawState primitiveState; /* the primitive being added */
static int primitiveHdOpaque;
static uint16_t primitiveHd[4];     /* ... its HD texture (BatchVertex) */
static int16_t primitiveHdTexels[4];
static int primitiveFilterBounds[4];

/* ---- HD textures: the images in use, side by side in one texture ---- */

static int hdAtlasSize;
static unsigned hdEpoch;            /* the images' (HdTexEpoch) the atlas holds */
static unsigned hdFill = 1;         /* the atlas's filling: an image is in it when hdPlaced[] says this one */
static unsigned *hdPlaced;
typedef struct HdPlacement {
    int x, y, w, h, opaque;
} HdPlacement;
static HdPlacement *hdPlace;                /* x, y, w, h of each image in it */
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
static const HdPlacement *HdPlaced(int image)
{
    const uint8_t *pixels = NULL;
    int w, h, size;

    if (HdTexEpoch() != hdEpoch || HdTexImageCount() != hdImages) {
        /* other images (another scale, another pack): an atlas for them */
        size = SDL_min(scale <= 6 ? 4096 : 8192, GpuMaxScale() * (SCREEN_COLUMNS + 2 * MAX_WIDE_MARGIN));
        HdEmpty();
        hdEpoch = HdTexEpoch();
        hdImages = HdTexImageCount();
        hdPlaced = SDL_realloc(hdPlaced, sizeof(unsigned) * (size_t)SDL_max(hdImages, 1));
        hdPlace = SDL_realloc(hdPlace, sizeof(*hdPlace) * (size_t)SDL_max(hdImages, 1));
        memset(hdPlaced, 0, sizeof(*hdPlaced) * (size_t)SDL_max(hdImages, 1));
        memset(hdPlace, 0, sizeof(*hdPlace) * (size_t)SDL_max(hdImages, 1));
        if (size != hdAtlasSize) {
            if (!hdAtlas) {
                RgGenTextures(1, &hdAtlas);
            }
            RgBindTexture(RG_TEXTURE_2D, hdAtlas);
            RgTexImage2D(RG_TEXTURE_2D, 0, RG_RGBA8, size, size, 0, RG_RGBA, RG_UNSIGNED_BYTE, NULL);
            RgTexParameteri(RG_TEXTURE_2D, RG_TEXTURE_MIN_FILTER, RG_NEAREST);
            RgTexParameteri(RG_TEXTURE_2D, RG_TEXTURE_MAG_FILTER, RG_NEAREST);
            hdAtlasSize = size;
        }
    }
    if (image >= hdImages) {
        return NULL;
    }
    HdPlacement *place = &hdPlace[image];
    if (hdPlaced[image] != hdFill) {
        if (!(pixels = HdTexImage(image, &w, &h)) || w > hdAtlasSize || h > hdAtlasSize) {
            return NULL;
        }
        if (hdShelfX + w > hdAtlasSize) {
            hdShelfY += hdShelfH;
            hdShelfX = hdShelfH = 0;
        }
        if (hdShelfY + h > hdAtlasSize) {
            /* full: emptied for the images in use now (once a picture: past that some go without) */
            if (hdEmptiedAt == pictures) {
                return NULL;
            }
            HdEmpty();
            hdEmptiedAt = pictures;
        }
        RgBindTexture(RG_TEXTURE_2D, hdAtlas);
        RgPixelStorei(RG_UNPACK_ALIGNMENT, 4);
        RgTexSubImage2D(RG_TEXTURE_2D, 0, hdShelfX, hdShelfY, w, h, RG_RGBA, RG_UNSIGNED_BYTE, pixels);
        RgPixelStorei(RG_UNPACK_ALIGNMENT, 2);
        *place = (HdPlacement){hdShelfX, hdShelfY, w, h, place->opaque};
        hdPlaced[image] = hdFill;
        hdShelfX += w;
        hdShelfH = SDL_max(hdShelfH, h);
    }
    if (ModernEnabled() && !place->opaque) {
        if (!pixels) pixels = HdTexImage(image, &w, &h);
        if (pixels && w == place->w && h == place->h) {
            place->opaque = 1;
            for (size_t i = 0; i < (size_t)place->w * place->h; i++) {
                if (pixels[i * 4 + 3] != 255) {
                    place->opaque = -1;
                    break;
                }
            }
        }
    }
    return place;
}

/* The primitive's HD texture, if the pack has one for its texels. */
static void FindHd(const GpuDrawState *state, const GpuVertex *vertices, int count)
{
    float u0 = vertices[0].u, v0 = vertices[0].v, u1 = u0, v1 = v0;
    HdTexHit hit;
    int i;

    primitiveHdOpaque = 0;
    memset(primitiveHd, 0, sizeof(primitiveHd));
    if (!state->textured || state->depth > 3 ||
        (state->depth != 3 && (!HdTexActive() || (scale == 1 && !RenderResourcesFor(RenderCurrent()).hdTexturesAtNativeScale)))) {
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
    if (!HdTexFind(state->pageX, state->pageY, state->depth, state->clutX, state->clutY, u0, v0, u1, v1, &hit))
        return;
    const HdPlacement *place = HdPlaced(hit.image);
    if (!place) return;
    /* The image's part over the replaced texels. */
    primitiveHd[0] = (uint16_t)(place->x + hit.srcX * place->w / hit.nameW);
    primitiveHd[1] = (uint16_t)(place->y + hit.srcY * place->h / hit.nameH);
    primitiveHd[2] = (uint16_t)SDL_max(hit.w * place->w / hit.nameW, 1);
    primitiveHd[3] = (uint16_t)SDL_max(hit.h * place->h / hit.nameH, 1);
    primitiveHdOpaque = place->opaque > 0;
    primitiveHdTexels[0] = (int16_t)hit.x;
    primitiveHdTexels[1] = (int16_t)hit.y;
    primitiveHdTexels[2] = (int16_t)hit.w;
    primitiveHdTexels[3] = (int16_t)hit.h;
}


static void Begin(const GpuDrawState *state, int vertices, int blend)
{
    if (batchCount && (!SameState(state, &batchState, blend) || batchCount + vertices > BATCH_VERTICES ||
                       (state->textured && state->semi == 2))) {
        FlushBatch();
    }
    if (!batchCount) {
        CopyDrawState(&batchState, state);
        batchBlend = blend;
        batchBlendDirty = 0;
        batchBlendRunCount = 1;
        batchBlendRuns[0] = (BlendRun){0, blend};
        batchDepth = 0;
        batchPalette = state->textured && state->semi == 2 && !primitiveHd[2] &&
                       (state->depth == 0 || state->depth == 1);
    } else if (!state->textured || state->semi != 2 || state->depth != batchState.depth ||
               state->clutX != batchState.clutX || state->clutY != batchState.clutY || primitiveHd[2]) {
        batchPalette = 0;
    }
    CopyDrawState(&primitiveState, state);
}

/* Coverage only changes the colour commands. Keep the whole batch's
   depth prepass ahead of those commands, including across cutout edges. */
static void SelectColourBlend(int blend)
{
    if (batchBlendDirty || batchBlendRuns[batchBlendRunCount - 1].blend == blend) return;
    if (batchCount) batchBlendRunCount++;
    batchBlendRuns[batchBlendRunCount - 1] = (BlendRun){batchCount, blend};
}

static void AddExisting(const GpuVertex *v)
{
    RenderVertex *out = &baselineBatch[batchCount++];

    out->x = v->x;
    out->y = v->y;
    out->w = v->w > 0.0f && v->noDepth ? -v->w : v->w; /* (the vertex shader's position.z) */
    batchDepth |= out->w > 0.0f && v->a == 255;
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
    out->mode[1] = (uint8_t)primitiveState.raw;
    out->mode[2] = (uint8_t)primitiveState.depth;
    /* (the vertex shader's: a polygon seen from behind; the blending) */
    out->mode[3] = (uint8_t)((v->backFacing || v->exactDepth) |
                           (primitiveState.hudColourOverlay != 0) << 1 | (primitiveState.semi + 1) << 4);
    memcpy(out->hd, primitiveHd, sizeof(out->hd));
    memcpy(out->hdTexels, primitiveHdTexels, sizeof(out->hdTexels));
}

static void Add(const GpuVertex *v, unsigned shade, float brightness, int pcTint)
{
    BatchVertex *out = &batch[batchCount++];

    out->x = v->x;
    out->y = v->y;
    out->w = v->w > 0.0f && v->noDepth ? -v->w : v->w; /* (the vertex shader's position.z) */
    batchDepth |= out->w > 0.0f && v->a == 255;
    out->r = v->r * (brightness / 255.0f);
    out->g = v->g * (brightness / 255.0f);
    out->b = v->b * (brightness / 255.0f);
    out->a = v->a / 255.0f;
    memcpy(out->view, v->view, sizeof(v->view));
    out->view[3] = v->w > 0 && v->view[2] != 0 && !v->noDepth &&
                   primitiveState.semi < 0 && !primitiveState.raw && primitiveState.pointLights != NULL;
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
    out->mode[0] = (uint8_t)(primitiveState.textured | (primitiveState.filtered & 12) | (primitiveState.emission.enabled ? 16 : 0) |
                            (primitiveState.screenOverlay ? 32 : 0) | (primitiveState.hullMotion ? 64 : 0) |
                            (primitiveState.sparkStreak ? 128 : 0));
    for (int i = 0; i < 4; i++) out->emission[i] = (int16_t)primitiveState.emission.uv[i];
    if (primitiveState.hullMotion) memcpy(out->shutter, v->shutterDelta, sizeof(out->shutter));
    else memset(out->shutter, 0, sizeof(out->shutter));
    out->mode[1] = (uint8_t)shade;
    out->mode[2] = (uint8_t)(primitiveState.depth | (primitiveState.textureFlags >> 2) |
                            (primitiveState.ps1Light ? 32 : 0) | (pcTint ? 64 : 0));
    /* (the vertex shader's: a polygon seen from behind; the blending) */
    out->mode[3] = (uint8_t)((v->backFacing || v->exactDepth) |
                           (primitiveState.hudColourOverlay != 0) << 1 | (primitiveState.filtered & 3) << 2 |
                           (primitiveState.semi + 1) << 4);
    for (int i = 0; i < 4; i++) out->filterBounds[i] = (int16_t)primitiveFilterBounds[i];
    memcpy(out->hd, primitiveHd, sizeof(out->hd));
    memcpy(out->hdTexels, primitiveHdTexels, sizeof(out->hdTexels));
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

/* A wrapped range is conservatively covered by the whole axis. Texture
   windows can remap it again, so find the bounds of the remapped texels. */
static void FilterAxis(float low, float high, float offset, int mask, int window, int *first, int *last)
{
    int lo = (int)floorf(low - offset - 0.0001f), hi = (int)floorf(high - offset + 0.0001f) + 1;
    if (lo < 0 || hi > 255) { lo = 0; hi = 255; }
    if (!mask) {
        *first = lo;
        *last = hi;
        return;
    }
    *first = 255;
    *last = 0;
    for (int i = lo; i <= hi; i++) {
        int at = (i & ~(mask * 8)) | ((window & mask) * 8);
        *first = SDL_min(*first, at);
        *last = SDL_max(*last, at);
    }
}

static int FilteredTextureOpaque(const GpuDrawState *s, const GpuVertex *vertices, int count)
{
    int filter = s->filtered & 3;
    if (!s->textured || s->depth < 0 || s->depth >= 3 || s->semi >= 0 || s->fade ||
        filter == RENDER_NEAREST || (primitiveHd[2] && !primitiveHdOpaque))
        return 0;
    const int *bounds = primitiveFilterBounds;
    if (bounds[2] >= bounds[0]) {
        return bounds[0] >= 0 && bounds[1] >= 0 && bounds[2] <= 255 && bounds[3] <= 255 &&
               bounds[3] >= bounds[1] && OpaqueTextureRegion(s, bounds);
    }
    float left = vertices[0].u, right = left, top = vertices[0].v, bottom = top;
    for (int i = 0; i < count; i++) {
        float u = vertices[i].u, v = vertices[i].v;
        if (!(u >= 0 && u <= 256 && v >= 0 && v <= 256)) return 0;
        left = SDL_min(left, u);
        right = SDL_max(right, u);
        top = SDL_min(top, v);
        bottom = SDL_max(bottom, v);
    }
    float offset = filter == RENDER_BILINEAR ? 0.5f : 0.0f;
    int maskX = s->n64 ? 0 : s->windowMaskX, maskY = s->n64 ? 0 : s->windowMaskY;
    if ((unsigned)maskX > 31 || (unsigned)maskY > 31) return 0;
    int uv[4];
    FilterAxis(left, right, offset, maskX, s->windowOffsetX, &uv[0], &uv[2]);
    FilterAxis(top, bottom, offset, maskY, s->windowOffsetY, &uv[1], &uv[3]);
    return OpaqueTextureRegion(s, uv);
}

void GpuTriangles(const GpuDrawState *given, const GpuVertex *vertices, int count)
{
    GpuDrawState faded;
    CopyDrawState(&faded, given);
    const GpuDrawState *state = &faded;
    int i;
    int perspective = 1, stretch, backdrop;
    float minX = vertices[0].x, minY = vertices[0].y, maxX = minX, maxY = minY;

    for (i = 1; i < count; i++) {
        minX = SDL_min(minX, vertices[i].x);
        minY = SDL_min(minY, vertices[i].y);
        maxX = SDL_max(maxX, vertices[i].x);
        maxY = SDL_max(maxY, vertices[i].y);
    }
    MarkDrawn(minX, minY, maxX, maxY);
    const RenderFrame *frame = ModernEnabled() ? RenderCurrent() : NULL;
    for (i = 0; i < count; i++) {
        faded.fade |= vertices[i].a < 255;
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
    {
        float left = areaWide && areaX == 0 && areaW == SCREEN_COLUMNS ? (float)-wideMargin : (float)areaX;
        float right = areaWide && areaX == 0 && areaW == SCREEN_COLUMNS ? (float)(SCREEN_COLUMNS + wideMargin)
                                                                      : (float)(areaX + areaW);
        float x0 = minX, x1 = maxX, y0 = minY, y1 = maxY;
        if (stretch) {
            x0 = x0 * WideWidth() / SCREEN_COLUMNS - wideMargin;
            x1 = x1 * WideWidth() / SCREEN_COLUMNS - wideMargin;
        } else if (backdrop) {
            float zoom = (float)WideWidth() / SCREEN_COLUMNS, middle = areaY + areaH * 0.5f;
            x0 = SCREEN_COLUMNS * 0.5f + (x0 - SCREEN_COLUMNS * 0.5f) * zoom;
            x1 = SCREEN_COLUMNS * 0.5f + (x1 - SCREEN_COLUMNS * 0.5f) * zoom;
            y0 = middle + (y0 - middle) * zoom;
            y1 = middle + (y1 - middle) * zoom;
        }
        if (x1 < left - 1 || x0 > right + 1 || y1 < areaY - 1 || y0 > areaY + areaH + 1) {
            /* Keep the original batch boundary: merging its neighbours could
               change how their depth prepasses interact, even though this
               primitive itself cannot cover a sample in the draw area. */
            Begin(state, count, Blended(state));
            drawnSincePresent |= !interpolatingDraw;
            if (areaWide) wideDirty = 1;
            return;
        }
    }
    RenderStyle style = {0};
    float brightness = 1.0f;
    if (frame) {
        RenderMaterial material = GpuMaterialFor(state, vertices, count);
        faded.n64 = material.n64;
        primitiveFilterBounds[0] = primitiveFilterBounds[1] = 0;
        primitiveFilterBounds[2] = primitiveFilterBounds[3] = -1;
        int bounds = frame->id == RENDER_MODERN && faded.textured && faded.depth < 3 &&
            PortN64TextureInfo(faded.pageX, faded.pageY, faded.depth, faded.clutX, faded.clutY,
                               vertices[0].u, vertices[0].v, primitiveFilterBounds, &faded.textureFlags);
        faded.n64 = material.n64 = bounds || material.n64;
        int nativeBounds[4];
        int native = frame->id == RENDER_MODERN && !material.ui && faded.textured && !faded.n64 &&
                     NativeFilterBounds(state, vertices, count, nativeBounds);
        material.bounds = bounds || native;
        style = RenderPrepare(frame, &material);
        if (style.nativeBounds) memcpy(primitiveFilterBounds, nativeBounds, sizeof(nativeBounds));
        faded.filtered = style.filter | (style.coverage << 2);
        faded.screenOverlay = material.ui;
        if (frame->id != RENDER_MODERN || !frame->settings.modern.weaponLights) {
            faded.pointLights = NULL;
            memset(faded.shieldGlow, 0, sizeof(faded.shieldGlow));
        }
        brightness = (style.shade & SHADE_WORLD_TINT) ?
                           frame->settings.modern.brightness / (float)MODERN_BRIGHTNESS_MAX : 1.0f;
    }
    FindHd(state, vertices, count);
    int blend = Blended(state);
    Begin(state, count, blend);
    if (frame && blend && !batchBlendDirty)
        SelectColourBlend(!FilteredTextureOpaque(state, vertices, count));
    /* Separate loops let the baseline discard the unused vertex tail entirely. */
    if (!frame) {
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
            AddExisting(&v);
        }
    } else {
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
            Add(&v, style.shade, brightness, 0);
        }
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
    ClearDrawn(x, y, w, h);
    GpuClearArea(x, y, w, h, r, g, b);
}

/* Clears (x, y, w, h) of the framebuffers: `buffers` (colour to r, g, b, depth to the farthest). */
static void Clear(int x, int y, int w, int h, RgBits buffers, uint8_t r, uint8_t g, uint8_t b)
{
    RgBits needed;

    Flush();
    drawPass++;
    if (buffers & RG_COLOR_BUFFER_BIT) RgColorMask(1, 1, 1, 1);
    RgEnable(RG_SCISSOR_TEST);
    RgClearColor(r / 255.0f, g / 255.0f, b / 255.0f, 1.0f);
    RgDepthMask(RG_TRUE);
    needed = NeededClear(&depthUse, x, y, w, h, buffers);
    if (needed) {
        RgBindFramebuffer(RG_FRAMEBUFFER, framebuffer);
        RgScissor(PixelX(x), PixelY(y), SpanX(x, w), SpanY(y, h));
        RgClear(needed);
    }
    if (InScreenColumns(x, w) && wideFramebuffer) {
        /* across the whole screen: the sides too */
        int left = x <= 0 && x + w >= SCREEN_COLUMNS ? 0 : SDL_max(x, 0) + wideMargin;
        int right = x <= 0 && x + w >= SCREEN_COLUMNS ? WideWidth() : SDL_min(x + w, SCREEN_COLUMNS) + wideMargin;

        needed = NeededClear(&wideDepthUse, left, y, right - left, h, buffers);
        if (needed) {
            RgBindFramebuffer(RG_FRAMEBUFFER, wideFramebuffer);
            RgScissor(PixelX(left), PixelY(y), SpanX(left, right - left), SpanY(y, h));
            RgClear(needed);
        }
    }
    /* Every draw/transfer binds its own target. Restoring the regular VRAM
       target here only ends a widescreen render pass that drawing resumes. */
    RgDisable(RG_SCISSOR_TEST);
}

void GpuClearArea(int x, int y, int w, int h, uint8_t r, uint8_t g, uint8_t b)
{
    Clear(x, y, w, h, RG_COLOR_BUFFER_BIT | RG_DEPTH_BUFFER_BIT, r, g, b);
    SeedWrittenPicture(x, y, w, h);
}

void GpuClearSceneDepth(int x, int y, int w, int h, int mainTable)
{
    Clear(x, y, w, h, RG_DEPTH_BUFFER_BIT, 0, 0, 0);
    if (mainTable && GpuRaceWide() && (sceneAux.bands || wideAux.bands))
        SeedWrittenPicture(x, y, w, h);
}

void GpuCaptureCameraMotion(int x, int y, int w, int h, const GpuCameraMotion *motion, int hudOnly)
{
    if (x != 0 || w != SCREEN_COLUMNS || y < 0 || h <= 0 || y + h > VRAM_HEIGHT ||
        y / 256 != (y + h - 1) / 256) return;
    Flush();

    int band = y / 256, wide = wideMargin != 0;
    SceneAuxiliary *scene = wide ? &wideAux : &sceneAux;
    ModernMotion *snapshot = &cameraMotion[band];
    RenderRect region = {0, PixelY(y), PixelX(wide ? WideWidth() : w), SpanY(y, h)};
    if (hudOnly) {
        if (snapshot->revision && snapshot->pass + 1 == drawPass && snapshot->wide == wide &&
            !memcmp(&snapshot->region, &region, sizeof(region)))
            snapshot->revision = pictureOwners[band].revision;
        return;
    }
    snapshot->revision = 0;
    if (!motion || !(scene->bands & (1u << band)) || !scene->velocity.texture ||
        !ModernSurfaceResize(&snapshot->depth, region.w, region.h, RENDER_DEPTH)) return;

    RgBindFramebuffer(RG_READ_FRAMEBUFFER, wide ? wideFramebuffer : framebuffer);
    RgBindFramebuffer(RG_DRAW_FRAMEBUFFER, snapshot->depth.target);
    RgDisable(RG_SCISSOR_TEST);
    RgBlitFramebuffer(0, region.y, region.w, region.y + region.h, 0, 0, region.w, region.h,
                      RG_DEPTH_BUFFER_BIT, RG_NEAREST);
    RgBindFramebuffer(RG_FRAMEBUFFER, framebuffer);
    snapshot->region = region;
    snapshot->projection[0] = motion->centreX * (float)scale + (wide ? PixelX(wideMargin) : 0);
    snapshot->projection[1] = motion->centreY * (float)scale;
    snapshot->projection[2] = motion->focal * (float)scale;
    snapshot->projection[3] = motion->focal * (float)scale;
    memcpy(snapshot->earlier, motion->earlier, sizeof(snapshot->earlier));
    snapshot->wide = wide;
    snapshot->pass = drawPass;
    snapshot->revision = pictureOwners[band].revision;
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
        static RgHandle small, smallTexture;
        static int smallW, smallH;

        if (!small) {
            RgGenFramebuffers(1, &small);
            RgGenTextures(1, &smallTexture);
        }
        if (w > smallW || h > smallH) {
            smallW = SDL_max(w, smallW);
            smallH = SDL_max(h, smallH);
            RgBindTexture(RG_TEXTURE_2D, smallTexture);
            RgTexImage2D(RG_TEXTURE_2D, 0, RG_RGBA8, smallW, smallH, 0, RG_RGBA, RG_UNSIGNED_BYTE, NULL);
            RgBindFramebuffer(RG_FRAMEBUFFER, small);
            RgFramebufferTexture2D(RG_FRAMEBUFFER, RG_COLOR_ATTACHMENT0, RG_TEXTURE_2D, smallTexture, 0);
        }
        RgBindFramebuffer(RG_READ_FRAMEBUFFER, framebuffer);
        RgBindFramebuffer(RG_DRAW_FRAMEBUFFER, small);
        RgDisable(RG_SCISSOR_TEST);
        RgBlitFramebuffer(PixelX(x), PixelY(y), PixelX(x + w), PixelY(y + h), 0, 0, w, h, RG_COLOR_BUFFER_BIT,
                          RG_NEAREST);
        RgBindFramebuffer(RG_READ_FRAMEBUFFER, small);
        RgReadPixels(0, 0, w, h, RG_RGBA, RG_UNSIGNED_BYTE, pixels);
        RgBindFramebuffer(RG_FRAMEBUFFER, framebuffer);
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
    static RgHandle texture, readFramebuffer;
    uint8_t *rgba;
    int i, j;

    if (w <= 0 || h <= 0 || x < 0 || y < 0 || x + w > VRAM_WIDTH || y + h > VRAM_HEIGHT) {
        return;
    }
    Flush();
    if (!texture) {
        RgGenTextures(1, &texture);
        RgGenFramebuffers(1, &readFramebuffer);
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
    RgBindTexture(RG_TEXTURE_2D, texture);
    RgPixelStorei(RG_UNPACK_ALIGNMENT, 4);
    RgTexImage2D(RG_TEXTURE_2D, 0, RG_RGBA8, w, h, 0, RG_RGBA, RG_UNSIGNED_BYTE, rgba);
    RgPixelStorei(RG_UNPACK_ALIGNMENT, 2);
    free(rgba);
    RgBindFramebuffer(RG_READ_FRAMEBUFFER, readFramebuffer);
    RgFramebufferTexture2D(RG_READ_FRAMEBUFFER, RG_COLOR_ATTACHMENT0, RG_TEXTURE_2D, texture, 0);
    RgBindFramebuffer(RG_DRAW_FRAMEBUFFER, framebuffer);
    RgDisable(RG_SCISSOR_TEST);
    RgBlitFramebuffer(0, 0, w, h, PixelX(x), PixelY(y), PixelX(x + w), PixelY(y + h), RG_COLOR_BUFFER_BIT, RG_NEAREST);
    RgBindFramebuffer(RG_FRAMEBUFFER, framebuffer);
    ClearDrawn(x, y, w, h);
    CopyToWide(x, y, w, h);
    SeedWrittenPicture(x, y, w, h);
}

/* MoveImage in the drawn picture: the rectangle copied at the display's
   scale (through a texture, as the two may overlap). */
void GpuMoveFramebuffer(int x, int y, int w, int h, int toX, int toY)
{
    static RgHandle texture, readFramebuffer;

    if (w <= 0 || h <= 0 || x < 0 || y < 0 || toX < 0 || toY < 0 || x + w > VRAM_WIDTH || y + h > VRAM_HEIGHT ||
        toX + w > VRAM_WIDTH || toY + h > VRAM_HEIGHT) {
        return;
    }
    Flush();
    SyncWide();
    if (!texture) {
        RgGenTextures(1, &texture);
        RgGenFramebuffers(1, &readFramebuffer);
    }
    RgBindFramebuffer(RG_READ_FRAMEBUFFER, framebuffer);
    RgBindTexture(RG_TEXTURE_2D, texture);
    RgTexImage2D(RG_TEXTURE_2D, 0, RG_RGBA8, SpanX(x, w), SpanY(y, h), 0, RG_RGBA, RG_UNSIGNED_BYTE, NULL);
    RgCopyTexSubImage2D(RG_TEXTURE_2D, 0, 0, 0, PixelX(x), PixelY(y), SpanX(x, w), SpanY(y, h));
    RgBindFramebuffer(RG_READ_FRAMEBUFFER, readFramebuffer);
    RgFramebufferTexture2D(RG_READ_FRAMEBUFFER, RG_COLOR_ATTACHMENT0, RG_TEXTURE_2D, texture, 0);
    RgBindFramebuffer(RG_DRAW_FRAMEBUFFER, framebuffer);
    RgDisable(RG_SCISSOR_TEST);
    RgBlitFramebuffer(0, 0, SpanX(x, w), SpanY(y, h), PixelX(toX), PixelY(toY), PixelX(toX + w), PixelY(toY + h),
                      RG_COLOR_BUFFER_BIT, RG_NEAREST);
    RgBindFramebuffer(RG_FRAMEBUFFER, framebuffer);
    /* both copies now agree in the destination */
    ClearDrawn(toX, toY, w, h);
    CopyToWide(toX, toY, w, h);
    SeedWrittenPicture(toX, toY, w, h);
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
    size_t need = (size_t)SpanY(y, h) * 4;
    size_t i;

    colour[0] = colour[1] = colour[2] = 0;
    if (need > size) {
        pixels = realloc(pixels, need);
        size = need;
    }
    RgBindFramebuffer(RG_READ_FRAMEBUFFER, framebuffer);
    RgPixelStorei(RG_PACK_ALIGNMENT, 4);
    RgReadPixels(PixelX(x), PixelY(y), 1, SpanY(y, h), RG_RGBA, RG_UNSIGNED_BYTE, pixels);
    for (i = 4; i < need; i += 4) {
        if (memcmp(pixels + i, pixels, 3) != 0) {
            return;
        }
    }
    for (i = 0; i < 3; i++) {
        colour[i] = pixels[i] / 255.0f;
    }
}

/* A presentation pass owns its texture; it never changes the game's drawing buffers. */
typedef struct PictureBuffer {
    RgHandle framebuffer, texture;
    int width, height;
} PictureBuffer;

static void BindPictureBuffer(PictureBuffer *buffer, int w, int h)
{
    if (!buffer->framebuffer) {
        RgGenFramebuffers(1, &buffer->framebuffer);
        RgGenTextures(1, &buffer->texture);
    }
    RgBindTexture(RG_TEXTURE_2D, buffer->texture);
    RgBindFramebuffer(RG_FRAMEBUFFER, buffer->framebuffer);
    if (buffer->width != w || buffer->height != h) {
        RgTexImage2D(RG_TEXTURE_2D, 0, RG_RGBA8, w, h, 0, RG_RGBA, RG_UNSIGNED_BYTE, NULL);
        RgTexParameteri(RG_TEXTURE_2D, RG_TEXTURE_MIN_FILTER, RG_LINEAR);
        RgTexParameteri(RG_TEXTURE_2D, RG_TEXTURE_MAG_FILTER, RG_LINEAR);
        RgTexParameteri(RG_TEXTURE_2D, RG_TEXTURE_WRAP_S, RG_CLAMP_TO_EDGE);
        RgTexParameteri(RG_TEXTURE_2D, RG_TEXTURE_WRAP_T, RG_CLAMP_TO_EDGE);
        RgFramebufferTexture2D(RG_FRAMEBUFFER, RG_COLOR_ATTACHMENT0, RG_TEXTURE_2D, buffer->texture, 0);
        if (RgCheckFramebufferStatus(RG_FRAMEBUFFER) != RG_FRAMEBUFFER_COMPLETE) {
            SDL_Log("downsampling framebuffer incomplete");
            abort();
        }
        buffer->width = w;
        buffer->height = h;
    }
    RgViewport(0, 0, w, h);
}

static RgHandle PictureProgram(RenderShader kind) { return RgProgram(kind); }

/* Adaptive downsampling is inspired by DuckStation (https://github.com/stenzek/duckstation).
   These are independent variance-guided shaders, not DuckStation source. See docs/THIRD_PARTY.md.
   Spread detail weights over neighbouring target pixels before reconstruction, so a fine feature
   cannot disappear where it meets a flat cell. Only low-detail regions use the reduced image. */
static RgHandle AdaptivePicture(RgHandle source, PictureBuffer *average, int left, int top, int w, int h, int targetScale)
{
    static PictureBuffer weights, result;
    static RgHandle weightProgram, compositeProgram;
    static int weightSource, compositeSource, compositeCoarse, compositeRect, compositeStep;

    if (!weightProgram) {
        weightProgram = PictureProgram(RENDER_DETAIL);
        compositeProgram = PictureProgram(RENDER_COMPOSITE);
        weightSource = RgGetUniformLocation(weightProgram, "source");
        compositeSource = RgGetUniformLocation(compositeProgram, "source");
        compositeCoarse = RgGetUniformLocation(compositeProgram, "coarse");
        compositeRect = RgGetUniformLocation(compositeProgram, "rect");
        compositeStep = RgGetUniformLocation(compositeProgram, "stepSize");
    }
    BindPictureBuffer(&weights, average->width, average->height);
    RgUseProgram(weightProgram);
    RgUniform1i(weightSource, 0);
    RgBindTexture(RG_TEXTURE_2D, average->texture);
    RgDrawArrays(RG_TRIANGLES, 0, 3);

    BindPictureBuffer(&result, w, h);
    RgUseProgram(compositeProgram);
    RgUniform1i(compositeSource, 0);
    RgUniform1i(compositeCoarse, 1);
    RgUniform4i(compositeRect, left, top, left + w, top + h);
    RgUniform1f(compositeStep, (float)scale / targetScale);
    RgBindTexture(RG_TEXTURE_2D, source);
    RgActiveTexture(RG_TEXTURE1);
    RgBindTexture(RG_TEXTURE_2D, weights.texture);
    RgDrawArrays(RG_TRIANGLES, 0, 3);
    RgActiveTexture(RG_TEXTURE0);
    return result.framebuffer;
}

/* Presentation only: leave the game's framebuffer and VRAM readbacks at the rendering resolution.
   A linear blit samples at most four texels, so average source texels by their coverage of each target pixel. */
static void BlitPicture(int wide, int left, int top, int right, int bottom, RgHandle target,
                        int outLeft, int outTop, int outRight, int outBottom)
{
    static PictureBuffer average;
    static RgHandle averageProgram, averageArray;
    static int sourceUniform, rectUniform, scaleUniform, targetScaleUniform, adaptiveUniform;
    int w, h;
    RgPushState();
    int downsample = PortLinkDownsample(), style = PortLinkDownsampleStyle();
    RgHandle picture;
    RgHandle sourceFramebuffer = wide ? wideFramebuffer : framebuffer;
    RgHandle sourceColour = wide ? wideColour : colour;
    int band = top < PixelY(256) ? 0 : 1;
    int acrossBands = band == 0 && bottom > PixelY(256);
    uint64_t revision = pictureOwners[band].revision;
    const RenderFrame *profile = revision ? &pictureOwners[band].profile : RenderCurrent();
    const SceneAuxiliary *scene = wide ? &wideAux : &sceneAux;
    unsigned bands = acrossBands ? 3u : 1u << band;
    const ModernSurface *modern = NULL;
    int cropLeft = wide ? 0 : left;
    if (profile->id == RENDER_MODERN && pictureOwners[band].race &&
        (scene->bands & bands) == bands && (wide || right <= PixelX(SCREEN_COLUMNS))) {
        const ModernMotion *motion = &cameraMotion[band];
        if (acrossBands || motion->revision != revision || motion->wide != wide ||
            motion->region.y != top || motion->region.h != bottom - top)
            motion = NULL;
        ModernPicture input = {
            sourceColour, scene->hdr.texture, scene->mask.texture, scene->velocity.texture,
            acrossBands ? SDL_max(revision, pictureOwners[1].revision) : revision,
            {cropLeft, top, (wide ? PixelX(WideWidth()) : right) - cropLeft, bottom - top},
            motion, scale
        };
        modern = ModernResolvePicture(&input, &profile->settings.modern);
    }
    if (modern) {
        sourceFramebuffer = modern->target;
        sourceColour = modern->texture;
        right = right - cropLeft + 1;
        left = left - cropLeft + 1;
        bottom = bottom - top + 1;
        top = 1;
    }

    if (downsample < 0) {
        downsample = portSettings.downsample;
    }
    if (style < 0) {
        style = portSettings.downsampleStyle;
    }
    downsample = PortDownsampleTarget(scale, downsample, style);
    /* A 1:1 Box reduction is an identity. Copy directly with the same nearest
       output sampling rather than rendering a full-size intermediate image. */
    if (!downsample || (downsample == scale && style == DOWNSAMPLE_BOX) || right <= left || bottom <= top) {
        RgDisable(RG_SCISSOR_TEST);
        RgBindFramebuffer(RG_READ_FRAMEBUFFER, sourceFramebuffer);
        RgBindFramebuffer(RG_DRAW_FRAMEBUFFER, target);
        RgBlitFramebuffer(left, top, right, bottom, outLeft, outTop, outRight, outBottom,
                          RG_COLOR_BUFFER_BIT, downsample || !(profile->id != RENDER_MODERN || profile->settings.modern.resample) ? RG_NEAREST : RG_LINEAR);
        goto restore;
    }
    w = ((right - left) * downsample + scale - 1) / scale;
    h = ((bottom - top) * downsample + scale - 1) / scale;
    if (!averageProgram) {
        averageProgram = PictureProgram(RENDER_AVERAGE);
        sourceUniform = RgGetUniformLocation(averageProgram, "source");
        rectUniform = RgGetUniformLocation(averageProgram, "rect");
        scaleUniform = RgGetUniformLocation(averageProgram, "factor");
        targetScaleUniform = RgGetUniformLocation(averageProgram, "targetScale");
        adaptiveUniform = RgGetUniformLocation(averageProgram, "adaptive");
        RgGenVertexArrays(1, &averageArray);
    }
    RgDisable(RG_BLEND);
    RgDisable(RG_DEPTH_TEST);
    RgDisable(RG_SCISSOR_TEST);
    RgActiveTexture(RG_TEXTURE0);
    BindPictureBuffer(&average, w, h);
    RgUseProgram(averageProgram);
    RgUniform1i(sourceUniform, 0);
    RgUniform1i(scaleUniform, scale);
    RgUniform1i(targetScaleUniform, downsample);
    RgUniform1i(adaptiveUniform, style == DOWNSAMPLE_ADAPTIVE);
    RgUniform4i(rectUniform, left, top, right, bottom);
    RgBindTexture(RG_TEXTURE_2D, sourceColour);
    RgBindVertexArray(averageArray);
    RgDrawArrays(RG_TRIANGLES, 0, 3);
    picture = average.framebuffer;
    if (style == DOWNSAMPLE_ADAPTIVE) {
        w = right - left;
        h = bottom - top;
        picture = AdaptivePicture(sourceColour, &average, left, top, w, h, downsample);
    }
    RgBindFramebuffer(RG_READ_FRAMEBUFFER, picture);
    RgBindFramebuffer(RG_DRAW_FRAMEBUFFER, target);
    RgBlitFramebuffer(0, 0, w, h, outLeft, outTop, outRight, outBottom, RG_COLOR_BUFFER_BIT,
                      style == DOWNSAMPLE_ADAPTIVE ? RG_LINEAR : RG_NEAREST);
restore:
    RgPopState();
}

static void Present(int x, int y, int w, int h, int interpolated)
{
    int width, height, outW, outH, outX, outY;

    Flush();
    if (interpolated) {
        PortVideoBetweenFrames();
    }
    PortDrawableSize(&width, &height);
    RgBindFramebuffer(RG_DRAW_FRAMEBUFFER, windowFramebuffer);
    RgDisable(RG_SCISSOR_TEST);
    RgViewport(0, 0, width, height);
    RgClearColor(0, 0, 0, 1);
    RgClear(RG_COLOR_BUFFER_BIT);
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
        }
        left = wide ? PixelX((float)(WideWidth() - across) / 2) : PixelX(x);
        right = wide ? PixelX(WideWidth()) - left : PixelX(x + w);
        BlitPicture(wide, left, PixelY(y), right, PixelY(y + h), windowFramebuffer,
                    outX, outY + outH, outX + outW, outY);
        if (!wide && portSettings.widescreen && outX > 0) {
            float colour[3];

            /* BlitPicture restores the binding SyncWide left; the bars belong to the window. */
            RgBindFramebuffer(RG_DRAW_FRAMEBUFFER, windowFramebuffer);
            BarColour(x, y, h, colour);
            RgEnable(RG_SCISSOR_TEST);
            RgScissor(0, 0, outX, height);
            RgClearColor(colour[0], colour[1], colour[2], 1);
            RgClear(RG_COLOR_BUFFER_BIT);
            BarColour(x + w - 1, y, h, colour);
            RgScissor(outX + outW, 0, width - outX - outW, height);
            RgClearColor(colour[0], colour[1], colour[2], 1);
            RgClear(RG_COLOR_BUFFER_BIT);
            RgDisable(RG_SCISSOR_TEST);
        }
    }
    RgBindFramebuffer(RG_FRAMEBUFFER, windowFramebuffer);
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
static void SetScale(int newScale)
{
    RgHandle oldColour = colour, oldFramebuffer = framebuffer, oldDepth = depthBuffer;
    DepthUse oldDepthUse = depthUse;
    int oldScale = scale, margin = wideMargin;
    int oldW = PixelX(VRAM_WIDTH), oldH = PixelY(VRAM_HEIGHT);

    Flush();
    SetWideMargin(0); /* (its middle into video memory's first) */
    scale = newScale;
    if (!CreateFramebuffer()) {
        RgDeleteFramebuffers(1, &framebuffer);
        RgDeleteTextures(1, &colour);
        RgDeleteRenderbuffers(1, &depthBuffer);
        colour = oldColour;
        framebuffer = oldFramebuffer;
        depthBuffer = oldDepth;
        depthUse = oldDepthUse;
        scale = oldScale;
    } else {
        FreeSceneAux(&sceneAux);
        for (int band = 0; band < 2; band++) {
            ModernSurfaceFree(&cameraMotion[band].depth);
            cameraMotion[band].revision = 0;
            if (pictureOwners[band].revision) pictureOwners[band].revision = ++nextPictureRevision;
        }
        RgBindFramebuffer(RG_READ_FRAMEBUFFER, oldFramebuffer);
        RgBindFramebuffer(RG_DRAW_FRAMEBUFFER, framebuffer);
        RgDisable(RG_SCISSOR_TEST);
        RgBlitFramebuffer(0, 0, oldW, oldH, 0, 0, PixelX(VRAM_WIDTH),
                          PixelY(VRAM_HEIGHT), RG_COLOR_BUFFER_BIT, PixelX(VRAM_WIDTH) < oldW || PixelY(VRAM_HEIGHT) < oldH ? RG_LINEAR : RG_NEAREST);
        RgDeleteFramebuffers(1, &oldFramebuffer);
        RgDeleteTextures(1, &oldColour);
        RgDeleteRenderbuffers(1, &oldDepth);
    }
    RgBindFramebuffer(RG_FRAMEBUFFER, framebuffer);
    SetWideMargin(margin);
    HdTexSetScale(scale);
}

void GpuUpdateResolution(void)
{
    if (!initialised) return;
    int nextScale = ChooseScale();
    if (nextScale != scale) SetScale(nextScale);
}

void GpuSetWindowFramebuffer(unsigned int number)
{
    windowFramebuffer = 0; /* native window attachments belong to the device */
}

void GpuClearWindow(float r, float g, float b)
{
    int width, height;

    Flush();
    PortDrawableSize(&width, &height);
    RgBindFramebuffer(RG_FRAMEBUFFER, windowFramebuffer);
    RgDisable(RG_SCISSOR_TEST);
    RgViewport(0, 0, width, height);
    RgClearColor(r, g, b, 1.0f);
    RgClear(RG_COLOR_BUFFER_BIT);
}

/* After a new picture: the window's shape, the setting or the end of a race may ask for another margin (a
   picture shown again, as each field shows one, or while the frame recorder holds the game, changes nothing),
   or another scale. */
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
    size_t need = (size_t)SpanX(x, w) * SpanY(y, h) * 4;

    Flush();
    SyncWide();
    if (need > size) {
        pixels = realloc(pixels, need);
        size = need;
    }
    RgBindFramebuffer(RG_READ_FRAMEBUFFER, framebuffer);
    RgPixelStorei(RG_PACK_ALIGNMENT, 4);
    RgReadPixels(PixelX(x), PixelY(y), SpanX(x, w), SpanY(y, h), RG_RGBA, RG_UNSIGNED_BYTE, pixels);
    RgBindFramebuffer(RG_FRAMEBUFFER, windowFramebuffer);
    *outW = SpanX(x, w);
    *outH = SpanY(y, h);
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
    static RgHandle small, smallTexture;
    static int smallW, smallH;
    int wide = margin && margin == GpuShownMargin(x, w);

    Flush();
    SyncWide();
    if (!small || smallW != outW || smallH != outH) {
        if (!small) {
            RgGenFramebuffers(1, &small);
            RgGenTextures(1, &smallTexture);
        }
        RgBindTexture(RG_TEXTURE_2D, smallTexture);
        RgTexImage2D(RG_TEXTURE_2D, 0, RG_RGBA8, outW, outH, 0, RG_RGBA, RG_UNSIGNED_BYTE, NULL);
        RgBindFramebuffer(RG_FRAMEBUFFER, small);
        RgFramebufferTexture2D(RG_FRAMEBUFFER, RG_COLOR_ATTACHMENT0, RG_TEXTURE_2D, smallTexture, 0);
        smallW = outW;
        smallH = outH;
    }
    RgDisable(RG_SCISSOR_TEST);
    BlitPicture(wide, wide ? 0 : PixelX(x), PixelY(y), PixelX(wide ? WideWidth() : x + w),
                PixelY(y + h), small, 0, 0, outW, outH);
    RgBindFramebuffer(RG_READ_FRAMEBUFFER, small);
    RgPixelStorei(RG_PACK_ALIGNMENT, 4);
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
    RgReadPixels(0, 0, outW, outH, RG_RGBA, RG_UNSIGNED_BYTE, pixels);
    RgBindFramebuffer(RG_FRAMEBUFFER, windowFramebuffer);
    return pixels; /* video memory's rows go up the framebuffer: the first row read is the picture's top */
}

/* The device owns readback tickets; switches drain them to CPU memory before
   releasing the old API's resources, without changing reader identities. */
int GpuReadPictureBusy(int reader) {return RgReadbackBusy(reader);}
int GpuReadPictureStart(int reader,int x,int y,int w,int h,int margin,int outW,int outH)
{
    if(RgReadbackBusy(reader))return 0;
    ScalePicture(x,y,w,h,margin,outW,outH);
    int started=RgReadbackBegin(reader,outW,outH);
    RgBindFramebuffer(RG_FRAMEBUFFER,windowFramebuffer);return started;
}
int GpuReadPictureDone(int reader,int wait,void (*use)(const uint8_t *,int,int,void *),void *user)
{
    return RgReadbackDone(reader,wait,use,user);
}

void GpuPresentSplit(int x, int y, int w, int h, const uint8_t *lower, int lowerW, int lowerH, int lowerNew)
{
    static RgHandle texture, readFramebuffer;
    static int textureW, textureH;
    int width, height, ox, oy, ow, oh;

    /* widescreen: each half as wide as the window, which is as wide as the picture with its sides (half the
       height across the same width is the band's shape) */
    int wide = GpuShownMargin(x, w) != 0, across = wide ? WideWidth() : w;

    Flush();
    if (!wide) {
        SyncWide();
    }
    PortDrawableSize(&width, &height);
    /* the other game draws the lower half as wide, and hands it over at its size in pixels */
    PortLinkSetBand(GpuShownMargin(x, w), width, height / 2, portSettings.downsample, portSettings.downsampleStyle);
    RgBindFramebuffer(RG_DRAW_FRAMEBUFFER, windowFramebuffer);
    RgDisable(RG_SCISSOR_TEST);
    RgViewport(0, 0, width, height);
    RgClearColor(0, 0, 0, 1);
    RgClear(RG_COLOR_BUFFER_BIT);
    /* the upper half: this game's band (OpenGL's rows go up: the upper half is from height / 2), its pixels as wide
       as the console's are (PAL's 256 lines across the same 4:3 screen: wider than tall) */
    Fit(0, height - height / 2, width, height / 2, across * GPU_LINES, h * 240, &ox, &oy, &ow, &oh);
    BlitPicture(wide, wide ? 0 : PixelX(x), PixelY(y), PixelX(wide ? across : x + w),
                PixelY(y + h), windowFramebuffer, ox, oy + oh, ox + ow, oy);
    /* the lower half: the other game's band, as it read it (rows from the bottom) */
    if (lower && lowerW > 0 && lowerH > 0) {
        if (!texture) {
            RgGenTextures(1, &texture);
            RgGenFramebuffers(1, &readFramebuffer);
        }
        RgBindTexture(RG_TEXTURE_2D, texture);
        RgPixelStorei(RG_UNPACK_ALIGNMENT, 4);
        if (lowerW != textureW || lowerH != textureH) {
            RgTexImage2D(RG_TEXTURE_2D, 0, RG_RGBA8, lowerW, lowerH, 0, RG_RGBA, RG_UNSIGNED_BYTE, lower);
            RgTexParameteri(RG_TEXTURE_2D, RG_TEXTURE_MIN_FILTER, RG_LINEAR);
            RgTexParameteri(RG_TEXTURE_2D, RG_TEXTURE_MAG_FILTER, RG_LINEAR);
            textureW = lowerW;
            textureH = lowerH;
        } else if (lowerNew) {
            RgTexSubImage2D(RG_TEXTURE_2D, 0, 0, 0, lowerW, lowerH, RG_RGBA, RG_UNSIGNED_BYTE, lower);
        }
        RgPixelStorei(RG_UNPACK_ALIGNMENT, 2);
        Fit(0, 0, width, height / 2, lowerW, lowerH, &ox, &oy, &ow, &oh);
        RgBindFramebuffer(RG_READ_FRAMEBUFFER, readFramebuffer);
        RgFramebufferTexture2D(RG_READ_FRAMEBUFFER, RG_COLOR_ATTACHMENT0, RG_TEXTURE_2D, texture, 0);
        RgBlitFramebuffer(0, 0, lowerW, lowerH, ox, oy + oh, ox + ow, oy, RG_COLOR_BUFFER_BIT,
                          portSettings.downsample && portSettings.downsampleStyle == DOWNSAMPLE_BOX ? RG_NEAREST : RG_LINEAR);
    }
    RgBindFramebuffer(RG_FRAMEBUFFER, windowFramebuffer);
    PortVideoPresent();
    AfterPicture();
}

void GpuShowImage(const uint8_t *rgb, int width, int height)
{
    static RgHandle texture, readFramebuffer;
    static int textureWidth, textureHeight;
    int windowWidth, windowHeight, outW, outH, outX, outY;

    Flush();
    if (!texture) {
        RgGenTextures(1, &texture);
        RgGenFramebuffers(1, &readFramebuffer);
    }
    RgBindTexture(RG_TEXTURE_2D, texture);
    RgPixelStorei(RG_UNPACK_ALIGNMENT, 1);
    if (width != textureWidth || height != textureHeight) {
        RgTexImage2D(RG_TEXTURE_2D, 0, RG_RGB8, width, height, 0, RG_RGB, RG_UNSIGNED_BYTE, rgb);
        RgTexParameteri(RG_TEXTURE_2D, RG_TEXTURE_MIN_FILTER, RG_LINEAR);
        RgTexParameteri(RG_TEXTURE_2D, RG_TEXTURE_MAG_FILTER, RG_LINEAR);
        textureWidth = width;
        textureHeight = height;
    } else {
        RgTexSubImage2D(RG_TEXTURE_2D, 0, 0, 0, width, height, RG_RGB, RG_UNSIGNED_BYTE, rgb);
    }
    RgPixelStorei(RG_UNPACK_ALIGNMENT, 2);
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
    RgBindFramebuffer(RG_DRAW_FRAMEBUFFER, windowFramebuffer);
    RgDisable(RG_SCISSOR_TEST);
    RgViewport(0, 0, windowWidth, windowHeight);
    RgClearColor(0, 0, 0, 1);
    RgClear(RG_COLOR_BUFFER_BIT);
    RgBindFramebuffer(RG_READ_FRAMEBUFFER, readFramebuffer);
    RgFramebufferTexture2D(RG_READ_FRAMEBUFFER, RG_COLOR_ATTACHMENT0, RG_TEXTURE_2D, texture, 0);
    /* image row 0 is the top: flip into OpenGL's upward rows */
    RgBlitFramebuffer(0, 0, width, height, outX, outY + outH, outX + outW, outY, RG_COLOR_BUFFER_BIT, RG_LINEAR);
    RgBindFramebuffer(RG_FRAMEBUFFER, windowFramebuffer);
    PortVideoPresent();
}

void GpuClearDepth(int x, int y, int w, int h) { GpuClearSceneDepth(x, y, w, h, 0); }

void GpuRendererShutdown(void)
{
    FreeSceneAux(&sceneAux); FreeSceneAux(&wideAux);
    for (int band = 0; band < 2; band++) ModernSurfaceFree(&cameraMotion[band].depth);
    ModernPostReset();
}

unsigned GpuModernFailures(void)
{
    const SceneAuxiliary *scene = wideMargin ? &wideAux : &sceneAux;
    return ModernPostFailures() | (scene->unavailable ? 8u : 0u) |
           (scene->velocity.failedDevice == RenderDeviceGeneration() && scene->velocity.failedWidth ? 2u : 0u);
}
