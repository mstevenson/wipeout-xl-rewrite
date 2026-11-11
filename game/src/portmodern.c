/* Port: read-only Modern source adapter. No graphics device or simulation ownership. */
#include <stdlib.h>
#include <string.h>
#include <stddef.h>
#include <limits.h>
#include "functions.h"
#include "track.h"
#include "portorig.h"
#include "weapon.h"
#include "sparks.h"
#include "portmodern.h"
#include "../../platform/render/modern/point_lights.h"
extern TrackData *track;
extern ShipData *shipIndex;
extern int ownShip;
extern Explosion explosions[];
static void ResetSources(void);
static uint32_t sourceGeneration;
static const HeapBlock *courseHeap;
static uintptr_t *sourceObjects;
static size_t sourceObjectCount;
extern HeapBlock *heap;
static unsigned char *faceEncoding;
static int faceEncodingCount;
void PortModernTrackLoaded(void)
{
    PortResetTrackLighting();
    ResetSources();
    courseHeap = heap;
    if (!track || track->faceCount <= 0) return;
    faceEncoding = malloc((size_t)track->faceCount);
    if (faceEncoding) {
        faceEncodingCount = track->faceCount;
        memset(faceEncoding, PortOriginalTrackIs64() ? RENDER_LIGHT_RGB8 : RENDER_LIGHT_PS1,
               (size_t)faceEncodingCount);
    }
}
int PortModernLightFormat(const Face *face)
{
    if (!track || !faceEncoding) return RENDER_LIGHT_PS1;
    ptrdiff_t index = face - track->faces;
    return index >= 0 && index < faceEncodingCount ? faceEncoding[index] : RENDER_LIGHT_PS1;
}
void PortModernTrackColour(const Face *face)
{
    if (!track || !faceEncoding) return;
    ptrdiff_t index = face - track->faces;
    if (index >= 0 && index < faceEncodingCount) faceEncoding[index] = RENDER_LIGHT_PS1;
}
/* Port: each corner joins only through a shared edge on the same kind of
   surface. This keeps crossings, road/wall creases and special pads separate. */
typedef struct TrackLightEdge {
    int low, high;
    int lowCorner, highCorner;
} TrackLightEdge;

static int *trackLightNext;
static int trackLightBuilt;
static const int trackLightCornerOrder[] = {1, 0, 2, 3};

void PortResetTrackLighting(void)
{
    free(faceEncoding); faceEncoding = NULL; faceEncodingCount = 0;
    free(trackLightNext);
    trackLightNext = NULL;
    trackLightBuilt = 0;
}

static int CompareLightEdges(const void *a, const void *b)
{
    const TrackLightEdge *left = a, *right = b;
    if (left->low != right->low) return left->low - right->low;
    return left->high - right->high;
}

static int LightRoot(int *parent, int corner)
{
    while (parent[corner] != corner) {
        parent[corner] = parent[parent[corner]];
        corner = parent[corner];
    }
    return corner;
}

static int SmoothSurface(const Face *a, const Face *b)
{
    if ((a->flags | b->flags) & (WEAPON_GRID_LEFT | WEAPON_GRID_RIGHT | SPEED_UP | PIT_LANE | CHECKPOINT)) return 0;
    if (((a->flags ^ b->flags) & TRACK_BASE) != 0) return 0;
    double ax = a->normal.vx, ay = a->normal.vy, az = a->normal.vz;
    double bx = b->normal.vx, by = b->normal.vy, bz = b->normal.vz;
    double dot = ax * bx + ay * by + az * bz;
    /* Preserve sharp creases, allowing up to 60 degrees between neighbours. */
    return dot > 0 && 4 * dot * dot >= (ax * ax + ay * ay + az * az) * (bx * bx + by * by + bz * bz);
}

