/* Source artwork metadata, independent of renderer selection and recording. */
#ifndef PORT_MATERIAL_SOURCE_H
#define PORT_MATERIAL_SOURCE_H
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif

typedef struct MaterialEmission {
    int enabled, whiteOnly;
    int uv[4]; /* centre U/V and half-width/height; zero width means the whole face */
} MaterialEmission;

void PortTagTrackEncoding(const void *packet, int ps1);
int ModernPacketPs1Light(const void *packet);
void ModernPacketReused(const void *packet);
int ModernPacketEmission(const void *packet, MaterialEmission *emission);
int ModernPacketTrack(const void *packet, uint32_t colours[4]);
int ModernSparkPacket(const void *packet);
void ModernMetadataReset(void);
void PortTagObjectPrims(const void *start, const void *end, const char *what, const char *name,
                        uint32_t assetTag, int index, const int position[3]);
#ifdef __cplusplus
}
#endif
#endif
