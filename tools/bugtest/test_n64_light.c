/* Wipeout 64's textures are modulated at the cartridge's scale, except under a colour the game wrote (a pad's). */
#include "../../platform/psx/libgpu.c"
#include <assert.h>
int portPal;
int PortScreenMargin(void) { return 0; }
FrameCapPrim *FrameCapAdd(void) { return NULL; } /* not recording */
void FrameCapTagRecord(FrameCapPrim *prim) { (void)prim; }
static int drawn, tint;
void GpuTriangles(const GpuDrawState *state, const GpuVertex *vertices, int count)
{
    (void)vertices; (void)count;
    drawn++;
    tint = state->n64Tint;
}
/* A flat textured triangle (POLY_FT3's words) with the given texture page. */
static int Draw(uint32_t page)
{
    uint32_t w[7] = {0x24808080u, 0x00000000u, 0, 0x00000040u, page << 16, 0x00400000u, 0};
    drawn = 0;
    Polygon(w, 0x24);
    assert(drawn == 1);
    return tint;
}
int main(void)
{
    assert(!Draw(0x0005));                                         /* XL's */
    assert(Draw(0x0005 | PORT_TPAGE_N64));                         /* Wipeout 64's */
    assert(!Draw(0x0005 | PORT_TPAGE_N64 | PORT_TPAGE_PS1_LIGHT)); /* ... under a pad's colour */
    assert((state.texpage & (PORT_TPAGE_N64 | PORT_TPAGE_PS1_LIGHT)) == 0); /* never the current page's */
    puts("Wipeout 64 textures modulate at 255, pads' colours at the PS1's 128: OK");
    return 0;
}
