/* Real weapon/effect source adaptation: no simulation writes or renderer globals. */
#include "../../game/src/portlights.c"
#include <assert.h>
#include <string.h>
#include <math.h>

static WeaponData weapons[30];
WeaponData *weaponIndex = weapons;
static ShipData ships[15];
static TrackSection section;
ShipData *shipIndex = ships;
static VECTOR wallVertex;
static TrackData course = {.vertices = &wallVertex};
TrackData *track = &course;
static uint64_t frame;
uint64_t PortGameFrames(void) { return frame; }
int ownShip, portPal;
Explosion explosions[EXPLOSION_COUNT];
static Skeleton rotation, camera;
static TrackCamera trackCamera;
TrackCamera *cameraPtr = &trackCamera;
static GpuLightSource captured[GPU_LIGHT_SOURCES];
static int capturedCount;
void PsxCaptureLights(const GpuLightSource *sources, int count)
{
    assert(count >= 0 && count <= GPU_LIGHT_SOURCES);
    capturedCount = count;
    if (count) memcpy(captured, sources, count * sizeof(*sources));
}
#define UPDATE(name) void name(ShipData *a, WeaponShape *b, WeaponData *c, WeaponData *d) {}
UPDATE(UpdateRocket)
UPDATE(UpdateMissile)
UPDATE(UpdateElecBolt)
UPDATE(UpdatePlasma)
UPDATE(InitRocket)

