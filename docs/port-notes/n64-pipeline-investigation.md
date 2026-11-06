# Wipeout 64 texturing, lighting and dithering investigation

Investigated 2026-11-03 against the USA `.z64` cartridge image, SHA-256
`fdcf4ae70d90abef4f299b04664e009a967e55f97a065b43f596b749dd7d6b15`.
The boot mapping is ROM `0x1000` → virtual `0x80000400`.
The investigation itself used isolated ROM routines, not full-machine graphics
emulation. Current behaviour is documented in [N64 rendering](../N64_RENDERING.md);
implementation and validation are in the [change record](renderer-changes.md).

## Confirmed: colour dithering and VI de-dithering

Running the original render-state builder at `0x8000973c` for all six arguments
produces the following state changes. These are display-list deltas: a missing
setting is inherited, not necessarily disabled.

| Mode | Texture filter | RGB dither | Render-mode word |
| --- | --- | --- | --- |
| 0 | BILERP | Magic square | `00552230` |
| 1 | BILERP | Magic square | `00552220` |
| 2 | BILERP | Magic square | `0F0A7038` |
| 3 | BILERP | Magic square | `0F0A4000` |
| 4, orthographic | POINT | Disabled | `00504240` |
| 5 | BILERP | Magic square | `00504A50` |

The exact colour-dither command is `BA000602 00000000` (magic square), or
`BA000602 000000C0` (disabled). The static initialization lists initially
disable dithering at ROM offsets `0x99418` and `0x99530`; relying only on those
lists would miss the later enable. The colour-image command at ROM `0x99470`,
`FF10013F`, selects RGBA16 with width 320. The clear rectangle covers 320×240.

The startup code chooses the NTSC VI mode at `0x800974b0` (table entry 2,
initial control `0x311e`). At `0x800662c4` and `0x800662cc` it calls the
special-features routine at `0x80078590` with `0x40` and `2`:

- `OS_VI_DITHER_FILTER_ON` enables the VI reconstruction filter.
- `OS_VI_GAMMA_OFF` disables gamma correction.

Executing that routine with the original mode data produces VI control
`0x13016`: 16-bit output, dither filter and divot enabled, AA mode 0, gamma
correction off. The gamma-dither bit remains set; that alone is not a reason
to apply a gamma-correction/noise effect when gamma correction is disabled.
The ROM's PAL and MPAL mode records also start with `0x311e`; only the NTSC
selection was explicitly checked in the verifier.

