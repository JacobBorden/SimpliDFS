#!/usr/bin/env python3
"""Run actual startup and failure cleanup with a disposable shared-log sentinel."""
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
startup = source[source.index('# Clear logs and PID files'):source.index('cleanup_and_exit() {')]
cleanup = source[source.index('cleanup_and_exit() {'):source.index('# Start Metaserver\n')]
with tempfile.TemporaryDirectory(prefix='simplidfs-log-') as directory:
    base = Path(directory)
    owned = base / 'owned'
    owned.mkdir()
    sentinel = base / 'shared.log'
    sentinel.write_text('unrelated-log-secret')
    setup = """set -euo pipefail
BASE_TMP_DIR=$1
METASERVER_LOG="$BASE_TMP_DIR/meta"
FUSE_ADAPTER_STDOUT_LOG="$BASE_TMP_DIR/adapter"
MOUNT_POINT="$BASE_TMP_DIR/mount"
NUM_NODES=0
NODE_WRAPPER_PIDS=()
NODE_WRAPPER_PID_FILES=()
NODE_WRAPPER_LOGS=()
METASERVER_PID=""
FUSE_ADAPTER_PID=""
fusermount3() { return 1; }
fusermount() { return 1; }
umount() { return 1; }
"""
    # Substitute only the inherited global path: never access an actual shared log.
    script = setup + startup + """
echo owned-meta > "$METASERVER_LOG"
echo owned-adapter > "$FUSE_ADAPTER_STDOUT_LOG"
""" + cleanup + "cleanup_and_exit 37\n"
    script = script.replace('/tmp/fuse_adapter_main.log', str(sentinel))
    run = subprocess.run(['bash', '-c', script, 'fixture', str(owned)],
                         capture_output=True, text=True, timeout=10)
    assert run.returncode == 37, run
    assert sentinel.exists(), 'shared log deleted'
    assert sentinel.read_text() == 'unrelated-log-secret'
    assert 'unrelated-log-secret' not in run.stdout, 'shared log disclosed'
    assert 'owned-meta' in run.stdout and 'owned-adapter' in run.stdout, run
    assert not owned.exists(), run
    print(json.dumps({'passed': 1, 'exit': run.returncode,
                      'shared_log_preserved': True, 'shared_log_disclosed': False}))
