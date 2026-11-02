# Wipeout 64 rendering

Imported Wipeout 64 tracks use their own material lighting, brightness and
three-point texture filtering within XL's renderer. This guide describes
that pipeline, scenery culling and the shared offscreen projection fix.
Detailed ROM evidence, implementation notes, imported-track trophy behavior
and historical validation are in the [port note](port-notes/n64-rendering.md).

## Materials and filtering

**Material identity and lighting.** Races mix N64 and PS1 assets, so rules
are selected per image. The importer marks each TIM, including road
subdivisions, with `PORT_TIM_N64`; `NewLoadTexture` carries that identity
into `PORT_TPAGE_N64`, which libgpu preserves. Both markers are defined in
`platform/n64_material.h`. Replacing a texture page also replaces its marker.
`GouraudTextured` in `game/src/objdraw.c` preserves N64 corner colours across
plain, lit and fixed-slot passes, including clipping. PS1 materials retain
their existing shading policy.

**Brightness.** N64 tint uses 255 for full brightness; PS1 uses 128.
`PortN64Modulation` rounds `value * 128 / 255` to the nearest integer.
`PortPrmNormalizeN64` in `game/src/port_widen.c` converts copied scenery, sky
and signs once, changing textured RGB only. `NormalizeN64TrackFaces` in
`platform/original.c` converts roads before `WADInitFaces` saves `colCopy`,
keeping road rendering, ship tint and colour restores on the same scale.
Animated weapon pads already use PS1's scale; native PS1 tints are unchanged.

**Image bounds.** `PlaceTims` registers each full image with
`PortN64TextureRegister` before subdivision. All 21 road pieces share those
bounds, allowing filtering across internal seams without sampling adjacent
packed images. Animation frames reuse the bounds; track changes clear them.
`GpuTriangles` filters only when `PortN64TextureBounds` finds the image.
Native taps are clamped before and after wrapping/windowing; HD taps are
clamped to their atlas image. Missing bounds fall back to nearest sampling.

**Three-point filtering.** The USA ROM selects `G_TF_BILERP` for its filtered
3D states and POINT for its orthographic mode. `FilteredTexture` in
`platform/psx/gpu_gl.c` implements the three-point rule for native and HD
images: split each texel square along the top-right/bottom-left diagonal,
then blend the containing triangle's corners with 1/32-texel weight steps.
At the centre, only the diagonal pair contributes; BILERP does not enable
`G_TF_AVERAGE`'s four-corner centre average. Native sampling shares palette
lookup, direct 16-bit colour decoding and texture-window addressing with
the PS1 path, preserving the semi-transparency bit. PS1 materials retain
nearest sampling, wrapping, zero-texel rejection, modulation and blend rules.

**Transparency and depth.** Filter taps decode palette entries before
blending. Transparent texels contribute no coverage; opaque and
semi-transparent contributions remain separate for their PS1 blend rules,
including subtraction. RGB is premultiplied by coverage and tinted colours
are clamped to it to prevent bright fringes. Filtered polygons enable
blending even when otherwise opaque. With console-depth disabled, only fully
opaque, unfaded coverage writes depth. `batchDepth` considers the entire
batch, so a leading translucent effect cannot disable an opaque road's
depth writes.

## Scenery geometry and projection

### Independent triangle culling

N64 scenery quads can be bent: one triangle may face away while the other
remains visible. XL's whole-quad facing test discarded both, causing holes
in Klies Bridge's snowy mountain. `Wipeout64Parts` calls
`PortPrmTriangulateN64` after texture remapping and colour normalization to
convert unlit scenery quads into `(0,1,2)` and `(2,1,3)` triangle pairs.
The conversion preserves winding, flags, texture references, UVs and corner
colours so the renderer can cull and clip each half independently. It does
not apply to PS1 imports or the separate N64 sky and sign models.

### Shared projection fix

`Rtp` in `platform/psx/gte.c` retains precise corners beyond the console's
screen-coordinate range. Such polygons can reach libgpu despite IR1/IR2
saturation; losing a precise corner forced affine drawing and could trigger
oversized-triangle rejection, leaving screen-edge holes. Integer GTE state
is unchanged, precise polygons keep perspective geometry, and explicitly
flat geometry retains the console's rejection rule. This also applies to
PS1 geometry and addresses a separate issue from bent-quad culling.

## Validation and limitations

Run from the repository root, using an already configured build tree:

```sh
sh tools/bugtest/run.sh
sh tools/bugtest/render.sh
cmake --build build/local --parallel 2
```

The CPU suite covers tint conversion, markers, image bounds and corner
colours. The OpenGL suite covers filter geometry and precision, native/HD
images, edges, transparency, depth batches and PS1 sampling behavior.

Check the ROM's filter commands with a local USA ROM and Python environment
containing `unicorn`:

```sh
python tools/bugtest/verify_n64_filter_rom.py "../roms/Wipeout 64 (USA).z64"
```

Some historically recorded culling and screen-edge tests and the culling ROM
verifier are absent from this checkout. The recorded MIPS oracle comparison
failed to compile, and no console lockstep recording was available. See the
[validation record](port-notes/n64-rendering.md#historical-validation) for
results and missing checks.

The port matches the filter shape and fractional weights, not the complete
N64 pixel pipeline: colour decoding and blending use floating-point
arithmetic rather than every RDP integer rounding step, and the Video
Interface is not emulated. ROM command-block checks and native replays do
not establish whole-renderer or console equivalence.
