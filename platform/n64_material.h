#ifndef PORT_N64_MATERIAL_H
#define PORT_N64_MATERIAL_H

#ifdef __cplusplus
extern "C" {
#endif

/* Round N64's 0..255 texture tint to PS1's 0..128 scale (128 is unmodulated). */
static inline unsigned char PortN64Modulation(unsigned char value)
{
    return (unsigned char)(((unsigned)value * 128 + 127) / 255);
}

/* Inclusive page-relative texel bounds [left, top, right, bottom] of the whole image,
   not its 21 road pieces, to avoid filtering seams and neighbour bleed. Animation frames reuse the bounds. */
void PortN64TexturesReset(void);
int PortN64TextureRegister(int x, int y, int wordWidth, int height, int depth, int clutX, int clutY, int flags);
int PortN64TextureInfo(int pageX, int pageY, int depth, int clutX, int clutY,
                       float u, float v, int bounds[4], int *flags);
int PortN64TextureBounds(int pageX, int pageY, int depth, int clutX, int clutY,
                        float u, float v, int bounds[4]);

#ifdef __cplusplus
}
#endif
#endif
