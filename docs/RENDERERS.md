# Rendering profiles

**Graphics** starts with **Use Modern for all platforms**, enabled by default
for every disc and imported track. Turn it off to use the selections in the
**PlayStation**, **PC**, **Nintendo 64**, and **Sega Saturn** columns. Each
selection starts with its matching renderer. Saturn discs use the Saturn
preference, PC discs/folders use the PC preference, and PlayStation discs use the
PlayStation preference. Imported PlayStation and Wipeout 64 tracks use their own
platform preferences, returning to the loaded disc's preference afterwards.
**Active renderer** shows the committed frame's renderer. Selection changes
rendering, not the game version or assets.

Modern appears first in each platform selector and in the row of profile panels.
All four platform selectors stay visible. Below them, each renderer profile has
its own controls and reset button. Profiles keep their settings; platforms
choosing the same profile share those controls. The profile panels scroll
horizontally in narrow windows and with larger fonts.
Resolution, frame rate, and draw distance sit at the top of each panel,
followed by hardware-specific controls. **Enhancements** groups controls for
port-specific features.
Each profile has its own **Resolution** selector and remembers its scale.
At 1×, PS1 uses 320×240 (320×256 in PAL mode), N64 uses 320×240,
Saturn uses 352×240, and PC uses 640×480. Higher choices multiply that profile's
base dimensions; **Auto** follows the window size. Widescreen adds columns while
presentation preserves the 4:3 base aspect ratio. Switching profiles also switches
resolution. Modern defaults to Auto and uses the PlayStation base dimensions.
The four hardware profiles default to 1×.
Widescreen, the HD textures toggle, and the choice of texture pack are shared. Each profile saves its **Draw distance**
choice, Full-mode fade toggle and range, and distant engine flare toggle. Modern defaults to
**Full** with both effects enabled. The four hardware profiles default to
**Native** with scenery fade off.
`WIPEOUT_FAR_VIEW`, `WIPEOUT_FADE`, and `WIPEOUT_FAR_FLARES`
override these preferences for the current run. For Modern, `WIPEOUT_FAR_VIEW=0`
selects **Custom**, which uses its own fade range.

Modern's **Custom** slider sets the distance from the camera in world units.
The range is 20,000 to 100,000 and defaults to 96,000. Scenery always fades
into the sky over the last third of that distance. A saved Modern Native
selection opens as Custom.

Native draw distance follows the active renderer. PC uses
its original F3 road and scenery limits and its separate rendered-ship gate.
PS1 retains the original regional road, scenery and ship rules. N64 uses its
camera-direction/junction road walker, original scenery bounds and remembered
ship visibility. Saturn uses contiguous authored road spans, course depth buckets,
model bounds and its separate ship depth interval. These policies apply when
**Native** is selected, including renderer overrides on another platform's assets.
**Full** and Modern's **Custom** both use the extended scene pass.
Original course-specific exceptions
apply when their source course is loaded. See [native distance verification](port-notes/native-distance.md).
With **HD textures** enabled, Modern uses matching replacements from the selected pack at every scale, including 1×.
The hardware profiles require a scale above 1×, including when Auto selects a higher scale.

Modern's **Track Detail** applies at both draw distances. **Native** (the
default) preserves the usual road detail at each distance. **Medium** raises
Low tiles to Medium and Medium tiles to High, including Medium detail on the
extended road. **High** uses High tiles throughout the visible road. Both
raised settings use the road packet arena and keep the same section visibility.

Each profile saves its own **Frame rate** mode and **Frames per second** cap.
The hardware profiles default to **Original**. Modern defaults to
**Display refresh (VSync)**, which syncs presentation to the display.
The initial cap when **Capped** is selected is 144 fps.
`WIPEOUT_FRAME_RATE` can still override presentation pacing for the current run.

**Frame rate > Original** shows the cadence for the profile being edited.
PlayStation uses 30 fps (25 in PAL mode), Nintendo 64 uses 30 fps,
Sega Saturn uses 20 fps, and PC and Modern use 60 fps. Changing profiles
switches the pacing settings at the frame boundary.
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
| PC | Bilinear texture filtering, dithered RGB565, scenery/ship lighting, depth and linear display resampling on |
| N64 | Three-point filtering, RGB5 precision, magic-square dithering, VI dedithering, approximate divot filtering and linear display resampling on; depth always on |
| Modern | Auto resolution, full-precision projection, perspective-correct world textures, bilinear world texture filtering, depth and linear display resampling |

