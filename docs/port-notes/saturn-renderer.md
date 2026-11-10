# Saturn renderer profile

The global renderer now offers Sega Saturn, with independently saved snapping,
affine mapping, RGB5 precision, mesh transparency and optional depth settings.
`WIPEOUT_RENDER_PROFILE=saturn` selects it explicitly; the automatic N64 track override
also accepts Saturn as its base. See [profile details and limits](../RENDERERS.md#saturn-profile).

## Changes

- `platform/render/renderer.[ch]`: append the Saturn ID without renumbering existing
  IDs; add defaults, registry/profile dispatch, persistence and frame-boundary comparison.
- `saturn_renderer.c`: material and geometry policy, preserving source encoding and
  compatibility lighting; native ship draws receive the road-light input. UI excludes inferred mesh coverage.
- `saturn_gl.c`: independent scene/presentation resources, nearest sampling and
  scaling, undithered RGB555 expansion and native-pixel mesh for half-blended world fragments.
- `shaders/material_fragment.h`: optional owner-supplied coverage hook after pass
  classification. Existing renderer shader output is unchanged.
- `gl_pipeline.[ch]`: register the independent Saturn pipeline.
- `renderer_ui.cpp`: selector entry, independent controls/reset and explanatory tooltips.
- `CMakeLists.txt`, `tools/bugtest/run.sh`, `tools/bugtest/render.sh`: link the new
  profile in the game, both players and regression binaries; test independent linkage.
- Renderer settings/UI and production shader tests: persistence, environment override,
  frame commits, N64 override, controls/reset, RGB555, alternating mesh pixels,
  mixed opaque/semi texels, pass splitting, depth eligibility and unchanged additive output.
- `README.md`, `docs/RENDERERS.md`: selection, controls and fidelity limits.

## Native disc rendering

`platform/render/saturn_native.[ch]` owns native model/track material copies and
packet bindings. Binding payload addresses with collision resolution, and clearing
a binding when AddPrim reuses its packet, avoids stale material identities. Source
textures remain available before the compatibility converter resamples collapsed
quads. The renderer steps A-D and B-C, draws connecting lines, and preserves repeated
writes. Tests cover rotated coverage, collapsed sides, a one-pixel quad, magnified
texels, clipping without UV stretching, transparent index zero, mesh, end codes,
source half luminance and destination-shadow requests.

The Saturn loader now correctly names the model record's +12 textured/+8 flat word
CMDPMOD. The original command builder copies this word into command +4; it is not a
Gouraud address. Lit records additionally contain a runtime shade-table offset.
Native track materials keep the source face palette rather than fitting a shared
palette and a multiplicative tint. The face LUT is already lit; the separate road
shade must not be added to it again. Non-native profiles still
use the existing converted assets.

Evidence from the locally supplied European disc's `0.AAA`, loaded at 0x06010000:

- 0x0605e08c–0x0605e0ac copies flat material/mode words and selects the runtime
  Gouraud table. Textured command records have the corresponding extra texture words.
- 0x060143ba–0x060143d8 and 0x0601487c–0x0601488e construct track CMDPMOD as
  0x1488 OR the face's SPD bit. Neither enables Gouraud. The earlier renderer's
  forced 0x8c applied lighting twice, crushing Sagarmatha roads to black and
  overbrightening Talon's Reach. This was found during the outdoor race validation.
- 0x0603f2a6–0x0603f32a reads the ship's road shade and builds corners
  `0xc210`, road shade, per-channel `max(road−4,0)`, `0xb18c`.
- 0x0602550c–0x06025528 configures RBG0 and its two parameter tables; the
  descriptor at 0x0604427c points to characters at 0x25e40000 and names at 0x25e00000.
- 0x0602466c–0x0602471a sets the rotation parameters: heading/2, 120−pitch/3,
  pivot (176,120), unit scale and the roll matrix. The host derives the angles
  from the background's view matrix and aligns the pivot to its viewport centre.

The full panorama and its palette are copied before the compatibility cylinder is
built. `libgpu.c` selects native data only with a Saturn disc and active Saturn
profile; `gl_backend.c` composites emitted native pixels in an ordered premultiplied buffer,
flushed as a texture rectangle before compatibility draws, clears, readback or
presentation. This preserves interleaved draw order without six GPU vertices per
pixel. Optional depth keeps a one-point-per-pixel path. `saturn_gl.c` owns the
composite and panorama shaders/resources; the panorama is one GPU draw. Existing GL shaders and
presentation for other renderers remain independently linked.

Hardware references: Sega's [distorted sprite command](https://www.infochunk.com/saturn/segahtml_en/hard/vdp1/hon/p07_60.htm)
and [end-code rules](https://www.infochunk.com/saturn/segahtml_en/hard/vdp1/hon/p06_34.htm).
Extracted executable/archive data and captures stay outside the repository.

## Validation and limits

`sh tools/bugtest/run.sh`, `sh tools/bugtest/render.sh` and
`cmake --build build/mac -j 2` pass on macOS.
The native raster unit test also runs with AddressSanitizer and UndefinedBehaviorSanitizer.
GPU readback verifies additive RGB555 native pixels and the background's full-resolution
sampling, wrapping, signed heading offset, pitch and roll. Disc-backed autopilot
races exercise Talon's Reach and Sagarmatha, including tunnels, banks, outdoor
backgrounds and lap transitions, without reading or writing player settings/saves.

No game sources or generated assets change; MIPS routine-equivalence checks do not
apply. No complete matched-frame original-Saturn comparison has been performed;
the sky's camera convention and VDP2 parameters have been checked against an
original-game emulator state (see below). The port's
projection, ordering/subdivision and converted HUD remain. Near-plane cuts now
retain native materials and interpolated native UV/light attributes, including
separate UVs at collapsed model corners. Host-created clipping triangles rasterize
only their visible bounds, preventing huge projected edges from causing repeated
CPU work. Their clipping and edge coverage are not hardware-equivalent; runtime logo animation, full VDP2 priority/window composition and hardware
feedback/rounding are not implemented. Ship lighting decodes the host road colour,
so saturated source light is lossy. See [renderer limits](../RENDERERS.md#saturn-profile).

## Performance and artifact repair (2026-11-06)

- `saturn.c`: corrected prelit track CMDPMOD using the original executable evidence above;
  empty buffer appends return without null-pointer arithmetic (caught by the synthetic
  course loader's UBSan run).
- `saturn_native.[ch]`, `psx/gpu.h`, `psx/libgpu.c`: retain native attributes at
  near-plane intersections; expand collapsed model corner attributes before clipping;
  bound generated triangle rasterization to the viewport.
- `gl_backend.c`, `saturn_gl.c`, `gl_pipeline.h`, `gl_program.c`: ordered CPU
  compositing and rectangle uploads, single-draw GPU background, and a point path
  for optional depth. Existing renderer draw modes and shaders remain separate.
- Regression tests cover the production course loader's prelit dark colours,
  huge clipped triangles, additive clipping interpolation, native/compatibility
  ordering, repeated transparency and shadows, background sampling and lifecycle.

Race validation uses the locally supplied European disc, scripted controller input,
autopilot, isolated saves and hidden windows. Captures and timing logs remain in
`/tmp`, outside source changes. Fixed-step runs measure throughput rather than a
real-time presentation guarantee. No matched Saturn hardware capture is available.

Final Sagarmatha validation: 6,000 real-time fields, 8× internal render scale,
60 FPS cap, 2560×1920 window, original European disc. The run completed and capture
inspection confirmed textured roads through lap 2. One-second timing samples after
40 seconds measured median 47.34 displayed FPS (35.89–60.80) and median 29.97 game
updates/sec. The wider game-update range (23.49–37.03) includes capture stalls and
catch-up; this is **not a locked 60 FPS claim**. Logs/captures:
`/tmp/saturn-realtime-verified/`. This measures the host renderer, not Saturn equivalence.

The earlier fixed-step Talon's Reach run completed 6,000 fields with 81–138
presentations/sec during its measured race segment at 2× scale; that run preceded
the final prelit palette correction. The corrected Sagarmatha fixed-step run also
completed 6,000 fields at 8× (`/tmp/saturn-sagarmatha-corrected/`).

Final Talon's Reach validation also completed 4,000 real-time fields at 2× scale,
with the corrected prelit palettes. Indoor capture inspection confirmed retained
road detail and native clipped materials (`/tmp/saturn-talon-verified/`).
The native raster and production synthetic course-loader tests both passed ASan
and UBSan with the final loader fix. No source files under `game/` changed.

## GPU quad path (2026-11-06)

Native quad rasterization and per-pixel shading now run on the GPU by default.
`WIPEOUT_SATURN_REFERENCE=1` retains the CPU rasterizer as a comparison path. Both
use the same GPU VDP2 panorama. The CPU still prepares command and row descriptors;
it does not emit individual pixels in the default path.

`saturn_gl.c` batches commands in draw order and instances narrow strips for each
joining line. The shaders implement integer coverage, connecting pixels, original
source indices, per-command palettes, RGB555 additive shades, mesh, end codes,
source half luminance, half blends and shadows. Host-created near-plane triangles
use bounded fragment coverage with native UV and shade interpolation. Optional
depth uses the host depth buffer; opaque and translucent commands preserve their
different depth-write policies.

Native textures are cached in a renderer-owned R8UI atlas, invalidated by the
material generation on load/replacement. Commands use a streamed 16 KiB-compatible
uniform buffer, supported by the existing OpenGL 3.3 / ES 3 API baseline. The first
GPU prototype used a command texture; profiling on macOS showed its updates
forcing repeated render-pass submissions. The uniform buffer removes that cost.
No shader compilation or source texture uploads occur per polygon after warmup.

Race profiling also exposed severe clustering in the native packet-to-material
lookup table. Its former low-bit address fold caused long linear-probe chains in
primitive arenas. Full-address mixing removes that CPU bottleneck for both the GPU
and reference paths. Conservative CPU geometry culling omits joining-line instances
whose bounds cannot reach the viewport; exact coverage and shading remain on the
GPU. These fixes are necessary alongside the rasterizer change.

CPU and GPU attribute sampling share a 1e-5 texel boundary tolerance. This avoids
selecting adjacent texels when equivalent interpolation lands just below an integer
on one processor. This is host arithmetic stabilization, not evidence of original
VDP1 rounding equivalence.

Validation on Apple M4 Max, macOS, RelWithDebInfo:

- `cmake --build build/mac -j 2`, `sh tools/bugtest/run.sh`, and
  `sh tools/bugtest/render.sh` pass. The GPU suite checks 18,432,000 RGB channels
  against software reference output at 1× and 2×, with a tolerance of two 8-bit
  levels. Cases include collapsed/reversed/crossed quads, mesh, end codes, shading,
  half blends/shadows, packed VRAM/window sampling, clipped triangles and fades.
  Additional checks cover multiple command-buffer turnovers, optional depth and
  mixed translucent/opaque ordering. Other renderer and isolation tests pass.
- Matched uncapped disc races with the final lookup/culling fixes:

  | Track | Scale | Software reference | GPU | GPU gain |
  | --- | --- | --- | --- | --- |
  | Talon's Reach | 2× | ~211 FPS | ~304 FPS | ~44% |
  | Sagarmatha | 8× | ~155 FPS | ~233 FPS | ~51% |

  These are time-weighted presentation rates from one-second CSV samples in the
  racing segment (roughly fields 2,500–5,500 for Talon and 2,500–6,500 for
  Sagarmatha, reconstructed from cumulative field rates). Runs use the same pad
  script and fixed simulation steps; Talon runs include identical capture points,
  while Sagarmatha throughput runs omit captures. Both reference and GPU include
  the lookup fix. The previous software measurements were ~112 FPS and ~90 FPS
  respectively; the combined changes improve those by approximately 2.6–2.7×.
  These are local throughput measurements, not real-time pacing guarantees.
  Logs: `/tmp/saturn-{gpu,ref}-hash-talon/` and
  `/tmp/saturn-{gpu,ref}-hash-sag/`.
- Talon GPU/reference runs complete 6,000 fields through lap 2. Six matched
  2560×1920 captures differ in at most 0.00521% of RGB channels, with maximum
  whole-frame mean absolute error 0.00108/255. Two are byte-identical. Remaining
  differences are isolated texture/edge/interpolation pixels (maximum individual
  channel difference 41); this is not a bit-exact framebuffer claim. The culling
  and lookup fixes preserve every byte of the earlier GPU captures.
- Sagarmatha GPU/reference runs complete 7,000 fields at 8×. Earlier matched
  captures at 2× differed in at most 0.176% of RGB channels. Visual inspection
  includes road detail, ships, indoor/outdoor backgrounds and lap transitions.
- Final capped Sagarmatha validation: 4,000 real-time fields, 8× internal scale,
  2560×1920 presentation, 60 FPS cap. Race samples after 40 seconds measured
  median 59.73 displayed FPS (56.99–60.93) and 29.97 game updates/sec
  (29.81–30.23), including capture stalls. This is near 60, not a claim that every
  frame meets its deadline. Captures show the original panorama, shaded ships,
  textured roads and lap progression. Logs/captures:
  `/tmp/saturn-gpu-validated-realtime/`.
- Final native raster tests also pass AddressSanitizer and UndefinedBehaviorSanitizer.

The scripted pad fixtures `tools/bugtest/saturn_talon.pads` and
`tools/bugtest/saturn_sagarmatha.pads` navigate the default menus with isolated
saves. Example from the repository root (create the output/save directories first):

```sh
WIPEOUT_XL_DISC='../roms/Sega Saturn/WipEout 2097 (Europe).cue' \
WIPEOUT_HIDDEN=1 WIPEOUT_MUTE=1 WIPEOUT_SKIP_INTRO=1 WIPEOUT_SKIP_LOGOS=1 \
WIPEOUT_RENDER_PROFILE=saturn WIPEOUT_AUTOPILOT=all \
WIPEOUT_SAVE_DIR=/tmp/saturn-validation-save \
WIPEOUT_PADS=tools/bugtest/saturn_talon.pads \
WIPEOUT_FIXED_STEP=1 WIPEOUT_FRAMES=6000 WIPEOUT_RENDER_SCALE=2 \
WIPEOUT_CAPTURE_DIR=/tmp/saturn-validation-gpu \
WIPEOUT_CAPTURE_FROM=2400 WIPEOUT_CAPTURE_EVERY=600 \
WIPEOUT_TIMING_LOG=/tmp/saturn-validation-gpu/timing.csv \
build/mac/wipeoutxl
```

For matched captures with the current renderer, use `WIPEOUT_RENDER_SCALE=1`
for both runs and repeat with `WIPEOUT_SATURN_REFERENCE=1` and a separate
output directory. The GPU now rasterizes at the selected scale while the CPU
reference draws on the 1× grid. For real-time pacing, omit `WIPEOUT_FIXED_STEP` and add
`WIPEOUT_FRAME_RATE=60`; presentation-numbered captures then do not identify the
same simulated instant across runs. For throughput comparisons omit capture
variables to avoid readback and file I/O stalls. These tests compare against the
retained reference, not Saturn hardware; the fidelity limits above still apply.


## Sky redraw and camera yaw repair (2026-11-06)

The sky's once-per-pass flag used to reset only in `GpuClearDepth`. Interpolated
pictures clear their render target through `GpuClearArea`, leaving the old flag
set and skipping the panorama. Resetting the flag in the shared clear operation
restores the sky for both normal and interpolated pictures, while repeated sky
polygons within one pass still draw only once. The regression clears and redraws
both screen-buffer regions and checks that subsequent sky packets cannot cover
foreground geometry.

The view matrix already rotates world positions opposite the camera. Negating its
extracted yaw a second time reversed panorama scrolling. The GTE sky metadata now
uses the view-matrix heading directly; increasing the source sample offset moves
panorama landmarks left during a clockwise camera turn. Regression checks cover
both camera turn directions and movement of a distinguishable panorama landmark.

`tools/bugtest/saturn_valparaiso.pads` selects Venom/Valparaiso from default menus.
Race validation explicitly uses `WIPEOUT_INTERPOLATE=1` with fixed steps: hidden
fixed-step tests otherwise skip interpolation and cannot reproduce this bug.
A pre-fix captured sequence reproduced a completely black sky band every fourth
picture. Valparaiso validation uses the European Saturn disc, isolated saves,
autopilot, 2× internal scale, and both consecutive and longer race captures.
The 100 consecutive Valparaiso captures contain no black-sky dropouts; inspection
of the changing top band distinguishes the passing start gantry from sky pixels.
The longer run covers 5,000 fields through lap 2. The macOS build, CPU suite and
GPU suite pass. Artifacts stay under `/tmp/saturn-val-sky-{after,consecutive}/`.

## Original-game sky calibration (2026-11-06)

The European disc was run in OpenEmu's Mednafen core. Keyboard input did not
reach the game, so the reference is its running attract-mode race, not a
manually controlled matched race. The captured state identifies course 20
(Phenitia Park) at `0x06088658`; its sky map and character data exactly match
`sky20.rbh` and `sky20.vri`. Valparaiso and Phenitia Park share the RBG0 setup
routine at `0x0602466c` (the separate course-17 effect does not apply).

In that state, camera globals at `0x060734dc` are pitch=-303, heading=459,
roll=0. The 16.16 view matrix at `0x0607b804` is:

```
 49951      0  -42424
-19017  58583  -22391
 37923  29376   44651
```

Its extracted matrix elevation is +303: negating it is required to recover
Saturn camera pitch before applying `My=120-pitch/3`. Previously the sky moved
opposite the scenery vertically, exaggerating their relative movement.
The GTE regression includes this captured matrix as well as both pitch/yaw
turn directions. No disc data or state files are checked in.

The live VDP2 state confirms HRes=1, VRes=1, noninterlaced output (352x240),
unit `DX`, `DYst`, `kx`, `ky`, and `P=C=(176,120)`. Its active parameters contain
`Mx=224`, `My=237`; the RAM table is already being prepared for the next frame,
so it must not be compared directly to the active display parameters. The
original executable's heading/2 and pitch/3 rates are retained. The host's
320x240 coordinates now map to 352x240 Saturn dots, reducing horizontal
screen displacement by 320/352 (about 9.1%) and preserving the original
panorama field of view. Widescreen extends the mapping instead of stretching it.
The source-dot conversion preserves exact integer boundaries to avoid wrapping
X=0 to the opposite edge through floating-point rounding.

The transform follows Sega's [rotation scroll coordinate equations](https://www.infochunk.com/saturn/segahtml_en/hard/vdp2/hon/p06_10.htm).
The GPU regression uses an asymmetric 2048x512 pattern, signed heading/pitch
changes, and both display buffers to check the original sampling rate and
coordinate conversion. Small repeated textures alone cannot distinguish some
sign and magnitude errors.

Validation: the CPU bug suite, GPU shader/isolation suite, and macOS build pass.
A disc-backed Valparaiso autopilot race completed 5,000 fields at 2x render scale
with interpolation enabled. Saved 311 captures spanning presentation frames
5000-8100; inspected samples covering the start, slopes, turns and tunnels. This is a host race
smoke test plus original-state calibration, not full-frame Saturn equivalence.
Local evidence: `/tmp/saturn-sky-original-reference.json`,
`/tmp/saturn-original-demo.state`, `/tmp/saturn-val-sky-angle-fixed/`, and
`/tmp/saturn-sky-angles-{cpu,gpu,build}.log`.

## Sky motion follows the world camera (2026-11-06)

The preceding calibration checks the original flat RBG0 scroll transform. It
was insufficient to make sky landmarks track the port's 3D scenery: at the
screen centre, heading/2 with the 352:320 dot mapping moves the sky about
296 pixels/radian, versus the world's 160 pixels/radian. The user explicitly
selected camera-following motion over literal Saturn scrolling.

The camera-aligned sky shader unprojects each scene pixel using the captured GTE
focal length, applies the inverse view rotation, and samples panorama longitude
and elevation. It retains the original artwork, neutral sample (176,240), and
angular extents. It does not simply reduce the heading multiplier: a complete
camera revolution still wraps through exactly one panorama, with no orientation
seam. Pitch and roll use the same ray rotation. Widescreen adds viewing rays
without changing their angular scale. The Saturn profile defaults to the original
flat mapping and offers camera alignment as an enhancement.
`WIPEOUT_SATURN_VDP2_REFERENCE=1` forces flat mapping independently of the
enhancement and the VDP1 software reference.
This is an intentional camera adaptation, not a claim of literal VDP2 equivalence.

The sky pose also interpolates between game frames. A single previous sky pose
is retained per game instance; yaw and roll follow the shortest angular path.
The world's existing camera-cut detection suppresses interpolation across cuts.
No console camera state or gameplay routine changes.

GPU tests forward-project fixed world landmarks independently and check the
sky at those same screen pixels. Cases include off-centre landmarks, both turn
directions, combined pitch/roll, two focal lengths, both screen buffers, and
heading values differing by a full revolution. The literal VDP2 sampling tests
remain enabled in reference mode. CPU tests cover focal capture, original angle
conventions, interpolation across the yaw seam, and camera-cut/disabled guards.

Validation: the full CPU bug suite and production GL shader suite pass, as does
`cmake --build build/mac -j 2`. A 5,000-field Saturn-disc Valparaiso autopilot
run completed with interpolation enabled. It captured 311 presentations at 2x
scale (presentations 5000–8100, every tenth); inspected samples cover the race
start, turns, slopes, tunnels and return to open sky. This validates the host
camera adaptation, not equivalence to original Saturn scrolling. Local evidence:
`/tmp/saturn-val-sky-world/`, `/tmp/saturn-world-contact.png`, and
`/tmp/saturn-sky-world-{cpu,gpu,build}.log`.

A sequential 8x-scale, 5,000-field Valparaiso comparison used the same rebuilt
binary and fixture without captures, with interpolation and fixed-step throughput
measurement enabled. Excluding the first eight seconds of menus/loading, the
world-camera mode averaged 188.1 displayed FPS (median 183.0); the literal-plane
mode averaged 188.7 (median 187.6). This single-run comparison found no substantial
throughput regression; it is not a controlled statistical benchmark. Logs:
`/tmp/saturn-{world,flat}-performance.csv`.

## Review fixes (2026-11-07)

- `saturn.c` restores the road and weapon palettes and calls
  `SaturnLoadNativeSky` when a course WAD opens, including cache hits.
  `saturn_models.c` writes a stable native material tag to each converted
  object's scratch word. Cached PRMs retain the tag and the registry keeps
  the corresponding material until shutdown. Scene names still serve the
  game's object lookups. Texture-only conversion does not register models.
- `objdraw.c` passes the scratch word to `PortTagObjectPrims` from
  `DrawObject` and its near-fill path. `framecap.c` decodes the tag and
  binds its material to the emitted packet. `libgpu.c` keeps the atlas UVs
  for frame inspection and reconstructs native UVs on the expanded corners
  during clipping. Guest tracks keep their own sky and face materials.
- `saturn_gl.c` supplies GLSL ES fragment precision for the quad, panorama,
  and compositor programs. `saturn_gl.c` and `saturn_native.c` interpolate
  reciprocal depth across native surfaces for shared depth testing.
  `PortWeaponGridVisual` wraps the gameplay angle before selecting a palette,
  including when the signed angle counter wraps.

Both bug suites and the macOS build passed. Chrome WebGL 2 compiled and linked
all three native shader programs. The European Saturn disc probe loaded
courses 01, 06, and 01 and confirmed restoration of the first course's sky,
face palettes, and per-instance `wall_c4` materials. It also checked weapon
pad depletion and recharge. The Talon's Reach smoke run completed 3,500
fields at 2× render scale.

Compiler diagnostics were clean for `objdraw.c` and `weapon.c`. The local
`B0` oracle snapshot was unavailable, so the MIPS comparison used `HEAD`.
`BindSpriteCorner` and `Culled` differ because the working tree's existing
`GtePrecise` sky fields change its layout. `DrawObject` includes that layout
change and the new packet tag argument, including its inlined near-fill
call. `CycleWeaponGrids` includes the existing visual phase hook.

## Foreground track distortion at race start

The Talon's Reach start camera projects some near track tiles far outside the
viewport. In a 6× capture at the starting grid, one tile produced 1,300 visible
VDP1 joining lines. These lines overlapped across the viewport and used
screen-linear UVs. The road could stretch and shift as the camera moved, while
the extra fragment work raised frame time.

Native road tiles now submit two hardware triangles per quad in one instance.
Perspective-correct UVs keep the texture attached to the projected road.
Near-plane intersections interpolate UVs in view space. The software reference
uses the same interpolation and clips its work to the viewport. Road sampling
keeps the Saturn per-face palette and nearest texels. Models and effects keep
their VDP1 joining lines.

In two matched 6× runs per path with fixed steps and capture disabled, the
95th-percentile presentation time across the Talon's Reach fly-in dropped
from 12.69–12.73 ms to 10.02–10.08 ms. Foreground captures covered the fly-in
and starting grid. The regression tests cover the near-plane UVs, a steep
textured tile in both rasterizers, and a tile whose projected corners extend
beyond the forward rasterizer's coordinate range.

## Native race material animations

`ColourDroidLights` publishes one material tick per active race update.
`saturn_native.c` resets this clock with the native course and uses it for
palette changes and smoke frame selection. `libgpu.c` applies palette changes
to the Saturn draw's material copy before clipping. Both rasterizers consume
the same selected artwork.

`saturn_models.c` identifies animated palettes using the five source words
the European executable uses to find their VDP1 locations. Phenitia Park's
three neon palettes alternate between their source values and values dimmed
by eight per channel (0x0603d574 and 0x0603d808). Valparaiso's waterfall
rotates entries 4 through 15 (0x0603d4e8). The rescue droid pulses two light
ramps (0x0603da04). The pulse calculation reproduces the channel levels
from the original sine table for every angle.

Talon's Reach loads all 13 smoke sprites from course block 4 using the
shared sprite descriptor reader in `saturn_art.c`. The object-frame registry
binds these to the existing emitter geometry. The two emitter rates follow
the host game's slow and fast smoke steps. The palette cycles also run on
the host race update clock.

The macOS build, CPU and GPU suites, and native ASan and UBSan tests passed.
European disc captures cover Talon's Reach, Valparaiso, and Phenitia Park.
All pixels affected by each animation in four representative captures matched
between the GPU and software paths. The 51 PS1-profile comparison captures
matched the earlier build byte for byte.
The MIPS comparison against `HEAD` found the new visual tick in
`ColourDroidLights` and the existing object-frame changes in `DrawDroid`,
`ResetLightStart`, and `UpdateLightStart`. Compiler diagnostics for
`effects.c` were clean.

## Shield interpolation and compatibility light blending (2026-11-07)

`libgpu.c` now freezes the frame's packet metadata in `PsxUpcomingTables`, before
Swap's VSync waits can present interpolated pictures. Previously only the final
`DrawOTag` refreshed it. Reusing an ordering-table address then paired the new
primitive arena with two-frames-old shield/hull ranges and release points. Some
shell faces were covered by the hull or held without reaching their release
packet. All presentations now share the current frame's snapshot; direct table
draws still prepare their metadata on demand. This also keeps shadow, track and
native exhaust metadata current during interpolation, without changing GP0 links.

`saturn_gl.c` applies mesh coverage independently of additive colour blending.
Previously all covered compatibility effects became opaque replacements. Dark
Gouraud ray edges could erase the bright flare centre or earlier trail light.
Additive and quarter-additive effects now preserve their operation; disabling
checkerboard coverage halves their contribution without attenuating the background.
Native VDP1 command modes and the other renderer shaders are unchanged. The
renderer guide, README and checkbox tooltip describe the half-coverage option.

Validation:

- `cmake --build build/mac -j 2`, `sh tools/bugtest/run.sh`, and
  `sh tools/bugtest/render.sh` pass. The packet regression rebuilds the same OT
  address with different shield/hull packets, presents it three times before its
  final draw, and checks expiration on the first interpolated picture. GPU
  readback covers blue light over a black shadow, a dim ray over a white centre,
  checkerboard gaps, smooth coverage, quarter-additive weights and mixed native
  flare/compatibility trail draws in GPU and software-reference modes.
- Compiling the new tests against the pre-fix staged `libgpu.c` and `saturn_gl.c`
  fails at the stale hull release point and incorrect additive output respectively.
- Matched European Saturn-disc Valparaiso races reproduced the dark compatibility
  flare with snapping, affine textures and RGB5 disabled. The corrected captures
  show the bright centre and retained overlapping light. A scripted shield run
  captured 160 consecutive presentations at 2x scale with interpolation enabled;
  the pre-fix sequence shows alternating missing shell coverage and the corrected
  sequence stays stable. These are host regression checks, not full-frame Saturn
  hardware equivalence. Captures, isolated saves, scripts and logs are under
  `/tmp/saturn-effects-fix/`.

No game code or generated data changed; no MIPS routine-equivalence claim is made.

## Billboard palettes and display color (2026-11-07)

Talon's Reach's striped poles were still using the compatibility billboard
palette fit and multiplicative tint. Their original palette has distinct shades
that this fit merges or darkens. `saturn_models.c` now retains the original LUT
and CMDPMOD for billboards whose parts share a material, and registers their
flattened indices through the existing object packet hook. Atlas deduplication
and native pixel ownership remain separate. Mixed-material billboards keep the
compatibility fallback; native selection still requires affine mapping, as for
other native model materials. The composite's existing resolution is unchanged.

Saturn presentation copies scene values directly as sRGB with nearest sampling,
without a display-transfer conversion. This also covers screenshots and
linked-player bands. Native quads, the software composite, sky palettes and
compatibility RGB5 output retain five-bit channels shifted left three bits.
The billboard palette correction is independent of presentation.

Validation:

- `cmake --build build/mac -j 2`, `sh tools/bugtest/run.sh` and
  `sh tools/bugtest/render.sh` pass. The GPU suite includes 18,432,000 native
  rasterizer/reference channel comparisons and a new presentation test for all
  32 channel levels, black/white, crops, both flips, nearest scaling, repeated
  presentation and untouched source pixels. Direct presentation must preserve
  the source channel values exactly.
- The billboard regression checks distinct adjacent dark colors through the
  production converter, registry and rasterizer; atlas deduplication, retained
  ownership and mixed-material fallback are covered. It passes with ASan/UBSan
  and fails against the pre-fix converter because native billboard data is absent.
- A European-disc Talon's Reach run completed 3,600 fields at 2x render scale;
  captures from the initial display-conversion experiment showed the restored
  pole shades. Those captures predate the return to direct sRGB presentation.
  GPU and software-reference runs completed 2,800 fields at 1x. Their two
  full-scene captures agree on more than 99.9% of pixels but are not identical;
  these are host regression checks, not console equivalence. Two matched
  PS1-profile captures remain byte-identical to the pre-fix build.
- Local captures, comparisons, isolated saves and logs are under
  `/tmp/saturn-colour-fix/`. No game code or generated data changed.
