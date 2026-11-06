/* Private page metadata survives packet decoding, and is cleared by PS1 pages. */
#include "../../platform/psx/libgpu.c"
#include <assert.h>
#include <stddef.h>
_Static_assert(offsetof(GpuDrawState, fade) == 13 * sizeof(int), "frame recording layout");
PortSettings portSettings = {.render = RENDER_DEFAULTS};
int PortOriginalTrackIs64(void) { return 0; }
static int frameClears, flushes;
void GpuFlush(void) { flushes++; }
void FrameCapClear(void) { frameClears++; }
static GpuVertex clipped[12];
static int clippedCount;
void GpuTriangles(const GpuDrawState *draw, const GpuVertex *v, int count)
{
    (void)draw;
    assert(count == 3 && clippedCount + count <= 12);
    memcpy(clipped + clippedCount, v, (size_t)count * sizeof(*v));
    clippedCount += count;
}

static void ImportedQuad(void)
{
    uint32_t packet[16] = {0};
    GpuDrawState draw;
    GpuVertex v[4] = {0};
    portSettings.render.selection = RENDER_PC;
    RenderCommit();
    walkTrack = -1;
    StateFor(&draw, 0, 0, 0);
    assert(!draw.n64); /* an untextured packet cannot inherit material identity */
    GteWriteControl(26, 160);
    resolvedMask = 15;
    for (int i = 0; i < 4; i++) {
        resolved[i] = (GtePrecise){.z = 200, .fade = 1,
            .view = {(i & 1) ? 100 : -100, (i & 2) ? 100 : -100, 200}};
        Vertex(&v[i], 0, &resolved[i]);
        v[i].r = (uint8_t)(20 + i * 40);
    }
    assert(AlternateQuad(&draw, v, 4, packet));
    /* Every corner's provenance counts, including a tag only on the last corner. */
    for (int i = 0; i < 4; i++) {
        resolved[i].n64 = 1;
        Vertex(&v[i], 0, &resolved[i]);
        assert(!AlternateQuad(&draw, v, 4, packet));
        v[i].n64 = resolved[i].n64 = 0;
    }
    for (int i = 0; i < 4; i++) v[i].n64 = resolved[i].n64 = 1;
    assert(!NearCut(&draw, v, 4, AlternateQuad(&draw, v, 4, packet)));
    /* Corner 3 crosses the near plane. Authored triangle 0/1/2 must remain intact. */
    resolved[3].z = resolved[3].view[2] = 1;
    clippedCount = 0;
    assert(NearCut(&draw, v, 4, AlternateQuad(&draw, v, 4, packet)));
    assert(clippedCount == 9);
    for (int i = 0; i < 3; i++) assert(clipped[i].r == v[i].r);
    for (int i = 0; i < clippedCount; i++) assert(clipped[i].n64);
    /* Native PC geometry keeps its alternate diagonal through the same clipper. */
    for (int i = 0; i < 4; i++) v[i].n64 = resolved[i].n64 = 0;
    clippedCount = 0;
    assert(NearCut(&draw, v, 4, AlternateQuad(&draw, v, 4, packet)));
    assert(clippedCount == 12);
    resolvedMask = 0;
    puts("Untextured N64 vertex provenance preserves authored quad diagonals before and during clipping: OK");
}

