/* Wipeout 64's colours. The cartridge modulates textures with 8-bit colours where 255 leaves a texture unchanged
   (the RDP's TEXEL x SHADE or TEXEL x PRIM); the PS1's equivalent is 128. Its road faces' and textured primitives'
   colours are put on the PS1's scale as the track is made XL's (platform/original.c), so they draw as XL's do and
   light XL's ships as XL's road does. Its untextured colours mean the same on both and are left as they are. */
#ifndef PORT_N64_MATERIAL_H
#define PORT_N64_MATERIAL_H

/* Object.flags: the object is the cartridge's (set on import; XL's and the original Wipeout's files leave the flags
   0). Its textured Gouraud polygons keep their corner colours in every renderer, as the cartridge draws them. */
#define PORT_OBJECT_N64 0x4000

/* An N64 colour on the PS1's scale: 255 to 128, rounded. The cartridge's textures have five bits a channel and it
   draws to a 16-bit frame buffer, so the bit this drops never reaches its screen. */
static inline unsigned char PortN64Modulation(unsigned char value)
{
    return (unsigned char)(((unsigned)value * 128 + 127) / 255);
}

#endif
