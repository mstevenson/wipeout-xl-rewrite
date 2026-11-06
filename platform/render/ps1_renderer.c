#include "renderer.h"

static RenderStyle Prepare(const void *profile, const RenderMaterial *m)
{
    const Ps1RenderSettings *p = profile;
    RenderStyle s = {m->raw ? SHADE_RAW : 0, 0};

    if (m->n64) {
        s.shade |= SHADE_N64;
    }
    s.shade |= SHADE_AFFINE_COLOUR;
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

const Renderer ps1Renderer = {RENDER_PS1, "PlayStation", Prepare, Depth, Geometry};
