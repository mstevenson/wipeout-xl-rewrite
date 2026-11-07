# Rendering profiles

In **Graphics > Global renderer**, choose **PlayStation** (default), **PC** or **Sega Saturn**.
**Use N64 renderer for Wipeout 64 tracks** is on by default and overrides that
choice only on imported N64 tracks. **Active renderer** shows the committed
frame's renderer. Selection changes rendering, not the game version or assets.

Each profile keeps its settings and has its own reset button. The global and N64
panels appear side by side when space allows; the N64 panel is dimmed when off.
Resolution, widescreen, frame rate, draw distance, fading and HD textures are shared.

| Profile | Defaults |
| --- | --- |
| PS1 | Vertex snapping, affine textures, RGB5 precision and eligible primitive dithering on; depth enhancement off (ordering tables) |
| Saturn | Vertex snapping, affine textures, undithered RGB5 colour and checkerboard transparency on; depth enhancement off; nearest display scaling |
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

## Saturn profile

Select **Sega Saturn** in the global renderer, or set `WIPEOUT_RENDERER=saturn`.
It works with any loaded assets; loading a Saturn disc does not change the selection.
The Wipeout 64 track override still takes priority when enabled.

Independent controls are `saturn_quantize_vertices`, `saturn_affine_textures`,
`saturn_colour_precision`, `saturn_mesh` (all default 1), and `saturn_depth` (default 0).
Textures use nearest sampling. RGB555 output is undithered, with each five-bit
channel shifted into the high bits of an eight-bit channel (0–248). Presentation
copies these values directly as sRGB with nearest sampling, including screenshot
readback. **Checkerboard transparency**
draws mesh effects on alternating console pixels. Turn it off (`saturn_mesh = 0`)
to blend the same effects at half coverage, including ship shadows, native flares
and world effects inferred from half, additive and quarter-additive blending.
Compatibility light effects retain their additive operation on covered pixels;
with the checkerboard off they add half as much light without darkening the background.
Ship shadows keep their textured outlines and draw black on the covered pixels.
Texture cutouts and black additive samples remain clear in both modes. The HUD
keeps its own blending. Alpha-blended mesh effects test against the optional
depth buffer without writing to it.

