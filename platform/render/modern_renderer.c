#include "renderer.h"

static RenderStyle Prepare(const void *profile, const RenderMaterial *m)
{
    const ModernRenderSettings *settings = profile;
    RenderStyle style = {.shade = RenderBaseShade(m)};
    if (settings->smoothTrackLighting && m->smoothTrackLighting) style.shade &= ~SHADE_AFFINE_COLOUR;
    if (m->textured && !m->ui) {
        if (!m->raw) style.shade |= SHADE_WORLD_TINT;
        if (settings->bilinear) {
            style.filter = RENDER_BILINEAR;
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
    const ModernRenderSettings *settings = profile;
    return (RenderGeometry){.lightFormat = RENDER_LIGHT_PS1, .worldBillboards = 1,
                            .smoothTrackLighting = settings->smoothTrackLighting};
}

static RenderResources Resources(const void *profile)
{
    const ModernRenderSettings *settings = profile;
    return (RenderResources){.originalTextures = 1, .hdr = settings->bloom || settings->weaponLights};
}

static RenderPacketPolicy Packets(const void *profile)
{
    const ModernRenderSettings *settings = profile;
    return (RenderPacketPolicy){.smoothTrackLighting = settings->smoothTrackLighting};
}

const Renderer modernRenderer = {"Modern", Prepare, Depth, Geometry, Resources, Packets};
