#include "n64_material.h"
#include <stdlib.h>
#include <string.h>

typedef struct N64Texture {
    int pageX, pageY, depth, clutX, clutY;
    int bounds[4];
} N64Texture;
static N64Texture *textures;
static int textureCount;

void PortN64TexturesReset(void)
{
    /* Track changes reuse VRAM and invalidate these bounds. */
    free(textures);
    textures = NULL;
    textureCount = 0;
}

int PortN64TextureRegister(int x, int y, int wordWidth, int height, int depth, int clutX, int clutY)
{
    N64Texture *grown, *t;
    int texels;
    if (depth < 0 || depth > 2 || wordWidth <= 0 || height <= 0) return 0;
    /* Video memory is addressed in 16-bit words: each holds 4, 2 or 1 texture pixels. */
    texels = 4 >> depth;
    grown = realloc(textures, sizeof(*textures) * (size_t)(textureCount + 1));
    if (!grown) return 0;
    textures = grown;
    t = &textures[textureCount++];
    /* Match the GPU's page origin, then express the image edges in texture pixels within it. */
    t->pageX = x & ~63;
    t->pageY = y & ~255;
    t->depth = depth;
    t->clutX = clutX;
    t->clutY = clutY;
    t->bounds[0] = (x & 63) * texels;
    t->bounds[1] = y & 255;
    t->bounds[2] = t->bounds[0] + wordWidth * texels - 1;
    t->bounds[3] = t->bounds[1] + height - 1;
    return 1;
}

int PortN64TextureBounds(int pageX, int pageY, int depth, int clutX, int clutY,
                        float u, float v, int bounds[4])
{
    /* Distinguish images sharing a page; tolerate UV rounding at their edges. */
    for (int i = 0; i < textureCount; i++) {
        const N64Texture *t = &textures[i];
        if (t->pageX == pageX && t->pageY == pageY && t->depth == depth &&
            t->clutX == clutX && t->clutY == clutY &&
            u >= t->bounds[0] - 0.01f && u <= t->bounds[2] + 0.01f &&
            v >= t->bounds[1] - 0.01f && v <= t->bounds[3] + 0.01f) {
            memcpy(bounds, t->bounds, sizeof(t->bounds));
            return 1;
        }
    }
    return 0;
}
