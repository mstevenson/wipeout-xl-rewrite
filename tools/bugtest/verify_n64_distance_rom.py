#!/usr/bin/env python3
"""Execute Wipeout 64 USA visibility branches against boundary inputs.

Requires Unicorn. Reads the original ROM without extracting or modifying it.
Compares the compiled host visibility policy with isolated original MIPS
routines using synthetic RAM. This is not whole-frame console equivalence.
"""
from pathlib import Path
import struct
from unicorn import Uc,UC_ARCH_MIPS,UC_MODE_MIPS32,UC_MODE_BIG_ENDIAN
from unicorn.mips_const import *
import argparse
import hashlib
parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument("rom", type=Path)
args = parser.parse_args()
rom = args.rom.read_bytes()
assert hashlib.sha256(rom).hexdigest() == "fdcf4ae70d90abef4f299b04664e009a967e55f97a065b43f596b749dd7d6b15", "Expected USA big-endian ROM"

def cpu():
 c=Uc(UC_ARCH_MIPS,UC_MODE_MIPS32|UC_MODE_BIG_ENDIAN);c.mem_map(0,0x800000);c.mem_write(0x400,rom[0x1000:]);c.reg_write(UC_MIPS_REG_SP,0x807f0000);return c
for players,expected in enumerate(((800,1200),(600,1000),(400,450),(400,450)),1):
 c=cpu();c.mem_write(0xc5b30,bytes([players]));c.emu_start(0x800110a0,0x80011170,count=200)
 sp=c.reg_read(UC_MIPS_REG_SP)&0x1fffffff
 assert struct.unpack('>2I',bytes(c.mem_read(sp+0x1d4,8)))==expected
 print('Original threshold selection',players,expected)
from unicorn import UC_HOOK_CODE

def gate(magnitude,z,remembered,limit=1200,destroyed=False,camera=False):
 c=cpu();sp=0x7f0000;ship=0x600000
 def word(a,v):c.mem_write(a,struct.pack('>I',v&0xffffffff))
 c.reg_write(UC_MIPS_REG_S1,0x80600000);c.reg_write(UC_MIPS_REG_S2,magnitude);c.reg_write(UC_MIPS_REG_S3,1)
 word(sp+0x1d8,limit);word(sp+0x1d0,0);word(sp+0x210,1 if camera else 0);word(sp+0x28,z)
 c.mem_write(ship+0xfe,bytes([remembered]));c.mem_write(ship+0xb0,struct.pack('>H',0x8000 if destroyed else 0))
 hit=[]
 def stop(c,address,size,data):
  if address in (0x800113e8,0x80011c8c):hit.append(address);c.emu_stop()
 c.hook_add(UC_HOOK_CODE,stop);c.emu_start(0x8001136c,0,count=200)
 assert hit
 return hit[0]==0x800113e8,bytes(c.mem_read(ship+0xfe,1))[0]
checks = 0
for limit in (1200, 1000, 450):
    for remembered in (0, 1):
        for mag in (limit - 1, limit, limit + 1, 2899, 2900, 2901):
            for z in (-1, 0, 1):
                for destroyed in (False, True):
                    for camera in (False, True):
                        retained = remembered or mag < limit
                        visible = not destroyed and retained and (z > 0 or camera)
                        if not visible and mag >= 2900:
                            retained = False
                        actual = gate(mag, z, remembered, limit, destroyed, camera)
                        assert actual == (bool(visible), int(retained)), (mag, z, remembered, limit, destroyed, camera, actual)
                        checks += 1
print(f"Original ship visibility branches: {checks} boundary cases passed")
near = struct.unpack_from(">8i", rom, 0x91c44 + 0xc00)
far = struct.unpack_from(">8i", rom, 0x91c64 + 0xc00)
assert near == (2048,) * 7 + (3072,)
assert far == (38200,) * 7 + (37000,)
print("Original scenery tables verified; coordinate conversion is a separate check")

