# Renderer profiles on the shared backend

The merge combines upstream main at `c34020f` with the renderer-profiles branch
at `d9505a6`. `WIPEOUT_RENDER_PROFILE` selects PS1, PC, N64, Saturn or Modern.
Upstream's `WIPEOUT_RENDERER` selects the device, and
`WIPEOUT_RENDERER_SWITCH` schedules live device changes.

- Modern draws HDR color, emissive and overlay coverage, and hull velocity
  into auxiliary targets. It snapshots world depth for camera motion before
  the HUD clears depth. Display and capture share the shutter, bloom and
  tone-mapping passes. A device without half-float targets can still draw the
  SDR picture.
- Saturn draws native VDP1 quads from instanced joining lines and samples the
  VDP2 panorama with camera-aligned or original scroll. The CPU VDP1 reference
  is available through `WIPEOUT_SATURN_REFERENCE`.
- Frame capture retains the profile snapshot and version 5 format alongside
  upstream's asynchronous readback and retained image storage.
- The original OpenGL profile programs remain available as test references.

## Validation

The ordinary macOS build with the embedded Metal library, CPU regressions,
desktop GPU regressions and standalone OpenGL profile references were
exercised. The shared GPU probes cover Modern bloom, motion, HUD protection and
pending recorder reads through a device switch, plus Saturn's sky, indexed
quads and transparent texels. The Mac build completed scripted races on Metal
and OpenGL with both profiles. A Modern interpolation run switched from Metal
to OpenGL and back while capturing pictures. The GLSL ES shaders compile and
link offline, and desktop OpenGL checks the WebGL metadata and upload paths.
An Emscripten build and retail PS1 lockstep were unavailable.

All 17 I/O tests pass using the cached single-file US XL cue. The CDDA and
music tests had assumed a Redump file per track. Their reference reader now
follows the cue's binary file and track indexes and reads the expected samples
directly. Optional SPU oracle and extracted-disc checks require local fixtures
that were unavailable.
