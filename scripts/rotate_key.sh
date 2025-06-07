#!/bin/bash
set -e
WINDOW=${1:-60}
REPO_ROOT=$(cd "$(dirname "$0")/.." && pwd)
"$REPO_ROOT/build/simplidfs" ctl rotate-key "$WINDOW"

