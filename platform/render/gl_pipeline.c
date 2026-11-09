#include "gl_pipeline.h"

const RenderPipeline *RenderGlPipeline(RendererId id)
{
    switch (id) {
    case RENDER_MODERN: return &modernPipeline;
    case RENDER_N64: return &n64Pipeline;
    case RENDER_SATURN: return &saturnPipeline;
    case RENDER_PC: return &pcPipeline;
    default: return &ps1Pipeline;
    }
}
