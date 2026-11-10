# Wipeout 1995 scenery lighting

- `PortOriginalSceneInit` in `game/src/portorig.c` marks the imported
  1995 scenery list with the private object flag declared in
  `game/include/portorig.h`. The sky already has its own colour policy.
- `platform/render/geometry.h` declares the policy for retaining the
  imported scenery's corner colours. The Modern and PlayStation profiles
  enable it in their `Geometry` functions.
- `GouraudColours` and `GouraudTextured` in `game/src/objdraw.c` select the
  existing Gouraud packets for marked 1995 objects. `DrawObject` covers
  the normal and far passes, and `DrawNearPrimitive` preserves the same
  corner inputs through clipping. Native XL objects keep their packet rules.
  No disc or console packet layout, raw offset, or buffer size changed.
- The guest scenery and near-object CPU tests check provenance and both
  textured primitive sizes, including a native object during a guest race.

Compiler diagnostics were clean for both edited game source files. The B0
snapshot was unavailable, so the MIPS oracle compared against pre-change
HEAD (`f6a6df7`). It reported two changed functions. `PortOriginalSceneInit`
sets the provenance flag and `DrawObject` includes the inlined colour policy
and clipping decision.

The macOS build and CPU regression suite passed. Packet captures from the US
PlayStation disc matched the authored Terramax scenery colours in Venom and
Rapier on both profiles. Modern also matched on distant and near-clipped
scenery. Native Talon's Reach before and after produced identical captures
and packet streams on both profiles, excluding host memory addresses.
The retail XL differential harness passed 11,212 checks and compared 155,522
packets with no failures. Its local build linked the existing Saturn model
helpers and used temporary stubs for native profile dispatch, which is inactive
in tracing mode.
