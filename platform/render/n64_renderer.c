#include "renderer.h"

static RenderStyle Prepare(const void *profile, const RenderMaterial *m)
{
    const N64RenderSettings *settings = profile;
    RenderStyle s = {m->raw ? SHADE_RAW : 0, 0};

    /* Source encoding affects decoding, while all rasterization uses this profile. */
    if (m->n64) {
        s.shade |= SHADE_N64;
    }
    s.shade |= SHADE_AFFINE_COLOUR;
    /* Screen-space HUD, menus and glyphs stay point sampled; projected world sprites still filter. */
    if (!m->ui && m->textured && settings->threePoint && (m->fullColour || !m->n64 || m->bounds)) {
        s.filter = 1;
    }
    return s;
}

static int Depth(const void *profile)
{
    (void)profile;
    return 1; /* N64 visibility always uses the depth buffer. */
}

static RenderGeometry Geometry(const void *profile)
{
    (void)profile;
    return (RenderGeometry){0, 0, 0, RENDER_LIGHT_PS1};
}

const Renderer n64Renderer = {RENDER_N64, "Nintendo 64", Prepare, Depth, Geometry};
