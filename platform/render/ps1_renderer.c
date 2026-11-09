#include "renderer.h"

static RenderStyle Prepare(const void *profile, const RenderMaterial *m)
{
    const Ps1RenderSettings *p = profile;
    RenderStyle s = {.shade = RenderBaseShade(m)};

    if (p->quantizeVertices) {
        s.shade |= SHADE_SNAP;
    }
    if (p->affineTextures) {
        s.shade |= SHADE_AFFINE_UV;
    }
    if (p->colourPrecision) {
        s.shade |= SHADE_PS1_COLOUR;
        if (p->dither && m->dither) {
            s.shade |= SHADE_DITHER;
        }
    }
    return s;
}

static int Depth(const void *profile)
{
    const Ps1RenderSettings *settings = profile;
    return settings->depth;
}

static RenderGeometry Geometry(const void *profile)
{
    (void)profile;
    return (RenderGeometry){0, 0, 0, RENDER_LIGHT_PS1};
}

const Renderer ps1Renderer = {"PlayStation", Prepare, Depth, Geometry};
