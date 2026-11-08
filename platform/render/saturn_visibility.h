#ifndef SATURN_VISIBILITY_H
#define SATURN_VISIBILITY_H
#include <stdint.h>
/* Original 16.16 Saturn coordinates: one host world unit is 8192. */
int SaturnNativeRoadDirection(int heading, int32_t dx, int32_t dz);
int SaturnNativeFarBucket(int courseIndex);
int SaturnSphereDepthVisible(int32_t zFixed, int32_t radiusFixed, int courseIndex);
int SaturnShipDepthVisible(int32_t zFixed, int cameraShip, int32_t radiusFixed, int courseIndex);
int SaturnModelDepthVisible(const int32_t zFixed[4], int mode, int courseIndex);
int SaturnTrackDepthVisible(const int32_t zFixed[4], int courseIndex);
#endif
