#!/bin/bash

echo "INFO: Wrapper script starting..."

# Paths to executables - CTest's WORKING_DIRECTORY for the test is typically build/tests.
METASERVER_EXEC=../metaserver
FUSE_ADAPTER_EXEC=../simpli_fuse_adapter
TEST_EXEC=./SimpliDFSFuseConcurrencyTest

MOUNT_POINT="/tmp/myfusemount"
METASERVER_LOG="/tmp/metaserver_wrapper.log"
FUSE_ADAPTER_STDOUT_LOG="/tmp/fuse_adapter_wrapper.log"
# SimpliDFSFuseConcurrencyTest will log to its own stdout/stderr, captured by CTest

# Clean up any previous instances or remnants
echo "INFO: Initial cleanup..."
# Use pkill to be more robust in finding processes by name
pkill -f metaserver > /dev/null 2>&1 || true
pkill -f simpli_fuse_adapter > /dev/null 2>&1 || true
# Try fusermount3 first, then fusermount, then umount
fusermount3 -u ${MOUNT_POINT} > /dev/null 2>&1 || \
   fusermount -u ${MOUNT_POINT} > /dev/null 2>&1 || \
   umount ${MOUNT_POINT} > /dev/null 2>&1 || true
rm -rf ${MOUNT_POINT}
mkdir -p ${MOUNT_POINT}
rm -f ${METASERVER_LOG} ${FUSE_ADAPTER_STDOUT_LOG} /tmp/fuse_adapter_main.log # Clear previous main log too
rm -f /tmp/metaserver_wrapper.pid /tmp/fuse_adapter_wrapper.pid # Clear PID files

# Start Metaserver
echo "INFO: Starting Metaserver..."
${METASERVER_EXEC} 60000 > ${METASERVER_LOG} 2>&1 &
METASERVER_PID=$!
echo ${METASERVER_PID} > /tmp/metaserver_wrapper.pid
sleep 2 # Give it a moment to start

# Check Metaserver
if ! ps -p ${METASERVER_PID} > /dev/null; then
   echo "ERROR: Metaserver process ${METASERVER_PID} failed to start or died."
   echo "--- Metaserver Log (${METASERVER_LOG}) ---"
   cat ${METASERVER_LOG}
   echo "-------------------------------------------"
   exit 1
fi
echo "INFO: Metaserver PID: ${METASERVER_PID}"

# Start FUSE adapter
echo "INFO: Starting FUSE adapter..."
# The -f flag keeps fuse in the foreground for its own process, but we still background the script's execution of it.
${FUSE_ADAPTER_EXEC} 127.0.0.1 60000 ${MOUNT_POINT} -f > ${FUSE_ADAPTER_STDOUT_LOG} 2>&1 &
FUSE_ADAPTER_PID=$!
echo ${FUSE_ADAPTER_PID} > /tmp/fuse_adapter_wrapper.pid
echo "INFO: Waiting for FUSE adapter to mount (5 seconds)..."
sleep 5 # Give it time to mount

# Check FUSE Adapter and Mount
if ! ps -p ${FUSE_ADAPTER_PID} > /dev/null; then
   echo "ERROR: FUSE adapter process ${FUSE_ADAPTER_PID} failed to start or died."
   echo "--- FUSE Adapter Stdout/Stderr Log (${FUSE_ADAPTER_STDOUT_LOG}) ---"
   cat ${FUSE_ADAPTER_STDOUT_LOG}
   echo "--------------------------------------------------------------------"
   if [ -f /tmp/fuse_adapter_main.log ]; then
       echo "--- FUSE Adapter Main Log (/tmp/fuse_adapter_main.log) ---"
       cat /tmp/fuse_adapter_main.log
       echo "-----------------------------------------------------------"
   fi
   kill ${METASERVER_PID} # Cleanup metaserver
   wait ${METASERVER_PID} 2>/dev/null || true
   exit 1
fi
echo "INFO: FUSE Adapter PID: ${FUSE_ADAPTER_PID}"

if ! mount | grep -q ${MOUNT_POINT}; then
    echo "ERROR: Mount check: ${MOUNT_POINT} does not appear to be mounted."
    echo "--- FUSE Adapter Stdout/Stderr Log (${FUSE_ADAPTER_STDOUT_LOG}) ---"
    cat ${FUSE_ADAPTER_STDOUT_LOG}
    echo "--------------------------------------------------------------------"
    if [ -f /tmp/fuse_adapter_main.log ]; then
        echo "--- FUSE Adapter Main Log (/tmp/fuse_adapter_main.log) ---"
        cat /tmp/fuse_adapter_main.log
        echo "-----------------------------------------------------------"
    fi
    kill ${FUSE_ADAPTER_PID} # Cleanup fuse adapter
    wait ${FUSE_ADAPTER_PID} 2>/dev/null || true
    kill ${METASERVER_PID} # Cleanup metaserver
    wait ${METASERVER_PID} 2>/dev/null || true
    exit 1
fi
echo "INFO: FUSE successfully mounted at ${MOUNT_POINT}."

# Run the actual test
echo "INFO: Running SimpliDFSFuseConcurrencyTest..."
${TEST_EXEC}
TEST_EXIT_CODE=$?
echo "INFO: Test executable finished with exit code: ${TEST_EXIT_CODE}"

# Cleanup
echo "INFO: Cleaning up..."
echo "INFO: Killing FUSE adapter PID ${FUSE_ADAPTER_PID}..."
kill ${FUSE_ADAPTER_PID}
wait ${FUSE_ADAPTER_PID} 2>/dev/null || true

echo "INFO: Killing Metaserver PID ${METASERVER_PID}..."
kill ${METASERVER_PID}
wait ${METASERVER_PID} 2>/dev/null || true

echo "INFO: Unmounting ${MOUNT_POINT}..."
fusermount3 -u ${MOUNT_POINT} > /dev/null 2>&1 || \
   fusermount -u ${MOUNT_POINT} > /dev/null 2>&1 || \
   umount ${MOUNT_POINT} > /dev/null 2>&1 || true

# rm -f /tmp/metaserver_wrapper.pid /tmp/fuse_adapter_wrapper.pid # Keep other logs for now

echo "INFO: Wrapper script finished. Exiting with test exit code: ${TEST_EXIT_CODE}"
exit ${TEST_EXIT_CODE}
