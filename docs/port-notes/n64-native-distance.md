# Wipeout 64 Native visibility

Reference: USA big-endian ROM, SHA-256
`fdcf4ae70d90abef4f299b04664e009a967e55f97a065b43f596b749dd7d6b15`.
Code addresses below are virtual addresses. Their ROM offset is address minus
`0x80000000` plus `0xc00`. The verification script reads the user's image and
does not save extracted assets.

## Road selection

`platform/render/n64_visibility.c` implements the dispatch at `0x80017760`
and list construction at `0x80016e34`. This replaces the inherited PS1 authored
forward view list when the N64 profile uses Native distance.

- Base budgets: 22 sections for one player (20 on course 7), 18 for two,
  14 for three and 10 for four. The reduced-view flag makes the last two 4.
- Forward dispatch ordinarily walks base minus two, then two backwards.
  Three/four-player forward dispatch walks the whole base and skips the
  ordinary backward run. Auxiliary cameras can add that run.
- Courses 1, 4 and 5 have original section-number intervals with budgets
  12, 15 or 18. Imported section numbers are preserved separately because
  the host renumbers sections for its gameplay.
- Looking sideways splits the budget using the original integer quotient
  `1000/base` and single-precision division. Minimum allocations are eight
  each, or two each for four players. The signed angle boundary -1536 is
  sideways; +1536 is backwards.
- The original angle calculation narrows section deltas to signed 16 bits,
  rounds a ratio into a 4097-entry atan table, and drops four result bits.
  All table entries match the mathematical generator used by the host.
- A nearby junction can move the starting section two steps and extend the
  count. Alternate sections interleave into the same budget, with at most
  ten alternate entries. They do not receive a second independent budget.
- The start camera enables authored south lists for fewer than three
  players. Those runs advance through contiguous section records
  (`0x80016d84`), not next-section links; low-detail runs cap at 100.

The host adapter uses its supported single view. Multiplayer policy branches
are recovered and instruction-tested, but this does not add N64 multiplayer.
For a single view the auxiliary runs only duplicate already selected sections.

## Scenery

The import preserves original PRM translations, extents and TRV coordinates.
The original PRM loader halves each relative skeleton translation at
`0x80009fdc..0x8000a00c`; cameras do the same before matrix composition.
`NativeN64SkeletonMatrix` therefore halves each relative translation before
using the original wrapped integer composition (`0x80019ce0/0x800194f0`).
Halving the final floating-point depth would produce different edge rounding.

The depth gate `0x80029180..0x800291b4` uses radius `extent >> 1`. It requires
`depth + radius > near` and `depth - radius < far`. The tables at
`0x80091c44/64` contain near 2048 and far 38200 for indices 0..6, then
3072/37000 at index 7. Initialization at `0x80029b60..0x80029b80` sets the
index to course minus one, so all seven shipping courses use 2048/38200.
The following projected sphere bounds use scale 160 and **120 on both axes**
(`0x80029270..0x800292a4`).

Scenery candidates also matter: the original radius check at
`0x8001a6c4..0x8001aac8` doubles the already-halved PRM position, subtracts
the section centre, applies global and local axis bounds, then compares its
integer magnitude minus extent minus 1024 with the radius. The local search
visits at most 30 next and 30 previous sections. One-player mode retains the
authored radii. `PortBuildNativeScene` captures independent PC/N64 candidate
lists at the end of `SceneRadiusCheck`, before animation, regardless of the
currently selected profile. Switching profiles thus cannot reuse a PS1
candidate list whose rounding omitted an object. The cache is freed and
rebuilt on every scene initialization and is guarded by the track identity.

## Ships

The gate is `0x8001136c..0x800113e8`, with rejection cleanup at
`0x80011c64`. Per-view history starts at 1 (`0x80012e70..0x80012e7c`).
Magnitude below 1200 (one player), 1000 (two), or 450 (three/four) sets it to 1.
Destroyed ships, a zero history byte, or nonpositive view depth reject a ship;
the camera ship bypasses the depth condition. Only a **rejected** ship with
magnitude at least 2900 clears history. A remembered ship in front can remain
visible beyond 2900. This is not a simple radial cutoff or ordinary hysteresis.
There is no PS1 section-distance gate here.

The camera's half-world translation is doubled before adding the full-world
ship position. Magnitude shifts those deltas by three, uses wrapped 32-bit
squares, and calls `0x80019f90`: normalized lookup estimate plus one integer
Newton step, capped at 65535. The implementation derives the same lookup
values (including its capped index 256), rather than using the PS1 square root.
For the front/back gate the deltas narrow to signed 16 bits; the original
transform shifts each matrix product separately (`0x80014df4`). Render history
is separate from the PS1 gameplay flags and resets on race/profile entry.

## Changes and checks

- `platform/render/n64_visibility.{h,c}`: recovered pure policies and integer
  arithmetic; no console pointers or extracted data in host code.
- `game/src/portfar.c`: N64 graph adapter, exact scenery matrix/bounds, and
  immutable per-load PC/N64 candidate lists. New rendering-only behavior.
- `game/src/radscn.c`: `SceneRadiusCheck` calls the cache builder after the
  original list construction. `oracle32` against HEAD reports only that
  function changed, 589 to 592 assembly lines, because of this added call.
  Existing PS1 list construction is unchanged.
- `game/src/draw.c`: Native landscape dispatch bypasses the inherited
  candidate list and increments `waveIndex` once, as the original path does.
- `game/include/portfar.h`: declarations for those rendering-only helpers.
- `tools/bugtest/test_native_scene_dispatch.c`: actual integration tests for
  Native depth boundaries, per-node odd-coordinate rounding, profile changes,
  candidate-list differences, animation snapshot behavior and cache rebuild.

Run the original-code comparison with Python containing Unicorn:

```sh
python tools/bugtest/verify_n64_distance_rom.py '/path/to/Wipeout 64 (USA).z64'
```

It compiles the host policy and compares it against original MIPS instructions:
5,043 square-root, 2,000 magnitude, 768 stateful ship-gate, 504 scenery-depth,
2,400 junction-walker, 3,000 angle, 2,824 road-dispatch, 2,000 ship-transform,
2,000 matrix-composition, 3,000 candidate-radius, and 2,000 complete scenery
bounds cases. It also verifies all 4,097 atan table entries, player thresholds,
initial ship-gate boundaries and the loader's odd positive/negative translations.
Dispatch comparison substitutes the independently verified walker for its
rendering calls; triangle submission and the complete RSP/RDP frame are not
emulated. These results establish the tested visibility rules, not whole-frame
console equivalence or N64 gameplay equivalence. Diagnostics for `radscn.c`
and `portfar.c` are clean.

Intentional machine semantics include signed shifts, narrowing, unsigned
32-bit wrap, and single-precision road allocation. No unrelated game behavior,
generated data, protected routines or file-record representations were changed.
