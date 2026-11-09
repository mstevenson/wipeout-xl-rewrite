# Modern Track Detail in Full view

- `game/src/track.c` and `game/include/portfar.h` rename the shared road
  hook to `PortDrawTrack`. The callers retain their original draw paths when
  the hook declines to handle the view.
- In `game/src/portfar.c`, `SelectAuthoredRoad` can use the original
  contiguous lists for the raised Full detail levels. `RaisedRoadDetail`
  implements Medium's Low-to-Medium and Medium-to-High promotion and High's
  uniform detail. `PortDrawTrack` starts the arena and ordering table for
  raised Full detail. `DrawFarTrack` appends the visible extended sections
  without redrawing the authored selections. `PortFarDraw` keeps the first
  pass's table when appending the extended road, scenery, and ships. Native
  detail retains the existing Full rendering, including its far-fill pass.
- `RenderSet` and `RenderSave` use `medium` for the renamed option and accept
  the previous `medium_plus` value on load. `ProfileSettings` offers Track
  Detail at both draw distances.

The change uses the existing road arena, packet layouts, and floating-point
visibility tests. It introduces no console offsets or game data format changes.

## Oracle audit

The PS1-policy MIPS comparison against `48b60f3` reports the road hook rename.
`DrawNewTrack` and `DrawAllTrack` differ only at the call relocation.
`PortDrawNativeLandscape` has the same source and now tests the scenery
policy before the one-bit native-scene flag. Host diagnostics were clean.

## Validation

The macOS build and CPU and OpenGL regression suites passed. The dispatch
fixture verifies both promotions, extended section culling, and one arena
submission per visible section. Three default Full race captures from the
US PlayStation disc matched the previous build byte-for-byte, as did three
High Native captures. Full Medium and Full High produced distinct captures.
