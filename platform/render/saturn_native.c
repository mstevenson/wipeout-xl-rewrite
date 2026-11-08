#include "saturn_native.h"
#include <math.h>
#include <stdlib.h>
#include <string.h>

/* The data registry owns copies: converter buffers can be freed immediately. */
typedef struct Model {
    SaturnNativePoly *polys;
    int count, frameCount; /* additional materials for primitive zero */
    int32_t radius;
    int haveRadius, billboard;
    uint32_t billboardParts;
} Model;
static Model *models;
static uint32_t modelCount, modelCapacity;
/* Synthetic PRMs use the top 12 bits as a signature, followed by a frame and model ID. */
#define MODEL_TAG_BASE UINT32_C(0x53400000)
#define MODEL_FRAME_SHIFT 16
#define MODEL_FRAME_MASK UINT32_C(0x000f0000)
static Model trackModel;
static uint32_t raceTicks;
static uint16_t weaponPalettes[SATURN_WEAPON_PAD_PALETTES][16];
static int haveWeaponPalettes;
static uint64_t materialRevision=1;
uint64_t SaturnNativeMaterialRevision(void) { return materialRevision; }
static uint8_t *sky;
static uint64_t skyRevision;
uint64_t SaturnNativeSkyRevision(void) { return skyRevision; }
static uint16_t skyPalette[256];
static int skyWidth, skyHeight;
#define TAGS 262144
static struct { uintptr_t address; const SaturnNativePoly *poly; } tags[TAGS];

static void FreePolys(Model *m)
{
    for (int i = 0; i < m->count + m->frameCount; i++) {
        free(m->polys[i].pixels);
    }
    free(m->polys);
}

static void CopyPolys(Model *m, const SaturnNativePoly *polys, int count,
                      const SaturnNativePoly *frames, int frameCount)
{
    int total = count + frameCount;
    m->count = count;
    m->frameCount = frameCount;
    m->polys = total ? calloc((size_t)total, sizeof(*polys)) : NULL;
    if (total && !m->polys) abort();
    for (int i = 0; i < total; i++) {
        const SaturnNativePoly *source = i < count ? &polys[i] : &frames[i - count];
        SaturnNativePoly *dest = &m->polys[i];
        *dest = *source;
        if (i >= count) memcpy(dest->corners, polys[0].corners, sizeof(dest->corners));
        dest->pixels = NULL;
        if (source->pixels && source->width > 0 && source->height > 0) {
            size_t size = (size_t)source->width * source->height;
            dest->pixels = malloc(size);
            if (!dest->pixels) abort();
            memcpy(dest->pixels, source->pixels, size);
        }
    }
}

uint32_t SaturnNativeModel(const SaturnNativePoly *polys, int count,
                           const SaturnNativePoly *frames, int frameCount)
{
    if (modelCount == UINT16_MAX || frameCount >= SATURN_MODEL_FRAMES) abort();
    if (modelCount == modelCapacity) {
        modelCapacity = modelCapacity ? modelCapacity * 2 : 64;
        Model *grown = realloc(models, modelCapacity * sizeof(*models));
        if (!grown) abort();
        models = grown;
    }
    memset(&models[modelCount], 0, sizeof(*models));
    CopyPolys(&models[modelCount++], polys, count, frames, frameCount);
    return MODEL_TAG_BASE + modelCount;
}

void SaturnNativeTrack(const SaturnNativePoly *polys, int count, const uint16_t *palettes)
{
    materialRevision++;
    /* Old packets can refer to faces from the previous race. */
    memset(tags, 0, sizeof(tags));
    FreePolys(&trackModel);
    CopyPolys(&trackModel, polys, count, NULL, 0);
    raceTicks = 0;
    for (int i = 0; i < count; i++) trackModel.polys[i].trackTile = 1;
    haveWeaponPalettes = palettes != NULL;
    if (palettes) memcpy(weaponPalettes, palettes, sizeof(weaponPalettes));
}

void PortRaceMaterialTick(void)
{
    if (trackModel.count) raceTicks++;
}

