#!/bin/bash

# SimpliDFS FUSE stress test runner.
# This script repeatedly executes the FUSE concurrency tests to
# aggressively exercise the filesystem implementation.
#
# It is designed for CI use and assumes all binaries are already
# built in the ./build directory.

set -euo pipefail

# Number of times each FUSE test is executed. Can be overridden by
# setting the STRESS_ITERATIONS environment variable.
ITERATIONS="${STRESS_ITERATIONS:-3}"

# Skip if FUSE device is unavailable (e.g., CI environment without the kernel module).
if [ ! -e /dev/fuse ]; then
    echo "SKIP: /dev/fuse not found, skipping FUSE stress tests."
    exit 0
fi

# Move to the directory containing compiled test binaries.
cd build/tests

# Path to the wrapper script that handles launching the metaserver,
# storage nodes and FUSE adapter for each test run.
WRAPPER="../../tests/run_fuse_concurrency_test_wrapper.sh"

# Array of FUSE concurrency test executables to run.
TEST_EXECUTABLES=(
    "SimpliDFSFuseRandomWriteTest"
    "SimpliDFSFuseAppendTest"
    "SimpliDFSFuseRandomWriteSingleThreadTest"
    "SimpliDFSFuseAppendSingleThreadTest"
)

for test_exec in "${TEST_EXECUTABLES[@]}"; do
    echo "INFO: Stress testing ${test_exec} for ${ITERATIONS} iterations"
    for i in $(seq 1 "${ITERATIONS}"); do
        echo "INFO: [${test_exec}] Iteration ${i}/${ITERATIONS}"
        "${WRAPPER}" "${test_exec}"
    done
    echo "INFO: Completed stress runs for ${test_exec}"
    echo "" # Blank line for readability
done

cd ../..

echo "INFO: FUSE stress testing complete."

