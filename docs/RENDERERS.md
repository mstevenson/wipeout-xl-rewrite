# Rendering profiles

**Graphics** shows a horizontal row of **PlayStation**, **PC**, **Nintendo 64**
and **Sega Saturn** columns. Each selects the renderer profile to use for that
platform, defaulting to its matching renderer. Saturn discs use the Saturn
preference, PC discs/folders use the PC preference, and PlayStation discs use the
PlayStation preference. Imported PlayStation and Wipeout 64 tracks use their own
platform preferences, returning to the loaded disc's preference afterwards.
**Active renderer** shows the committed frame's renderer. Selection changes
rendering, not the game version or assets.

All four platform selectors stay visible. Below them, each renderer profile has
its own controls and reset button. Profiles keep their settings; platforms
choosing the same profile share those controls. The profile panels scroll
horizontally in narrow windows and with larger fonts.
Each profile has its own **Resolution** selector and remembers its scale. The
default is 1×: PS1 uses 320×240 (320×256 in PAL mode), N64 uses 320×240,
Saturn uses 352×240, and PC uses 640×480. Higher choices multiply that profile's
base dimensions; **Auto** follows the window size. Widescreen adds columns while
presentation preserves the 4:3 base aspect ratio. Switching profiles also switches
resolution; restoring a profile's defaults restores its 1× resolution.
Widescreen, frame rate, draw distance, fading and HD textures are shared.
HD texture replacements apply only above 1×, including when Auto selects a higher scale.

