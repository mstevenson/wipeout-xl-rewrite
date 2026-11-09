#include "renderer.h"

static RenderStyle Prepare(const void *profile, const RenderMaterial *m)
{
    const PcRenderSettings *settings = profile;
    RenderStyle s = {.shade = RenderBaseShade(m)};

    /* Affine world billboards use PC modulation too; HUD colours retain the PS1 scale. */
    if (!m->ui && !m->fullColour) {
        s.shade |= SHADE_WORLD_TINT;
    }
    if (m->textured && settings->bilinear && (m->fullColour || !m->n64 || m->bounds)) {
        s.filter = RENDER_BILINEAR;
    }
    return s;
}

static int Depth(const void *profile)
{
    const PcRenderSettings *settings = profile;
    return settings->depth;
}

/* The PC loader uses diagonal 0-3 in packet order and retains Gouraud scenery.
   Its ship render queue replaces every corner with the RGB8 road light. */
static RenderGeometry Geometry(const void *profile)
{
    const PcRenderSettings *settings = profile;
    return (RenderGeometry){1, settings->lighting, settings->lighting,
                            settings->lighting ? RENDER_LIGHT_RGB8 : RENDER_LIGHT_PS1, 1};
}

const Renderer pcRenderer = {"PC", Prepare, Depth, Geometry};