/* The 16.16 sine table at 0605539c gives these same quantized channel levels. */
static int Pulse(int angle, int shift)
{
    return (int)lround(65536.0 * sin((angle & 4095) * (6.283185307179586 / 4096.0))) >> shift;
}

void SaturnNativeAnimatePalette(SaturnNativePoly *poly)
{
    if (!raceTicks) return;
    uint16_t *palette = poly->palette;
    switch (poly->animation) {
    case SATURN_ANIM_NEON:
        /* 0603d574 builds the dim palette by subtracting eight per channel.
           0603d808 alternates between it and the original every six ticks. */
        if ((raceTicks / 6) & 1) {
            for (int i = 0; i < 16; i++) {
                unsigned word = 0x8000;
                for (int shift = 0; shift < 15; shift += 5) {
                    int channel = (palette[i] >> shift) & 31;
                    if (channel > 8) word |= (unsigned)(channel - 8) << shift;
                }
                palette[i] = (uint16_t)word;
            }
        }
        break;
    case SATURN_ANIM_WATERFALL: {
        /* 0603d4e8 rotates only the moving-water ramp, entries 4 through 15. */
        uint16_t ramp[12];
        memcpy(ramp, palette + 4, sizeof(ramp));
        unsigned offset = raceTicks % 12;
        for (unsigned i = 0; i < 12; i++) palette[4 + i] = ramp[(i + 12 - offset) % 12];
        break;
    }
    case SATURN_ANIM_RESCUE_RED:
    case SATURN_ANIM_RESCUE_RGB: {
        /* 0603da04 rewrites two light ramps, with phase offsets and a 140-unit step. */
        int angle = (uint16_t)((raceTicks - 1) * 140u);
        int rgb = poly->animation == SATURN_ANIM_RESCUE_RGB;
        int r = 23 + Pulse(angle + (rgb ? 0 : 100), 13);
        int g = rgb ? 23 + Pulse(angle + 75, 13) : 0;
        int b = rgb ? 16 + Pulse((angle >> 1) + 32, 12) : 0;
        if (b > 31) b = 31;
        for (int i = rgb ? 15 : 14; i >= (rgb ? 11 : 2); i--) {
            palette[i] = (uint16_t)(0x8000 | r | g << 5 | b << 10);
            if (r > 1) r -= 2;
            if (g > 1) g -= 2;
            if (b > 1) b -= 2;
        }
        break;
    }
    default:
        break;
    }
}

void PortWeaponGridVisual(int leftFace, int rightFace, int angle, int depleted)
{
    if (!haveWeaponPalettes) return;
    /* 0.AAA 06032e4e..06032ed4 selects one of 64 phases or the next LUT on pickup. */
    unsigned index = depleted ? SATURN_WEAPON_PAD_PHASES : ((unsigned)angle & 4095) / 64;
    const int faces[] = {leftFace, rightFace};
    for (int i = 0; i < 2; i++) {
        int face = faces[i];
        if (face >= 0 && face < trackModel.count) {
            memcpy(trackModel.polys[face].palette, weaponPalettes[index], sizeof(weaponPalettes[index]));
        }
    }
}

static size_t TagSlot(uintptr_t address)
{
    /* Primitive arenas contain long runs of nearby addresses. Folding low bits
       alone clusters both live tags and misses, making packet lookup dominate a
       race. Mix the entire address before linear probing. */
    uint64_t hash=(uint64_t)(address>>2);
    hash=(hash^(hash>>30))*UINT64_C(0xbf58476d1ce4e5b9);
    hash=(hash^(hash>>27))*UINT64_C(0x94d049bb133111eb);
    size_t slot=(size_t)(hash^(hash>>31))&(TAGS-1);
    for (int probe = 0; probe < TAGS; probe++) {
        if (!tags[slot].address || tags[slot].address == address) return slot;
        slot = (slot+1) & (TAGS-1);
    }
    return TAGS;
}

