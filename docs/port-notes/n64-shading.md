# Wipeout 64 shading

Wipeout 64's colours are on the N64's scale: the RDP modulates a texture with
an 8-bit colour (TEXEL x SHADE for scenery, TEXEL x PRIM for the road), where
255 leaves the texture unchanged. The PS1's equivalent is 128, so a cartridge
colour modulated as a PS1 colour came out at up to twice its brightness. The
cartridge's scenery also keeps a colour at each corner of its textured Gouraud
polygons. XL's plain object renderer used only the first one, which banded the
Klies Bridge cave walls and roof. Its road faces have a single colour each, as
XL's do.

The scale is carried with the data, not chosen where things are drawn. The
textures are marked when the track is built, so everything drawn with them
gets the cartridge's scale: road, scenery, sky and signs. Colours the game
writes itself stay on the PS1's scale.

## Edits

New header:

- `platform/n64_material.h` holds the marks:
  - `PORT_TIM_N64` on a TIM's flags word.
  - `PORT_TPAGE_N64` and `PORT_TPAGE_PS1_LIGHT`, texture page bits 14 and 15,
    which the console's GPU leaves unused.
  - `RENDER_LIGHT_PS1` and `RENDER_LIGHT_RGB8`, the scale of a face's colour.
  - `PortN64Modulation`, which converts 0-255 to 0-128.

Marking the data:

- `platform/original.c` (`PlaceTims`, `Window`, `MakeLibrary`) puts
  `PORT_TIM_N64` in the flags word of every cartridge TIM, including the
  road's 21 tile windows. Only the CLUT mode below it is kept, as before.
- `init.c` (`NewLoadTexture`) turns that flag into `PORT_TPAGE_N64` on the
  texture's `tsb`. `scene.c`, `trackasm.c` and `DrawSprite` already copy the
  `tsb` into primitives, tiles and packets.
- `wipeout.h`: `Face` gains `lightFormat` in its padding byte. Layout is
  unchanged on both ABIs: offset 19, 20 bytes (`tools/layouts.py show Face`).
  Since ships receive the face colour as a `CVECTOR`, the byte is that
  vector's `cd`.
- `track.c` (`WADInitFaces`) sets each face's `lightFormat` after
  `IntelFace`: `RGB8` on a Wipeout 64 track, `PS1` otherwise. The file's own
  padding is never trusted.

Colours the game writes:

- `track.h` adds two inline helpers:
  - `PortSetTrackColour` writes a face colour and marks it `PS1`.
  - `PortTrackLightPage` adds `PORT_TPAGE_PS1_LIGHT` to a packet's texture
    page when its texture is the cartridge's and its colour is the PS1's.
  - `track.h` now includes `wipeout.h` (for `Face`) instead of `psyq.h`.
- The game writes these colours through `PortSetTrackColour`:
  - `dynam.c` (`SetTrackAttributes`): weapon pads, speed pads, the pit lane
    and checkpoints.
  - `weapon.c` (`CycleWeaponGrids`): the weapon pads' flashing.

Drawing:

- `trackdraw.c`: the new `SetTrackLight` writes the face colour and that page
  bit at all five places a road packet takes its face colour. These are in
  `AsmTrackLo`, `DrawGridQuad`, `AsmTrackHi` and `PortTrackFar` (twice, once
  through `DrawNearFace`).
- `crack.c` (`FillMedLoCrack`, `FillHiMedCrack`) marks the crack-filling
  triangles' pages the same way, and includes `track.h`.
- `objdraw.c`:
  - `GouraudTextured` keeps the corner colours of a GT3/GT4 whose page has
    `PORT_TPAGE_N64`, in every renderer. Those are the plain one, the lit one
    and the sky's, both on screen (`DrawObject`) and at the near plane and
    sky boundary (`DrawClippedPrimitive`).
  - XL polygons are unchanged.
  - `LitColour` converts a Wipeout 64 road colour to the PS1's scale with
    `PortN64Modulation` before the lit renderer tints an XL ship or the rescue
    droid with it. A pad's colour is already on the PS1's scale and is left as
    it is.
- `portorig.c` (`PortOriginalDrawDroid`): the sign panel blinks at 255 and
  128, because its texture now draws on the cartridge's scale. It used to
  blink at 128 and 64, which compensated for the PS1 scale.
