/* Exercise production inspection and serialize fixtures for the Python reader. */
#include "../../platform/psx/framecap.c"
#include <assert.h>

PortSettings portSettings = {.render = RENDER_DEFAULTS};
int PortOriginalTrackIs64(void) { return 0; }

int main(int argc, char **argv)
{
    assert(argc == 2);
    FrameCapPrim prims[4] = {0};
    const float l[3] = {1.0f/3, 1.0f/3, 1.0f/3};
    count = 4;
    for (int mode = 0; mode < 4; mode++) {
        FrameCapPrim *p = &prims[mode];
        FrameCapFrame *f = &frames[mode];
        f->render = (RenderFrame){.settings = RENDER_DEFAULTS,
                                 .id = mode < 2 ? RENDER_PS1 : mode == 2 ? RENDER_PC : RENDER_N64};
        f->render.settings.ps1.affineTextures = mode == 0;
        f->render.settings.ps1.colourPrecision = 0;
        f->prims = p; f->count = 1; f->vram = -1;
        p->count = 3; p->kind = FRAMECAP_POLYGON;
        p->v[0] = (GpuVertex){.x=.9f, .y=.9f, .w=1, .a=0};
        p->v[1] = (GpuVertex){.x=6.9f, .y=.9f, .w=2, .r=120, .u=100, .a=120};
        p->v[2] = (GpuVertex){.x=.9f, .y=6.9f, .w=4, .r=240, .a=240};
        current = mode;
        FrameCapTagRecord(p);
        assert(p->shade & SHADE_AFFINE_COLOUR);
        assert(!!(p->shade & SHADE_AFFINE_UV) == (mode == 0));
        /* Changing live settings must not reinterpret the captured primitive. */
        portSettings.render.selection = RENDER_PC; RenderCommit();
        const GpuVertex *v[3] = {&p->v[0], &p->v[1], &p->v[2]};
        FrameCapHit hit = {0};
        Shade(p, v, l, NULL, &hit);
        assert(fabsf(hit.r - 120) < .001f);
        assert(fabsf(hit.u - (mode == 0 ? 100.0f/3 : 200.0f/7)) < .001f);
        assert(fabsf(hit.alpha - 480.0f/7) < .001f); /* fade remains perspective */
        FrameCapHit picked[2];
        assert(FrameCapHits(mode, 0, 0, picked, 2) == (mode < 2 ? 1 : 0)); /* snapped coverage */
    }
    FILE *file = fopen(argv[1], "wb");
    assert(file); WriteFrames(file); assert(!fclose(file));
    puts("Frame inspection: captured UV/colour interpolation, perspective fade and snapped coverage: OK");
    return 0;
}
