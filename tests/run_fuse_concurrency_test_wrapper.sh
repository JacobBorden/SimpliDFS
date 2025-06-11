#!/bin/bash

echo "INFO: Wrapper script starting..."

# Skip if FUSE device isn't available. This mirrors the check used in other
# FUSE-related test scripts so the suite can run in environments without the
# kernel module loaded (e.g. CI containers).
if [ ! -e /dev/fuse ]; then
    echo "SKIP: /dev/fuse not found, skipping FUSE concurrency test."
    exit 0
fi

# Paths to executables - CTest's WORKING_DIRECTORY for the test is typically build/tests.
METASERVER_EXEC=../metaserver
FUSE_ADAPTER_EXEC=../simpli_fuse_adapter
NODE_EXEC=../node # Added for Node executable
TEST_EXEC=${1:-./SimpliDFSFuseConcurrencyTest}

BASE_TMP_DIR=$(mktemp -d /tmp/fuse_concurrency_XXXXXX)
MOUNT_POINT="${BASE_TMP_DIR}/myfusemount"
export SIMPLIDFS_CONCURRENCY_MOUNT="${MOUNT_POINT}"
METASERVER_PORT=50505 # Changed port
METASERVER_LOG="${BASE_TMP_DIR}/metaserver_wrapper.log"
FUSE_ADAPTER_STDOUT_LOG="${BASE_TMP_DIR}/fuse_adapter_wrapper.log"

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
rm -f ${BASE_TMP_DIR}/metaserver_wrapper.pid ${BASE_TMP_DIR}/fuse_adapter_wrapper.pid
for i in $(seq 1 $NUM_NODES)
do
    NODE_NAME="NodeWrapper$i"
    rm -f "${BASE_TMP_DIR}/${NODE_NAME}.log" "${BASE_TMP_DIR}/${NODE_NAME}.pid"
done
# Remove stale metadata files to avoid persistence between runs
rm -f file_metadata.dat node_registry.dat

