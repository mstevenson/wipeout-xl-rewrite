#include "n64_material.h"
#include <stdlib.h>
#include <stdint.h>
#include <string.h>

typedef struct N64Texture {
    int pageX, pageY, depth, clutX, clutY;
    int bounds[4];
    int flags, next;
} N64Texture;
static N64Texture *textures;
static int textureCount, textureCapacity;
#define TEXTURE_BUCKETS 1024
/* Index+1 links survive realloc. Keep registration order within each bucket. */
static int heads[TEXTURE_BUCKETS], tails[TEXTURE_BUCKETS];

static unsigned TextureBucket(int pageX, int pageY, int depth, int clutX, int clutY)
{
    uint32_t hash = (uint32_t)pageX * 73856093u ^ (uint32_t)pageY * 19349663u ^
                    (uint32_t)depth * 83492791u ^ (uint32_t)clutX * 2654435761u ^ (uint32_t)clutY * 2246822519u;
    hash ^= hash >> 16;
    return hash & (TEXTURE_BUCKETS - 1);
}

void PortN64TexturesReset(void)
{
    /* Track changes reuse VRAM and invalidate these bounds. */
    free(textures);
    textures = NULL;
    textureCount = textureCapacity = 0;
    memset(heads, 0, sizeof(heads));
    memset(tails, 0, sizeof(tails));
}

int PortN64TextureRegister(int x, int y, int wordWidth, int height, int depth, int clutX, int clutY, int flags)
{
    N64Texture *grown, *t;
    int texels;
    if (depth < 0 || depth > 2 || wordWidth <= 0 || height <= 0) return 0;
    /* Video memory is addressed in 16-bit words: each holds 4, 2 or 1 texture pixels. */
    texels = 4 >> depth;
    if (textureCount == textureCapacity) {
        int capacity = textureCapacity ? textureCapacity * 2 : 128;
        grown = realloc(textures, sizeof(*textures) * (size_t)capacity);
        if (!grown) return 0;
        textures = grown;
        textureCapacity = capacity;
    }
    t = &textures[textureCount++];
    /* Match the GPU's page origin, then express the image edges in texture pixels within it. */
    t->pageX = x & ~63;
    t->pageY = y & ~255;
    t->depth = depth;
    t->flags = flags & 0x70;
    t->clutX = clutX;
    t->clutY = clutY;
    t->bounds[0] = (x & 63) * texels;
    t->bounds[1] = y & 255;
    t->bounds[2] = t->bounds[0] + wordWidth * texels - 1;
    t->bounds[3] = t->bounds[1] + height - 1;
    t->next = 0;
    unsigned bucket = TextureBucket(t->pageX, t->pageY, depth, clutX, clutY);
    if (tails[bucket]) textures[tails[bucket] - 1].next = textureCount;
    else heads[bucket] = textureCount;
    tails[bucket] = textureCount;
    return 1;
}

int PortN64TextureInfo(int pageX, int pageY, int depth, int clutX, int clutY,
                      float u, float v, int bounds[4], int *flags)
{
    const N64Texture *wrapped = NULL;
    int matches = 0;
    /* Distinguish images sharing a page; tolerate UV rounding at their edges. */
    unsigned bucket = TextureBucket(pageX, pageY, depth, clutX, clutY);
    for (int link = heads[bucket]; link; link = textures[link - 1].next) {
        const N64Texture *t = &textures[link - 1];
        if (t->pageX == pageX && t->pageY == pageY && t->depth == depth &&
            t->clutX == clutX && t->clutY == clutY) {
            matches++;
            if (t->flags) wrapped = t;
            if (u >= t->bounds[0] - 0.01f && u <= t->bounds[2] + 0.01f &&
                v >= t->bounds[1] - 0.01f && v <= t->bounds[3] + 0.01f) {
                memcpy(bounds, t->bounds, sizeof(t->bounds));
                *flags = t->flags;
                return 1;
            }
        }
    }
    /* A masked image can deliberately address outside its bounds. Its palette identifies it
       when unique; never guess between packed images sharing both page and palette. */
    if (matches == 1 && wrapped) {
        memcpy(bounds, wrapped->bounds, sizeof(wrapped->bounds));
        *flags = wrapped->flags;
        return 1;
    }
    return 0;
}

int PortN64TextureBounds(int pageX, int pageY, int depth, int clutX, int clutY,
                        float u, float v, int bounds[4])
{
    int flags;
    return PortN64TextureInfo(pageX, pageY, depth, clutX, clutY, u, v, bounds, &flags);
}
