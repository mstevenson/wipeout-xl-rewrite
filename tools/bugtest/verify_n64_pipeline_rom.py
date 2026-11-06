#!/usr/bin/env python3
"""Inspect Wipeout 64 USA's dither, VI, palette, tile and vertex-colour routines.

Requires unicorn. Executes isolated original MIPS routines with synthetic RAM;
this is not a race/display-list replay or a full RSP/RDP emulator. Never writes
the ROM or saves extracted assets. See docs/port-notes/n64-pipeline-investigation.md.
"""
import argparse
from collections import Counter
import hashlib
from pathlib import Path
import struct

from unicorn import Uc, UC_ARCH_MIPS, UC_MODE_MIPS32, UC_MODE_BIG_ENDIAN, UC_HOOK_CODE
from unicorn.mips_const import (
    UC_MIPS_REG_A0, UC_MIPS_REG_A1, UC_MIPS_REG_PC, UC_MIPS_REG_RA,
    UC_MIPS_REG_SP, UC_MIPS_REG_V0, UC_MIPS_REG_S0, UC_MIPS_REG_S1,
    UC_MIPS_REG_S2, UC_MIPS_REG_S3, UC_MIPS_REG_S4, UC_MIPS_REG_T5,
    UC_MIPS_REG_FP,
)

parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument("rom", type=Path)
args = parser.parse_args()
rom = args.rom.read_bytes()
assert hashlib.sha256(rom).hexdigest() == "fdcf4ae70d90abef4f299b04664e009a967e55f97a065b43f596b749dd7d6b15", \
    "Expected the USA big-endian .z64 ROM"


def cpu():
    result = Uc(UC_ARCH_MIPS, UC_MODE_MIPS32 | UC_MODE_BIG_ENDIAN)
    result.mem_map(0, 0x800000)
    result.mem_write(0x400, rom[0x1000:])
    result.reg_write(UC_MIPS_REG_SP, 0x807ff000)
    result.reg_write(UC_MIPS_REG_RA, 0x80000000)
    return result


def run(c, start, end=0x80000000, count=10000):
    c.emu_start(start, end, count=count)
    assert c.reg_read(UC_MIPS_REG_PC) == end, f"Routine {start:08x} did not finish"


def commands(data):
    for offset in range(0, len(data), 8):
        a, b = struct.unpack_from(">II", data, offset)
        yield a, b
        if a == 0xb8000000:
            return
    raise AssertionError("Display list has no terminator")


# OtherMode-H bits 6..7: RGB dither 0 = magic square, 3 = disabled.
# Mode lists are state DELTAS; absent settings are inherited, not necessarily off.
render_modes = (0x00552230, 0x00552220, 0x0f0a7038, 0x0f0a4000, 0x00504240, 0x00504a50)
for mode in range(6):
    c = cpu()
    c.reg_write(UC_MIPS_REG_A0, 0x80600000)
    c.reg_write(UC_MIPS_REG_A1, mode)
    run(c, 0x8000973c, 0x8007a300)  # stop before cache writeback
    dl = list(commands(bytes(c.mem_read(0x600000, 0x100))))
    assert [b for a, b in dl if a == 0xba000602] == [0xc0 if mode == 4 else 0]
    assert [b for a, b in dl if a == 0xba000c02] == [0 if mode == 4 else 0x2000]
    assert [b for a, b in dl if a == 0xb900031d] == [render_modes[mode]]
    assert not any(a == 0xb7000000 and b & 0x20000 for a, b in dl)  # no G_LIGHTING enable
    print(f"State {mode}: RGB dither={'OFF' if mode == 4 else 'MAGICSQ'}, "
          f"filter={'POINT' if mode == 4 else 'BILERP'}, render mode={render_modes[mode]:08x}")
    for a, b in dl:
        if a >> 24 == 0xfc:
            print(f"  combiner: {a:08x} {b:08x}")
            if mode == 1:
                # Both RGB cycles are (TEXEL0 - 0) * PRIMITIVE + 0, not SHADE.
                assert ((a >> 20) & 15, (b >> 28) & 15, (a >> 15) & 31, (b >> 15) & 7) == (1, 15, 3, 7)
                assert ((a >> 5) & 15, (b >> 24) & 15, a & 31, (b >> 6) & 7) == (1, 15, 3, 7)
    assert not any(a == 0xba000402 for a, b in dl)  # alpha dither not explicitly selected

for offset in (0x99418, 0x99530):
    assert struct.unpack_from(">II", rom, offset) == (0xba000602, 0xc0)
assert struct.unpack_from(">I", rom, 0x99470)[0] == 0xff10013f  # RGBA16, width 320
print("Static setup: dither OFF; colour framebuffer RGBA16, width 320")

