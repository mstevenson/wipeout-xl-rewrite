#include "material_source.h"
#include "original.h"
#include "pc.h"
#include "saturn.h"
#include "psx/framecap.h"
#include "render/saturn_native.h"
#include <SDL3/SDL.h>
#include <string.h>

int PortActiveTrackNumber(void); /* game/src/portinfo.c */

void PortTagPrims(const void *start, const void *end, const char *what, const char *name, int index)
{
    if (!name && (strstr(what, "track") || strstr(what, "face")) && PortOriginalTrack() < 0) {
        SaturnNativeBindTrack(start, end, index);
    }
    FrameCapTag(start, end, what, name, index);
}

void PortTagObjectPrims(const void *start, const void *end, const char *what, const char *name, uint32_t assetTag, int index,
                        const int position[3])
{
    /* The synthetic PRM's scratch word is copied byte-for-byte by the loader. */
    SaturnNativeBind(start, end, SDL_Swap32BE(assetTag), index);
    FrameCapTag(start, end, what, name, index);
    if (start == end || !name || PortOriginalTrack() >= 0 || PortPcData() || PortSaturnDisc()) return;
    int track = PortActiveTrackNumber();
    MaterialEmission emission = {0};
    if (track == 20 && index == 0 && !strcmp(name, "grid1")) {
        /* The three animated neon faces are lit when modulation is white. */
        emission = (MaterialEmission){.enabled = 1, .whiteOnly = 1};
    }
    if (track == 2 && (index == 10 || index == 22 || index == 23 || index == 69) &&
        !strcmp(name, "new") && position &&
        position[0] == 56942 && position[1] == -1339 && position[2] == 42075) {
        /* Four wall triangles share a texture; only this bulb emits. */
        emission = (MaterialEmission){.enabled = 1, .uv = {174, 148, 11, 9}};
    }
    if (emission.enabled) GpuTagEmission(start, end, &emission);
}

void PortObjectVisualFrame(int *assetTag, unsigned frame)
{
    *assetTag = (int)SDL_Swap32BE(SaturnNativeFrameTag(SDL_Swap32BE((uint32_t)*assetTag), frame));
}

