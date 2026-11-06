# PC billboards

PC billboards now follow the original Direct3D `LoadPrmCommon`/`BuildQuads`
geometry: upright 3D quads with the original sizing and facing, per-corner
depth, perspective-correct textures, clipping, and depth testing/writes.
World sprites receive PC modulation; HUD brightness and the PS1 path are preserved.

The PC geometry policy selects `DrawWorldSprite` in `objdraw.c`, using the
existing projection, interpolation and clipping infrastructure. Its floating-point
geometry is an intentional rendering change; packet and asset layouts are unchanged.

Validation passed:

- CPU/GPU regression suites and targeted AddressSanitizer/UBSan checks.
- macOS build and game-source diagnostics; six existing declaration discrepancies remain.
- Scripted PC/PS1 race runs; all eight PS1 captures matched the previous executable byte-for-byte.
- MIPS oracle against pre-change commit `55c50cf`: all 14 object-renderer functions
  match with the production PS1 geometry policy. The full runtime comparison
  changes only `DrawObject` (1 of 706 functions), which contains the new PC branch.

Oracle headers, reproducible snapshots and failure handling are repaired; see
[`tools/oracle32/README.md`](../../tools/oracle32/README.md) for commands.
Console lockstep recordings were unavailable; retail pixel equivalence remains unverified.