# Compile the same pure C policy used by the renderer, not a Python facsimile.
import ctypes as C
import subprocess
import tempfile
import random
import math
root = Path(__file__).resolve().parents[2]
_temp = tempfile.TemporaryDirectory(prefix='n64-visibility-')
libpath = Path(_temp.name) / 'policy.so'
subprocess.run(['cc', '-std=c11', '-shared', '-fPIC', '-O2',
                str(root / 'platform/render/n64_visibility.c'), '-lm', '-o', str(libpath)], check=True)
lib = C.CDLL(str(libpath))
class Ship(C.Structure):
    _fields_ = [('remembered', C.c_uint8)]
class Section(C.Structure):
    _fields_ = [('previous', C.c_int), ('next', C.c_int), ('junction', C.c_int), ('junctionFace', C.c_ubyte)]
Emit = C.CFUNCTYPE(None, C.c_int, C.c_void_p)
lib.N64NativeSquareRoot.argtypes = [C.c_uint32]
lib.N64NativeSquareRoot.restype = C.c_uint32
lib.N64NativeMagnitude.argtypes = [C.c_int32] * 3
lib.N64NativeMagnitude.restype = C.c_uint32
lib.N64NativeShipGate.argtypes = [C.POINTER(Ship), C.c_uint32] + [C.c_int] * 4
lib.N64NativeSceneryVisible.argtypes = [C.c_int32, C.c_int32, C.c_int]
lib.N64NativeRoadWalk.argtypes = [C.POINTER(Section)] + [C.c_int] * 6 + [Emit, C.c_void_p]
lib.N64NativeRoadSelect.argtypes = [C.POINTER(Section)] + [C.c_int] * 9 + [Emit, C.c_void_p]

def word(c, address, value):
    c.mem_write(address & 0x1fffffff, struct.pack('>I', value & 0xffffffff))
def readword(c, address):
    return struct.unpack('>I', bytes(c.mem_read(address & 0x1fffffff, 4)))[0]

c = cpu()
rng = random.Random(64)
values = [0, 1, 2, 3, 4, 0xffffffff, 0xfffffffe] + [rng.randrange(2**32) for _ in range(5000)]
values += [i*i+d for i in (449,450,451,999,1000,1001,1199,1200,1201,2899,2900,2901) for d in (-1,0,1)]
for value in values:
    c.reg_write(UC_MIPS_REG_A0, value)
    c.reg_write(UC_MIPS_REG_RA, 0x807e0000)
    c.emu_start(0x80019f90, 0x807e0000, count=200)
    expected = c.reg_read(UC_MIPS_REG_V0)
    assert lib.N64NativeSquareRoot(value) == expected, (value, expected)
print(f'Host square root vs original instructions: {len(values)} cases passed')
for _ in range(2000):
    delta = [rng.randrange(-500000,500001) for _ in range(3)]
    for reg, value in zip((UC_MIPS_REG_A0,UC_MIPS_REG_A1,UC_MIPS_REG_A2), delta):
        c.reg_write(reg, (value >> 3) & 0xffffffff)
    c.reg_write(UC_MIPS_REG_RA, 0x807e0000)
    c.emu_start(0x8001cec4, 0x807e0000, count=300)
    assert lib.N64NativeMagnitude(*delta) == c.reg_read(UC_MIPS_REG_V0), delta
print('Host magnitude vs original instructions: 2000 signed-coordinate cases passed')
for players in range(1,5):
    limit = (1200,1000,450,450)[players-1]
    for remembered in (0,1):
        for magnitude in (0,limit-1,limit,limit+1,2899,2900,2901,65535):
            for z in (-1,0,1):
                for destroyed in (0,1):
                    for camera in (0,1):
                        state = Ship(remembered)
                        actual = lib.N64NativeShipGate(C.byref(state),magnitude,z,camera,destroyed,players)
                        expected = gate(magnitude,z,remembered,limit,destroyed,camera)
                        assert (bool(actual),state.remembered) == expected
print('Host stateful ship gate vs original instructions: 768 boundary cases passed')

