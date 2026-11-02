#!/usr/bin/env python3
"""Verify Wipeout 64 USA's texture-filter settings using a supplied .z64 ROM.

Requires the optional Python unicorn package. Runs the ROM's render-state
builder in isolation and reads the display lists it writes. No ROM bytes or
emulator snapshots are saved. See docs/N64_RENDERING.md for the interpretation.
"""
import argparse
import hashlib
from pathlib import Path
import struct

from unicorn import Uc, UC_ARCH_MIPS, UC_MODE_MIPS32, UC_MODE_BIG_ENDIAN
from unicorn.mips_const import (
    UC_MIPS_REG_A0, UC_MIPS_REG_A1, UC_MIPS_REG_PC, UC_MIPS_REG_RA,
    UC_MIPS_REG_SP,
)

parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument("rom", type=Path)
args = parser.parse_args()
rom = args.rom.read_bytes()
assert hashlib.sha256(rom).hexdigest() == "fdcf4ae70d90abef4f299b04664e009a967e55f97a065b43f596b749dd7d6b15", \
    "Expected the USA big-endian .z64 ROM"

# Fast3D/F3DEX OtherMode-H: write two texture-filter bits starting at bit 12.
SET_FILTER = 0xba000c02
POINT = 0x00000000
BILERP = 0x00002000
END_LIST = 0xb8000000

# The static setup lists also select BILERP with perspective on, and POINT
# with perspective off. These offsets are ROM offsets, not virtual addresses.
for offset, expected in ((0x993e8, BILERP), (0x99500, POINT)):
    assert struct.unpack_from(">II", rom, offset) == (SET_FILTER, expected)
    print(f"Static setup at ROM 0x{offset:06x}: {'G_TF_BILERP' if expected else 'G_TF_POINT'}")

for mode in range(6):
    cpu = Uc(UC_ARCH_MIPS, UC_MODE_MIPS32 | UC_MODE_BIG_ENDIAN)
    cpu.mem_map(0, 0x800000)
    # The cartridge boot code loads offset 0x1000 at virtual address 0x80000400.
    cpu.mem_write(0x400, rom[0x1000:])
    cpu.reg_write(UC_MIPS_REG_A0, 0x80600000)  # output display list in scratch RAM
    cpu.reg_write(UC_MIPS_REG_A1, mode)
    cpu.reg_write(UC_MIPS_REG_SP, 0x807ff000)
    cpu.reg_write(UC_MIPS_REG_RA, 0x80000000)
    # All six paths finish by calling cache writeback after the last command.
    # Stop there: cache maintenance is unnecessary for inspecting their output.
    cpu.emu_start(0x8000973c, 0x8007a300, count=10000)
    assert cpu.reg_read(UC_MIPS_REG_PC) == 0x8007a300, f"Mode {mode} did not finish"
    data = bytes(cpu.mem_read(0x600000, 0x100))
    filters = []
    for offset in range(0, len(data), 8):
        command, value = struct.unpack_from(">II", data, offset)
        if command == SET_FILTER:
            filters.append(value)
        if command == END_LIST:
            break
    else:
        raise AssertionError(f"Mode {mode} emitted no display-list terminator")
    expected = POINT if mode == 4 else BILERP
    assert filters == [expected], f"Mode {mode}: unexpected texture filters {filters}"
    print(f"Render-state mode {mode}: {'G_TF_BILERP' if expected else 'G_TF_POINT'}")

print("USA ROM: filtered render states use three-point G_TF_BILERP; orthographic mode 4 uses POINT: OK")
