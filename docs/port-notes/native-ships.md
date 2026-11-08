# Native ship visibility

Native distance selects a renderer policy independently of the imported assets.
`DrawShips` retains the PlayStation simulation decisions (combat flags, proximity,
engine sound and skeleton updates). Only its packet submission is replaced when
`PortNativeShipsEnabled` reports an initialized Native non-PlayStation scene.
Full distance and lockstep tracing retain the original dispatch.

## Original references

- PlayStation: `DrawShips` retains magnitude below 1200 after the world delta is
  shifted by three, positive transformed Z (camera ship bypass), and wrapped
  section distance strictly between -40 and 40. The gameplay proximity threshold
  remains 2900. No native policy writes these simulation flags.
- Windows PowerVR: `wipeout-xl-decomp/pc/src/ships.c`, `AddShipRenderObjects`,
  original address `435e30`, listed exact in `config/powervr/symbols.tsv` and
  `config/powervr/verified.txt`. The actual renderer uses **1200 and 40** too.
  The separate `DrawShips` function's 4800/80 gates update combat state, not
  rendered hulls. Its `GetMagnitude` uses float squares (Y, X, Z accumulation),
  square root and integer truncation. `PcNativeShipVisible` reproduces that
  calculation, wrapped section gate, camera bypass and death-count exception.
  Original `RotTrans` at `431dd0` shifts each matrix product before adding, just
  as the N64 routine does; the ship front gate shares that integer helper.
- Nintendo 64: `n64_visibility.c` records original ROM addresses and implements
  the recovered per-ship remembered visibility. The port keeps that render state
  separate from PlayStation attributes. Camera translations retain the original
  half-world storage rounding before adding ship positions. The front/back gate
  narrows relative coordinates to signed 16-bit and shifts each matrix product
  separately, as the original transform does. The host has one rendered view, so
  it uses the one-player threshold. State resets on race initialization and
  renderer changes.
- Saturn: `SaturnShipDepthVisible` implements the original 20 < Z < 4000
  interval, model sphere gate and separate camera-ship path. World Z is converted
  to original 16.16 units (8192 per host world unit). Imported Saturn objects use
  their preserved authored radius; other asset sets use their host bounding
  radius. Course descriptors follow `{1,8,13,20,2,17,6,7}` rather than file order.

Host race selection, internal camera suppression, ghost translucency and road
lighting still apply. A selected hull uses the existing precise object projection
rather than overflowing a PlayStation vector or far ordering-table slot.
Native shadows use the same hull mask; they no longer depend on a previous
frame's PlayStation visibility. The original shadow registration is preserved.

## Changed game functions

- `DrawShips`: separate visual dispatch from unchanged simulation decisions.
- `initShipData`: reset per-race native render history.
- `DrawShadow`: defer Native non-PlayStation shadows to the selected-hull pass.
- `PortNativeShipsEnabled`, `PortNativeShipsReset`, `PortDrawNativeShips`: new
  rendering-only dispatch, state and precise hull/shadow pass.
- `PcNativeShipVisible`, `NativeSaturnCourse`: original PC renderer gate and
  host-course mapping helpers.

The existing full-distance ship/shadow implementations are retained.

## Focused validation

`test_native_ship_dispatch.c` executes the actual `DrawShips` with replacement
rendering disabled and enabled. Ship data, proximity and engine-sound arguments
are byte-identical; only legacy packet submission and native dispatch change.

`test_native_ship_policy.c` executes the actual native pass on a synthetic road,
checks the PC 9599/9600 boundary and section wrapping, N64 remembered visibility,
rejection and reacquisition, and Saturn 160/161/31999/32000 host-unit boundaries.
The actual precise shadow pass emits 16 road quads for each selected hull and
none for rejected hulls. It also checks the PlayStation bypass and inactive
native-scene gate. Packet output, projection recording and an identity skeleton
transform are test doubles; these checks do not assert full original-frame
pixel equivalence. Separate original-ROM/disc policy oracles cover N64/Saturn.

## PC scenery follow-through

The same audit found two boundary differences before the hull pass:
`SceneRadiusCheck` (`45df80`, exact-verified) calls the PC `SquareRoot0`
(`433f10`), which truncates the real square root of an unsigned 32-bit sum,
whereas the PlayStation uses a table approximation. `PcNativeObjectInRadius`
preserves the original axis tests, jump widening, wrapped squared sum and
extent/1024 subtraction. The native scene cache uses that predicate separately
from the PlayStation candidate lists and retains initialization-time positions.

`DrawSectionObjects` (`43fde0`, exact-verified in the same PowerVR symbol files)
uses integer composed skeleton translations, near depth 2048, the selected F3
far distance, and integer screen bounds. `NativePcSkeletonMatrix` reproduces
full-world composition with wrapped dot products and arithmetic shifts;
`PcNativeSceneryVisible` preserves the strict near/far boundaries, original Y
clamp and integer screen-radius division. `PortDrawNativeScenery` performs
those checks before precise projection. Sharing the integer composition helper
with N64 does not halve PC coordinates.

`PortDrawNativeTrack` resets native ship history while Full/PlayStation dispatch
is selected, so re-enabling Native cannot revive stale remembered N64 state.
`test_native_scene_dispatch.c` executes that reset and the actual Native road
and scenery dispatch, including PC near/far, frustum, composition-rounding and
candidate-radius boundaries, native topology differences, and PS1 bypass.
