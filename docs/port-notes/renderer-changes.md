# Renderer changes and validation

Consolidates the PC/N64 implementation, renderer split/isolation and subsequent
review fixes. Current behaviour lives in [Rendering profiles](../RENDERERS.md),
[PC](../PC.md#rendering-evidence-and-limits) and [N64 rendering](../N64_RENDERING.md).
Results below are historical records from those changes, not fresh test runs.

## Game-source changes

These are intentional rendering changes, with `Port:` representation comments.

| File | Changes |
| --- | --- |
| `game/include/port_platform.h` | Exposes neutral `RenderGeometry` policy, replacing named PC policy queries. |
| `game/src/objdraw.c` | `GouraudColours` retains PC scenery/sky corners; `OverrideObjectLight` (formerly `PcObjectLight`) applies all-corner lighting. `DrawObject` consumes geometry policy and converts only imported RGB8 road light for PS1-scaled ships. `ProjectVertices` and `BindSpriteCorner` carry/reset N64 provenance through near/far geometry. Shared model data is unchanged. |
| `game/src/port_widen.c`, `game/include/port_widen.h` | `PortPrmMarkN64` replaces lossy normalization; `ConvertPrm` preserves RGB and marks flags through `PortRecordField.fileOffset`, not native pointer-bearing offsets. |
| `game/src/portorig.c` | `PortOriginalDrawDroid` uses original sign intensities 255/128; geometry and timing unchanged. |
| `game/include/wipeout.h` | `Face.lightFormat` occupies trailing padding byte 19. Record size remains 20, alignment 2; host/MIPS o32 field offsets and the RGB-to-`CVECTOR.cd` view agree. Runtime metadata, not a disc field. |
| `game/src/track.c` | `WADInitFaces` initializes provenance after loading/endian conversion: N64 RGB8, other sources PS1 scale. Ignores disc padding; the pointer-free 20-byte raw read is unchanged. |
| `game/include/track.h`, `game/src/dynam.c`, `game/src/weapon.c` | `PortSetTrackColour` tags gameplay RGB as PS1 scale. `SetTrackAttributes` and `CycleWeaponGrids` use it for pad/checkpoint colours, including neutral 128 and overbright animation. RGB values, timing and gameplay decisions are unchanged. |
| `game/src/trackdraw.c` | `SetTrackLight` preserves RGB and tags PS1-scale light on N64 packets. Used by `AsmTrackLo`, `DrawGridQuad`, `DrawPartialCell`, `DrawNearFace` and `PortTrackFar`; faces, tiles, geometry, ordering and packet layouts remain unchanged. |
| `tools/asmtest/render_test.c` | Console geometry-policy stub for differential tests. |

No protected routines (including `DrawEnvMap`), PsyQ declarations, generated data,
assets or ROMs changed. No new missing returns, absolute console addresses,
pointer-bearing serialization/offsets, uninitialised reads, floating-point game
arithmetic or undefined integer operations were introduced (port rules 7–10).
No unresolved layout dependencies were reported beyond the verified Face/CVECTOR
correspondence. Rendering fidelity and blocked oracle checks remain uncertain.

## Platform changes

- `platform/original.c`: `Wipeout64Parts` retains road RGB and marks scenery,
  sky and signs; `PlaceTims` retains addressing flags. `n64_material.[ch]` stores
  full-image bounds and page/depth/palette hash buckets with geometric growth,
  preserving registration order and rejection of ambiguous lookups.
- `platform/psx/gte.[ch]`, `gpu.h`, `libgpu.c`: projection provenance survives
  deduplication, clipping and untextured submission. `AlternateQuad` checks every
  corner plus page identity; `Polygon` shares its diagonal with `NearCut`.
  Imported quads retain their authored split; native PC objects use the PC split.
- `libgpu.c` (`NearCut`, `CutTriangle`, `CutColour`): immutable operation flags
  select interpolation. Affine UV/RGB use projected fraction `t * endZ / clippedZ`;
  perspective UV/fade use view-space fraction. No endpoint-depth division is
  needed. Extrapolated RGB saturates before byte conversion; UVs remain floats.
- `n64_material.h`, `gpu.h`, `libgpu.c`, `render/gl_backend.c` and shared shaders:
  private page bit 15 (`PORT_TPAGE_PS1_LIGHT`) carries road-light provenance into
  `GpuDrawState.ps1Light` and mode-byte bit 5. N64 identity remains page bit 14;
  address bits remain 2–4 and texture depth 0–1. Draw-mode masks prevent leakage
  to later primitives; clipping preserves the tag. All profiles use 128-neutral
  modulation for tagged packets without clamping away overbright RGB.
- `platform/render/`: separate profile policies, scene programs and presentation
  resources over common GL transport; source decoding stays available to every
  profile. N64 nearest modulation clamps before output processing, fade and blend
  weighting, matching filtered/HD paths. Raw textures are unchanged.
- `render/n64_gl.c`, `shaders/n64_vi.h`: RGB5/dedither/divot stages, shared display
  and capture path, per-screen caches and lazy targets. Dedither adapts
  [paraLLEl-RDP's MIT implementation](https://github.com/Themaister/parallel-rdp/blob/master/parallel-rdp/shaders/vi_fetch.frag);
  the source retains its license. `glyphs.c` uploads only level zero.
- `settings.[ch]`, `debug_ui.cpp`, `render/renderer_ui.cpp`, `link.c`, `trace.c`:
  independent saved profiles/UI, legacy migration, mutex-protected player settings
  and effective-policy commits at frame boundaries. `video.c` releases GPU
  resources before context teardown. `CMakeLists.txt` includes both game copies.
- `psx/framecap.[ch]`, `tools/framecap.py`: recorded profile and per-primitive
  shading flags, version-4 serialization and older-format compatibility.

## Recorded validation

- CPU/GPU suites (`sh tools/bugtest/run.sh`, `sh tools/bugtest/render.sh`) and
  `cmake --build build/mac -j 2` passed through the follow-ups, including
  second-game symbol isolation and application packaging. Working/staged
  whitespace checks passed. Exported-index CMake configure/generate passed after
  source lists were staged alongside the backend move.
- Regression coverage includes settings/UI/linking, active-policy invalidation,
  independently linked renderers, material-index growth/collisions/ambiguity,
  native/HD sampling/addressing, lighting, depth, screen ownership, GPU resource
  release, VI caching/target counts and all 64 N64 settings combinations.
  Capture tests use the production serializer and Python reader, including v3.
- `test_near_object.c` passed ASan/UBSan (existing harness disables shift-base
  checking). Near-clipping tests also passed float-to-integer overflow checking:
  the pre-fix affine RGB case produced 85 instead of 170. Saturated half-opacity
  tint previously produced red 0.996094 instead of 0.5; the regression now passes.
- Native 4,200-field smoke runs passed for PS1, PC and N64 with captures visually
  inspected. Initial renderer-split before/after replays matched all 39 captures
  (13 per profile). N64 PC-preference comparisons matched seven race captures
  at scale 2 and seven at scale 1 widescreen; early menus could differ. Later
  isolation smoke runs produced four race captures per profile. These are native
  comparisons, not console evidence; final resource/raster/lighting follow-ups
  did not repeat full-game smoke tests.
- N64 filter/pipeline ROM verifiers passed against the USA image, executing
  isolated routines, including all 65,536 palette words. See the
  [investigation](n64-pipeline-investigation.md) for reproduction and scope.
- `tools/diag.py` was clean for `objdraw.c`, `port_widen.c`, `portorig.c`,
  `dynam.c`, `weapon.c`, `track.c` and `trackdraw.c`. `tools/layouts.py show Face`
  and host static assertions confirmed the layout above. `tools/xdecl.py`
  retained six existing discrepancies: `InitRaceData`, `InitSpeedo`,
  `UpdateRaceHud`, `fill_speedo`, `cdMusicTable`, `padBuf1`.

**Blocked/unverified:** `tools/oracle32.py diff B0` could not compile `objdraw.c`,
`port_widen.c`, `portorig.c`, `dynam.c`, `weapon.c` or `track.c` because MIPS headers
lacked `sys/types.h`; `trackdraw.c` lacked `string.h`. Trailing zero-difference
counts are not equivalence results. No valid oracle differences were produced;
intended differences are the policy, colour and metadata changes listed above.
`tools/asmtest/build.sh` was blocked by existing unresolved support symbols,
including packet constructors and `portPal`; no differential executable resulted.
No console lockstep, hardware/retail-PC framebuffer comparison or non-macOS
execution was recorded. Native builds/tests do not establish console equivalence.
