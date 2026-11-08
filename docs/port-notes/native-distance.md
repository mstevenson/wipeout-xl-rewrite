# Native draw distance by renderer

Native distance is selected by the active renderer, independently of the asset
platform. Full distance and lockstep tracing bypass the new native passes.
The PlayStation simulation still determines combat visibility, proximity and
engine sound. Renderer-specific ship visibility does not change those flags.

## Rules and references

- **PlayStation:** retain the original regional scenery near/far tables, authored
  road lists and `DrawShips` gates. The scenery near-bound relaxation now applies
  only to Full distance. The Native PS1 path remains the compatibility renderer.
- **PC:** keep the original F3 north-road low-detail run caps and scenery ranges.
  Runs follow section topology; all-view lists are not subject to the F3 cap.
  Rendered ships use the original PowerVR `AddShipRenderObjects` 1200/40 gates,
  not the larger gameplay thresholds. See [ship evidence](native-ships.md).
- **N64:** use the original road walker, junction handling, direction lookup,
  course exceptions and remembered ship visibility. Preserve cartridge section
  ordinals before host gameplay renumbering. Scenery transforms retain the
  original half-world rounding. See [ROM evidence](n64-native-distance.md).
- **Saturn:** use contiguous authored road runs, exact direction boundaries,
  course depth buckets and authored model radius/depth modes. Hulls and their
  shadows use the original ship depth interval. See [SH-2 evidence](saturn-native-distance.md).

The native non-PS1 passes submit precise geometry through the existing packet
renderer, so the PS1 GTE's signed-16-bit input and far ordering-table limits do
not silently remove geometry selected by another platform's visibility policy.
Native hulls and shadows share a selection mask. Existing trail simulation is
retained independently, as in the original games.

## Verification

The N64 and Saturn scripts execute original instructions from the user's local
ROM/executable and compare their decisions with the compiled host policies.
They include equality boundaries, coordinate rounding, road direction and
junction cases, and ship history. Original images and extracted files stay
outside the repository.

`tools/bugtest/run.sh` includes tests of actual native track, scenery, ship and
shadow dispatch. The ship test also compares original gameplay outputs with
native packet submission enabled and disabled. `tools/bugtest/render.sh` checks
all four production GL profiles and independent renderer linkage.

The ILP32 oracle differences are intentional rendering dispatch/near-bound
changes in `DrawLandscape`, `DrawNewTrack`, `DrawAllTrack`, `DrawShips`,
`initShipData`, `DrawShadow`, `PortTrackFar`, `SceneRadiusCheck`, and the native object path. Changes
reported for `PortFarDraw` and `DrawFarShadows` also reflect compiler inlining
and shared native helpers; the Full-distance policy is retained. New functions
are rendering-only helpers. No protected race-control routine was changed.

These checks establish visibility-policy correspondence and native integration;
they do not assert whole-frame pixel equivalence between host and consoles.

Final validation included the macOS build, CPU and GPU regression suites,
clean diagnostics for all changed game sources, reviewed ILP32 oracle diffs,
and 5,000-frame Native demo smoke runs in all four profiles. N64 was selected
through the same renderer preference under the debugger; test settings and
captures remained isolated from the user's settings. Original-policy oracles
passed 25,539 N64 host/MIPS cases plus table/loader checks, and 90,224 Saturn
SH-2 comparisons. The native integration fixtures include Full/PS1 bypass,
ship/shadow boundaries, remembered visibility, candidate caches, and partial
Saturn billboard disappearance. No full console lockstep recording was
available in this checkout; no whole-frame equivalence is claimed.