static void ClippedInterpolation(void)
{
    /* One corner is cut away. Both edge directions must agree at the new corners.
       At z=8 -> 32, the z=16 cut is 1/3 along the view-space edge but 2/3
       along its screen projection: RGB/affine UV must be 170, not 85. */
    const struct { float z, affine, perspective; int r, g, b, alpha; } cases[] = {
        {8, 170, 85, 170, 85, 150, 85},
        {0, 255, 127.5f, 255, 0, 210, 128},
        {-8, 306, 153, 255, 0, 246, 153}
    };
    GteWriteControl(26, 32);
    for (int id = RENDER_PS1; id <= RENDER_N64; id++) {
        for (int affine = 0; affine <= 1; affine++) {
            portSettings.render = (RenderSettings)RENDER_DEFAULTS;
            portSettings.render.selection = (RendererId)id;
            portSettings.render.ps1.affineTextures = affine;
            RenderCommit();
            /* Pending changes, including to the active profile, wait for a boundary. */
            portSettings.render.ps1.affineTextures ^= 1;
            for (int imported = 0; imported <= 1; imported++) {
                for (size_t c = 0; c < sizeof(cases) / sizeof(cases[0]); c++) {
                    GpuDrawState draw = {.textured = 1, .semi = -1, .n64 = imported};
                    GpuVertex v[3] = {0};
                    resolved[0] = (GtePrecise){.z = cases[c].z, .view = {1, 1, cases[c].z}};
                    resolved[1] = (GtePrecise){.z = 32, .view = {32, 0, 32}};
                    resolved[2] = (GtePrecise){.z = 32, .view = {0, 32, 32}};
                    resolvedMask = 7;
                    for (int i = 0; i < 3; i++) {
                        v[i] = (GpuVertex){.w = resolved[i].z, .r = i ? 255 : 0,
                            .g = i ? 0 : 255, .b = i ? 210 : 30, .a = i ? 255 : 0,
                            .u = i ? 255 : 0, .v = i ? 0 : 255, .n64 = imported, .world = 1};
                    }
                    clippedCount = 0;
                    assert(NearCut(&draw, v, 3, 0) && clippedCount == 6);
                    int intersections = 0;
                    for (int i = 0; i < clippedCount; i++) {
                        const GpuVertex *p = &clipped[i];
                        assert(p->n64 == imported && p->world);
                        if (p->w == 16) {
                            intersections++;
                            float u = id == RENDER_PS1 && affine ? cases[c].affine : cases[c].perspective;
                            assert(p->r == cases[c].r && p->g == cases[c].g && p->b == cases[c].b);
                            assert(SDL_fabsf(p->u - u) < 0.001f && SDL_fabsf(p->v - (255 - u)) < 0.001f);
                            assert(p->a == cases[c].alpha); /* fade stays perspective-correct */
                        } else {
                            assert(p->w == 32 && p->r == 255 && p->g == 0 && p->b == 210);
                            assert(p->a == 255 && p->u == 255 && p->v == 0);
                        }
                    }
                    assert(intersections == 3); /* first intersection appears in both fan triangles */
                }
            }
        }
    }
    resolvedMask = 0;
    puts("Near clipping respects committed renderer RGB/UV interpolation, perspective fade and eye-plane crossings: OK");
}

static void RendererBoundaries(void)
{
    u_long table[1];
    for (int id = RENDER_PS1; id <= RENDER_N64; id++) {
        portSettings.render = (RenderSettings)RENDER_DEFAULTS;
        portSettings.render.selection = (RendererId)id;
        RenderCommit();
        upcoming.table = shownTables.table = table;
        drawnFrame.pending = 1;
        frameClears = flushes = 0;
        if (id != RENDER_PS1) portSettings.render.ps1.dither ^= 1;
        if (id != RENDER_PC) portSettings.render.pc.depth ^= 1;
        if (id != RENDER_N64) portSettings.render.n64.divot ^= 1;
        PsxRendererBoundary();
        assert(flushes == 1 && frameClears == 0 && drawnFrame.pending);
        assert(upcoming.table == table && shownTables.table == table);
        assert(!memcmp(&RenderCurrent()->settings, &portSettings.render, sizeof(RenderSettings)));
        if (id == RENDER_PS1) portSettings.render.ps1.dither ^= 1;
        if (id == RENDER_PC) portSettings.render.pc.depth ^= 1;
        if (id == RENDER_N64) portSettings.render.n64.divot ^= 1;
        PsxRendererBoundary();
        assert(flushes == 2 && frameClears == 1 && !drawnFrame.pending);
        assert(!upcoming.table && !shownTables.table);
    }
    portSettings.render = (RenderSettings)RENDER_DEFAULTS;
    RenderCommit();
    portSettings.render.selection = RENDER_PS1;
    assert(!RenderCommit()); /* same effective renderer, different selection spelling */
    puts("Inactive profiles preserve interpolation and recordings at the real frame boundary: OK");
}

