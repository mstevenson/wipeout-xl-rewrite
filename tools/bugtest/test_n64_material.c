/* The real N64 import path: colour scale, texture provenance and full-image bounds. */
#include "../../platform/original.c"
#include <assert.h>
int PortSaturnDisc(void) { return 0; }

int main(void)
{
    uint8_t faces[FACE_BYTES * 2], saved[sizeof(faces)];
    memset(faces, 0xab, sizeof(faces));
    faces[16] = 255; faces[17] = 128; faces[18] = 0;
    memcpy(saved, faces, sizeof(faces));
    NormalizeN64TrackFaces(faces, sizeof(faces));
    assert(faces[16] == 128 && faces[17] == 64 && faces[18] == 0);
    for (size_t i = 0; i < sizeof(faces); i++)
        if (i % FACE_BYTES < 16 || i % FACE_BYTES > 18) assert(faces[i] == saved[i]);

    /* Two PRM objects; textured flat/Gouraud, untextured Gouraud and a sprite.
       Serialized sizes/offsets are deliberately independent of the host structs. */
    uint8_t model[144 + 28 + 36 + 28 + 144 + 16] = {0}, original[sizeof(model)];
    uint8_t *ft = model + 144, *gt = ft + 28, *g = gt + 36, *second = g + 28, *spr = second + 144;
    PutBe16(model + 32, 3); PutBe16(second + 32, 1);
    PutBe16(ft, 2); PutBe16(gt, 6); PutBe16(g, 7); PutBe16(spr, 10);
    memset(ft + 24, 255, 4); memset(gt + 24, 255, 12);
    gt[28] = 128; gt[32] = 0;
    memset(g + 12, 255, 16); memset(spr + 12, 255, 4);
    memcpy(original, model, sizeof(model));
    assert(PortPrmShiftTextures((char *)model, sizeof(model), 0));
    assert(!memcmp(model, original, sizeof(model))); /* the existing PS1 conversion */
    assert(PortPrmNormalizeN64((char *)model, sizeof(model)));
    assert(ft[24] == 128 && ft[25] == 128 && ft[26] == 128 && ft[27] == 255);
    assert(gt[24] == 128 && gt[28] == 64 && gt[32] == 0);
    assert(gt[27] == 255 && gt[31] == 255 && gt[35] == 255);
    assert(!memcmp(g + 12, original + (g - model) + 12, 16));
    assert(spr[12] == 128 && spr[15] == 255);
    assert(!PortPrmNormalizeN64((char *)original, sizeof(original) - 1));

    /* A 32x32 4-bit image becomes 21 TIMs, all still marked as N64, while
       filtering uses its full 32x32 footprint even for a tiny near-road piece. */
    TimList tiles = {0}, pieces = {0}; Layout layout = {0};
    uint8_t *tim = SDL_calloc(1, 64 + 8 * 32 * 2), *cmp, *library, *templates;
    size_t size, librarySize, templatesSize;
    int bounds[4];
    PutLe32(tim, 16); PutLe32(tim + 4, 0xf8); /* original flags must be replaced */
    PutLe32(tim + 8, 44); PutLe16(tim + 16, 16); PutLe16(tim + 18, 1);
    PutLe32(tim + 52, 12 + 8 * 32 * 2); PutLe16(tim + 60, 8); PutLe16(tim + 62, 32);
    TimAdd(&tiles, tim, 64 + 8 * 32 * 2); cmp = Pack(&tiles, &size);
    assert(MakeLibrary(cmp, size, &layout, &library, &librarySize, &templates, &templatesSize));
    assert(Unpack(library, librarySize, &pieces) && pieces.count == 21);
    for (int i = 0; i < pieces.count; i++) assert(Le32(pieces.tims[i] + 4) == (PORT_TIM_N64 | 8));
    assert(PortN64TextureBounds(0, 512, 0, 768, 768, 7, 7, bounds));
    assert(bounds[0] == 0 && bounds[1] == 0 && bounds[2] == 31 && bounds[3] == 31);
    assert(!PortN64TextureBounds(0, 0, 0, 768, 768, 7, 7, bounds)); /* original PS1 page */
    assert(!PortN64TextureBounds(0, 512, 0, 784, 768, 7, 7, bounds)); /* different palette */
    assert(!PortN64TextureBounds(0, 512, 0, 768, 768, 32, 7, bounds)); /* neighbor */
    PortN64TexturesReset();
    assert(!PortN64TextureBounds(0, 512, 0, 768, 768, 7, 7, bounds));
    SDL_free(cmp); SDL_free(library); SDL_free(templates);
    TimFree(&tiles); TimFree(&pieces); LayoutFree(&layout);
    puts("N64 import normalizes only textured colours, tags every road window and retains full image bounds: OK");
    return 0;
}
