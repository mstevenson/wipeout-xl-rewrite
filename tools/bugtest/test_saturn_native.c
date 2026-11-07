/* VDP1 material/raster tests, independent of SDL, OpenGL and copyrighted data. */
#include "render/saturn_native.h"
#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <string.h>

static unsigned hits[32][32], writes;
static uint16_t colour[32][32];
static int blendAt[32][32];
static void Pixel(void *user, int x, int y, uint16_t rgb, int blend, float z, uint8_t alpha)
{
    (void)user;
    assert(x >= 0 && x < 32 && y >= 0 && y < 32);
    assert(isfinite(z) && alpha == 255);
    hits[y][x]++; writes++; colour[y][x] = rgb; blendAt[y][x] = blend;
}
static void Clear(void) { memset(hits, 0, sizeof(hits)); writes = 0; }
static GpuVertex Vertex(float x, float y) { return (GpuVertex){.x=x, .y=y, .w=100, .a=255}; }
static void Raster(void)
{
    SaturnNativePoly p = {.colour=0xffff, .mode=0xc0, .corners={0,1,2,3}};
    GpuVertex q[4] = {Vertex(4,0),Vertex(12,8),Vertex(8,12),Vertex(0,4)};
    SaturnVdp1Draw(&p,q,0,0,32,32,Pixel,NULL,NULL);
    for (int y=1;y<12;y++) for (int x=1;x<12;x++) {
        if (x+y>4 && x+y<20 && x-y<4 && y-x<4) assert(hits[y][x]);
    }
    /* Texture magnification distributes each source texel evenly, including the last. */
    uint8_t texture[8] = {0,1,2,3,4,5,6,7};
    p.textured=1; p.width=8; p.height=1; p.pixels=texture;
    for (int i=0;i<16;i++) p.palette[i]=(uint16_t)(0x8000+i);
    q[0]=Vertex(0,0);q[1]=Vertex(15,0);q[2]=Vertex(15,3);q[3]=Vertex(0,3);
    Clear(); SaturnVdp1Draw(&p,q,0,0,32,32,Pixel,NULL,NULL);
    assert(writes==64);
    for (int x=0;x<16;x++) assert(colour[1][x]==0x8000+x/2);
    /* Clipping must not stretch the surviving texture. */
    Clear(); SaturnVdp1Draw(&p,q,5,1,11,3,Pixel,NULL,NULL);
    assert(writes==12 && colour[1][5]==0x8002 && colour[2][10]==0x8005);
    p.mode=0x80;
    Clear(); SaturnVdp1Draw(&p,q,0,0,32,32,Pixel,NULL,NULL);
    assert(!hits[0][0] && !hits[0][1] && hits[0][2]);
    p.mode=0xc0|0x100;
    Clear(); SaturnVdp1Draw(&p,q,0,0,32,32,Pixel,NULL,NULL);
    assert(writes==32);
    for(int y=0;y<4;y++) for(int x=0;x<16;x++) assert(!!hits[y][x]==!((x+y)&1));
    /* A-D collapsed: still all four source corners, not an atlas-resampled triangle. */
    p.mode=0xc0;p.corners[3]=0;
    q[0]=Vertex(0,0);q[1]=Vertex(7,0);q[2]=Vertex(7,7);
    Clear(); SaturnVdp1Draw(&p,q,0,0,32,32,Pixel,NULL,NULL);
    assert(hits[0][0]>1 && hits[0][7] && hits[7][7] && !hits[7][0]);
    /* All four corners collapsed must write exactly one pixel. */
    p.corners[1]=p.corners[2]=0;
    Clear(); SaturnVdp1Draw(&p,q,0,0,32,32,Pixel,NULL,NULL);
    assert(writes==1);
    /* Two distinct end codes terminate the source row, even if clipped away. */
    p.corners[1]=1;p.corners[2]=2;p.corners[3]=3;
    q[0]=Vertex(0,0);q[1]=Vertex(7,0);q[2]=Vertex(7,0);q[3]=Vertex(0,0);
    texture[0]=15;texture[4]=15;p.mode=0;
    Clear(); SaturnVdp1Draw(&p,q,3,0,8,1,Pixel,NULL,NULL);
    assert(writes==1 && hits[0][3]);
    p.mode=0x80;
    Clear(); SaturnVdp1Draw(&p,q,0,0,32,32,Pixel,NULL,NULL);
    assert(writes==8 && colour[0][4]==0x800f);
    /* Mode 2 halves the source; mode 1 requests destination shadow. */
    p.textured=0;p.colour=0xffff;p.mode=2;
    Clear(); SaturnVdp1Draw(&p,q,0,0,32,32,Pixel,NULL,NULL);
    assert(colour[0][0]==0xbdef && blendAt[0][0]==-1);
    p.mode=1;Clear();SaturnVdp1Draw(&p,q,0,0,32,32,Pixel,NULL,NULL);
    assert(blendAt[0][0]==-2);
    q[0].x=NAN;Clear();SaturnVdp1Draw(&p,q,0,0,32,32,Pixel,NULL,NULL);assert(!writes);
}
static void Lighting(void)
{
    float neutral[3]={16,16,16}, shade[3]={0,31,16};
    assert(SaturnGouraud(0x9234,neutral)==0x9234);
    uint16_t rgb=SaturnGouraud(0x8000|10|(24<<5)|(9<<10),shade);
    assert((rgb&31)==0 && ((rgb>>5)&31)==31 && ((rgb>>10)&31)==9);
    uint16_t table[4];
    SaturnShipLight(0x8000|2|(18<<5)|(31<<10),table);
    assert(table[0]==0xc210 && table[3]==0xb18c);
    assert((table[2]&31)==0 && ((table[2]>>5)&31)==14 && ((table[2]>>10)&31)==27);
    assert(SaturnTrackLight(128,128,128)==0xc210);
}
static void NativeAttributes(void)
{
    /* Track palettes already contain lighting: do not apply their road-light
       table again. A dark prelit pixel must retain its detail. */
    SaturnNativePoly p={.corners={0,1,2,3},.mode=0x14c8,.colour=0x8842,
        .gouraud={0x8842,0x8842,0x8842,0x8842}};
    GpuVertex v[4]={Vertex(0,0),Vertex(7,0),Vertex(7,7),Vertex(0,7)};
    Clear();SaturnVdp1Draw(&p,v,0,0,32,32,Pixel,NULL,NULL);
    assert(writes==64 && colour[3][3]==0x8842);
    /* Huge clipped vertices stay bounded by the viewport and retain native colour. */
    p.mode=0xc4;p.clipped=p.vertexAttributes=1;p.colour=0x800a;
    v[0]=Vertex(-100000,0);v[1]=Vertex(100000,0);v[2]=Vertex(0,100000);
    for(int i=0;i<3;i++) for(int c=0;c<3;c++) v[i].saturnShade[c]=c?16:21;
    Clear();SaturnVdp1Draw(&p,v,0,0,32,32,Pixel,NULL,NULL);
    assert(writes==1024);
    for(int y=0;y<32;y++) for(int x=0;x<32;x++) assert(colour[y][x]==0x800f);
    SaturnNativeShutdown();
}

