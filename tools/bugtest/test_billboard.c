/* PC BuildQuads geometry in model units, with real projection bindings; PS1 packets stay affine. */
#include "../../game/src/objdraw.c"
#include <assert.h>
#include <string.h>

static POLY_FT4 packet;
static u_long ordering[8192];
static int linked;
static u_long *packetSlot;
static Texture texture = {.textureW = 16, .textureH = 128, .tsb = 17, .cba = 42,
                          .u0 = 16, .v0 = 32, .u1 = 79, .v1 = 32,
                          .u2 = 16, .v2 = 159, .u3 = 79, .v3 = 159};
Texture *textureTable[] = {&texture};

void AddPrim(u_long *slot, void *poly)
{
    assert(poly == &packet);
    linked++;
    packetSlot = slot;
}
u_long *PortFarSlot(int entry, double depth)
{
    (void)depth;
    assert(entry >= 0 && entry < 8192);
    return ordering + entry;
}
/* Only the projection's transport is substituted; use the production view projection and registry. */
void PortFarProjectAny(double x, double y, double z, PortFarVertex *out)
{
    double view[3] = {x, y, z < PORT_FAR_NEAR ? PORT_FAR_NEAR : z};
    float sx, sy;
    GteProjectView(view, &sx, &sy);
    int ix = (int)fmax(-1024, fmin(1023, floor(sx)));
    int iy = (int)fmax(-1024, fmin(1023, floor(sy)));
    out->screen = PackScreen(ix, iy);
    out->handle = GteRecordProjection((uint32_t)out->screen, sx, sy, (float)z, 1);
    GteRecordView(out->handle, x, y, z);
}
static void Corners(GtePrecise p[4])
{
    const short *xy[] = {&packet.x0, &packet.x1, &packet.x2, &packet.x3};
    for (int i = 0; i < 4; i++) {
        assert(GtePreciseOf(GteBoundHandle(xy[i], *(const uint32_t *)xy[i]), &p[i]));
        assert(p[i].key && p[i].exact && !p[i].background);
    }
    for (int i = 0; i < 4; i++) for (int j = 0; j < i; j++) assert(p[i].key != p[j].key);
    assert(packet.code == 0x2c && ((unsigned char *)&packet)[3] == 9);
    assert(packet.r0 == 128 && packet.g0 == 96 && packet.b0 == 64);
    assert(packet.tpage == 17 && packet.clut == 42);
    assert(packet.u0 == 16 && packet.u1 == 79 && packet.v0 == 32 && packet.v2 == 159);
}
static void Expect(const GtePrecise *p, double x, double y, double z)
{
    assert(fabs(p->view[0] - x) < .001 && fabs(p->view[1] - y) < .001 && fabs(p->view[2] - z) < .001);
    assert(fabs(p->z - z) < .001);
    if (z >= PORT_FAR_NEAR) {
        assert(fabs(p->x - (160 + 160 * x / z)) < .001);
        assert(fabs(p->y - (120 + 160 * y / z)) < .001);
    }
}
int main(void)
{
    SVECTOR position = {0, 100, 4096};
    Object object = {.vertices = &position, .vertexCount = 1};
    SPR sprite = {.type = TypeBSPR, .width = 1024, .height = 2048, .colour = {128, 96, 64, 0}};
    GtePrecise p[4], other[4];
    GteWriteControl(0, 4096); GteWriteControl(2, 4096); GteWriteControl(4, 4096);
    GteWriteControl(24, 160 << 16); GteWriteControl(25, 120 << 16); GteWriteControl(26, 160);
    currentDrawing = 123;
    char *out = (char *)&packet;
    assert(DrawWorldSprite(&object, 0, out, ordering, &sprite) == out + sizeof(packet));
    assert(linked == 1 && packetSlot == ordering + 1024);
    Corners(p);
    /* LoadPrmCommon width*2 and BuildQuads 2*width*TIM-word-width/256. */
    Expect(&p[0], 256, -924, 4096); Expect(&p[1], -256, -924, 4096);
    Expect(&p[2], 256, 100, 4096); Expect(&p[3], -256, 100, 4096);
    sprite.width = 20000;
    assert(DrawWorldSprite(&object, 0, out, ordering, &sprite) != out);
    Corners(other);
    Expect(&other[0], -3192, -924, 4096); /* PC SpriteQuad.width wraps after doubling into 16 bits. */
    sprite.width = 1024;
    sprite.type = TypeTSPR;
    assert(DrawWorldSprite(&object, 1, out, ordering, &sprite) != out);
    Corners(other);
    for (int i = 0; i < 4; i++) {
        assert(!memcmp(p[i].view, other[i].view, sizeof(p[i].view)));
        assert(p[i].key != other[i].key); /* two sprites may share one anchor */
    }
    /* Pitched camera, anchor away from the optical axis: golden BuildQuads corners.
       Both horizontal facing and vertical pitch contribute distinct corner depths. */
    PortView view = {.m = {{1, 0, 0}, {0, .8, -.6}, {0, .6, .8}}, .t = {1500, 200, 2000}};
    farView = &view; position = (SVECTOR){0};
    object.flags = PORT_OBJECT_N64;
    assert(DrawWorldSprite(&object, 0, out, ordering, &sprite) != out);
    Corners(p);
    Expect(&p[0], 1704.8, -527.04, 1262.72); Expect(&p[1], 1295.2, -711.36, 1508.48);
    Expect(&p[2], 1704.8, 292.16, 1877.12); Expect(&p[3], 1295.2, 107.84, 2122.88);
    for (int i = 0; i < 4; i++) assert(p[i].n64);
    /* A rejected/near anchor must not suppress visible 3D corners: libgpu clips them. */
    view.t[0] = 300; view.t[2] = 400;
    projected[0].flag = 1;
    assert(DrawWorldSprite(&object, 0, out, ordering, &sprite) != out);
    Corners(p);
    assert(p[0].z < 0 && p[2].z > PORT_FAR_NEAR);
    view.t[2] = -2000;
    assert(DrawWorldSprite(&object, 0, out, ordering, &sprite) == out);
    forceReject = 1; view.t[2] = 2000;
    assert(DrawWorldSprite(&object, 0, out, ordering, &sprite) == out);
    forceReject = 0; farView = NULL; object.flags = 0;
    /* Retain the original PS1 sprite corners, anchor sorting, near cutoff and lit omission. */
    position = (SVECTOR){0, 0, 4096};
    projected[0] = (ProjectedVertex){PackScreen(160, 120), 4096, 0};
    GteKeyNext(0, 123);
    projectedHandle[0] = GteRecordProjection((uint32_t)projected[0].screen, 160, 120, 4096, 1);
    GteRecordView(projectedHandle[0], 0, 0, 4096);
    assert(DrawSprite(DRAW_PLAIN, out, ordering, 0, &sprite, 0) != out);
    assert(packet.x0 == 140 && packet.y0 == 40 && packet.x3 == 180 && packet.y3 == 120);
    assert(packetSlot == ordering + 1024);
    GtePrecise affine;
    assert(GtePreciseOf(GteBoundHandle(&packet.x0, *(uint32_t *)&packet.x0), &affine));
    assert(affine.z == 0 && !affine.exact);
    assert(DrawSprite(DRAW_LIT, out, ordering, 0, &sprite, 0) != out);
    assert(((unsigned char *)&packet)[3] == 0);
    projected[0].depth = 2047;
    assert(DrawSprite(DRAW_PLAIN, out, ordering, 0, &sprite, 0) == out);
    puts("PC billboard dimensions, upright facing, per-corner projection, clipping inputs and keys; unchanged PS1 sprites: OK");
}