static void BuildTrackLighting(void)
{
    trackLightBuilt = 1;
    if (track->faceCount <= 0) return;
    if ((size_t)track->faceCount > (size_t)INT_MAX / 4) return;
    int corners = track->faceCount * 4;
    TrackLightEdge *edges = malloc((size_t)corners * sizeof(*edges));
    int *parent = malloc((size_t)corners * sizeof(*parent));
    int *next = malloc((size_t)corners * sizeof(*next));
    if (edges && parent && next) {
        static const int perimeter[] = {1, 0, 3, 2, 1};
        int count = 0;
        for (int i = 0; i < corners; i++) parent[i] = next[i] = i;
        for (int i = 0; i < track->faceCount; i++) {
            const Face *face = &track->faces[i];
            for (int j = 0; j < 4; j++) {
                int a = perimeter[j], b = perimeter[j + 1];
                int va = face->vertex[a], vb = face->vertex[b];
                if (va < 0 || vb < 0 || va >= track->vertexCount || vb >= track->vertexCount || va == vb) continue;
                edges[count++] = va < vb ? (TrackLightEdge){va, vb, 4 * i + a, 4 * i + b}
                                        : (TrackLightEdge){vb, va, 4 * i + b, 4 * i + a};
            }
        }
        qsort(edges, (size_t)count, sizeof(*edges), CompareLightEdges);
        for (int first = 0, end; first < count; first = end) {
            for (end = first + 1; end < count && !CompareLightEdges(&edges[first], &edges[end]); end++) {}
            if (end - first != 2) continue; /* Open or ambiguous edge. */
            const TrackLightEdge *a = &edges[first], *b = &edges[first + 1];
            if (a->lowCorner / 4 == b->lowCorner / 4 ||
                !SmoothSurface(&track->faces[a->lowCorner / 4], &track->faces[b->lowCorner / 4])) continue;
            parent[LightRoot(parent, a->lowCorner)] = LightRoot(parent, b->lowCorner);
            parent[LightRoot(parent, a->highCorner)] = LightRoot(parent, b->highCorner);
        }
        /* Make a ring for each group so all its faces contribute the same mean. */
        for (int i = 0; i < corners; i++) {
            int root = LightRoot(parent, i);
            if (root != i) {
                next[i] = next[root];
                next[root] = i;
            }
        }
        trackLightNext = next;
    } else {
        free(next);
    }
    free(parent);
    free(edges);
}

/* Packet order is face vertices 1, 0, 2, 3. Use live RGB, keeping the
   lighting scale of the receiving face (notably on Wipeout 64 imports). */
int PortModernTrackCorners(const Face *face, uint32_t colours[4])
{
    if (!PortRenderGeometry().smoothTrackLighting) return 0;
    if (!trackLightBuilt) BuildTrackLighting();
    if (!trackLightNext) return 0;
    int base = 4 * (int)(face - track->faces);
    int blended = 0;
    for (int i = 0; i < 4; i++) {
        int start = base + trackLightCornerOrder[i], corner = start;
        unsigned r = 0, g = 0, b = 0, count = 0;
        do {
            const Face *source = &track->faces[corner / 4];
            if (PortModernLightFormat(source) == PortModernLightFormat(face)) {
                r += source->red;
                g += source->green;
                b += source->blue;
                count++;
            }
            corner = trackLightNext[corner];
        } while (corner != start);
        colours[i] = ((r + count / 2) / count) | (((g + count / 2) / count) << 8) |
                     (((b + count / 2) / count) << 16);
        blended |= count > 1;
    }
    return blended;
}

/* Sample the two triangles of the low-detail face, including their common
   diagonal. A shared edge samples the same colours at every grid resolution. */
enum { TRACK_LIGHT_GRID = 8 };
uint32_t PortModernTrackSample(const uint32_t colours[4], int x, int y)
{
    int weights[4] = {0, 0, 0, 0};
    int diagonal = TRACK_LIGHT_GRID - x - y;
    if (diagonal >= 0) {
        weights[0] = diagonal;
        weights[1] = x;
        weights[2] = y;
    } else {
        weights[1] = TRACK_LIGHT_GRID - y;
        weights[2] = TRACK_LIGHT_GRID - x;
        weights[3] = -diagonal;
    }
    uint32_t colour = 0;
    for (int shift = 0; shift < 24; shift += 8) {
        unsigned value = TRACK_LIGHT_GRID / 2;
        for (int i = 0; i < 4; i++) value += ((colours[i] >> shift) & 255) * weights[i];
        colour |= (value / TRACK_LIGHT_GRID) << shift;
    }
    return colour;
}