static void Ownership(void)
{
    uint8_t pixels[]={1,2,3,4};
    SaturnNativePoly p={.pixels=pixels,.width=2,.height=2,.mode=0x14c8};
    uint32_t packet[5]={4u<<24}, other[5]={4u<<24};
    uint32_t first=SaturnNativeModel(&p,1,NULL,0);
    SaturnNativeBind(packet,packet+5,first,0);
    const SaturnNativePoly *found=SaturnNativeLookup(packet+1);
    assert(found && found->mode==0x14c8 && found->pixels[2]==3);
    pixels[2]=9;assert(found->pixels[2]==3); /* converter memory is independent */
    p.mode=0x14cc;
    uint32_t second=SaturnNativeModel(&p,1,NULL,0);
    SaturnNativeBind(other,other+5,second,0);
    const SaturnNativePoly *otherModel=SaturnNativeLookup(other+1);
    assert(otherModel && otherModel->mode==0x14cc && otherModel->pixels[2]==9);
    assert(SaturnNativeLookup(packet+1)==found);
    SaturnNativeForget(packet);assert(!SaturnNativeLookup(packet+1));
    assert(SaturnNativeLookup(other+1)==otherModel);
    SaturnNativeTrack(NULL,0,NULL);assert(!SaturnNativeLookup(other+1));
    SaturnNativeBind(packet,packet+5,0,0);assert(!SaturnNativeLookup(packet+1));
    SaturnNativeBind(packet,packet+5,first,0);assert(SaturnNativeLookup(packet+1)==found);
    uint16_t palette[256]={0};palette[1]=0x801f;
    SaturnNativeSky(pixels,palette,2,2);
    const uint16_t *saved;int w,h;
    const uint8_t *sky=SaturnNativeSkyData(&saved,&w,&h);
    assert(w==2 && h==2 && sky[2]==9 && saved[1]==0x801f);
    pixels[2]=0;assert(sky[2]==9);
    SaturnNativeShutdown();
    assert(!SaturnNativeLookup(other+1) && !SaturnNativeSkyData(&saved,&w,&h) && !w && !h);
}