static void Bind(const void *start, const void *end, const SaturnNativePoly *poly)
{
    if (start == end) return;
    /* Bind payload addresses, not every word in the primitive pool. Linear
       probing keeps simultaneous packets whose addresses hash alike distinct. */
    const uint32_t *packet = start, *limit = end;
    while (packet < limit) {
        unsigned words = (*packet >> 24) + 1;
        if (words <= 1 || words > (size_t)(limit-packet)) break;
        uintptr_t address = (uintptr_t)(packet+1);
        size_t slot = TagSlot(address);
        if (slot < TAGS) {
            tags[slot].address = address;
            tags[slot].poly = poly;
        }
        packet += words;
    }
}

static const Model *ModelForTag(uint32_t modelTag)
{
    uint32_t id = (modelTag & ~MODEL_FRAME_MASK) - MODEL_TAG_BASE;
    return id && id <= modelCount ? &models[id - 1] : NULL;
}

void SaturnNativeSetRadius(uint32_t tag, int32_t radius)
{
    Model *model = (Model *)ModelForTag(tag);
    if (model) { model->radius = radius; model->haveRadius = 1; }
}

int SaturnNativeRadius(uint32_t tag, int32_t *radius)
{
    const Model *model = ModelForTag(tag);
    if (!model || !model->haveRadius) return 0;
    *radius = model->radius;
    return 1;
}

void SaturnNativeSetBillboard(uint32_t tag)
{
    Model *model = (Model *)ModelForTag(tag);
    if (model) model->billboard = 1;
}

void SaturnNativeSetBillboardParts(uint32_t tag, uint32_t parts)
{
    Model *model = (Model *)ModelForTag(tag);
    if (model) model->billboardParts = parts;
}

uint32_t SaturnNativeBillboardParts(uint32_t tag, int *count)
{
    const Model *model = ModelForTag(tag);
    const Model *parts = model ? ModelForTag(model->billboardParts) : NULL;
    *count = parts ? parts->count : 0;
    return parts ? model->billboardParts : 0;
}

int SaturnNativeBillboard(uint32_t tag)
{
    const Model *model = ModelForTag(tag);
    return model && model->billboard;
}

const SaturnNativePoly *SaturnNativeModelPoly(uint32_t tag, int index)
{
    const Model *model = ModelForTag(tag);
    return model && index >= 0 && index < model->count ? &model->polys[index] : NULL;
}

uint32_t SaturnNativeFrameTag(uint32_t modelTag, unsigned frame)
{
    const Model *model = ModelForTag(modelTag);
    if (!model || frame > (unsigned)model->frameCount) return modelTag;
    return (modelTag & ~MODEL_FRAME_MASK) | (frame << MODEL_FRAME_SHIFT);
}

void SaturnNativeBind(const void *start, const void *end, uint32_t modelTag, int index)
{
    const Model *model = ModelForTag(modelTag);
    if (!model || index < 0 || index >= model->count) return;
    unsigned frame = (modelTag & MODEL_FRAME_MASK) >> MODEL_FRAME_SHIFT;
    if (index == 0 && model->frameCount && raceTicks) {
        SaturnMaterialAnimation animation = model->polys[0].animation;
        if (animation == SATURN_ANIM_SMOKE_SLOW || animation == SATURN_ANIM_SMOKE_FAST) {
            unsigned step = animation == SATURN_ANIM_SMOKE_FAST ? 2 : 1;
            frame = ((raceTicks - 1) * step) % (unsigned)(model->frameCount + 1);
        }
    }
    if (index == 0 && frame && frame <= (unsigned)model->frameCount) {
        index = model->count + (int)frame - 1;
    }
    Bind(start, end, &model->polys[index]);
}

void SaturnNativeBindTrack(const void *start, const void *end, int index)
{
    if (index >= 0 && index < trackModel.count) Bind(start, end, &trackModel.polys[index]);
}

void SaturnNativeForget(const void *packet)
{
    if (!models && !trackModel.count) return;
    size_t slot = TagSlot((uintptr_t)packet + 4);
    if (slot < TAGS) tags[slot].poly = NULL;
}

