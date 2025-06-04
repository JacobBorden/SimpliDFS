#!/bin/bash

echo "INFO: setup_fuse_test_env.sh starting..."

# Skip FUSE-dependent tests if the fuse device is not available. This allows
# the rest of the test suite to run in environments where the FUSE kernel
# module isn't loaded (e.g. CI containers).
if [ ! -e /dev/fuse ]; then
    echo "SKIP: /dev/fuse not found, skipping FUSE tests."
    exit 0
fi

NUM_NODES=3
NODE_START_PORT=50000

# Create the mount point directory
mkdir -p /tmp/myfusemount
echo "INFO: Mount point /tmp/myfusemount ensured."

# Start the metaserver
echo "INFO: Starting Metaserver..."
nohup ../metaserver 50505 > /tmp/metaserver.log 2>&1 < /dev/null &
METASERVER_PID=$!
echo $! > /tmp/metaserver.pid
echo "INFO: Metaserver PID: $METASERVER_PID"

# Start the FUSE adapter
echo "INFO: Starting FUSE adapter (simpli_fuse_adapter)..."
nohup ../simpli_fuse_adapter 127.0.0.1 50505 /tmp/myfusemount -f > /tmp/fuse_adapter.log 2>&1 < /dev/null &
FUSE_ADAPTER_PID=$!
echo $! > /tmp/fuse_adapter.pid
echo "INFO: FUSE Adapter PID: $FUSE_ADAPTER_PID"

# Start Storage Nodes
for i in $(seq 1 $NUM_NODES)
do
    NODE_NAME="Node$i"
    NODE_PORT=$((NODE_START_PORT + i))
    echo "INFO: Starting $NODE_NAME on port $NODE_PORT..."
    nohup ../node $NODE_NAME $NODE_PORT > /tmp/$NODE_NAME.log 2>&1 < /dev/null &
    NODE_PID=$!
    echo $NODE_PID > /tmp/$NODE_NAME.pid
    # Store PIDs in an array for later checks and cleanup
    NODE_PIDS[$i]=$NODE_PID
    echo "INFO: $NODE_NAME PID: $NODE_PID"
done

echo "INFO: Waiting for services to initialize (7 seconds)..."
sleep 7

echo "--- Metaserver Log (/tmp/metaserver.log) ---"
cat /tmp/metaserver.log || echo "Metaserver log not found or cat failed"
echo "--------------------------------------------"
echo "--- FUSE Adapter Log (stdout/stderr) (/tmp/fuse_adapter.log) ---"
cat /tmp/fuse_adapter.log || echo "FUSE adapter stdout/stderr log not found or cat failed"
echo "-----------------------------------------------------------------"
echo "--- FUSE Adapter Main Log (/tmp/fuse_adapter_main.log) ---"
if [ -f /tmp/fuse_adapter_main.log ]; then
    cat /tmp/fuse_adapter_main.log
else
    echo "/tmp/fuse_adapter_main.log not found."
fi
echo "-------------------------------------------------------------------"

# Display Node Logs
for i in $(seq 1 $NUM_NODES)
do
    NODE_NAME="Node$i"
    echo "--- $NODE_NAME Log (/tmp/$NODE_NAME.log) ---"
    cat /tmp/$NODE_NAME.log || echo "$NODE_NAME log not found or cat failed"
    echo "--------------------------------------------"
done

FINAL_STATUS=0

# Check if processes are running
if ! ps -p $METASERVER_PID > /dev/null; then
   echo "ERROR: Metaserver process $METASERVER_PID died."
   FINAL_STATUS=1
fi

FUSE_ADAPTER_RUNNING=true
if ! ps -p $FUSE_ADAPTER_PID > /dev/null; then
   echo "ERROR: FUSE adapter process $FUSE_ADAPTER_PID died."
   FUSE_ADAPTER_RUNNING=false
   FINAL_STATUS=1
fi

