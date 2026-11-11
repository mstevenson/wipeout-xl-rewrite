# Modern settings cleanup

Date: 2026-11-10. Branch: `modern-renderer`.
Baseline: `c34020f2b14d1caab3f78c99c2b345e605246768`.
This change edits the existing uncommitted Modern implementation in place.

## Removed features and supporting code

- `platform/render/modern/modern.[ch]`, `ui.cpp`: remove Modern resolution,
  pacing, custom/full distance, fade/flaring overrides and track-detail settings,
  defaults, parsing, serialization, environment handling and controls. Global
  settings remain authoritative. Old Modern-only keys are ignored, and saving
  omits them. Reset Modern does not change global preferences.
- `platform/render/modern/geometry.h`, `scene.c`: remove the track-detail enum
  and geometry-policy field; preserve lighting and billboard policies.
- `platform/main.c`, `platform/psx/libetc.c`: restore the baseline files exactly.
  Global draw distance, fade, flares and frame pacing apply to both renderers.
- `platform/psx/libgpu.c`: restore global interpolation selection, including
  no interpolated presentation in Original mode; retain history when blur needs
  it. Remove the resolution-update call tied to Modern policy changes.
- `platform/psx/gpu_renderer.c`, `gpu.h`: restore baseline global scale selection,
  remove unused per-axis raster scale state and the unused pixel-size API.
  HDR and motion resources still follow framebuffer resolution changes.
- `platform/psx/framecap.c`: remove obsolete Modern-only values from the optional
  JSON sidecar; existing capture dimensions and XLFC v3 remain unchanged.
- `tools/bugtest/test_modern_detail.c`, `run.sh`, `test_far_lifetime.c`: remove
  the deleted feature's fixture, runner entry and stub.
- `tools/bugtest/test_modern_settings.c`, `test_modern_gpu.c`,
  `test_modern_decoder.c`: verify ignored obsolete keys, shared preferences across
  reset/toggles, live global resolution changes, and global frame-rate behavior.
- `README.md`, `docs/MODERN_RENDERER.md`, `docs/MODERN_RENDERER_SPEC.md`,
  `docs/port-notes/modern-renderer.md`: update usage, scope and accounting.
  Earlier validation results remain explicitly historical.

## Game-source audit

| File | Functions and category |
| --- | --- |
| `game/include/portfar.h` | Remove `PortDrawTrack` and `PortTrackDetailed` declarations; file matches baseline. |
| `game/src/track.c` | Remove dispatch from `DrawNewTrack` and `DrawAllTrack` and its include. Both functions match baseline; retain the existing `WADInitFaces` source-metadata observation. |
| `game/src/portfar.c` | Remove `roadDetails`, `roadDetailCapacity`, `RaisedRoadDetail`, `RoadRuns`, `PortDrawTrack` and their includes. Restore baseline `DrawFarTrack`. Retain existing sampled ship lighting and allocation-failure handling. |
| `game/src/trackdraw.c` | Delete `RoadPoint`, `RoadClip`, `PortTrackDetailed` and the now-unused standard-library include. Retain original authored Lo/Med/Hi paths and Modern smooth-light observations. |

These are deletions of optional presentation behavior, with no new representation
changes. No protected function was edited. No new rule 7–10 concerns, raw
pointer-bearing offsets, serialized layouts, uninitialized reads, or uncertain
field interpretations were introduced. Deleted detailed-road projection math was
floating point; retained sampling math is accounted for in the original ledger.

## Validation

- `cmake --build build/mac -j4`: passed, including second-game symbol isolation
  and the macOS app bundle.
- `WIPEOUT_TEST_GL=1 WIPEOUT_TEST_METAL=1 sh tools/bugtest/run.sh`: passed on
  Apple M4 Max. Includes production OpenGL/Metal effects, 512-case GPU colour/depth
  fixtures, settings, topology, packet metadata, capture and lifecycle checks.
  Optional disc-movie tests were not requested; no disc arguments were supplied.
- The decoder fixture additionally checks every global frame-rate mode across
  Existing → Modern → Existing, including blur-history retention without
  enabling interpolation in Original mode.
- `tools/diag.py` for `track.c`, `trackdraw.c`, `portfar.c`: no diagnostics.
  Direct `tools/cc.sh` syntax checks also passed.
- A fresh oracle snapshot of HEAD compiled all 61 files without failures.
  `tools/oracle32.py diff modern-settings-cleanup-base track.c trackdraw.c portfar.c`
  compared every requested file and reported the eight retained differences below.
- Final diff review and whitespace checks cover this cleanup separately from
  the pre-existing uncommitted implementation.

### Retained oracle differences from HEAD

| Function | Reason |
| --- | --- |
| `portfar:PortFarDraw` | Existing Modern ship-road-light sampling in its inlined far-ship drawing. |
| `portfar:PortFarMarkFace` | Existing allocation-failure guard. |
| `track:WADInitFaces` | Existing source metadata/topology reset after loading faces. |
| `trackdraw:AsmTrackLo` | Existing Modern packet light metadata observation. |
| `trackdraw:AsmTrackMed` | Existing smooth track grid sampling and tagged grid drawing. |
| `trackdraw:AsmTrackHi` | Existing smooth grid/partial-cell sampling and tagged drawing. |
| `trackdraw:DrawGridQuad` | Existing track light encoding and corner-colour metadata. |
| `trackdraw:PortTrackFar` | Existing far/near-fill face light metadata, including inlined helpers. |

`DrawNewTrack` and `DrawAllTrack` no longer differ in the oracle. This audit does
not establish whole-game console equivalence. Retail lockstep, browser/mobile
runtime tests and interactive race visual inspection were not rerun for this
cleanup; no shader algorithms or assets were changed.