# Execute the original scenery depth gates after its transform (the renderer's
# profile policy consumes transformed depth). Radii retain signed shift rounding.
c = cpu()
for level in range(8):
    for extent in (0,1,2,3,500,501,10000):
        for z in (-5,0,2*near[level]-extent-1,2*near[level]-extent,
                  2*near[level]-extent+1,2*near[level]-extent+2,
                  2*far[level]+extent-1,2*far[level]+extent,2*far[level]+extent+1):
            sp = 0x7f0000
            word(c,sp+0x18,z>>1);word(c,sp+0x2c,near[level]);word(c,sp+0x30,far[level])
            c.reg_write(UC_MIPS_REG_S1,extent>>1)
            hit=[]
            def depthstop(c,address,size,data):
                if address in (0x800291bc,0x80029328,0x8002932c):
                    hit.append(address);c.emu_stop()
            hook=c.hook_add(UC_HOOK_CODE,depthstop)
            c.emu_start(0x80029190,0,count=30);c.hook_del(hook)
            assert hit
            assert bool(lib.N64NativeSceneryVisible(z,extent,level)) == (hit[0]==0x800291bc), (level,z,extent)
print('Host scenery depth vs original instructions: 504 boundary cases passed')

# Original road walker on synthetic linked sections, including every branch
# orientation and both junction face flags. Execute list construction verbatim;
# skip matrix setup, then stop before any triangle commands are generated.
wc=cpu()
base=0x80600000
track=0x80610000
faces=0x80620000
word(wc,0x8008a98c,track);word(wc,track+0x1c,faces)
def install_graph(c, sections):
    word(c,0x8008a98c,track);word(c,track+0x1c,faces)
    for i,s in enumerate(sections):
        addr=base+i*156
        for offset,index in ((0,s.junction),(4,s.previous),(8,s.next)):
            word(c,addr+offset,0 if index<0 else base+index*156)
        c.mem_write((addr+0x8e)&0x1fffffff,struct.pack('>h',i*2))
        c.mem_write((faces+i*40+15)&0x1fffffff,bytes([0x40 if s.junctionFace else 0]))
        c.mem_write((faces+i*40+35)&0x1fffffff,b'\0')

