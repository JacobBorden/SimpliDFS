#!/usr/bin/env python3
"""Exercise actual wrapper policy staging and node launch with inert nodes."""
import json
import subprocess
import tempfile
from pathlib import Path

root = Path(__file__).resolve().parents[2]
wrapper = (root / 'tests/run_fuse_concurrency_test_wrapper.sh').read_text()
staging = wrapper.split('# Stage the repository test policy', 1)[1].split('# Clean up any previous', 1)[0]
staging = '# Stage the repository test policy' + staging
launch = next(line.strip() for line in wrapper.splitlines() if '(cd -- "${NODE_WORK_DIR}" && exec' in line)
probe = r'''
#include "utilities/rbac.h"
int main() {
  SimpliDFS::RBACPolicy policy;
  if (!policy.loadFromFile("rbac_policy.yaml")) return 1;
  if (!policy.isAllowed(0, "write") || !policy.isAllowed(1000, "read")) return 2;
  if (policy.isAllowed(1001, "write") || policy.isAllowed(9999, "read")) return 3;
}
'''
results = []
with tempfile.TemporaryDirectory(prefix='simplidfs policy ') as directory:
    base = Path(directory)
    source = base / 'probe.cpp'
    source.write_text(probe)
    for standard in ['c++17', 'c++20']:
        binary = base / ('node ' + standard)
        subprocess.run(['g++', '-std=' + standard, '-Wall', '-Wextra', '-Werror',
                        '-I' + str(root / 'include'), str(source),
                        str(root / 'src/utilities/rbac.cpp'), '-lyaml-cpp', '-o', str(binary)], check=True)
        # Mirror only the source layout needed for BASH_SOURCE resolution.
        fixture = base / standard
        (fixture / 'tests').mkdir(parents=True)
        (fixture / 'rbac_policy.yaml').write_bytes((root / 'rbac_policy.yaml').read_bytes())
        script = fixture / 'tests/stage.sh'
        script.write_text('set -euo pipefail\nBASE_TMP_DIR=$1\nNODE_EXEC=$2\n' + staging + '\n'
                          'NODE_NAME=fixture\nNODE_PORT=1\nMETASERVER_PORT=2\nNODE_LOG_FILE="$BASE_TMP_DIR/node.log"\n'
                          + launch + '\nwait $!\n')
        run = subprocess.run(['bash', str(script), str(fixture), str(binary)], cwd=base,
                             capture_output=True, text=True)
        assert run.returncode == 0, run
        assert (fixture / 'nodes/rbac_policy.yaml').read_bytes() == (root / 'rbac_policy.yaml').read_bytes()
        # Missing source policy must stop staging rather than launch a node.
        (fixture / 'rbac_policy.yaml').unlink()
        negative = subprocess.run(['bash', str(script), str(fixture), str(binary)], cwd=base,
                                  capture_output=True, text=True)
        assert negative.returncode != 0, negative
        results.append({'standard': standard, 'launch_exit': run.returncode,
                        'missing_policy_exit': negative.returncode})
print(json.dumps({'passed': len(results), 'results': results}, indent=2))
