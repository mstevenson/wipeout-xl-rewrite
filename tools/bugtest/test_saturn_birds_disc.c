/* Optional: compare every converted bird pixel with the player's Saturn disc. */
#include "saturn.h"
#include "psx/disc.h"
#include <SDL3/SDL.h>
#include <assert.h>
#include <stdio.h>
#include <string.h>

static Disc *disc;
char *cdTrackNames[13], *cdTrackArtists[13];
const Disc *PsxIoDisc(void) { return disc; }
static unsigned Word(const unsigned char *p) { return (unsigned)p[0] << 8 | p[1]; }

int main(int argc, char **argv)
{
    char error[256];
    assert(argc == 2);
    disc = DiscOpen(argv[1], error, sizeof(error));
    if (!disc) { fprintf(stderr, "%s\n", error); return 1; }
    assert(PortSaturnDisc());
    size_t size;
    unsigned char *table = SaturnBlock("art\\common\\birds.rbh", 0, &size);
    SaturnVideo *memory = SaturnLoadVideo("art\\common\\birds.vri");
    assert(table && memory);
    unsigned short converted[32][4096], texels[4096], clut[16];
    for (int frame = 0; frame < 32; frame++) {
        const unsigned char *record = SaturnSpriteRecord(table, size, frame / 2, NULL);
        assert(record && Word(record) == 1 && Word(record + 14) == 0x0840);
        const unsigned char *source = SaturnVideoAt(memory, 0x25c00000u + Word(record + 12) * 8u, 2048);
        const unsigned char *palette = SaturnVideoAt(memory, 0x25c00000u + Word(record + 6) * 8u, 32);
        assert(source && palette);
        assert(SaturnArtTim("bird.cmp", frame, 0, 64, 64, clut, 16, texels));
        int visible = 0;
        for (int p = 0; p < 4096; p++) {
            unsigned index = (source[p / 2] >> (p & 1 ? 0 : 4)) & 15;
            unsigned rgb = Word(palette + index * 2) & 0x7fff;
            unsigned expected = index ? (rgb ? rgb : 1) : 0;
            assert(texels[p] < 16 && clut[texels[p]] == expected);
            converted[frame][p] = clut[texels[p]];
            visible += expected != 0;
        }
        assert(visible > 0 && visible < 4096);
        if (frame & 1) assert(!memcmp(converted[frame], converted[frame - 1], sizeof(converted[frame])));
    }
    for (int a = 0; a < 32; a += 2) for (int b = a + 2; b < 32; b += 2)
        assert(memcmp(converted[a], converted[b], sizeof(converted[a])));
    SDL_free(table);
    SDL_free(memory);
    DiscClose(disc);
    puts("Saturn disc: all 131072 converted bird pixels match; 16 distinct poses, paired slots and transparent borders: OK");
}
