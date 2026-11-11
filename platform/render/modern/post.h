#ifndef PORT_MODERN_PASS_H
#define PORT_MODERN_PASS_H

#include "modern.h"
#include "../state.h"

typedef struct ModernSurface {
    RgHandle texture, target;
    int width, height;
    RenderFormat format;
    int failedWidth, failedHeight;
    uint64_t failedDevice;
} ModernSurface;

typedef struct ModernMotion {
    ModernSurface depth;
    RenderRect region;
    float projection[4], earlier[3][4];
    uint64_t revision, pass;
    int wide;
} ModernMotion;

typedef struct ModernPicture {
    RgHandle sdr, hdr, mask, velocity;
    uint64_t revision;
    RenderRect crop;
    const ModernMotion *motion;
    int scale;
} ModernPicture;

int ModernSurfaceResize(ModernSurface *surface, int width, int height, RenderFormat format);
void ModernSurfaceFree(ModernSurface *surface);
void ModernPostReset(void);
unsigned ModernPostFailures(void);
uint64_t ModernResolveCount(void);
/* The resolved image has one pixel of padding around the crop. */
const ModernSurface *ModernResolvePicture(const ModernPicture *picture, const ModernRenderSettings *settings);

#endif