# NTSC startup chooses table entry 2, then calls osViSetSpecialFeatures(0x40)
# and osViSetSpecialFeatures(2). Check the actual call instructions too.
assert struct.unpack_from(">4I", rom, 0x662c4 + 0xc00) == (
    0x0c01e164, 0x24040040, 0x0c01e164, 0x24040002)
assert struct.unpack_from(">I", rom, 0x66248 + 0xc00)[0] == 0x24070002  # NTSC mode 2
mode_address = 0x800974b0
mode = struct.unpack_from(">20I", rom, mode_address - 0x80000000 + 0xc00)
assert mode[0] == 0x02000000 and mode[1] == 0x311e and mode[2] == 320
c = cpu()
c.mem_write(0x981b4, struct.pack(">I", 0x80600000))  # __osViNext
c.mem_write(0x600008, struct.pack(">II", mode_address, mode[1]))


def skip_interrupt_control(c, address, size, user):
    # The two OS helpers only change CPU interrupt state, not VI configuration.
    if address in (0x800767c0, 0x800767e0):
        c.reg_write(UC_MIPS_REG_V0, 0)
        c.reg_write(UC_MIPS_REG_PC, c.reg_read(UC_MIPS_REG_RA))


c.hook_add(UC_HOOK_CODE, skip_interrupt_control)
for flags in (0x40, 2):
    c.reg_write(UC_MIPS_REG_A0, flags)
    c.reg_write(UC_MIPS_REG_RA, 0x80000000)
    run(c, 0x80078590)
control, = struct.unpack(">I", c.mem_read(0x60000c, 4))
assert control == 0x13016
print("VI control 00013016: 16-bit, de-dither ON, divot ON, AA mode 0, gamma OFF")
print("  Gamma-dither bit remains set; gamma correction itself is disabled")

# Exhaustively exercise the loader's little-endian TIM -> RGBA5551 palette conversion.
c = cpu()
c.mem_write(0x600000, struct.pack("<65536H", *range(65536)))
c.reg_write(UC_MIPS_REG_A0, 0x80600000)
c.reg_write(UC_MIPS_REG_A1, 65536)
run(c, 0x80010454, count=2500000)
for value, actual in enumerate(struct.unpack(">65536H", c.mem_read(0x600000, 131072))):
    expected = ((value & 31) << 11) | (((value >> 5) & 31) << 6) | \
               (((value >> 10) & 31) << 1) | int(bool(value & 0x7fff))
    assert actual == expected, f"Palette conversion mismatch at {value:04x}"
print("Palette: all 65,536 inputs match; bit 15 ignored, zero RGB transparent")

# Texture-descriptor builder: mirror flags and power-of-two masks for CI4/CI8.
for depth in (0, 1):
    for flags in (0, 0x10, 0x20, 0x30, 0x40, 0x70):
        c = cpu()
        descriptor = bytearray(512)
        struct.pack_into(">h", descriptor, 0, depth)
        struct.pack_into(">hh", descriptor, 6, 32, 32)
        struct.pack_into(">IIHHI", descriptor, 12, 0x80610000, 0x80620000, 0x8000, 0x8000, flags)
        c.mem_write(0x600000, bytes(descriptor))
        c.reg_write(UC_MIPS_REG_A0, 0x80600000)
        run(c, 0x80010988)
        tiles = [(a, b) for a, b in commands(bytes(c.mem_read(0x600020, 256)))
                 if a >> 24 == 0xf5 and (b >> 24 & 7) == 0]
        assert len(tiles) == 1
        a, b = tiles[0]
        assert a == (0xf5400400 if depth == 0 else 0xf5480800)
        expected = (0x14050 if flags else 0) | (0x100 if flags & 0x10 else 0) | \
                   (0x40000 if flags & 0x20 else 0)
        assert b == expected
print("CI4/CI8 tiles: flags 10/20 mirror S/T; flag 40 enables wrapping masks")


def unpack_cmp(data):
    """Read the same MSB-first LZSS stream as platform/original.c, in memory only."""
    count, = struct.unpack_from("<I", data)
    sizes = struct.unpack_from(f"<{count}I", data, 4)
    packed = data[4 + 4 * count:]
    bit_position = 0
    window = bytearray(8192)
    position = 1
    out = bytearray()

    def bits(n):
        nonlocal bit_position
        value = 0
        for _ in range(n):
            value = (value << 1) | ((packed[bit_position >> 3] >> (7 - (bit_position & 7))) & 1)
            bit_position += 1
        return value

    total = sum(sizes)
    while len(out) < total:
        if bits(1):
            value = bits(8)
            out.append(value)
            window[position] = value
            position = (position + 1) & 8191
        else:
            source = bits(13)
            if not source:
                break
            for i in range(bits(4) + 3):
                value = window[(source + i) & 8191]
                out.append(value)
                window[position] = value
                position = (position + 1) & 8191
    assert len(out) == total
    offset = 0
    for size in sizes:
        assert struct.unpack_from("<I", out, offset)[0] == 0x10
        yield struct.unpack_from("<I", out, offset + 4)[0]
        offset += size


