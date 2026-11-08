#!/usr/bin/env python3
"""Execute original European Saturn SH-2 visibility branches against the host policy.

Usage: python3 tools/bugtest/verify_saturn_distance.py /path/to/extracted/0.AAA
The input remains external; no proprietary game bytes are stored in this test.
"""
import ctypes
import hashlib
import math
from pathlib import Path
import random
import struct
import subprocess
import sys
import tempfile

BASE = 0x06010000
EXPECTED = '54509b22e246497f96fdee469ba3cd5c46c3440611fa9af43e689373fc3fbbd2'

def signed(x):
    return ctypes.c_int32(x).value

class SH:
    """Small fail-closed interpreter for the actual SH-2 branch instructions."""
    def __init__(self, rom):
        self.rom, self.r, self.t, self.memory = rom, [0] * 16, 0, {}

    def read(self, address, size=4):
        if BASE <= address < BASE + len(self.rom):
            return int.from_bytes(self.rom[address-BASE:address-BASE+size], 'big')
        return self.memory[address]

    def run(self, pc, ends):
        delayed = None
        for _ in range(200):
            if pc in ends: return ends[pc]
            op = self.read(pc, 2)
            n, m = (op >> 8) & 15, (op >> 4) & 15
            previous, delayed = delayed, None
            nextpc = pc + 2
            if op == 0x0009: pass
            elif op & 0xf0ff == 0x4015: self.t = signed(self.r[n]) > 0
            elif op >> 12 == 9: self.r[n] = ctypes.c_int16(self.read(pc + 4 + (op & 255) * 2, 2)).value
            elif op >> 12 == 0xe: self.r[n] = ctypes.c_int8(op & 255).value
            elif op >> 12 == 7: self.r[n] += ctypes.c_int8(op & 255).value
            elif op & 0xf00f == 0x2009: self.r[n] &= self.r[m]
            elif op >> 12 == 0xa:
                offset = (op & 4095) - (4096 if op & 2048 else 0)
                delayed = pc + 4 + offset * 2
            elif op >> 12 == 0xd: self.r[n] = self.read(((pc + 4) & ~3) + (op & 255) * 4)
            elif op >> 12 == 5: self.r[n] = self.read(self.r[m] + (op & 15) * 4)
            elif op >> 12 == 1: self.memory[self.r[n] + (op & 15) * 4] = self.r[m]
            elif op & 0xf00f == 0x6003: self.r[n] = self.r[m]
            elif op & 0xf00f == 0x300c: self.r[n] += self.r[m]
            elif op & 0xf00f == 0x3008: self.r[n] -= self.r[m]
            elif op & 0xf00f == 0x6009: self.r[n] = (self.r[m] << 16) | (self.r[m] >> 16)
            elif op & 0xf00f == 0x600f: self.r[n] = ctypes.c_int16(self.r[m]).value
            elif op & 0xf00f == 0x3007: self.t = signed(self.r[n]) > signed(self.r[m])
            elif op & 0xf00f == 0x3002: self.t = self.r[n] >= self.r[m]
            elif op & 0xf00f == 0x3006: self.t = self.r[n] > self.r[m]
            elif op & 0xf00f == 0x3003: self.t = signed(self.r[n]) >= signed(self.r[m])
            elif op & 0xf0ff == 0x4004:
                self.t = self.r[n] >> 31
                self.r[n] = (self.r[n] << 1) | self.t
            elif op & 0xf0ff == 0x0029: self.r[n] = self.t
            elif op & 0xf0ff == 0x4021: self.r[n] = signed(self.r[n]) >> 1
            elif op & 0xf0ff == 0x4011: self.t = signed(self.r[n]) >= 0
            elif op >> 8 in (0x89, 0x8b, 0x8d, 0x8f):
                take = bool(self.t) == (op >> 8 in (0x89, 0x8d))
                destination = pc + 4 + ctypes.c_int8(op & 255).value * 2 if take else pc + 4
                if op >> 8 in (0x8d, 0x8f): delayed = destination
                elif take: nextpc = destination
            else: raise AssertionError(f'Unhandled SH instruction {op:04x} at {pc:08x}')
            self.r = [x & 0xffffffff for x in self.r]
            pc = previous if previous is not None else nextpc
        raise AssertionError('SH step limit')

def sphere(rom, z, radius, bucket):
    sh = SH(rom)
    sh.r[2], sh.r[8], sh.r[9] = z & 0xffffffff, radius & 0xffffffff, radius & 0xffffffff
    sh.r[10], sh.r[15] = 0x1000, 0x2000
    sh.memory[0x1030] = bucket * 65536
    return sh.run(0x0604c3ce, {0x0604c49c: False, 0x0604c3fa: True})

def ship(rom, z):
    sh = SH(rom)
    sh.r[6] = z & 0xffffffff
    return sh.run(0x06028562, {0x06028572: True, 0x0602856e: False})

