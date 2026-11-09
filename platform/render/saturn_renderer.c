#include "renderer.h"

static RenderStyle Prepare(const void *profile, const RenderMaterial *m)
{
    const SaturnRenderSettings *p = profile;
    RenderStyle s = {.shade = RenderBaseShade(m)};

    if (p->quantizeVertices) {
        s.shade |= SHADE_SNAP;
    }
    if (p->affineTextures) {
        s.shade |= SHADE_AFFINE_UV;
    }
    if (p->colourPrecision) {
        s.shade |= SHADE_PS1_COLOUR;
    }
    if (!m->ui) {
        s.coverage = p->mesh ? RENDER_COVERAGE_MESH : RENDER_COVERAGE_HALF;
    }
    return s;
}

static int Depth(const void *profile)
{
    const SaturnRenderSettings *settings = profile;
    return settings->depth;
}

static RenderGeometry Geometry(const void *profile)
{
    (void)profile;
    return (RenderGeometry){0, 1, 1, RENDER_LIGHT_PS1};
}

static RenderPacketPolicy Packets(const void *profile)
{
    const SaturnRenderSettings *settings = profile;
    return (RenderPacketPolicy){.nativeQuads = settings->affineTextures, .nativeSky = 1, .shadowMask = 1};
}

const Renderer saturnRenderer = {
    .name = "Sega Saturn", .prepare = Prepare, .depth = Depth, .geometry = Geometry, .packets = Packets,
};
