#ifndef PORT_RENDERER_H
#define PORT_RENDERER_H
#include <stdio.h>
#include "geometry.h"
#ifdef __cplusplus
extern "C" {
#endif

typedef enum RendererId { RENDER_AUTO, RENDER_PS1, RENDER_PC, RENDER_N64, RENDER_SATURN, RENDER_MODERN, RENDER_COUNT } RendererId;
typedef struct Ps1RenderSettings {
    int quantizeVertices, affineTextures, colourPrecision, dither, depth, resample, scale;
} Ps1RenderSettings;
enum { PC_DRAW_DISTANCE_LEVELS = 8 };
typedef struct PcRenderSettings {
    int bilinear, lighting, depth, resample, scale;
    int rgb565, drawDistance, correctMenuAspect;
} PcRenderSettings;
typedef struct N64RenderSettings {
    int threePoint, colourPrecision, dither, dedither, divot, resample, scale;
} N64RenderSettings;
typedef struct SaturnRenderSettings {
    int quantizeVertices, affineTextures, colourPrecision, mesh, depth, cameraAlignedSky, resample, scale;
} SaturnRenderSettings;
enum { MODERN_BRIGHTNESS_MIN = 50, MODERN_BRIGHTNESS_DEFAULT = 85, MODERN_BRIGHTNESS_MAX = 100 };
enum { MODERN_TONEMAP_ORIGINAL, MODERN_TONEMAP_SOFT_SHADOWS };
typedef struct ModernRenderSettings {
    int resample, scale, brightness, bilinear, bloom, tonemapper, smoothTrackLighting;
} ModernRenderSettings;
typedef struct RenderSettings {
    RendererId selection; /* runtime override; AUTO follows the loaded platform */
    RendererId ps1Selection;
    RendererId pcSelection, n64Selection, saturnSelection;
    Ps1RenderSettings ps1;
    PcRenderSettings pc;
    N64RenderSettings n64;
    SaturnRenderSettings saturn;
    ModernRenderSettings modern;
    int modernBloomOverride; /* 0 follows the setting, -1 forces off, 1 forces on */
} RenderSettings;
#define RENDER_DEFAULTS { RENDER_AUTO, RENDER_PS1, RENDER_PC, RENDER_N64, RENDER_SATURN, {1,1,1,1,0,0,1}, {1,1,1,1,1,1,1,0}, {1,1,1,1,1,1,1}, {1,1,1,1,0,0,0,1}, {1,0,MODERN_BRIGHTNESS_DEFAULT,1,1,MODERN_TONEMAP_SOFT_SHADOWS,1}, 0 }

/* Immutable for a game frame. The source track is independent of an explicit renderer choice. */
typedef union RenderProfileSettings {
    Ps1RenderSettings ps1;
    PcRenderSettings pc;
    N64RenderSettings n64;
    SaturnRenderSettings saturn;
    ModernRenderSettings modern;
} RenderProfileSettings;
struct RenderProfileInfo;
typedef struct RenderFrame {
    RenderProfileSettings settings; /* only the selected profile, owned by value */
    RendererId id;
    const struct RenderProfileInfo *profile;
} RenderFrame;
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
/* Optional resources/operations requested by the profile, implemented by shared transport. */
typedef struct RenderResources {
    int originalTextures, hdr;
} RenderResources;
typedef struct RenderPacketPolicy {
    int nativeQuads, nativeSky, shadowMask, smoothTrackLighting;
} RenderPacketPolicy;
typedef struct Renderer {
    const char *name;
    /* The registry passes only this renderer's profile, never other profiles or mutable settings. */
    RenderStyle (*prepare)(const void *profile, const RenderMaterial *);
    int (*depth)(const void *profile);
    RenderGeometry (*geometry)(const void *profile);
    RenderResources (*resources)(const void *profile);
    RenderPacketPolicy (*packets)(const void *profile);
} Renderer;
extern const Renderer ps1Renderer, pcRenderer, n64Renderer, saturnRenderer, modernRenderer;
const Renderer *RenderGet(RendererId id);
RendererId RenderIdFromName(const char *name);
int RenderModernBloom(const RenderSettings *settings);
/* Original display cadence of the committed renderer profile. */
int RenderOriginalFrameRate(int pal);
int RenderOriginalInterpolates(void);
int RenderPcViewLimits(const RenderFrame *frame, int *sections, int *scenery);
const void *RenderProfile(const RenderFrame *frame);
int RenderDepth(const RenderFrame *frame);
/* Scale 0 follows the window; positive values multiply this profile's native size. */
const char *RenderScaleKey(RendererId id);
int *RenderScaleSetting(RenderSettings *settings, RendererId id);
int RenderScale(const RenderFrame *frame);
void RenderNativeSize(RendererId id, int pal, int *width, int *height);
RenderGeometry RenderGeometryFor(const RenderFrame *frame);
RenderStyle RenderPrepare(const RenderFrame *frame, const RenderMaterial *material);
RenderResources RenderResourcesFor(const RenderFrame *frame);
RenderPacketPolicy RenderPacketPolicyFor(const RenderFrame *frame);
RenderFrame RenderSnapshot(RendererId id, const RenderSettings *settings);
const RenderFrame *RenderCurrent(void);
/* Full configuration at the last commit, used by renderer.ini diagnostics. */
const RenderSettings *RenderCommittedSettings(void);
int RenderCommit(void); /* call after flushing the previous frame; returns whether policy changed */
void RenderInvalidate(void); /* startup/settings reload */
void RenderDefaults(RenderSettings *settings, RendererId id); /* resets only this profile */
int RenderSet(RenderSettings *settings, const char *key, const char *value);
void RenderSave(FILE *file, const RenderSettings *settings);
#ifdef __cplusplus
}
#endif
#endif