def track(rom, zs, bucket):
    depths = []
    for z in zs:
        sh = SH(rom)
        sh.r[4], sh.r[7] = 0x1000, 0x2000
        sh.memory[0x1008], sh.memory[0x2008] = z & 0xffffffff, 0
        sh.run(0x0605f488, {0x0605f494: True})
        depths.append(sh.r[0])
    sh = SH(rom)
    sh.r[4] = 0x3000
    sh.memory[0x3014] = bucket
    for i, depth in enumerate(depths):
        sh.r[8+i] = 0x4000+i*16
        sh.memory[0x4008+i*16] = depth
    return sh.run(0x06014364, {0x06014384: True, 0x060143f6: False})

def model(rom, zs, mode, bucket):
    sh = SH(rom)
    sh.r[6] = bucket
    for register,z in zip((1,2,3,8),zs):
        sh.r[register] = 0x4000+register*16
        sh.memory[sh.r[register]+8] = (z//131072)&0xffffffff
        sh.memory[sh.r[register]+4] = 0
    return sh.run((0x0605d6ec,0x0605d70c,0x0605d754,0x0605d730)[mode],
                  {0x0605d650: True, 0x0605d6d0: False})

def direction(rom, heading, dx, dz):
    x, z = abs(dx), abs(dz)
    while max(x,z) > 0x100000: x, z = x//2, z//2
    if not z: angle = 1024
    else:
        small, large = min(x,z), max(x,z)
        ratio = (small*1024 + large//2)//large
        angle = struct.unpack_from('>H',rom,0x6afa0+ratio*2)[0]
        if x > z: angle = 1024-angle
    if dx < 0: angle = angle-2048 if dz < 0 else -angle
    elif dz < 0: angle = 2048-angle
    sh = SH(rom)
    sh.r[1] = (heading-angle)&0xffffffff
    sh.run(0x060225f2, {0x06022620: True})
    return sh.r[2]

def main():
    rom = Path(sys.argv[1]).read_bytes()
    assert hashlib.sha256(rom).hexdigest() == EXPECTED, 'Wrong original executable'
    root = Path(__file__).resolve().parents[2]
    with tempfile.TemporaryDirectory() as tmp:
        lib = Path(tmp) / 'visibility.so'
        subprocess.run(['cc', '-std=c11', '-shared', '-fPIC', str(root/'platform/render/saturn_visibility.c'), '-lm', '-o', str(lib)], check=True)
        host = ctypes.CDLL(str(lib))
        rng = random.Random(2097)
        table = struct.unpack_from('>1025H', rom, 0x6afa0)
        assert all(value == round(math.atan(i/1024)*4096/(2*math.pi)) for i,value in enumerate(table))
        direction_count = 0
        vectors = [(0,0),(0,1),(1,0),(-1,0),(1,1),(-1,-1),(17,31),(31,17),(131071,-65537)]
        for dx,dz in vectors:
            for heading in range(4096):
                assert host.SaturnNativeRoadDirection(heading,dx,dz) == direction(rom,heading,dx,dz)
                direction_count += 1
        count = 0
        for course in range(8):
            nominal = struct.unpack_from('>H', rom, 0x14188 + course * 40 + 8)[0]
            bucket = (((nominal + 7) & ~7) + 15) // 16 * 8
            assert host.SaturnNativeFarBucket(course) == bucket
            for radius in (0, 1, 8192, 100*65536, 0x1234567):
                points = [-radius, 0, radius, 20*65536, 4000*65536, bucket*131072+radius]
                zs = [p+d for p in points for d in (-2,-1,0,1,2)]
                zs += [rng.randrange(-100000000, 600000000) for _ in range(500)]
                for z in zs:
                    visible = sphere(rom, z, radius, bucket)
                    assert host.SaturnSphereDepthVisible(z, radius, course) == visible, (course,z,radius)
                    assert host.SaturnShipDepthVisible(z, 0, radius, course) == (ship(rom,z) and visible), (course,z,radius)
                    assert host.SaturnShipDepthVisible(z, 1, radius, course) == 1
                    count += 1
            for far in (bucket*131072-1, bucket*131072, bucket*131072+1, -1, 0):
                for i in range(4):
                    zs = [-10000]*4
                    zs[i] = far
                    array = (ctypes.c_int32*4)(*zs)
                    assert host.SaturnTrackDepthVisible(array, course) == track(rom,zs,bucket), (course,zs)
        model_count = 0
        for course in range(8):
            bucket = host.SaturnNativeFarBucket(course)
            for mode in range(4):
                for _ in range(1000):
                    zs = [rng.randrange(-100000000,600000000) for _ in range(4)]
                    array = (ctypes.c_int32*4)(*zs)
                    assert host.SaturnModelDepthVisible(array,mode,course) == model(rom,zs,mode,bucket), (course,mode,zs)
                    model_count += 1
        print(f'PASS: {model_count} original model-polygon cases')
        print(f'PASS: {direction_count} direction cases and {count} original SH-2 sphere/ship cases plus 160 road boundary cases across all eight course descriptors')

if __name__ == '__main__': main()
