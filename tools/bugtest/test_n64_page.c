/* Private page metadata survives packet decoding, and is cleared by PS1 pages. */
#include "../../platform/psx/libgpu.c"
#include <assert.h>
#include <math.h>
#include <stddef.h>
_Static_assert(offsetof(GpuDrawState, fade) == 13 * sizeof(int), "frame recording layout");
PortSettings portSettings = {.render = RENDER_DEFAULTS};
PortConfig portConfig;
static int testLinkRole, testLinkInterpolate, testTracing;
int PortLinkRole(void) { return testLinkRole; }
int PortLinkSplit(void) { return testLinkRole != LINK_NONE; }
int PortLinkInterpolate(void) { return testLinkInterpolate; }
void PortLinkSetInterpolate(int on) { testLinkInterpolate = on; }
int PortTracing(void) { return testTracing; }
int portPal;
int PortScreenMargin(void) { return 0; }
static int testSaturnDisc;
int PortSaturnDisc(void) { return testSaturnDisc; }
int PortOriginalTrack(void) { return -1; }
int PortOriginalTrackIs64(void) { return 0; }
int PortPcData(void) { return 0; }
const struct Disc *PsxIoDisc(void) { return NULL; }
POLY_G4 flare[SHIP_EXHAUSTS + PLASMA_EXHAUSTS][FLARE_RAYS][2];
Trail Plasma[PLASMA_EXHAUSTS];
Trail *shipTrails;
static const SaturnNativePoly testFlares[2] = {
    {.width = 64, .height = 64, .corners = {0, 1, 3, 2}},
    {.width = 64, .height = 64, .corners = {0, 1, 3, 2}}
};
static int testFlareArt;
const SaturnNativePoly *SaturnArtFlare(int alternate) { return testFlareArt ? &testFlares[alternate] : NULL; }
static int frameClears, flushes;
void GpuFlush(void) { flushes++; }
void GpuUpdateResolution(void) {}
void FrameCapClear(void) { frameClears++; }
static FrameCapPrim recorded;
static int recordNative;
FrameCapPrim *FrameCapAdd(void) { return recordNative ? &recorded : NULL; }
void FrameCapTagRecord(FrameCapPrim *prim) { (void)prim; }
static GpuVertex clipped[12];
static int clippedCount;
static GpuDrawState lastDraw;
void GpuTriangles(const GpuDrawState *draw, const GpuVertex *v, int count)
{
    if (recordNative) {
        assert(draw->saturn && !draw->saturn->clipped);
    } else if(draw->saturn) {
        assert(draw->saturn->clipped && draw->saturn->vertexAttributes);
        assert(draw->saturn->corners[0]==0 && draw->saturn->corners[3]==0);
    }
    assert(count == 3 && clippedCount + count <= 12);
    memcpy(clipped + clippedCount, v, (size_t)count * sizeof(*v));
    clippedCount += count;
    lastDraw = *draw;
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

static void SaturnFlarePacket(void)
{
    GpuDrawState draw = {0};
    GpuVertex v[4] = {{0}, {.x = 100.5f, .y = 356.25f, .w = 400, .a = 230, .world = 1}};
    int32_t x[] = {101, 100, 113, 112}, y[] = {101, 100, 89, 88};
    const uint32_t *ray = (const uint32_t *)&flare[0][4][1] + 1;
    assert(!SaturnFlare(ray, &draw, v, x, y)); /* no Saturn art: retain the packet */
    testFlareArt = 1;
    assert(SaturnFlare((const uint32_t *)&flare[0][11][1] + 1, &draw, v, x, y) && !draw.saturn);
    assert(SaturnFlare((const uint32_t *)&flare[0][0][1] + 1, &draw, v, x, y) && !draw.saturn);
    assert(SaturnFlare(ray, &draw, v, x, y) && draw.saturn == &testFlares[0]);
    assert(v[0].x == 88.5f && v[0].y == 344.25f && v[3].x == 112.5f && v[3].y == 368.25f);
    for (int i = 0; i < 4; i++) assert(v[i].w == 400 && v[i].a == 230 && v[i].world);
    x[1] = 100;
    assert(SaturnFlare((const uint32_t *)&flare[18][4][0] + 1, &draw, v, x, y) && draw.saturn == &testFlares[1]);
    testFlareArt = 0;
    puts("Saturn exhaust replaces the flare quads with one anchored sprite and the appropriate palette: OK");
}

static void SaturnExhaustOrdering(void)
{
    RenderSettings saved = portSettings.render;
    static Trail trails[SHIP_EXHAUSTS];
    shipTrails = trails;
    portSettings.render = (RenderSettings)RENDER_DEFAULTS;
    portSettings.render.selection = RENDER_SATURN;
    RenderCommit();
    testSaturnDisc = testFlareArt = 1;

    u_long ot[4];
    ClearOTagR(ot, 4);
    TrackPackets table = {.table = &ot[3]};
    POLY_G4 *shipFlare = &flare[0][FLARE_ANCHOR_RAY][0];
    POLY_G4 *otherFlare = &flare[1][FLARE_ANCHOR_RAY][0];
    POLY_G4 *plasmaFlare = &flare[SHIP_EXHAUSTS + 1][FLARE_ANCHOR_RAY][0];
    POLY_G4 *shipTail = &trails[0].segment[0].leftHalf[0];
    POLY_G4 *shipMiddle = &trails[0].segment[1].rightHalf[0];
    POLY_G4 *otherTail = &trails[1].segment[0].leftHalf[0];
    POLY_G4 *plasmaTail = &Plasma[1].segment[0].rightHalf[0];
    POLY_G4 *packets[] = {shipFlare, otherFlare, plasmaFlare, shipTail, shipMiddle, otherTail, plasmaTail};
    for (size_t i = 0; i < SDL_arraysize(packets); i++) {
        SetPolyG4(packets[i]);
        SetSemiTrans(packets[i], 1);
    }
    AddPrim(&ot[3], shipMiddle);
    AddPrim(&ot[3], shipFlare); /* Drawn before the trail in its own bucket. */
    AddPrim(&ot[2], plasmaFlare);
    AddPrim(&ot[1], otherTail);
    AddPrim(&ot[1], shipTail);
    AddPrim(&ot[1], plasmaTail);
    AddPrim(&ot[0], otherFlare); /* Already after its own trail. */
    OrderSaturnExhaust(&table);
    assert(table.reorderExhaust);
    assert(table.exhaust[0].flare == (const uint32_t *)shipFlare + 1);
    assert(table.exhaust[0].after == (const uint32_t *)shipTail + 1);
    assert(!table.exhaust[1].after);
    assert(table.exhaust[SHIP_EXHAUSTS + 1].flare == (const uint32_t *)plasmaFlare + 1);
    assert(table.exhaust[SHIP_EXHAUSTS + 1].after == (const uint32_t *)plasmaTail + 1);
    assert(NodeAfter((u_long *)shipFlare) == (u_long *)shipMiddle); /* Original packet links retained. */

    testSaturnDisc = testFlareArt = 0;
    shipTrails = NULL;
    portSettings.render = saved;
    RenderCommit();
    puts("Saturn flares defer only to the last trail packet of the same exhaust: OK");
}

static void SaturnShadowPacket(void)
{
    POLY_FT4 packets[2] = {0};
    u_long ot[1];
    SetPolyFT4(&packets[0]);
    SetShadeTex(&packets[0], 1);
    SetSemiTrans(&packets[0], 1);
    packets[0].tpage = GetTPage(0, 2, 640, 256);
    packets[0].clut = GetClut(400, 450);
    packets[0].x1 = packets[0].x3 = 16;
    packets[0].y2 = packets[0].y3 = 16;
    packets[1] = packets[0];
    ClearOTagR(ot, 1);
    PortShadowRange(packets, sizeof(packets[0]));
    WalkTable(ot, 1);

    portSettings.render.selection = RENDER_SATURN;
    RenderCommit();
    clippedCount = 0;
    Polygon(&packets[0].tag + 1, packets[0].code);
    assert(lastDraw.textured && !lastDraw.raw && lastDraw.semi == 0);
    assert(lastDraw.clutX == 400 && lastDraw.clutY == 450);
    assert(clippedCount == 6 && clipped[0].world && clipped[0].w == 0);
    Polygon(&packets[1].tag + 1, packets[1].code);
    assert(lastDraw.raw && lastDraw.semi == 2);

    portSettings.render.selection = RENDER_PS1;
    RenderCommit();
    clippedCount = 0;
    Polygon(&packets[0].tag + 1, packets[0].code);
    assert(lastDraw.raw && lastDraw.semi == 2);
    walkTrack = -1;
    puts("Saturn ship shadow packets keep their mask and opt into mesh coverage: OK");
}

static void ShieldOrdering(void)
{
    u_long ot[4];
    POLY_G3 shield[2] = {0}, hull[2] = {0}, other = {0};
    ClearOTagR(ot, 4);
    for (int i = 0; i < 2; i++) {
        SetPolyG3(&shield[i]);
        SetSemiTrans(&shield[i], 1);
        SetPolyG3(&hull[i]);
    }
    SetPolyG3(&other);
    AddPrim(&ot[3], &shield[0]);
    AddPrim(&ot[2], &hull[1]);
    AddPrim(&ot[1], &hull[0]);
    AddPrim(&ot[0], &other);
    AddPrim(&ot[0], &shield[1]);
    PortShieldPackets(3, shield, shield + 2);
    PortShipPackets(3, hull, hull + 2);
    PortShipPackets(2, &other, &other + 1);
    WalkTable(&ot[3], 1);
    assert(tableTrack[walkTrack].shields[0].lastHull == &hull[0].tag);
    assert(!HoldShield(&other.tag));
    state.texpage = 0x220;
    assert(HoldShield(&shield[0].tag));
    assert(heldShields[0].count == 1 && heldShields[0].faces[0].texpage == 0x220);
    assert(NodeAfter(&shield[0].tag) == &ot[2]);
    WalkTable(&ot[3], 0);
    assert(heldShields[0].count == 0 && HoldShield(&shield[0].tag));
    WalkTable(NULL, 0);
    assert(!HoldShield(&shield[0].tag));

    /* Swap presents the newly rebuilt table during VSync, before DrawOTag.
       The same OT address must not reuse the previous frame's arena ranges. */
    POLY_G3 nextShield = {0}, nextHull = {0};
    SetPolyG3(&nextShield);
    SetSemiTrans(&nextShield, 1);
    SetPolyG3(&nextHull);
    ClearOTagR(ot, 4);
    AddPrim(&ot[3], &nextShield);
    AddPrim(&ot[1], &nextHull);
    PortShieldPackets(3, &nextShield, &nextShield + 1);
    PortShipPackets(3, &nextHull, &nextHull + 1);
    DRAWENV env = {0};
    PsxUpcomingTables(&ot[3], NULL, ot, &env);
    for (int presentation = 0; presentation < 4; presentation++) {
        WalkTable(&ot[3], presentation == 3);
        assert(heldShieldCount == 1);
        assert(heldShields[0].pending->lastHull == &nextHull.tag);
        assert(!HoldShield(&shield[0].tag));
        assert(HoldShield(&nextShield.tag));
        assert(SceneSprite(&nextShield.tag) && SceneSprite(&nextHull.tag));
    }
    /* Expiration must also be visible on the very first interpolated picture. */
    ClearOTagR(ot, 4);
    AddPrim(&ot[1], &nextHull);
    PsxUpcomingTables(&ot[3], NULL, ot, &env);
    WalkTable(&ot[3], 0);
    assert(!heldShieldCount && !HoldShield(&nextShield.tag));
    WalkTable(&ot[3], 1);
    assert(!heldShieldCount);
    WalkTable(NULL, 0);
    puts("Transparent shield follows its own hull and retains its draw mode during table replay: OK");
}

static void ClippedInterpolation(void)
{
    /* Both edge directions must agree at the cut. At z=8 -> 32, the cut is
       1/3 along the view-space edge and 2/3 along its screen projection.
       At or behind the eye plane, UVs use the view-space fraction. */
    const struct { float z, affine, perspective; int r, g, b, alpha; } cases[] = {
        {8, 170, 85, 170, 85, 150, 85},
        {0, 127.5f, 127.5f, 255, 0, 210, 128},
        {-8, 153, 153, 255, 0, 246, 153}
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

/* A clockwise camera turn moves a world landmark left in the view. The sky
   must sample farther right in its panorama, moving that landmark left too. */
static void SaturnSkyAngles(void)
{
    const int sine=1567,cosine=3784; /* approximately 22.5 degrees */
    GteSetBackground(1);
    GteWriteControl(26,160);
    for(int direction=-1;direction<=1;direction++) {
        int sn=direction*sine,cs=direction ? cosine : 4096;
        GteWriteControl(0,(uint16_t)cs);
        GteWriteControl(1,(uint16_t)-sn);
        GteWriteControl(2,4096);
        GteWriteControl(3,(uint16_t)sn);
        GteWriteControl(4,cs);
        GtePrecise p;
        uint32_t handle=GteRecordProjection(0,0,0,100,1);
        assert(GtePreciseOf(handle,&p) && p.background);
        float projectedWorldX=(float)-sn/cs;
        if(direction) assert(projectedWorldX*p.skyYaw<0);
        else assert(p.skyYaw==0);
        assert(SDL_fabsf(p.skyYaw-direction*256)<1);
        assert(p.skyFocal==160);
    }
    for(int direction=-1;direction<=1;direction++) {
        int sn=direction*sine,cs=direction ? cosine : 4096;
        GteWriteControl(0,4096);
        GteWriteControl(1,0);
        GteWriteControl(2,(uint16_t)cs | ((uint32_t)(uint16_t)-sn<<16));
        GteWriteControl(3,(uint32_t)(uint16_t)sn<<16);
        GteWriteControl(4,cs);
        GtePrecise p;
        uint32_t handle=GteRecordProjection(0,0,0,100,1);
        assert(GtePreciseOf(handle,&p) && p.background);
        /* World Y goes up for positive view pitch. My=120-pitch/3 must
           increase the source row, moving the sky up as well. */
        float projectedWorldY=(float)-sn/cs;
        if(direction) assert(projectedWorldY*(-p.skyPitch/3)<0);
        assert(SDL_fabsf(p.skyPitch+direction*256)<1);
    }
    /* Original European Saturn Phenitia Park demo, captured in Mednafen:
       camera angles at 0x060734dc = {-303,459,0}; view matrix at
       0x0607b804. Convert its 16.16 elements to GTE 4.12. This checks the
       angle convention against the running original, including combined axes. */
    const int32_t matrix[9]={49951,0,-42424,-19017,58583,-22391,37923,29376,44651};
    for(int i=0;i<4;i++)
        GteWriteControl(i,(uint16_t)(matrix[i*2]/16) | ((uint32_t)(uint16_t)(matrix[i*2+1]/16)<<16));
    GteWriteControl(4,matrix[8]/16);
    GtePrecise original;
    assert(GtePreciseOf(GteRecordProjection(0,0,0,100,1),&original));
    assert(SDL_fabsf(original.skyYaw-459)<1);
    assert(SDL_fabsf(original.skyPitch+303)<1);
    assert(original.skyRoll==0);
    GteSetBackground(0);
    GteWriteControl(0,4096);GteWriteControl(1,0);GteWriteControl(2,4096);GteWriteControl(3,0);GteWriteControl(4,4096);
    puts("Saturn panorama yaw/pitch follow world motion and match original Saturn camera angles: OK");
}

static void SaturnSkyInterpolation(void)
{
    GteKeepPast(1);
    for(int frame=0;frame<3;frame++) {
        GteClearPrecise();
        GteSetBackground(0);
        /* Stable world correspondences allow interpolation; the last frame
           jumps far enough to exercise the same cut guard as world geometry. */
        for(int i=0;i<8;i++) {
            GteKeyNext(0,100+i);
            uint32_t h=GteRecordProjection(i,0,0,500,1);
            GteRecordView(h,(i&1)*100+(frame==2 ? 1000000 : 0),((i>>1)&1)*100,500+((i>>2)&1)*100);
        }
        float yaw=frame==0 ? 2040 : frame==1 ? -2040 : -2000;
        int sn=(int)roundf(sinf(yaw*(6.28318530718f/4096))*4096);
        int cs=(int)roundf(cosf(yaw*(6.28318530718f/4096))*4096);
        GteWriteControl(0,(uint16_t)cs);GteWriteControl(1,(uint16_t)-sn);
        GteWriteControl(2,4096);GteWriteControl(3,(uint16_t)sn);GteWriteControl(4,cs);
        GteSetBackground(1);
        GtePrecise p,moved;
        assert(GtePreciseOf(GteRecordProjection(50,0,0,500,1),&p));
        if(frame==1) {
            GteInterpolateSky(&p,.5f,&moved);
            assert(fabsf(fabsf(moved.skyYaw)-2048)<.1f);
            GteInterpolateSky(&p,1,&moved);assert(moved.skyYaw==p.skyYaw);
            GteKeepPast(0);GteInterpolateSky(&p,.5f,&moved);assert(moved.skyYaw==p.skyYaw);
            GteKeepPast(1);
        } else if(frame==2) {
            GteInterpolateSky(&p,.5f,&moved);assert(moved.skyYaw==p.skyYaw);
        }
    }
    GteKeepPast(0);GteSetBackground(0);GteClearPrecise();
    GteWriteControl(0,4096);GteWriteControl(1,0);GteWriteControl(3,0);GteWriteControl(4,4096);
    puts("Saturn sky interpolates across the yaw seam and respects disabled interpolation and camera cuts: OK");
}

static void SaturnClip(void)
{
    portSettings.render=(RenderSettings)RENDER_DEFAULTS;
    portSettings.render.selection=RENDER_SATURN;RenderCommit();
    SaturnNativePoly poly={.mode=0xc4,.corners={0,1,2,0}};
    GpuDrawState draw={.saturn=&poly};
    GpuVertex v[3]={0};
    GteWriteControl(26,32);resolvedMask=7;
    for(int i=0;i<3;i++) {
        resolved[i]=(GtePrecise){.z=i?32:8,.view={i==1?32:0,i==2?32:0,i?32:8}};
        v[i]=(GpuVertex){.w=i?32:8,.a=255,.u=i?24:0,.saturnShade={i?24:0,16,16}};
    }
    clippedCount=0;assert(NearCut(&draw,v,3,0) && clippedCount==6);
    for(int i=0;i<clippedCount;i++) if(clipped[i].w==16) {
        assert(SDL_fabsf(clipped[i].saturnShade[0]-16)<.001f);
        assert(SDL_fabsf(clipped[i].u-16)<.001f);
    }
    poly.trackTile=1;
    clippedCount=0;assert(NearCut(&draw,v,3,0) && clippedCount==6);
    for(int i=0;i<clippedCount;i++) if(clipped[i].w==16) {
        assert(SDL_fabsf(clipped[i].u-8)<.001f);
    }
    resolvedMask=0;
}

static void SaturnCaptureAtlasUV(void)
{
    uint8_t indices[8]={0};
    SaturnNativePoly material={.textured=1,.width=8,.height=1,.pixels=indices,.corners={0,1,2,0}};
    POLY_FT3 packet={.tag=7u<<24,.u0=150,.v0=90,.u1=170,.v1=90,.u2=150,.v2=110};
    const uint32_t *words=(const uint32_t *)&packet+1;

    SaturnNativeBind(&packet,&packet+1,SaturnNativeModel(&material,1,NULL,0),0);
    testSaturnDisc=recordNative=1;
    clippedCount=0;
    Polygon(words,0x24);
    assert(clippedCount==3 && recorded.count==3 && !recorded.draw.saturn);
    assert(recorded.v[0].u==150 && recorded.v[0].v==90);
    assert(recorded.v[1].u==170 && recorded.v[2].v==110);
    testSaturnDisc=recordNative=0;
    SaturnNativeShutdown();
    puts("Saturn frame inspection keeps compatibility atlas UVs: OK");
}

static void WorldBillboardClip(void)
{
    POLY_FT4 packet = {0};
    const uint32_t *words[] = {(const uint32_t *)&packet.x0, (const uint32_t *)&packet.x1,
                              (const uint32_t *)&packet.x2, (const uint32_t *)&packet.x3};
    GpuVertex v[4] = {0};
    GpuDrawState draw = {.textured = 1, .depth = 2, .semi = -1};
    portSettings.render.selection = RENDER_PC; RenderCommit();
    GteSetExactDepth(1);
    for (int i = 0; i < 4; i++) {
        double x = i & 1 ? 20 : -20, y = i < 2 ? -20 : 20, z = i < 2 ? -100 : 200;
        uint32_t handle = GteRecordProjection(0, 0, 0, (float)z, 1);
        GteRecordView(handle, x, y, z);
        GteBindPrecise(words[i], handle);
    }
    GteSetExactDepth(0);
    assert(Vertices(v, words, 4) == 15 && GteResolvedBound());
    for (int i = 0; i < 4; i++) {
        assert(v[i].world && v[i].exactDepth && !v[i].noDepth);
        v[i].u = i & 1 ? 255 : 0;
        v[i].v = i < 2 ? 0 : 255;
    }
    clippedCount = 0;
    assert(NearCut(&draw, v, 4, AlternateQuad(&draw, v, 4, (const uint32_t *)&packet)));
    assert(clippedCount == 9);
    for (int i = 0; i < clippedCount; i++) {
        assert(clipped[i].w >= 16 && clipped[i].world && clipped[i].exactDepth && !clipped[i].noDepth);
        if (clipped[i].w == 16) assert(SDL_fabsf(clipped[i].v - 98.6f) < .001f);
    }
    resolvedMask = 0;
    puts("PC billboard bound corners retain perspective UVs and depth through eye-plane clipping: OK");
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

static void OriginalInterpolation(void)
{
    portSettings.frameRate = FRAME_RATE_CONSOLE;
    for (int id = RENDER_PS1; id <= RENDER_SATURN; id++) {
        portSettings.render.selection = (RendererId)id; RenderCommit();
        assert(PsxInterpolationOn() == (id != RENDER_PS1));
        portConfig.hidden = 1; assert(!PsxInterpolationOn()); portConfig.hidden = 0;
        portConfig.fixedStep = 1; assert(!PsxInterpolationOn()); portConfig.fixedStep = 0;
        testTracing = 1; assert(!PsxInterpolationOn()); testTracing = 0;
        testLinkRole = LINK_MASTER;
        assert(PsxInterpolationOn() == (id != RENDER_PS1));
        testLinkRole = LINK_SLAVE;
        assert(PsxInterpolationOn() == (id != RENDER_PS1));
        testLinkRole = LINK_NONE;
    }
    portSettings.frameRate = FRAME_RATE_UNCAPPED;
    portSettings.render.selection = RENDER_PS1; RenderCommit();
    assert(PsxInterpolationOn());
    GteKeepPast(0);
    puts("Original interpolation follows the renderer; fixed-step, tracing and linked-player rules remain intact: OK");
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
    SaturnSkyAngles();
    SaturnSkyInterpolation();
    SaturnClip();
    SaturnCaptureAtlasUV();
    SaturnFlarePacket();
    SaturnExhaustOrdering();
    SaturnShadowPacket();
    ShieldOrdering();
    WorldBillboardClip();
    OriginalInterpolation();
    return 0;
}
