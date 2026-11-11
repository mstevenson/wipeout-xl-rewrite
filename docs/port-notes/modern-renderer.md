# Modern renderer implementation ledger

Date: 2026-11-10. Branch: `modern-renderer`.
Baseline: `c34020f2b14d1caab3f78c99c2b345e605246768`.
Selective reference: `769c546a87d301ae2bc6cabba43fe7b4ed54b5de`.
The five-profile registry and native hardware-specific rendering paths were not
merged. [Usage and ownership](../MODERN_RENDERER.md) describe the resulting path.

## Game-source changes

These are presentation observations or explicitly gated presentation changes,
not claims of representation-only MIPS equivalence. New observation calls can
change machine code even though they do not change simulation. No console packet,
asset, particle, object, face or save structure acquired a field or private bit.

| File | Functions and category/reason |
| --- | --- |
| `game/include/port_platform.h` | Include the narrow Modern game contract; declare upcoming-table camera-cut observation. No PsyQ prototype change. |
| `game/include/portmodern.h` (new) | Typed host observation/sampling contract using real game types. |
| `game/src/portmodern.c` (new) | `PortModernTrackLoaded`, `PortModernHeapReset`, `PortResetTrackLighting`, `BuildTrackLighting`, edge/root helpers: generation reset and host-owned adjacency, exactly two incident compatible edges, marker/crease exclusions. `PortModernTrackColour/LightFormat/Corners/Sample/TagTrackFace`: live RGB and source encoding in sidecars. `FindShipRoadTriangle/Light`, `PortShipTrackLight`: nearest local-route triangle and bounded lane fallback. `PortCaptureWeaponLights`, `Light/View`, `PortSetShieldFlash`, `PortWallSparkLight`: copied, bounded source snapshots, deterministic flicker, no RNG calls. `PortModernWallBegin/End/ParticleSpawn/WallParticle`, `ResetSources`: host-only particle provenance and slot invalidation. `PortModernSceneLoaded/ObjectRgb8`, `CompareObjects`: imported source identity without object flags. |
| `game/src/collide.c` | `NewWingCollision`, `WingCollision`, `NoseCollision`: bracket existing particle calls and observe the wall contact. Original spawn order, arguments, particle types and RNG unchanged. `collideLineToPlane` text unchanged. |
| `game/src/display.c` | `Swap`: publish a camera-update-function change alongside existing upcoming tables; no camera logic changes. |
| `game/src/dynam.c` | `SetTrackAttributes` colour-writer sites: observe PS1-encoded gameplay marker writes instead of consuming a face padding byte. Original colour writes unchanged. |
| `game/src/malloc.c` | `InitDynamicMem`: observe heap reset before writes; release only the owning course's host metadata. Allocation behavior is unchanged. |
| `game/src/objdraw.c` | `ProjectVertices`, `BindSpriteCorner`: source light encoding in precise metadata and copied sprite corners. `GouraudColours`, new `GouraudTextured`: retain authored original-1995 corner colours and source modulation in Modern. `DrawGouraudTexturedTriangle/Quad`, `DrawGouraudTriangle/Quad`: policy dispatch. New `DrawWorldSprite`: upright source-sized quads with source-specific anchor/UV conventions and view keys. `DrawClippedPrimitive`: source tags and bounded Modern arena allocation; `ObjectLight` converts source light encodings in draw temporaries. `DrawObject`, `PortDrawObjectFar`: Modern sprite/detail/colour policy and source tags; existing fixed-slot sky preserved. No source object mutation for provenance. |
| `game/src/partdraw.c` | `AsmDrawParticles`: tag wall streak after `AddPrim`, retaining original line packet and type semantics. |
| `game/src/pc/pcmenus.c` | `DrawMenuModel`: Modern-only horizontal menu-model aspect correction in the existing temporary transform path. |
| `game/src/portfar.c` | `PortFarMarkFace`: retain allocation on failure. `DrawFarShips`: same draw-only sampled road tint as near ships. No general primitive-buffer expansion; Modern object allocation uses the existing bounded far arena from `objdraw.c`. |
| `game/src/portinfo.c` | `PortInfoRaceDrawn`: copy light sources at the race-drawn phase. New `PortActiveTrackNumber`: source-fixture identity query. |
| `game/src/portorig.c` | `PortOriginalSceneInit`: register source objects independently of recording or renderer toggle. |
| `game/src/ships.c` | `DrawShips`: use a temporary sampled road tint only in Modern; original face remains unchanged. |
| `game/src/sparks.c` | `AddRandomParticles`, `AddScrapingParticles`, `AddSnow`, `AddRain`, `AddExplodingParticles`, `AddEboltParticles`, `AddRisingParticles`, `AddFireyParticles`, `AddPitParticles`, `ClearParticleTable`, `InitParticle` (inlined clear): invalidate/tag each reused particle slot, including non-wall and cleared slots. No changes to RNG/type/lifetime writes. |
| `game/src/track.c` | `WADInitFaces`: reset source encoding/topology after face decoding on every load. |
| `game/src/trackdraw.c` | `SetTrackLight` remains original packet RGB; `AsmTrackLo`, `DrawGridQuad`, `DrawNearFace`, `PortTrackFar` tag live road colours after linking. `ShadeTrackGrid`, `TagTrackGridLight`, `AsmTrackMed`, `AsmTrackHi`, `DrawPartialCell`: common triangle colour field over subdivision and clipping. Retain existing shared world keys. |
| `game/src/weapon.c` | `InitShield`, `UpdateShield`: reset/sample flash at shield draw phase and associate shield packet range. `CycleWeaponGrids`: observe marker encoding. New `ShieldPoint`, `TransformTransparentObject`: retain original DVECTOR write and additionally bind precise data in Modern. `GetNewWeapon` text unchanged. |

