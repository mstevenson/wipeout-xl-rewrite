/* Wipeout 64's prm files are put on the PS1's colour scale as they are imported: textured primitives' colours only,
   every object marked, the texture shift unchanged. */
#include "../../game/src/port_widen.c"
#include <assert.h>
char errorString[256];
void *DAlloc(HeapBlock *head, int size) { (void)head; (void)size; return NULL; }
void DFree(HeapBlock *head, char *ptr) { (void)head; (void)ptr; }
void Error(char *string, short errorLevel) { (void)string; (void)errorLevel; }

static void PutBig(unsigned char *p, int value)
{
    p[0] = (unsigned char)(value >> 8);
    p[1] = (unsigned char)value;
}

int main(void)
{
    enum { HEADER = 64 + 80 };
    /* one object without vertices: a GT4 (textured, four colours), then a G3 (untextured) */
    static unsigned char file[HEADER + sizeof(GT4) + sizeof(G3)];
    unsigned char *gt4 = file + HEADER, *g3 = gt4 + sizeof(GT4);
    const int flags = (int)FindField(&PortRecord_Object, "flags")->fileOffset;
    static const unsigned char in[4] = {255, 254, 127, 0}, out[4] = {128, 127, 64, 0};

    PutBig(file + FindField(&PortRecord_Object, "primitiveCount")->fileOffset, 2);
    PutBig(file + flags, 0x0001);
    PutBig(gt4, TypeGT4);
    PutBig(gt4 + offsetof(GT4, texture), 5);
    for (int i = 0; i < 4; i++) {
        memset(gt4 + offsetof(GT4, colour) + i * sizeof(CVECTOR), in[i], 3);
        gt4[offsetof(GT4, colour) + i * sizeof(CVECTOR) + 3] = 0x3C; /* (the code byte: left as it is) */
    }
    PutBig(g3, TypeG3);
    memset(g3 + offsetof(G3, colour), 200, 3 * sizeof(CVECTOR));

    assert(PortPrmShiftTextures((char *)file, sizeof(file), 3));
    assert(ReadBigShort(gt4 + offsetof(GT4, texture)) == 8);
    assert(gt4[offsetof(GT4, colour)] == 255 && ReadBigShort(file + flags) == 0x0001); /* shifting alone */

    assert(PortPrmN64((char *)file, sizeof(file)));
    assert(ReadBigShort(gt4 + offsetof(GT4, texture)) == 8);
    assert(ReadBigShort(file + flags) == (0x0001 | PORT_OBJECT_N64));
    for (int i = 0; i < 4; i++) {
        const unsigned char *c = gt4 + offsetof(GT4, colour) + i * sizeof(CVECTOR);
        assert(c[0] == out[i] && c[1] == out[i] && c[2] == out[i] && c[3] == 0x3C);
    }
    for (int i = 0; i < (int)(3 * sizeof(CVECTOR)); i++) assert(g3[offsetof(G3, colour) + i] == 200);

    assert(!PortPrmN64((char *)file, sizeof(file) - 1)); /* a file the walk does not end on */
    puts("Wipeout 64 prm colours put on the PS1's scale (textured only) and its objects marked: OK");
    return 0;
}
