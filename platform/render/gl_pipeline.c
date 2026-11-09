#include "gl_pipeline.h"

static const RenderPipeline *const pipelines[RENDER_COUNT] = {
#define RENDER_PROFILE(id, field, w, h, palH, rate, palRate, interpolate) [id] = &field##Pipeline,
#include "profiles.def"
#undef RENDER_PROFILE
};

const RenderPipeline *RenderGlPipeline(RendererId id)
{
    return pipelines[id >= RENDER_PS1 && id < RENDER_COUNT ? id : RENDER_PS1];
}