def original_walk(sections,start,players,count,direction,flag):
    c=wc
    install_graph(c,sections)
    c.reg_write(UC_MIPS_REG_SP,0x807f0000)
    for reg,value in ((UC_MIPS_REG_A0,base+start*156),(UC_MIPS_REG_A2,count),(UC_MIPS_REG_A3,direction)):
        c.reg_write(reg,value)
    word(c,0x807f0018,flag);c.mem_write(0xc5b30,bytes([players]))
    hit=[]
    def walkerstop(c,address,size,data):
        if address == 0x80016f30:
            c.reg_write(UC_MIPS_REG_S4,0x80090000)
            c.reg_write(UC_MIPS_REG_S0,0x800a0000)
            c.reg_write(UC_MIPS_REG_S1,0x800a0000)
            word(c,0x800a0f34,0);word(c,0x800a0ee0,0)
            c.reg_write(UC_MIPS_REG_PC,0x80016fb4)
        elif address == 0x80017184:
            sp=c.reg_read(UC_MIPS_REG_SP)
            n=c.reg_read(UC_MIPS_REG_A2)
            result=[(readword(c,sp+0x38+i*4)-base)//156 for i in range(n)]
            hit.append((result[::-1],readword(c,sp+0x118)));c.emu_stop()
    hook=c.hook_add(UC_HOOK_CODE,walkerstop)
    c.emu_start(0x80016e34,0,count=3000);c.hook_del(hook)
    assert hit
    return hit[0]

def host_walk(sections,start,players,count,direction,flag):
    result=[]
    emit=Emit(lambda section,context:result.append(section))
    ret=lib.N64NativeRoadWalk(sections,len(sections),start,players,count,direction,flag,emit,None)
    return result,ret

def circle(n=64):
    return (Section*n)(*[Section((i-1)%n,(i+1)%n,-1,0) for i in range(n)])
walker_cases=0
for junction in (-1,29,30,31,35):
    for faceflag in (0,1):
        for orientation in range(3):
            sections=circle()
            if junction>=0:
                sections[junction].junction=50
                sections[30].junctionFace=faceflag
                if orientation==1:sections[50].next=junction
                elif orientation==2:sections[50].previous=junction
                sections[49].junctionFace=orientation==1
                sections[51].junctionFace=orientation==2
            for players in (1,2,3,4):
                for count in (2,4,10,20,22):
                    for direction in (1,2):
                        for flag in (0,1):
                            expected=original_walk(sections,30,players,count,direction,flag)
                            actual=host_walk(sections,30,players,count,direction,flag)
                            assert actual==expected,(junction,faceflag,orientation,players,count,direction,flag,actual,expected)
                            walker_cases+=1
print(f'Host junction road walker vs original instructions: {walker_cases} cases passed')

# The angle lookup is generated mathematically; verify every table entry and
# the complete original angle helper, including signed-16-bit deltas.
lib.N64NativeRoadAngle.argtypes=[C.c_int,C.c_int32,C.c_int32]
atan_table=struct.unpack_from('>4097H',rom,0x8d6b4+0xc00)
assert all(v==int(math.atan(i/4096)*32768/math.pi+0.5) for i,v in enumerate(atan_table))
ac=cpu()
for i in range(3000):
    dx,dz=(rng.randrange(-100000,100001) for _ in range(2))
    if i<9:dx,dz=(0,1,-1)[i//3],(0,1,-1)[i%3]
    heading=rng.randrange(-2048,2048)
    ac.reg_write(UC_MIPS_REG_A0,C.c_int16(dx).value&0xffffffff)
    ac.reg_write(UC_MIPS_REG_A1,C.c_int16(dz).value&0xffffffff)
    ac.reg_write(UC_MIPS_REG_RA,0x807e0000)
    ac.emu_start(0x80014c30,0x807e0000,count=300)
    angle=C.c_int32(ac.reg_read(UC_MIPS_REG_V0)).value
    expected=((heading-angle+2048)&4095)-2048
    assert lib.N64NativeRoadAngle(heading,dx,dz)==expected,(heading,dx,dz,angle)
print('Host road angle vs original instructions: 3000 cases; all 4097 atan entries passed')

# Execute dispatch with camera/matrix work stubbed. Calls into the walker use
# the independently instruction-verified walker above, so this checks course
# ranges, player counts, sides, authored-list choice, and junction return use.
dc=cpu()
def original_select(sections,players,course,section,angle,reduced,authored,auxiliary):
    c=dc;start=30;camera=0x80640000;ship=0x80650000;sp=0x807f0000
    install_graph(c,sections)
    word(c,camera+4,base+start*156)
    c.mem_write((base+start*156+0x98)&0x1fffffff,struct.pack('>H',section))
    c.mem_write(0xc5b30,bytes([players]));word(c,0x800c5b2c,course)
    c.mem_write(0x9ba18,bytes([reduced]));word(c,0x8008fad8,authored)
    word(c,0x800aa088,0);c.mem_write((ship+0xb0)&0x1fffffff,b'\0\0')
    for reg,value in ((UC_MIPS_REG_SP,sp),(UC_MIPS_REG_A1,0),(UC_MIPS_REG_S1,0),
                      (UC_MIPS_REG_S2,camera),(UC_MIPS_REG_S3,ship),(UC_MIPS_REG_S4,auxiliary)):
        c.reg_write(reg,value)
    output=[];authored_result=[];stopped=[]
    def dispatchstop(c,address,size,data):
        if address==0x8004f660:
            c.reg_write(UC_MIPS_REG_PC,c.reg_read(UC_MIPS_REG_RA))
        elif address==0x80028a10:
            direction=1 if -512<=angle<512 else 4 if 512<=angle<1536 else 2 if angle < -1536 or angle>=1536 else 8
            word(c,c.reg_read(UC_MIPS_REG_A1),angle)
            c.reg_write(UC_MIPS_REG_V0,direction)
            c.reg_write(UC_MIPS_REG_PC,c.reg_read(UC_MIPS_REG_RA))
        elif address==0x80016e34:
            count=C.c_int32(c.reg_read(UC_MIPS_REG_A2)).value
            direction=c.reg_read(UC_MIPS_REG_A3)
            flag=readword(c,sp+0x18)
            indices,ret=host_walk(sections,start,players,count,direction,flag)
            output.extend(indices)
            c.reg_write(UC_MIPS_REG_V0,ret)
            c.reg_write(UC_MIPS_REG_PC,c.reg_read(UC_MIPS_REG_RA))
        elif address in (0x80017b94,0x80017f70):
            authored_result.append(int(address==0x80017b94));stopped.append(1);c.emu_stop()
    hook=c.hook_add(UC_HOOK_CODE,dispatchstop)
    c.emu_start(0x8001786c,0,count=2000);c.hook_del(hook)
    assert stopped,(players,course,section,angle)
    return output,authored_result[0]

def host_select(sections,players,course,section,angle,reduced,authored,auxiliary):
    output=[];emit=Emit(lambda index,context:output.append(index))
    ret=lib.N64NativeRoadSelect(sections,len(sections),30,players,course,section,angle,reduced,authored,auxiliary,emit,None)
    return output,ret

cases=[]
for course,bounds in ((1,(99,134,200,253)),(4,(229,250,251,277)),(5,(72,91,166,190,266,310,338,350,390,405)),(7,(0,))):
    for bound in bounds:
        for section in (max(0,bound-1),bound,bound+1):
            for players in (1,2,3,4):
                for angle in (0,512,1024,1535,1536,-513,-1536,-1537):
                    cases.append((players,course,section,angle,0,0,0))
for _ in range(1000):
    cases.append((rng.randrange(1,5),rng.randrange(1,8),rng.randrange(450),rng.randrange(-2048,2048),
                  rng.randrange(2),rng.randrange(2),rng.randrange(2)))
for i,case in enumerate(cases):
    sections=circle()
    if i%2:sections[30].junction=50
    expected=original_select(sections,*case)
    actual=host_select(sections,*case)
    assert actual==expected,(case,actual,expected)
print(f'Host road dispatch vs original instructions: {len(cases)} cases passed')

lib.N64NativeViewZ.argtypes=[C.POINTER(C.c_int16)]+[C.c_int32]*4
lib.N64NativeViewZ.restype=C.c_int32
vc=cpu()
for i in range(2000):
    row=[rng.randrange(-4096,4097) for _ in range(3)]
    delta=[rng.randrange(-100000,100001) for _ in range(3)]
    translation=rng.randrange(-50000,50001)
    vc.mem_write(0x60000c,struct.pack('>3h',*row));word(vc,0x8060001c,translation)
    vc.mem_write(0x610000,struct.pack('>3h',*[C.c_int16(v).value for v in delta]))
    for reg,value in ((UC_MIPS_REG_A0,0x80600000),(UC_MIPS_REG_A1,0x80610000),
                      (UC_MIPS_REG_A2,0x80620000),(UC_MIPS_REG_RA,0x807e0000)):
        vc.reg_write(reg,value)
    vc.emu_start(0x80014df4,0x807e0000,count=200)
    expected=C.c_int32(readword(vc,0x80620008)).value
    assert lib.N64NativeViewZ((C.c_int16*3)(*row),translation,*delta)==expected
print('Host ship view-space transform vs original instructions: 2000 cases passed')

class Matrix(C.Structure):
    _fields_=[('m',(C.c_int16*3)*3),('t',C.c_int32*3)]
lib.N64NativeCompose.argtypes=[C.POINTER(Matrix)]*3
mc=cpu()
def install_matrix(c,address,matrix):
    c.mem_write(address&0x1fffffff,struct.pack('>9h',*[v for row in matrix.m for v in row]))
    for axis in range(3):word(c,address+0x14+axis*4,matrix.t[axis])
for i in range(2000):
    matrices=[]
    for _ in range(2):
        matrix=Matrix()
        for row in range(3):
            for col in range(3):matrix.m[row][col]=rng.randrange(-4096,4097)
            matrix.t[row]=rng.randrange(-200000,200001)
        matrices.append(matrix)
    parent,relative=matrices
    install_matrix(mc,0x80600000,parent);install_matrix(mc,0x80610000,relative)
    word(mc,0x80600044,0);word(mc,0x80610044,0x80600000)
    mc.mem_write(0x610040,b'\0\1')
    mc.reg_write(UC_MIPS_REG_A0,0x80610000);mc.reg_write(UC_MIPS_REG_RA,0x807e0000)
    mc.emu_start(0x80019ce0,0x807e0000,count=1000)
    expected_m=struct.unpack('>9h',bytes(mc.mem_read(0x610020,18)))
    expected_t=struct.unpack('>3i',bytes(mc.mem_read(0x610034,12)))
    actual=Matrix();lib.N64NativeCompose(C.byref(parent),C.byref(relative),C.byref(actual))
    assert tuple(v for row in actual.m for v in row)==expected_m
    assert tuple(actual.t)==expected_t
print('Host scenery matrix composition vs original instructions: 2000 cases passed')

# The loader halves the three relative translations before attaching a camera.
for values in ((1,-1,3),(40001,-40001,76543)):
    lc=cpu();word(lc,0x80600030,0x80610000)
    for axis,value in enumerate(values):word(lc,0x80610014+axis*4,value)
    lc.reg_write(UC_MIPS_REG_V1,0x80610000);lc.reg_write(UC_MIPS_REG_S3,0x80600000)
    lc.emu_start(0x80009fdc,0x8000a010,count=30)
    assert struct.unpack('>3i',bytes(lc.mem_read(0x610014,12)))==tuple(v>>1 for v in values)
print('Original PRM translation conversion: odd positive/negative cases passed')

lib.N64NativeObjectInRadius.argtypes=[C.c_int32]*4+[C.c_int]*2
rc=cpu()
word(rc,0x80600030,0x80610000)
for i in range(3000):
    radius=rng.randrange(0,32768);wide=rng.randrange(2);extent=rng.randrange(0,20001)
    delta=[rng.randrange(-65536,65537) for _ in range(3)]
    if i%3==0:delta=[radius + (i%5)-2,0,0]
    for axis in range(3):
        word(rc,0x80610014+axis*4,0)
        word(rc,0x8062000c+axis*4,-delta[axis])
    word(rc,0x80600034,extent)
    rc.mem_write(0x620092,struct.pack('>h',radius))
    rc.mem_write(0x620096,struct.pack('>H',wide))
    for reg,value in ((UC_MIPS_REG_S2,0x80600000),(UC_MIPS_REG_S3,0x80620000),(UC_MIPS_REG_S4,0)):
        rc.reg_write(reg,value)
    stopped=[]
    def radiusstop(c,address,size,data):
        if address==0x8001a81c:stopped.append(1);c.emu_stop()
    hook=rc.hook_add(UC_HOOK_CODE,radiusstop)
    rc.emu_start(0x8001a6c4,0,count=1000);rc.hook_del(hook)
    assert stopped, (i, hex(rc.reg_read(UC_MIPS_REG_PC)),delta,radius,wide,extent)
    assert lib.N64NativeObjectInRadius(*delta,extent,radius,wide)==rc.reg_read(UC_MIPS_REG_S0), (delta,extent,radius,wide)
print('Host scenery candidate radius vs original instructions: 3000 cases passed')

lib.N64NativeSceneryBoundsVisible.argtypes=[C.POINTER(C.c_int32),C.c_int32,C.c_int]
fc=cpu()
for i in range(2000):
    level=i%8;extent=rng.randrange(20000)
    position=[rng.randrange(-50000,50001),rng.randrange(-50000,50001),rng.randrange(-20000,60001)]
    for axis in range(3):word(fc,0x807f0010+axis*4,position[axis])
    word(fc,0x807f002c,near[level]);word(fc,0x807f0030,far[level])
    fc.reg_write(UC_MIPS_REG_S1,extent>>1)
    hit=[]
    def boundsstop(c,address,size,data):
        if address in (0x800292a8,0x80029328,0x8002932c):hit.append(address);c.emu_stop()
    hook=fc.hook_add(UC_HOOK_CODE,boundsstop)
    fc.emu_start(0x80029190,0,count=200);fc.hook_del(hook)
    assert hit
    assert bool(lib.N64NativeSceneryBoundsVisible((C.c_int32*3)(*position),extent,level))==(hit[0]==0x800292a8)
print('Host complete scenery bounds vs original instructions: 2000 cases passed')
