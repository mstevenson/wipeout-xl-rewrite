# Saturn native asset tag byte order

`Object.scratch` retains the big-endian bytes written into synthetic PRMs.
The Saturn native registry expects host-order identifiers. Native visibility
was passing the stored word directly, so authored radius, billboard pieces
and polygon depth-mode lookups missed their models on little-endian hosts.

- `platform/render/saturn_native.h`: add explicit stored-to-host and
  host-to-stored conversions, preserving all identifier and frame bits.
- `game/src/objdraw.c`: `DrawWorldSprite` and `DrawObject` decode tags before
  billboard registry lookups. `DrawSaturnBillboard` encodes the parts tag
  before passing it to object-depth checking and packet tagging, whose
  interface uses the stored representation.
- `game/src/portfar.c`: `PortDrawNativeScenery` and `PortDrawNativeShips`
  decode tags before radius/billboard lookups; `PortNativeObjectDepthVisible`
  decodes the stored tag before selecting polygon corners and depth mode.
- `tools/bugtest/test_billboard.c`: replace registry mocks with the real
  registry and packet binder. Populate scratch using explicit PRM bytes;
  verify selected billboard geometry and the bound native material.
- `tools/bugtest/test_native_scene_dispatch.c`: exercise the real registry
  through scenery dispatch and depth checking, including authored radius,
  billboard sphere bypass, nondefault depth mode, and a frame-bearing tag.
- `tools/bugtest/run.sh`: link the billboard fixture to the real registry.

Validation: CPU regression suite, GPU regression suite, and macOS build
(`cmake --build build/mac -j 2`) passed. Both updated fixtures fail against
temporary copies of the staged pre-fix game sources. Diagnostics for
`objdraw.c` and `portfar.c` are clean; diff whitespace checks passed.

The `tools/oracle32.py` compiler/comparer was run against temporary copies of
the staged sources and the fixed working sources. ILP32 changes are confined
to `DrawObject` (including inlined billboard drawing), `PortDrawNativeScenery`,
`PortDrawNativeShips`, and `PortNativeObjectDepthVisible`: each now performs
explicit tag byte conversion. These intentional representation corrections
restore the native Saturn lookups; no protected gameplay routine or file
record layout changes. No additional rule 7–10 issues or unresolved raw
struct-offset dependencies were introduced. Whole-frame console equivalence
and optional disc-dependent tests were not run.
