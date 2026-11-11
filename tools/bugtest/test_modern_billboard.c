#define main NearObjectProbeMain
#include "test_near_object.c"
#undef main
static Texture spriteTexture={.textureW=16,.textureH=128,.tsb=17,.cba=42,
                              .u0=16,.u1=79,.u2=16,.u3=79,.v0=32,.v1=32,.v2=159,.v3=159};
Texture *textureTable[]={&spriteTexture};
static int pcSprite;
int PortPcData(void) { return pcSprite; }
static void Check(const short *xy,float x,float y)
{
    GtePrecise p;
    assert(GtePreciseOf(GteBoundHandle(xy,*(const uint32_t *)xy),&p));
    assert(p.view[0]==x && p.view[1]==y && p.view[2]==4096 && p.z>0 && p.exact);
}
int main(void)
{
    GteWriteControl(0,4096); GteWriteControl(2,4096); GteWriteControl(4,4096);
    GteWriteControl(26,160);
    SVECTOR position={0,100,4096}; Object object={.vertices=&position,.vertexCount=1};
    SPR sprite={.type=TypeBSPR,.width=100,.height=200,.colour={128,128,128,0}};
    POLY_FT4 poly={0}; currentDrawing=123;
    assert(DrawWorldSprite(&object,0,(char *)&poly,ordering,&sprite)==(char *)(&poly+1));
    Check(&poly.x0,-50,-100); Check(&poly.x3,50,100);
    assert(poly.u0==16 && poly.u1==79 && poly.v0==32 && poly.v2==159);
    sprite.type=TypeTSPR;
    DrawWorldSprite(&object,0,(char *)&poly,ordering,&sprite);
    Check(&poly.x0,-50,100); Check(&poly.x3,50,300);
    pcSprite=1;
    DrawWorldSprite(&object,0,(char *)&poly,ordering,&sprite);
    Check(&poly.x0,25,0); Check(&poly.x3,-25,100);
    puts("Modern billboards retain PS1 top/bottom anchors, dimensions, UVs and PC units: OK");
}
