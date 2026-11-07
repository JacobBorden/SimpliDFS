#!/usr/bin/env python3
"""Run finite synthetic key-manager regressions in independent processes."""
import argparse
import json
from pathlib import Path
import subprocess


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--suite', choices=('hex', 'rotation', 'combined'), required=True)
    parser.add_argument('--executable', type=Path, required=True)
    args = parser.parse_args()
    cases = json.loads(Path(__file__).with_name('cases.json').read_text())
    modes = ('failure-with-previous', 'failure-without-previous',
             'initialization-failure', 'success')
    invocations = []
    if args.suite == 'rotation':
        invocations = [(mode, [mode]) for mode in modes]
    else:
        for index, case in enumerate(cases):
            values = [case['input'], case['expected'], 'valid' if case['valid'] else 'invalid']
            if args.suite == 'combined':
                values.insert(0, modes[index % len(modes)])
            invocations.append((case['name'], values))
    for name, values in invocations:
        result = subprocess.run([str(args.executable.resolve()), *values],
                                capture_output=True, text=True, timeout=10)
        if result.returncode or result.stderr or 'cleanup:' not in result.stdout:
            print(f'FAIL {args.suite}/{name}: exit={result.returncode}')
            print(result.stdout + result.stderr)
            return 1
    print(f'PASS {args.suite}: {len(invocations)} cases; all secure allocations freed and zeroed')
    return 0


if __name__ == '__main__':
    raise SystemExit(main())
