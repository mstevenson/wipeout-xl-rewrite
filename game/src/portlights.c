/* Port: read-only weapon/effect/ship metadata for enhanced presentation. No game state is changed. */
#include "functions.h"
#include "weapon.h"
#include "sparks.h"
#include "../../platform/render/point_lights.h"

extern ShipData *shipIndex;
extern int ownShip;
extern Explosion explosions[];

/* Apply the skeleton chain without GetSkeletonMatrix's cache writes or GTE side effects. */
static void LightView(const Skeleton *skeleton, double p[3])
{
    while (skeleton) {
        double next[3];
        for (int i = 0; i < 3; i++) {
            next[i] = skeleton->relative.t[i];
            for (int j = 0; j < 3; j++) next[i] += skeleton->relative.m[i][j] * p[j] / 4096.0;
        }
        for (int i = 0; i < 3; i++) p[i] = next[i];
        skeleton = skeleton->super;
    }
}

static GpuLightSource Light(unsigned id, int life, double x, double y, double z,
                            float radius, float r, float g, float b, float intensity)
{
    const Skeleton *camera = cameraPtr->camPos;
    double p[3] = {x + camera->relative.t[0], y + camera->relative.t[1], z + camera->relative.t[2]};
    LightView(camera->super, p);
    return (GpuLightSource){{{(float)p[0], (float)p[1], (float)p[2]}, radius, {r,g,b}, intensity}, id, life};
}

void PortCaptureWeaponLights(void)
{
    GpuLightSource sources[GPU_LIGHT_SOURCES];
    int count = 0;
    if (!cameraPtr || !cameraPtr->camPos) {
        PsxCaptureLights(NULL, 0);
        return;
    }
    for (int i = 0; weaponIndex && i < 30; i++) {
        const WeaponData *w = &weaponIndex[i];
        if (w->count <= 0) continue;
        float radius, r, g, b, intensity;
        /* Initializers can hold a delayed weapon whose position is not valid yet. */
        if ((w->type == WEAPON_ROCKET && w->update == UpdateRocket) ||
            (w->type == WEAPON_MISSILE && w->update == UpdateMissile)) {
            radius = 1800; r = 1; g = 0.32f; b = 0.055f; intensity = 2.8f;
        } else if (w->type == WEAPON_ELECTROBOLT && w->update == UpdateElecBolt) {
            radius = 2200; r = 0.12f; g = 0.4f; b = 1; intensity = 3.2f;
        } else if (w->type == WEAPON_PLASMA && w->update == UpdatePlasma) {
            /* UpdatePlasma draws only the charge bar above this regional threshold. */
            if (w->owner == ownShip && w->count > REGION(200, 140)) continue;
            radius = 2800; r = 0.4f; g = 0.16f; b = 1; intensity = 4;
        } else continue;
        sources[count++] = Light((unsigned)(i * 32 + w->type + 1), w->count,
                                  w->pos.vx, w->pos.vy, w->pos.vz, radius, r, g, b, intensity);
    }
    for (int i = 0; i < EXPLOSION_COUNT; i++) {
        const Explosion *e = &explosions[i];
        /* DrawExplode follows this hook and decrements additive explosions once.
           Smoke and subtractive sprites are not light sources. */
        int life = e->life - 1;
        if (life <= 0 || e->blend != 1) continue;
        double x = e->x, y = e->y, z = e->z;
        if (e->ship >= 0) {
            if (!shipIndex || e->ship >= 15) continue;
            x += shipIndex[e->ship].ppivot.vx;
            y += shipIndex[e->ship].ppivot.vy;
            z += shipIndex[e->ship].ppivot.vz;
        }
        float fade = life < 12 ? life / 12.0f : 1.0f;
        float size = 1.0f / (1u << (e->size < 4 ? e->size : 4));
        sources[count++] = Light(1024u + (unsigned)i, life, x, y, z,
                                  1800 + 2800 * size, e->r / 128.0f,
                                  e->g / 128.0f * 0.65f, e->b / 128.0f * 0.3f, 5 * fade * fade);
    }
    for (int i = 0; shipIndex && i < 15; i++) {
        const ShipData *ship = &shipIndex[i];
        if (!ship->nearTrkSect || (ship->combatAttr & DESTROYED)) continue;
        /* Match UpdateShipTrails' engine position, including the vertical offset.
           Off-screen ships and the cockpit ship can still light nearby surfaces. */
        double x = ship->ppivot.vx - ship->unitVecNose.vx * (420.0 / 4096.0);
        double y = ship->ppivot.vy - ship->unitVecNose.vy * (420.0 / 4096.0) - 15;
        double z = ship->ppivot.vz - ship->unitVecNose.vz * (420.0 / 4096.0);
        sources[count++] = Light(2048u + (unsigned)i, 1, x, y, z,
                                  1000, 0.05f, 0.25f, 1, 0.35f);
        /* The inventory weapon is not an active shield. Read its live flag. */
        if (ship->attr & SHIELDED)
            sources[count++] = Light(4096u + (unsigned)i, 1,
                                      ship->ppivot.vx, ship->ppivot.vy, ship->ppivot.vz,
                                      1600, 0.08f, 0.32f, 1, 1.8f);
    }
    PsxCaptureLights(sources, count);
}
