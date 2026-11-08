/* Optional local disc: the train is embedded in Gare d'Europa, outside its
   scenery instance table. Check the converted PRM against every source face. */
#include "../../platform/saturn_models.c"
#include "psx/disc.h"
#include <assert.h>

static Disc *disc;
char *cdTrackNames[13], *cdTrackArtists[13];
const Disc *PsxIoDisc(void) { return disc; }

static unsigned Little(const uint8_t *p)
{
    return (unsigned)p[0] | (unsigned)p[1] << 8 | (unsigned)p[2] << 16 | (unsigned)p[3] << 24;
}

static void CheckCmp(const uint8_t *cmp, size_t size, int train)
{
    unsigned count = Little(cmp);
    size_t header = 4 + 4 * count, total = 0, at = 0;
    assert(count > 0 && header < size);
    for (unsigned i = 0; i < count; i++) total += Little(cmp + 4 + i * 4);
    uint8_t *data = SDL_malloc(total);
    assert(PortLzssDecompress(cmp + header, size - header, data, total) == total);
    for (unsigned i = 0; i < count; i++) {
        const uint8_t *tim = data + at;
        assert(Little(tim) == 0x10 && Little(tim + 4) == 8);
        unsigned cx = Little(tim + 12) & 65535, cy = Little(tim + 12) >> 16;
        const uint8_t *rect = tim + 8 + Little(tim + 8) + 4;
        unsigned x = Little(rect) & 65535, y = Little(rect) >> 16;
        unsigned w = Little(rect + 4) & 65535, h = Little(rect + 4) >> 16;
        assert(w && h && x + w <= 1024 && y + h <= 1004 && y >= 512);
        if (train) {
            assert(x >= 960 && y >= 768 && cx >= 960 && cy == 1018);
        } else {
            assert(x + w <= 960 || y + h <= 768);
            assert(cy >= 1011 && (cy < 1018 || (cy == 1018 && cx < 960)));
        }
        at += Little(cmp + 4 + i * 4);
    }
    assert(at == total);
    SDL_free(data);
}

int main(int argc, char **argv)
{
    char error[256];
    assert(argc == 2);
    disc = DiscOpen(argv[1], error, sizeof(error));
    if (!disc) { fprintf(stderr, "%s\n", error); return 1; }
    assert(PortSaturnDisc());
    size_t size, vertexSize, prmSize, cmpSize;
    uint8_t *block = SaturnBlock("art\\track02\\course02.rbh", 2, &size);
    uint8_t *vertices = SaturnBlock("art\\track02\\course02.rbh", 4, &vertexSize);
    SaturnVideo *video = SaturnLoadVideo("art\\track02\\course02.vri");
    Model source; int origin[3] = {0};
    assert(block && vertices && video);
    assert(ReadModel(block, size, vertices, vertexSize, Be32(block), origin, &source));
    assert(source.vertexCount == 85 && source.polyCount == 63);
    uint8_t *prm = SaturnMakeModel("train.prm", &prmSize);
    uint8_t *cmp = SaturnMakeModel("train.cmp", &cmpSize);
    assert(prm && cmp && cmpSize > 10000);
    CheckCmp(cmp, cmpSize, 1);
    size_t at = 0;
    uint32_t tags[6];
    for (int car = 0; car < 6; car++) {
        assert(at + 144 + 85 * 8 <= prmSize);
        assert(!memcmp(prm + at, "grid", 4));
        assert(Be16(prm + at + 16) == source.vertexCount);
        assert(Be16(prm + at + 32) == source.polyCount);
        tags[car] = Be32(prm + at + 40);
        for (int old = 0; old < car; old++) assert(tags[old] != tags[car]);
        assert((int32_t)Be32(prm + at + 84) > 90000); /* positioned on the railway */
        assert((int32_t)Be32(prm + at + 88) < -6000);
        for (int v = 0; v < 85; v++) for (int k = 0; k < 3; k++)
            assert((int16_t)Be16(prm + at + 144 + v * 8 + k * 2) == source.vertex[v][k]);
        at += 144 + 85 * 8;
        for (int face = 0; face < source.polyCount; face++) {
            assert(at + 4 <= prmSize);
            int kind = Be16(prm + at);
            assert(face < 12 ? (kind == G3 || kind == G4) : (kind == GT3 || kind == GT4));
            uint32_t packet[10] = {9u << 24};
            SaturnNativeBind(packet, packet + 10, tags[car], face);
            const SaturnNativePoly *native = SaturnNativeLookup(packet + 1);
            const Poly *p = &source.poly[face];
            assert(native && native->mode == p->mode && native->colour == p->colr);
            if (p->flags & TEXTURED) {
                int w, h;
                uint8_t *pixels = Texture(video, p, &w, &h);
                uint16_t palette[16]; Lut(video, p->colr, palette);
                assert(native->textured && native->width == w && native->height == h);
                assert(!memcmp(native->pixels, pixels, (size_t)w * h));
                assert(!memcmp(native->palette, palette, sizeof(palette)));
                SDL_free(pixels);
            }
            at += kind == G3 ? 24 : kind == G4 ? 28 : kind == GT3 ? 36 : 44;
        }
    }
    assert(at == prmSize);
    /* The separately loaded texture page and CLUTs cannot overwrite scenery. */
    Region scene, train;
    MakeRegion(&scene, R_SCENE); MakeRegion(&train, R_TRAIN);
    for (int i = 0; i < scene.pages; i++)
        assert(scene.page[i].x != train.page[0].x || scene.page[i].y != train.page[0].y);
    for (int i = 0; i < scene.cluts; i++) for (int j = 0; j < train.cluts; j++)
        assert(memcmp(scene.clut[i], train.clut[j], sizeof(scene.clut[i])));
    for (const SaturnSceneNames *track = saturnSceneNames; track->names; track++) {
        size_t bytes;
        uint8_t *scenery = SaturnMakeTrackModel(track->track, "scene.cmp", &bytes);
        assert(scenery);
        CheckCmp(scenery, bytes, 0);
        SDL_free(scenery);
    }
    FreeModel(&source); SDL_free(block); SDL_free(vertices); SDL_free(video);
    SDL_free(prm); SDL_free(cmp); SaturnNativeShutdown(); DiscClose(disc);
    puts("Saturn train: six positioned carriages, 378 source faces, light layout, native pixels/palettes and VRAM isolation across all eight courses: OK");
}
