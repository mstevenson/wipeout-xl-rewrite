/* The real N64 import path: colour scale, texture provenance and full-image bounds. */
#include "../../platform/original.c"
#include <assert.h>
int PortSaturnDisc(void) { return 0; }

int main(void)
{
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
    assert(PortPrmMarkN64((char *)model, sizeof(model)));
    assert(!memcmp(ft, original + (ft - model), 28 + 36 + 28));
    assert(!memcmp(spr, original + (spr - model), 16));
    assert(Be16(model + 56) == PORT_OBJECT_N64 && Be16(second + 56) == PORT_OBJECT_N64);
    assert(PortPrmMarkN64((char *)model, sizeof(model))); /* marking is idempotent */
    assert(!PortPrmMarkN64((char *)original, sizeof(original) - 1));

    /* A 32x32 4-bit image becomes 21 TIMs, all still marked as N64, while
       filtering uses its full 32x32 footprint even for a tiny near-road piece. */
    TimList tiles = {0}, pieces = {0}; Layout layout = {0};
    uint8_t *tim = SDL_calloc(1, 64 + 8 * 32 * 2), *cmp, *library, *templates;
    size_t size, librarySize, templatesSize;
    int bounds[4], flags;
    PutLe32(tim, 16); PutLe32(tim + 4, 0xf8); /* original flags must be replaced */
    PutLe32(tim + 8, 44); PutLe16(tim + 16, 16); PutLe16(tim + 18, 1);
    PutLe32(tim + 52, 12 + 8 * 32 * 2); PutLe16(tim + 60, 8); PutLe16(tim + 62, 32);
    TimAdd(&tiles, tim, 64 + 8 * 32 * 2); cmp = Pack(&tiles, &size);
    assert(MakeLibrary(cmp, size, &layout, &library, &librarySize, &templates, &templatesSize));
    assert(Unpack(library, librarySize, &pieces) && pieces.count == 21);
    for (int i = 0; i < pieces.count; i++) assert(Le32(pieces.tims[i] + 4) == (PORT_TIM_N64 | 8));
    assert(PortN64TextureBounds(0, 512, 0, 768, 768, 7, 7, bounds));
    assert(PortN64TextureInfo(0, 512, 0, 768, 768, 7, 7, bounds, &flags) && flags == 0x70);
    assert(bounds[0] == 0 && bounds[1] == 0 && bounds[2] == 31 && bounds[3] == 31);
    assert(!PortN64TextureBounds(0, 0, 0, 768, 768, 7, 7, bounds)); /* original PS1 page */
    assert(!PortN64TextureBounds(0, 512, 0, 784, 768, 7, 7, bounds)); /* different palette */
    assert(PortN64TextureBounds(0, 512, 0, 768, 768, 32, 7, bounds)); /* masked UV beyond edge */
    assert(PortN64TextureRegister(8, 512, 8, 32, 0, 768, 768, 0x40));
    assert(!PortN64TextureBounds(0, 512, 0, 768, 768, 80, 7, bounds)); /* ambiguous palette */
    PortN64TexturesReset();
    assert(!PortN64TextureBounds(0, 512, 0, 768, 768, 7, 7, bounds));
    /* Exercise index growth, collisions, palette separation and reset/re-registration. */
    for (int i = 0; i < 2500; i++)
        assert(PortN64TextureRegister((i % 16) * 64, (i % 4) * 256, 2, 8, i % 3,
                                      (i % 64) * 16, i / 64, 0x40));
    for (int i = 2499; i >= 0; i--) {
        assert(PortN64TextureInfo((i % 16) * 64, (i % 4) * 256, i % 3,
                                 (i % 64) * 16, i / 64, 0, 0, bounds, &flags));
        assert(bounds[2] == 2 * (4 >> (i % 3)) - 1 && bounds[3] == 7 && flags == 0x40);
    }
    PortN64TexturesReset();
    assert(!PortN64TextureInfo(0, 0, 0, 0, 0, 0, 0, bounds, &flags));
    assert(PortN64TextureRegister(0, 0, 2, 8, 0, 0, 0, 0));
    assert(PortN64TextureRegister(0, 0, 4, 16, 0, 0, 0, 0x40));
    assert(PortN64TextureInfo(0, 0, 0, 0, 0, 0, 0, bounds, &flags) && flags == 0 && bounds[2] == 7);
    assert(!PortN64TextureInfo(0, 0, 0, 0, 0, 100, 100, bounds, &flags));
    PortN64TexturesReset();
    SDL_free(cmp); SDL_free(library); SDL_free(templates);
    TimFree(&tiles); TimFree(&pieces); LayoutFree(&layout);
    puts("N64 import preserves all colour bytes and addressing flags, tags every road window and retains full image bounds: OK");
    return 0;
}
