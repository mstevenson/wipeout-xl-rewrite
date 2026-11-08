# Saturn native draw distance

Reference: European WipEout 2097 Saturn `0.AAA`, loaded at `06010000`,
SHA-256 `54509b22e246497f96fdee469ba3cd5c46c3440611fa9af43e689373fc3fbbd2`.
Addresses below are original SH-2 addresses, not host addresses. The original
image and extracted game files remain external to the repository.

## Units and course range

The Saturn world is one eighth the host world. Matrix translations and model
sphere radii use Saturn 16.16 coordinates: multiply host units by 8192.
Model-header offset 24 contains the authored sphere radius. The importer now
retains it independently of the compatible PRM's reconstructed vertex extent;
this also preserves billboard bounds and does not change other profiles.

The eight 40-byte descriptors at `06024188` contain course IDs
1, 8, 13, 20, 2, 17, 6, 7. The unsigned halfword at offset 8 is 5000 for the
first six courses and 4000 for the two bonus courses. Context creation at
`06045abc` rounds this up to eight. For a 16-bit drawing context, `0604ddb0`
rounds up to sixteen and halves the result. The final context depth buckets
are therefore **2504 and 2000**, equivalent to host depths **40064 and 32000**.
`060541bc` copies this value, shifted by sixteen, to camera offset 48.
An original Phenitia Park demo save independently has context far 2504 and
camera far `09c80000`.

## Scenery and ships

The scenery traversal at `0602201c` uses each section's authored object list.
The count is section offset 114 and the offset into course block 1 is at 116.
Entries are visited in stored order; their low twelve bits identify the scene
instance and their high nibble selects a special animation/draw handler.
`0602214e..06022166` strips that nibble before the instance lookup. Every entry
in all eight original courses resolves to a valid scene instance, including
special types 1..9 and 12. The importer retains these lists separately from
XL's generated proximity lists and restores them on cached course loads.
Its ordinary object path calls sphere culler `0604c3a8`. The depth portion
at `0604c3ce..0604c3f6` halves centre depth and radius separately, truncating
towards zero, rejects `halfZ + halfRadius < 0`, and rejects
`halfZ - halfRadius >= cameraFar`. The following original code performs
viewport clipping; the host still performs its normal viewport clipping.
A large object's centre can therefore lie beyond the far plane while its
sphere remains visible.

Billboard models take `06021ee8` with flag 4 and bypass the coarse sphere test.
Their primitives still obey the model polygon depth gate. Model group bits
14..15 select the table at `0605d780`: average of four integer depth buckets,
maximum, minimum, or maximum plus 32 buckets. Each path rejects a bucket at or
beyond the context far value. Maximum modes also reject negative buckets;
average/minimum modes clamp them to zero for ordering. The converter preserves
the raw sorting bits independently of compatibility transparency flags.

Native billboard rendering retains and draws the original individual quads,
materials and depth modes, so a composite's far pieces disappear independently.
The compatibility composite remains in use with Full enabled. Native smoke
pieces retain the shared frame animation. The billboard regression test covers
partial dropout, the exact course far boundary and switching back to Full.

The camera ship follows the separate branch at `06028306`, without a distance
or sphere test. Other ships translate their positions by shifting left thirteen
at `06028520`. The unsigned comparison at `06028562..0602856c` accepts exactly
`20 < z < 4000` in Saturn units, or **160 < z < 32000** in host units. The
selected model subsequently undergoes the same sphere test at `06028616`.
The combat visibility and LOD comparisons use other thresholds; neither is
the ship draw-distance gate. Native rendering must not overwrite gameplay
visibility flags to obtain its render visibility.

## Road

`06022584` selects directional authored spans. `06022fd8` selects all-view
spans. Both submit contiguous section-array runs; the callback `060232b0`
advances by the original 172-byte section size and does not impose a 100-section
limit or follow section topology links.
The direction angle is `(cameraYaw - heading(nextCentre - centre)) & 4095`.
Its four ranges are 0..511 plus 3584..4095 (list offsets 24/28/32),
512..1535 (60/64/68), 1536..2559 (36/40/44), and 2560..3583
(48/52/56), as dispatched at `060225ee..06022634`.

Road projection `0605f488..0605f498` adds the matrix's z translation,
arithmetically halves the fixed depth, and stores its signed integer part.
Low/medium tile emission `06014364..06014382` finds the maximum of four corner
depths and rejects it with an unsigned comparison against context far. High
detail `060144c0..060144ea` finds the maximum fixed half-depth, extracts the
integer part and makes the same comparison. Thus a tile whose farthest corner
reaches the far bucket is rejected as a whole. This is not geometric clipping
against a far plane. Negative maximum depth is also rejected.

## Executable verification

Run with an externally extracted original executable:

```sh
python3 tools/bugtest/verify_saturn_distance.py /path/to/0.AAA
```

The script validates the executable hash and original descriptor table, compiles
the host policy, and executes the original SH-2 ship/sphere/projection/tile
branch instructions in a deliberately small, fail-closed interpreter. It checks
21,200 sphere/ship combinations, including single fixed-bit boundaries, and
160 road boundary cases across every course, 32,000 model-polygon cases and
36,864 direction dispatch cases. All 1025 original atan table entries are
checked against the rounded analytic formula used by the host. Camera-ship bypass is checked
separately. This verifies the recovered depth predicates against original
instructions; it does not claim a whole-frame pixel equivalence test.
