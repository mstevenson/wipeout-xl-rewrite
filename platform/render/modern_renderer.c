#include "renderer.h"

static RenderStyle Prepare(const void *profile, const RenderMaterial *m)
{
    const ModernRenderSettings *settings = profile;
    RenderStyle style = {.shade = RenderBaseShade(m)};
    if (m->textured && !m->ui) {
        if (!m->raw) style.shade |= SHADE_WORLD_TINT;
        if (settings->bilinear) {
            style.filter = 2;
            style.nativeBounds = !m->n64 && m->bounds;
        }
    }
    return style;
}

static int Depth(const void *profile)
{
    (void)profile;
    return 1;
}

static RenderGeometry Geometry(const void *profile)
{
    (void)profile;
    return (RenderGeometry){0, 0, 0, RENDER_LIGHT_PS1, 1};
}

static RenderResources Resources(const void *profile)
{
    const ModernRenderSettings *settings = profile;
    return (RenderResources){.originalTextures = 1, .hdr = settings->bloom};
}

const Renderer modernRenderer = {"Modern", Prepare, Depth, Geometry, Resources};
