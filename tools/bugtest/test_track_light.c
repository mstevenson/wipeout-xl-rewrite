/* Exercise road packet emission with authored and gameplay-written lighting. */
#include "../../game/src/trackdraw.c"
#include <assert.h>
#include <stdio.h>

static Face face;
static TrackData data;
TrackData *track = &data;
static void *linked;
static int smooth;
static uint32_t tagged[4];
RenderGeometry PortRenderGeometry(void) { return (RenderGeometry){.smoothTrackLighting = smooth}; }
void PortTagTrackLight(const void *packet, const uint32_t colours[4])
{
    assert(packet == linked);
    for (int i = 0; i < 4; i++) tagged[i] = colours[i] & 0xffffffu;
}
void AddPrim(u_long *slot, void *poly) { (void)slot; linked = poly; }
void PortTagPrims(const void *first, const void *end, const char *kind, const char *name, int index)
{ (void)first; (void)end; (void)kind; (void)name; (void)index; }

static void SmoothTrackLighting(void)
{
    Face faces[] = {
        {.vertex = {1, 0, 2, 3}, .normal = {.vy = 4096}, .flags = TRACK_BASE, .red = 32, .green = 64},
        {.vertex = {3, 2, 4, 5}, .normal = {.vy = 4096}, .flags = TRACK_BASE, .red = 224, .green = 128},
        {.vertex = {0, 6, 7, 2}, .normal = {.vy = 4096}, .red = 255},
        {.vertex = {5, 4, 8, 9}, .normal = {.vy = 4096}, .flags = TRACK_BASE | SPEED_UP, .red = 255},
        {.vertex = {2, 10, 11, 12}, .normal = {.vy = 4096}, .flags = TRACK_BASE, .red = 255},
    };
    data.faces = faces;
    data.faceCount = sizeof(faces) / sizeof(*faces);
    data.vertexCount = 13;
    uint32_t first[4], second[4];
    assert(!TrackCornerLights(&faces[0], first));
    smooth = 1;
    assert(TrackCornerLights(&faces[0], first));
    assert(TrackCornerLights(&faces[1], second));
    assert(first[0] == 0x4020 && first[1] == 0x4020);
    assert(first[2] == 0x6080 && first[3] == 0x6080);
    assert(second[0] == first[2] && second[1] == first[3]);
    assert(second[2] == 0x80e0 && second[3] == 0x80e0);
    for (int i = 2; i < data.faceCount; i++) {
        uint32_t flat[4];
        assert(!TrackCornerLights(&faces[i], flat));
    }

    GridPoint med[9], hi[25], partial[9];
    ShadeTrackGrid(med, 3, first, 0, 0, 4);
    ShadeTrackGrid(hi, 5, first, 0, 0, 2);
    ShadeTrackGrid(partial, 3, first, 2, 6, 1);
    for (int y = 0; y < 3; y++)
        for (int x = 0; x < 3; x++) assert(med[y * 3 + x].light == hi[y * 10 + x * 2].light);
    assert(partial[0].light == hi[16].light && partial[2].light == hi[17].light);
    assert(partial[6].light == hi[21].light && partial[8].light == hi[22].light);
    assert((med[4].light & 0xffffffu) == 0x5050);

    /* The shared edge matches even at the finest partial-tile coordinates. */
    for (int x = 0; x <= 8; x++)
        assert(SampleTrackLight(first, x, 8) == SampleTrackLight(second, x, 0));

    POLY_FT4 packet = {0};
    Tile tile = {0};
    u_long ordering[8192] = {0};
    DrawGridQuad(&packet, ordering, &faces[0], &tile, &med[3], &med[4], &med[6], &med[7]);
    assert(tagged[0] == 0x5050 && tagged[1] == 0x5050);
    assert(tagged[2] == 0x6080 && tagged[3] == 0x6080);
    assert(packet.r0 == 32 && packet.g0 == 64);
    TagTrackFaceLight(&packet, &faces[0]);
    assert(!memcmp(tagged, first, sizeof(tagged)));

    PortSetTrackColour(&faces[1], 128, 128, 128);
    assert(TrackCornerLights(&faces[0], first) && first[2] == 0x406050);
    faces[1].lightFormat = RENDER_LIGHT_RGB8;
    assert(!TrackCornerLights(&faces[0], first));
    PortResetTrackLighting();
    puts("Track smoothing shares edges across detail grids and preserves surface and light encoding boundaries: OK");
}

int main(void)
{
    POLY_FT4 packet = {0};
    Tile tile = {.tsb = PORT_TPAGE_N64 | 17, .cba = 42};
    GridPoint corner = {.depth = 100};
    u_long ordering[8192] = {0};
    data.faces = &face;
    face = (Face){.red = 255, .green = 128, .blue = 64, .lightFormat = RENDER_LIGHT_RGB8};
    for (int source = 0; source < 3; source++) {
        if (source == 1) PortSetTrackColour(&face, 128, 128, 128);
        if (source == 2) PortSetTrackColour(&face, 255, 35, 117);
        for (int imported = 1; imported >= 0; imported--) {
            tile.tsb = (imported ? PORT_TPAGE_N64 : 0) | 17;
            Tile original = tile;
            Face before = face;
            assert(DrawGridQuad(&packet, ordering, &face, &tile, &corner, &corner, &corner, &corner) == &packet + 1);
            assert(linked == &packet);
            assert(packet.r0 == face.red && packet.g0 == face.green && packet.b0 == face.blue);
            assert(packet.tpage == (tile.tsb | (imported && source ? PORT_TPAGE_PS1_LIGHT : 0)));
            assert(packet.clut == 42);
            assert(!memcmp(&tile, &original, sizeof(tile)) && !memcmp(&face, &before, sizeof(face)));
        }
    }
    /* Reused packets must not retain gameplay metadata when authored RGB8 returns. */
    packet.tpage = PORT_TPAGE_N64 | PORT_TPAGE_PS1_LIGHT | 17;
    face.lightFormat = RENDER_LIGHT_RGB8;
    SetTrackLight(&packet, &face);
    assert(packet.tpage == (PORT_TPAGE_N64 | 17));
    SmoothTrackLighting();
    puts("Road packets retain authored/gameplay light encoding without changing RGB, tiles or PS1 pages: OK");
    return 0;
}
