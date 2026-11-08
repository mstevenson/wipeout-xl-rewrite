#ifndef WIPEOUT_PC_VISIBILITY_H
#define WIPEOUT_PC_VISIBILITY_H
#include <math.h>
#include <stdint.h>

/* POWERVR.EXE SceneRadiusCheck 45df80 and SquareRoot0 433f10. */
static inline int PcNativeObjectInRadius(int32_t dx, int32_t dy, int32_t dz,
                                         int32_t extent, int radius, int wide)
{
    int64_t x=dx, y=dy, z=dz, bound=(int64_t)radius*(wide?2:1);
    if (x <= -bound || x >= bound || y <= -bound || y >= bound || z <= -bound || z >= bound) return 0;
    uint32_t square=(uint32_t)dx*(uint32_t)dx+(uint32_t)dy*(uint32_t)dy+(uint32_t)dz*(uint32_t)dz;
    return (int64_t)sqrt((double)square)-extent-1024 < radius;
}

/* DrawSectionObjects 43fde0, including RotTrans's signed Y clamp. */
static inline int PcNativeSceneryVisible(const int32_t position[3], int32_t extent, int distance)
{
    int32_t far=(int32_t)((uint32_t)position[2]+(uint32_t)extent);
    int32_t near=(int32_t)((uint32_t)position[2]-(uint32_t)extent);
    if (far <= 2048 || near >= distance) return 0;
    int32_t y=position[1] < -32767 ? -32767 : position[1] > 32767 ? 32767 : position[1];
    int32_t x=(int32_t)((uint32_t)position[0]*160u)/far;
    y=(int32_t)((uint32_t)y*160u)/far;
    int32_t radius=(int32_t)((uint32_t)extent*160u)/far;
    return x >= -160-radius && x <= 160+radius && y >= -120-radius && y <= 120+radius;
}
#endif