static double LightDot(const double a[3], const double b[3])
{
    return a[0] * b[0] + a[1] * b[1] + a[2] * b[2];
}

typedef struct ShipRoadLight {
    const Face *face;
    double distanceSquared;
    double weights[4];
} ShipRoadLight;

/* Port: project the pivot onto the triangle. Check its edges as well so that
   a raised ship can still sample the seam between differently banked faces. */
static void FindShipRoadTriangle(ShipRoadLight *best, const Face *face,
                                const double corners[4][3], const int indices[3])
{
    const double *a = corners[indices[0]];
    const double *b = corners[indices[1]];
    const double *c = corners[indices[2]];
    double ab[3], ac[3];
    for (int k = 0; k < 3; k++) {
        ab[k] = b[k] - a[k];
        ac[k] = c[k] - a[k];
    }
    double bb = LightDot(ab, ab), bc = LightDot(ab, ac), cc = LightDot(ac, ac);
    double determinant = bb * cc - bc * bc;
    if (determinant <= 0) return;
    double pb = -LightDot(a, ab), pc = -LightDot(a, ac);
    double v = (cc * pb - bc * pc) / determinant;
    double w = (bb * pc - bc * pb) / determinant;
    double weights[3] = {1 - v - w, v, w};
    double distanceSquared = best->distanceSquared;
    if (weights[0] >= 0 && v >= 0 && w >= 0) {
        double point[3];
        for (int k = 0; k < 3; k++) point[k] = a[k] + v * ab[k] + w * ac[k];
        distanceSquared = LightDot(point, point);
    } else {
        for (int i = 0; i < 3; i++) {
            int j = (i + 1) % 3;
            const double *start = corners[indices[i]];
            const double *end = corners[indices[j]];
            double edge[3], point[3];
            for (int k = 0; k < 3; k++) edge[k] = end[k] - start[k];
            double t = -LightDot(start, edge) / LightDot(edge, edge);
            t = t < 0 ? 0 : t > 1 ? 1 : t;
            for (int k = 0; k < 3; k++) point[k] = start[k] + t * edge[k];
            double distance = LightDot(point, point);
            if (distance < distanceSquared) {
                distanceSquared = distance;
                weights[i] = 1 - t;
                weights[j] = t;
                weights[(j + 1) % 3] = 0;
            }
        }
    }
    if (distanceSquared >= best->distanceSquared) return;
    best->face = face;
    best->distanceSquared = distanceSquared;
    for (int i = 0; i < 4; i++) best->weights[i] = 0;
    for (int i = 0; i < 3; i++) best->weights[indices[i]] = weights[i];
}

static void FindShipRoadLight(ShipRoadLight *best, const TrackSection *section, const VECTOR *pivot)
{
    if (!section || section == (TrackSection *)-1) return;
    for (int i = 0; i < section->faceCount; i++) {
        const Face *face = &track->faces[(u_short)section->faceStart + i];
        if (!(face->flags & TRACK_BASE)) continue;
        double corners[4][3];
        for (int k = 0; k < 4; k++) {
            const VECTOR *vertex = &track->vertices[face->vertex[trackLightCornerOrder[k]]];
            corners[k][0] = (double)vertex->vx - pivot->vx;
            corners[k][1] = (double)vertex->vy - pivot->vy;
            corners[k][2] = (double)vertex->vz - pivot->vz;
        }
        /* Same diagonal as SampleTrackLight, in packet corner order. */
        FindShipRoadTriangle(best, face, corners, (const int[]){0, 1, 2});
        FindShipRoadTriangle(best, face, corners, (const int[]){3, 2, 1});
    }
}

