/* The retail comparison runs with presentation enhancements disabled, as a
   lockstep trace does. Observations are inert; accidentally entering a host-only
   geometry path aborts instead of silently weakening the packet comparison. */
#include "functions.h"
#include "portfar.h"
#include "portinterp.h"
#include <stdlib.h>

int portPal;
int renderModernActive;
const RenderFrame *RenderCurrent(void) { static const RenderFrame frame = {0}; return &frame; }
RenderGeometry PortRenderGeometry(void) { return (RenderGeometry){0}; }
int PortTracing(void) { return 1; }
int PortScreenMargin(void) { return 0; }
int PortOriginalLooks(void) { return 0; }
int PortOriginalTrackIs64(void) { return 0; }
int PortPcData(void) { return 0; }
void PortBindScreen(void *corner, uint64_t key) {}
void PortFarMarkFace(Face *face) {}
unsigned char *PortFarObjectPrimitives(Object *object) { return NULL; }
const unsigned char *PortFarObjectDrawn(Object *object, int *complete) { abort(); }
char *PortFarAlloc(int bytes) { abort(); }
char *PortNearAlloc(int bytes) { abort(); }
void PortFarRelease(char *end) { abort(); }
int PortFarFaceDrawn(Face *face) { abort(); }
void PortFarDepthBias(double scale) { abort(); }
u_long *PortFarSlot(int entry, double depth) { abort(); }
void PortFarProject(const PortView *view, double x, double y, double z, PortFarVertex *out) { abort(); }
void PortFarProjectAny(double x, double y, double z, PortFarVertex *out) { abort(); }
void PortModernHeapReset(const HeapBlock *head) {}
int PortModernObjectRgb8(const Object *object) { return 0; }
int PortModernLightFormat(const Face *face) { abort(); }
int PortModernTrackCorners(const Face *face, uint32_t colours[4]) { abort(); }
uint32_t PortModernTrackSample(const uint32_t colours[4], int x, int y) { abort(); }
int PortModernWallParticle(const SParticle *particle) { return 0; }
void PortModernTagTrackFace(const POLY_FT4 *poly, const Face *face) { abort(); }
void PortTagTrackEncoding(const void *packet, int ps1) { abort(); }
void PortTagTrackLight(const void *packet, const uint32_t colours[4]) { abort(); }
void PortTagSparkStreak(const LINE_G2 *line) { abort(); }
void PortShipPackets(int ship, const void *start, const void *end, const CVECTOR *roadLight) { abort(); }
void PortTagPrims(const void *start, const void *end, const char *what, const char *name, int index) {}
void PortTagObjectPrims(const void *start, const void *end, const char *what, const char *name,
                        uint32_t assetTag, int index, const int position[3]) {}
/* These are linked for unreachable near-plane extensions. */
void SetPolyG3(POLY_G3 *p) { abort(); }
void SetPolyG4(POLY_G4 *p) { abort(); }
void SetPolyGT3(POLY_GT3 *p) { abort(); }
void SetPolyGT4(POLY_GT4 *p) { abort(); }