Snapping and colour precision are independent. Dithering requires RGB5 precision;
N64 dedithering does too. Disabled dependencies retain their saved values.
Turning texture filtering off selects nearest sampling. PC lighting is independent
of filtering, perspective mapping and depth.

Each profile has a **Linear display resampling** checkbox. Enable it to blend
neighbouring pixels when scaling the final image to the window; disable it for
sharp nearest-neighbour scaling. This affects the scene and HUD independently of
texture filtering. The saved keys are `ps1_resample`, `pc_resample`,
`saturn_resample`, `n64_resample` and `modern_resample`. Missing keys retain the defaults above.

Profiles control all materials, including full-colour pages and glyphs, while
preserving source palette, addressing, modulation and transparency encodings.
N64 filters projected world textures (including billboards), but samples HUD,
menus and glyphs with nearest filtering; its display processing includes the HUD.
World shaders use explicit texel reads and explicit dithering, with driver
dithering disabled. See [PC evidence and limits](PC.md#rendering-evidence-and-limits)
and [N64 rendering](N64_RENDERING.md) for fidelity limits.

## Modern profile

Choose **Modern** in a platform's renderer selector or set
`WIPEOUT_RENDERER=modern` for a single run. The profile uses the original
textures and models with full-colour rendering and Auto resolution.

**Bilinear texture filtering** is enabled by default and blends neighbouring
texels on projected world textures, including billboards. Uncheck it for
nearest-neighbour sampling. HUD and 2D menus always use nearest sampling.
The saved key `modern_bilinear` uses 1 for enabled and 0 for disabled.

**Smooth track lighting** (`modern_smooth_track_lighting`, on by default)
blends face lighting along connected track surfaces using perspective-correct
vertex colours. Ships sample the blended road tint beneath them. Road and walls
keep separate lighting, and weapon grids, boost pads, pit lanes and checkpoints
keep their original colours. Turn it off in **Graphics → Modern** to restore
the original per-face tint on the track and ships.

**Brightness** (`modern_brightness`) scales the tint on world textures before
colour clipping. The default is **85%**. **100%** uses PlayStation modulation.
**50%** halves the tint, approximating the PC modulation on PlayStation
textures. HUD and menu art use their own brightness.

**Dynamic lights** (`modern_weapon_lights`, on by default) adds coloured,
unshadowed point lights to rockets, missiles, plasma shots and electrobolts,
plus fading explosion flashes. Each live ship emits subtle blue light from its
engine. An active shield lights nearby surfaces and casts soft light onto its
ship's hull. The light brings out the hull texture in shadow and brightens at
grazing angles. Both effects follow the shield's hit flashes.
Holding an unused shield does not emit shield light. Up to sixteen lights per view illuminate
opaque track, scenery and ships, preserving their original shading. HUD, sky,
shadow decals and translucent effects do not receive this lighting. Lights
follow interpolated geometry and are independent for linked players. This
setting works with HDR bloom off; without half-float targets it uses SDR.
Lights do not cast shadows and can illuminate through nearby walls.

**HDR bloom** (`modern_bloom`, on by default) uses the light above SDR white to
add glow to Modern races. It also boosts the animated neon in Phenitia Park
and a tunnel bulb in Gare d'Europa on PlayStation tracks. The renderer adds
the glow in linear light and maps the highlights to SDR for the window and
frame captures. Opaque HUD pixels retain their original colours. Systems
without a renderable half-float scene target use the SDR path. Set
`WIPEOUT_MODERN_BLOOM=0` to disable it for one run or `WIPEOUT_MODERN_BLOOM=1`
to enable it for one run. The checkbox controls the saved preference.

While racing, open the debug console with the backtick key and use
**Graphics → Modern → Tonemapper** to compare the two presets. The control
requires HDR bloom or dynamic lights. **Classic** is saved as `original`.
**Soft Highlights** (`pbr_neutral_soft_shadows`) is the default. It uses Classic
through a linear RGB peak of 0.15 and smoothly blends into full PBR Neutral
by 0.75, favouring Classic in the midtones. The selection is saved as
`modern_tonemapper`.
[Khronos PBR Neutral specification](https://github.com/KhronosGroup/ToneMapping/tree/main/PBR_Neutral)

## PC profile

**Dithered RGB565** (`pc_rgb565`, default on) applies 4×4 ordered dithering to the
composited picture at the selected render resolution, with five bits each for
red and blue and six for green. Display scaling and capture use the quantized
picture. The scene still blends at RGBA8 precision, so overlapping effects can
differ from a real 16-bit framebuffer.

**PC draw distance** (`pc_draw_distance`, default 1) selects the original F3
level from 1 to 8 for road and scenery visibility in Native mode. Full mode
extends the scene independently of this level and supports the shared scenery fade.
See [PC rendering evidence](PC.md#rendering-evidence-and-limits).

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
Enable **Graphics > Sega Saturn > Enhancements > Camera-aligned sky** to make
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

## Settings

| INI value | Meaning |
| --- | --- |
| `modern_for_all_platforms` | Use Modern for every platform when set to 1 (default) |
| `modern_custom_distance` | Modern's Custom draw distance in world units. |
| `modern_track_detail` | Modern's road detail. Use `native` (default), `medium`, or `high`. |
| `ps1_renderer`, `pc_renderer`, `n64_renderer`, `saturn_renderer` | Profile for that platform: `ps1`, `pc`, `n64`, `saturn` or `modern`; defaults to the matching profile |
| `ps1_*`, `pc_*`, `saturn_*`, `n64_*`, `modern_*` controls | Independent profile settings |
| `ps1_render_scale`, `pc_render_scale`, `n64_render_scale`, `saturn_render_scale`, `modern_render_scale` | Profile resolution multiplier, limited by GPU capacity. Use 0 for Auto. |

`WIPEOUT_RENDER_SCALE` overrides all profile multipliers, including 0 for Auto.
`WIPEOUT_DEPTH_BUFFER` overrides PC depth.

`WIPEOUT_RENDERER=ps1|pc|n64|saturn|modern` takes precedence for the run.
`auto` follows the saved renderer settings. The INI stores each platform's chosen
profile. Hidden regression runs use defaults and environment overrides and skip
reading and saving player settings.

## Implementation

- `platform/render/profiles.def` registers CPU policies, GL pipelines, native
  sizes and Original cadence in one place. `renderer.[ch]` owns defaults,
  persistence and the committed frame. A frame owns a copy of just its active
  profile, and the debug panels edit the requested settings separately.
- `{ps1,pc,saturn,n64,modern}_gl.c` own their scene programs and presentation
  passes. Saturn also owns its native quad queue, software reference rasterizer
  and VDP2 sky. It submits through the same pipeline interface and uses the
  backend's draw area, framebuffer and picture tracking.
- `gl_backend.c` handles texture uploads, batching and framebuffers.
  `gl_program.c` shares fullscreen state setup, scratch-target allocation and
  picture cache keys. PC RGB565, N64 VI and Modern bloom keep their own passes
  and caches. Shared shaders decode assets and blend, and HD replacements
  preserve source addressing.
- `platform/material_source.[ch]`: source fixture identities, emission conditions and
  UV masks, and the packet-builder hooks that attach native material/recorder metadata.
  These facts do not depend on which profile is active or whether bloom is enabled.
- `platform/psx/libgpu.c` decodes packets and uses the active profile's packet
  policy for native art. Clipping, batching and frame inspection share material
  classification. Profiles choose texture sampling and coverage independently
  before the backend packs them for the shaders.
- `game/src/portfar.c` selects explicit road, scenery and ship visibility
  policies. They share traversal and packet emission where possible, preserving
  each source's rounding, visibility history and authored draw order.

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
recordings retain a separate settings snapshot for `renderer.ini`. Binary format
5 stores logical picture extents
separately from captured pixel dimensions, so Saturn and PC images can be inspected
in game coordinates. Format 4 added per-primitive shading flags for
affine/perspective interpolation and snapped coverage, with perspective fade.
The live inspector and `tools/framecap.py` use these flags. Python also reads older
formats, using available INI policy for interim version 3 and historical defaults
otherwise. Texture inspection estimates one native texel; it does not replay HD,
filtering or presentation effects.

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
[consolidated change record](port-notes/renderer-changes.md) and
[architecture refactor notes](port-notes/renderer-consolidation.md).

Boundary checks reject private profile settings and direct Saturn or Modern
dispatch in shared GL transport and the packet adapter. Source-material tests exercise
all five profiles, bloom toggles, animation, capture, replay and packet reuse.
Standalone profile tests link both CPU policy and GL code without the registry,
backend or other profiles, exercising private batch bindings, HDR presentation
and repeated destruction/recreation.
