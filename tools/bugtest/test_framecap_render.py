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

# Version 5 operations take precedence over sidecar/current policy.
(folder / 'renderer.ini').write_text('# Active renderer: PC\nrenderer = pc\n')
assert fc.load(folder)[0]['prims'][0].shade == frames[0]['prims'][0].shade

# Downgrade a fixture to the historical v3 layout (no shading word).
raw = (folder / 'frames.bin').read_bytes()
frame_size = fc.FRAME.size + 8
prim_size = fc.PRIM_HEAD.size + 4 * fc.VERTEX.size + fc.DRAW.size + fc.PRIM_TAIL.size
with tempfile.TemporaryDirectory() as old:
    old = Path(old)
    data = bytearray(b'XLFC' + struct.pack('<ii', 4, 4))
    pos = 12
    for _ in range(4):
        data.extend(raw[pos:pos + frame_size])
        data.extend(raw[pos + frame_size + 8:pos + frame_size + 8 + prim_size + 4])
        pos += frame_size + 8 + prim_size + 4
    (old / 'frames.bin').write_bytes(data)
    legacy = fc.load(old)
    for mode, frame in enumerate(legacy):
        assert frame['prims'][0].shade == frames[mode]['prims'][0].shade
        assert frame['picture_extent'] == (320, 240)
with tempfile.TemporaryDirectory() as old:
    old = Path(old)
    data = bytearray(b'XLFC' + struct.pack('<ii', 3, 4))
    pos = 12
    for _ in range(4):
        data.extend(raw[pos:pos + frame_size])
        data.extend(raw[pos + frame_size + 8:pos + frame_size + 8 + prim_size])
        pos += frame_size + 8 + prim_size + 4
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
# A native Saturn image and a PC image map back to the same game-coordinate samples.
for width, height, game_height in [(352, 240, 240), (640, 480, 256), (320, 240, 256)]:
    frame = dict(frames[0], picture_extent=(320, game_height))
    pixels = bytes(v for y in range(height) for x in range(width) for v in (x % 256, y % 256, 0))
    fc._pictures[str(folder / 'picture_000')] = (width, height, pixels)
    for x, y in [(0, 0), (159, 119), (319, game_height - 1)]:
        expected = (int((x + .5) * width / 320) % 256, int((y + .5) * height / game_height) % 256, 0)
        assert fc.pixel_rgb(frame, x, y) == expected
print('Python frame reader: production v5 captures, immutable policy and legacy v3/v4 compatibility: OK')
