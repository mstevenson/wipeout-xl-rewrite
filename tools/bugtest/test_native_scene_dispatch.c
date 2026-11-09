/* Execute the actual Native track/scenery dispatch with packet sinks. */
#include "../../game/src/portfar.c"
#include <assert.h>
#include <stdio.h>

TrackData *track;
ConfigData *gameData;
Object **trackObjects;
static int authored;
const unsigned short *PortSaturnNativeSceneObjects(int course, int section, int *count)
{
    static const unsigned short list[] = {0x1001, 0x2000};
    (void)course;
    if (!authored || section != 0) return NULL;
    *count = 2;
    return list;
}
ShipData *shipIndex;
int ownShip;
short CurrentScreen, loTileCount, medTileCount, hiTileCount;
u_short clipLevel=1;
u_long *OT[2];
CameraUpdateFunc CameraUpdate;
static int renderer, cap=2, hulls, trackDetail, fullView, tracing;
static Object *firstHull;
static unsigned char drawn[120], nearDetail[120], arenaDetail[120], arenaDraws[120];
int PortNativeRenderer(void) { return renderer; }
int PortFarViewEnabled(void) { return fullView; }
int PortTracing(void) { return tracing; }
int PortScreenMargin(void) { return 0; }
int32_t GteReadControl(int reg) { assert(reg==26); return 160; }
float GteFade(double distance) { (void)distance; return 1; }
RenderGeometry PortRenderGeometry(void) { return (RenderGeometry){.trackDetail = trackDetail}; }
int PortPcNativeView(int *sections, int *scenery) { *sections=cap; *scenery=32767; return renderer==RENDER_PC; }
int PortOriginalTrack(void) { return -1; }
int PortOriginalN64Course(void) { return 0; }
int PortOriginalN64SectionNumber(int i) { (void)i; return 0; }
void UpdateCameraRaceStart(TrackCamera *c, ShipData *s, RescueData *r) { (void)c; (void)s; (void)r; }
char TrackDirection(TrackCamera *c) { (void)c; return 1; }
void PsxLinkRegion(void *p, size_t n) { (void)p; (void)n; }
u_long *ClearOTagR(u_long *p, int n) { (void)n; return p; }
void AddPrims(u_long *ot, void *a, void *b) { (void)ot; (void)a; (void)b; }
void PortTrackPackets(const void *a, const void *b) { (void)a; (void)b; }
void PortTrackFar(TrackSection *s, const PortView *v) { (void)v; drawn[s-track->sections]=1; }
void PortTrackDetailed(TrackSection *s, const PortView *v, int detail)
{
    (void)v;
    arenaDetail[s-track->sections]=detail;
    arenaDraws[s-track->sections]++;
    assert(PortFarAlloc(sizeof(POLY_FT4)));
}
void AsmTrackHi(TrackSection *s, Skeleton *c, AutoMeshData *a, short n) { (void)c; (void)a; (void)n; nearDetail[s-track->sections]=3; }
void AsmTrackMed(TrackSection *s, Skeleton *c, short n) { (void)c; (void)n; nearDetail[s-track->sections]=2; }
void AsmTrackLo(TrackSection *s, Skeleton *c, short n) { (void)c; (void)n; nearDetail[s-track->sections]=1; }
void PortDrawObjectFar(Object *o, const PortView *v, const CVECTOR *c)
{
    (void)v; (void)c;
    if (!hulls) firstHull = o;
    hulls++;
}

