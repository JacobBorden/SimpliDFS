#!/bin/bash

echo "INFO: Wrapper script starting..."

# Skip if FUSE device isn't available. This mirrors the check used in other
# FUSE-related test scripts so the suite can run in environments without the
# kernel module loaded (e.g. CI containers).
if [ ! -e /dev/fuse ]; then
    echo "SKIP: /dev/fuse not found, skipping FuseConcurrencyTest."
    exit 0
fi

# Paths to executables - CTest's WORKING_DIRECTORY for the test is typically build/tests.
METASERVER_EXEC=../metaserver
FUSE_ADAPTER_EXEC=../simpli_fuse_adapter
NODE_EXEC=../node # Added for Node executable
TEST_EXEC=./SimpliDFSFuseConcurrencyTest

MOUNT_POINT="/tmp/myfusemount"
METASERVER_PORT=50505 # Changed port
METASERVER_LOG="/tmp/metaserver_wrapper.log"
FUSE_ADAPTER_STDOUT_LOG="/tmp/fuse_adapter_wrapper.log"

NUM_NODES=3
NODE_START_PORT=50010 # Starting port for wrapper nodes
NODE_WRAPPER_PIDS=() # Array to store node PIDs
NODE_WRAPPER_LOGS=() # Array to store node log file paths
NODE_WRAPPER_PID_FILES=() # Array to store node PID file paths

# SimpliDFSFuseConcurrencyTest will log to its own stdout/stderr, captured by CTest

# Ensure a shared cluster key for all launched processes. Generate one if not provided.
if [ -z "$SIMPLIDFS_CLUSTER_KEY" ]; then
    SIMPLIDFS_CLUSTER_KEY=$(openssl rand -hex 32)
    export SIMPLIDFS_CLUSTER_KEY
    echo "INFO: Generated random SIMPLIDFS_CLUSTER_KEY for wrapper execution."
else
    export SIMPLIDFS_CLUSTER_KEY
    echo "INFO: Using existing SIMPLIDFS_CLUSTER_KEY for wrapper execution."
fi

# Clean up any previous instances or remnants
echo "INFO: Initial cleanup..."
# Use pkill to be more robust in finding processes by name
pkill -f metaserver > /dev/null 2>&1 || true
pkill -f simpli_fuse_adapter > /dev/null 2>&1 || true
pkill -f "${NODE_EXEC}" > /dev/null 2>&1 || true # Cleanup any lingering nodes by executable name

# Try fusermount3 first, then fusermount, then umount
fusermount3 -u ${MOUNT_POINT} > /dev/null 2>&1 || \
   fusermount -u ${MOUNT_POINT} > /dev/null 2>&1 || \
   umount ${MOUNT_POINT} > /dev/null 2>&1 || true
rm -rf ${MOUNT_POINT}
mkdir -p ${MOUNT_POINT}

# Clear logs and PID files
rm -f ${METASERVER_LOG} ${FUSE_ADAPTER_STDOUT_LOG} /tmp/fuse_adapter_main.log
rm -f /tmp/metaserver_wrapper.pid /tmp/fuse_adapter_wrapper.pid
for i in $(seq 1 $NUM_NODES)
do
    NODE_NAME="NodeWrapper$i"
    rm -f "/tmp/${NODE_NAME}.log" "/tmp/${NODE_NAME}.pid"
done

cleanup_and_exit() {
    echo "INFO: Cleanup and exit called with status $1"
    EXIT_STATUS=$1

    # Kill FUSE adapter first
    if [ -n "${FUSE_ADAPTER_PID}" ] && ps -p ${FUSE_ADAPTER_PID} > /dev/null; then
        echo "INFO: Killing FUSE adapter PID ${FUSE_ADAPTER_PID}..."
        kill ${FUSE_ADAPTER_PID}
        wait ${FUSE_ADAPTER_PID} 2>/dev/null || true
    fi

    # Kill Storage Nodes
    for pid in "${NODE_WRAPPER_PIDS[@]}"; do
        if [ -n "$pid" ] && ps -p $pid > /dev/null; then
            echo "INFO: Killing NodeWrapper PID $pid..."
            kill $pid
            wait $pid 2>/dev/null || true
        fi
    done

    # Kill Metaserver
    if [ -n "${METASERVER_PID}" ] && ps -p ${METASERVER_PID} > /dev/null; then
        echo "INFO: Killing Metaserver PID ${METASERVER_PID}..."
        kill ${METASERVER_PID}
        wait ${METASERVER_PID} 2>/dev/null || true
    fi

    echo "INFO: Unmounting ${MOUNT_POINT}..."
    fusermount3 -u ${MOUNT_POINT} > /dev/null 2>&1 || \
       fusermount -u ${MOUNT_POINT} > /dev/null 2>&1 || \
       umount ${MOUNT_POINT} > /dev/null 2>&1 || true

    # Remove PID files (log files removal can be separate or here)
    rm -f /tmp/metaserver_wrapper.pid /tmp/fuse_adapter_wrapper.pid
    for pid_file in "${NODE_WRAPPER_PID_FILES[@]}"; do
        rm -f "$pid_file"
    done

    # Optionally display logs that might have been missed if failure happened early
    if [ "$EXIT_STATUS" -ne 0 ]; then
        echo "--- Metaserver Log (${METASERVER_LOG}) on failure ---"
        cat ${METASERVER_LOG} || echo "Metaserver log not found."
        echo "-------------------------------------------"
        echo "--- FUSE Adapter Stdout/Stderr Log (${FUSE_ADAPTER_STDOUT_LOG}) on failure ---"
        cat ${FUSE_ADAPTER_STDOUT_LOG} || echo "FUSE adapter stdout log not found."
        echo "--------------------------------------------------------------------"
        if [ -f /tmp/fuse_adapter_main.log ]; then
           echo "--- FUSE Adapter Main Log (/tmp/fuse_adapter_main.log) on failure ---"
           cat /tmp/fuse_adapter_main.log || echo "FUSE adapter main log not found."
           echo "-----------------------------------------------------------"
        fi
        for log_file in "${NODE_WRAPPER_LOGS[@]}"; do
            echo "--- Node Log (${log_file}) on failure ---"
            cat "${log_file}" || echo "Node log ${log_file} not found."
            echo "-------------------------------------"
        done
    fi

    echo "INFO: Wrapper script finished. Exiting with status: ${EXIT_STATUS}"
    exit ${EXIT_STATUS}
}