int main(void)
{
    trackCamera.camPos = &camera;
    camera.super = &rotation;
    camera.relative.t[0] = -100;
    rotation.relative.m[0][2] = 4096;
    rotation.relative.m[1][1] = 4096;
    rotation.relative.m[2][0] = -4096;
    rotation.relative.t[1] = 20;
    for (int i = 0; i < 5; i++) {
        weapons[i].count = 20;
        weapons[i].pos = (VECTOR){300, 400, 500};
    }
    weapons[0].type = WEAPON_ROCKET; weapons[0].update = UpdateRocket;
    weapons[1].type = WEAPON_MISSILE; weapons[1].update = UpdateMissile;
    weapons[2].type = WEAPON_ELECTROBOLT; weapons[2].update = UpdateElecBolt;
    weapons[3].type = WEAPON_PLASMA; weapons[3].update = UpdatePlasma;
    weapons[4].type = WEAPON_ROCKET; weapons[4].update = InitRocket;
    explosions[0] = (Explosion){.x=300, .y=400, .z=500, .r=128, .g=128, .b=128, .life=16, .blend=1, .ship=-1, .size=1};
    explosions[1] = explosions[0]; explosions[1].ship = 2; explosions[1].life = 5;
    ships[2].ppivot = (VECTOR){100, 200, 300};
    explosions[2] = explosions[0]; explosions[2].blend = 2;
    explosions[3] = explosions[0]; explosions[3].life = 1;
    WeaponData before[30]; Explosion effects[EXPLOSION_COUNT]; Skeleton beforeCamera = camera, beforeRotation = rotation;
    memcpy(before, weapons, sizeof(before)); memcpy(effects, explosions, sizeof(effects));
    PortCaptureWeaponLights();
    assert(capturedCount == 6);
    assert(captured[0].light.position[0] == 500 && captured[0].light.position[1] == 420 && captured[0].light.position[2] == -200);
    assert(captured[5].light.position[0] == 800 && captured[5].light.position[1] == 620 && captured[5].light.position[2] == -300);
    assert(captured[4].life == 15 && captured[5].life == 4);
    assert(captured[5].light.intensity < captured[4].light.intensity);
    assert(!memcmp(before, weapons, sizeof(before)) && !memcmp(effects, explosions, sizeof(effects)));
    assert(!memcmp(&camera, &beforeCamera, sizeof(camera)) && !memcmp(&rotation, &beforeRotation, sizeof(rotation)));
    for (int pal = 0; pal <= 1; pal++) {
        portPal = pal;
        weapons[3].count = REGION(200, 140) + 1;
        PortCaptureWeaponLights(); assert(capturedCount == 5);
        weapons[3].count--;
        PortCaptureWeaponLights(); assert(capturedCount == 6);
    }
    for (int i = 0; i < 30; i++) weapons[i] = weapons[0];
    for (int i = 0; i < EXPLOSION_COUNT; i++) explosions[i] = explosions[0];
    PortCaptureWeaponLights(); assert(capturedCount == 62);
    memset(weapons, 0, sizeof(weapons));
    memset(explosions, 0, sizeof(explosions));
    ships[0].nearTrkSect = ships[1].nearTrkSect = ships[2].nearTrkSect = &section;
    ships[0].ppivot = ships[1].ppivot = (VECTOR){300, 400, 500};
    ships[0].unitVecNose = (VECTOR){0, 0, 4096};
    ships[1].unitVecNose = (VECTOR){4096, 0, 0};
    ships[0].attr = VIEW_IN; /* Cockpit and off-screen ships still illuminate the world. */
    ships[0].weapon = WEAPON_SHIELD; /* Holding a shield does not activate its light. */
    ships[1].attr = SHIELDED;
    ships[2].combatAttr = DESTROYED;
    ships[2].attr = SHIELDED;
    ShipData beforeShips[15];
    memcpy(beforeShips, ships, sizeof(ships));
    PortCaptureWeaponLights(); assert(capturedCount == 3);
    assert(!memcmp(beforeShips, ships, sizeof(ships)));
    assert(captured[0].light.position[0] == 80 && captured[0].light.position[1] == 405 && captured[0].light.position[2] == -200);
    assert(captured[1].light.position[0] == 500 && captured[1].light.position[1] == 405 && captured[1].light.position[2] == 220);
    assert(captured[2].light.position[0] == 500 && captured[2].light.position[1] == 420 && captured[2].light.position[2] == -200);
    assert(captured[2].light.intensity > captured[1].light.intensity * 4);
    assert(captured[2].light.radius > captured[1].light.radius);
    for (int i = 0; i < 3; i++) assert(captured[i].light.colour[2] > captured[i].light.colour[1]);
    unsigned engine0 = captured[0].id, engine1 = captured[1].id, shield1 = captured[2].id;
    GpuPointLight idleShield = captured[2].light;
    PortSetShieldFlash(1, 245, 0); /* Wall hit. */
    PortCaptureWeaponLights();
    GpuPointLight wallFlash = captured[2].light;
    assert(wallFlash.colour[0] > wallFlash.colour[2] && wallFlash.colour[1] < idleShield.colour[1]);
    PortSetShieldFlash(1, 245, 245); /* Weapon hit, with both channels flashing. */
    PortCaptureWeaponLights();
    GpuPointLight weaponFlash = captured[2].light;
    assert(weaponFlash.colour[0] == wallFlash.colour[0] && weaponFlash.colour[1] > weaponFlash.colour[2]);
    PortSetShieldFlash(1, 125, 125);
    PortCaptureWeaponLights();
    assert(captured[2].light.colour[0] < weaponFlash.colour[0]);
    assert(captured[2].light.colour[1] < weaponFlash.colour[1]);
    assert(captured[2].light.colour[2] > weaponFlash.colour[2]);
    assert(captured[2].light.radius == idleShield.radius && captured[2].light.intensity == idleShield.intensity);
    assert(captured[2].id == shield1 && captured[1].id == engine1 && captured[1].light.colour[2] == 1);
    ships[1].attr &= ~SHIELDED;
    weaponIndex = NULL;
    PortCaptureWeaponLights(); assert(capturedCount == 2);
    assert(captured[0].id == engine0 && captured[1].id == engine1);
    ships[1].attr |= SHIELDED;
    PortSetShieldFlash(1, 0, 0);
    PortCaptureWeaponLights(); assert(capturedCount == 3 && captured[2].id == shield1);
    for (int i = 0; i < 3; i++) assert(captured[2].light.colour[i] == idleShield.colour[i]);
    shipIndex = NULL;
    PortCaptureWeaponLights(); assert(!capturedCount);
    shipIndex = ships; weaponIndex = weapons;
    for (int i = 0; i < 15; i++) {
        ships[i].nearTrkSect = &section;
        ships[i].combatAttr = 0;
        ships[i].attr = SHIELDED;
    }
    for (int i = 0; i < 30; i++) weapons[i] = before[0];
    for (int i = 0; i < EXPLOSION_COUNT; i++) explosions[i] = effects[0];
    PortCaptureWeaponLights(); assert(capturedCount == 92);
    Face wall = {.normal = {4096, 0, 0}};
    VECTOR contact = {90, 400, 500};
    wallVertex.vx = 100;
    for (int i = 0; i < 15; i++) PortWallSparkLight(&ships[i], &contact, &wall);
    PortCaptureWeaponLights(); assert(capturedCount == 107);
    for (int i = 0; i < capturedCount; i++)
        for (int j = 0; j < i; j++) assert(captured[i].id != captured[j].id);
    GpuLightSource firstSpark = captured[92];
    assert(firstSpark.light.radius == 770);
    assert(firstSpark.light.position[0] == 500 && firstSpark.light.position[1] == 420 && firstSpark.light.position[2] == -160);
    assert(firstSpark.light.colour[0] > firstSpark.light.colour[1] && firstSpark.light.colour[1] > firstSpark.light.colour[2]);
    frame++;
    PortWallSparkLight(&ships[0], &contact, &wall);
    PortCaptureWeaponLights(); assert(capturedCount == 107);
    assert(captured[92].id == firstSpark.id && captured[92].life == firstSpark.life);
    assert(captured[92].light.intensity != firstSpark.light.intensity);
    frame += WALL_SPARK_LIFE - 1;
    PortCaptureWeaponLights(); assert(capturedCount == 93);
    assert(captured[92].life == 1 && captured[92].light.intensity < firstSpark.light.intensity);
    frame++;
    PortCaptureWeaponLights(); assert(capturedCount == 92);
    cameraPtr = NULL; PortCaptureWeaponLights(); assert(!capturedCount);
    puts("Dynamic light sources: projectiles, explosions, engine offsets, active shields, wall sparks, view transform, full pools and read-only state: OK");
}