int main(void)
{
    RendererBoundaries();
    {
        GtePrecise p = {.x = 12, .y = 24, .fade = 1, .view = {0, 0, 100}};
        GpuVertex v;
        Vertex(&v, 0, &p);
        assert(v.world && v.w == 0); /* affine billboard retains world identity */
        p.view[2] = 0;
        Vertex(&v, 0, &p);
        assert(!v.world && v.w == 0); /* precise 2D PC UI */
        Vertex(&v, 0, NULL);
        assert(!v.world && v.w == 0); /* packet-only HUD */
    }
    GpuDrawState draw;
    unsigned page = GetTPage(1, 2, 128, 768);
    graphType = 0;
    SetTexpage(page | PORT_TPAGE_N64);
    StateFor(&draw, 1, 1, 0);
    assert(draw.n64 && !draw.filtered && draw.pageX == 128 && draw.pageY == 768 && draw.depth == 1 && draw.semi == 2);
    TextureFromPage(&draw, page | PORT_TPAGE_N64 | PORT_TPAGE_PS1_LIGHT);
    assert(draw.n64 && draw.ps1Light && draw.pageX == 128 && draw.pageY == 768 && draw.depth == 1);
    /* Lighting belongs to the road packet, never to the following object or sprite. */
    SetTexpage(page | PORT_TPAGE_N64 | PORT_TPAGE_PS1_LIGHT);
    StateFor(&draw, 1, 0, 0);
    assert(draw.n64 && !draw.ps1Light);
    TextureFromPage(&draw, page | PORT_TPAGE_PS1_LIGHT);
    assert(!draw.n64 && !draw.ps1Light);
    StateFor(&draw, 0, 0, 0);
    assert(!draw.n64 && !draw.filtered); /* untextured polygons cannot inherit the last texture identity */
    assert(DrawModeWord(1, 0, page | PORT_TPAGE_N64) & PORT_TPAGE_N64);
    SetTexpage(page);
    StateFor(&draw, 1, 1, 0);
    assert(!draw.n64 && !draw.filtered && draw.pageY == 768); /* high VRAM is not itself an N64 tag */
    TextureFromPage(&draw, GetTPage(0, 0, 640, 256));
    assert(!draw.n64 && !draw.filtered && draw.pageX == 640 && draw.pageY == 256 && draw.depth == 0);
    puts("N64 material tag survives GPU packets without leaking into PlayStation pages: OK");
    {
        uint32_t packets[16];
        GpuVertex v[4] = {0};
        assert(!AlternateQuad(&draw, v, 4, packets));
        portSettings.render.selection = RENDER_PC; RenderCommit();
        assert(AlternateQuad(&draw, v, 4, packets));
        assert(!AlternateQuad(&draw, v, 3, packets));
        draw.n64 = 1;
        assert(!AlternateQuad(&draw, v, 4, packets));
        draw.n64 = 0;
        walkTrack = 0;
        tableTrack[0].count = 1;
        tableTrack[0].ranges[0].start = (const char *)packets;
        tableTrack[0].ranges[0].end = (const char *)(packets + 8);
        assert(!AlternateQuad(&draw, v, 4, packets));
        assert(AlternateQuad(&draw, v, 4, packets + 8));
        puts("PC object quad diagonal leaves track and N64 geometry unchanged: OK");
    }
    ImportedQuad();
    ClippedInterpolation();
    return 0;
}
