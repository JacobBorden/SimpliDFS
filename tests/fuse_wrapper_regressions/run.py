#!/usr/bin/env python3
"""Exercise actual wrapper cleanup/test tail without processes, sockets or FUSE."""
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
cleanup = source[source.index('cleanup_and_exit() {'):source.index('# Start Metaserver\n')]
tail = source[source.index('# Run the actual test\n'):]
results = []
for code, failed_kill in [(0, False), (1, False), (37, False), (37, True)]:
    with tempfile.TemporaryDirectory(prefix='simplidfs-wrapper-') as directory:
        base = Path(directory)
        for name in ['meta', 'adapter', 'node']:
            (base / name).write_text(name + '-diagnostic\n')
        test = base / 'test with spaces'
        test.write_text('#!/bin/bash\nexit ' + str(code) + '\n')
        test.chmod(0o700)
        # Shell functions intercept every process/unmount operation. Only the
        # disposable directory is removed by the production cleanup function.
        setup = '''set -euo pipefail
BASE_TMP_DIR=$1
TEST_EXEC="$BASE_TMP_DIR/test with spaces"
METASERVER_LOG="$BASE_TMP_DIR/meta"
FUSE_ADAPTER_STDOUT_LOG="$BASE_TMP_DIR/adapter"
MOUNT_POINT="$BASE_TMP_DIR/mount"
NODE_WRAPPER_PIDS=()
NODE_WRAPPER_PID_FILES=()
NODE_WRAPPER_LOGS=("$BASE_TMP_DIR/node")
METASERVER_PID=""
FUSE_ADAPTER_PID=""
ps() { return 0; }
kill() { echo simulated-kill-failure; return 1; }
wait() { return 1; }
fusermount3() { return 1; }
fusermount() { return 1; }
umount() { return 1; }
'''
        if failed_kill:
            setup += 'FUSE_ADAPTER_PID=12345\n'
        # Redirect the inherited global log path into this fixture as well.
        script = setup + cleanup.replace('/tmp/fuse_adapter_main.log', str(base / 'absent')) + tail
        run = subprocess.run(['bash', '-c', script, 'wrapper-fixture', directory], capture_output=True, text=True)
        assert run.returncode == code, run
        assert not base.exists(), run.stdout
        assert 'Wrapper script finished.' in run.stdout, run.stdout
        for name in ['meta', 'adapter', 'node']:
            assert ((name + '-diagnostic') in run.stdout) == (code != 0), run.stdout
        if failed_kill:
            assert 'simulated-kill-failure' in run.stdout
        results.append({'test_exit': code, 'cleanup_kill_failure': failed_kill, 'exit': run.returncode, 'stdout': run.stdout, 'stderr': run.stderr})
print(json.dumps({'cases_passed': len(results), 'results': results}, indent=2))
