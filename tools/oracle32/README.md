# MIPS o32 oracle

The oracle compares compiled game functions with a specified Git commit using
Homebrew LLVM. Its declaration-only headers avoid using the host's 64-bit libc.
Snapshots include the commit's `game/` and `platform/` sources, and record the
commit, compiler, flags and header hashes. Existing snapshots are not overwritten.

For the PC billboard change, the pre-change commit is `55c50cf`:

```sh
python3 tools/oracle32.py snapshot 55c50cf pc-billboards-runtime objdraw.c
python3 tools/oracle32.py diff pc-billboards-runtime objdraw.c
python3 tools/oracle32.py snapshot 55c50cf pc-billboards-ps1 objdraw.c --renderer ps1
python3 tools/oracle32.py diff pc-billboards-ps1 objdraw.c --renderer ps1
```

The runtime comparison includes the new PC dispatch. `--renderer ps1` instead
compiles with the snapshot/current production `ps1Renderer.geometry` policy in
the same translation unit, allowing the compiler to remove inactive geometry
paths. Only the injected policy adapter functions are excluded from comparison;
game functions, including the dispatcher and inlined sprite code, are checked.
This verifies compiled PS1-path preservation, not retail GPU pixel equivalence.

Exit codes are 0 for identical functions/a completed snapshot, 1 for differences,
and 2 for missing inputs, incompatible snapshots or tool failures. Optional
source filenames restrict snapshots; without them, all top-level game sources
are compiled. Data-only sources have no functions to compare. `-G0` avoids LLVM's
MIPS-I small-data code-generation failure, and separate function/data sections
keep unrelated symbol placement from changing function-relative comparisons.

Run the infrastructure and PS1 mutation checks with:

```sh
PYTHONDONTWRITEBYTECODE=1 python3 tools/bugtest/test_oracle32.py
```
