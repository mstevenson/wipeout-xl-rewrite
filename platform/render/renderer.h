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
enum { MODERN_BRIGHTNESS_MIN = 50, MODERN_BRIGHTNESS_MAX = 100 };
enum { MODERN_TONEMAP_ORIGINAL, MODERN_TONEMAP_SOFT_SHADOWS };
typedef struct ModernRenderSettings {
    int resample, scale, brightness, bilinear, bloom, tonemapper;
} ModernRenderSettings;
typedef struct RenderSettings {
    RendererId selection; /* runtime override; AUTO follows the loaded platform */
    RendererId ps1Selection; /* PlayStation preference, including legacy automatic_renderer */
    RendererId pcSelection, n64Selection, saturnSelection;
    Ps1RenderSettings ps1;
    PcRenderSettings pc;
    N64RenderSettings n64;
    SaturnRenderSettings saturn;
    ModernRenderSettings modern;
} RenderSettings;
#define RENDER_DEFAULTS { RENDER_AUTO, RENDER_PS1, RENDER_PC, RENDER_N64, RENDER_SATURN, {1,1,1,1,0,0,1}, {1,1,1,1,1,1,1,0}, {1,1,1,1,1,1,1}, {1,1,1,1,0,0,0,1}, {1,0,MODERN_BRIGHTNESS_MAX,1,1,MODERN_TONEMAP_SOFT_SHADOWS} }

/* Immutable for a game frame. The source track is independent of an explicit renderer choice. */
typedef struct RenderFrame { RenderSettings settings; RendererId id; int n64Track; } RenderFrame;
/* Operation/material flags shared by CPU clipping and shaders, not renderer selection. */
enum { SHADE_RAW = 1, SHADE_WORLD_TINT = 2, SHADE_PS1_COLOUR = 4, SHADE_DITHER = 8,
       SHADE_AFFINE_COLOUR = 16, SHADE_AFFINE_UV = 32, SHADE_N64 = 64, SHADE_SNAP = 128 };
/* UI is independent of perspective/depth: affine world billboards are not screen-space UI. */
typedef struct RenderMaterial {
    int n64, fullColour, textured, spatial, raw, dither, bounds, ui;
} RenderMaterial;
/* The two sampling bits are profile-specific. Saturn uses nearest sampling in both modes. */
enum { RENDER_SAMPLE_SATURN_ALPHA = 2, RENDER_SAMPLE_MESH = 3 };
typedef struct RenderStyle {
    unsigned shade;
    int filter;
    int nativeBounds; /* request for source bounds */
} RenderStyle;
/* Optional resources/operations requested by the profile, implemented by shared transport. */
typedef struct RenderResources {
    int originalTextures, hdr;
    int nativeQuads, alphaMesh, cameraAlignedSky;
} RenderResources;
typedef struct Renderer {
    RendererId id;
    const char *name;
    /* The registry passes only this renderer's profile, never other profiles or mutable settings. */
    RenderStyle (*prepare)(const void *profile, const RenderMaterial *);
    int (*depth)(const void *profile);
    RenderGeometry (*geometry)(const void *profile);
    RenderResources (*resources)(const void *profile);
} Renderer;
extern const Renderer ps1Renderer, pcRenderer, n64Renderer, saturnRenderer, modernRenderer;
const Renderer *RenderGet(RendererId id);
/* Original display cadence of the committed renderer profile. */
int RenderOriginalFrameRate(int pal);
int RenderPcViewLimits(const RenderFrame *frame, int *sections, int *scenery);
const void *RenderProfile(const RenderFrame *frame);
int RenderDepth(const RenderFrame *frame);
/* Scale 0 follows the window; positive values multiply this profile's native size. */
int *RenderScaleSetting(RenderSettings *settings, RendererId id);
int RenderScale(const RenderFrame *frame);
void RenderNativeSize(RendererId id, int pal, int *width, int *height);
RenderGeometry RenderGeometryFor(const RenderFrame *frame);
RenderStyle RenderPrepare(const RenderFrame *frame, const RenderMaterial *material);
RenderResources RenderResourcesFor(const RenderFrame *frame);
const RenderFrame *RenderCurrent(void);
int RenderCommit(void); /* call after flushing the previous frame; returns whether policy changed */
void RenderInvalidate(void); /* startup/settings reload */
void RenderDefaults(RenderSettings *settings, RendererId id); /* resets only this profile */
int RenderSet(RenderSettings *settings, const char *key, const char *value);
void RenderSave(FILE *file, const RenderSettings *settings);
#ifdef __cplusplus
}
#endif
#endif
