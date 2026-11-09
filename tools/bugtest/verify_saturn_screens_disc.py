#!/usr/bin/env python3
"""Audit scenery dispatch in the European Saturn retail disc (raw BIN).

Read-only; no extraction, emulator, or third-party Python packages required.
This verifies disc records and executable branches, not rendered pixels.
"""
import argparse
from collections import Counter, defaultdict
import hashlib
import mmap
from pathlib import Path
import re
import struct


BASE = 0x06010000
EXE_SHA256 = "54509b22e246497f96fdee469ba3cd5c46c3440611fa9af43e689373fc3fbbd2"
ARCHIVE_SHA256 = "a73c3974a0d58ea116b0a3908937ee1c31d9048527e24f27ae3481fc03284843"
TRACKS = (1, 2, 6, 7, 8, 13, 17, 20)


def u16(data, offset=0):
    return struct.unpack_from(">H", data, offset)[0]


def u32(data, offset=0):
    return struct.unpack_from(">I", data, offset)[0]


def disc_files(image):
    def read(lba, size):
        return b"".join(image[i * 2352 + 16:i * 2352 + 2064]
                        for i in range(lba, lba + (size + 2047) // 2048))[:size]

    pvd = read(16, 2048)
    assert pvd[:7] == b"\x01CD001\x01", "Expected a raw MODE1/2352 data track"

    def record_data(record):
        return read(int.from_bytes(record[2:6], "little"),
                    int.from_bytes(record[10:14], "little"))

    directory = record_data(pvd[156:190])
    result = {}
    at = 0
    while at < len(directory):
        size = directory[at]
        if not size:
            at = (at // 2048 + 1) * 2048
            continue
        record = directory[at:at + size]
        at += size
        name = record[33:33 + record[32]].split(b";")[0]
        if name in (b"0.AAA", b"CDIMAGE.ELF"):
            result[name] = record_data(record)
    return result[b"0.AAA"], result[b"CDIMAGE.ELF"]


def course_blocks(archive, track):
    key = 0
    for char in f"art\\track{track:02}\\course{track:02}.rbh".encode():
        key = (key * 2 + char) & 0xffffffff
    entries = [16 + i * 16 for i in range(u32(archive))]
    matches = [at for at in entries if u32(archive, at) == key]
    assert len(matches) == 1
    at = matches[0]
    start = u32(archive, at + 4) * 2048
    data = archive[start:start + u32(archive, at + 8)]
    assert data[:4] == b"BIFF" and data[8:16] == b"RBHFRBHH"
    count = u32(data, 16) // 12
    at = 20 + count * 12
    blocks = []
    for _ in range(count):
        size = u32(data, at + 4)
        blocks.append(data[at + 8:at + 8 + size])
        assert len(blocks[-1]) == size
        at += 8 + size
    return blocks


def labels_for(source, track):
    # Labels are the rewrite's generated correspondence, not retail symbol names.
    pool_text = source.split(f"static const char names{track:02}[] =", 1)[1].split(";", 1)[0]
    pool = "".join(bytes(part, "ascii").decode("unicode_escape")
                   for part in re.findall(r'"([^"]*)"', pool_text))
    indices = re.search(r"index%02d\[\d+\] = \{(.*?)\}" % track, source, re.S)[1]
    return [pool[int(offset):].split("\0", 1)[0] for offset in re.findall(r"\d+", indices)]


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("bin", type=Path)
    args = parser.parse_args()
    with args.bin.open("rb") as stream, mmap.mmap(stream.fileno(), 0, access=mmap.ACCESS_READ) as image:
        exe, archive = disc_files(image)
    assert hashlib.sha256(exe).hexdigest() == EXE_SHA256, "Expected European Saturn 0.AAA"
    assert hashlib.sha256(archive).hexdigest() == ARCHIVE_SHA256, "Expected European Saturn archive"

    table = 0x0602217c
    targets = [table + struct.unpack_from(">h", exe, table - BASE + i * 2)[0] for i in range(12)]
    assert targets == [0x06022194, 0x0602220c, 0x06022280, 0x060222f4,
                       0x06022308, 0x06022396, 0x06022532, 0x060223c0,
                       0x06022420, 0x06022532, 0x06022532, 0x060224b2]
    # Type 4's BRA goes directly to the normal placement/draw path. Its delay
    # slot loads instance Z; there is no frame selector or texture write here.
    branch = u16(exe, 0x06022304 - BASE)
    assert branch >> 12 == 0xa
    displacement = (branch & 0xfff) - (0x1000 if branch & 0x800 else 0)
    assert 0x06022304 + 4 + displacement * 2 == 0x06022432
    assert u16(exe, 0x06022306 - BASE) == 0x5693
    # Type 7 goes straight to the next-instance loop, before all draw paths.
    assert exe[0x06022532 - BASE:0x0602253a - BASE].hex() == "50f951f770011f09"
    # Type 6 multiplies the scenery tick by 20 and calls the rotation routine.
    assert exe[0x060223ac - BASE:0x060223bc - BASE].hex() == "d111611264134408341cd119410b4408"

    names = (Path(__file__).resolve().parents[2] / "platform/saturn_scene_names.c").read_text()
    totals = Counter()
    print("track  Red Bull (static)  rotating adverts  screens (skipped)  screen indices")
    for track in TRACKS:
        blocks = course_blocks(archive, track)
        sections, faces, models = blocks[:3]
        labels = labels_for(names, track)
        table_at = u32(sections, 20)
        for index in range(len(labels)):
            header = u32(models, table_at + index * 16)
            assert u32(models, header + 8) == header
        seen = defaultdict(set)
        for section in range(u32(sections)):
            at = 0x1b4 + section * 172
            count, offset = u16(sections, at + 114), u32(sections, at + 116)
            assert offset + count * 2 <= len(faces)
            for n in range(count):
                value = u16(faces, offset + n * 2)
                index, kind = value & 0xfff, value >> 12
                assert index < len(labels) and kind <= 12
                seen[index].add(kind)
        assert len(seen) == len(labels) and all(len(kinds) == 1 for kinds in seen.values())
        groups = {}
        for prefix, kind in (("redb", 4), ("AGunit", 6), ("screen", 7)):
            indices = [i for i, label in enumerate(labels) if label.startswith(prefix)]
            assert indices == sorted(i for i, kinds in seen.items() if kinds == {kind})
            groups[prefix] = indices
            totals[prefix] += len(indices)
        print(f"{track:5}  {len(groups['redb']):17}  {len(groups['AGunit']):16}  "
              f"{len(groups['screen']):17}  {groups['screen']}")
    assert totals == {"redb": 25, "AGunit": 7, "screen": 5}
    print("PASS: retail hashes, dispatcher branches, and every course's scenery tags agree.")
    print("Static code/data audit only; no emulator or visual equivalence assertion.")


if __name__ == "__main__":
    main()
