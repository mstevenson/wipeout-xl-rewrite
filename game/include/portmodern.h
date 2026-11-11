#ifndef PORT_MODERN_GAME_H
#define PORT_MODERN_GAME_H
#include "../../platform/render/modern/modern.h"
#include "../../platform/render/modern/metadata.h"
void PortModernSceneLoaded(Object **objects, int count, Object *sky);
int PortModernObjectRgb8(const Object *object);
void PortResetTrackLighting(void);
void PortTagTrackEncoding(const void *packet, int ps1);
void PortModernTrackLoaded(void);
void PortModernHeapReset(const HeapBlock *head);
int PortModernLightFormat(const Face *face);
void PortModernTrackColour(const Face *face);
int PortModernTrackCorners(const Face *face, uint32_t colours[4]);
uint32_t PortModernTrackSample(const uint32_t colours[4], int x, int y);
void PortModernTagTrackFace(const POLY_FT4 *poly, const Face *face);
CVECTOR PortShipTrackLight(const ShipData *ship);
void PortCaptureWeaponLights(void);
void PortSetShieldFlash(int ship, int red, int green);
void PortWallSparkLight(const ShipData *ship, const VECTOR *pos, const Face *wall);
void PortTagTrackLight(const void *packet, const uint32_t colours[4]);
void PortShipPackets(int ship, const void *start, const void *end, const CVECTOR *roadLight);
void PortShieldPackets(int ship, const void *start, const void *end);
void PortTagSparkStreak(const LINE_G2 *line);
void PortModernParticleSpawn(const SParticle *particle);
int PortModernWallParticle(const SParticle *particle);
void PortModernWallBegin(void);
void PortModernWallEnd(void);
#endif
