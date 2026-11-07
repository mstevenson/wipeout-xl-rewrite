/* Production Saturn course conversion with synthetic, already-lit face palettes. */
#include "../../platform/saturn.c"
#include <assert.h>

static uint16_t drawn;
static int Sample(void *user, float u, float v) { (void)user;(void)u;(void)v;return 1; }
static void Pixel(void *user,int x,int y,uint16_t rgb,int blend,float depth,uint8_t alpha)
{ (void)user;(void)x;(void)y;(void)blend;(void)depth;(void)alpha;drawn=rgb; }

static void WeaponPads(void)
{
    uint8_t sections[SECTIONS_AT + SECTION] = {0}, faces[3 * FACE] = {0};
    uint8_t vri[8 + (SATURN_WEAPON_PAD_PALETTES + 1) * 32] = {0};
    Course c = {.sections = sections, .sectionCount = 1, .faces = faces, .facesSize = sizeof(faces),
        .vri = vri, .vriSize = sizeof(vri), .textures = 0x1000, .palettes = 0x1000,
        .paletteCount = SATURN_WEAPON_PAD_PALETTES + 1};
    PutBe16(sections + SECTIONS_AT + 158, 3);
    for (int i = 0; i < c.paletteCount; i++) PutBe16(vri + 8 + i * 32 + 2, 0x8000 + i);
    PutBe16(faces, SATURN_WEAPON_PAD_PALETTES);
    PutBe16(faces + FACE, SATURN_WEAPON_PAD_PALETTES);
    PutBe16(faces + 2 * FACE, SATURN_WEAPON_PAD_PALETTES);
    MakeNativeTrack(&c);

    uint32_t road[5] = {4u << 24}, left[5] = {4u << 24}, right[5] = {4u << 24};
    GpuVertex quad[4] = {{.x=0,.y=0,.a=255}, {.x=3,.y=0,.a=255},
                         {.x=0,.y=3,.a=255}, {.x=3,.y=3,.a=255}};
    SaturnNativeBindTrack(road, road + 5, 0);
    SaturnNativeBindTrack(left, left + 5, 1);
    SaturnNativeBindTrack(right, right + 5, 2);
    const SaturnNativePoly *leftPad = SaturnNativeLookup(left + 1);
    const SaturnNativePoly *rightPad = SaturnNativeLookup(right + 1);
    assert(leftPad && rightPad && leftPad->palette[1] == 0x8041 && rightPad->palette[1] == 0x8041);
    PortWeaponGridVisual(1, -1, 160, 0);
    assert(leftPad->palette[1] == 0x8002 && rightPad->palette[1] == 0x8041);
    PortWeaponGridVisual(1, 2, 192, 1);
    assert(rightPad->palette[1] == 0x8040);
    SaturnVdp1Draw(leftPad, quad, 0, 0, 4, 4, Pixel, Sample, NULL);
    assert(drawn == 0x8040);
    PortWeaponGridVisual(-1, 2, 224, 0);
    assert(leftPad->palette[1] == 0x8040);
    SaturnVdp1Draw(rightPad, quad, 0, 0, 4, 4, Pixel, Sample, NULL);
    assert(drawn == 0x8003);
    PortWeaponGridVisual(1, 2, -96, 0);
    assert(leftPad->palette[1] == 0x803e && rightPad->palette[1] == 0x803e);
    PortWeaponGridVisual(1, 2, -32, 0);
    assert(leftPad->palette[1] == 0x803f && rightPad->palette[1] == 0x803f);
    assert(SaturnNativeLookup(road + 1)->palette[1] == 0x8041);

    MakeNativeTrack(&c);
    SaturnNativeBindTrack(left, left + 5, 1);
    assert(SaturnNativeLookup(left + 1)->palette[1] == 0x8041);
    puts("Saturn weapon pads cycle, deplete, recharge, and reload independently of the road: OK");
}

int main(void)
{
    uint8_t sections[SECTIONS_AT+SECTION]={0}, faces[FACE]={0}, views[48]={0}, vri[48]={0};
    Course c={.sections=sections,.sectionCount=1,.faces=faces,.facesSize=sizeof(faces),
        .views=views,.viewsSize=sizeof(views),.vri=vri,.vriSize=sizeof(vri),
        .textures=0x1000,.palettes=0x1000,.gouraud=0x1020};
    uint8_t *section=sections+SECTIONS_AT;
    PutBe16(section+158,1);
    for(int i=0;i<3;i++) PutBe32(section+i*4,0xffffffffu);
    PutBe16(vri+8+2,0x8842); /* dark but visible texel */
    for(int i=0;i<4;i++) PutBe16(vri+40+i*2,0x8842); /* dark ship road-light input */
    for(int opaque=0;opaque<2;opaque++) {
        Buffer parts[5]={0};
        PutBe16(faces+4,opaque ? SPD : 0);
        MakeNativeTrack(&c);
        MakeGeometry(&c,&parts[0],&parts[1],&parts[2],&parts[3],&parts[4]);
        uint32_t packet[5]={4u<<24};
        SaturnNativeBindTrack(packet,packet+5,0);
        const SaturnNativePoly *p=SaturnNativeLookup(packet+1);
        assert(p && p->mode==(0x1488 | (opaque ? 0x40 : 0)));
        assert(p->gouraud[0]==0x8842 && p->palette[1]==0x8842);
        GpuVertex v[4]={{.x=0,.y=0,.a=255},{.x=3,.y=0,.a=255},
                        {.x=0,.y=3,.a=255},{.x=3,.y=3,.a=255}};
        drawn=0;SaturnVdp1Draw(p,v,0,0,4,4,Pixel,Sample,NULL);
        assert(drawn==0x8842); /* applying the road shade again would turn this black */
        for(int i=0;i<5;i++) SDL_free(parts[i].data);
    }
    puts("Saturn course loader: prelit palettes retain dark detail, original draw mode and SPD: OK");
    WeaponPads();
    SaturnNativeShutdown();
}
