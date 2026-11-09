#include "gl_pipeline.h"
#include <stdlib.h>

#define RENDER_AFFINE_TEXTURES
#define RENDER_RGB5_VERTEX_COLOUR
#define RENDER_POSITION "    vec2 pixel = (modeIn.y & 128u) != 0u ? floor(position.xy) : position.xy;\n"
#define RENDER_OUTPUT_COLOUR \
    "vec3 OutputColour(vec3 col) {\n" \
    "    if ((mode.y & 4) == 0) return col;\n" \
    "    const int offsets[16] = int[16](-4,0,-3,1, 2,-2,3,-1, -3,1,-4,0, 3,-1,2,-2);\n" \
    "    ivec2 p = ivec2(floor(vPixel.xy / vPixel.z)) & 3;\n" \
    "    float d = (mode.y & 8) != 0 ? float(offsets[p.y * 4 + p.x]) : 0.0;\n" \
    "    return floor(clamp(floor(col * 255.0 + 0.0001) + d, 0.0, 255.0) / 8.0) * (8.0 / 255.0);\n" \
    "}\n"

static const char *ps1VertexSource =
#include "shaders/scene_vertex.h"
;
static const char *ps1FragmentSource =
#include "shaders/material_fragment.h"
;
static RenderProgram ps1Program;

static RenderProgram *Ps1Scene(void)
{
    if (!RenderGlProgramInit(&ps1Program, ps1VertexSource, ps1FragmentSource)) abort();
    return &ps1Program;
}

static void Ps1Present(const void *profile, const RenderPresent *p)
{
    const Ps1RenderSettings *settings = profile;
    RenderGlPresentDirect(p, settings->resample ? GL_LINEAR : GL_NEAREST);
}

static void Ps1Destroy(void)
{
    RenderGlProgramDestroy(&ps1Program);
}

const RenderPipeline ps1Pipeline = {Ps1Scene, Ps1Present, Ps1Destroy};
