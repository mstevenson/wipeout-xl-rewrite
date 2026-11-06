# Wipeout 64 rendering

Imported tracks use the N64 profile by default. See [Rendering profiles](RENDERERS.md)
for selection, controls, architecture and tests, and the
[ROM investigation](port-notes/n64-pipeline-investigation.md) for source evidence.

## Materials and geometry

- **Identity:** `PORT_TIM_N64` becomes `PORT_TPAGE_N64` on load. Objects carry
  `PORT_OBJECT_N64` through precise projections, clipping and billboards, including
  untextured geometry. Missing bounds disable filtering without losing identity.
- **Colour:** roads, scenery, skies and signs retain RGB8; modulation is 255-neutral,
  shade interpolation screen-linear and UVs perspective-correct. Signs blink at
  255/128. Shared XL ships retain PS1 lighting; `DrawObject` locally converts
  imported road light. Separate lighting provenance preserves PS1-scale gameplay
  pad colours, including neutral 128 and overbright values, through near/far and
  subdivided road packets in all renderers.
- **Road lighting:** each face has a constant tint, with hard boundaries between
  differently coloured faces. The ROM emits one primitive colour for both triangles;
  texture filtering does not smooth face lighting. Scenery retains Gouraud colours.
- **Palette:** RGB5 expands by bit replication to reach 255. Source bit 15 is
  ignored; zero RGB (including `0x8000`) is transparent. Other entries are opaque
  unless the primitive requests translucency. PS1 assets retain PS1 decoding.
- **Sampling:** three-point filtering uses the top-right/bottom-left diagonal and
  1/32-texel weights. Texture flags `0x10`/`0x20` mirror S/T; `0x40` enables wrap
  masks. Masked power-of-two images wrap/mirror each tap; unmasked images clamp.
  All 21 road subdivisions share full-image bounds. Out-of-range masked UVs can
  resolve by unique page/palette identity; ambiguous lookup uses nearest sampling.
  HD taps retain source addressing/mask eligibility but use replacement spacing.
  N64 addressing ignores PS1 texture-window bits.
- **World/UI:** projected textures, including affine world billboards and full-colour
  images, use the filter. Screen-space HUD, menus and glyphs use nearest sampling;
  glyph coordinates account for actual atlas resolution.
- **Depth/blending:** depth is always enabled. Filtered edges use premultiplied
  coverage; only fully opaque, unfaded fragments write depth. Primitive blend
  modes remain the port's; not every RDP combiner/blender state is reproduced.

`Wipeout64Parts` calls `PortPrmTriangulateN64` after remapping to split bent,
unlit scenery quads into `(0,1,2)` / `(2,1,3)`, preserving attributes and allowing
independent culling. This fixes mountain holes without changing PS1 imports,
skies or signs. Separately, GTE `Rtp` preserves precise corners beyond the console
screen range, avoiding affine fallback and oversized-triangle rejection at screen
edges. Integer GTE state and explicitly flat geometry's rejection remain unchanged.

## VI display processing

`platform/render/n64_gl.c` and `shaders/n64_vi.h` process the blended scene:

1. Magic-square thresholds quantize RGB to five bits, anchored to native pixels.
2. A 3×3 dedither filter reconstructs low colour bits (constant grey 100 reconstructs
   to 100 in the interior after quantization to alternating RGB5 values 12/13).
3. An approximate horizontal median filter removes high-contrast divots, followed
   by optional linear display resampling. Gamma correction stays off.

Separate buffers preserve VRAM and prevent repeated captures compounding filters.
Cached output is reused for matching source revision, crop, scale and processing
settings; destination size/orientation do not require reprocessing. Only enabled
stages run; one pass needs one target, multiple passes two. Returning to one frees
the second. With precision and divot off, processing bypasses allocations; final
resampling still applies and clamps to the source crop. Texture filtering is independent.

This approximates VI output: there are no RDP coverage/hidden bits, exact VI scaling
registers, full fixed-point rounding or per-write RGB5 feedback. Divot eligibility
uses colour discontinuities. Whole-scene dithering includes XL's HUD rather than
replaying N64 orthographic dither-disable state. Higher resolutions add geometry
detail while filter/dither spacing stays at native pixels. XL simulation, ships
and effects also prevent a whole-frame N64 match.

## Evidence and limits

The [ROM verifier instructions](port-notes/n64-pipeline-investigation.md#reproduction)
check isolated original routines, not GPU output. Native tests and replays do not
establish hardware equivalence. Some earlier culling/screen-edge tests and the
culling ROM verifier are absent; see the
[historical geometry record](port-notes/n64-rendering.md#historical-validation).
The [change record](port-notes/renderer-changes.md) retains native results and
blocked MIPS comparisons.