CVECTOR PortShipTrackLight(const ShipData *ship)
{
    const TrackSection *section = ship->nearTrkSect;
    if (!track || !section || section == (TrackSection *)-1) return (CVECTOR){128,128,128,0};
    if (PortRenderGeometry().smoothTrackLighting) {
        /* Keep the search on the ship's local route, away from unrelated crossings.
           As with the ship shadow, a road more than 1000 world units away is too far. */
        ShipRoadLight best = {.distanceSquared = 1000.0 * 1000.0};
        const TrackSection *nearby[] = {section, section->prevSection.ptr, section->nextSection.ptr};
        for (int i = 0; i < 3; i++) FindShipRoadLight(&best, nearby[i], &ship->ppivot);
        for (int i = 0; i < 3; i++) {
            if (nearby[i] && nearby[i] != (TrackSection *)-1) FindShipRoadLight(&best, nearby[i]->junction.ptr, &ship->ppivot);
        }
        if (best.face) {
            uint32_t colours[4];
            if (PortModernTrackCorners(best.face, colours)) {
                u_char rgb[3];
                for (int channel = 0; channel < 3; channel++) {
                    double value = 0;
                    for (int i = 0; i < 4; i++)
                        value += ((colours[i] >> (8 * channel)) & 255) * best.weights[i];
                    rgb[channel] = (u_char)(value + 0.5);
                }
                return (CVECTOR){rgb[0], rgb[1], rgb[2], PortModernLightFormat(best.face)};
            }
            return (CVECTOR){best.face->red, best.face->green, best.face->blue, PortModernLightFormat(best.face)};
        }
    }

    /* Original lane and section tint, also used when the ship is away from the road. */
    const Face *face = track->faces + section->faceStart;
    int remaining = section->faceCount;
    while (remaining > 0 && !(face->flags & TRACK_BASE)) { face++; remaining--; }
    if (!remaining) return (CVECTOR){128,128,128,0};
    if (remaining > 1 && !(ship->attr & LSIDE)) face++;
    return (CVECTOR){face->red, face->green, face->blue, PortModernLightFormat(face)};
}

void PortModernTagTrackFace(const POLY_FT4 *poly, const Face *face)
{
    if (!ModernEnabled()) return;
    uint32_t colours[4];
    PortTagTrackEncoding(poly, PortModernLightFormat(face) == RENDER_LIGHT_PS1);
    if (PortModernTrackCorners(face, colours)) PortTagTrackLight(poly, colours);
}


static struct {
    int red, green;
} shieldFlash[15];

enum { WALL_SPARK_LIFE = 7 };
static struct WallSpark {
    uint64_t endFrame;
    double x, y, z;
} wallSparks[15];

void PortWallSparkLight(const ShipData *ship, const VECTOR *pos, const Face *wall)
{
    if (!ModernEnabled() || !shipIndex || !track || ship < shipIndex || ship >= shipIndex + 15) return;
    const VECTOR *vertex = &track->vertices[wall->vertex[0]];
    double nx = wall->normal.vx / 4096.0;
    double ny = wall->normal.vy / 4096.0;
    double nz = wall->normal.vz / 4096.0;
    double distance = (pos->vx - (double)vertex->vx) * nx + (pos->vy - (double)vertex->vy) * ny +
                      (pos->vz - (double)vertex->vz) * nz;
    /* Keep the light on the racing side of the wall even when the sparks start inside it. */
    double inset = distance < 160 ? 160 - distance : 0;
    wallSparks[ship - shipIndex] = (struct WallSpark){
        PortGameFrames() + WALL_SPARK_LIFE, pos->vx + nx * inset, pos->vy + ny * inset, pos->vz + nz * inset
    };
}

void PortSetShieldFlash(int ship, int red, int green)
{
    if (ship < 0 || ship >= 15) return;
    shieldFlash[ship].red = red;
    shieldFlash[ship].green = green;
}

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
    return (GpuLightSource){{{(float)p[0], (float)p[1], (float)p[2]}, radius, {r,g,b}, intensity}, id, sourceGeneration, life};
}

