#!/bin/bash

# Create the mount point directory
mkdir -p /tmp/myfusemount

# Start the metaserver
# Assuming this script is run from build/tests/
echo "Starting Metaserver..."
../src/metaserver/simpli_metaserver 60000 > /tmp/metaserver.log 2>&1 &
echo $! > /tmp/metaserver.pid
METASERVER_PID=$!
echo "Metaserver PID: $METASERVER_PID"

# Start the FUSE adapter
# Assuming this script is run from build/tests/
echo "Starting FUSE adapter..."
../src/utilities/simpli_fuse_adapter 127.0.0.1 60000 /tmp/myfusemount -f > /tmp/fuse_adapter.log 2>&1 &
echo $! > /tmp/fuse_adapter.pid
FUSE_ADAPTER_PID=$!
echo "FUSE Adapter PID: $FUSE_ADAPTER_PID"

# Sleep to allow services to initialize
echo "Waiting for services to initialize..."
sleep 5

# Check if processes are running
if ! ps -p $METASERVER_PID > /dev/null; then
   echo "ERROR: Metaserver process $METASERVER_PID died."
   cat /tmp/metaserver.log
   exit 1
fi

if ! ps -p $FUSE_ADAPTER_PID > /dev/null; then
   echo "ERROR: FUSE adapter process $FUSE_ADAPTER_PID died."
   cat /tmp/fuse_adapter.log
   exit 1
fi

# Check if mount was successful
if ! mount | grep -q /tmp/myfusemount; then
    echo "ERROR: /tmp/myfusemount does not appear to be mounted."
    echo "FUSE adapter log:"
    cat /tmp/fuse_adapter.log
    # kill both processes if mount failed
    kill $FUSE_ADAPTER_PID
    kill $METASERVER_PID
    wait $FUSE_ADAPTER_PID 2>/dev/null
    wait $METASERVER_PID 2>/dev/null
    rm -f /tmp/fuse_adapter.pid /tmp/metaserver.pid
    exit 1
fi

echo "FUSE test environment setup complete."