const SaturnNativePoly *SaturnNativeLookup(const void *packet)
{
    if (!models && !trackModel.count) return NULL;
    size_t slot = TagSlot((uintptr_t)packet);
    return slot < TAGS && tags[slot].address ? tags[slot].poly : NULL;
}

void SaturnNativeSky(const uint8_t *pixels, const uint16_t *palette, int width, int height)
{
    skyRevision++;
    free(sky);
    sky = NULL;
    skyWidth = skyHeight = 0;
    if (!pixels || width <= 0 || height <= 0) return;
    sky = malloc((size_t)width * height);
    if (!sky) abort();
    memcpy(sky, pixels, (size_t)width * height);
    memcpy(skyPalette, palette, sizeof(skyPalette));
    skyWidth = width; skyHeight = height;
}

const uint8_t *SaturnNativeSkyData(const uint16_t **palette, int *width, int *height)
{
    *palette = skyPalette; *width = skyWidth; *height = skyHeight;
    return sky;
}

/* The ship shade table in the European executable, 0603f2f8..0603f32a:
   neutral, the road's colour, that colour minus four, and RGB(12,12,12). */
void SaturnShipLight(uint16_t track, uint16_t shade[4])
{
    unsigned dim = 0x8000;
    for (int c = 0; c < 3; c++) {
        int level = (track >> (5*c)) & 31;
        dim |= (unsigned)(level > 4 ? level-4 : 0) << (5*c);
    }
    shade[0] = 0xc210; shade[1] = track;
    shade[2] = (uint16_t)dim; shade[3] = 0xb18c;
}

/* Inverse of the compatibility road colour encoding in saturn.c. */
uint16_t SaturnTrackLight(uint8_t r, uint8_t g, uint8_t b)
{
    uint8_t rgb[3] = {r,g,b};
    unsigned result = 0x8000;
    for (int c = 0; c < 3; c++) {
        unsigned level = rgb[c] <= 128 ? rgb[c]/8 : 16+(rgb[c]-128)/16;
        result |= level << (5*c);
    }
    return (uint16_t)result;
}

uint16_t SaturnGouraud(uint16_t colour, const float shade[3])
{
    unsigned result = 0x8000;
    for (int c = 0; c < 3; c++) {
        int value = (int)((colour >> (5 * c)) & 31) + (int)floorf(fmaxf(-32, fminf(64, shade[c]))) - 16;
        if (value < 0) value = 0;
        if (value > 31) value = 31;
        result |= (unsigned)value << (5 * c);
    }
    return (uint16_t)result;
}

/* Preserve native model attributes for host-created clipping intersections. */
void SaturnNativeAttributes(const SaturnNativePoly *p, GpuVertex *v)
{
    for (int k=0;k<4;k++) {
        int n=p->corners[k];
        for (int c=0;c<3;c++) v[n].saturnShade[c]=(p->gouraud[k]>>(5*c))&31;
        if (p->pixels) {
            v[n].u=(k==1 || k==2) ? p->width-0.001f : 0;
            v[n].v=k>=2 ? p->height-0.001f : 0;
        }
    }
}

static int Max(int a, int b) { return a > b ? a : b; }
static int Min(int a, int b) { return a < b ? a : b; }
static float ReciprocalDepth(float depth) { return depth > 0 ? 1.0f / depth : 0; }

static void WritePixel(const SaturnNativePoly *p, int x, int y, int left, int top, int right, int bottom,
                       uint16_t colour, float inverseDepth, uint8_t alpha, SaturnPixel emit, void *user)
{
    if (x < left || x >= right || y < top || y >= bottom) return;
    if (p->alphaMesh) alpha = (uint8_t)((alpha + 1) / 2);
    else if ((p->mode & 0x100) && ((x+y)&1)) return;
    int operation = p->mode & 3;
    /* -2 requests a destination shadow (half the destination colour). */
    emit(user, x, y, colour, operation == 1 ? -2 : operation == 3 ? 0 : -1, ReciprocalDepth(inverseDepth), alpha);
}