static void ObjectFrames(void)
{
    uint8_t pixels[] = {1, 2, 3, 4};
    SaturnNativePoly base[] = {{.corners={0, 2, 3, 1}, .colour=0x8001}, {.colour=0x8002}};
    SaturnNativePoly frame = {.pixels=pixels, .width=2, .height=2, .textured=1, .mode=0x14c8};
    frame.palette[1] = 0x83e0;
    uint32_t tag = SaturnNativeModel(base, 2, &frame, 1);
    uint32_t animated = SaturnNativeFrameTag(tag, 1);
    assert(animated != tag && SaturnNativeFrameTag(animated, 0) == tag);
    assert(SaturnNativeFrameTag(tag, 2) == tag && SaturnNativeFrameTag(0, 1) == 0);

    uint32_t on[5] = {4u << 24}, off[5] = {4u << 24}, housing[5] = {4u << 24};
    SaturnNativeBind(on, on + 5, animated, 0);
    SaturnNativeBind(off, off + 5, tag, 0);
    SaturnNativeBind(housing, housing + 5, animated, 1);
    pixels[1] = 0;
    const SaturnNativePoly *selected = SaturnNativeLookup(on + 1);
    assert(selected && selected->pixels[1] == 2 && selected->palette[1] == 0x83e0);
    assert(selected->corners[1] == 2 && selected->corners[3] == 1);
    assert(SaturnNativeLookup(off + 1)->colour == 0x8001);
    assert(SaturnNativeLookup(housing + 1)->colour == 0x8002);
    SaturnNativeShutdown();
}

static void RaceMaterials(void)
{
    SaturnNativePoly base = {0};
    for (int i = 0; i < 16; i++) base.palette[i] = (uint16_t)(0x8000 + i);
    base.palette[1] = 0xffff;
    SaturnNativeTrack(&base, 1, NULL);
    PortRaceMaterialTick();

    SaturnNativePoly draw = base;
    draw.animation = SATURN_ANIM_WATERFALL;
    SaturnNativeAnimatePalette(&draw);
    assert(draw.palette[3] == 0x8003 && draw.palette[4] == 0x800f && draw.palette[5] == 0x8004);

    draw = base;
    draw.animation = SATURN_ANIM_RESCUE_RED;
    SaturnNativeAnimatePalette(&draw);
    assert(draw.palette[1] == 0xffff && draw.palette[14] == 0x8018 && draw.palette[13] == 0x8016);
    draw = base;
    draw.animation = SATURN_ANIM_RESCUE_RGB;
    SaturnNativeAnimatePalette(&draw);
    assert(draw.palette[10] == 0x800a && draw.palette[15] == (0x8000 | 23 | 23 << 5 | 16 << 10));

    SaturnNativePoly smoke = {.animation = SATURN_ANIM_SMOKE_SLOW, .colour = 1};
    SaturnNativePoly frames[] = {{.colour = 2}, {.colour = 3}};
    uint32_t slow = SaturnNativeModel(&smoke, 1, frames, 2);
    smoke.animation = SATURN_ANIM_SMOKE_FAST;
    uint32_t fast = SaturnNativeModel(&smoke, 1, frames, 2);
    uint32_t first[5] = {4u << 24}, second[5] = {4u << 24}, third[5] = {4u << 24};
    SaturnNativeBind(first, first + 5, slow, 0);
    PortRaceMaterialTick();
    SaturnNativeBind(second, second + 5, slow, 0);
    SaturnNativeBind(third, third + 5, fast, 0);
    assert(SaturnNativeLookup(first + 1)->colour == 1);
    assert(SaturnNativeLookup(second + 1)->colour == 2);
    assert(SaturnNativeLookup(third + 1)->colour == 3);

    draw = base;
    draw.animation = SATURN_ANIM_NEON;
    SaturnNativeAnimatePalette(&draw);
    assert(draw.palette[1] == 0xffff);
    for (int i = 0; i < 4; i++) PortRaceMaterialTick();
    SaturnNativeAnimatePalette(&draw);
    assert(draw.palette[1] == 0xdef7 && draw.palette[2] == 0x8000);
    SaturnNativeTrack(&base, 1, NULL);
    draw = base;
    draw.animation = SATURN_ANIM_NEON;
    SaturnNativeAnimatePalette(&draw);
    assert(draw.palette[1] == 0xffff);
    SaturnNativeShutdown();
}

int main(void)
{
    Raster();Lighting();NativeAttributes();Ownership();ObjectFrames();RaceMaterials();
    puts("Saturn: quad coverage, clipping, RGB555 light, draw modes, animated materials and ownership: OK");
}
