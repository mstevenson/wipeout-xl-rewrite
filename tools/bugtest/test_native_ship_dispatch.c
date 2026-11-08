/* Execute the actual DrawShips: Native may replace packets, never simulation. */
#include "../../game/src/ships.c"
#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <string.h>

char errorString[256];
GhostClut ghostCluts[15];
short shipTex;
Texture *textureTable[1];
void Error(char *s, short level) { (void)s; (void)level; assert(0); }
u_short GetTPage(int tp, int abr, int x, int y) { (void)tp; (void)abr; (void)x; (void)y; return 0; }

int proximity, ownShip, raceType, remoteShip = 1, ghostShip = 2;
short numTrackSections = 100;
int shipTargetIndex[15];
RaceLine raceLines[100];
TrackData *track;
static int enabled, packets, marks, nativeCalls, sound[4];
int PortNativeShipsEnabled(void) { return enabled; }
void PortFarMarkShip(int ship) { marks |= 1 << ship; }
void PortInterpInstance(int instance) { (void)instance; }
void PortDrawNativeShips(TrackCamera *camera, ShipData *ships, Object **shapes, int cam)
{ (void)camera; (void)ships; (void)shapes; (void)cam; nativeCalls++; }
int GetMagnitude(VECTOR v) { return (int)sqrt((double)v.vx*v.vx+(double)v.vy*v.vy+(double)v.vz*v.vz); }
void SetSkeletonPosition(Skeleton *s, int x, int y, int z) { s->relative.t[0]=x; s->relative.t[1]=y; s->relative.t[2]=z; }
void SetSkeletonDirectionHPR(Skeleton *s, short h, short p, short r) { (void)s; (void)h; (void)p; (void)r; }
void SetSkeletonTransform(Skeleton *s) { (void)s; }
void RotTrans(SVECTOR *v, VECTOR *out, int *flag) { out->vx=v->vx; out->vy=v->vy; out->vz=v->vz; *flag=0; }
void AsmDrawObjectLit(Object *o, Skeleton *s, int r, CVECTOR *c) { (void)o; (void)s; (void)r; (void)c; packets++; }
void AdjustRemoteEngineSound(int a, int b, int c, int d) { sound[0]=a; sound[1]=b; sound[2]=c; sound[3]=d; }

int main(void)
{
    ShipData initial[15] = {0}, legacy[15], native[15];
    Skeleton skeleton = {0}, cam = {0};
    Object shape = {.skeleton=&skeleton}, *shapes[] = {&shape};
    TrackSection section = {0};
    Face faces[2] = {{.flags=TRACK_BASE},{.flags=TRACK_BASE}};
    TrackData course = {.faces=faces};
    TrackCamera camera = {.camPos=&cam,.section=&section};
    int legacySound[4], legacyProximity;
    track=&course;
    for (int i=0; i<15; i++) {
        initial[i].nearTrkSect=&section;
        initial[i].ppivot.vz=i*2400+1;
        initial[i].attr=LSIDE;
    }
    memcpy(legacy,initial,sizeof(initial)); memcpy(native,initial,sizeof(initial));
    DrawShips(legacy,shapes,0,camera,-1);
    assert(packets==4 && marks==15 && nativeCalls==0);
    legacyProximity=proximity; memcpy(legacySound,sound,sizeof(sound));
    enabled=1; packets=marks=0;
    DrawShips(native,shapes,0,camera,-1);
    assert(packets==0 && marks==0 && nativeCalls==1);
    assert(memcmp(legacy,native,sizeof(legacy))==0);
    assert(proximity==legacyProximity && memcmp(sound,legacySound,sizeof(sound))==0);
    puts("Native ship dispatch replaces drawing while preserving PS1 ship flags, proximity and engine sound: OK");
}
