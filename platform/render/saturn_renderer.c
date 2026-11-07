#include "renderer.h"

static RenderStyle Prepare(const void *profile, const RenderMaterial *m)
{
    const SaturnRenderSettings *p = profile;
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
    }
    if (!m->ui) {
        s.filter = p->mesh ? RENDER_SAMPLE_MESH : RENDER_SAMPLE_SATURN_ALPHA;
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

const Renderer saturnRenderer = {RENDER_SATURN, "Sega Saturn", Prepare, Depth, Geometry};