# Check Node Processes
for i in $(seq 1 $NUM_NODES)
do
    NODE_NAME="Node$i"
    NODE_PID_FILE="/tmp/$NODE_NAME.pid"
    if [ -f "$NODE_PID_FILE" ]; then
        NODE_PID_TO_CHECK=$(cat $NODE_PID_FILE)
        if ! ps -p $NODE_PID_TO_CHECK > /dev/null; then
            echo "ERROR: $NODE_NAME process $NODE_PID_TO_CHECK died."
            FINAL_STATUS=1
        else
            echo "INFO: $NODE_NAME process $NODE_PID_TO_CHECK is running."
        fi
    else
        echo "ERROR: PID file $NODE_PID_FILE for $NODE_NAME not found."
        FINAL_STATUS=1
    fi
done

MOUNT_SUCCESSFUL=false
if mount | grep -q /tmp/myfusemount; then
    MOUNT_SUCCESSFUL=true
    echo "INFO: Mount check: /tmp/myfusemount is mounted."
else
    echo "ERROR: Mount check: /tmp/myfusemount does not appear to be mounted."
    FINAL_STATUS=1
fi

if [ "$FINAL_STATUS" -eq 0 ]; then
    echo "INFO: Initial checks passed. Processes running and mount point verified. Sleeping for 2 more seconds..."
    sleep 2
    # Re-verify mount after short sleep, in case adapter died in this window
    if ! mount | grep -q /tmp/myfusemount; then
        echo "ERROR: Mount check AFTER SLEEP: /tmp/myfusemount is NO LONGER mounted."
        FINAL_STATUS=1
        if ps -p $FUSE_ADAPTER_PID > /dev/null; then # Check if FUSE_ADAPTER_PID is valid before killing
            echo "INFO: FUSE adapter was still running but mount disappeared. Killing adapter."
            kill $FUSE_ADAPTER_PID
            wait $FUSE_ADAPTER_PID 2>/dev/null || true
        else
            echo "INFO: FUSE adapter already died before post-sleep mount check."
        fi
    else
        echo "INFO: Mount check AFTER SLEEP: /tmp/myfusemount is STILL mounted."
    fi
fi


if [ "$FINAL_STATUS" -ne 0 ]; then
    echo "ERROR: Setup script determined a failure. Cleaning up started processes (if any)."
    if ps -p $FUSE_ADAPTER_PID > /dev/null; then kill $FUSE_ADAPTER_PID; wait $FUSE_ADAPTER_PID 2>/dev/null || true; fi
    if ps -p $METASERVER_PID > /dev/null; then kill $METASERVER_PID; wait $METASERVER_PID 2>/dev/null || true; fi
    for i in $(seq 1 $NUM_NODES)
    do
        NODE_NAME="Node$i"
        NODE_PID_FILE="/tmp/$NODE_NAME.pid"
        if [ -f "$NODE_PID_FILE" ]; then
            NODE_PID_TO_KILL=$(cat $NODE_PID_FILE)
            if ps -p $NODE_PID_TO_KILL > /dev/null; then
                echo "INFO: Killing $NODE_NAME process $NODE_PID_TO_KILL."
                kill $NODE_PID_TO_KILL; wait $NODE_PID_TO_KILL 2>/dev/null || true
            fi
            rm -f $NODE_PID_FILE
        fi
    done
    rm -f /tmp/fuse_adapter.pid /tmp/metaserver.pid
fi

echo "INFO: FUSE test environment setup script finished. Final Status (0=ok, 1=fail): $FINAL_STATUS"

# Attempt to cat the FUSE adapter main log one last time, regardless of status
echo "--- Final check: FUSE Adapter Main Log (/tmp/fuse_adapter_main.log) ---"
if [ -f /tmp/fuse_adapter_main.log ]; then
    cat /tmp/fuse_adapter_main.log
else
    echo "/tmp/fuse_adapter_main.log not found at the end of script."
fi
echo "-------------------------------------------------------------------"


if [ "$FINAL_STATUS" -ne 0 ]; then
    false
else
    true
fi
