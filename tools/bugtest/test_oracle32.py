"""Oracle infrastructure and a real PS1-path mutation; requires Homebrew LLVM."""
import contextlib
import importlib.util
import io
import shutil
import subprocess
import tempfile
import unittest
from pathlib import Path
from unittest.mock import patch

ROOT = Path(__file__).resolve().parents[2]
spec = importlib.util.spec_from_file_location('oracle32', ROOT / 'tools/oracle32.py')
oracle = importlib.util.module_from_spec(spec)
spec.loader.exec_module(oracle)


@unittest.skipUnless((oracle.LLVM / 'clang').exists(), 'Homebrew LLVM is unavailable')
class OracleTest(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory(prefix='oracle32-test-')
        self.addCleanup(self.temp.cleanup)
        self.root = Path(self.temp.name)
        (self.root / 'game/src').mkdir(parents=True)
        (self.root / 'platform').mkdir()
        self.source = self.root / 'game/src/probe.c'
        self.source.write_text('#include <math.h>\n#include <sys/types.h>\n'
                               '#include "../../platform/probe.h"\n'
                               'int Probe(void) { return sizeof(Record); }\n'
                               'double Radius(double x, double z) { return hypot(x, z); }\n')
        self.header = self.root / 'platform/probe.h'
        self.header.write_text('typedef struct { int value; } Record;\n')
        self.git('init', '-q')
        self.git('add', 'game', 'platform')
        self.git('-c', 'user.name=Oracle test', '-c', 'user.email=oracle@example.invalid',
                 'commit', '-qm', 'baseline')
        self.addCleanup(patch.stopall)
        patch.object(oracle, 'ROOT', self.root).start()
        patch.object(oracle, 'OUT', self.root / 'snapshots').start()
        self.output = io.StringIO()
        self.redirect = contextlib.redirect_stdout(self.output)
        self.redirect.__enter__()
        self.addCleanup(self.redirect.__exit__, None, None, None)

    def git(self, *args):
        return subprocess.run(['git', '-C', str(self.root), *args], check=True,
                              capture_output=True, text=True)

    def baseline(self):
        oracle.snapshot('HEAD', 'base', {'probe.c'})

    def test_snapshot_uses_committed_platform_headers(self):
        self.header.write_text('typedef struct { int value[2]; } Record;\n')
        self.baseline()
        self.assertEqual(oracle.diff('base', False, {'probe.c'}), 1)
        self.assertIn('Probe  CHANGED', self.output.getvalue())

    def test_identical_and_missing_baseline(self):
        self.baseline()
        self.assertEqual(oracle.diff('base', False, {'probe.c'}), 0)
        (oracle.OUT / 'base/probe.dis').unlink()
        with self.assertRaisesRegex(RuntimeError, 'missing baseline'):
            oracle.diff('base', False, {'probe.c'})

    def test_compile_failure_cannot_pass_or_create_snapshot(self):
        self.baseline()
        self.source.write_text('#error deliberate failure\n')
        with self.assertRaisesRegex(RuntimeError, 'compilation failed'):
            oracle.diff('base', False, {'probe.c'})
        self.git('add', 'game')
        self.git('-c', 'user.name=Oracle test', '-c', 'user.email=oracle@example.invalid',
                 'commit', '-qm', 'invalid source')
        with self.assertRaisesRegex(RuntimeError, 'compilation failed'):
            oracle.snapshot('HEAD', 'failed', {'probe.c'})
        self.assertFalse((oracle.OUT / 'failed').exists())

    def test_invalid_ref_file_and_configuration(self):
        with self.assertRaises(subprocess.CalledProcessError):
            oracle.snapshot('no-such-ref', 'invalid', {'probe.c'})
        with self.assertRaisesRegex(RuntimeError, 'source files not found'):
            oracle.snapshot('HEAD', 'invalid-file', {'missing.c'})
        self.baseline()
        with self.assertRaisesRegex(RuntimeError, 'options differ'):
            oracle.diff('base', False, {'probe.c'}, renderer='ps1')
        with self.assertRaisesRegex(RuntimeError, 'already exists'):
            self.baseline()

    def test_real_ps1_path_mutation_is_detected(self):
        # Change only a temporary copy: the PS1 near cutoff must survive specialization.
        shutil.copytree(ROOT / 'game/include', self.root / 'game/include')
        for relative in ('platform/psx/gte.h', 'platform/n64_material.h',
                         'platform/render/geometry.h', 'platform/render/renderer.h',
                         'platform/render/ps1_renderer.c'):
            dest = self.root / relative
            dest.parent.mkdir(parents=True, exist_ok=True)
            shutil.copyfile(ROOT / relative, dest)
        original = (ROOT / 'game/src/objdraw.c').read_text()
        self.assertIn('depth < 0x800', original)
        (self.root / 'game/src/objdraw.c').write_text(original.replace('depth < 0x800', 'depth < 0x801', 1))
        before = oracle.compile_tree(ROOT, self.root / 'before', {'objdraw.c'}, renderer='ps1')['objdraw']
        after = oracle.compile_tree(self.root, self.root / 'after', {'objdraw.c'}, renderer='ps1')['objdraw']
        self.assertTrue(before)
        self.assertIn('DrawObject', {name for name in before if before[name] != after.get(name)})
        self.assertFalse(any('hypot' in line for lines in before.values() for line in lines))


if __name__ == '__main__':
    unittest.main()
