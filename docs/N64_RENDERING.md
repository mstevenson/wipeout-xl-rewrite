# Wipeout 64 texture filtering

The USA ROM selects `G_TF_BILERP` for its filtered 3D render states. On N64
this means three-point filtering: blend three corners of a texture-pixel
square, choosing the triangle containing the sample. It is different from
the four-corner bilinear filter normally used by OpenGL.

## Evidence from the ROM

Inspected the 8 MiB big-endian USA cartridge image with SHA-256
`fdcf4ae70d90abef4f299b04664e009a967e55f97a065b43f596b749dd7d6b15`.
The boot mapping places ROM offset `0x1000` at virtual address `0x80000400`.

The render-state builder at `0x8000973c` was disassembled with Capstone and
executed with Unicorn for each of its six mode arguments. Its emitted
display lists contain these texture-filter commands:

| Mode argument | Command words | Meaning |
| --- | --- | --- |
| 0, 1, 2, 3, 5 | `BA000C02 00002000` | `G_TF_BILERP` |
| 4 | `BA000C02 00000000` | `G_TF_POINT` |

Modes 0, 1 and 2 also explicitly enable perspective texturing. Mode 4 calls
the orthographic setup at `0x80035498`. The static initialization lists agree:
ROM offset `0x993e8` selects BILERP in the list with perspective enabled;
offset `0x99500` selects POINT in the list with perspective disabled.
This confirms the filtered material setting, rather than assuming every
Wipeout 64 draw uses the same filter. It is an isolated routine check, not
a recording of every command emitted during a race.

Reproduce with a local ROM and a Python environment containing `unicorn`:

```sh
python tools/bugtest/verify_n64_filter_rom.py "../roms/Wipeout 64 (USA).z64"
```

The verifier runs the original instructions until the display list is
complete, stopping before cache writeback. It saves no cartridge data.

## Shader behavior

`FilteredTexture` in `platform/psx/gpu_gl.c` splits each texel square along
the top-right/bottom-left diagonal. The two corners on that diagonal are
shared; the third is either top-left or bottom-right. Weights use five
fractional bits (1/32-texel steps). At the exact centre, only the diagonal
pair contributes: BILERP does not enable the four-corner centre average
associated with `G_TF_AVERAGE`.

The same three-point rule applies to native images and HD replacements.
Each sample remains clamped to its image boundary. Existing colour scaling,
transparent coverage, blending and depth handling are retained. PS1
materials keep their existing sampling path; this change affects only
textures marked as imported N64 materials.

This matches the filter shape and fractional weights, not the complete
N64 pixel pipeline. The port still uses its existing colour decoding and
floating-point colour/blend arithmetic rather than reproducing every RDP
integer rounding step, and it does not emulate the Video Interface here.

`tools/bugtest/render.sh` checks production shader output for both triangles,
the diagonal, fractional precision, image edges, colour formats, HD images,
transparency, depth and unchanged PS1 sampling.

## References

- [Nintendo's texture-filter tutorial](https://jrra.zone/n64/doc/tutorial/graphics/3/3_6.htm)
  describes BILERP as interpolation of three texels.
- [GBI definitions](https://github.com/glankk/n64/blob/master/include/n64/gbi.h)
  identify texture-filter bits 12–13 and the POINT, BILERP and AVERAGE values.
- [ParaLLEl-RDP's texture implementation](https://github.com/Themaister/parallel-rdp/blob/master/parallel-rdp/shaders/texture.h)
  documents the five fractional bits, diagonal selection and separate
  centre-average mode.
