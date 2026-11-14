#include "modern.h"

static RenderStyle Prepare(const ModernRenderSettings *settings, const RenderMaterial *m)
{
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

static RenderResources Resources(const ModernRenderSettings *settings)
{
    return (RenderResources){.hdr = settings->bloom || settings->weaponLights,
                             .cameraBlur = settings->cameraBlur && settings->cameraBlurAmount > 0};
}

RenderStyle RenderPrepare(const RenderFrame *frame, const RenderMaterial *m)
{
    if (frame->id == RENDER_MODERN) return Prepare(&frame->settings.modern, m);
    return (RenderStyle){.shade = RenderBaseShade(m)};
}
RenderGeometry PortRenderGeometry(void)
{
    const RenderFrame *frame = RenderCurrent();
    return frame->id == RENDER_MODERN ? (RenderGeometry){frame->settings.modern.smoothTrackLighting}
                                      : (RenderGeometry){0};
}
RenderResources RenderResourcesFor(const RenderFrame *frame)
{
    return frame->id == RENDER_MODERN ? Resources(&frame->settings.modern) : (RenderResources){0};
}
RenderResources RenderFrameResources(const RenderFrame *frame, int linked, int tracing)
{
    RenderResources resources = RenderResourcesFor(frame);
    if (linked || tracing) resources.cameraBlur = 0;
    return resources;
}
