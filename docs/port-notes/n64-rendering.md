# Wipeout 64 rendering: investigation and validation record

This note preserves the detailed investigation and historical results moved
from [N64_RENDERING.md](../N64_RENDERING.md). See that guide for rendering
behavior, current validation commands and overall limitations. The results
below were recorded during the implementation work; they were not rerun for
this documentation reorganization.

## ROM filtering investigation

The USA ROM selects `G_TF_BILERP` for its filtered 3D render states. On N64
this blends three corners of a texel square, choosing the triangle containing
the sample, rather than OpenGL's usual four-corner bilinear filter.

Inspected the 8 MiB big-endian USA cartridge image with SHA-256
`fdcf4ae70d90abef4f299b04664e009a967e55f97a065b43f596b749dd7d6b15`.
The boot mapping places ROM offset `0x1000` at virtual address `0x80000400`.

The render-state builder at `0x8000973c` was disassembled with Capstone and
executed with Unicorn for each of its six mode arguments. Its emitted
display lists contain these texture-filter commands:

| Mode argument | Command words | Meaning |
| --- | --- | --- |
| 0, 1, 2, 3, 5 | `BA000C02 00002000` | `G_TF_BILERP` |
| 4 | `BA000C02 00000000` | `G_TF_POINT` |

Modes 0, 1 and 2 also explicitly enable perspective texturing. Mode 4 calls
the orthographic setup at `0x80035498`. The static initialization lists agree:
ROM offset `0x993e8` selects BILERP in the list with perspective enabled;
offset `0x99500` selects POINT in the list with perspective disabled.
This confirms the filtered material setting, rather than assuming every
Wipeout 64 draw uses the same filter. It is an isolated routine check, not
a recording of every command emitted during a race.

Reproduce with a local ROM and a Python environment containing `unicorn`:

```sh
python tools/bugtest/verify_n64_filter_rom.py "../roms/Wipeout 64 (USA).z64"
```

The verifier runs the original instructions until the display list is
complete, stopping before cache writeback. It saves no cartridge data.

## ROM culling investigation

In Klies Bridge, `rock27` has views where triangle `(0,1,2)` faces away but
`(2,1,3)` faces the camera. XL's whole-quad facing test discarded that visible
second half, producing holes in the snowy mountain near screen edges.

The USA ROM's culling-state block at `0x8000b458` sets/clears geometry mode
`0x1000` from primitive flag bit 0. Its unlit F4, FT4, G4 and GT4 emission
blocks at `0x8000b550`, `0x8000ba50`, `0x8000bc78` and `0x8000c2c8` emit
`G_TRI2`: `(0,1,2)` followed by `(2,1,3)`, with each triangle subject to
culling. These blocks share the second-triangle packer at `0x8000d908`.

The culling work recorded an isolated Unicorn check of those command-emission
blocks with synthetic flags and cached vertex indices. That verifier,
`tools/bugtest/verify_n64_quads_rom.py`, is not included in the current checkout.
The check did not replay the whole renderer or establish console equivalence.

## Scenery conversion details

`Wipeout64Parts` calls `PortPrmTriangulateN64` on copied scenery after texture
remapping and colour normalization. The unlit F4, FT4, G4 and GT4 records
become triangle pairs.

The helper validates and measures the complete input before writing. It
rejects truncated/unknown primitive records, output-size overflow and signed
primitive-count overflow. `Wipeout64Parts` replaces its temporary copy only
after allocation and conversion succeed. File-header offsets come from the
record tables; primitive attributes use `offsetof`/`sizeof` on pointer-free
records. Serialized pointers are never treated as native pointers.

The conversion retains the existing vertex-only walk for these unlit scenery
images; handling of normal arrays in unrelated lit PRM images and existing
raw serialized offsets in the importer is unchanged. Triangle ordering uses
the existing triangle renderer's average depth.

## Imported-track trophy colours

`AssignMedals` in `game/src/hud.c` sets the trophy's environment-map colour
for first, second and third place before returning for an imported track.
Both Wipeout 64 and original Wipeout tracks therefore show the correct gold,
silver or bronze finish colour. They still award no XL medals or progression
for Talon's Reach, whose slot hosts the imported race. Native XL medal
and progression rules are retained.

## Historical validation

The CPU and OpenGL suites (`tools/bugtest/run.sh` and
`tools/bugtest/render.sh`), the native build and the USA filtering verifier
passed during branch validation. The culling work also recorded passing tests
for serialized triangle attributes, multiple objects, malformed input, count overflow and
the captured Klies Bridge facing regression. Its screen-edge test checked
all four edges, perspective geometry, unchanged integer GTE state and retained
flat-polygon rejection. Those additional culling test cases and
`tools/bugtest/test_screen_edge.c` are not included in the current checkout.

Other recorded checks include the USA culling command-block verifier,
a targeted ASan/UBSan import test and clean `tools/diag.py` output for
`port_widen.c`.
Recorded native replay validation used 5,500 fields of Klies Bridge autopilot:
132 of 183 captures changed, restoring missing mountain faces, notably at
captures 2820 and 4260. A 4,300-field packaged-app replay also passed and its
capture 4260 matched the standalone replay. Local artifacts remain ignored
under `build/near-scenery-repro`; disc and ROM data were not changed.

The MIPS oracle command `python3 tools/oracle32.py diff B0 port_widen.c`
could not compile because its include environment lacked `sys/types.h`.
Its trailing “0 function(s) differ” is not a successful comparison. No console
lockstep recording was available. No new missing returns, absolute console
memory accesses or MIPS arithmetic changes were introduced by the culling
conversion, and no generated data was changed.

## References

- [Nintendo's texture-filter tutorial](https://jrra.zone/n64/doc/tutorial/graphics/3/3_6.htm)
  describes BILERP as interpolation of three texels.
- [GBI definitions](https://github.com/glankk/n64/blob/master/include/n64/gbi.h)
  identify texture-filter bits 12–13 and the POINT, BILERP and AVERAGE values.
- [ParaLLEl-RDP's texture implementation](https://github.com/Themaister/parallel-rdp/blob/master/parallel-rdp/shaders/texture.h)
  documents the five fractional bits, diagonal selection and separate
  centre-average mode.
