# Modern Track Detail

- `platform/render/renderer.h` gives Modern a saved Track Detail preference
  that defaults to Native. `RenderSet` and `RenderSave` round-trip it.
  `ProfileSettings` offers the three levels in Modern's Native distance mode.
  Modern's `Geometry` exposes the committed preference through `RenderGeometry`.
- `game/src/portfar.c` applies the detail preference in `PortDrawNativeTrack`
  after selecting the same authored visible sections. Native retains the
  existing packet path. Medium+ and High dispatch to `PortTrackDetailed`.
- `game/src/trackdraw.c` subdivides road faces using the authored tile grid.
  `RoadClip` culls cells outside the expanded view. `PortTrackDetailed` emits
  the remaining tiles in the existing road arena with precise view-space
  coordinates and interpolated lighting. The renderer clips near-plane
  crossings using the existing polygon path.
- The regression fixtures cover saved settings, keyboard selection, authored
  section dispatch, and precise packet emission beyond the old High pool.

The change uses existing native fields and packet sizes. The new floating-point
arithmetic is limited to the host render path. No console offsets, serialized
game structures, or PsyQ prototypes change.

## Oracle audit

The PS1-policy MIPS comparison against `48b60f3` found no differences in
`portfar.c`. In `trackdraw.c`, `PortTrackDetailed` is new, and the compiler
inlines `ShadeTrackGrid` into `AsmTrackHi` and `AsmTrackMed` instead of emitting
it separately. `DrawGridQuad` and `PortTrackFar` differ only in the relocation
labels of their frame-recorder strings. Host compiler diagnostics were clean.

## Validation

The macOS build and CPU and OpenGL regression suites passed. The arena test
emits 768 High tiles from 48 faces, checks the Medium and High texture pages
and mirrored tiles, and verifies near-plane coordinates and offscreen culling.
Fixed-step runs on the US PlayStation disc exercised all three levels. Three
sampled race frames matched the baseline byte-for-byte in both default Native
and Full with High saved. Medium+ and High produced distinct Native pictures.
