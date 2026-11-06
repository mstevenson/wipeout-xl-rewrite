# Rendering profiles

In **Graphics > Global renderer**, choose **PlayStation** (default) or **PC**.
**Use N64 renderer for Wipeout 64 tracks** is on by default and overrides that
choice only on imported N64 tracks. **Active renderer** shows the committed
frame's renderer. Selection changes rendering, not the game version or assets.

Each profile keeps its settings and has its own reset button. The global and N64
panels appear side by side when space allows; the N64 panel is dimmed when off.
Resolution, widescreen, frame rate, draw distance, fading and HD textures are shared.

| Profile | Defaults |
| --- | --- |
| PS1 | Vertex snapping, affine textures, RGB5 precision and eligible primitive dithering on; depth enhancement off (ordering tables) |
| PC | Bilinear filtering, scenery/ship lighting and depth on |
| N64 | Three-point filtering, RGB5 precision, magic-square dithering, VI dedithering, approximate divot filtering and linear display resampling on; depth always on |

Snapping and colour precision are independent. Dithering requires RGB5 precision;
N64 dedithering does too. Disabled dependencies retain their saved values.
Turning texture filtering off selects nearest sampling. PC lighting is independent
of filtering, perspective mapping and depth.

Profiles control all materials, including full-colour pages and glyphs, while
preserving source palette, addressing, modulation and transparency encodings.
N64 filters projected world textures (including billboards), but samples HUD,
menus and glyphs with nearest filtering; its display processing includes the HUD.
World shaders use explicit texel reads and explicit dithering, with driver
dithering disabled. See [PC evidence and limits](PC.md#rendering-evidence-and-limits)
and [N64 rendering](N64_RENDERING.md) for fidelity limits.

## Settings and migration

| INI value | Meaning |
| --- | --- |
| `renderer = auto` | N64 override on; `automatic_renderer = ps1|pc` selects global renderer |
| `renderer = ps1|pc` | Global renderer, N64 override off |
| Old `renderer = n64` | Migrates to `auto` |
| `ps1_*`, `pc_*`, `n64_*` | Independent profile settings; obsolete `n64_depth` ignored and no longer saved |

Old `pc_rendering` (preferred) or `smooth_textures` selects the global renderer
when the N64 override is enabled. Old `depth_buffer` initializes PC depth.
New keys win regardless of line order.

`WIPEOUT_RENDERER=ps1|pc` selects the global renderer and disables the override;
`auto|n64` enables it. Legacy `WIPEOUT_PC_RENDERING` / `WIPEOUT_SMOOTH_TEXTURES`
choose the global renderer only with the override enabled; `WIPEOUT_DEPTH_BUFFER`
sets PC depth for that run. Hidden regression runs neither read nor save player settings.

## Implementation

- `platform/render/renderer.[ch]`: registry, defaults, persistence and immutable
  frame snapshots. `*_renderer.c` owns each profile's material, depth and
  `RenderGeometry` policy; `renderer_ui.cpp` owns the panels.
- `{ps1,pc,n64}_gl.c`: independently linkable scene programs and presentation
  callbacks, each receiving only its own profile. N64 alone owns VI processing.
- `gl_backend.c`: texture uploads, batching and framebuffers. `gl_pipeline.c`
  dispatches; `gl_api.[ch]` and `gl_program.c` load GL and construct programs.
  Shared shaders decode assets and handle transport/blending without selecting
  a renderer. HD replacements preserve source addressing.
- `platform/psx/libgpu.c`: packet adapter. Game packet builders consume neutral
  geometry/light policy without selecting a named renderer or rewriting assets.

`PsxRendererBoundary` flushes work and commits settings at the completed game-loop
boundary. Only effective active-policy changes clear interpolation and inspection
history. Linked players exchange requested settings under the input mutex and
commit independently. Existing ordering-table depth clears remain.

Each screen buffer retains its renderer, profile, race-display state and colour
revision. Clears/uploads update the affected screen; copies inherit the source
policy; texture-only writes leave screen ownership alone. Back-buffer writes and
depth-only clears preserve front-buffer VI caches; reallocations invalidate both.
Drawing and presentation own resources separately, initialized on demand and
released when neither uses them. Presenting an old screen needs no scene program.
Glyph textures contain only level zero.

Display, screenshot/video readback and linked bands share presentation. Frame
recordings save `renderer.ini`; binary format 4 adds per-primitive shading flags
for affine/perspective interpolation and snapped coverage, with perspective fade.
The live inspector and `tools/framecap.py` use these flags. Python also reads older
formats, using available INI policy for interim version 3 and historical defaults
otherwise. Texture inspection estimates one native texel; it does not replay HD,
filtering or VI effects.

## Validation

Run from the repository root with an existing build tree:

```sh
sh tools/bugtest/run.sh
sh tools/bugtest/render.sh
cmake --build build/mac -j 2
```

The CPU suite covers settings/UI, frame boundaries, packets, lighting, captures,
material lookup and linked players. The GPU suite covers independent renderer
linking, production shaders, switches, resource ownership, HD addressing and all
64 N64 settings combinations against reference RGBA output across crops, scales,
flips and readbacks. Recorded results and blocked checks are in the
[consolidated change record](port-notes/renderer-changes.md).