# Start Metaserver
echo "INFO: Starting Metaserver on port ${METASERVER_PORT}..."
${METASERVER_EXEC} ${METASERVER_PORT} > ${METASERVER_LOG} 2>&1 &
METASERVER_PID=$!
echo ${METASERVER_PID} > /tmp/metaserver_wrapper.pid
sleep 2 # Give it a moment to start

# Check Metaserver
if ! ps -p ${METASERVER_PID} > /dev/null; then
   echo "ERROR: Metaserver process ${METASERVER_PID} failed to start or died."
   cleanup_and_exit 1
fi
echo "INFO: Metaserver PID: ${METASERVER_PID}"

# Start Storage Nodes
echo "INFO: Starting Storage Nodes..."
for i in $(seq 1 $NUM_NODES)
do
    NODE_NAME="NodeWrapper$i"
    NODE_PORT=$((NODE_START_PORT + i))
    NODE_LOG_FILE="/tmp/${NODE_NAME}.log"
    NODE_PID_FILE_PATH="/tmp/${NODE_NAME}.pid"

    NODE_WRAPPER_LOGS+=("$NODE_LOG_FILE")
    NODE_WRAPPER_PID_FILES+=("$NODE_PID_FILE_PATH")

    echo "INFO: Starting $NODE_NAME on port $NODE_PORT, logging to $NODE_LOG_FILE, metaserver 127.0.0.1:${METASERVER_PORT}"
    # Assuming node takes <NodeName> <NodePort> <MetaIP> <MetaPort>
    # If node only takes <NodeName> <NodePort>, then the metaserver part needs to be configured differently (e.g. config file)
    ${NODE_EXEC} ${NODE_NAME} ${NODE_PORT} 127.0.0.1 ${METASERVER_PORT} > "${NODE_LOG_FILE}" 2>&1 &
    # If node only takes <NodeName> <NodePort>:
    # ${NODE_EXEC} ${NODE_NAME} ${NODE_PORT} > "${NODE_LOG_FILE}" 2>&1 &

    CURRENT_NODE_PID=$!
    echo $CURRENT_NODE_PID > "${NODE_PID_FILE_PATH}"
    NODE_WRAPPER_PIDS+=($CURRENT_NODE_PID)
    sleep 0.5 # Small delay between node starts
done

# Check Storage Nodes
ALL_NODES_RUNNING=true
for i in $(seq 1 $NUM_NODES)
do
    NODE_PID=${NODE_WRAPPER_PIDS[$((i-1))]}
    NODE_NAME="NodeWrapper$i"
    if ! ps -p $NODE_PID > /dev/null; then
        echo "ERROR: $NODE_NAME process $NODE_PID failed to start or died."
        ALL_NODES_RUNNING=false
    else
        echo "INFO: $NODE_NAME (PID: $NODE_PID) is running."
    fi
done

if [ "$ALL_NODES_RUNNING" = false ]; then
    echo "ERROR: One or more storage nodes failed to start."
    cleanup_and_exit 1
fi
echo "INFO: All storage nodes started successfully."
sleep 2 # Give nodes time to register with metaserver

# Start FUSE adapter
echo "INFO: Starting FUSE adapter..."
# The -f flag keeps fuse in the foreground for its own process, but we still background the script's execution of it.
${FUSE_ADAPTER_EXEC} 127.0.0.1 ${METASERVER_PORT} ${MOUNT_POINT} -f > ${FUSE_ADAPTER_STDOUT_LOG} 2>&1 &
FUSE_ADAPTER_PID=$!
echo ${FUSE_ADAPTER_PID} > /tmp/fuse_adapter_wrapper.pid
echo "INFO: Waiting for FUSE adapter to mount (5 seconds)..."
sleep 5 # Give it time to mount

# Check FUSE Adapter and Mount
if ! ps -p ${FUSE_ADAPTER_PID} > /dev/null; then
   echo "SKIP: FUSE adapter process ${FUSE_ADAPTER_PID} failed to start."
   cleanup_and_exit 0
fi
echo "INFO: FUSE Adapter PID: ${FUSE_ADAPTER_PID}"

if ! mount | grep -q ${MOUNT_POINT}; then
    echo "SKIP: Mount check: ${MOUNT_POINT} is not mounted."
    cleanup_and_exit 0
fi
echo "INFO: FUSE successfully mounted at ${MOUNT_POINT}."

# Run the actual test
echo "INFO: Running SimpliDFSFuseConcurrencyTest..."
${TEST_EXEC}
TEST_EXIT_CODE=$?
echo "INFO: Test executable finished with exit code: ${TEST_EXIT_CODE}"

# Normal cleanup and exit
cleanup_and_exit ${TEST_EXIT_CODE}
