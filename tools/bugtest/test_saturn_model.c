/* The source record's draw-mode word must not be interpreted as a VRAM address. */
#include "../../platform/saturn_models.c"
#include <assert.h>
static void Word(uint8_t *p,unsigned n) { p[0]=(uint8_t)(n>>8);p[1]=(uint8_t)n; }
static void Long(uint8_t *p,unsigned n) { Word(p,n>>16);Word(p+2,n); }

static uint8_t video[512];
static unsigned levels;
static void Pixel(void *user,int x,int y,uint16_t rgb,int blend,float depth,uint8_t alpha)
{
    (void)user;(void)x;(void)y;(void)depth;
    assert(blend==-1 && alpha==255);
    assert(rgb==0x8421 || rgb==0x8842);
    levels |= 1u << (rgb&31);
}
unsigned char *SaturnVideoAt(SaturnVideo *v, unsigned address, size_t size)
{
    (void)v;
    return address >= VDP1_RAM && address - VDP1_RAM + size <= sizeof(video)
        ? video + (address - VDP1_RAM) : NULL;
}

static void Billboard(void)
{
    int vertices[4][3] = {{0,0,0},{64,0,0},{64,64,0},{0,64,0}}, origin[3] = {0};
    Poly poly = {.flags=TEXTURED, .corner={0,1,2,3}, .size=0x0108, .colr=8, .mode=0x1488};
    Model model = {.vertex=vertices, .vertexCount=4, .poly=&poly, .polyCount=1, .billboard=1};
    Converter cv;
    StartConverter(&cv, R_SCENE, 1);
    memset(video,0x12,32);
    for(int i=0;i<16;i++) Word(video+64+i*2,0x8000);
    Word(video+66,0x8421);Word(video+68,0x8842); /* distinct adjacent dark levels */
    AddBillboard(&cv,"pole",&model,NULL,origin);
    AddBillboard(&cv,"pole2",&model,NULL,origin); /* shared atlas, independent owned pixels */
    assert(cv.atlas.count==1);
    for(int i=0;i<2;i++) {
        SaturnNativePoly *p=&cv.object[i].spec[0].native;
        assert(p->pixels && p->palette[1]==0x8421 && p->palette[2]==0x8842);
        assert(p->mode==0x1488 && p->corners[0]==0 && p->corners[1]==1 &&
               p->corners[2]==3 && p->corners[3]==2);
        assert(p->pixels!=cv.atlas.image[0].px);
    }
    Buffer tims={0};size_t size, *timSizes=NULL;int timCount;
    uint8_t *prm=BuildPrm(&cv,&size,&tims,&timSizes,&timCount,0);
    /* The synthetic PRM's scratch word carries the registry tag through the
       existing object packet hook, including sprites. */
    uint32_t tag=Be32(prm+40), packet[10]={9u<<24};
    SaturnNativeBind(packet,packet+10,tag,0);
    const SaturnNativePoly *bound=SaturnNativeLookup(packet+1);
    assert(bound && bound->palette[1]==0x8421 && bound->palette[2]==0x8842);
    FreeConverter(&cv);
    assert(bound->pixels && memchr(bound->pixels,1,(size_t)bound->width*bound->height)); /* registry owns a copy */
    GpuVertex quad[4]={{.x=0,.y=0,.a=255},{.x=63,.y=0,.a=255},
                       {.x=0,.y=63,.a=255},{.x=63,.y=63,.a=255}};
    SaturnVdp1Draw(bound,quad,0,0,64,64,Pixel,NULL,NULL);
    assert(levels==6); /* both adjacent levels survive rasterization, without the fitted tint */
    SDL_free(prm);SDL_free(tims.data);SDL_free(timSizes);
    SaturnNativeShutdown();

    /* A multi-material composite must not silently use the first part's LUT. */
    StartConverter(&cv,R_SCENE,1);
    Poly parts[2]={poly,poly};parts[1].colr=12;
    model.poly=parts;model.polyCount=2;
    Word(video+12*8+2,0x801f);
    AddBillboard(&cv,"mixed",&model,NULL,origin);
    assert(!cv.object[0].spec[0].native.pixels);
    FreeConverter(&cv);
    puts("Saturn billboard palettes retain adjacent dark levels, native bindings and deduplicated ownership: OK");
}
int main(void)
{
    uint8_t block[128]={0}, vertices[48]={0};
    Long(block,64);Word(block+16,4);
    Word(block+64,1);Word(block+66,TEXTURED|8);Word(block+68,26);
    Word(block+72,2);Word(block+74,1);
    for(int i=0;i<4;i++) Word(block+76+2*i,i*12);
    Word(block+84,0x2cb8);Word(block+86,0x0418);
    Word(block+88,0x14cc);Word(block+90,0x1ff4);
    Word(block+98,0xffff);
    Model model;int origin[3]={0};
    assert(ReadModel(block,sizeof(block),vertices,sizeof(vertices),0,origin,&model));
    assert(model.polyCount==1 && model.poly[0].mode==0x14cc);
    assert(model.poly[0].srca==0x2cb8 && model.poly[0].size==0x0418 && model.poly[0].colr==0x1ff4);
    assert(model.poly[0].corner[3]==3 && model.poly[0].flags==(TEXTURED|8));
    FreeModel(&model);
    puts("Saturn model records preserve CMDPMOD, source texture and palette addresses: OK");
    Billboard();
}