# Confirm the flag paths above are used by actual imported track textures.
start = tracks = 0
flags_found = Counter()
while True:
    at = rom.find(b"track.trv", start)
    if at < 0:
        break
    start = at + 1
    at -= 2
    count, = struct.unpack_from("<H", rom, at)
    if count != 14:
        continue
    tracks += 1
    data_offset = at + 2 + count * 25
    for i in range(count):
        entry = at + 2 + i * 25
        name = rom[entry:entry + 16].split(b"\0")[0]
        size, = struct.unpack_from("<I", rom, entry + 16)
        if name in (b"library.cmp", b"sceneCom.cmp", b"sceneSin.cmp", b"sky.cmp"):
            flags_found.update(unpack_cmp(rom[data_offset:data_offset + size]))
        data_offset += size
assert tracks == 7
assert flags_found == {0x8: 390, 0x9: 126, 0x48: 133, 0x58: 10, 0x68: 1, 0x78: 29}
print("Track texture flags (689 images, seven tracks): " +
      ", ".join(f"{key:02x}={value}" for key, value in sorted(flags_found.items())))

# Execute the GT4 colour-copy block, with distinct low/odd/high values at each corner.
c = cpu()
primitive = bytearray(64)
struct.pack_into(">4h", primitive, 4, 0, 1, 2, 3)
colours = ((1, 127, 255), (33, 65, 129), (19, 101, 203), (255, 9, 78))
vertices = bytearray(64)
for i, rgb in enumerate(colours):
    primitive[28 + 4 * i:31 + 4 * i] = bytes(rgb)
    vertices[16 * i + 15] = 1  # this vertex has not yet received its primitive colour
c.mem_write(0x600000, bytes(primitive))
c.mem_write(0x610000, bytes(vertices))
for register, value in ((UC_MIPS_REG_S1, 0x80600000), (UC_MIPS_REG_S2, 0x80620000),
                        (UC_MIPS_REG_S0, 0x80630000), (UC_MIPS_REG_T5, 0x80610000)):
    c.reg_write(register, value)
run(c, 0x8000bccc, 0x8000be68)
for i, rgb in enumerate(colours):
    assert bytes(c.mem_read(0x61000c + 16 * i, 4)) == bytes((*rgb, 0x7f))
print("GT4 vertex colours: original 8-bit RGB copied unchanged; alpha set to 127")

# Track faces have one RGB triplet at offsets 16..18 of each 20-byte record.
# Execute both road emission blocks through the two-triangle command. These
# use primitive colour, unlike the scenery GT4 per-corner colour block above.
for start, end, base_register in ((0x80016c84, 0x80016d64, UC_MIPS_REG_FP),
                                  (0x800175d4, 0x800176b4, UC_MIPS_REG_T5)):
    c = cpu()

    def stop_road_block(c, address, size, user):
        if address == end:
            c.emu_stop()

    c.hook_add(UC_HOOK_CODE, stop_road_block)
    c.reg_write(base_register, 0x806151a4)  # -0x51a4(base) is the display-list cursor
    stack = c.reg_read(UC_MIPS_REG_SP) & 0x1fffffff  # Unicorn API uses physical mapped RAM
    c.mem_write(stack + 0x18, struct.pack(">4I", 0, 1, 2, 3))
    for value in range(256):
        rgb = bytes((value, 255 - value, (value * 37) & 255))
        faces = bytearray(60)
        faces[16:19] = rgb
        faces[36:39] = bytes(255 - channel for channel in rgb)  # different neighbouring face
        c.mem_write(0x600000, bytes(faces))
        c.mem_write(0x610000, struct.pack(">I", 0x80620000))
        c.reg_write(UC_MIPS_REG_S3, 0x80600012)  # current face's blue byte
        c.reg_write(UC_MIPS_REG_S4, 0)
        c.emu_start(start, 0x80000000, count=100)
        assert c.reg_read(UC_MIPS_REG_PC) == end
        emitted = bytes(c.mem_read(0x620000, 24))
        assert struct.unpack_from(">I", emitted)[0] == 0xfa000000  # gDPSetPrimColor
        assert emitted[4:8] == rgb + b"\xff"
        assert struct.unpack_from(">II", emitted, 8) == (0xe7000000, 0)  # pipe sync
        assert struct.unpack_from(">II", emitted, 16) == (0xb1000204, 0x00040600)  # both triangles
    print(f"Road block {start:08x}: one unchanged face RGB for both triangles; no neighbouring-face blending (256 cases)")
print("Isolated USA ROM pipeline checks: OK (not whole-frame equivalence)")
