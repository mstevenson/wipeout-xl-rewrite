/* Wipeout 64's colour scale, carried with its data. The cartridge modulates textures with 8-bit colours where 255
   leaves a texture unchanged (the RDP's TEXEL x SHADE or TEXEL x PRIM); the PS1's equivalent is 128. Its textures
   are marked when they are placed (platform/original.c), so everything drawn with them, the road, scenery, sky and
   signs alike, is modulated at its scale; colours the game itself writes stay at the PS1's. */
#ifndef PORT_N64_MATERIAL_H
#define PORT_N64_MATERIAL_H

/* A TIM's flags word: the image is the cartridge's (beyond the CLUT mode and the console's flags). */
#define PORT_TIM_N64 0x10000000
/* Texture page bits the console's GPU leaves unused, carried on a textured polygon's packet: its texture is the
   cartridge's (on Texture.tsb from loading on, so on every packet drawn with it) ... */
#define PORT_TPAGE_N64 0x4000
/* ... and its colour is the PS1's nonetheless (a road face's colour the game wrote: speed pads, weapon pads,
   checkpoints, the pit lane). Set on packets only, never on a texture. */
#define PORT_TPAGE_PS1_LIGHT 0x8000

/* Face.lightFormat: the scale of a road face's colour. */
enum { RENDER_LIGHT_PS1, RENDER_LIGHT_RGB8 };

/* An N64 colour on the PS1's scale (a road face's colour lighting an XL ship). */
static inline unsigned char PortN64Modulation(unsigned char value)
{
    return (unsigned char)(((unsigned)value * 128 + 127) / 255);
}

#endif
