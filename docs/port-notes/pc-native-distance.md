# Native PC draw distance

- `draw.c` applies the retail PC near and F3 far bounds in `DrawLandscape`
  for the active PC renderer in Native mode. Its accepted objects use precise
  projection through `PortPcDrawNativeScenery`.
- `track.c` dispatches `DrawNewTrack` and `DrawAllTrack` to the PC visibility
  path in Native mode. `portfar.c` adds `PortPcDrawNativeTrack`, which follows
  the authored section links, caps north low-detail runs and uses full-precision
  drawing for faces the compatibility track renderer misses. The compatibility
  tile pools keep their original sizes.
- `PortFarMarkFace`, `PortFarDraw`, `PortFade`, `PortFarObject`,
  `PortFarObjectPrimitives` and `PortNearAlloc` differ in the MIPS oracle because
  arena registration was shared with the new PC path and face marking can now
  run in Native PC mode. The oracle baseline was a snapshot of the pre-change
  HEAD because the local `B0` snapshot was unavailable.
- The new code uses typed fields and existing packet representations. It adds
  no console addresses, raw struct offsets, or file reads. The console path
  remains the fallback during lockstep tracing. The PC visibility tables are
  verified against the Windows disc executable. The compatibility renderer
  still supplies the console profiles' loaded course visibility and game-side
  ship drawing.