## Host integration accounting

| File(s) | Reason |
| --- | --- |
| `platform/render/modern/*` (new) | Dedicated policy/UI, scene shader, generation-scoped packet metadata, light interpolation/selection, fixture source rules, portable post passes and lazy resolve resources. |
| `platform/n64_material.[ch]` (new), `platform/original.c` | Preserve relocated imported material bounds/addressing in host records; reset registry on source unload. No new asset format. |
| `platform/settings.[ch]`, `debug_ui.cpp` | Delegate Modern parsing/saving and controls; baseline preferences retained, opt-in default. |
| `platform/trace.c`, `psx/libgpu.c` | Flush/commit at completed boundary. Decoder transports sidecars, source ranges, hull colours/motion and copied lights; clipping carries matching attributes. AddPrim invalidates packet reuse. Integer packet semantics remain baseline. |
| `platform/psx/gte.[ch]` | Read-only shutter/history queries, presentation fraction and source provenance attached to precise host projections. Integer GTE arithmetic unchanged. |
| `platform/psx/gpu.h`, `gpu_renderer.c` | Optional Modern submission/material data, original baseline vertex math, HDR/mask/velocity attachment ownership, revisioned buffer policy, pre-HUD depth capture, shared display/capture resolve, raw SDR preservation, failure/trim/shutdown behavior. |
| `platform/render/device.h`, `state.[ch]` | Portable float formats/MRT, dedicated Modern vertices and optional uniform payload, complete scoped draw-state restore, transactional resource migration and device generations. |
| `platform/render/device_gl.c`, `gl_functions.h` | Lazy Modern programs, float images and MRT transfer, separate vertex layouts and attribute disabling, optional lighting/post uniforms. |
| `platform/render/device_metal.m`, `shaders_metal.metal` | Matching scene/float/MRT behavior and compact baseline uniforms, Modern depth-only routing, optional post uniforms; generated post fragment section only. |
| `platform/link.c`, `CMakeLists.txt` | Copy requested values under existing input lock and compile mutable Modern modules independently for the second game. |
| `platform/video.c` | Destroy Modern resources before the render device. |
| `platform/psx/framecap.[ch]`, `tools/framecap.py` | Immutable frame policy plus optional semantic JSON; preserve XLFC v3 and old readers. |
| `tools/oracle32.py`, `tools/oracle32/include/*`, `o32.h` | Restore freestanding ILP32 headers and compiler resource includes, include host headers in snapshots, use `-G0` on both sides to avoid LLVM 21 GP-relative lowering failure, reject compilation/missing-baseline/empty-selection failures instead of false success. |
| `tools/asmtest/host_stubs.c`, `build.sh` | Link inert presentation observations for retail packet comparisons; abort if a host-only geometry path is unexpectedly entered. |
| `tools/bugtest/modern_benchmark.py` | Serial warmed, rotating-order replay measurements with fresh saves, wait4 CPU/RSS, field-count validation and JSON results. |
| `tools/webtest/modern_renderer.sh`, `renderer.html` | Compile and run the production GPU fixture in a WebGL 2 browser. |
| `tools/generate_modern_metal.py` (new) | Explicit reproducible generation and drift check for post math; no runtime conversion dependency. |
| `tools/bugtest/run.sh`, new `test_modern_*` | Production-path settings, topology, sources, metadata, billboards and GPU/lifecycle tests. |
| Existing bugtest fixtures | Supply new observation/policy stubs without changing original expected results: far lifetime, GPU HUD/passes, guest-1995, link lifecycle, near-object, ordering-cycle; framecap-storage additionally checks baseline/Modern colour interpolation; rename the fixture-local pixel-size helper to avoid the device helper collision. |
| `README.md`, `docs/RENDER_BACKENDS.md`, `docs/MODERN_RENDERER.md`, this ledger | Usage, architecture, validation and limitations. |

