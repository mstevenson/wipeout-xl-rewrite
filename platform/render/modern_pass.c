/* Device-independent Modern shutter, bloom and tone-mapping passes. */
#include "modern_pass.h"

#include <SDL3/SDL.h>
#include <string.h>

enum { FIELD, BLOCKS, TILES, NEARBY, MOTION_SURFACES };
enum { BLOOM_A, BLOOM_B, RESOLVED, BLOOM_SURFACES };
enum { EXTRACT = 1, BLUR, RESOLVE, LINEAR_SCENE };

static ModernSurface motion[MOTION_SURFACES], bloom[BLOOM_SURFACES], linear;
static struct {
    RgHandle hdr, sdr, depth;
    RenderRect crop;
    uint64_t revision;
    int divisor, amount, glow, hdrEnabled, tonemapper;
} cache;

int ModernSurfaceResize(ModernSurface *surface, int width, int height, RenderFormat format)
{
    if (surface->width == width && surface->height == height && surface->texture) return 1;
    if (!surface->texture) {
        RgGenTextures(1, &surface->texture);
        RgGenFramebuffers(1, &surface->target);
    }
    if (!RgAllocateImage(surface->texture, width, height, format)) return 0;
    RgBindFramebuffer(RG_DRAW_FRAMEBUFFER, surface->target);
    RgFramebufferTexture2D(RG_DRAW_FRAMEBUFFER, format == RENDER_DEPTH ? RG_DEPTH_ATTACHMENT : RG_COLOR_ATTACHMENT0,
                           RG_TEXTURE_2D, surface->texture, 0);
    surface->width = width;
    surface->height = height;
    return 1;
}

void ModernSurfaceFree(ModernSurface *surface)
{
    if (surface->target) RgDeleteFramebuffers(1, &surface->target);
    if (surface->texture) RgDeleteTextures(1, &surface->texture);
    *surface = (ModernSurface){0};
}

static void Draw(RenderShader shader, const ModernSurface *target, const float effect[RENDER_PASS_VECTORS][4],
                 const RgHandle textures[RENDER_TEXTURES])
{
    RgBindFramebuffer(RG_DRAW_FRAMEBUFFER, target->target);
    RgViewport(0, 0, target->width, target->height);
    RgDrawPass(shader, effect, textures, 0, 0, 3, 0);
}

static int MotionField(const ModernPicture *picture, int percent)
{
    const ModernMotion *view = picture->motion;
    int width = picture->crop.w, height = picture->crop.h;
    float amount = 1.5f * (percent / 100.0f);
    float radius = 0.007f * height * amount;
    int tile = 4 * SDL_max(1, (int)SDL_ceilf(radius / 4));
    int sizes[MOTION_SURFACES][2] = {
        {width, height}, {(width + 3) / 4, (height + 3) / 4},
        {(width + tile - 1) / tile, (height + tile - 1) / tile},
        {(width + tile - 1) / tile, (height + tile - 1) / tile}
    };
    for (int i = 0; i < MOTION_SURFACES; i++) {
        if (!ModernSurfaceResize(&motion[i], sizes[i][0], sizes[i][1], RENDER_RGBA16_FLOAT)) {
            for (int j = 0; j < MOTION_SURFACES; j++) ModernSurfaceFree(&motion[j]);
            return 0;
        }
    }

    float effect[RENDER_PASS_VECTORS][4] = {
        {picture->crop.x, picture->crop.y},
        {view->region.x, view->region.y, view->region.w, view->region.h},
        {0}, {0}, {0}, {0},
        {0, tile, amount, radius}
    };
    memcpy(effect[2], view->projection, sizeof(view->projection));
    memcpy(effect[3], view->earlier, sizeof(view->earlier));
    RgHandle inputs[RENDER_TEXTURES] = {picture->mask, view->depth.texture, picture->velocity, picture->mask};
    for (int i = 0; i < MOTION_SURFACES; i++) {
        effect[6][0] = i;
        Draw(RENDER_MODERN_MOTION, &motion[i], effect, inputs);
        inputs[3] = motion[i].texture;
    }
    return tile;
}