static int OwnsEdge(const GpuVertex *a, const GpuVertex *b, double det)
{
    if (a->y == b->y) return det > 0 ? a->x < b->x : b->x < a->x;
    return det > 0 ? a->y > b->y : b->y > a->y;
}

/* Rasterize only the visible bounding box of a host triangle. */
static void ClippedTriangle(const SaturnNativePoly *p, const GpuVertex *v,
    int left,int top,int right,int bottom,SaturnPixel emit,SaturnIndex sample,void *user)
{
    const GpuVertex *a=&v[0],*b=&v[1],*c=&v[2];
    double det=(b->x-a->x)*(double)(c->y-a->y)-(b->y-a->y)*(double)(c->x-a->x);
    if(!isfinite(det) || fabs(det)<0.00001) return;
    double minX=fmin(a->x,fmin(b->x,c->x)),maxX=fmax(a->x,fmax(b->x,c->x));
    double minY=fmin(a->y,fmin(b->y,c->y)),maxY=fmax(a->y,fmax(b->y,c->y));
    if(maxX<left || minX>=right || maxY<top || minY>=bottom) return;
    const float za=ReciprocalDepth(a->w), zb=ReciprocalDepth(b->w), zc=ReciprocalDepth(c->w);
    const int edgeA=OwnsEdge(b,c,det), edgeB=OwnsEdge(c,a,det), edgeC=OwnsEdge(a,b,det);
    left=Max(left,(int)fmax(left,floor(minX)));right=Min(right,(int)fmin(right,ceil(maxX)+1));
    top=Max(top,(int)fmax(top,floor(minY)));bottom=Min(bottom,(int)fmin(bottom,ceil(maxY)+1));
    for(int y=top;y<bottom;y++) for(int x=left;x<right;x++) {
        double px=x-a->x,py=y-a->y;
        if (p->trackTile) { px+=.5;py+=.5; }
        double u=(px*(double)(c->y-a->y)-py*(double)(c->x-a->x))/det;
        double w=((b->x-a->x)*py-(b->y-a->y)*px)/det;
        double t=1-u-w;
        if(u< -0.000001 || w< -0.000001 || t< -0.000001) continue;
        /* Adjacent track triangles share a diagonal. Only one may blend its pixels. */
        if (p->trackTile && ((!edgeA && t <= 0.000001) || (!edgeB && u <= 0.000001) ||
                             (!edgeC && w <= 0.000001))) continue;
        double inverseDepth=za*t+zb*u+zc*w;
        if (p->trackTile && inverseDepth > 0) {
            t=t*za/inverseDepth;u=u*zb/inverseDepth;w=w*zc/inverseDepth;
        }
        uint16_t colour=p->colour;
        if(p->textured) {
            float tx=(float)(a->u*t+b->u*u+c->u*w),ty=(float)(a->v*t+b->v*u+c->v*w);
            int index;
            if(p->pixels) {
                int ix=(int)fmax(0,fmin(p->width-1,floor(tx+0.00001f)));
                int iy=(int)fmax(0,fmin(p->height-1,floor(ty+0.00001f)));
                index=p->pixels[iy*p->width+ix];
            } else { if(!sample) return; index=sample(user,tx,ty); }
            if((!index && !(p->mode&0x40)) || (index==15 && !(p->mode&0x80))) continue;
            colour=p->palette[index&15];
        }
        if(p->mode&4) {
            float shade[3];
            for(int k=0;k<3;k++) shade[k]=(float)(a->saturnShade[k]*t+b->saturnShade[k]*u+c->saturnShade[k]*w+0.00001);
            colour=SaturnGouraud(colour,shade);
        }
        if((p->mode&3)==2) colour=(uint16_t)(0x8000|((colour&0x7bde)>>1));
        WritePixel(p,x,y,left,top,right,bottom,colour,(float)inverseDepth,
            (uint8_t)(a->a*t+b->a*u+c->a*w+.5),emit,user);
    }
}