The pre-existing `.gitignore` edit and user-provided specification are retained.
No bundled library, generated game data, disc image, extracted asset or save was
changed. No commits were created.

## Port-rule audit

* Rule 7: no new non-void bare returns or changed return-value assumptions.
* Rule 8: no new absolute console-memory accesses.
* Rule 9: no changed asset/memory-card binary layouts or native struct dumps.
  Optional `modern.json` uses scalar semantic fields beside unchanged XLFC v3.
* Rule 10: floating-point/double math added in `portmodern.c` sampling/source
  transforms, `objdraw.c:DrawWorldSprite/ObjectLight`, and
  track-light sampling. It is presentation-only. Removed raised-detail projection
  math is documented in the cleanup note.
  No new game RNG, overflow/shift/division behavior, uninitialized game reads or
  unaligned console loads were intentionally introduced.
* Existing pointer-free PRM packet sizes and object transform conventions in
  `objdraw.c` remain guarded by its size assertions. Array access uses actual
  fields and element sizes. No new hardcoded pointer-bearing struct offsets.
* All thirteen protected functions remain textually unchanged; untouched
  protected files were not edited. `collideLineToPlane` and `GetNewWeapon` were
  compared explicitly against the baseline.
* The no-road ship fallback bounds the lane increment to the section's available
  faces; this is confined to the new presentation sampler.

## Validation evidence before settings cleanup

These results describe the original implementation. See
[the cleanup note](modern-settings-cleanup.md) for validation of the reduced feature set.

Run on Apple M4 Max, macOS, using the pinned baseline and the current native
build. Temporary logs and captures are outside source changes under
`/tmp/wipeout-modern-*`.

