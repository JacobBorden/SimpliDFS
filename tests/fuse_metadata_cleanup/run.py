#!/usr/bin/env python3
"""Verify actual wrapper startup cleanup preserves caller-owned metadata."""
import argparse
import json
import subprocess
import tempfile
from pathlib import Path

root = Path(__file__).resolve().parents[2]
parser = argparse.ArgumentParser()
parser.add_argument('--source', type=Path, default=root / 'tests/run_fuse_concurrency_test_wrapper.sh')
args = parser.parse_args()
source = args.source.read_text()
fragment = source[source.index('# Clear logs and PID files'):source.index('cleanup_and_exit() {')]
with tempfile.TemporaryDirectory(prefix='simplidfs metadata ') as directory:
    base = Path(directory)
    caller = base / 'caller'
    caller.mkdir()
    disposable = base / 'disposable'
    disposable.mkdir()
    names = ['file_metadata.dat', 'node_registry.dat']
    for name in names:
        (caller / name).write_bytes(b'caller-owned sentinel\x00')
    setup = """set -euo pipefail
BASE_TMP_DIR=$1
METASERVER_LOG="$BASE_TMP_DIR/meta.log"
FUSE_ADAPTER_STDOUT_LOG="$BASE_TMP_DIR/fuse.log"
NUM_NODES=3
rm() {
    for target in "$@"; do
        if [[ "$target" == /tmp/* ]]; then
            [[ "$target" == "$BASE_TMP_DIR/"* ]] || continue
        fi
        command rm -f -- "$target"
    done
}
"""
    run = subprocess.run(['bash', '-c', setup + fragment, 'fixture', str(disposable)],
                         cwd=caller, capture_output=True, text=True, timeout=10)
    assert run.returncode == 0, run
    for name in names:
        assert (caller / name).exists(), name + ' was deleted'
        assert (caller / name).read_bytes() == b'caller-owned sentinel\x00'
    print(json.dumps({'cases_passed': 1, 'caller_metadata_files_preserved': len(names),
                      'exit': run.returncode, 'stdout': run.stdout, 'stderr': run.stderr}))