The European Saturn race capture uses CMDPMOD `0x1584` on Gouraud-shaded
trail polygons and `0x1588` on the separate exhaust flare sprite. With a Saturn
disc and affine mapping enabled, the flare uses the 64×64 image and RGB555
palettes from race sprite entries 48 and 49, including the near-white centre.
The sprite follows the port's projected exhaust position and size. If its
ordering-table entry comes before any of its own trail segments, the renderer
draws it immediately after the last of those segments, using its original depth.
[Sega VDP1 mesh rules](https://www.infochunk.com/saturn/segahtml_en/hard/vdp1/hon/p06_33.htm)

With a Saturn disc, the profile also retains native materials alongside the
compatibility assets. Billboards whose parts share one material retain their
original palette with the flattened sprite, avoiding the fitted compatibility
palette and tint that can collapse adjacent shades. Mixed-material billboards
retain the compatibility fallback. Model faces keep their original CMDPMOD, per-face LUT, source
texels and A/B/C/D correspondence. The default GPU model path draws instanced strips for
VDP1 joining lines. Shaders compute coverage on the selected render pixel grid,
with original texel lookup, additive RGB555 shading, mesh, end codes and ordered
blending, including collapsed quads and repeated writes. Both native and inferred
mesh stay on the 1× console grid at higher render resolutions. The CPU submits
commands and row descriptors in streamed buffers. Native source indices are
cached in a renderer-owned atlas. Optional depth is GPU-tested.

Choose **Saturn native** in **Graphics > Resolution** for the port's 1× console
pixel grid (320×240 in 4:3 NTSC). Higher resolutions, including **Auto** when it
selects a higher scale, give finer VDP1 edges and VDP2 panorama sampling. Native
model quads keep affine texture mapping and additive Gouraud shading at every scale.

Set `WIPEOUT_SATURN_REFERENCE=1` to select the retained software quad rasterizer
for comparison. It draws on the 1× grid at every display scale. It composites
pixels on the CPU and uploads batch rectangles, or emits points with optional
depth. The background remains GPU-rendered in both modes. GPU tests compare the
GPU rasterizer with the software algorithm on each tested grid, including clipped attributes,
VRAM texture windows, fades and blend modes. Tiny host floating-point and framebuffer
rounding differences remain possible. Integer UV boundaries use a shared 1e-5
texel tolerance.
Unclipped native model quads have no texture-mapping diagonal. Native track tiles
use viewport-clipped triangles with perspective-correct texture coordinates to
stabilize nearby road textures and bound drawing work during camera moves. They use
the original, already-lit palettes and CMDPMOD 0x1488 plus SPD. Weapon grids select from
the course's palette cycle and use its depleted palette during pickup cooldown.
Native model Gouraud shading adds
a signed per-channel offset (16 is neutral) and saturates at the five-bit limits.
Ships use the recovered four-corner shade-table recipe: neutral, road light,
road light minus four, and RGB(12,12,12).

The race sky uses the original 2048×512 VDP2 cell-map panorama and colour RAM,
without the compatibility cylinder's cropping or downsampling. By default, the
panorama follows the world camera: the shader maps perspective viewing rays,
using the captured focal length and inverse camera rotation, to panorama angles.
Sky landmarks then move with the scenery through yaw, pitch and roll. Enable
**Graphics > Sega Saturn > Original Saturn sky scrolling** to use the game's
faster, flat-screen scrolling. The setting is saved as `saturn_original_sky_scroll`.
The original RBG0 transform uses heading/2, vertical offset 120−pitch/3, roll,
unit scale and pivot (176,120), with 352 Saturn dots mapped onto the host's
320-dot scene. It keeps console-pixel sampling at higher resolutions.
`WIPEOUT_SATURN_VDP2_REFERENCE=1` forces this mode independently of the setting
and the VDP1 CPU reference switch. In both modes the sky pose interpolates
between game frames with the world's camera-cut guard, and the sky draws
behind geometry and outside depth testing.

Disable affine mapping to use the compatibility geometry with perspective
correction. Native model quads snap to the selected render grid and use RGB555 colour.
Vertex-snapping and colour-precision toggles affect compatibility draws. Native
CMDPMOD determines which primitives get the selected transparency treatment.
Other discs keep the compatibility material path.

This is a native material/quad/background renderer, not a cycle-accurate Saturn
emulator. Projection, ordering, track subdivision, HUD and projectile stand-ins
still come from the port. Near-plane cuts use the host's triangle clipping, retaining
native palettes,
source UVs and additive shades. Those generated triangles use bounded screen-space
rasterization; they are not claimed to reproduce the Saturn game's clipping rules.
Runtime menu-logo shade animation, arbitrary VDP2 layer
priorities/windows, and exact VDP1 framebuffer feedback/rounding are not reproduced.
Ship road light is decoded from the compatibility colour; source levels above its
8-bit saturation point cannot be recovered. Frame inspection shows compatibility
texture samples, rather than native scan-converted pixels. Builds, tests and race
captures validate this implementation; they do not establish console equivalence.

## Settings and migration

| INI value | Meaning |
| --- | --- |
| `renderer = auto` | N64 override on; `automatic_renderer = ps1`, `pc` or `saturn` selects global renderer |
| `renderer = ps1`, `pc` or `saturn` | Global renderer, N64 override off |
| Old `renderer = n64` | Migrates to `auto` |
| `ps1_*`, `pc_*`, `saturn_*`, `n64_*` | Independent profile settings; obsolete `n64_depth` ignored and no longer saved |

Old `pc_rendering` (preferred) or `smooth_textures` selects the global renderer
when the N64 override is enabled. Old `depth_buffer` initializes PC depth.
New keys win regardless of line order.

`WIPEOUT_RENDERER=ps1|pc|saturn` selects the global renderer and disables the override;
`auto|n64` enables it. Legacy `WIPEOUT_PC_RENDERING` / `WIPEOUT_SMOOTH_TEXTURES`
choose the global renderer only with the override enabled; `WIPEOUT_DEPTH_BUFFER`
sets PC depth for that run. Hidden regression runs neither read nor save player settings.

## Implementation

- `platform/render/renderer.[ch]`: registry, defaults, persistence and immutable
  frame snapshots. `*_renderer.c` owns each profile's material, depth and
  `RenderGeometry` policy; `renderer_ui.cpp` owns the panels.
- `{ps1,pc,saturn,n64}_gl.c`: independently linkable scene programs and presentation
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