cleanup_and_exit() {
    echo "INFO: Cleanup and exit called with status $1"
    EXIT_STATUS=$1
    PIDS_TO_WAIT_ON=()

    # Kill FUSE adapter first
    FUSE_ADAPTER_PID_TO_KILL=""
    if [ -f ${BASE_TMP_DIR}/fuse_adapter_wrapper.pid ]; then
        FUSE_ADAPTER_PID_FROM_FILE=$(cat ${BASE_TMP_DIR}/fuse_adapter_wrapper.pid)
        if [ -n "${FUSE_ADAPTER_PID_FROM_FILE}" ] && ps -p ${FUSE_ADAPTER_PID_FROM_FILE} > /dev/null; then
            FUSE_ADAPTER_PID_TO_KILL=${FUSE_ADAPTER_PID_FROM_FILE}
        elif [ -n "${FUSE_ADAPTER_PID_FROM_FILE}" ]; then
             echo "INFO: FUSE adapter PID ${FUSE_ADAPTER_PID_FROM_FILE} from PID file not found running prior to kill."
        fi
    elif [ -n "${FUSE_ADAPTER_PID}" ] && ps -p ${FUSE_ADAPTER_PID} > /dev/null; then # Fallback to global var if PID file missing
        FUSE_ADAPTER_PID_TO_KILL=${FUSE_ADAPTER_PID}
        echo "WARN: FUSE adapter PID file missing, using global PID ${FUSE_ADAPTER_PID} for cleanup."
    fi

    if [ -n "${FUSE_ADAPTER_PID_TO_KILL}" ]; then
        echo "INFO: Attempting to kill FUSE adapter PID ${FUSE_ADAPTER_PID_TO_KILL}..."
        kill ${FUSE_ADAPTER_PID_TO_KILL}
        PIDS_TO_WAIT_ON+=(${FUSE_ADAPTER_PID_TO_KILL})
    else
        echo "INFO: FUSE adapter process not found or PID file missing, skipping kill."
    fi

    # Kill Storage Nodes
    for i in $(seq 0 $((${#NODE_WRAPPER_PIDS[@]}-1))); do
        # Prioritize PID from the array if available, then from file.
        NODE_PID_TO_KILL=""
        NODE_PID_ARRAY_VAL=${NODE_WRAPPER_PIDS[$i]}
        NODE_PID_FILE_VAL=""
        if [ -f "${NODE_WRAPPER_PID_FILES[$i]}" ]; then
            NODE_PID_FILE_VAL=$(cat "${NODE_WRAPPER_PID_FILES[$i]}")
        fi

        node_name_for_cleanup="NodeWrapper$((i+1))"

        if [ -n "$NODE_PID_ARRAY_VAL" ] && ps -p "$NODE_PID_ARRAY_VAL" > /dev/null; then
            NODE_PID_TO_KILL=$NODE_PID_ARRAY_VAL
            if [ -n "$NODE_PID_FILE_VAL" ] && [ "$NODE_PID_ARRAY_VAL" != "$NODE_PID_FILE_VAL" ]; then
                echo "WARN: $node_name_for_cleanup PID mismatch: array ($NODE_PID_ARRAY_VAL) vs file ($NODE_PID_FILE_VAL). Using array PID."
            fi
        elif [ -n "$NODE_PID_FILE_VAL" ] && ps -p "$NODE_PID_FILE_VAL" > /dev/null; then
            NODE_PID_TO_KILL=$NODE_PID_FILE_VAL
            echo "WARN: $node_name_for_cleanup PID for running process taken from file ($NODE_PID_FILE_VAL) as array PID ($NODE_PID_ARRAY_VAL) not running or invalid."
        elif [ -n "$NODE_PID_ARRAY_VAL" ] || [ -n "$NODE_PID_FILE_VAL" ]; then
             echo "INFO: $node_name_for_cleanup PID $NODE_PID_ARRAY_VAL (array) or $NODE_PID_FILE_VAL (file) not found running prior to kill."
        fi

        if [ -n "${NODE_PID_TO_KILL}" ]; then
            echo "INFO: Attempting to kill $node_name_for_cleanup PID ${NODE_PID_TO_KILL}..."
            kill ${NODE_PID_TO_KILL}
            PIDS_TO_WAIT_ON+=(${NODE_PID_TO_KILL})
        else
            # This case is already covered by the "not found running prior to kill" message above.
            # echo "INFO: $node_name_for_cleanup process not found, skipping kill."
            : # No operation needed here, placeholder for clarity
        fi
    done

    # Kill Metaserver
    METASERVER_PID_TO_KILL=""
    if [ -f ${BASE_TMP_DIR}/metaserver_wrapper.pid ]; then
        METASERVER_PID_FROM_FILE=$(cat ${BASE_TMP_DIR}/metaserver_wrapper.pid)
        if [ -n "${METASERVER_PID_FROM_FILE}" ] && ps -p ${METASERVER_PID_FROM_FILE} > /dev/null; then
            METASERVER_PID_TO_KILL=${METASERVER_PID_FROM_FILE}
        elif [ -n "${METASERVER_PID_FROM_FILE}" ]; then
            echo "INFO: Metaserver PID ${METASERVER_PID_FROM_FILE} from PID file not found running prior to kill."
        fi
    elif [ -n "${METASERVER_PID}" ] && ps -p ${METASERVER_PID} > /dev/null; then # Fallback to global var
        METASERVER_PID_TO_KILL=${METASERVER_PID}
        echo "WARN: Metaserver PID file missing, using global PID ${METASERVER_PID} for cleanup."
    fi

    if [ -n "${METASERVER_PID_TO_KILL}" ]; then
        echo "INFO: Attempting to kill Metaserver PID ${METASERVER_PID_TO_KILL}..."
        kill ${METASERVER_PID_TO_KILL}
        PIDS_TO_WAIT_ON+=(${METASERVER_PID_TO_KILL})
    else
        echo "INFO: Metaserver process not found or PID file missing, skipping kill."
    fi

    # Wait for all killed processes
    if [ ${#PIDS_TO_WAIT_ON[@]} -gt 0 ]; then
        echo "INFO: Waiting for PIDs: ${PIDS_TO_WAIT_ON[*]} to terminate..."
        for pid_to_wait in "${PIDS_TO_WAIT_ON[@]}"; do
            wait ${pid_to_wait} 2>/dev/null || echo "WARN: Process ${pid_to_wait} may not have terminated cleanly or was already dead."
        done
        echo "INFO: Finished waiting for PIDs."
    else
        echo "INFO: No PIDs were marked for waiting."
    fi

    echo "INFO: Attempting to unmount ${MOUNT_POINT}..."
    # Try fusermount3 first, then fusermount, then umount
    fusermount3 -u "${MOUNT_POINT}" > /dev/null 2>&1 || \
       fusermount -u "${MOUNT_POINT}" > /dev/null 2>&1 || \
       umount "${MOUNT_POINT}" > /dev/null 2>&1 || true

    # Remove PID files (log files removal can be separate or here)
    echo "INFO: Removing PID files..."
    rm -f ${BASE_TMP_DIR}/metaserver_wrapper.pid ${BASE_TMP_DIR}/fuse_adapter_wrapper.pid
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

    # Remove temporary directory with logs and mount point after logs are shown
    rm -rf "${BASE_TMP_DIR}"

    echo "INFO: Wrapper script finished. Exiting with status: ${EXIT_STATUS}"
    exit ${EXIT_STATUS}
}

# Start Metaserver
echo "INFO: Attempting to start Metaserver on port ${METASERVER_PORT}..."
${METASERVER_EXEC} ${METASERVER_PORT} > ${METASERVER_LOG} 2>&1 &
METASERVER_PID=$!
echo ${METASERVER_PID} > ${BASE_TMP_DIR}/metaserver_wrapper.pid
echo "INFO: Metaserver process initiated with PID: ${METASERVER_PID}"

echo "INFO: Waiting for Metaserver (PID: ${METASERVER_PID}) to be ready..."
MS_READY=0
# Max attempts for metaserver readiness
MS_MAX_ATTEMPTS=10
MS_SLEEP_INTERVAL=0.5

for i in $(seq 1 ${MS_MAX_ATTEMPTS}); do
    if ! ps -p ${METASERVER_PID} > /dev/null; then
        echo "ERROR: Metaserver process ${METASERVER_PID} died during startup."
        cleanup_and_exit 1
    fi

    # Attempt to connect using netcat as primary check
    if command -v nc >/dev/null && nc -z 127.0.0.1 ${METASERVER_PORT} >/dev/null 2>&1; then
        echo "INFO: Metaserver detected as ready via netcat on port ${METASERVER_PORT} (attempt $i/${MS_MAX_ATTEMPTS})."
        MS_READY=1
        break
    fi

    # Fallback: Check for a specific log message if nc failed or not available
    # Adjust "Metaserver listening on port" to the actual success message in your metaserver's log
    if grep -q "Metaserver listening on port" "${METASERVER_LOG}"; then
        echo "INFO: Metaserver detected as ready via log message in ${METASERVER_LOG} (attempt $i/${MS_MAX_ATTEMPTS})."
        MS_READY=1
        break
    fi

    echo "INFO: Metaserver not ready yet (attempt $i/${MS_MAX_ATTEMPTS}), waiting ${MS_SLEEP_INTERVAL}s..."
    sleep ${MS_SLEEP_INTERVAL}
done

if [ "$MS_READY" -eq 0 ]; then
    echo "ERROR: Metaserver failed to become ready on port ${METASERVER_PORT} after ${MS_MAX_ATTEMPTS} attempts."
    # Additional check to confirm if process is still running
    if ! ps -p ${METASERVER_PID} > /dev/null; then
        echo "ERROR: Metaserver process ${METASERVER_PID} is not running at timeout."
    else
        echo "INFO: Metaserver process ${METASERVER_PID} is running but did not signal readiness (port unresponsive and log message not found)."
    fi
    cleanup_and_exit 1
fi
echo "INFO: Metaserver started successfully. PID: ${METASERVER_PID}"

# Start Storage Nodes
echo "INFO: Starting Storage Nodes..."
for i in $(seq 1 $NUM_NODES)
do
    NODE_NAME="NodeWrapper$i"
    NODE_PORT=$((NODE_START_PORT + i))
    NODE_LOG_FILE="${BASE_TMP_DIR}/${NODE_NAME}.log"
    NODE_PID_FILE_PATH="${BASE_TMP_DIR}/${NODE_NAME}.pid"

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
echo "INFO: Attempting to start FUSE adapter..."
# The -f flag keeps fuse in the foreground for its own process, but we still background the script's execution of it.
${FUSE_ADAPTER_EXEC} 127.0.0.1 ${METASERVER_PORT} ${MOUNT_POINT} -f > ${FUSE_ADAPTER_STDOUT_LOG} 2>&1 &
FUSE_ADAPTER_PID=$!
echo ${FUSE_ADAPTER_PID} > ${BASE_TMP_DIR}/fuse_adapter_wrapper.pid
echo "INFO: FUSE adapter process initiated with PID: ${FUSE_ADAPTER_PID}"

echo "INFO: Waiting for FUSE adapter (PID: ${FUSE_ADAPTER_PID}) to mount ${MOUNT_POINT}..."
MOUNT_READY=0
# Max attempts for FUSE adapter readiness
FUSE_MAX_ATTEMPTS=10
FUSE_SLEEP_INTERVAL=1 # Increased sleep interval for FUSE mount

for i in $(seq 1 ${FUSE_MAX_ATTEMPTS}); do
    if ! ps -p ${FUSE_ADAPTER_PID} > /dev/null; then
        echo "ERROR: FUSE adapter process ${FUSE_ADAPTER_PID} died during startup or mount attempt."
        # Check common reason for FUSE failure
        if grep -q "Operation not permitted" "${FUSE_ADAPTER_STDOUT_LOG}" 2>/dev/null; then
            echo "SKIP: FUSE mount operation not permitted. Skipping test based on log."
            cleanup_and_exit 0 # Exit with 0 for skip
        fi
        cleanup_and_exit 1
    fi

    # Check if mount point is active
    if mount | grep -q "${MOUNT_POINT}"; then
        echo "INFO: FUSE adapter mounted ${MOUNT_POINT} successfully (attempt $i/${FUSE_MAX_ATTEMPTS})."
        MOUNT_READY=1
        break
    fi

    echo "INFO: Mount point ${MOUNT_POINT} not ready yet (attempt $i/${FUSE_MAX_ATTEMPTS}). FUSE PID ${FUSE_ADAPTER_PID} is running. Waiting ${FUSE_SLEEP_INTERVAL}s..."
    sleep ${FUSE_SLEEP_INTERVAL}
done

if [ "$MOUNT_READY" -eq 0 ]; then
    echo "ERROR: FUSE adapter failed to mount ${MOUNT_POINT} after ${FUSE_MAX_ATTEMPTS} attempts."
    if ! ps -p ${FUSE_ADAPTER_PID} > /dev/null; then
        echo "ERROR: FUSE adapter process ${FUSE_ADAPTER_PID} is not running at timeout."
    else
        echo "INFO: FUSE adapter process ${FUSE_ADAPTER_PID} is running but mount point '${MOUNT_POINT}' not detected."
    fi
    # Check common reason for FUSE failure again on timeout
    if grep -q "Operation not permitted" "${FUSE_ADAPTER_STDOUT_LOG}" 2>/dev/null; then
        echo "SKIP: FUSE mount operation not permitted. Skipping test based on log during timeout."
        cleanup_and_exit 0 # Exit with 0 for skip
    fi
    cleanup_and_exit 1
fi
echo "INFO: FUSE adapter started and mounted successfully. PID: ${FUSE_ADAPTER_PID}, Mount: ${MOUNT_POINT}"

# Run the actual test
echo "INFO: Running ${TEST_EXEC}..."
${TEST_EXEC}
TEST_EXIT_CODE=$?
echo "INFO: Test executable finished with exit code: ${TEST_EXIT_CODE}"

# Normal cleanup and exit
cleanup_and_exit ${TEST_EXIT_CODE}
