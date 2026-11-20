#!/usr/bin/env python3
"""Run actual startup cleanup with disposable processes and inert mount tools."""
import json
import os
import subprocess
import tempfile
from pathlib import Path

root = Path(__file__).resolve().parents[2]
source = Path(os.environ.get('WRAPPER_SOURCE', str(root / 'tests/run_fuse_concurrency_test_wrapper.sh'))).read_text()
# Include the historical broad-kill section so restoring it fails this regression.
start = source.index('# The mount and data directories') if '# The mount and data directories' in source else source.index('# Clean up any previous instances')
fragment = source[start:source.index('# Clear logs and PID files')]
with tempfile.TemporaryDirectory(prefix='simplidfs cleanup ') as directory:
    base = Path(directory)
    tools = base / 'tools'
    tools.mkdir()
    calls = base / 'calls'
    for name in ['pkill', 'fusermount3', 'fusermount', 'umount']:
        tool = tools / name
        tool.write_text('#!/bin/bash\nprintf "%s\\n" "${0##*/}" >> "$CALLS"\nexit 1\n')
        tool.chmod(0o755)
    # A separate test invocation may use the same executable names.
    other = subprocess.Popen(['bash', '-c', 'exec -a metaserver sleep 60'])
    try:
        mount = base / 'mount with spaces'
        mount.mkdir()
        (mount / 'stale').write_text('disposable')
        env = dict(os.environ, PATH=str(tools) + ':' + os.environ['PATH'], CALLS=str(calls))
        result = subprocess.run(['bash', '-c', 'set -euo pipefail\nMOUNT_POINT=$1\nNODE_EXEC=$2\n' + fragment,
                                 'fixture', str(mount), '/inert/node'], env=env,
                                capture_output=True, text=True, timeout=10)
        assert result.returncode == 0, result
        invoked = calls.read_text().splitlines()
        assert 'pkill' not in invoked, invoked
        assert invoked == ['fusermount3', 'fusermount', 'umount'], invoked
        assert other.poll() is None, 'unrelated process stopped'
        assert mount.is_dir() and not (mount / 'stale').exists()
        print(json.dumps({'passed': 1, 'startup_exit': result.returncode,
                          'unmount_fallbacks': invoked, 'unrelated_process_alive': True}))
    finally:
        other.terminate()
        other.wait(timeout=5)
