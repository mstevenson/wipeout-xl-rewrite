# Modern HDR bloom

Modern race geometry writes unclamped display-encoded colour alongside the
existing SDR picture. Both outputs use the same coverage and depth tests.
The game's blend modes operate on display-encoded colours, which preserves
the strength of subtractive shadows and additive exhaust flares. The resolve
uses SDR colour as a floor to recover from subtractive underflow. The HDR
presentation pass decodes to linear light, extracts highlights above SDR
white and averages them into a lower-resolution buffer sized in game pixels.
A separable Gaussian samples
adjacent texels in this buffer at every render scale. The resolve rolls off
the composited highlights for SDR display and capture. A coverage mask
protects the HUD, and the console's VRAM operations use the SDR picture.

The HDR draw framebuffer shares the SDR colour texture and depth buffer with
the regular framebuffer. It keeps the same draw attachments across batches.
Raw VRAM reads synchronize only SDR, and widescreen display and capture resolve
the same full-width picture before cropping.

Phenitia Park's three animated neon faces receive extra emission while their
original modulation is white. Gare d'Europa's tunnel fixture is the panel
named `new` at (56942, -1339, 42075). Four triangles share its wall texture.
The shader limits the extra emission to the bulb near UV (174, 148).

Fixture lookup, animation conditions and the bulb's UV mask live together in
`platform/material_source.c`. Packet metadata is attached independently of the
active renderer; the recorder only captures it. The packet adapter evaluates the
source's white-modulation condition and preserves the mask through clipping and
replay. Modern's shader consumes that mask as a vertex attribute.

`modern_renderer.c` requests original textures, source bounds and HDR targets.
The GL pipeline's optional batch-binding callback keeps Modern uniforms in
`modern_gl.c`. Shared transport allocates, seeds, copies and resizes HDR
attachments without reading Modern settings. The source/renderer boundary
refactor adds no game-code edits.

## Game source changes

- `game/include/port_platform.h` adds the model's source position to
  `PortTagObjectPrims`.
- `game/src/objdraw.c` passes the existing `MATRIX.t` field from
  `DrawObject`, `DrawNearPrimitive` and `DrawSaturnBillboard`. The position
  is host metadata carried outside the ordering-table packets.
- `game/src/portinfo.c` adds `PortActiveTrackNumber`. It reads the course
  from the active `ConfigData` for fixture lookup.

Compiler diagnostics passed for both C files. The MIPS oracle comparisons
could not run because the local `B0` snapshot was missing.

## Verification

The boundary refactor passes `tools/bugtest/run.sh`, `tools/bugtest/render.sh`
and the existing macOS build. Added checks cover source metadata under all
five profiles and bloom settings, animated emission, packet reuse/replay,
inactive profile resource isolation, arbitrary per-material emission masks,
and independently linked CPU/GL profiles with cache invalidation and HDR
presentation. Static checks guard the shared transport and recorder boundaries.
The optional Saturn-disc fixtures were not run; these checks do not establish
console equivalence. Concurrent changes to the default tonemapper were retained;
pixel fixtures select their expected curve explicitly.

The macOS build and CPU and GPU regression suites passed. The GPU test
covers unclamped colour with nearest and filtered sampling, tagged emission,
depth and HUD coverage, resizing, widescreen, SDR fallback and raw VRAM
readback. Both Modern shader programs compiled and linked as GLSL ES 3.00.
Fixed-step US PlayStation captures exercised both fixtures, including a
widescreen interpolated run on Gare d'Europa. The standalone assembly
harness could not link because it lacks existing Saturn renderer symbols.

In an unthrottled fixed-step Phenitia Park replay on an Apple M4 Max, with
widescreen, 8× resolution and interpolated frames, the median one-second
presentation rate with HDR rose from 107 to 160 fps. The slowest sampled
race interval rose from 90 to 142 fps. The rebuilt SDR path measured a
median of 181 fps in the same replay.
