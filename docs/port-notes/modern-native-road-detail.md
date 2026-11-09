# Modern road detail at Native draw distance

- `game/src/portfar.c` enables the authored near-road detail pass for Modern.
  `PortDrawNativeTrack` still selects the same linked sections and uses
  `PortTrackFar` for faces the detail pass cannot draw. The shared helper is now
  named `DrawNearRoadSection`.
- `tools/bugtest/test_native_scene_dispatch.c` checks the selected high, medium
  and low detail paths, the exclusion of an unselected section, and fallback
  when the high-detail packet pool is full.

The PS1-policy MIPS oracle reported zero differences across 43 functions in
`portfar.c` against `b28c2a8`. Host diagnostics were clean. The code change
introduces no representation changes or console arithmetic dependencies.

The macOS build and CPU regression suite passed. The detail assertion failed
before the fix and passed afterwards. Fixed-step captures from the US PlayStation
disc showed sharper road textures in Modern Native mode. The three sampled
Modern Full frames matched the corresponding frames from before the change byte
for byte.
