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

static RenderGeometry Geometry(const ModernRenderSettings *settings)
{
    return (RenderGeometry){.lightFormat = RENDER_LIGHT_PS1, .worldBillboards = 1,
                            .smoothTrackLighting = settings->smoothTrackLighting,
                            .originalSceneryColours = 1};
}

static RenderResources Resources(const ModernRenderSettings *settings)
{
    return (RenderResources){.hdTexturesAtNativeScale = 1, .hdTexturesAlways = 1,
                             .hdr = settings->bloom || settings->weaponLights,
                             .cameraBlur = settings->cameraBlur && settings->cameraBlurAmount > 0,
                             .motionShutterSeconds = 0.008f};
}

static RenderPacketPolicy Packets(const ModernRenderSettings *settings)
{
    return (RenderPacketPolicy){.smoothTrackLighting = settings->smoothTrackLighting};
}


RenderStyle RenderPrepare(const RenderFrame *frame, const RenderMaterial *m)
{
    if (frame->id == RENDER_MODERN) return Prepare(&frame->settings.modern, m);
    return (RenderStyle){.shade = RenderBaseShade(m)};
}
RenderGeometry PortRenderGeometry(void)
{
    const RenderFrame *frame = RenderCurrent();
    return frame->id == RENDER_MODERN ? Geometry(&frame->settings.modern) : (RenderGeometry){0};
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
RenderPacketPolicy RenderPacketPolicyFor(const RenderFrame *frame)
{
    return frame->id == RENDER_MODERN ? Packets(&frame->settings.modern) : (RenderPacketPolicy){0};
}
