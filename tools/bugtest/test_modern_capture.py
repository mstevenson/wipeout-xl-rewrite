"""The optional semantic sidecar must preserve the inspector's colour interpolation."""
import importlib.util
import json
from pathlib import Path
import struct
import tempfile

source = Path(__file__).resolve().parents[1] / 'framecap.py'
spec = importlib.util.spec_from_file_location('framecap', source)
fc = importlib.util.module_from_spec(spec)
spec.loader.exec_module(fc)
with tempfile.TemporaryDirectory() as folder:
    path = Path(folder)
    # One empty v3 frame is readable with or without an optional sidecar.
    (path / 'frames.bin').write_bytes(b'XLFC' + struct.pack('<ii', 3, 1) +
        fc.FRAME.pack(12, 4, 0, 0, 0, 0, 320, 240, 0, 0) + struct.pack('<if', 0, 0))
    assert 'render' not in fc.load(path)[0]
    (path / 'modern.json').write_text(json.dumps({'version': 1, 'frames': [
        {'mode': 1, 'brightness': 85, 'primitives': []}]}))
    assert fc.load(path)[0]['render']['brightness'] == 85

fc.vram = lambda _: None
p = fc.Prim()
p.draw = {'textured': 0, 'depth': 0}
triangle = [dict(w=w, r=r, g=0, b=0, a=255, u=0, v=0)
            for w, r in [(100, 0), (200, 240), (100, 0)]]
weights = (.25, .5, .25)
assert abs(fc.shade({}, p, triangle, weights)['rgb'][0] - 80) < .01
modern = {'render': {'mode': 1}}
assert abs(fc.shade(modern, p, triangle, weights)['rgb'][0] - 120) < .01
p.draw['smoothTrack'] = 1
assert abs(fc.shade(modern, p, triangle, weights)['rgb'][0] - 80) < .01
print('Capture sidecar compatibility and affine/perspective colour policy: OK')