int main(void)
{
    TrackSection sections[120]={0};
    TrackData course={.sections=sections,.sectionCount=120};
    Skeleton rotation={0}, cameraPosition={.super=&rotation};
    TrackCamera camera={.camPos=&cameraPosition,.section=sections};
    ShipData ships[15]={0};
    u_long ordering[8192];
    short near[]={0,1}, middle[]={3,1}, distant[]={5,5}, all[]={0,120};
    track=&course; shipIndex=ships; OT[0]=ordering;
    for(int i=0;i<3;i++) rotation.relative.m[i][i]=cameraPosition.relative.m[i][i]=4096;
    for(int i=0;i<120;i++) {
        sections[i].nextSection.ptr=&sections[(i+7)%120];
        sections[i].prevSection.ptr=&sections[(i+113)%120];
        sections[i].junction.index=-1;
        sections[i].centre.vz=i*100;
    }
    sections[0].northViewSection[0]=near; sections[0].northViewCount[0]=2;
    sections[0].northViewSection[1]=middle; sections[0].northViewCount[1]=2;
    sections[0].northViewSection[2]=distant; sections[0].northViewCount[2]=2;
    sections[0].allViewSection[2]=all; sections[0].allViewCount[2]=2;
    for(renderer=0;renderer<=1;renderer++) {
        nativeShipState[0].remembered=0;
        assert(!PortDrawTrack(&camera,NULL,0));
        assert(nativeShipState[0].remembered==1);
        assert(!nativeScene);
        Object object={.skeleton=&rotation};
        assert(!PortDrawNativeScenery(&object));
    }
    renderer=RENDER_PC;
    assert(PortDrawTrack(&camera,NULL,0));
    for(int i=0;i<120;i++) assert(drawn[i]==(i==0||i==3||i==5||i==12));
    memset(drawn,0,sizeof(drawn));
    assert(PortDrawTrack(&camera,NULL,1));
    for(int i=0;i<120;i++) assert(drawn[i]);
    Face roadFace={0};
    VECTOR roadVertex={.vz=4096};
    course.faces=&roadFace;
    course.vertices=&roadVertex;
    sections[0].faceCount=sections[3].faceCount=sections[5].faceCount=sections[7].faceCount=1;
    renderer=RENDER_MODERN; memset(drawn,0,sizeof(drawn));
    assert(PortDrawTrack(&camera,NULL,0));
    assert(nearDetail[0]==3 && nearDetail[3]==2 && nearDetail[5]==1 && !nearDetail[7]);
    for(int i=0;i<120;i++) assert(drawn[i]==(i==0||i==3||i==5||i==12||i==19||i==26||i==33));
    hiTileCount=0x1c0;
    memset(nearDetail,0,sizeof(nearDetail));
    memset(drawn,0,sizeof(drawn));
    assert(PortDrawTrack(&camera,NULL,0));
    assert(!nearDetail[0] && drawn[0] && nearDetail[3]==2);
    hiTileCount=0;
    for (trackDetail=RENDER_TRACK_MEDIUM; trackDetail<=RENDER_TRACK_HIGH; trackDetail++) {
        memset(drawn,0,sizeof(drawn));
        memset(nearDetail,0,sizeof(nearDetail));
        memset(arenaDetail,0,sizeof(arenaDetail));
        assert(PortDrawTrack(&camera,NULL,0));
        for (int i=0;i<120;i++) {
            int visible = i==0 || i==3 || i==5 || i==12 || i==19 || i==26 || i==33;
            int detail = i==0 || i==3 || trackDetail==RENDER_TRACK_HIGH ? 3 : 2;
            assert(arenaDetail[i]==(visible ? detail : 0) && !drawn[i] && !nearDetail[i]);
        }
        if (trackDetail==RENDER_TRACK_HIGH) {
            assert(PortDrawTrack(&camera,NULL,1));
            for (int i=0;i<120;i++) assert(arenaDetail[i]==3);
        }
    }
    renderer=RENDER_AUTO;
    fullView=1;
    sections[119].centre.vx=100000;
    for (trackDetail=RENDER_TRACK_MEDIUM; trackDetail<=RENDER_TRACK_HIGH; trackDetail++) {
        memset(arenaDetail,0,sizeof(arenaDetail));
        memset(arenaDraws,0,sizeof(arenaDraws));
        assert(PortDrawTrack(&camera,NULL,0) && !nativeScene);
        for (int i=0;i<120;i++) {
            int authored = i==0 || i==3 || (i>=5 && i<=9);
            int detail = i==0 || i==3 || trackDetail==RENDER_TRACK_HIGH ? 3 : 2;
            assert(arenaDetail[i]==(authored ? detail : 0));
        }
        DrawFarTrack(&camera);
        for (int i=0;i<119;i++) {
            int detail = i==0 || i==3 || trackDetail==RENDER_TRACK_HIGH ? 3 : 2;
            assert(arenaDetail[i]==detail && arenaDraws[i]==1);
        }
        assert(!arenaDraws[119] && arenaUsed==119*sizeof(POLY_FT4));
    }
    trackDetail=RENDER_TRACK_HIGH;
    tracing=1;
    assert(!PortDrawTrack(&camera,NULL,0) && !fullRoadDetail);
    tracing=0;
    trackDetail=RENDER_TRACK_NATIVE;
    assert(!PortDrawTrack(&camera,NULL,0) && !fullRoadDetail);
    memset(drawn,0,sizeof(drawn));
    DrawFarTrack(&camera);
    for (int i=0;i<120;i++) assert(drawn[i]==(i<119));
    fullView=0;
    sections[119].centre.vx=0;
    sections[0].faceCount=sections[3].faceCount=sections[5].faceCount=sections[7].faceCount=0;
    renderer=RENDER_SATURN; memset(drawn,0,sizeof(drawn));
    assert(PortDrawTrack(&camera,NULL,0));
    for(int i=0;i<120;i++) assert(drawn[i]==(i==0||i==3||(i>=5&&i<=9)));
    renderer=RENDER_N64; memset(drawn,0,sizeof(drawn));
    assert(PortDrawTrack(&camera,NULL,0));
    int count=0; for(int i=0;i<120;i++) count+=drawn[i];
    assert(count==21 && drawn[113]);
    for(int i=0;i<20;i++) assert(drawn[(i*7)%120]);

    Skeleton objectPosition={.super=&rotation};
    Object object={.skeleton=&objectPosition,.extent=0};
    for(int i=0;i<3;i++) objectPosition.relative.m[i][i]=4096;
    renderer=RENDER_MODERN; hulls=0;
    objectPosition.relative.t[2]=2048;
    assert(PortDrawNativeScenery(&object) && hulls==1);
    assert(!PortDrawNativeLandscape(&camera));
    renderer=RENDER_PC; hulls=0;
    objectPosition.relative.t[2]=2048; assert(PortDrawNativeScenery(&object) && hulls==0);
    objectPosition.relative.t[2]=2049; assert(PortDrawNativeScenery(&object) && hulls==1);
    objectPosition.relative.t[2]=32766; assert(PortDrawNativeScenery(&object) && hulls==2);
    objectPosition.relative.t[2]=32767; assert(PortDrawNativeScenery(&object) && hulls==2);
    objectPosition.relative.t[2]=10000; objectPosition.relative.t[0]=10062;
    assert(PortDrawNativeScenery(&object) && hulls==3);
    objectPosition.relative.t[0]=10063; assert(PortDrawNativeScenery(&object) && hulls==3);
    objectPosition.relative.t[0]=1; objectPosition.relative.t[2]=2048;
    rotation.relative.m[2][0]=4095;
    assert(PortDrawNativeScenery(&object) && hulls==3); /* composed depth2048, not2048.999 */
    rotation.relative.m[2][0]=0; objectPosition.relative.t[0]=0;
    assert(!PcNativeObjectInRadius(3000,3000,0,0,3218,0));
    assert(PcNativeObjectInRadius(3000,3000,0,0,3219,0));
    assert(!PcNativeObjectInRadius(3219,0,0,0,3219,0));
    assert(PcNativeObjectInRadius(3219,0,0,0,3219,1));
    renderer=RENDER_SATURN; hulls=0;
    objectPosition.relative.t[2]=40063; assert(PortDrawNativeScenery(&object) && hulls==1);
    objectPosition.relative.t[2]=40064; assert(PortDrawNativeScenery(&object) && hulls==1);
    objectPosition.relative.t[2]=-1; assert(PortDrawNativeScenery(&object) && hulls==1);
    /* Real PRM byte order and registry: authored radius and depth mode must
       override the compatibility extent/default average, including frame tags. */
    SaturnNativePoly nativePoly = {.corners={0,1,2,3}, .depthMode=1};
    uint32_t nativeTag = SaturnNativeModel(&nativePoly, 1, &nativePoly, 1);
    SaturnNativeSetRadius(nativeTag, 8192);
    nativeTag = SaturnNativeFrameTag(nativeTag, 1);
    unsigned char storedTag[4] = {nativeTag >> 24, nativeTag >> 16, nativeTag >> 8, nativeTag};
    memcpy(&object.scratch, storedTag, sizeof(storedTag));
    hulls=0; objectPosition.relative.t[2]=40064;
    assert(PortDrawNativeScenery(&object) && hulls==1); /* authored radius reaches inside far plane */
    double depths[4] = {0,0,0,80000};
    assert(!PortNativeObjectDepthVisible(depths, (uint32_t)object.scratch, 0));
    depths[3]=40063;
    assert(PortNativeObjectDepthVisible(depths, (uint32_t)object.scratch, 0));
    SaturnNativeSetBillboard(nativeTag);
    objectPosition.relative.t[2]=-10000;
    assert(PortDrawNativeScenery(&object) && hulls==2); /* billboard bypasses sphere gate */
    object.scratch=0;
    renderer=RENDER_N64; hulls=0;
    objectPosition.relative.t[2]=20000; assert(PortDrawNativeScenery(&object) && hulls==1);
    objectPosition.relative.t[2]=100000; assert(PortDrawNativeScenery(&object) && hulls==1);
    objectPosition.relative.t[2]=4097; assert(PortDrawNativeScenery(&object) && hulls==1);
    objectPosition.relative.t[2]=4098; assert(PortDrawNativeScenery(&object) && hulls==2);
    objectPosition.relative.t[2]=76399; assert(PortDrawNativeScenery(&object) && hulls==3);
    objectPosition.relative.t[2]=76400; assert(PortDrawNativeScenery(&object) && hulls==3);
    rotation.relative.t[2]=1;
    objectPosition.relative.t[2]=4097; assert(PortDrawNativeScenery(&object) && hulls==3);
    objectPosition.relative.t[2]=4098; assert(PortDrawNativeScenery(&object) && hulls==4);
    rotation.relative.t[2]=0;

    /* Capture both profiles before any animation, even when Native is off.
       The odd X coordinate is outside PC's local AABB, but N64's loader
       rounds it to 5000: using the inherited PS1 list would omit it. */
    Skeleton secondPosition=objectPosition;
    Object second={.skeleton=&secondPosition,.extent=3000};
    object.next=&second; object.extent=3000;
    objectPosition.relative.t[0]=5001; objectPosition.relative.t[2]=5000;
    secondPosition.relative.t[0]=1000; secondPosition.relative.t[2]=5000;
    for(int i=0;i<120;i++) {
        sections[i].globalRadius=10000; sections[i].localRadius=5001;
        sections[i].objectCount=0;
    }
    renderer=RENDER_PS1; PortBuildNativeScene(&object);
    assert(nativeSceneSections[0][0].count==1 && nativeSceneSections[1][0].count==2);
    assert(!PortDrawNativeLandscape(&camera));
    renderer=RENDER_PC; hulls=0; assert(PortDrawNativeLandscape(&camera) && hulls==1);
    renderer=RENDER_N64; hulls=0; assert(PortDrawNativeLandscape(&camera) && hulls==2);
    ConfigData config = {0};
    Object *objects[] = {&object};
    gameData = &config;
    trackObjects = objects;
    renderer=RENDER_SATURN; hulls=0;
    assert(!PortDrawNativeLandscape(&camera));
    authored = 1;
    assert(PortDrawNativeLandscape(&camera) && hulls==2 && firstHull==&second);
    objectPosition.relative.t[0]=0;
    renderer=RENDER_PC; hulls=0; assert(PortDrawNativeLandscape(&camera) && hulls==1);
    PortBuildNativeScene(&object); hulls=0;
    assert(PortDrawNativeLandscape(&camera) && hulls==2);
    nativeScene=0; assert(!PortDrawNativeLandscape(&camera));
    track=NULL; PortBuildNativeScene(NULL);
    assert(!nativeSceneSections[0] && !nativeSceneSections[1]);
    puts("Track/scenery dispatch: hardware policies and Modern Native/Full road detail: OK");
}
