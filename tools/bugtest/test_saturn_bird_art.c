/* Bird artwork uses a separate VDP1 image and a 16-pose cycle in 32 game slots. */
#include "../../platform/saturn_art.c"
#include <assert.h>

struct SaturnVideo { unsigned char bytes[32 + 16 * 2048]; };
static struct SaturnVideo birdMemory, commonMemory;
static unsigned char birdTable[4 + 16 * 4 + 16 * 16];
static int videoLoads, tableLoads;

static void Word(unsigned char *p, unsigned n) { p[0] = n >> 8; p[1] = n; }
static void Long(unsigned char *p, unsigned n) { Word(p, n >> 16); Word(p + 2, n); }

SaturnVideo *SaturnLoadVideo(const char *path)
{
    assert(!strcmp(path, "art\\common\\birds.vri"));
    videoLoads++;
    return &birdMemory;
}

unsigned char *SaturnBlock(const char *path, int block, size_t *size)
{
    assert(!strcmp(path, "art\\common\\birds.rbh") && block == 0);
    tableLoads++;
    *size = sizeof(birdTable);
    unsigned char *copy = SDL_malloc(*size);
    memcpy(copy, birdTable, *size);
    return copy;
}

unsigned char *SaturnVideoAt(SaturnVideo *v, unsigned address, size_t size)
{
    if (!v || address < VDP1_RAM || address - VDP1_RAM > sizeof(v->bytes) ||
        size > sizeof(v->bytes) - (address - VDP1_RAM)) return NULL;
    return v->bytes + address - VDP1_RAM;
}

int PortSaturnEurope(void) { return 1; }
int SaturnTitlePicture(unsigned short *pixels) { (void)pixels; assert(0); return 0; }

int LoadImage(RECT *rect, u_long *data) { (void)rect; (void)data; assert(0); return 0; }

int main(void)
{
    /* Common art is already loaded; the birds must not sample its unrelated VRAM. */
    loaded = 1;
    video = &commonMemory;
    pieces = hudMap = sprites = birdTable;
    memset(commonMemory.bytes, 0xff, sizeof(commonMemory.bytes));
    Long(birdTable, 4);
    Word(birdMemory.bytes + 2, 0x801f); /* opaque red */
    Word(birdMemory.bytes + 4, 0xfc00); /* opaque blue */
    for (int i = 0; i < 16; i++) {
        int offset = 4 + 16 * 4 + i * 16;
        Long(birdTable + 4 + i * 4, offset);
        unsigned char *record = birdTable + offset;
        Word(record, 1); Word(record + 2, 0xdead); Word(record + 4, 0x1488);
        Word(record + 12, 4 + i * 256); Word(record + 14, 0x0840);
        birdMemory.bytes[32 + i * 2048 + (20 + i) * 32 + 15] = 0x12;
    }
    unsigned short texels[64 * 64], previous[64 * 64], clut[16];
    for (int i = 0; i < 32; i++) {
        assert(SaturnArtTim("bird.cmp", i, 0, 64, 64, clut, 16, texels));
        int at = (20 + i / 2) * 64 + 30, opaque = 0;
        assert(clut[texels[at]] == 0x001f && clut[texels[at + 1]] == 0x7c00);
        assert(clut[0] == 0);
        for (int p = 0; p < 64 * 64; p++) opaque += texels[p] != 0;
        assert(opaque == 2);
        if (i & 1) assert(!memcmp(previous, texels, sizeof(texels)));
        else if (i) assert(memcmp(previous, texels, sizeof(texels)));
        memcpy(previous, texels, sizeof(texels));
    }
    assert(videoLoads == 1 && tableLoads == 1);
    assert(!SaturnArtTim("bird.cmp", -1, 0, 64, 64, clut, 16, texels));
    assert(!SaturnArtTim("bird.cmp", 32, 0, 64, 64, clut, 16, texels));
    assert(!SaturnSpriteRecord(birdTable, sizeof(birdTable) - 1, 15, NULL));
    puts("Saturn birds: separate artwork, all 16 poses, both animation parities, palette and transparency: OK");
}
