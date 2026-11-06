#include "gl_pipeline.h"
#include <stdlib.h>

#define RENDER_POSITION "    vec2 pixel = position.xy;\n"
#define RENDER_TEXCOORD "    vTexel = vec3(texel, 1.0);\n"
#define RENDER_VERTEX_COLOUR "    return c;\n"
#define RENDER_OUTPUT_COLOUR "vec3 OutputColour(vec3 c) { return c; }\n"
#define RENDER_TINT "vec3 TextureTint() { return VertexColour() * ((mode.y & 66) != 0 && ps1Light == 0 ? 1.0 : 255.0 / 128.0); }\n"
#define RENDER_FILTER \
    "#define FILTER_TAPS 4\n" \
    "vec2 FilterPosition(vec2 t) { return t - 0.5; }\n" \
    "vec2 FilterHdPosition(vec2 t) { return t * vec2(hd.zw) / vec2(hdTexels.zw) - 0.5; }\n" \
    "vec2 FilterFraction(vec2 f) { return f; }\n" \
    "ivec2 FilterCorner(int i, vec2 f) { return ivec2(i & 1, i >> 1); }\n" \
    "float FilterWeight(int i, vec2 f) { vec2 w = mix(1.0 - f, f, vec2(FilterCorner(i, f))); return w.x * w.y; }\n"

static const char *pcVertexSource =
#include "shaders/scene_vertex.h"
;
static const char *pcFragmentSource =
#include "shaders/material_fragment.h"
;
static RenderProgram pcProgram;

static RenderProgram *PcScene(void)
{
    if (!RenderGlProgramInit(&pcProgram, pcVertexSource, pcFragmentSource)) abort();
    return &pcProgram;
}

static void PcPresent(const void *profile, const RenderPresent *p)
{
    (void)profile;
    RenderGlPresentDirect(p, GL_LINEAR);
}

static void PcDestroy(void)
{
    RenderGlProgramDestroy(&pcProgram);
}

const RenderPipeline pcPipeline = {PcScene, PcPresent, PcDestroy};