References: Nintendo's [colour-dither](https://ultra64.ca/files/documentation/online-manuals/man/n64man/gdp/gDPSetColorDither.html)
and [VI feature](https://ultra64.ca/files/documentation/online-manuals/man/n64man/os/osViSetSpecialFeatures.html)
documentation, [libreultra](https://github.com/n64decomp/libreultra/blob/master/src/io/visetspecial.c),
and [paraLLEl-RDP dither arithmetic](https://github.com/Themaister/parallel-rdp/blob/master/parallel-rdp/shaders/dither.h).
N64 dithering thresholds low bits before RGB5 storage; it is not PS1 signed-offset dithering.

## Texturing

BILERP uses three-point filtering with 1/32-texel weights, not PC bilinear
filtering. Initial state selects tile LOD, not mipmap LOD.

The ROM's palette converter at `0x80010454` takes the stored little-endian TIM
palette and writes N64 RGBA5551. Exhaustive execution for all 65,536 source
words confirms:

```
R = source & 31
G = (source >> 5) & 31
B = (source >> 10) & 31
A = (source & 0x7fff) != 0
```

The source bit 15 is ignored. Thus `0x8000` is transparent black, and nonblack
entries with bit 15 set are not intrinsically semitransparent. Alpha depends on
the converted palette and combiner/render mode, not PS1 bit-15 blending.

N64 RGBA16 channels expand as `(c << 3) | (c >> 2)`; multiplying by 8 only
reaches 248. The
[RDP texture decoder and filter arithmetic](https://github.com/Themaister/parallel-rdp/blob/master/parallel-rdp/shaders/texture.h)
also provide the integer rounding rules that our floating-point weighted sum
approximates.

The texture-descriptor builder at `0x80010988` preserves addressing flags:

| Descriptor flags | Behaviour demonstrated with 32×32 CI4 and CI8 images |
| --- | --- |
| `0` | No mask; image-edge clamping |
| `0x10` | Mirror S, power-of-two S/T masks |
| `0x20` | Mirror T, power-of-two S/T masks |
| `0x30` | Mirror both axes |
| `0x40` | Wrapping masks without mirroring |

Mask calculation is at `0x800108fc`; addressing applies inside image bounds to each filter tap.

These paths are used by real assets: inspecting `library.cmp`, `sceneCom.cmp`,
`sceneSin.cmp` and `sky.cmp` for all seven tracks found 689 image records:

| TIM flags | Count |
| --- | ---: |
| `08` | 390 |
| `09` | 126 |
| `48` | 133 |
| `58` | 10 |
| `68` | 1 |
| `78` | 29 |

That is 173 images carrying addressing flags, including 40 with mirroring.
Counts include repeated images across files/tracks; they are not unique-image
counts. Multiplayer-only scenery and effect archives were not included.

## Shading and lighting

The GT4 colour-copy block `0x8000bccc`–`0x8000be68` copies each corner's original
eight-bit RGB directly into the N64 vertices and assigns alpha 127. Executing
it with four different colours, including odd and near-extreme values,
confirms there is no conversion to the PS1's 0–128 modulation range there.

The emitted colour combiners are also more specific than one universal
texture-times-colour operation:

- Modes 0 and 2: `FC127E24 FFFFF3F9`, texture RGB × vertex shade RGB,
  texture alpha.
- Mode 1: `FC119623 FFFFFFFB`, texture RGB × primitive RGB; in the active
  one-cycle combiner, primitive alpha. The two combiner cycles differ in alpha.
- Mode 4: `FCFFFFFF FFFCF279`, texture colour/alpha.
- Mode 5: `FCFFFFFF FFFE793C`, vertex shade colour/alpha.
- Mode 3 does not install a new combiner; it inherits one.

The six state blocks do not enable `G_LIGHTING`. The static geometry reset
clears it, and the inspected object block supplies colour bytes, not normals.
This supports preserving authored/per-corner colours for this scenery path;
it does **not** establish that every effect or ship uses no CPU-generated or
directional lighting. Those paths need separate tracing before changing them.

N64 shade interpolation is screen-linear, separate from perspective-correct
texture coordinates; see
[RDP attribute interpolation](https://github.com/Themaister/parallel-rdp/blob/master/parallel-rdp/shaders/interpolation.h).

## Road-face lighting

Blocks `0x80016c84` and `0x800175d4` emit one `gDPSetPrimColor` for both triangles
of each road quad. Isolated execution with 256 RGB triplets confirms constant
face tint, with no neighbour averaging. This differs from scenery Gouraud shading.

## Reproduction

With Python `unicorn` installed:

```sh
python tools/bugtest/verify_n64_pipeline_rom.py "../roms/Wipeout 64 (USA).z64"
python tools/bugtest/verify_n64_filter_rom.py "../roms/Wipeout 64 (USA).z64"
```

Both passed against the local USA image. The new verifier executes original
instructions for all six render states, the VI control routine, every palette
word, CI4/CI8 descriptor flag combinations, and GT4 corner-colour copying. It
also inventories the real track TIM flags. VI interrupt-control helpers are
stubbed; it does not emulate video output or a complete race. No alpha-dither
selection command was found in the inspected state lists; its effective
inherited state remains unproven, so no alpha-noise recommendation is made.

These checks do not establish whole-frame or complete lighting equivalence.
