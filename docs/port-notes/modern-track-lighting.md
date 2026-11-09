# Modern track lighting

Modern defaults to smooth track lighting. The committed profile controls both
packet tagging and perspective-correct colour interpolation. The original
POLY_FT4 packets and gameplay face colours keep their existing representation.

- `game/src/trackdraw.c` builds corner groups from shared edges between
  compatible surfaces. Open and ambiguous edges, sharp creases, distinct
  road and wall surfaces, and gameplay marker faces keep their boundaries.
  Each group samples the current face colours in the receiving face's light
  encoding. Low detail and near/far fill use the group colours directly.
  Medium, high and partial tiles sample the same two-triangle colour field.
- `game/src/trackasm.c` resets the adjacency cache when `AsmGenerateTiles`
  starts a new course. `game/include/trackasm.h` declares the reset hook.
- `game/include/port_platform.h` declares the packet tag.
  `platform/psx/libgpu.c` stores tags beside the packets for the precise
  projection frame and clears a tag when the packet is reused. It applies the
  colours before near clipping. `platform/psx/gpu.h` carries the material
  flag through GPU submission and frame inspection.
- `platform/render/` owns the policy, checkbox and saved default.
  Only Modern chooses perspective-correct colour for the tagged material.

The macOS build and CPU and GPU regression suites passed. The GPU test draws
a receding road patch with 1×1, 2×2, 4×4 and 8×8 tile grids and checks both
the expected perspective gradient and the pixel agreement between grids.
A 180-frame Gare d'Europa recording contained tagged low, subdivided, partial,
far and near-fill tiles. Repeated projected vertices on the same face in a
frame had matching RGB across 510,257 face/position keys.

Compiler diagnostics were clean for both edited game files. The B0 snapshot
was unavailable, so the MIPS oracle used a fresh snapshot of the pre-change
HEAD. It reported the new adjacency and sampling helpers (`CompareLightEdges`,
`PortResetTrackLighting`, `ShadeTrackGrid`, `TrackCornerLights`) and the
expected changes to `AsmGenerateTiles` (cache reset), `AsmTrackLo`,
`AsmTrackMed`, `AsmTrackHi`, `DrawGridQuad` and `PortTrackFar` (optional
sampling and packet tags). No console-layout offsets or new raw packet fields
were introduced. The retail differential test could not link due to existing
missing native-renderer support symbols. Real-disc validation used the US
PlayStation release.
