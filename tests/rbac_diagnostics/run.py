#!/usr/bin/env python3
"""Run actual Node policy-load/authorization fragments with inert transport."""
import json
import subprocess
import tempfile
from pathlib import Path

root = Path(__file__).resolve().parents[2]
source = (root / 'include/node/node.h').read_text()
constructor = source.split('    const bool policyLoaded =', 1)[1].split('\n  }', 1)[0]
constructor = 'const bool policyLoaded =' + constructor
allowed = source.split('      auto allowed =', 1)[1].split('\n\n      switch', 1)[0]
program = r'''
#include "utilities/rbac.h"
#include <iostream>
#include <string>
struct Transport {
  std::string sent;
  void Send(const std::string &text, int) { sent = text; }
};
int main(int argc, char **argv) {
  SimpliDFS::RBACPolicy rbacPolicy;
  std::string nodeName = "disposable-node";
  Transport server;
  int client = 0;
  struct { unsigned int _Uid; } message{static_cast<unsigned int>(std::stoul(argv[1]))};
''' + constructor + '\n auto allowed =' + allowed + r'''
  const bool result = allowed("read");
  if (result != (std::string(argv[2]) == "allow")) return 1;
  if (server.sent != (result ? "" : "Access denied")) return 2;
}
'''
results = []
with tempfile.TemporaryDirectory(prefix='simplidfs-rbac-') as directory:
    base = Path(directory)
    cpp = base / 'probe.cpp'
    cpp.write_text(program)
    for standard in ['c++17', 'c++20']:
        binary = base / standard
        command = ['g++', '-std=' + standard, '-Wall', '-Wextra', '-Werror',
                   '-Wno-unused-parameter', '-I' + str(root / 'include'), str(cpp),
                   str(root / 'src/utilities/rbac.cpp'), '-lyaml-cpp', '-o', str(binary)]
        subprocess.run(command, check=True, capture_output=True, text=True)
        for name, policy, uid, permitted, loaded in [
            ('missing', None, 1000, False, False),
            ('malformed', 'roles: [', 1000, False, False),
            ('authorized', (root / 'rbac_policy.yaml').read_text(), 1000, True, True),
            ('unknown-user', (root / 'rbac_policy.yaml').read_text(), 9999, False, True),
        ]:
            path = base / 'rbac_policy.yaml'
            path.unlink(missing_ok=True)
            if policy is not None:
                path.write_text(policy)
            run = subprocess.run([str(binary), str(uid), 'allow' if permitted else 'deny'],
                                 cwd=base, capture_output=True, text=True)
            assert run.returncode == 0, run
            assert ('path=rbac_policy.yaml loaded=' + str(int(loaded))) in run.stderr, run.stderr
            denial = f'RBAC denied uid={uid} operation=read'
            assert (denial in run.stderr) == (not permitted), run.stderr
            results.append({'standard': standard, 'case': name, 'exit': run.returncode,
                            'stderr': run.stderr})
print(json.dumps({'passed': len(results), 'results': results}, indent=2))
