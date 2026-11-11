#!/usr/bin/env python3
"""Interleaved native replay timings, including startup; no recorder or GPU readback.

Example: modern_benchmark.py --binary main=/tmp/main --binary current=build/mac/wipeoutxl
         --disc /path/game.chd --pads /path/pads.txt --output /tmp/renderer-timings
CPU/RSS are wait4 child statistics. RSS excludes some GPU/driver allocations.
"""
import argparse
import json
import os
from pathlib import Path
import re
import statistics
import subprocess
import sys
import time


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--binary', action='append', required=True, metavar='LABEL=PATH')
    parser.add_argument('--disc', type=Path, required=True)
    parser.add_argument('--pads', type=Path, required=True)
    parser.add_argument('--output', type=Path, required=True)
    parser.add_argument('--frames', type=int, default=10000)
    parser.add_argument('--repeats', type=int, default=5)
    parser.add_argument('--scales', type=int, nargs='+', default=[1, 3, 6])
    parser.add_argument('--backend', default='Metal')
    args = parser.parse_args()
    binaries = [item.split('=', 1) for item in args.binary]
    binaries = [(label, str(Path(path).resolve())) for label, path in binaries]
    output = args.output.resolve()
    output.mkdir(parents=True, exist_ok=True)
    rows = []
    for scale in args.scales:
        # Warm every executable equally before collecting interleaved samples.
        for repeat in range(-1, args.repeats):
            order = binaries if repeat < 0 else binaries[repeat % len(binaries):] + binaries[:repeat % len(binaries)]
            for label, binary in order:
                folder = output / f'{scale}-{repeat+1}-{label}'
                folder.mkdir()  # refuse to reuse saves from a previous benchmark
                (folder / 'save').mkdir()
                env = {k: v for k, v in os.environ.items() if not k.startswith('WIPEOUT_')}
                env.update(WIPEOUT_HIDDEN='1', WIPEOUT_MUTE='1', WIPEOUT_FIXED_STEP='1', WIPEOUT_NO_GAMEPAD='1',
                    WIPEOUT_RENDERER=args.backend, WIPEOUT_RENDERER_MODE='existing', WIPEOUT_RENDER_SCALE=str(scale),
                    WIPEOUT_FRAMES=str(args.frames), WIPEOUT_PADS=str(args.pads.resolve()),
                    WIPEOUT_SAVE_DIR=str(folder / 'save'), WIPEOUT_FAR_VIEW='2', WIPEOUT_AUTOPILOT='all')
                with (folder / 'run.log').open('w') as log:
                    start = time.perf_counter()
                    child = subprocess.Popen([binary, '--disc', str(args.disc.resolve()), '--window', '640x480'],
                                             env=env, stdout=log, stderr=log)
                    _, status, usage = os.wait4(child.pid, 0)
                    child.returncode = os.waitstatus_to_exitcode(status)
                elapsed = time.perf_counter() - start
                if child.returncode:
                    raise RuntimeError(f'{label} failed: {folder / "run.log"}')
                text = (folder / 'run.log').read_text()
                matched = re.search(r'(\d+) pictures shown in (\d+) fields', text)
                if not matched or int(matched[2]) != args.frames:
                    raise RuntimeError(f'Incomplete replay: {folder / "run.log"}')
                if repeat < 0:
                    continue
                row = dict(label=label, scale=scale, repeat=repeat, elapsed=elapsed,
                    cpu=usage.ru_utime + usage.ru_stime, user=usage.ru_utime, system=usage.ru_stime,
                    rssMiB=usage.ru_maxrss / (1048576 if sys.platform == 'darwin' else 1024), pictures=int(matched[1]))
                rows.append(row)
                print(json.dumps(row), flush=True)
                (output / 'samples.json').write_text(json.dumps(rows, indent=2) + '\n')
    summary = []
    for scale in args.scales:
        for label, _ in binaries:
            selected = [r for r in rows if r['scale'] == scale and r['label'] == label]
            summary.append(dict(label=label, scale=scale, samples=len(selected), **{
                name: statistics.median(r[name] for r in selected) for name in ('elapsed', 'cpu', 'rssMiB')}))
    (output / 'summary.json').write_text(json.dumps(summary, indent=2) + '\n')
    print(json.dumps(summary, indent=2))


if __name__ == '__main__':
    main()
