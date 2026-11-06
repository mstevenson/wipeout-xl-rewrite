/* Exercise road packet emission with authored and gameplay-written lighting. */
#include "../../game/src/trackdraw.c"
#include <assert.h>
#include <stdio.h>

static Face face;
static TrackData data;
TrackData *track = &data;
static void *linked;
void AddPrim(u_long *slot, void *poly) { (void)slot; linked = poly; }
void PortTagPrims(const void *first, const void *end, const char *kind, const char *name, int index)
{ (void)first; (void)end; (void)kind; (void)name; (void)index; }

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
    puts("Road packets retain authored/gameplay light encoding without changing RGB, tiles or PS1 pages: OK");
    return 0;
}