| Check | Result |
| --- | --- |
| Baseline build and existing bugtest suite before edits | Passed. |
| `cmake --build build/mac -j4` | Passed, including second-game symbol-isolation check and packaged metallib. |
| `WIPEOUT_TEST_GL=1 WIPEOUT_TEST_METAL=1 sh tools/bugtest/run.sh` | Passed after final shader/payload changes. Runtime Metal compilation and real production OpenGL/Metal submission exercised. |
| Existing GPU fixtures | All 512 colour/depth cases pass per device; raw SDR, indexed/full-colour/HD, depth, wrapping and existing lifecycle checks retained. |
| Modern GPU fixtures | Numeric Classic/Soft Highlights dark/midtone/white fixtures, HDR light/SDR separation, cached repeated resolve, old-front policy, tone change, bloom/motion/HUD masks, wide resolve, asynchronous readback through device switches (2 UNORM tolerance), disable cleanup and injected unavailable HDR passed. |
| New CPU/game fixtures | Settings/defaults/overrides, exact-edge topology/markers/creases/live RGB/reload, road triangle sampling, all source types and full source pool, copied read-only state, stable light selection/interpolation/reuse, authored detail/near clipping/arena exhaustion, and billboard source dimensions/anchors passed. |
| Matched baseline-off captures | Seven Talon's Reach pictures (fields 2800–3400) byte-identical to the pinned main executable on OpenGL. |
| Matched game trace | 1,530 state events matched exactly between baseline and Modern over 3,500 fields, including RNG and player position. This is sampled state evidence, not a retail console-equivalence claim. |
| Production asset smoke | PS1 USA NTSC, European PAL, PC and Saturn race runs completed; Metal at 1x, 3x and 6x; OpenGL at 1x. |
| Capture compatibility | 29 resolved race pictures and Modern semantic metadata exported; current and pinned v3 readers agree on every frame/primitive count. |
| WebGL 2 production fixture | Passed in headless Chrome with ANGLE Metal on Apple M4 Max: real production HDR, tone, bloom, motion, HUD, widescreen, raw SDR, old-front/repeated resolve and disable/failure cleanup. Browser backend switching is unavailable and was not claimed. |
| Web regression suite | `tools/webtest/run.sh` passed with Emscripten: browser helpers, disc/audio bridges, heap-growth handling and glyph distances. |
| Additional production races | 10,000 fields each: Gare d'Europa, Phenitia Park, imported 1995 Altima VII and imported N64 Klies Bridge. Capture metadata confirms the source and track, plus 116 bulb-emission packets in Gare and 14 neon-emission packets in Phenitia. Screenshots inspected. |
| Widescreen interpolation lifecycle | Modern Metal at 3x, 1280x720 window: 6,200 fields and 11,762 presentations completed. Screenshots confirm pause, resumed race, second pause and restarted race with reset countdown/lap. |
| Imported retail fixtures | N64 USA: 18 checkpoint transforms and all 8,193 trig entries matched ROM data. PS1 USA 1995: scenery, 80 aurora quads and Gouraud sky packets matched at all 15 fixture updates. |
| Recorder state regression | Poisoned draw-state fixture passes Existing/Modern/Existing transitions, preserves HUD classification and N64 corner encoding, and checks optional fields do not leak into disabled capture. |
| Shader generation | `tools/generate_modern_metal.py --check` passed. |
| Game diagnostics | `tools/diag.py` run for every changed game C file and new `portmodern.c`; clean. |
| Cross declarations | Same six pre-existing alias/prototype reports as the pinned baseline (`InitRaceData`, `InitSpeedo`, `UpdateRaceHud`, `cdMusicTable`, `fill_speedo`, `padBuf1`); no added reports. |

### Disabled-path performance investigation

Sampling identified unnecessary frame-boundary flushing, per-packet policy and
source lookups, and copies of Modern-only state in baseline submission. The
completed-boundary callback now flushes only when the committed policy changes,
while the callback still observes the old policy. The committed mode has a
cheap cached bit; pending requests cannot change it. Existing submission skips
Modern material/lighting preparation and copies only its original state fields.
Uniform bindings are refreshed on program changes, unused sidecar tables are
not cleared, and near-plane vertex copies are deferred until clipping is needed.
Decoded baseline draw states initialize only their consumed prefix; recorder
copies initialize the complete optional tail. Queued ranges retain only blend,
depth and optional light state instead of copying already-packed materials.
Metal reuses an unchanged encoder pipeline without a dictionary lookup, and its
render-thread-owned state uses direct records instead of atomic property copies.

Ordering-table validation still runs on every required traversal, including
when callers rewrite tags directly. Brent's cycle detector reduces redundant
edge reads without caching away checks or dropping the valid tail. Tests cover
all 2,145 prefix/cycle combinations through 65 nodes and preserve packet payloads.
Boundary tests verify unchanged settings do not flush, changed settings flush
before publication, and invalidation cannot recursively publish during a flush.

### End-to-end timing evidence and user acceptance

