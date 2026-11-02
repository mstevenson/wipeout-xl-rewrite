/* Private page metadata survives packet decoding, and is cleared by PS1 pages. */
#include "../../platform/psx/libgpu.c"
#include <assert.h>
#include <stddef.h>
_Static_assert(offsetof(GpuDrawState, fade) == 13 * sizeof(int), "frame recording layout");

int main(void)
{
    GpuDrawState draw;
    unsigned page = GetTPage(1, 2, 128, 768);
    graphType = 0;
    SetTexpage(page | PORT_TPAGE_N64);
    StateFor(&draw, 1, 1, 0);
    assert(draw.filtered && draw.pageX == 128 && draw.pageY == 768 && draw.depth == 1 && draw.semi == 2);
    assert(DrawModeWord(1, 0, page | PORT_TPAGE_N64) & PORT_TPAGE_N64);
    SetTexpage(page);
    StateFor(&draw, 1, 1, 0);
    assert(!draw.filtered && draw.pageY == 768); /* high VRAM is not itself an N64 tag */
    TextureFromPage(&draw, GetTPage(0, 0, 640, 256));
    assert(!draw.filtered && draw.pageX == 640 && draw.pageY == 256 && draw.depth == 0);
    puts("N64 material tag survives GPU packets without leaking into PlayStation pages: OK");
    return 0;
}
