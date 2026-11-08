/* Native Saturn draw data, retained alongside the compatible PlayStation assets. */
#ifndef SATURN_NATIVE_H
#define SATURN_NATIVE_H
#include <stdint.h>
#include "../psx/gpu.h"

typedef enum SaturnMaterialAnimation {
    SATURN_ANIM_NONE, SATURN_ANIM_NEON, SATURN_ANIM_WATERFALL,
    SATURN_ANIM_RESCUE_RED, SATURN_ANIM_RESCUE_RGB,
    SATURN_ANIM_SMOKE_SLOW, SATURN_ANIM_SMOKE_FAST
} SaturnMaterialAnimation;

typedef struct SaturnNativePoly {
    uint16_t palette[16], gouraud[4], colour, mode;
    int width, height, textured, dynamicLight, corners[4]; /* VDP1 A B C D -> packet corner */
    int billboardVertex[4][3]; /* original billboard corners in host world units */
    int depthMode; /* model group bits 14..15: average/max/min/max+32 */
    int trackTile; /* projected road tile uses bounded, perspective-correct triangles */
    int clipped; /* host near-plane triangle; bounded inverse rasterization */
    int vertexAttributes; /* native UV and additive shades carried through clipping */
    int alphaMesh; /* draw-time option: replace mesh coverage with half opacity */
    SaturnMaterialAnimation animation;
    uint8_t *pixels; /* unpacked indices, before triangle resampling */
} SaturnNativePoly;
enum { SATURN_WEAPON_PAD_PHASES = 64, SATURN_WEAPON_PAD_PALETTES = 65 };
enum { SATURN_MODEL_FRAMES = 16 };
uint64_t SaturnNativeMaterialRevision(void);
uint32_t SaturnNativeModel(const SaturnNativePoly *polys, int count,
                           const SaturnNativePoly *frames, int frameCount);
/* Port: Object.scratch preserves the PRM's big-endian bytes. Registry tags use
   host byte order; packet tagging accepts the stored object representation. */
static inline uint32_t SaturnNativeObjectTag(uint32_t stored)
{
    const uint8_t *bytes = (const uint8_t *)&stored;
    return (uint32_t)bytes[0] << 24 | (uint32_t)bytes[1] << 16 |
           (uint32_t)bytes[2] << 8 | bytes[3];
}

static inline uint32_t SaturnNativeStoredTag(uint32_t tag)
{
    uint32_t stored;
    uint8_t *bytes = (uint8_t *)&stored;
    bytes[0] = (uint8_t)(tag >> 24); bytes[1] = (uint8_t)(tag >> 16);
    bytes[2] = (uint8_t)(tag >> 8); bytes[3] = (uint8_t)tag;
    return stored;
}

/* Authored model-header radius in Saturn 16.16 units (host units * 8192). */
void SaturnNativeSetRadius(uint32_t tag, int32_t radius);
int SaturnNativeRadius(uint32_t tag, int32_t *radius);
void SaturnNativeSetBillboard(uint32_t tag);
void SaturnNativeSetBillboardParts(uint32_t tag, uint32_t parts);
uint32_t SaturnNativeBillboardParts(uint32_t tag, int *count);
int SaturnNativeBillboard(uint32_t tag);
const SaturnNativePoly *SaturnNativeModelPoly(uint32_t tag, int index);
uint32_t SaturnNativeFrameTag(uint32_t modelTag, unsigned frame);
void SaturnNativeTrack(const SaturnNativePoly *polys, int count, const uint16_t *weaponPalettes);
void PortWeaponGridVisual(int leftFace, int rightFace, int angle, int depleted);
void PortRaceMaterialTick(void);
void SaturnNativeAnimatePalette(SaturnNativePoly *poly);
void SaturnNativeBind(const void *start, const void *end, uint32_t modelTag, int index);
void SaturnNativeBindTrack(const void *start, const void *end, int index);
void SaturnNativeForget(const void *packet);
const SaturnNativePoly *SaturnNativeLookup(const void *packet);
uint64_t SaturnNativeSkyRevision(void);
void SaturnNativeSky(const uint8_t *pixels, const uint16_t *palette, int width, int height);
const uint8_t *SaturnNativeSkyData(const uint16_t **palette, int *width, int *height);
/* Bounds are exclusive right/bottom; emit preserves draw order, including repeated writes. */
typedef int (*SaturnIndex)(void *user, float u, float v);
typedef void (*SaturnPixel)(void *user, int x, int y, uint16_t rgb, int blend, float depth, uint8_t alpha);
void SaturnVdp1Draw(const SaturnNativePoly *poly, const GpuVertex *vertices,
                   int left, int top, int right, int bottom, SaturnPixel emit, SaturnIndex sample, void *user);
void SaturnNativeAttributes(const SaturnNativePoly *poly, GpuVertex *v);
void SaturnShipLight(uint16_t track, uint16_t shade[4]);
uint16_t SaturnTrackLight(uint8_t r, uint8_t g, uint8_t b);
uint16_t SaturnGouraud(uint16_t colour, const float shade[3]);
void SaturnNativeShutdown(void);
#endif