- `libgpu.c` (`Polygon`) sets `GpuDrawState.n64Tint` for a textured polygon
  whose page has `PORT_TPAGE_N64` without `PORT_TPAGE_PS1_LIGHT`. Both bits
  belong to that packet only: `SetTexpage` keeps only bits `0x9FF`, so the
  current page never holds them.
- `gpu_renderer.c` passes `n64Tint` to the shaders in bit 1 of the vertex's
  texture colour mode. The GL and Metal fragment shaders then use 255, not
  128, as neutral.
- `framecap.c` and `tools/framecap.py` shade captured primitives the same
  way.

The previous approach is withdrawn. A scenery draw kind
(`DRAW_SMOOTH_SCENERY`, `PortDrawScenery`, `PortFarScenery`) handled only
scenery. It is removed, and `draw.c`, `portfar.c` and `portfar.h` are back to
the base (c34020f).

## Tests

- `test_near_object.c`:
  - XL triangles and quads drawn at the near plane are flattened to their
    first corner's colour.
  - Wipeout 64 triangles and quads keep each corner's colour at the near
    plane, at the sky boundary and on screen, and their page bit is kept.
  - The renderer decision follows the page bit in the plain and sky
    renderers.
  - `LitColour` converts a cartridge road colour, and leaves a pad's colour,
    and every colour on an XL track, as they are.
- `test_n64_light.c` (new, run by `run.sh`) gives libgpu flat textured
  packets with an XL page, a Wipeout 64 page and a Wipeout 64 page with
  `PORT_TPAGE_PS1_LIGHT`. It checks the scale each gets, and that the
  current page keeps neither bit.
- `test_gpu_passes.c` checks N64 and PS1 tints in one batch, with and without
  HD textures.
- `test_guest_sign.c` checks the new blink values.

## Checks

- `cmake --build build/n64-tint` builds.
- `tools/bugtest/run.sh` passes, and so does `WIPEOUT_TEST_GL=1`, which adds
  the OpenGL suites.
- `tools/diag.py` reports nothing for `objdraw.c`, `trackdraw.c`, `crack.c`,
  `track.c`, `init.c`, `dynam.c`, `weapon.c` and `portorig.c`.
- `tools/xdecl.py` reports the same 6 issues the base does.

The MIPS o32 comparison against the base (c34020f) needed two workarounds:

- Temporary libc headers (`limits.h`, `math.h`, `stddef.h`, `stdio.h`,
  `stdlib.h`, `string.h`, `sys/types.h`), removed afterwards.
- A baseline built from a tree including `platform/`, which
  `oracle32.py snapshot` leaves out.

It reported changes only where intended:

- `crack.c`: `FillMedLoCrack` and `FillHiMedCrack`, from the page mark.
- `dynam.c`: `SetTrackAttributes`, from `PortSetTrackColour`'s extra store.
- `weapon.c`: `CycleWeaponGrids`, the same.
- `init.c`: `NewLoadTexture`, from the texture mark.
- `track.c`: `WADInitFaces`, from the `lightFormat` loop.
- `trackdraw.c`: `AsmTrackLo`, `AsmTrackHi`, `DrawGridQuad` and
  `PortTrackFar`, from `SetTrackLight`.
- `objdraw.c`: `DrawClippedPrimitive`, from the page test. `DrawObject`,
  from the page test and `LitColour`, both inlined.
- `draw.c` and `portfar.c`: no change from the base.
- `portorig.c` could not be compared, because clang's MIPS-I backend crashes
  in `PortOriginalClassMenu` at both the base and the branch.

## Not done or not verified

- Not compared in a running game: the road's, the pads', the ships' and the
  sign housing's brightness on screen. The sign housing's authored colour is
  a uniform 127, so it now draws at half brightness, as on the cartridge.
- The N64's texture decoding is not reproduced. RGB5 expands by bit
  replication (`(c << 3) | (c >> 2)`, reaching 255), where the shaders
  multiply by 8 and reach 248. Palette bit 15 is ignored and texture
  mirror/wrap flags apply. Its three-point filtering and VI dither filter
  are not reproduced either.