The shared **Frame rate > Original** mode follows the **active renderer profile**,
including overrides chosen for another platform. Its label shows the profile and
target: PlayStation 30 fps (25 in PAL mode), Nintendo 64 30 fps, Sega Saturn 20 fps,
and PC 60 fps. Changing profiles switches the target at the frame boundary.
The Saturn target follows [developer David Swan's account](https://www.arcadeattack.co.uk/david-swan-interview/)
of the 20 fps cap; N64 uses a 30 fps target for the supported USA release, and PC
uses a 60 fps presentation default, rather than claiming a fixed historical PC rate.
These targets do not reproduce hardware slowdowns.

Simulation, audio and disc timing keep their existing NTSC/PAL clock. PS1 Original
shows the game's frames without interpolation. Other profiles interpolate display
frames on their presentation clock, allowing PC's 60 fps and Saturn's 20 fps
without speeding up or slowing down gameplay. Original uses timed presentation
instead of monitor VSync. Fixed-step captures retain their deterministic behavior;
linked games share the main player's presentation policy.

| Profile | Defaults |
| --- | --- |
| PS1 | Vertex snapping, affine textures, RGB5 precision and eligible primitive dithering on; depth enhancement off (ordering tables); nearest display scaling |
| Saturn | Vertex snapping, affine textures, undithered RGB5 colour and checkerboard transparency on; original sky scrolling; depth enhancement off; nearest display scaling |
| PC | Bilinear texture filtering, scenery/ship lighting, depth and linear display resampling on |
| N64 | Three-point filtering, RGB5 precision, magic-square dithering, VI dedithering, approximate divot filtering and linear display resampling on; depth always on |

Snapping and colour precision are independent. Dithering requires RGB5 precision;
N64 dedithering does too. Disabled dependencies retain their saved values.
Turning texture filtering off selects nearest sampling. PC lighting is independent
of filtering, perspective mapping and depth.

Each profile has a **Linear display resampling** checkbox. Enable it to blend
neighbouring pixels when scaling the final image to the window; disable it for
sharp nearest-neighbour scaling. This affects the scene and HUD independently of
texture filtering. The saved keys are `ps1_resample`, `pc_resample`,
`saturn_resample` and `n64_resample`. Missing keys retain the defaults above.

Profiles control all materials, including full-colour pages and glyphs, while
preserving source palette, addressing, modulation and transparency encodings.
N64 filters projected world textures (including billboards), but samples HUD,
menus and glyphs with nearest filtering; its display processing includes the HUD.
World shaders use explicit texel reads and explicit dithering, with driver
dithering disabled. See [PC evidence and limits](PC.md#rendering-evidence-and-limits)
and [N64 rendering](N64_RENDERING.md) for fidelity limits.

## Saturn profile

Loading a Saturn disc uses **Sega Saturn** by default. Choose another profile in
the Sega Saturn column to override that platform, or choose the Saturn profile
for another platform. `WIPEOUT_RENDERER=saturn` forces it for the run, including
imported tracks. The profile works with any loaded assets.

Independent controls are `saturn_quantize_vertices`, `saturn_affine_textures`,
`saturn_colour_precision`, `saturn_mesh` (all default 1), and `saturn_depth` (default 0).
Textures use nearest sampling. RGB555 output is undithered, with each five-bit
channel shifted into the high bits of an eight-bit channel (0–248). Presentation
copies these values directly as sRGB with nearest sampling by default, including
screenshot readback. Enable `saturn_resample` for linear display scaling.
**Checkerboard transparency** draws mesh effects on alternating console pixels. Turn it off (`saturn_mesh = 0`)
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

Choose **352x240 (native)** in the Saturn profile's **Resolution** setting for
the console pixel grid. Higher resolutions, including **Auto** when it
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
without the compatibility cylinder's cropping or downsampling. The default is
the original Saturn game's faster, flat-screen scrolling. Its RBG0 transform
uses heading/2, vertical offset 120−pitch/3, roll, unit scale and pivot (176,120),
with 352 Saturn dots mapped onto the host's 320-dot scene. It keeps console-pixel
sampling at higher resolutions.
Enable **Graphics > Sega Saturn > Camera-aligned sky (enhancement)** to make
sky landmarks move with the scenery through yaw, pitch and roll. The shader
maps perspective viewing rays to panorama angles using the captured focal
length and inverse camera rotation. The setting is saved as
`saturn_camera_aligned_sky`. Settings files with only the retired
`saturn_original_sky_scroll` key start with the enhancement off.
`WIPEOUT_SATURN_VDP2_REFERENCE=1` forces original scrolling even when the
enhancement is enabled, independently of the VDP1 CPU reference switch.
In both modes the sky pose interpolates between game frames with the world's
camera-cut guard, and the sky draws behind geometry and outside depth testing.

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
| `ps1_renderer`, `pc_renderer`, `n64_renderer`, `saturn_renderer` | Profile for that platform: `ps1`, `pc`, `n64` or `saturn`; defaults to the matching profile |
| Old `renderer = auto` or `n64` | `automatic_renderer` migrates to the PlayStation preference; N64 defaults to its own profile |
| Old `renderer = ps1`, `pc` or `saturn` | Migrates to the PlayStation and N64 preferences (the old N64 override was off) |
| `ps1_*`, `pc_*`, `saturn_*`, `n64_*` controls | Independent profile settings; obsolete `n64_depth` ignored and no longer saved |
| `ps1_render_scale`, `pc_render_scale`, `n64_render_scale`, `saturn_render_scale` | Profile resolution multiplier; 1 by default, 0 for Auto; limited by GPU capacity |

The old global `render_scale` migrates only where a profile scale is missing.
Positive values become native multipliers; PC halves the old multiplier, rounding
up, because its base is now 640×480. The former default Auto value becomes 1× so
each platform starts at its native/default resolution. Explicit new profile keys
win regardless of line order. `WIPEOUT_RENDER_SCALE` overrides all profile
multipliers for a run, including 0 for Auto.

Migration keeps existing profile controls. The newly independent PC and Saturn
preferences default to their matching profiles. Old `pc_rendering` (preferred)
or `smooth_textures` initializes the PlayStation preference when neither
`automatic_renderer` nor `ps1_renderer` is present. Old `depth_buffer` initializes
PC depth. New keys win regardless of line order.

`WIPEOUT_RENDERER=ps1|pc|saturn` forces that renderer for the run; `auto|n64`
follows platform preferences. Legacy `WIPEOUT_PC_RENDERING` /
`WIPEOUT_SMOOTH_TEXTURES` choose the PlayStation preference;
`WIPEOUT_DEPTH_BUFFER` sets PC depth for that run. The forced renderer is not
saved over platform preferences. Hidden regression runs neither read nor save
player settings.

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
recordings save `renderer.ini`; binary format 5 stores logical picture extents
separately from captured pixel dimensions, so Saturn and PC images can be inspected
in game coordinates. Format 4 added per-primitive shading flags for
affine/perspective interpolation and snapped coverage, with perspective fade.
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
