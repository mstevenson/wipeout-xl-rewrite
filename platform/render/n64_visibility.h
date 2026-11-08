#ifndef WIPEOUT_N64_VISIBILITY_H
#define WIPEOUT_N64_VISIBILITY_H
#include <stdint.h>

/* Wipeout 64 USA: visibility policy, independent of the host renderer/PS1 state. */
typedef struct N64NativeShipState { uint8_t remembered; } N64NativeShipState;
uint32_t N64NativeSquareRoot(uint32_t value);
uint32_t N64NativeMagnitude(int32_t dx, int32_t dy, int32_t dz);
int32_t N64NativeViewZ(const int16_t row[3], int32_t translation,
                       int32_t dx, int32_t dy, int32_t dz);
/* Initialize remembered to 1 on race entry, separately for every view. */
int N64NativeShipVisible(N64NativeShipState *state, int32_t dx, int32_t dy, int32_t dz,
                        int32_t viewZ, int cameraShip, int destroyed, int players);
int N64NativeShipGate(N64NativeShipState *state, uint32_t magnitude, int32_t viewZ,
                     int cameraShip, int destroyed, int players);
/* Inputs retain the PRM/TRV world units, twice the original render units. */
int N64NativeSceneryVisible(int32_t viewZ, int32_t extent, int clipLevel);
int N64NativeSceneryDepthVisible(int32_t renderDepth, int32_t extent, int clipLevel);
int N64NativeSceneryBoundsVisible(const int32_t renderPosition[3], int32_t extent, int clipLevel);
typedef struct N64NativeMatrix { int16_t m[3][3]; int32_t t[3]; } N64NativeMatrix;
/* Relative translations must already be in the original half-world units. */
void N64NativeCompose(const N64NativeMatrix *parent, const N64NativeMatrix *relative, N64NativeMatrix *out);
int N64NativeObjectInRadius(int32_t dx, int32_t dy, int32_t dz, int32_t extent, int radius, int wide);

typedef struct N64NativeSection {
    int previous, next, junction; /* -1 for no junction */
    unsigned char junctionFace; /* either of the first two faces has flag 0x40 */
} N64NativeSection;
typedef void (*N64NativeSectionEmit)(int section, void *context);
int N64NativeRoadAngle(int heading, int32_t nextMinusCurrentX, int32_t nextMinusCurrentZ);
/* Returns 1 for the original authored south lists (caller draws those instead).
   course is 1..7 for the original courses, 0 for other geometry; sectionNumber
   is the original section record's number. angle is signed, 4096 units/turn. */
int N64NativeRoadSelect(const N64NativeSection *sections, int numSections, int start,
                       int players, int course, int sectionNumber, int angle,
                       int reducedView, int authoredBackward, int auxiliaryCamera,
                       N64NativeSectionEmit emit, void *context);
/* Exposed for the original-code oracle; the selector uses this exact walker. */
int N64NativeRoadWalk(const N64NativeSection *sections, int numSections, int start,
                     int players, int count, int direction, int flag,
                     N64NativeSectionEmit emit, void *context);
#endif
