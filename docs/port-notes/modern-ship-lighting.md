# Modern ship lighting

- `game/src/trackdraw.c` and `game/include/trackasm.h` expose
  `PortShipTrackLight`. It searches the road triangles in the current section,
  its immediate neighbours and their linked junctions. It samples live corner
  colours at the closest point within 1000 world units, using the track's
  corner order and diagonal. The original section and lane tint applies when
  smoothing is off or the ship is farther from the road. The sampler uses
  host-side double precision and passes the face's colour encoding through.
- `DrawShips` in `game/src/ships.c` and `PortDrawNativeShips` and
  `DrawFarShips` in `game/src/portfar.c` use the shared sampler at draw time.
- `DrawObject` in `game/src/objdraw.c` publishes the tint after the existing
  model colour conversion through `PortShipPackets`, declared in
  `game/include/port_platform.h`. The GPU adapter keeps the previous sample
  beside the ordering table and blends it into flat textured hull polygons
  during interpolation. It requires consecutive ship samples and uses the
  geometry's presentation fraction, including the camera cut guard.

Compiler diagnostics were clean for the four edited game source files.
The B0 snapshot was unavailable, so the MIPS oracle used pre-change HEAD
(`4933322`). It reported the new functions `FindShipRoadLight`,
`FindShipRoadTriangle` and `PortShipTrackLight`, and the shared corner order in
`TrackCornerLights`. It also reported `DrawShips`, `PortDrawNativeShips`,
`PortFarDraw` (which includes the inlined `DrawFarShips`) and `DrawObject`.
These changes are the tint sampling and packet metadata calls described above.
The console face, ship and packet layouts are preserved.

The macOS build and CPU and OpenGL regression suites passed. The ship tests
cover lane and section seams, junction lookup, live colours, imported colour
encoding, fallback and interpolated presentation. A 180-frame capture from a
moving Gare d'Europa race on the US PlayStation disc showed varying hull tints
in both near and far packets, including a rival that moved between the two
draw paths.