/* Forward mapping: step A-D and B-C, drawing the line joining the two edges.
   Diagonal steps include the connecting pixel: omitting it leaves cracks in
   rotated sprites. Repeated writes are intentional, including at collapsed edges.
   Source texels span the whole line, with no triangle diagonal or resampled atlas.
   Attribute UVs use a 1e-5 texel tolerance at integer boundaries, shared with the
   GPU, so arithmetic contraction/reciprocal rounding does not select a neighbour. */
void SaturnVdp1Draw(const SaturnNativePoly *p, const GpuVertex *v,
                   int left, int top, int right, int bottom, SaturnPixel emit, SaturnIndex sample, void *user)
{
    if (left >= right || top >= bottom) return;
    if (p->clipped) { ClippedTriangle(p,v,left,top,right,bottom,emit,sample,user); return; }
    GpuVertex q[4];
    float inverseDepth[4];
    for (int k = 0; k < 4; k++) {
        if (p->corners[k] < 0 || p->corners[k] > 3) return;
        q[k] = v[p->corners[k]];
        if (p->trackTile) continue;
        inverseDepth[k] = ReciprocalDepth(q[k].w);
        q[k].x = floorf(q[k].x); q[k].y = floorf(q[k].y);
        if (!isfinite(q[k].x) || !isfinite(q[k].y) || fabsf(q[k].x) > 32768 || fabsf(q[k].y) > 32768) return;
    }
    if (p->trackTile) {
        /* Convert A B C D back to the packet's strip ordering. */
        GpuVertex tmp=q[2];q[2]=q[3];q[3]=tmp;
        ClippedTriangle(p,q,left,top,right,bottom,emit,sample,user);
        ClippedTriangle(p,q+1,left,top,right,bottom,emit,sample,user);
        return;
    }
    int edge = Max(Max(abs((int)(q[3].x-q[0].x)), abs((int)(q[3].y-q[0].y))),
                   Max(abs((int)(q[2].x-q[1].x)), abs((int)(q[2].y-q[1].y))));
    for (int row = 0; row <= edge; row++) {
        float t = edge ? (float)row / edge : 0;
        float za = inverseDepth[0]*(1-t) + inverseDepth[3]*t;
        float zb = inverseDepth[1]*(1-t) + inverseDepth[2]*t;
        int ax = (int)floorf(q[0].x + (q[3].x-q[0].x)*t + .5f);
        int ay = (int)floorf(q[0].y + (q[3].y-q[0].y)*t + .5f);
        int bx = (int)floorf(q[1].x + (q[2].x-q[1].x)*t + .5f);
        int by = (int)floorf(q[1].y + (q[2].y-q[1].y)*t + .5f);
        if (Max(ax,bx) < left || Min(ax,bx) >= right || Max(ay,by) < top || Min(ay,by) >= bottom) continue;
        int dx = abs(bx-ax), dy = abs(by-ay), sx = ax < bx ? 1 : -1, sy = ay < by ? 1 : -1;
        int steps = Max(dx,dy), first = 0, last = steps;
        /* Clip work on the major axis, without changing the texture or shade fractions. */
        if (dx >= dy) {
            first = Max(0, sx > 0 ? left-ax-1 : ax-right);
            last = Min(steps, sx > 0 ? right-ax : ax-left+1);
        } else {
            first = Max(0, sy > 0 ? top-ay-1 : ay-bottom);
            last = Min(steps, sy > 0 ? bottom-ay : ay-top+1);
        }
        int endCodes = 0, lastTexel = -1;
        /* End-code detection precedes clipping, so do not skip source reads. */
        if (p->textured && !(p->mode & 0x80)) first = 0;
        for (int col = first; col <= last; col++) {
            float s = steps ? (float)col / steps : 0;
            int x = ax + sx * (dx >= dy ? col : (int)(((int64_t)col*dx + dy/2)/dy));
            int y = ay + sy * (dy > dx ? col : dx ? (int)(((int64_t)col*dy + dx/2)/dx) : 0);
            uint16_t colour = p->colour;
            if (p->textured) {
                int index;
                if (p->pixels && p->width > 0 && p->height > 0) {
                    int u = (int)((int64_t)col*p->width/(steps+1));
                    int w = (int)((int64_t)row*p->height/(edge+1));
                    if (p->vertexAttributes) {
                        u=(int)floorf((q[0].u*(1-s)+q[1].u*s)*(1-t)+(q[3].u*(1-s)+q[2].u*s)*t+0.00001f);
                        w=(int)floorf((q[0].v*(1-s)+q[1].v*s)*(1-t)+(q[3].v*(1-s)+q[2].v*s)*t+0.00001f);
                        u=Max(0,Min(p->width-1,u)); w=Max(0,Min(p->height-1,w));
                    }
                    index = p->pixels[w*p->width+u];
                    if (!(p->mode & 0x80) && index == 15 && u != lastTexel && ++endCodes == 2) break;
                    lastTexel = u;
                } else {
                    float u = (q[0].u*(1-s)+q[1].u*s)*(1-t)+(q[3].u*(1-s)+q[2].u*s)*t;
                    float w = (q[0].v*(1-s)+q[1].v*s)*(1-t)+(q[3].v*(1-s)+q[2].v*s)*t;
                    if (!sample) return;
                    index = sample(user, u, w);
                    int texel = (int)floorf(u+0.00001f) + 256*(int)floorf(w+0.00001f);
                    if (!(p->mode & 0x80) && index == 15 && texel != lastTexel && ++endCodes == 2) break;
                    lastTexel = texel;
                }
                if ((!index && !(p->mode & 0x40)) || (index == 15 && !(p->mode & 0x80))) continue;
                colour = p->palette[index & 15];
            }
            if (p->mode & 4) {
                float shade[3];
                for (int c = 0; c < 3; c++) {
                    if (p->vertexAttributes) {
                        shade[c]=(q[0].saturnShade[c]*(1-s)+q[1].saturnShade[c]*s)*(1-t)+
                                 (q[3].saturnShade[c]*(1-s)+q[2].saturnShade[c]*s)*t + 0.00001f;
                    } else {
                        int64_t a = (p->gouraud[0] >> (5*c)) & 31;
                        int64_t b = (p->gouraud[1] >> (5*c)) & 31;
                        int64_t d = (p->gouraud[3] >> (5*c)) & 31;
                        int64_t e = (p->gouraud[2] >> (5*c)) & 31;
                        int horizontal = steps ? steps : 1, vertical = edge ? edge : 1;
                        shade[c] = (float)(((a*(horizontal-col)+b*col)*(vertical-row) +
                            (d*(horizontal-col)+e*col)*row) / ((int64_t)horizontal*vertical));
                    }
                }
                colour = SaturnGouraud(colour, shade);
            }
            if ((p->mode & 3) == 2) colour = (uint16_t)(0x8000 | ((colour & 0x7bde) >> 1));
            float depth = za*(1-s) + zb*s;
            uint8_t alpha = (uint8_t)((q[0].a*(1-s)+q[1].a*s)*(1-t)+(q[3].a*(1-s)+q[2].a*s)*t + .5f);
            WritePixel(p,x,y,left,top,right,bottom,colour,depth,alpha,emit,user);
            if (col < steps) {
                int nx = ax + sx*(dx >= dy ? col+1 : (int)(((int64_t)(col+1)*dx + dy/2)/dy));
                int ny = ay + sy*(dy > dx ? col+1 : dx ? (int)(((int64_t)(col+1)*dy + dx/2)/dx) : 0);
                if (x != nx && y != ny) WritePixel(p,nx,y,left,top,right,bottom,colour,depth,alpha,emit,user);
            }
        }
    }
}

void SaturnNativeShutdown(void)
{
    raceTicks = 0;
    materialRevision++;
    for (uint32_t i = 0; i < modelCount; i++) FreePolys(&models[i]);
    free(models);
    models = NULL;
    modelCount = modelCapacity = 0;
    FreePolys(&trackModel);
    trackModel = (Model){0};
    haveWeaponPalettes = 0;
    free(sky); sky = NULL; skyWidth = skyHeight = 0;
    memset(tags, 0, sizeof(tags));
}
