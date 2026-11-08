/* Native PC hull gates from exact-verified POWERVR 435e30, not its AI gate. */
#include "../../game/src/portfar.c"
#include <assert.h>
#include <stdio.h>
static int renderer;
int PortNativeRenderer(void) { return renderer; }

ConfigData *gameData;
RaceLine raceLines[100];
TrackData *track;
Shadow *shadowData;
short numTrackSections=100, CurrentScreen;
int ownShip, remoteShip=1, ghostShip=2, raceType, shipTargetIndex[15];
static int hulls, shadowPackets;
u_long *OT[2];
void AddPrim(u_long *ot, void *p) { (void)ot; (void)p; shadowPackets++; }
int PlaneLineCollision(VECTOR p, VECTOR s, SVector n) { (void)p; (void)s; (void)n; return 0; }
void GteBindPrecise(const void *p, uint32_t h) { (void)p; (void)h; }
float GteFade(double d) { (void)d; return 1; }
uint64_t GteKeyMix(uint64_t a, uint64_t b) { return a^b; }
void GteKeyNext(int s, uint64_t k) { (void)s; (void)k; }
int32_t GteReadControl(int r) { return r==26?160:0; }
uint32_t GteRecordProjection(uint32_t s, float x, float y, float z, float f) { (void)s; (void)x; (void)y; (void)z; (void)f; return 1; }
void GteRecordView(uint32_t h, double x, double y, double z) { (void)h; (void)x; (void)y; (void)z; }

int PortOriginalTrack(void) { return -1; }
void SetSkeletonPosition(Skeleton *s, int x, int y, int z) { s->relative.t[0]=x; s->relative.t[1]=y; s->relative.t[2]=z; }
void SetSkeletonDirectionHPR(Skeleton *s, short h, short p, short r) { (void)s; (void)h; (void)p; (void)r; }
MATRIX *GetSkeletonMatrix(Skeleton *s) { return &s->relative; }
void PortInterpInstance(int i) { (void)i; }
void InitTransShip(Object *o, short f) { (void)o; (void)f; }
void PortDrawObjectFar(Object *o, const PortView *v, const CVECTOR *c) { (void)o; (void)v; (void)c; hulls++; }
static int DrawAt(int profile, int z)
{
    ShipData ships[15]={0};
    Skeleton skeleton={0}, cam={0};
    Object shape={.skeleton=&skeleton}, *shapes[]={&shape};
    Face faces[2]={{.flags=TRACK_BASE},{.flags=TRACK_BASE}};
    TrackSection section={0};
    VECTOR vertices[4]={{-100000,0,-100000,0},{100000,0,-100000,0},{-100000,0,100000,0},{100000,0,100000,0}};
    TrackData course={.faces=faces,.vertices=vertices};
    Shadow shadows[15]={0};
    u_long ordering[8192]={0};
    section.faceCount=1; section.prevSection.ptr=section.nextSection.ptr=&section; section.junction.index=-1;
    faces[0].normal.vy=4096;
    for(int i=0;i<4;i++) faces[0].vertex[i]=i;
    OT[0]=ordering; shadowData=shadows; shadowPackets=0;
    TrackCamera camera={.camPos=&cam,.section=&section};
    for(int i=0;i<3;i++) { skeleton.relative.m[i][i]=4096; cam.relative.m[i][i]=4096; }
    cam.super=&skeleton;
    for(int i=0;i<15;i++) { ships[i].nearTrkSect=&section; ships[i].attr=VIEW_IN; }
    ships[0].attr=LSIDE; ships[0].ppivot.vz=z;
    track=&course; renderer=profile; nativeScene=1; hulls=0; farShips=0;
    PortDrawNativeShips(&camera,ships,shapes,14);
    assert(farShips==(hulls?1:0));
    assert(shadowPackets==(hulls?16:0));
    return hulls;
}

int main(void)
{
    for (renderer=0; renderer<=4; renderer++) {
        nativeScene=0; assert(!PortNativeShipsEnabled());
        nativeScene=1; assert(PortNativeShipsEnabled()==(renderer>=2));
    }
    assert(PcNativeShipVisible(0,0,9599,9599,0,0,0,0,100));
    assert(!PcNativeShipVisible(0,0,9600,9600,0,0,0,0,100));
    assert(!PcNativeShipVisible(0,0,20000,20000,0,0,0,0,100));
    assert(!PcNativeShipVisible(0,0,100,0,0,0,0,0,100));
    assert(PcNativeShipVisible(0,0,-50000,-50000,1,0,0,0,100));
    assert(PcNativeShipVisible(0,0,100,100,0,0,0,39,100));
    assert(!PcNativeShipVisible(0,0,100,100,0,0,0,40,100));
    assert(PcNativeShipVisible(0,0,100,100,0,0,0,-39,100));
    assert(!PcNativeShipVisible(0,0,100,100,0,0,0,-40,100));
    assert(PcNativeShipVisible(0,0,100,100,0,0,0,99,100));
    assert(PcNativeShipVisible(0,0,100,100,0,0,0,-99,100));
    assert(!PcNativeShipVisible(0,0,100,100,0,1,10,0,100));
    assert(PcNativeShipVisible(0,0,100,100,0,1,11,0,100));
    /* float PC magnitude avoids the PS1 squared-sum/root quantization. */
    assert(PcNativeShipVisible(6784,0,6784,6784,0,0,0,0,100));
    assert(!PcNativeShipVisible(6792,0,6792,6792,0,0,0,0,100));
    assert(DrawAt(RENDER_PS1,100)==0);
    assert(DrawAt(RENDER_PC,9599)==1);
    assert(DrawAt(RENDER_PC,9600)==0);
    PortNativeShipsReset();
    assert(DrawAt(RENDER_N64,100)==1);
    assert(DrawAt(RENDER_N64,20000)==1); /* original remembers visible ships */
    assert(DrawAt(RENDER_N64,-24000)==0); /* rejection clears remembered */
    assert(DrawAt(RENDER_N64,20000)==0);
    assert(DrawAt(RENDER_N64,100)==1); /* reacquired */
    assert(DrawAt(RENDER_SATURN,160)==0);
    assert(DrawAt(RENDER_SATURN,161)==1);
    assert(DrawAt(RENDER_SATURN,31999)==1);
    assert(DrawAt(RENDER_SATURN,32000)==0);
    puts("Native ship dispatch: PC, N64 and Saturn hull/shadow boundaries, history, and PS1 bypass: OK");
}