`tools/bugtest/modern_benchmark.py` runs the same 10,000-field Talon's Reach
replay serially, with fresh saves, equal warm-up, rotating executable order,
Metal, 640x480, Full distance and no capture. Each sample contains 9,994
presentations. CPU is user plus system time; RSS is process memory, **not**
retained GPU allocation accounting.

The original 3,500-field smoke indicated 4–7% disabled-path overhead. Longer
interleaved runs confirmed redundant host work. During optimization, matched
3x medians fell from approximately 5% overhead to 1% in both CPU and elapsed
time (5.242 versus 5.194 CPU seconds; 4.835 versus 4.785 elapsed seconds).
The final longer matrix was contaminated by another game instance: even pinned
baseline elapsed times increased from roughly 4.6 to 7.7 seconds. The user
identified the concurrent instance and explicitly accepted current performance.
Those contaminated samples are retained in `/tmp/wipeout-modern-benchmark-verified`
for transparency; they do **not** establish zero overhead or a numeric budget.
No further performance tuning is required for this user's accepted scope.

Per-stage CPU submission/GPU scene and resolve timing, separate-effect cases,
sustained GPU allocation measurements and comparison with the pinned reference
branch remain specification rollout evidence, separate from this acceptance.

### Oracle and retail validation

The restored MIPS o32 oracle compiled all 61 baseline top-level game files and
compared all 14 changed existing top-level game files: **54 function differences,
zero uncompared files**. This is an instruction-difference audit, not a claim
that Modern presentation is instruction-equivalent to the baseline. Both sides
use the same `-G0` option; this avoids an LLVM 21 small-data lowering failure and
does not reproduce the historical compiler's instruction selection.

Every reported difference is accounted for by the game-source table above,
with these compiler effects made explicit:

* `objdraw:BindSpriteCorner` carries N64 source provenance through precise corners.
* `sparks:InitParticle` inlines the changed `ClearParticleTable` observer, with
  resulting register allocation and zero-store scheduling changes.
* `portfar:PortFade`, `PortFarAlloc`, `PortFarFlared`, `PortFarFlares`,
  `PortFarObject`, `PortFarObjectPrimitives`, `PortFarSlot`, `PortFlareSlot`,
  `PortNearAlloc`, `PortShadowDrawn`: each differing instruction is a `.bss`
  address addend shifted by eight bytes by the new road-detail statics. The
  instruction counts and operations are unchanged.
* `portfar:PortFarDraw` includes the new draw-only road-detail and ship-lighting
  dispatch; `RoadRuns` and `PortDrawTrack` are new functions.
* A separate o32 compilation of `pc/pcmenus.c` found `DrawMenuModel` and
  `PcClassSelect`, `PcDispHiScores`, `PcMenuControl`. The added Modern-only aspect
  correction changes `DrawMenuModel` and LLVM's inlining of it into those callers;
  the latter changes include consequent branch offsets and register scheduling.
* Remaining reported functions are the exact observation/presentation sites
  named in the game-source table. New `portmodern.c` has no baseline counterpart.

The retail assembly harness passed **11,200 checks and 155,474 packets, zero
failures**, over tracks 01, 02, 06, 07, 08, 13, 17 and 20 from the local USA disc
fixtures. AddressSanitizer and UBSan were enabled; leak detection was disabled
for fixture-lifetime allocations. Modern is explicitly off and tracing on in
this harness. These are comparisons of the exercised retail routines, not a
whole-game console-equivalence claim.

### Remaining acceptance evidence

Actual mobile OpenGL ES hardware has not been tested; desktop ANGLE does not
substitute for a phone GPU or a full browser game run. Full split-screen/link
visual coverage and destruction/rescue transitions remain manual acceptance
checks; unit lifecycle coverage does not establish their visual correctness.
Whole-game console lockstep requires external retail traces that are not
present locally. Full
CPU submission/GPU scene/resolve stage measurements, separate-effect timing,
peak retained GPU allocations and a controlled comparison to the pinned
reference branch remain outstanding. Full specification acceptance and a
default-on rollout are therefore **not claimed**.
