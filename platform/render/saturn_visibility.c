#include "saturn_visibility.h"
#include <math.h>

int SaturnNativeRoadDirection(int heading, int32_t dx, int32_t dz)
{
    /* 06054bd0: round the smaller/larger ratio to the 1024-entry atan
       table. Its 1025 values equal round(atan(i/1024)*4096/(2*pi)). */
    int64_t x = dx < 0 ? -(int64_t)dx : dx;
    int64_t z = dz < 0 ? -(int64_t)dz : dz;
    while (x > 0x100000 || z > 0x100000) { x >>= 1; z >>= 1; }
    int angle;
    if (!z) angle = 1024;
    else {
        int64_t large = x > z ? x : z, small = x > z ? z : x;
        int ratio = (int)((small * 1024 + large / 2) / large);
        angle = (int)lround(atan(ratio / 1024.0) * (4096.0 / 6.283185307179586));
        if (x > z) angle = 1024 - angle;
    }
    if (dx < 0) angle = dz < 0 ? angle - 2048 : -angle;
    else if (dz < 0) angle = 2048 - angle;
    unsigned relative = ((unsigned)heading - (unsigned)angle) & 4095;
    /* 060225f2..06022620, including the exact quadrant boundaries. */
    if (relative < 512 || relative >= 3584) return 1;
    if (relative < 1536) return 4;
    if (relative < 2560) return 2;
    return 8;
}

int SaturnNativeFarBucket(int courseIndex)
{
    /* 06024188 course descriptors, 06045abc round-to-eight, 0604ddb0
       round-to-sixteen then halve for the 16-bit drawing context. */
    unsigned far = courseIndex >= 6 && courseIndex < 8 ? 4000 : 5000;
    far = (far + 7) & ~7u;
    return (int)(((far + 15) >> 4) << 3);
}

int SaturnSphereDepthVisible(int32_t zFixed, int32_t radiusFixed, int courseIndex)
{
    /* 0604c3ce..0604c3f6: both divisions truncate towards zero. */
    int32_t z = zFixed / 2, radius = radiusFixed / 2;
    int32_t near = (int32_t)((uint32_t)z + (uint32_t)radius);
    int32_t far = (int32_t)((uint32_t)z - (uint32_t)radius);
    return near >= 0 && far < SaturnNativeFarBucket(courseIndex) * 65536;
}

int SaturnShipDepthVisible(int32_t zFixed, int cameraShip, int32_t radiusFixed, int courseIndex)
{
    /* Camera ship follows the separate 06028306 path. Other ships use the
       unsigned range comparison at 06028562, then the model sphere test. */
    if (cameraShip) return 1;
    return (uint32_t)zFixed - UINT32_C(0x00140001) <= UINT32_C(0x0f8bfffe) &&
           SaturnSphereDepthVisible(zFixed, radiusFixed, courseIndex);
}

int SaturnTrackDepthVisible(const int32_t zFixed[4], int courseIndex)
{
    /* 06014364 / 060144c0: the farthest corner controls the whole tile. */
    int32_t z = zFixed[0];
    for (int i = 1; i < 4; i++) if (zFixed[i] > z) z = zFixed[i];
    return z >= 0 && (uint32_t)z < (uint32_t)SaturnNativeFarBucket(courseIndex) * 131072u;
}

int SaturnModelDepthVisible(const int32_t zFixed[4], int mode, int courseIndex)
{
    /* 0605d6ec/0605d70c/0605d754/0605d730 use projected integer buckets. */
    int32_t depth[4], bucket;
    for (int i = 0; i < 4; i++) {
        int32_t z = zFixed[i];
        depth[i] = z >= 0 ? z / 131072 : (int32_t)(-((-(int64_t)z + 131071) / 131072));
    }
    bucket = depth[0];
    if (mode == 0) {
        int sum = depth[0] + depth[1] + depth[2] + depth[3];
        bucket = sum >= 0 ? sum / 4 : -((-sum + 3) / 4);
    } else {
        for (int i = 1; i < 4; i++)
            if (mode == 2 ? depth[i] < bucket : depth[i] > bucket) bucket = depth[i];
        if (mode == 3) bucket += 32;
    }
    if (mode == 1 || mode == 3) return (uint32_t)bucket < (uint32_t)SaturnNativeFarBucket(courseIndex);
    return bucket < SaturnNativeFarBucket(courseIndex);
}
