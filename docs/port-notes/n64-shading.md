# Wipeout 64 shading

Wipeout 64's colours are on the N64's scale: the RDP modulates a texture with
an 8-bit colour (TEXEL x SHADE for scenery, TEXEL x PRIM for the road), where
255 leaves the texture unchanged. The PS1's equivalent is 128, so a cartridge
colour modulated as a PS1 colour came out at up to twice its brightness. The
cartridge's scenery also keeps a colour at each corner of its textured Gouraud
polygons. XL's plain object renderer used only the first one, which banded the
Klies Bridge cave walls and roof.

The colours are put on the PS1's scale once, when the cartridge's track is made
XL's files (`platform/original.c`). After that a Wipeout 64 track is drawn,
and lights XL's ships, exactly as XL's own tracks are. The PS1 renderer, the
shaders and the frame recorder are unchanged.

## Why 7 bits are enough

The cartridge's data is authored at 8 bits: across its 7 tracks, 11,818 road
faces and about 9,700 scenery and sky primitives use all 256 values, 55% of
them odd. Its screen cannot show the eighth bit:

- Every track texture is a 4- or 8-bit CLUT TIM with a 15-bit palette
  (`TimBlocks` takes no other kind), so texels have 5 bits a channel.
- The game draws into a 16-bit RGBA5551 frame buffer. Its only colour image
  setup is `G_SETCIMG` RGBA 16-bit, 320 wide (`lui 0xFF10; ori 0x013F` at ROM
  0x4dd44, and two static display-list words of the same); there is none for
  32 bits.

Measured over every 5-bit texel and every colour the data uses, the 0-128
scale changes the output by at most 0.99 and on average 0.22 of 255, about an
eighth of the frame buffer's own step. That is well under the texel expansion
the port already accepts (x8, reaching 248, against the N64's bit replication,
reaching 255: up to 7). Rescaling flattens 1.9% of the corner-colour gradients,
all of them a single step (n to n+1); the median gradient spans 32 and keeps
its shape.

## Edits

- `platform/n64_material.h` (new): `PORT_OBJECT_N64`, an `Object.flags` bit,
  and `PortN64Modulation`, which converts 0-255 to 0-128, rounded.
- `platform/original.c` (`Wipeout64Parts`):
  - puts the colours of the `track.trf` copy's faces on the PS1's scale;
  - calls `PortPrmN64` on the scenery (`sceneCom` and `sceneSin` as one, after
    the texture shift), the sky (after `Wipeout64Sky`) and the pit and sign
    models (`pit.prm`).
- `game/src/port_widen.c`: `PortPrmShiftTextures` becomes `ConvertPrm`, shared
  with the new `PortPrmN64`. `PortPrmN64` puts the colours of textured
  primitives (FT, GT, LSFT, LSGT, sprites) on the PS1's scale and sets
  `PORT_OBJECT_N64` in each object's flags. Untextured colours are not
  modulated on either console and stay as they are; the code bytes are kept.
  Declared in `port_widen.h`.
- `game/src/objdraw.c`: `GouraudTextured(kind, object)` keeps the corner
  colours of a GT3/GT4 in every renderer when the object carries
  `PORT_OBJECT_N64`. It replaces `GouraudColours` in `DrawObject` and for
  GT3/GT4 in `DrawClippedPrimitive` (near plane and sky boundary). XL's and the
  original Wipeout's objects are unchanged.

`Object.flags` is free for the mark: the game only byte-swaps it
(`scene.c`) and copies it, and every object in XL's and the original
Wipeout's PRM files has flags 0 (3,069 and 4,336 objects on the USA discs).

The sign panel keeps blinking at 128 and 64 (`PortOriginalDrawDroid`), already
the PS1's scale. The sign housing's authored 127 becomes 64, half brightness,
as on the cartridge.

The earlier version of this change carried the 255 scale to the GPU: a
`PORT_TIM_N64` TIM flag, texture page bits 0x4000 and 0x8000, `Face.lightFormat`,
`PortSetTrackColour`, `PortTrackLightPage`, `LitColour`, `GpuDrawState.n64Tint`,
a texture mode bit, shader and frame recorder changes. All of it is withdrawn.
`dynam.c`, `weapon.c`, `crack.c`, `trackdraw.c`, `track.c`, `track.h`,
`wipeout.h`, `init.c`, `portorig.c`, `platform/psx/`, `platform/render/` and
`tools/framecap.py` are back to the base (c34020f).

## Tests

- `test_n64_import.c` (new, run by `run.sh`): `PortPrmN64` rescales a GT4's
  four colours (255, 254, 127, 0 to 128, 127, 64, 0), keeps their code bytes,
  leaves a G3's colours alone, marks the object while keeping its other flags
  and keeps the texture shift; a file the walk does not end on fails.
- `test_near_object.c`: XL's GT3/GT4 at the near plane are flattened to their
  first colour; a marked object's keep each corner at the near plane, at the
  sky boundary and on screen; `GouraudTextured` follows the mark in the plain
  and sky renderers.

## Not done or not verified

- Not compared in a running game: the road's, the pads', the ships' and the
  sign housing's brightness on screen.
- The primitive buffer (`PrimitiveBuffer0/1`, 0xAC00 bytes, unchecked) now
  takes POLY_GT3/POLY_GT4 for Wipeout 64's textured Gouraud scenery instead of
  POLY_FT3/POLY_FT4: up to 30% more per polygon. Its high-water mark on dense
  tracks (Klies Bridge) is not measured.
- `tools/oracle32.py diff` not run. Expected changes: `DrawObject` and
  `DrawClippedPrimitive` (objdraw.c), and `PortPrmShiftTextures` (port-only,
  now through `ConvertPrm`).
- The N64's texture decoding, three-point filtering and VI filters are not
  reproduced.
