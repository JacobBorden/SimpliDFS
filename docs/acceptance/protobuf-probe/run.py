#!/usr/bin/env python3
"""Capture current project protocol-generation compatibility; no dependency writes."""
import argparse
import hashlib
import json
from pathlib import Path
import shlex
import shutil
import subprocess
import sys

parser = argparse.ArgumentParser()
parser.add_argument('--output', required=True)
args = parser.parse_args()
root = Path(__file__).resolve().parents[3]
out = Path(args.output).resolve()
out.mkdir(parents=True, exist_ok=False)
results = []
def run(argv):
    p = subprocess.run(argv, cwd=root, text=True, stdout=subprocess.PIPE,
                       stderr=subprocess.STDOUT, timeout=120)
    results.append(dict(command=argv, exit_code=p.returncode, output=p.stdout))
    return p

for command in [['c++', '--version'], ['protoc', '--version'],
                ['pkg-config', '--modversion', 'protobuf', 'grpc++']]:
    run(command)
flags = run(['pkg-config', '--cflags', 'grpc++'])
generate = run(['protoc', '-I.', '--cpp_out=' + str(out),
                '--grpc_out=' + str(out),
                '--plugin=protoc-gen-grpc=' + shutil.which('grpc_cpp_plugin'),
                'proto/filesystem.proto'])
if generate.returncode == 0 and flags.returncode == 0:
    for source in ['filesystem.pb.cc', 'filesystem.grpc.pb.cc']:
        run(['c++', '-std=c++17', '-fsyntax-only', '-I' + str(out)] +
            shlex.split(flags.stdout) + [str(out / 'proto' / source)])
libs = run(['pkg-config', '--libs', 'grpc++'])
if generate.returncode == 0 and flags.returncode == 0 and libs.returncode == 0:
    linked = run(['c++', '-std=c++17', '-I' + str(out)] + shlex.split(flags.stdout) +
                 [str(root / 'docs/acceptance/protobuf-probe/main.cpp'),
                  str(out / 'proto/filesystem.pb.cc'),
                  str(out / 'proto/filesystem.grpc.pb.cc'),
                  '/usr/lib/libprotobuf.a'] + shlex.split(libs.stdout) +
                 ['-o', str(out / 'probe')])
    if linked.returncode == 0:
        run([str(out / 'probe')])
        run(['ldd', str(out / 'probe')])
hashes = {}
for path in [root / 'CMakeLists.txt', root / 'proto/filesystem.proto',
             Path(__file__).resolve(), root / 'docs/acceptance/protobuf-probe/main.cpp', Path('/usr/include/google/protobuf/stubs/common.h')]:
    hashes[str(path)] = hashlib.sha256(path.read_bytes()).hexdigest()
report = dict(source_head=run(['git', 'rev-parse', 'HEAD']).stdout.strip(),
              canonical_base=run(['git', 'rev-parse', 'origin/development']).stdout.strip(),
              results=results, sha256=hashes,
              limits=['One dependency probe only; not full-static or project acceptance',
                      'No full-project build, FUSE or storage acceptance'])
(out / 'results.json').write_text(json.dumps(report, indent=2) + '\n')
failed = any(r['exit_code'] for r in results)
print(json.dumps(dict(commands=len(results), failed=failed, report=str(out / 'results.json'))))
sys.exit(1 if failed else 0)
