/* Optional presentation policy. Requested settings are committed only after the old frame flushes. */
#ifndef PORT_MODERN_H
#define PORT_MODERN_H
#include <stdio.h>
#include <stdint.h>
#include "geometry.h"
#ifdef __cplusplus
extern "C" {
#endif
typedef enum RendererId { RENDER_EXISTING, RENDER_MODERN } RendererId;
enum { MODERN_BRIGHTNESS_MIN = 50, MODERN_BRIGHTNESS_DEFAULT = 85, MODERN_BRIGHTNESS_MAX = 100 };
enum { MODERN_TONEMAP_ORIGINAL, MODERN_TONEMAP_SOFT_SHADOWS };
enum { MODERN_BLUR_DEFAULT = 100, MODERN_BLUR_MAX = 300 };
typedef struct ModernRenderSettings {
    int resample, brightness, bilinear, bloom, tonemapper, smoothTrackLighting, weaponLights;
    int cameraBlur, cameraBlurAmount;
} ModernRenderSettings;
/* Operation/material flags shared by CPU clipping and shaders, not renderer selection. */
enum { SHADE_RAW = 1, SHADE_WORLD_TINT = 2, SHADE_PS1_COLOUR = 4, SHADE_DITHER = 8,
       SHADE_AFFINE_COLOUR = 16, SHADE_AFFINE_UV = 32, SHADE_N64 = 64, SHADE_SNAP = 128 };
/* UI is independent of perspective/depth: affine world billboards are not screen-space UI. */
typedef struct RenderMaterial {
    int n64, fullColour, textured, raw, dither, bounds, ui, perspective;
    int smoothTrackLighting;
} RenderMaterial;
typedef enum RenderFilter {
    RENDER_NEAREST, RENDER_THREE_POINT, RENDER_BILINEAR
} RenderFilter;
typedef enum RenderCoverage {
    RENDER_COVERAGE_DEFAULT, RENDER_COVERAGE_HALF, RENDER_COVERAGE_MESH
} RenderCoverage;
typedef struct RenderStyle {
    unsigned shade;
    RenderFilter filter;
    RenderCoverage coverage;
    int nativeBounds; /* request for source bounds */
} RenderStyle;

static inline unsigned RenderBaseShade(const RenderMaterial *material)
{
    return SHADE_AFFINE_COLOUR | (material->raw ? SHADE_RAW : 0) |
           (material->n64 ? SHADE_N64 : 0);
}
/* Optional resources/operations requested by Modern, implemented by shared transport. */
typedef struct RenderResources {
    int hdTexturesAtNativeScale, hdr, cameraBlur;
    int hdTexturesAlways; /* draw the chosen HD pack even while the HD textures setting is off */
    float motionShutterSeconds; /* presentation exposure sampled by motion transport */
} RenderResources;
typedef struct RenderPacketPolicy {
    int smoothTrackLighting;
} RenderPacketPolicy;

#define MODERN_DEFAULTS {1,85,1,1,MODERN_TONEMAP_SOFT_SHADOWS,1,1,1,100}
typedef struct RenderSettings { RendererId selection; ModernRenderSettings modern; } RenderSettings;
#define RENDER_DEFAULTS {RENDER_EXISTING, MODERN_DEFAULTS}
typedef struct RenderFrame {
    struct { ModernRenderSettings modern; } settings;
    RendererId id;
    uint64_t generation;
} RenderFrame;
const RenderFrame *RenderCurrent(void);
int RenderCommit(void);
/* Flush queued work only when a new immutable policy will be published. */
int RenderCommitBefore(void (*beforeChange)(void));
void RenderInvalidate(void);
void ModernReset(RenderSettings *settings);
int RenderSet(RenderSettings *settings, const char *key, const char *value);
void RenderSave(FILE *file, const RenderSettings *settings);
RenderStyle RenderPrepare(const RenderFrame *, const RenderMaterial *);
RenderResources RenderResourcesFor(const RenderFrame *);
RenderResources RenderFrameResources(const RenderFrame *, int linked, int tracing);
RenderPacketPolicy RenderPacketPolicyFor(const RenderFrame *);
/* Read-only outside modern.c. -1 means lazy initialization is still needed.
   The frame boundary publishes this with current; packet hot paths need only
   one load, not an out-of-line policy query for every corner. */
extern int renderModernActive;
static inline int ModernEnabled(void)
{
    return renderModernActive >= 0 ? renderModernActive : RenderCurrent()->id == RENDER_MODERN;
}
int ModernUI(void);
#ifdef __cplusplus
}
#endif
#endif
