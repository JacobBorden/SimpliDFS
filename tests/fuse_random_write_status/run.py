#!/usr/bin/env python3
"""Compile the actual random-write function and main with bounded fault seams.

Uses disposable ordinary files, without FUSE, services, sockets or real data.
Only prerequisite helpers are substituted in run_random_write_test; all writer,
verification and Boolean-to-process-status code comes from the selected source.
"""
import argparse
import hashlib
import json
import os
from pathlib import Path
import subprocess
import tempfile

root = Path(__file__).resolve().parents[2]
parser = argparse.ArgumentParser()
parser.add_argument('--source', type=Path, default=root / 'tests/fuse_concurrency_tests.cpp')
parser.add_argument('--output', type=Path, required=True)
args = parser.parse_args()
args.output.mkdir(parents=True, exist_ok=True)
source = args.source.read_text()
seam = r'''
static std::string fault() { return std::getenv("STATUS_FAULT"); }
static bool fixture_mount() { return fault() != "mount"; }
static int fixture_sodium() { return fault() == "sodium" ? -1 : sodium_init(); }
static bool fixture_preallocate(const std::string &path, off_t size) {
  return fault() != "preallocate" && preallocateFile(path, size);
}
static bool fixture_open(const std::string &path, std::ifstream &stream,
                         std::ios::openmode mode) {
  if (fault() == "verification-open") return false;
  return openFileWithRetry(path, stream, mode);
}
#define check_mount_point_ready fixture_mount
#define sodium_init fixture_sodium
#define preallocateFile fixture_preallocate
#define openFileWithRetry fixture_open
'''
marker = 'bool run_random_write_test() {'
assert source.count(marker) == 1
unit = source.replace(marker, seam + '\n' + marker)
results = []
with tempfile.TemporaryDirectory(prefix='simplidfs-status-') as directory:
    base = Path(directory)
    generated = base / 'candidate.cpp'
    generated.write_text(unit)
    for standard in ('c++17', 'c++20'):
        binary = base / standard
        command = ['g++', '-std=' + standard, '-pthread', '-I' + str(root / 'tests'),
                   str(generated), str(root / 'tests/fuse_random_write_main.cpp'),
                   '-lsodium', '-o', str(binary)]
        build = subprocess.run(command, capture_output=True, text=True, timeout=90)
        assert build.returncode == 0, build.stderr
        for case in ('mount', 'sodium', 'create', 'preallocate', 'verification-open', 'success'):
            mount = base / (standard + '-' + case)
            mount.mkdir()
            if case == 'create':
                (mount / 'concurrent_write_test.txt').mkdir()
            env = dict(os.environ, STATUS_FAULT=case, SIMPLIDFS_CONCURRENCY_MOUNT=str(mount))
            run = subprocess.run([str(binary)], env=env, capture_output=True, text=True, timeout=30)
            (args.output / (standard + '-' + case + '.log')).write_text(run.stdout + run.stderr)
            expected = 0 if case == 'success' else 1
            results.append(dict(standard=standard, case=case, expected=expected, actual=run.returncode))
        results.append(dict(standard=standard, build_command=command, build_exit=build.returncode))
report = dict(source_sha256=hashlib.sha256(args.source.read_bytes()).hexdigest(),
              main_sha256=hashlib.sha256((root / 'tests/fuse_random_write_main.cpp').read_bytes()).hexdigest(),
              compiler=subprocess.check_output(['g++', '--version'], text=True).splitlines()[0],
              results=results)
(args.output / 'results.json').write_text(json.dumps(report, indent=2) + '\n')
failures = [r for r in results if 'expected' in r and r['actual'] != r['expected']]
print(json.dumps(dict(cases=12, failures=failures)))
raise SystemExit(bool(failures))