const ModernSurface *ModernResolvePicture(const ModernPicture *picture, const ModernRenderSettings *settings)
{
    RenderRect crop = picture->crop;
    int hdr = settings->bloom || settings->weaponLights;
    int amount = picture->motion && settings->cameraBlur ? settings->cameraBlurAmount : 0;
    if ((!hdr && !amount) || crop.w <= 0 || crop.h <= 0) return NULL;

    int divisor = SDL_max(2, (3 * picture->scale + 1) / 2);
    RgHandle depth = amount ? picture->motion->depth.texture : 0;
    if (cache.hdr == picture->hdr && cache.sdr == picture->sdr && cache.depth == depth &&
        cache.revision == picture->revision && !memcmp(&cache.crop, &crop, sizeof(crop)) &&
        cache.divisor == divisor && cache.amount == amount && cache.glow == settings->bloom &&
        cache.hdrEnabled == hdr && cache.tonemapper == settings->tonemapper)
        return &bloom[RESOLVED];

    cache.hdr = 0;
    int w = (crop.w + divisor - 1) / divisor, h = (crop.h + divisor - 1) / divisor;
    if (!ModernSurfaceResize(&bloom[RESOLVED], crop.w + 2, crop.h + 2, RENDER_RGBA8)) return NULL;
    int glow = settings->bloom;
    if (glow) {
        for (int i = BLOOM_A; i <= BLOOM_B; i++) {
            if (!ModernSurfaceResize(&bloom[i], w, h, RENDER_RGBA16_FLOAT)) {
                ModernSurfaceFree(&bloom[BLOOM_A]);
                ModernSurfaceFree(&bloom[BLOOM_B]);
                glow = 0;
                break;
            }
            RgActiveTexture(RG_TEXTURE0);
            RgBindTexture(RG_TEXTURE_2D, bloom[i].texture);
            RgTexParameteri(RG_TEXTURE_2D, RG_TEXTURE_MIN_FILTER, RG_LINEAR);
            RgTexParameteri(RG_TEXTURE_2D, RG_TEXTURE_MAG_FILTER, RG_LINEAR);
        }
    }

    RgDisable(RG_BLEND);
    RgDisable(RG_DEPTH_TEST);
    RgDisable(RG_SCISSOR_TEST);
    RgDepthMask(0);
    RgColorMask(1, 1, 1, 1);
    int tile = amount ? MotionField(picture, amount) : 0;
    if (!hdr && !tile) return NULL;

    float effect[RENDER_PASS_VECTORS][4] = {
        {crop.x, crop.y, crop.x + crop.w, crop.y + crop.h},
        {0, 0, EXTRACT, divisor},
        {tile, settings->tonemapper == MODERN_TONEMAP_SOFT_SHADOWS, glow, hdr}
    };
    RgHandle inputs[RENDER_TEXTURES] = {picture->hdr, picture->mask, picture->sdr, picture->sdr,
                                       tile ? motion[FIELD].texture : picture->sdr,
                                       tile ? motion[NEARBY].texture : picture->sdr};
    if (glow) {
        Draw(RENDER_MODERN_RESOLVE, &bloom[BLOOM_A], effect, inputs);
        effect[1][2] = BLUR;
        effect[1][0] = 1.0f / w;
        inputs[0] = bloom[BLOOM_A].texture;
        Draw(RENDER_MODERN_RESOLVE, &bloom[BLOOM_B], effect, inputs);
        effect[1][0] = 0;
        effect[1][1] = 1.0f / h;
        inputs[0] = bloom[BLOOM_B].texture;
        Draw(RENDER_MODERN_RESOLVE, &bloom[BLOOM_A], effect, inputs);
        inputs[2] = bloom[BLOOM_A].texture;
    }
    inputs[0] = picture->hdr;
    /* A longer shutter reuses enough taps to amortize linearizing the scene once. */
    int prepareLinear = tile > 4 && RenderActiveBackend() == RENDER_METAL &&
                        ModernSurfaceResize(&linear, crop.w, crop.h, RENDER_RGBA32_FLOAT);
    if (prepareLinear) {
        effect[1][2] = LINEAR_SCENE;
        Draw(RENDER_MODERN_RESOLVE, &linear, effect, inputs);
        inputs[0] = linear.texture;
        effect[3][0] = 1;
    }
    effect[1][2] = RESOLVE;
    Draw(RENDER_MODERN_RESOLVE, &bloom[RESOLVED], effect, inputs);
    if (prepareLinear) RgDiscardImageContents(linear.texture);
    if (glow == settings->bloom && (!!tile == !!amount)) {
        cache.hdr = picture->hdr;
        cache.sdr = picture->sdr;
        cache.depth = depth;
        cache.crop = crop;
        cache.revision = picture->revision;
        cache.divisor = divisor;
        cache.amount = amount;
        cache.glow = glow;
        cache.hdrEnabled = hdr;
        cache.tonemapper = settings->tonemapper;
    }
    return &bloom[RESOLVED];
}
