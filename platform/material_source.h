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

void PortTagPrims(const void *start, const void *end, const char *what, const char *name, int index);
void PortTagObjectPrims(const void *start, const void *end, const char *what, const char *name,
                        uint32_t assetTag, int index, const int position[3]);
void PortObjectVisualFrame(int *assetTag, unsigned frame);
#ifdef __cplusplus
}
#endif
#endif
