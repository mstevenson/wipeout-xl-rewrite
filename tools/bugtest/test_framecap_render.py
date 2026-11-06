"""Read the C recorder's real output; retain old-recording compatibility."""
import importlib.util
import math
from pathlib import Path
import struct
import sys
import tempfile

spec = importlib.util.spec_from_file_location('framecap', Path(__file__).parents[1] / 'framecap.py')
fc = importlib.util.module_from_spec(spec)
spec.loader.exec_module(fc)
folder = Path(sys.argv[1])
frames = fc.load(folder)
assert len(frames) == 4
for mode, frame in enumerate(frames):
    p = frame['prims'][0]
    result = fc.shade(frame, p, p.v[:3], [1/3]*3)
    assert math.isclose(result['rgb'][0], 120)
    assert math.isclose(result['u'], 100/3 if mode == 0 else 200/7)
    assert math.isclose(result['alpha'], 480/7)
    assert len(fc.hits(frame, 0, 0)) == (1 if mode < 2 else 0)

# Version 4 operations take precedence over sidecar/current policy.
(folder / 'renderer.ini').write_text('# Active renderer: PC\nrenderer = pc\n')
assert fc.load(folder)[0]['prims'][0].shade == frames[0]['prims'][0].shade

# Downgrade a fixture to the historical v3 layout (no shading word).
raw = (folder / 'frames.bin').read_bytes()
frame_size = fc.FRAME.size + 8
prim_size = fc.PRIM_HEAD.size + 4 * fc.VERTEX.size + fc.DRAW.size + fc.PRIM_TAIL.size
with tempfile.TemporaryDirectory() as old:
    old = Path(old)
    data = bytearray(b'XLFC' + struct.pack('<ii', 3, 4))
    pos = 12
    for _ in range(4):
        data.extend(raw[pos:pos + frame_size + prim_size])
        pos += frame_size + prim_size + 4
    (old / 'frames.bin').write_bytes(data)
    legacy = fc.load(old)
    p = legacy[0]['prims'][0]
    result = fc.shade(legacy[0], p, p.v[:3], [1/3]*3)
    assert math.isclose(result['rgb'][0], 480/7)
    assert math.isclose(result['u'], 200/7)
    assert not fc.hits(legacy[0], 0, 0)
    # The interim renderer-split v3 captures already have a policy sidecar.
    (old / 'renderer.ini').write_text('# Active renderer: PlayStation\nrenderer = auto\nps1_affine_textures = 1\n')
    legacy = fc.load(old)
    p = legacy[0]['prims'][0]
    result = fc.shade(legacy[0], p, p.v[:3], [1/3]*3)
    assert math.isclose(result['rgb'][0], 120)
    assert math.isclose(result['u'], 100/3)
    assert len(fc.hits(legacy[0], 0, 0)) == 1
print('Python frame reader: production v4 captures, immutable policy and legacy v3 compatibility: OK')