void PortCaptureWeaponLights(void)
{
    if (!ModernEnabled() || !RenderCurrent()->settings.modern.weaponLights) return;
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
        if (ship->attr & SHIELDED) {
            float red = shieldFlash[i].red / 255.0f;
            float green = shieldFlash[i].green / 255.0f;
            /* Hits raise red alone or both channels. The shell's blue varies per vertex. */
            float idle = 1.0f - red;
            sources[count++] = Light(GPU_SHIELD_LIGHT_BASE + (unsigned)i, 1,
                                      ship->ppivot.vx, ship->ppivot.vy, ship->ppivot.vz,
                                      1600, idle * 0.08f + red, idle * 0.32f + green,
                                      idle + red * 0.12f, 1.8f);
        }
    }
    uint64_t frame = PortGameFrames();
    for (int i = 0; shipIndex && i < 15; i++) {
        const struct WallSpark *spark = &wallSparks[i];
        if (spark->endFrame <= frame) continue;
        int life = (int)(spark->endFrame - frame);
        /* Vary sustained contact too, without advancing the game's random sequence. */
        uint32_t noise = ((uint32_t)frame + 17u * (unsigned)i) * 0x9e3779b9u;
        float flicker = 0.3f + 0.7f * ((noise >> 16) & 255) / 255.0f;
        float fade = life / (float)WALL_SPARK_LIFE;
        sources[count++] = Light(3072u + (unsigned)i, life, spark->x, spark->y, spark->z,
                                  770, 1, 0.38f, 0.075f, 3.2f * flicker * fade * fade);
    }
    PsxCaptureLights(sources, count);
}

static int wallSpawn;
static struct { const SParticle *particle; int wall; } particleTags[PARTICLE_COUNT];
void PortModernWallBegin(void) { wallSpawn = ModernEnabled(); }
void PortModernWallEnd(void) { wallSpawn = 0; }
void PortModernParticleSpawn(const SParticle *particle)
{
    /* Every spawning routine visits the same ring slot. Reuse supersedes provenance. */
    unsigned index = (unsigned)(((uintptr_t)particle / sizeof(*particle)) % PARTICLE_COUNT);
    particleTags[index].particle = particle;
    particleTags[index].wall = wallSpawn;
}
int PortModernWallParticle(const SParticle *particle)
{
    if (!ModernEnabled()) return 0;
    unsigned index = (unsigned)(((uintptr_t)particle / sizeof(*particle)) % PARTICLE_COUNT);
    return particleTags[index].particle == particle && particleTags[index].wall && particle->life > 0;
}

static void ResetSources(void)
{
    sourceGeneration++;
    memset(shieldFlash, 0, sizeof(shieldFlash));
    memset(wallSparks, 0, sizeof(wallSparks));
    memset(particleTags, 0, sizeof(particleTags));
    wallSpawn = 0;
}

/* Port: a course lives in the heap the game resets when leaving the race. */
void PortModernHeapReset(const HeapBlock *head)
{
    if (!courseHeap || head != courseHeap) return;
    PortResetTrackLighting();
    ResetSources();
    free(sourceObjects); sourceObjects = NULL; sourceObjectCount = 0;
    courseHeap = NULL;
}

/* Port: source identity is host-owned, never packed into Object.flags/scratch. */
static int CompareObjects(const void *a, const void *b)
{
    uintptr_t x = *(const uintptr_t *)a, y = *(const uintptr_t *)b;
    return (x > y) - (x < y);
}
void PortModernSceneLoaded(Object **objects, int count, Object *sky)
{
    free(sourceObjects); sourceObjects = NULL; sourceObjectCount = 0;
    if (!PortOriginalTrackIs64() || count < 0 || count > 32767) return;
    sourceObjects = malloc(((size_t)count + 1) * sizeof(*sourceObjects));
    if (!sourceObjects) return;
    for (int i = 0; i < count; i++) if (objects[i]) sourceObjects[sourceObjectCount++] = (uintptr_t)objects[i];
    if (sky) sourceObjects[sourceObjectCount++] = (uintptr_t)sky;
    qsort(sourceObjects, sourceObjectCount, sizeof(*sourceObjects), CompareObjects);
}
int PortModernObjectRgb8(const Object *object)
{
    if (!ModernEnabled() || !sourceObjectCount) return 0;
    uintptr_t key = (uintptr_t)object;
    return bsearch(&key, sourceObjects, sourceObjectCount, sizeof(*sourceObjects), CompareObjects) != NULL;
}
